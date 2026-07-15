#include "mastering/dsp/DynamicTools.h"
#include "mastering/dsp/ProcessorChain.h"

#include <algorithm>
#include <cmath>

namespace mastering::dsp {
namespace {

float readRing(std::vector<float>& line, std::size_t& write, int length, float input) noexcept
{
    if (length <= 0 || line.empty())
        return input;
    // Size-L circular buffer: read oldest then overwrite → exact L-sample delay.
    const auto len = static_cast<std::size_t>(length);
    const auto out = line[write];
    line[write] = input;
    write = (write + 1) % len;
    return out;
}

} // namespace

void TruePeakLimiter::prepare(double sampleRate, int maxBlockSize, int channels, int factor) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    maxBlock_ = std::max(1, maxBlockSize);
    channels_ = std::clamp(channels, 1, 2);

    // Apply any pending topology from setSettings.
    if (topologyChangePending_) {
        settings_.lookAheadMs = pendingSettings_.lookAheadMs;
        settings_.oversamplingFactor = pendingSettings_.oversamplingFactor;
        topologyChangePending_ = false;
    }

    int f = factor;
    if (settings_.oversamplingFactor == 1 || settings_.oversamplingFactor == 2
        || settings_.oversamplingFactor == 4 || settings_.oversamplingFactor == 8)
        f = settings_.oversamplingFactor;
    else if (!(f == 1 || f == 2 || f == 4 || f == 8))
        f = 4;
    settings_.oversamplingFactor = f;
    preparedFactor_ = f;

    oversampler_.prepare(sampleRate_, maxBlock_, channels_, preparedFactor_);
    ceilingSm_.prepare(sampleRate_, 10.0);
    inputSm_.prepare(sampleRate_, 20.0);
    outSm_.prepare(sampleRate_, 20.0);
    bypassSm_.prepare(sampleRate_, 15.0);

    lookAheadBase_ = std::max(
        1,
        static_cast<int>(std::lround(settings_.lookAheadMs * 0.001 * sampleRate_)));
    lookAheadOs_ = std::max(1, lookAheadBase_ * preparedFactor_);
    rebuildLatency();
    ensureLookAheadCapacity();

    for (int ch = 0; ch < 2; ++ch) {
        bypassDelay_[static_cast<std::size_t>(ch)].assign(
            static_cast<std::size_t>(std::max(1, totalLatency_)), 0.0f);
        bypassWrite_[static_cast<std::size_t>(ch)] = 0;
        dryScratch_[static_cast<std::size_t>(ch)].assign(static_cast<std::size_t>(maxBlock_), 0.0f);
    }

    updateReleaseCoeff();
    reset();
}

void TruePeakLimiter::rebuildLatency() noexcept
{
    // OS round-trip (base-rate) + look-ahead (base-rate). Look-ahead OS delay converts 1:1
    // to base-rate samples: lookAheadOs / factor = lookAheadBase.
    totalLatency_ = oversampler_.latencySamplesBaseRate() + lookAheadBase_;
}

void TruePeakLimiter::ensureLookAheadCapacity() noexcept
{
    laOsLength_ = std::max(2, lookAheadOs_ + 1);
    for (int ch = 0; ch < 2; ++ch) {
        lookAheadOsRing_[static_cast<std::size_t>(ch)].assign(
            static_cast<std::size_t>(laOsLength_), 0.0f);
        laOsWrite_[static_cast<std::size_t>(ch)] = 0;
    }
}

void TruePeakLimiter::updateReleaseCoeff() noexcept
{
    const auto osRate = sampleRate_ * static_cast<double>(std::max(1, preparedFactor_));
    releaseCoeff_ = std::exp(-1.0 / (std::max(5.0, settings_.releaseMs) * 0.001 * osRate));
}

void TruePeakLimiter::reset() noexcept
{
    oversampler_.reset();
    envelope_ = 1.0;
    clearMeters();
    ceilingSm_.reset(dbToGain(settings_.ceilingDbTp));
    inputSm_.reset(dbToGain(settings_.inputGainDb));
    outSm_.reset(dbToGain(settings_.outputTrimDb));
    bypassSm_.reset(settings_.bypass ? 0.0 : 1.0);
    for (auto& b : lookAheadOsRing_)
        std::fill(b.begin(), b.end(), 0.0f);
    for (auto& b : bypassDelay_)
        std::fill(b.begin(), b.end(), 0.0f);
    laOsWrite_.fill(0);
    bypassWrite_.fill(0);
    grEnvelopeWrite_ = 0;
}

void TruePeakLimiter::clearMeters() noexcept
{
    meters_ = {};
    grSumDb_ = 0.0;
    grCount_ = 0;
}

void TruePeakLimiter::reserveGainEnvelope(std::size_t capacity) noexcept
{
    grEnvelopeDb_.assign(capacity, 0.0f);
    grEnvelopeWrite_ = 0;
    captureEnvelope_ = capacity > 0;
}

void TruePeakLimiter::setSettings(const TruePeakLimiterSettings& settings) noexcept
{
    // Continuous / smoothed parameters — realtime-safe.
    settings_.bypass = settings.bypass;
    settings_.ceilingDbTp = std::clamp(settings.ceilingDbTp, -12.0, -0.05);
    settings_.releaseMs = std::max(5.0, settings.releaseMs);
    settings_.inputGainDb = settings.inputGainDb;
    settings_.outputTrimDb = settings.outputTrimDb;
    settings_.stereoLink = true; // M1B: linked only
    settings_.maxGainReductionDb = std::clamp(settings.maxGainReductionDb, 1.0, 48.0);

    ceilingSm_.setTarget(dbToGain(settings_.ceilingDbTp));
    inputSm_.setTarget(dbToGain(settings_.inputGainDb));
    outSm_.setTarget(dbToGain(settings_.outputTrimDb));
    bypassSm_.setTarget(settings_.bypass ? 0.0 : 1.0);
    updateReleaseCoeff();

    // Topology: defer allocation to prepare().
    int factor = settings.oversamplingFactor;
    if (factor != 1 && factor != 2 && factor != 4 && factor != 8)
        factor = 4;
    const auto laMs = std::clamp(settings.lookAheadMs, 0.1, 20.0);
    const bool topoChanged = (factor != preparedFactor_)
        || (std::abs(laMs - settings_.lookAheadMs) > 1.0e-9)
        || (std::max(1, static_cast<int>(std::lround(laMs * 0.001 * sampleRate_))) != lookAheadBase_);

    pendingSettings_ = settings_;
    pendingSettings_.oversamplingFactor = factor;
    pendingSettings_.lookAheadMs = laMs;

    if (topoChanged) {
        topologyChangePending_ = true;
        // Keep running with prepared topology until prepare() is called.
    } else {
        settings_.lookAheadMs = laMs;
        settings_.oversamplingFactor = preparedFactor_;
    }
}

void TruePeakLimiter::setCeilingDb(double ceilingDb) noexcept
{
    settings_.ceilingDbTp = std::clamp(ceilingDb, -12.0, -0.05);
    ceilingSm_.setTarget(dbToGain(settings_.ceilingDbTp));
}

void TruePeakLimiter::setReleaseMs(double releaseMs) noexcept
{
    settings_.releaseMs = std::max(5.0, releaseMs);
    updateReleaseCoeff();
}

void TruePeakLimiter::processOsLookAhead(float* const* os, int chCount, int osSamples) noexcept
{
    // User-facing ceiling. Documented reconstruction guard compensates for post-downsample /
    // TP-meter peaks that can exceed OS sample peaks (decimation FIR sidelobes).
    // This is NOT the old hidden −1 dB mastering headroom.
    // Declared before validation: kReconstructionGuardDb = 0.12 dB.
    constexpr double kReconstructionGuardDb = 0.12;
    const auto userCeiling = std::max(1.0e-6, ceilingSm_.current());
    const auto ceiling = userCeiling * dbToGain(-kReconstructionGuardDb);
    const auto minGain = dbToGain(-settings_.maxGainReductionDb);
    const auto la = lookAheadOs_;
    const auto ringLen = laOsLength_;

    for (int i = 0; i < osSamples; ++i) {
        for (int ch = 0; ch < chCount; ++ch) {
            auto x = static_cast<double>(os[ch][i]);
            if (!std::isfinite(x))
                x = 0.0;
            auto& ring = lookAheadOsRing_[static_cast<std::size_t>(ch)];
            auto& w = laOsWrite_[static_cast<std::size_t>(ch)];
            ring[w] = static_cast<float>(x);
            w = (w + 1) % static_cast<std::size_t>(ringLen);
        }

        double peak = 0.0;
        for (int ch = 0; ch < chCount; ++ch) {
            const auto& ring = lookAheadOsRing_[static_cast<std::size_t>(ch)];
            const auto w = laOsWrite_[static_cast<std::size_t>(ch)];
            double prev = 0.0;
            for (int k = 0; k < la; ++k) {
                const auto idx = (w + static_cast<std::size_t>(ringLen) - 1
                                    - static_cast<std::size_t>(k))
                    % static_cast<std::size_t>(ringLen);
                const auto a = static_cast<double>(ring[idx]);
                peak = std::max(peak, std::abs(a));
                if (k > 0) {
                    peak = std::max(peak, std::abs(0.5 * (a + prev)));
                    peak = std::max(peak, std::abs(0.75 * a + 0.25 * prev));
                    peak = std::max(peak, std::abs(0.25 * a + 0.75 * prev));
                }
                prev = a;
            }
        }

        double target = peak > ceiling ? ceiling / peak : 1.0;
        target = std::max(minGain, std::min(1.0, target));
        const auto prevEnv = envelope_;
        if (target < envelope_)
            envelope_ = target;
        else
            envelope_ = target + (envelope_ - target) * releaseCoeff_;

        meters_.maxPerSampleGainJump =
            std::max(meters_.maxPerSampleGainJump, std::abs(envelope_ - prevEnv));

        const auto grDb = envelope_ < 1.0 - 1.0e-12 ? -gainToDb(envelope_) : 0.0;
        meters_.lastGainReductionDb = grDb;
        meters_.maxGainReductionDb = std::max(meters_.maxGainReductionDb, grDb);
        grSumDb_ += grDb;
        ++grCount_;
        meters_.averageGainReductionDb =
            grCount_ > 0 ? grSumDb_ / static_cast<double>(grCount_) : 0.0;
        if (grDb > 0.05)
            ++meters_.activeSampleCount;

        for (int ch = 0; ch < chCount; ++ch) {
            const auto& ring = lookAheadOsRing_[static_cast<std::size_t>(ch)];
            const auto w = laOsWrite_[static_cast<std::size_t>(ch)];
            // Newest at (w-1); delayed by la → (w - 1 - la).
            const auto readIdx = (w + static_cast<std::size_t>(ringLen) - 1
                                     - static_cast<std::size_t>(la))
                % static_cast<std::size_t>(ringLen);
            auto y = static_cast<double>(ring[readIdx]) * envelope_;
            if (!std::isfinite(y))
                y = 0.0;

            const auto excess = std::abs(y) - userCeiling;
            if (excess > 0.0) {
                meters_.maxPreClampOvershootLinear =
                    std::max(meters_.maxPreClampOvershootLinear, excess);
                ++meters_.safetyClampActivationCount;
                y = std::copysign(userCeiling, y);
            }
            os[ch][i] = static_cast<float>(y);
        }

        if (captureEnvelope_ && grEnvelopeWrite_ < grEnvelopeDb_.size())
            grEnvelopeDb_[grEnvelopeWrite_++] = static_cast<float>(grDb);
    }
}

void TruePeakLimiter::processChunk(float* const* channels, int channelCount, int sampleCount) noexcept
{
    for (int i = 0; i < sampleCount; ++i) {
        const auto inG = inputSm_.next();
        for (int ch = 0; ch < channelCount; ++ch) {
            auto x = static_cast<double>(channels[ch][i]) * inG;
            if (!std::isfinite(x))
                x = 0.0;
            dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = channels[ch][i];
            channels[ch][i] = static_cast<float>(x);
        }
    }

    auto apply = [this](float* const* os, int chCount, int osSamples, double) {
        processOsLookAhead(os, chCount, osSamples);
    };

    if (oversampler_.factor() <= 1)
        apply(channels, channelCount, sampleCount, 1.0);
    else
        oversampler_.process(channels, channelCount, sampleCount, apply);

    for (int i = 0; i < sampleCount; ++i) {
        const auto outG = outSm_.next();
        const auto wetAmt = bypassSm_.next();
        (void) ceilingSm_.next();
        for (int ch = 0; ch < channelCount; ++ch) {
            const auto dryIn = dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)];
            const auto delayedDry = readRing(
                bypassDelay_[static_cast<std::size_t>(ch)],
                bypassWrite_[static_cast<std::size_t>(ch)],
                totalLatency_,
                dryIn);
            auto wet = static_cast<double>(channels[ch][i]) * outG;
            auto out = delayedDry + wetAmt * (wet - delayedDry);
            if (!std::isfinite(out))
                out = 0.0;
            channels[ch][i] = static_cast<float>(out);
        }
    }
}

void TruePeakLimiter::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channels_);

    // Oversized blocks: safe chunked processing — never bypass the limiter.
    for (int offset = 0; offset < sampleCount;) {
        const int n = std::min(maxBlock_, sampleCount - offset);
        float* slice[2] {};
        for (int ch = 0; ch < channelCount; ++ch)
            slice[ch] = channels[ch] + offset;
        processChunk(slice, channelCount, n);
        offset += n;
    }
}

void TruePeakLimiter::finalize(float* const* channels, int channelCount, int maxSamplesToWrite) noexcept
{
    if (channels == nullptr || maxSamplesToWrite <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channels_);
    const int flush = std::min(maxSamplesToWrite, totalLatency_ + 64);
    for (int ch = 0; ch < channelCount; ++ch)
        for (int i = 0; i < flush; ++i)
            channels[ch][i] = 0.0f;
    process(channels, channelCount, flush);
}

void DynamicSeparator::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    setAttackMs(5.0);
    setReleaseMs(80.0);
    reset();
}

void DynamicSeparator::reset() noexcept { envelope_ = 0.0; }

void DynamicSeparator::setDepthDb(double depthDb) noexcept
{
    depthDb_ = std::clamp(depthDb, 0.0, 12.0);
}

void DynamicSeparator::setAttackMs(double attackMs) noexcept
{
    const auto seconds = std::max(0.001, attackMs * 0.001);
    attackCoefficient_ = std::exp(-1.0 / (seconds * sampleRate_));
}

void DynamicSeparator::setReleaseMs(double releaseMs) noexcept
{
    const auto seconds = std::max(0.001, releaseMs * 0.001);
    releaseCoefficient_ = std::exp(-1.0 / (seconds * sampleRate_));
}

void DynamicSeparator::process(
    const float* const* sidechain,
    float* const* target,
    int channelCount,
    int sampleCount) noexcept
{
    if (sidechain == nullptr || target == nullptr || channelCount <= 0 || sampleCount <= 0)
        return;

    for (int sample = 0; sample < sampleCount; ++sample) {
        double peak = 0.0;
        for (int channel = 0; channel < channelCount; ++channel)
            peak = std::max(peak, std::abs(static_cast<double>(sidechain[channel][sample])));

        if (peak > envelope_)
            envelope_ = peak + (envelope_ - peak) * attackCoefficient_;
        else
            envelope_ = peak + (envelope_ - peak) * releaseCoefficient_;

        const auto reduction = 1.0
            - (1.0 - dbToGain(-depthDb_)) * std::clamp(envelope_, 0.0, 1.0);
        for (int channel = 0; channel < channelCount; ++channel)
            target[channel][sample] = static_cast<float>(
                static_cast<double>(target[channel][sample]) * reduction);
    }
}

} // namespace mastering::dsp
