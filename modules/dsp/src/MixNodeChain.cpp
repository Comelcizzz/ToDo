#include "mastering/dsp/MixNodeChain.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace mastering::dsp {
namespace {

constexpr double kSilenceFloor = 1.0e-12;

float peakOf(const float* x, int n) noexcept
{
    float p = 0.0f;
    for (int i = 0; i < n; ++i)
        p = std::max(p, std::abs(x[i]));
    return p;
}

} // namespace

void MixNodeChain::prepare(double sampleRate, int maxBlockSize, int channels) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    maxBlock_ = std::max(1, maxBlockSize);
    channels_ = std::clamp(channels, 1, 2);

    inputGainSm_.prepare(sampleRate_, 15.0);
    outputGainSm_.prepare(sampleRate_, 15.0);
    bypassSm_.prepare(sampleRate_, 8.0);
    staticFreqSm_.prepare(sampleRate_, 20.0);
    staticGainSm_.prepare(sampleRate_, 20.0);
    staticQSm_.prepare(sampleRate_, 20.0);

    for (auto& f : staticEqFilters_)
        f.reset();

    if (state_.dynamicEq.bandCount < 1)
        state_.dynamicEq.bandCount = 1;
    dynamicEq_.prepare(sampleRate_, maxBlock_, channels_);
    dynamicEq_.setState(state_.dynamicEq);

    pendingOsFactor_ = std::max(1, state_.saturation.oversamplingFactor);
    applySaturationPrepareIfNeeded();

    for (int c = 0; c < 2; ++c)
        dryScratch_[static_cast<std::size_t>(c)].assign(static_cast<std::size_t>(maxBlock_), 0.0f);

    inputGainSm_.reset(dbToGainSafe(state_.inputGainDb));
    outputGainSm_.reset(dbToGainSafe(state_.outputGainDb));
    bypassSm_.reset(state_.bypass ? 1.0 : 0.0);
    staticFreqSm_.reset(state_.staticEq.frequencyHz);
    staticGainSm_.reset(state_.staticEq.gainDb);
    staticQSm_.reset(state_.staticEq.q);
    clearMeters();
}

void MixNodeChain::reset() noexcept
{
    for (auto& f : staticEqFilters_)
        f.reset();
    dynamicEq_.reset();
    saturation_.reset();
    clearMeters();
}

void MixNodeChain::applySaturationPrepareIfNeeded() noexcept
{
    const int factor = state_.saturationEnabled
        ? std::max(1, pendingOsFactor_)
        : 1;
    saturation_.prepare(sampleRate_, maxBlock_, channels_, factor);
    auto sat = state_.saturation;
    sat.bypass = !state_.saturationEnabled || sat.bypass;
    sat.oversamplingFactor = factor;
    saturation_.setSettings(sat);
}

void MixNodeChain::setState(const MixNodeChainState& state) noexcept
{
    state_ = state;
    if (state_.schemaVersion <= 0)
        state_.schemaVersion = kMixNodeChainSchemaVersion;
    state_.dynamicEq.bandCount = std::clamp(state_.dynamicEq.bandCount, 1, kMaxDynamicEqBands);
    pendingOsFactor_ = std::max(1, state_.saturation.oversamplingFactor);

    dynamicEq_.setState(state_.dynamicEq);
    applySaturationPrepareIfNeeded();

    inputGainSm_.setTarget(dbToGainSafe(state_.inputGainDb));
    outputGainSm_.setTarget(dbToGainSafe(state_.outputGainDb));
    bypassSm_.setTarget(state_.bypass ? 1.0 : 0.0);
    staticFreqSm_.setTarget(state_.staticEq.frequencyHz);
    staticGainSm_.setTarget(state_.staticEq.gainDb);
    staticQSm_.setTarget(state_.staticEq.q);
}

void MixNodeChain::setContinuousParameters(const MixNodeChainState& state) noexcept
{
    // Preserve structural fields from current state.
    const auto bandCount = state_.dynamicEq.bandCount;
    const auto os = state_.saturation.oversamplingFactor;
    const auto rev = state_.revision;
    state_ = state;
    state_.dynamicEq.bandCount = bandCount;
    state_.saturation.oversamplingFactor = os;
    state_.revision = rev;
    pendingOsFactor_ = std::max(1, os);

    dynamicEq_.setContinuousParameters(state_.dynamicEq);
    auto sat = state_.saturation;
    sat.bypass = !state_.saturationEnabled || sat.bypass;
    saturation_.setSettings(sat);

    inputGainSm_.setTarget(dbToGainSafe(state_.inputGainDb));
    outputGainSm_.setTarget(dbToGainSafe(state_.outputGainDb));
    bypassSm_.setTarget(state_.bypass ? 1.0 : 0.0);
    staticFreqSm_.setTarget(state_.staticEq.frequencyHz);
    staticGainSm_.setTarget(state_.staticEq.gainDb);
    staticQSm_.setTarget(state_.staticEq.q);
}

int MixNodeChain::latencySamples() const noexcept
{
    if (!state_.saturationEnabled || state_.saturation.bypass)
        return 0;
    return saturation_.latencySamples();
}

void MixNodeChain::clearMeters() noexcept
{
    meters_ = {};
}

void MixNodeChain::processGain(
    float* const* channels,
    int channelCount,
    int sampleCount,
    ParameterSmoother& smoother) noexcept
{
    for (int i = 0; i < sampleCount; ++i) {
        const auto g = static_cast<float>(smoother.next());
        for (int c = 0; c < channelCount; ++c)
            channels[c][i] *= g;
    }
}

void MixNodeChain::processStaticEq(
    float* const* channels,
    int channelCount,
    int sampleCount) noexcept
{
    if (!state_.staticEq.enabled || state_.staticEq.bypass)
        return;

    for (int i = 0; i < sampleCount; ++i) {
        const auto freq = staticFreqSm_.next();
        const auto gain = staticGainSm_.next();
        const auto q = staticQSm_.next();
        for (int c = 0; c < channelCount; ++c) {
            auto& f = staticEqFilters_[static_cast<std::size_t>(c)];
            f.setType(SvfFilter::Type::bell);
            f.setParams(sampleRate_, freq, q, gain);
            channels[c][i] = static_cast<float>(f.process(channels[c][i]));
        }
    }
}

void MixNodeChain::process(
    float* const* main,
    const float* const* sidechain,
    int channelCount,
    int sampleCount) noexcept
{
    if (main == nullptr || channelCount <= 0 || sampleCount <= 0)
        return;

    channelCount = std::min(channelCount, channels_);

    // Chunk oversized blocks (never silent bypass).
    int offset = 0;
    while (offset < sampleCount) {
        const int n = std::min(maxBlock_, sampleCount - offset);
        float* chunkMain[2] {};
        const float* chunkSc[2] {};
        for (int c = 0; c < channelCount; ++c)
            chunkMain[c] = main[c] + offset;
        if (sidechain != nullptr) {
            for (int c = 0; c < channelCount; ++c)
                chunkSc[c] = sidechain[c] != nullptr ? sidechain[c] + offset : nullptr;
        }

        // Capture dry for timing-aligned bypass (buffers sized in prepare).
        for (int c = 0; c < channelCount; ++c) {
            auto& dry = dryScratch_[static_cast<std::size_t>(c)];
            std::copy_n(chunkMain[c], n, dry.data());
        }

        meters_.inputPeakL = std::max(meters_.inputPeakL, peakOf(chunkMain[0], n));
        if (channelCount > 1)
            meters_.inputPeakR = std::max(meters_.inputPeakR, peakOf(chunkMain[1], n));
        if (sidechain != nullptr && chunkSc[0] != nullptr)
            meters_.sidechainPeak = std::max(meters_.sidechainPeak, peakOf(chunkSc[0], n));

        processGain(chunkMain, channelCount, n, inputGainSm_);
        processStaticEq(chunkMain, channelCount, n);

        const float* const* scPtr = (sidechain != nullptr) ? chunkSc : nullptr;
        dynamicEq_.process(chunkMain, scPtr, channelCount, n);

        if (state_.saturationEnabled)
            saturation_.process(chunkMain, channelCount, n);

        processGain(chunkMain, channelCount, n, outputGainSm_);

        // Crossfade toward delayed-aligned dry when bypassing. Saturation already
        // delay-compensates internally; for zero-latency chain, dry is same-time.
        for (int i = 0; i < n; ++i) {
            const auto bypass = static_cast<float>(bypassSm_.next());
            for (int c = 0; c < channelCount; ++c) {
                const auto dry = dryScratch_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
                auto& wet = chunkMain[c][i];
                if (!std::isfinite(wet))
                    wet = 0.0f;
                wet = wet * (1.0f - bypass) + dry * bypass;
            }
        }

        meters_.outputPeakL = std::max(meters_.outputPeakL, peakOf(chunkMain[0], n));
        if (channelCount > 1)
            meters_.outputPeakR = std::max(meters_.outputPeakR, peakOf(chunkMain[1], n));

        offset += n;
    }

    meters_.dynamicEq = dynamicEq_.meters();
    (void) kSilenceFloor;
}

std::string MixNodeChain::toJson() const
{
    nlohmann::json j;
    j["schemaVersion"] = state_.schemaVersion;
    j["revision"] = state_.revision;
    j["bypass"] = state_.bypass;
    j["inputGainDb"] = state_.inputGainDb;
    j["outputGainDb"] = state_.outputGainDb;
    j["staticEq"] = {
        {"enabled", state_.staticEq.enabled},
        {"bypass", state_.staticEq.bypass},
        {"frequencyHz", state_.staticEq.frequencyHz},
        {"gainDb", state_.staticEq.gainDb},
        {"q", state_.staticEq.q}
    };
    j["dynamicEq"] = nlohmann::json::parse(dynamicEq_.toJson());
    j["saturationEnabled"] = state_.saturationEnabled;
    j["saturation"] = {
        {"bypass", state_.saturation.bypass},
        {"drive", state_.saturation.drive},
        {"mix", state_.saturation.mix},
        {"outputTrimDb", state_.saturation.outputTrimDb},
        {"oversamplingFactor", state_.saturation.oversamplingFactor}
    };
    return j.dump();
}

bool MixNodeChain::fromJson(std::string_view json) noexcept
{
    try {
        const auto j = nlohmann::json::parse(json);
        MixNodeChainState s;
        s.schemaVersion = j.value("schemaVersion", kMixNodeChainSchemaVersion);
        if (s.schemaVersion > kMixNodeChainSchemaVersion)
            return false;
        s.revision = j.value("revision", 1u);
        s.bypass = j.value("bypass", false);
        s.inputGainDb = j.value("inputGainDb", 0.0);
        s.outputGainDb = j.value("outputGainDb", 0.0);
        if (j.contains("staticEq")) {
            const auto& eq = j["staticEq"];
            s.staticEq.enabled = eq.value("enabled", true);
            s.staticEq.bypass = eq.value("bypass", false);
            s.staticEq.frequencyHz = eq.value("frequencyHz", 1'000.0);
            s.staticEq.gainDb = eq.value("gainDb", 0.0);
            s.staticEq.q = eq.value("q", 1.0);
        }
        if (j.contains("dynamicEq")) {
            DynamicEqProcessor tmp;
            tmp.fromJson(j["dynamicEq"].dump());
            s.dynamicEq = tmp.state();
        }
        s.saturationEnabled = j.value("saturationEnabled", false);
        if (j.contains("saturation")) {
            const auto& sat = j["saturation"];
            s.saturation.bypass = sat.value("bypass", false);
            s.saturation.drive = sat.value("drive", 1.0);
            s.saturation.mix = sat.value("mix", 1.0);
            s.saturation.outputTrimDb = sat.value("outputTrimDb", 0.0);
            s.saturation.oversamplingFactor = sat.value("oversamplingFactor", 1);
        }
        setState(s);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace mastering::dsp
