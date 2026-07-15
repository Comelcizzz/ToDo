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
    const auto len = static_cast<std::size_t>(length);
    const auto read = (write + 1) % len;
    const auto out = line[read];
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
    settings_.oversamplingFactor = (factor == 1 || factor == 2 || factor == 4 || factor == 8) ? factor : 4;

    oversampler_.prepare(sampleRate_, maxBlock_, channels_, settings_.oversamplingFactor);
    ceilingSm_.prepare(sampleRate_, 10.0);
    inputSm_.prepare(sampleRate_, 20.0);
    outSm_.prepare(sampleRate_, 20.0);
    bypassSm_.prepare(sampleRate_, 15.0);

    lookAheadSamples_ = std::max(
        1,
        static_cast<int>(std::lround(settings_.lookAheadMs * 0.001 * sampleRate_)));
    rebuildLatency();

    // Look-ahead delay buffers are base-rate (lookAheadSamples_).
    for (int ch = 0; ch < 2; ++ch) {
        lookAhead_[static_cast<std::size_t>(ch)].assign(
            static_cast<std::size_t>(std::max(1, lookAheadSamples_)), 0.0f);
        laWrite_[static_cast<std::size_t>(ch)] = 0;
        bypassDelay_[static_cast<std::size_t>(ch)].assign(
            static_cast<std::size_t>(std::max(1, totalLatency_)), 0.0f);
        bypassWrite_[static_cast<std::size_t>(ch)] = 0;
        dryScratch_[static_cast<std::size_t>(ch)].assign(static_cast<std::size_t>(maxBlock_), 0.0f);
    }
    gainDelay_.clear();
    gainWrite_ = 0;

    setReleaseMs(settings_.releaseMs);
    reset();
}

void TruePeakLimiter::rebuildLatency() noexcept
{
    // OS up+down delay + look-ahead (base-rate samples).
    totalLatency_ = oversampler_.latencySamplesBaseRate() + lookAheadSamples_;
}

void TruePeakLimiter::reset() noexcept
{
    oversampler_.reset();
    envelope_ = 1.0;
    lastReductionDb_ = 0.0;
    clearMeters();
    ceilingSm_.reset(dbToGain(settings_.ceilingDbTp));
    inputSm_.reset(dbToGain(settings_.inputGainDb));
    outSm_.reset(dbToGain(settings_.outputTrimDb));
    bypassSm_.reset(settings_.bypass ? 0.0 : 1.0);
    for (auto& b : lookAhead_)
        std::fill(b.begin(), b.end(), 0.0f);
    for (auto& b : bypassDelay_)
        std::fill(b.begin(), b.end(), 0.0f);
    std::fill(gainDelay_.begin(), gainDelay_.end(), 1.0);
    laWrite_.fill(0);
    bypassWrite_.fill(0);
    gainWrite_ = 0;
}

void TruePeakLimiter::clearMeters() noexcept
{
    maxReductionDb_ = 0.0;
    grSumDb_ = 0.0;
    grCount_ = 0;
    activeSamples_ = 0;
}

void TruePeakLimiter::setSettings(const TruePeakLimiterSettings& settings) noexcept
{
    settings_ = settings;
    settings_.ceilingDbTp = std::clamp(settings_.ceilingDbTp, -12.0, -0.05);
    settings_.lookAheadMs = std::clamp(settings_.lookAheadMs, 0.1, 20.0);
    settings_.releaseMs = std::max(5.0, settings_.releaseMs);
    settings_.maxGainReductionDb = std::clamp(settings_.maxGainReductionDb, 1.0, 48.0);
    if (settings_.oversamplingFactor != 1 && settings_.oversamplingFactor != 2
        && settings_.oversamplingFactor != 4 && settings_.oversamplingFactor != 8)
        settings_.oversamplingFactor = 4;

    const auto newLa = std::max(
        1,
        static_cast<int>(std::lround(settings_.lookAheadMs * 0.001 * sampleRate_)));
    if (newLa != lookAheadSamples_ || settings_.oversamplingFactor != oversampler_.factor()) {
        lookAheadSamples_ = newLa;
        oversampler_.prepare(sampleRate_, maxBlock_, channels_, settings_.oversamplingFactor);
        rebuildLatency();
        const int osLa = lookAheadSamples_; // base-rate look-ahead buffer length
        for (int ch = 0; ch < 2; ++ch) {
            lookAhead_[static_cast<std::size_t>(ch)].assign(static_cast<std::size_t>(std::max(1, osLa)), 0.0f);
            bypassDelay_[static_cast<std::size_t>(ch)].assign(
                static_cast<std::size_t>(std::max(1, totalLatency_)), 0.0f);
            laWrite_[static_cast<std::size_t>(ch)] = 0;
            bypassWrite_[static_cast<std::size_t>(ch)] = 0;
        }
        gainDelay_.clear();
        gainWrite_ = 0;
    }
    ceilingSm_.setTarget(dbToGain(settings_.ceilingDbTp));
    inputSm_.setTarget(dbToGain(settings_.inputGainDb));
    outSm_.setTarget(dbToGain(settings_.outputTrimDb));
    bypassSm_.setTarget(settings_.bypass ? 0.0 : 1.0);
    setReleaseMs(settings_.releaseMs);
}

void TruePeakLimiter::setCeilingDb(double ceilingDb) noexcept
{
    settings_.ceilingDbTp = std::clamp(ceilingDb, -12.0, -0.05);
    ceilingSm_.setTarget(dbToGain(settings_.ceilingDbTp));
}

void TruePeakLimiter::setReleaseMs(double releaseMs) noexcept
{
    settings_.releaseMs = std::max(5.0, releaseMs);
    const auto osRate = sampleRate_ * static_cast<double>(std::max(1, oversampler_.factor()));
    releaseCoeff_ = std::exp(-1.0 / (settings_.releaseMs * 0.001 * osRate));
}

double TruePeakLimiter::detectOsPeak(const float* const* os, int chCount, int index) const noexcept
{
    double peak = 0.0;
    for (int ch = 0; ch < chCount; ++ch)
        peak = std::max(peak, std::abs(static_cast<double>(os[ch][index])));
    return peak;
}

void TruePeakLimiter::processOsGain(float* const* os, int chCount, int osSamples) noexcept
{
    const auto minGain = dbToGain(-settings_.maxGainReductionDb);
    // Headroom so reconstructed peaks after downsample stay within declared tolerance.
    const auto ceiling = std::max(1.0e-6, ceilingSm_.current() * dbToGain(-1.0));

    for (int i = 0; i < osSamples; ++i) {
        double peak = 0.0;
        for (int ch = 0; ch < chCount; ++ch) {
            const auto a = static_cast<double>(os[ch][i]);
            if (!std::isfinite(a))
                continue;
            peak = std::max(peak, std::abs(a));
            if (i + 1 < osSamples) {
                const auto b = static_cast<double>(os[ch][i + 1]);
                if (std::isfinite(b)) {
                    peak = std::max(peak, std::abs(0.5 * (a + b)));
                    peak = std::max(peak, std::abs(0.75 * a + 0.25 * b));
                    peak = std::max(peak, std::abs(0.25 * a + 0.75 * b));
                }
            }
        }

        double target = peak > ceiling ? ceiling / peak : 1.0;
        target = std::max(minGain, target);
        if (target < envelope_)
            envelope_ = target; // instant attack
        else
            envelope_ = target + (envelope_ - target) * releaseCoeff_;

        const auto grDb = envelope_ < 1.0 - 1.0e-12 ? -gainToDb(envelope_) : 0.0;
        lastReductionDb_ = grDb;
        maxReductionDb_ = std::max(maxReductionDb_, grDb);
        grSumDb_ += grDb;
        ++grCount_;
        if (grDb > 0.05)
            ++activeSamples_;

        for (int ch = 0; ch < chCount; ++ch) {
            auto x = static_cast<double>(os[ch][i]) * envelope_;
            if (!std::isfinite(x))
                x = 0.0;
            x = std::clamp(x, -ceiling, ceiling);
            os[ch][i] = static_cast<float>(x);
        }
    }
}

void TruePeakLimiter::process(float* const* channels, int channelCount, int sampleCount) noexcept
{
    if (channels == nullptr || sampleCount <= 0)
        return;
    channelCount = std::clamp(channelCount, 1, channels_);

    if (sampleCount > maxBlock_) {
        for (int i = 0; i < sampleCount; ++i) {
            bypassSm_.next();
            for (int ch = 0; ch < channelCount; ++ch) {
                channels[ch][i] = readRing(
                    bypassDelay_[static_cast<std::size_t>(ch)],
                    bypassWrite_[static_cast<std::size_t>(ch)],
                    totalLatency_,
                    channels[ch][i]);
            }
        }
        return;
    }

    // Snapshot dry, apply input gain, then base-rate look-ahead delay before OS limiting.
    for (int i = 0; i < sampleCount; ++i) {
        const auto inG = inputSm_.next();
        for (int ch = 0; ch < channelCount; ++ch) {
            auto x = static_cast<double>(channels[ch][i]) * inG;
            if (!std::isfinite(x))
                x = 0.0;
            dryScratch_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] =
                channels[ch][i];
            channels[ch][i] = readRing(
                lookAhead_[static_cast<std::size_t>(ch)],
                laWrite_[static_cast<std::size_t>(ch)],
                lookAheadSamples_,
                static_cast<float>(x));
        }
    }

    auto apply = [this](float* const* os, int chCount, int osSamples, double) {
        processOsGain(os, chCount, osSamples);
    };

    if (oversampler_.factor() <= 1)
        apply(channels, channelCount, sampleCount, 1.0);
    else
        oversampler_.process(channels, channelCount, sampleCount, apply);

    for (int i = 0; i < sampleCount; ++i) {
        const auto outG = outSm_.next();
        const auto wetAmt = bypassSm_.next();
        ceilingSm_.next();
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
