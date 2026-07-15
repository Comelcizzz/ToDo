#include "mastering/dsp/DynamicEq.h"
#include "mastering/dsp/ProcessorChain.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace mastering::dsp {
namespace {

SvfFilter::Type toSvf(DynamicEqFilterType t) noexcept
{
    switch (t) {
    case DynamicEqFilterType::lowShelf:
        return SvfFilter::Type::lowShelf;
    case DynamicEqFilterType::highShelf:
        return SvfFilter::Type::highShelf;
    case DynamicEqFilterType::bell:
    default:
        return SvfFilter::Type::bell;
    }
}

} // namespace

double DynamicEqProcessor::computeGrDb(
    double levelDb,
    double thresholdDb,
    double ratio,
    double maxCut) noexcept
{
    ratio = std::max(1.0, ratio);
    maxCut = std::max(0.0, maxCut);
    const auto overshoot = std::max(0.0, levelDb - thresholdDb);
    // Soft knee near threshold: quadratic blend over 3 dB for C0 continuity.
    constexpr double knee = 3.0;
    double effective = overshoot;
    if (overshoot > 0.0 && overshoot < knee) {
        const auto t = overshoot / knee;
        effective = overshoot * t; // smooth start from 0
    }
    const auto gr = effective * (1.0 - 1.0 / ratio);
    return std::min(maxCut, std::max(0.0, gr));
}

void DynamicEqProcessor::prepare(double sampleRate, int maxBlockSize, int channels) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    maxBlock_ = std::max(1, maxBlockSize);
    channels_ = std::clamp(channels, 1, 2);

    wetSm_.prepare(sampleRate_, 20.0);
    outSm_.prepare(sampleRate_, 20.0);
    bypassSm_.prepare(sampleRate_, 15.0);

    for (int b = 0; b < kMaxDynamicEqBands; ++b) {
        auto& band = bands_[static_cast<std::size_t>(b)];
        band.detector.prepare(sampleRate_, maxBlock_, channels_);
        band.detectorR.prepare(sampleRate_, maxBlock_, 1);
        band.freqSm_.prepare(sampleRate_, 15.0);
        band.qSm_.prepare(sampleRate_, 15.0);
        band.staticGainSm_.prepare(sampleRate_, 15.0);
        band.thresholdSm_.prepare(sampleRate_, 15.0);
        band.grSmL_.prepare(sampleRate_, 5.0);
        band.grSmR_.prepare(sampleRate_, 5.0);
        for (auto& f : band.filter)
            f.reset();
    }

    for (int ch = 0; ch < 2; ++ch)
        dryScratch_[static_cast<std::size_t>(ch)].assign(
            static_cast<std::size_t>(maxBlock_), 0.0f);

    setState(state_);
    reset();
}

void DynamicEqProcessor::reset() noexcept
{
    for (int b = 0; b < kMaxDynamicEqBands; ++b) {
        auto& band = bands_[static_cast<std::size_t>(b)];
        band.detector.reset();
        band.detectorR.reset();
        for (auto& f : band.filter)
            f.reset();
        band.grSum = 0.0;
        band.grCount = 0;
        refreshBandSmoothingTargets(b);
        band.grSmL_.reset(0.0);
        band.grSmR_.reset(0.0);
    }
    wetSm_.reset(std::clamp(state_.wetDry, 0.0, 1.0));
    outSm_.reset(dbToGainSafe(state_.outputGainDb));
    bypassSm_.reset(state_.bypass ? 0.0 : 1.0);
    clearMeters();
}

void DynamicEqProcessor::clearMeters() noexcept
{
    meters_ = {};
    for (int b = 0; b < state_.bandCount; ++b)
        meters_.bands[static_cast<std::size_t>(b)].thresholdDb =
            state_.bands[static_cast<std::size_t>(b)].thresholdDb;
}

void DynamicEqProcessor::refreshBandSmoothingTargets(int bandIndex) noexcept
{
    auto& band = bands_[static_cast<std::size_t>(bandIndex)];
    const auto& s = band.settings;
    band.freqSm_.setTarget(s.frequencyHz);
    band.qSm_.setTarget(s.q);
    band.staticGainSm_.setTarget(s.staticGainDb);
    band.thresholdSm_.setTarget(s.thresholdDb);

    EnvelopeDetectorSettings det;
    det.mode = s.detectorMode;
    det.source = s.detectorSource;
    det.attackMs = s.attackMs;
    det.releaseMs = s.releaseMs;
    det.bandPassEnabled = s.detectorBandPass;
    det.bandPassHz = s.detectorFrequencyHz;
    det.bandPassQ = s.detectorQ;
    det.channelMode = DetectorChannelMode::stereoMax;
    band.detector.setSettings(det);

    EnvelopeDetectorSettings detR = det;
    detR.channelMode = DetectorChannelMode::stereoMax;
    band.detectorR.setSettings(detR);

    // Match GR smoother time constants to attack/release roughly.
    band.grSmL_.setTimeMs(std::max(1.0, s.attackMs));
    band.grSmR_.setTimeMs(std::max(1.0, s.attackMs));
}

void DynamicEqProcessor::setState(const DynamicEqState& state) noexcept
{
    state_ = state;
    state_.schemaVersion = kDynamicEqSchemaVersion;
    state_.bandCount = std::clamp(state_.bandCount, 1, kMaxDynamicEqBands);
    state_.wetDry = std::clamp(state_.wetDry, 0.0, 1.0);

    for (int b = 0; b < kMaxDynamicEqBands; ++b) {
        auto& bs = state_.bands[static_cast<std::size_t>(b)];
        if (bs.bandId == 0)
            bs.bandId = static_cast<std::uint32_t>(b + 1);
        bs.frequencyHz = std::clamp(bs.frequencyHz, 20.0, 20'000.0);
        bs.q = std::clamp(bs.q, 0.1, 40.0);
        bs.staticGainDb = std::clamp(bs.staticGainDb, -24.0, 24.0);
        bs.ratio = std::max(1.0, bs.ratio);
        bs.maxCutDb = std::clamp(bs.maxCutDb, 0.0, 24.0);
        bs.detectorFrequencyHz = std::clamp(bs.detectorFrequencyHz, 20.0, 20'000.0);
        bs.detectorQ = std::clamp(bs.detectorQ, 0.1, 40.0);
        bands_[static_cast<std::size_t>(b)].settings = bs;
        refreshBandSmoothingTargets(b);
        // Snap continuous smoothers on structural setState for deterministic restore.
        bands_[static_cast<std::size_t>(b)].freqSm_.reset(bs.frequencyHz);
        bands_[static_cast<std::size_t>(b)].qSm_.reset(bs.q);
        bands_[static_cast<std::size_t>(b)].staticGainSm_.reset(bs.staticGainDb);
        bands_[static_cast<std::size_t>(b)].thresholdSm_.reset(bs.thresholdDb);
        bands_[static_cast<std::size_t>(b)].grSmL_.reset(0.0);
        bands_[static_cast<std::size_t>(b)].grSmR_.reset(0.0);
    }

    wetSm_.setTarget(state_.wetDry);
    outSm_.setTarget(dbToGainSafe(state_.outputGainDb));
    bypassSm_.setTarget(state_.bypass ? 0.0 : 1.0);
    wetSm_.reset(state_.wetDry);
    outSm_.reset(dbToGainSafe(state_.outputGainDb));
    bypassSm_.reset(state_.bypass ? 0.0 : 1.0);
    clearMeters();
}

void DynamicEqProcessor::setContinuousParameters(const DynamicEqState& state) noexcept
{
    // Do not change bandCount here (structural).
    state_.bypass = state.bypass;
    state_.wetDry = std::clamp(state.wetDry, 0.0, 1.0);
    state_.outputGainDb = state.outputGainDb;
    wetSm_.setTarget(state_.wetDry);
    outSm_.setTarget(dbToGainSafe(state_.outputGainDb));
    bypassSm_.setTarget(state_.bypass ? 0.0 : 1.0);

    const int n = std::min(state_.bandCount, state.bandCount);
    for (int b = 0; b < n; ++b) {
        auto incoming = state.bands[static_cast<std::size_t>(b)];
        incoming.bandId = state_.bands[static_cast<std::size_t>(b)].bandId;
        incoming.frequencyHz = std::clamp(incoming.frequencyHz, 20.0, 20'000.0);
        incoming.q = std::clamp(incoming.q, 0.1, 40.0);
        incoming.staticGainDb = std::clamp(incoming.staticGainDb, -24.0, 24.0);
        incoming.ratio = std::max(1.0, incoming.ratio);
        incoming.maxCutDb = std::clamp(incoming.maxCutDb, 0.0, 24.0);
        state_.bands[static_cast<std::size_t>(b)] = incoming;
        bands_[static_cast<std::size_t>(b)].settings = incoming;
        refreshBandSmoothingTargets(b);
    }
}

void DynamicEqProcessor::processChunk(
    float* const* main,
    const float* const* sidechain,
    int channelCount,
    int sampleCount) noexcept
{
    for (int i = 0; i < sampleCount; ++i) {
        for (int ch = 0; ch < channelCount; ++ch)
            dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = main[ch][i];

        const auto wetAmt = bypassSm_.next();
        const auto mix = wetSm_.next();
        const auto outG = outSm_.next();

        for (int b = 0; b < state_.bandCount; ++b) {
            auto& band = bands_[static_cast<std::size_t>(b)];
            auto& meters = meters_.bands[static_cast<std::size_t>(b)];
            if (!band.settings.enabled)
                continue;

            const auto freq = band.freqSm_.next();
            const auto q = band.qSm_.next();
            const auto staticG = band.staticGainSm_.next();
            const auto thr = band.thresholdSm_.next();

            double grL = 0.0;
            double grR = 0.0;

            // Detect from dry (pre-EQ) for internal source — never from filtered output (no feedback).
            const float* dryPtrs[2] {
                dryScratch_[0].data(),
                dryScratch_[1].data()
            };
            const float* const* detMain = dryPtrs;
            const float* const* detSc = sidechain;
            if (band.settings.detectorSource == DetectorSource::external)
                detMain = dryPtrs; // main unused when external; detector reads sidechain
            else
                detSc = nullptr;

            if (band.settings.stereoMode == DynamicEqStereoMode::linked
                || channelCount == 1) {
                const auto env = band.detector.processSample(detMain, detSc, channelCount, i);
                const auto levelDb = gainToDb(std::max(1.0e-6, env));
                const auto gr = computeGrDb(levelDb, thr, band.settings.ratio, band.settings.maxCutDb);
                band.grSmL_.setTarget(gr);
                band.grSmR_.setTarget(gr);
                grL = band.grSmL_.next();
                grR = band.grSmR_.next();
                meters.detectorLevelDb = levelDb;
            } else {
                const float* lMain[1] {dryPtrs[0]};
                const float* rMain[1] {dryPtrs[1]};
                const float* lSc[1] {};
                const float* rSc[1] {};
                const float* const* scL = nullptr;
                const float* const* scR = nullptr;
                if (sidechain != nullptr) {
                    lSc[0] = sidechain[0];
                    rSc[0] = sidechain[std::min(1, channelCount - 1)];
                    scL = lSc;
                    scR = rSc;
                }
                const auto envL = band.detector.processSample(lMain, scL, 1, i);
                const auto envR = band.detectorR.processSample(rMain, scR, 1, i);
                const auto grRawL =
                    computeGrDb(gainToDb(std::max(1.0e-6, envL)), thr, band.settings.ratio, band.settings.maxCutDb);
                const auto grRawR =
                    computeGrDb(gainToDb(std::max(1.0e-6, envR)), thr, band.settings.ratio, band.settings.maxCutDb);
                band.grSmL_.setTarget(grRawL);
                band.grSmR_.setTarget(grRawR);
                grL = band.grSmL_.next();
                grR = band.grSmR_.next();
                meters.detectorLevelDb = gainToDb(std::max(1.0e-6, std::max(envL, envR)));
            }

            meters.thresholdDb = thr;
            meters.currentGrDb = std::max(grL, grR);
            meters.maxGrDb = std::max(meters.maxGrDb, meters.currentGrDb);
            band.grSum += meters.currentGrDb;
            ++band.grCount;
            meters.avgGrDb = band.grCount > 0 ? band.grSum / static_cast<double>(band.grCount) : 0.0;
            if (meters.currentGrDb > 0.05)
                ++meters.activeSamples;
            meters.grHistoryDb[static_cast<std::size_t>(meters.grHistoryWrite)] =
                static_cast<float>(meters.currentGrDb);
            meters.grHistoryWrite = (meters.grHistoryWrite + 1) % DynamicEqBandMeters::kHistory;

            for (int ch = 0; ch < channelCount; ++ch) {
                const auto gr = (ch == 0) ? grL : grR;
                const auto filterGainDb = staticG - gr;
                auto& filt = band.filter[static_cast<std::size_t>(ch)];
                filt.setType(toSvf(band.settings.filterType));
                filt.setParams(sampleRate_, freq, q, filterGainDb);
                auto x = static_cast<double>(main[ch][i]);
                if (!std::isfinite(x)) {
                    x = 0.0;
                    meters_.degraded = true;
                }
                x = filt.process(x);
                if (!std::isfinite(x)) {
                    x = 0.0;
                    meters_.degraded = true;
                }
                main[ch][i] = static_cast<float>(x);
            }
        }

        for (int ch = 0; ch < channelCount; ++ch) {
            const auto dry = dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
            auto wet = static_cast<double>(main[ch][i]) * outG;
            auto mixed = dry + mix * (wet - dry);
            auto out = dry + wetAmt * (mixed - dry);
            if (!std::isfinite(out)) {
                out = 0.0;
                meters_.degraded = true;
            }
            main[ch][i] = static_cast<float>(out);
        }
    }
}

void DynamicEqProcessor::process(
    float* const* main,
    const float* const* sidechain,
    int channelCount,
    int sampleCount) noexcept
{
    if (main == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channels_);

    for (int offset = 0; offset < sampleCount;) {
        const int n = std::min(maxBlock_, sampleCount - offset);
        float* slice[2] {};
        for (int ch = 0; ch < channelCount; ++ch)
            slice[ch] = main[ch] + offset;
        const float* scSlice[2] {};
        const float* const* sc = nullptr;
        if (sidechain != nullptr) {
            for (int ch = 0; ch < channelCount; ++ch)
                scSlice[ch] = sidechain[ch] + offset;
            sc = scSlice;
        }
        processChunk(slice, sc, channelCount, n);
        offset += n;
    }
}

std::string DynamicEqProcessor::toJson() const
{
    nlohmann::json j;
    j["schemaVersion"] = state_.schemaVersion;
    j["processorRevision"] = state_.processorRevision;
    j["bypass"] = state_.bypass;
    j["wetDry"] = state_.wetDry;
    j["outputGainDb"] = state_.outputGainDb;
    j["bandCount"] = state_.bandCount;
    j["bands"] = nlohmann::json::array();
    for (int b = 0; b < state_.bandCount; ++b) {
        const auto& band = state_.bands[static_cast<std::size_t>(b)];
        j["bands"].push_back({
            {"bandId", band.bandId},
            {"enabled", band.enabled},
            {"filterType", static_cast<int>(band.filterType)},
            {"frequencyHz", band.frequencyHz},
            {"q", band.q},
            {"staticGainDb", band.staticGainDb},
            {"thresholdDb", band.thresholdDb},
            {"ratio", band.ratio},
            {"maxCutDb", band.maxCutDb},
            {"attackMs", band.attackMs},
            {"releaseMs", band.releaseMs},
            {"detectorBandPass", band.detectorBandPass},
            {"detectorFrequencyHz", band.detectorFrequencyHz},
            {"detectorQ", band.detectorQ},
            {"detectorSource", static_cast<int>(band.detectorSource)},
            {"detectorMode", static_cast<int>(band.detectorMode)},
            {"stereoMode", static_cast<int>(band.stereoMode)},
            {"targetTrackId", band.targetTrackId},
            {"sidechainSourceId", band.sidechainSourceId},
        });
    }
    return j.dump(2);
}

void DynamicEqProcessor::fromJson(const std::string& json)
{
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded())
        return;
    DynamicEqState s;
    s.schemaVersion = j.value("schemaVersion", kDynamicEqSchemaVersion);
    s.processorRevision = j.value("processorRevision", 1u);
    s.bypass = j.value("bypass", false);
    s.wetDry = j.value("wetDry", 1.0);
    s.outputGainDb = j.value("outputGainDb", 0.0);
    s.bandCount = j.value("bandCount", 1);
    if (j.contains("bands") && j["bands"].is_array()) {
        int i = 0;
        for (const auto& bj : j["bands"]) {
            if (i >= kMaxDynamicEqBands)
                break;
            auto& band = s.bands[static_cast<std::size_t>(i)];
            band.bandId = bj.value("bandId", static_cast<std::uint32_t>(i + 1));
            band.enabled = bj.value("enabled", true);
            band.filterType = static_cast<DynamicEqFilterType>(bj.value("filterType", 0));
            band.frequencyHz = bj.value("frequencyHz", 1'000.0);
            band.q = bj.value("q", 1.0);
            band.staticGainDb = bj.value("staticGainDb", 0.0);
            band.thresholdDb = bj.value("thresholdDb", -24.0);
            band.ratio = bj.value("ratio", 2.0);
            band.maxCutDb = bj.value("maxCutDb", 12.0);
            band.attackMs = bj.value("attackMs", 10.0);
            band.releaseMs = bj.value("releaseMs", 100.0);
            band.detectorBandPass = bj.value("detectorBandPass", true);
            band.detectorFrequencyHz = bj.value("detectorFrequencyHz", band.frequencyHz);
            band.detectorQ = bj.value("detectorQ", 1.0);
            band.detectorSource = static_cast<DetectorSource>(bj.value("detectorSource", 0));
            band.detectorMode = static_cast<DetectorMode>(bj.value("detectorMode", 1));
            band.stereoMode = static_cast<DynamicEqStereoMode>(bj.value("stereoMode", 0));
            band.targetTrackId = bj.value("targetTrackId", std::string {});
            band.sidechainSourceId = bj.value("sidechainSourceId", std::string {});
            ++i;
        }
        s.bandCount = std::max(1, i);
    }
    setState(s);
}

void FrequencyDependentSidechain::prepare(double sampleRate, int maxBlock, int channels) noexcept
{
    eq_.prepare(sampleRate, maxBlock, channels);
    syncState();
    reset();
}

void FrequencyDependentSidechain::reset() noexcept
{
    eq_.reset();
}

void FrequencyDependentSidechain::setSettings(const FrequencyDependentSidechainSettings& settings) noexcept
{
    settings_ = settings;
    syncState();
}

void FrequencyDependentSidechain::syncState() noexcept
{
    DynamicEqState state;
    state.bypass = settings_.bypass;
    state.bandCount = 1;
    state.wetDry = 1.0;
    state.outputGainDb = 0.0;
    auto& band = state.bands[0];
    band.bandId = 1;
    band.enabled = true;
    band.filterType = settings_.targetType;
    band.frequencyHz = settings_.targetFrequencyHz;
    band.q = settings_.targetQ;
    band.staticGainDb = 0.0;
    band.thresholdDb = settings_.thresholdDb;
    band.ratio = settings_.ratio;
    band.maxCutDb = settings_.maxCutDb;
    band.attackMs = settings_.attackMs;
    band.releaseMs = settings_.releaseMs;
    band.detectorBandPass = true;
    band.detectorFrequencyHz = settings_.detectorFrequencyHz;
    band.detectorQ = settings_.detectorQ;
    band.detectorSource = DetectorSource::external;
    band.detectorMode = settings_.detectorMode;
    band.stereoMode = settings_.stereoMode;
    eq_.setState(state);
}

void FrequencyDependentSidechain::process(
    float* const* target,
    const float* const* sidechain,
    int channelCount,
    int sampleCount) noexcept
{
    eq_.process(target, sidechain, channelCount, sampleCount);
}

} // namespace mastering::dsp
