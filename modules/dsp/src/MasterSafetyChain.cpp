#include "mastering/dsp/MasterSafetyChain.h"

#include <algorithm>
#include <cmath>

namespace mastering::dsp {

void MasterSafetyChain::prepare(double sampleRate, int maxBlockSize, int channels, int defaultOsFactor) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    maxBlock_ = std::max(1, maxBlockSize);
    channels_ = std::clamp(channels, 1, 2);
    osFactor_ = (defaultOsFactor == 1 || defaultOsFactor == 2 || defaultOsFactor == 4 || defaultOsFactor == 8)
        ? defaultOsFactor
        : 4;

    // Apply deferred OS factors into child prepares (topology changes happen here only).
    if (settings_.saturation.oversamplingFactor == 0)
        settings_.saturation.oversamplingFactor = osFactor_;
    if (settings_.softClip.oversamplingFactor == 0)
        settings_.softClip.oversamplingFactor = osFactor_;
    if (settings_.hardClip.oversamplingFactor == 0)
        settings_.hardClip.oversamplingFactor = osFactor_;
    if (settings_.limiter.oversamplingFactor == 0)
        settings_.limiter.oversamplingFactor = osFactor_;

    inputSm_.prepare(sampleRate_, 20.0);
    outputSm_.prepare(sampleRate_, 20.0);
    saturation_.prepare(sampleRate_, maxBlock_, channels_, settings_.saturation.oversamplingFactor);
    softClip_.prepare(sampleRate_, maxBlock_, channels_, settings_.softClip.oversamplingFactor);
    hardClip_.prepare(sampleRate_, maxBlock_, channels_, settings_.hardClip.oversamplingFactor);
    limiter_.prepare(sampleRate_, maxBlock_, channels_, settings_.limiter.oversamplingFactor);
    reset();
}

void MasterSafetyChain::reset() noexcept
{
    saturation_.reset();
    softClip_.reset();
    hardClip_.reset();
    limiter_.reset();
    inputSm_.reset(dbToGainSafe(settings_.inputGainDb));
    outputSm_.reset(dbToGainSafe(settings_.outputGainDb));
    clearMeters();
}

void MasterSafetyChain::clearMeters() noexcept
{
    meters_ = {};
    limiter_.clearMeters();
}

void MasterSafetyChain::setSettings(const MasterSafetySettings& settings) noexcept
{
    settings_ = settings;
    if (settings_.clipMode == ClipMode::soft) {
        settings_.hardClip.bypass = true;
        settings_.softClip.bypass = false;
    } else if (settings_.clipMode == ClipMode::hard) {
        settings_.softClip.bypass = true;
        settings_.hardClip.bypass = false;
    } else {
        settings_.softClip.bypass = true;
        settings_.hardClip.bypass = true;
    }
    settings_.saturation.bypass = !settings_.saturationEnabled;
    settings_.limiter.bypass = !settings_.limiterEnabled;

    if (settings_.saturation.oversamplingFactor == 0)
        settings_.saturation.oversamplingFactor = osFactor_;
    if (settings_.softClip.oversamplingFactor == 0)
        settings_.softClip.oversamplingFactor = osFactor_;
    if (settings_.hardClip.oversamplingFactor == 0)
        settings_.hardClip.oversamplingFactor = osFactor_;
    if (settings_.limiter.oversamplingFactor == 0)
        settings_.limiter.oversamplingFactor = osFactor_;

    // Continuous params only — OS factor changes deferred until prepare().
    saturation_.setSettings(settings_.saturation);
    softClip_.setSettings(settings_.softClip);
    hardClip_.setSettings(settings_.hardClip);
    limiter_.setSettings(settings_.limiter);
    inputSm_.setTarget(dbToGainSafe(settings_.inputGainDb));
    outputSm_.setTarget(dbToGainSafe(settings_.outputGainDb));
}

int MasterSafetyChain::latencySamples() const noexcept
{
    int latency = 0;
    if (settings_.saturationEnabled)
        latency += saturation_.latencySamples();
    if (settings_.clipMode == ClipMode::soft)
        latency += softClip_.latencySamples();
    else if (settings_.clipMode == ClipMode::hard)
        latency += hardClip_.latencySamples();
    if (settings_.limiterEnabled)
        latency += limiter_.latencySamples();
    return latency;
}

void MasterSafetyChain::processChunk(float* const* channels, int channelCount, int sampleCount) noexcept
{
    double inPeak = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        const auto g = inputSm_.next();
        for (int ch = 0; ch < channelCount; ++ch) {
            auto x = static_cast<double>(channels[ch][i]) * g;
            if (!std::isfinite(x)) {
                x = 0.0;
                meters_.degraded = true;
            }
            inPeak = std::max(inPeak, std::abs(x));
            channels[ch][i] = static_cast<float>(x);
        }
    }
    meters_.inputPeakLinear = std::max(meters_.inputPeakLinear, inPeak);

    if (settings_.saturationEnabled)
        saturation_.process(channels, channelCount, sampleCount);

    if (settings_.clipMode == ClipMode::soft)
        softClip_.process(channels, channelCount, sampleCount);
    else if (settings_.clipMode == ClipMode::hard)
        hardClip_.process(channels, channelCount, sampleCount);

    if (settings_.limiterEnabled)
        limiter_.process(channels, channelCount, sampleCount);

    double outPeak = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        const auto g = outputSm_.next();
        for (int ch = 0; ch < channelCount; ++ch) {
            auto x = static_cast<double>(channels[ch][i]) * g;
            if (!std::isfinite(x)) {
                x = 0.0;
                meters_.degraded = true;
            }
            outPeak = std::max(outPeak, std::abs(x));
            channels[ch][i] = static_cast<float>(x);
        }
    }
    meters_.outputPeakLinear = std::max(meters_.outputPeakLinear, outPeak);
    meters_.limiterGrDb = limiter_.lastGainReductionDb();
    meters_.limiterMaxGrDb = limiter_.maxGainReductionDb();
    meters_.limiterAvgGrDb = limiter_.averageGainReductionDb();
    meters_.limiterActiveSamples = limiter_.activeSampleCount();
    meters_.safetyClampActivationCount = limiter_.safetyClampActivationCount();
}

void MasterSafetyChain::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channels_);

    // Never pass-through without limiting. Oversized blocks are chunked safely.
    for (int offset = 0; offset < sampleCount;) {
        const int n = std::min(maxBlock_, sampleCount - offset);
        float* slice[2] {};
        for (int ch = 0; ch < channelCount; ++ch)
            slice[ch] = channels[ch] + offset;
        processChunk(slice, channelCount, n);
        offset += n;
    }
}

void MasterSafetyChain::finalize(float* const* channels, int channelCount, int maxSamples) noexcept
{
    if (channels == nullptr || maxSamples <= 0)
        return;
    const int need = latencySamples() + 64;
    const int n = std::min(maxSamples, need);
    for (int ch = 0; ch < std::clamp(channelCount, 1, channels_); ++ch)
        for (int i = 0; i < n; ++i)
            channels[ch][i] = 0.0f;
    process(channels, channelCount, n);
}

} // namespace mastering::dsp
