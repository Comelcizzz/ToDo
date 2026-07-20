#include "mastering/dsp/EnvelopeDetector.h"
#include "mastering/dsp/ProcessorChain.h"

#include <algorithm>
#include <cmath>

namespace mastering::dsp {

void EnvelopeDetector::prepare(double sampleRate, int maxBlock, int channels) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    maxBlock_ = std::max(1, maxBlock);
    channels_ = std::clamp(channels, 1, 2);
    updateCoefficients();
    reset();
}

void EnvelopeDetector::reset() noexcept
{
    envelope_ = 0.0;
    rmsState_ = 0.0;
    holdCounter_ = 0;
    for (auto& f : bandPass_)
        f.reset();
    for (auto& f : highPass_)
        f.reset();
    for (auto& f : lowPass_)
        f.reset();
}

void EnvelopeDetector::setSettings(const EnvelopeDetectorSettings& settings) noexcept
{
    settings_ = settings;
    settings_.attackMs = std::max(0.1, settings_.attackMs);
    settings_.releaseMs = std::max(1.0, settings_.releaseMs);
    settings_.holdMs = std::max(0.0, settings_.holdMs);
    settings_.bandPassQ = std::clamp(settings_.bandPassQ, 0.1, 40.0);
    settings_.silenceFloorLinear = std::max(1.0e-9, settings_.silenceFloorLinear);
    updateCoefficients();
}

void EnvelopeDetector::updateCoefficients() noexcept
{
    const auto atk = std::max(0.0001, settings_.attackMs * 0.001);
    const auto rel = std::max(0.001, settings_.releaseMs * 0.001);
    attackCoeff_ = std::exp(-1.0 / (atk * sampleRate_));
    releaseCoeff_ = std::exp(-1.0 / (rel * sampleRate_));
    holdSamples_ = static_cast<int>(std::lround(settings_.holdMs * 0.001 * sampleRate_));
    // RMS averaging ~10 ms.
    rmsCoeff_ = std::exp(-1.0 / (0.010 * sampleRate_));

    for (int ch = 0; ch < 2; ++ch) {
        bandPass_[static_cast<std::size_t>(ch)].setType(SvfFilter::Type::bandPass);
        bandPass_[static_cast<std::size_t>(ch)].setParams(
            sampleRate_, settings_.bandPassHz, settings_.bandPassQ, 0.0);
        highPass_[static_cast<std::size_t>(ch)].setType(SvfFilter::Type::highPass);
        highPass_[static_cast<std::size_t>(ch)].setParams(
            sampleRate_, settings_.highPassHz, 0.707, 0.0);
        lowPass_[static_cast<std::size_t>(ch)].setType(SvfFilter::Type::lowPass);
        lowPass_[static_cast<std::size_t>(ch)].setParams(
            sampleRate_, settings_.lowPassHz, 0.707, 0.0);
    }
}

double EnvelopeDetector::filterSample(double x, int channel) noexcept
{
    const auto idx = static_cast<std::size_t>(std::clamp(channel, 0, 1));
    if (settings_.highPassEnabled)
        x = highPass_[idx].process(x);
    if (settings_.lowPassEnabled)
        x = lowPass_[idx].process(x);
    if (settings_.bandPassEnabled)
        x = bandPass_[idx].process(x);
    return x;
}

double EnvelopeDetector::aggregate(const float* const* input, int channelCount, int i) noexcept
{
    if (input == nullptr || channelCount <= 0)
        return 0.0;
    channelCount = std::clamp(channelCount, 1, channels_);

    if (settings_.channelMode == DetectorChannelMode::monoSum) {
        double sum = 0.0;
        for (int ch = 0; ch < channelCount; ++ch) {
            auto x = static_cast<double>(input[ch][i]);
            if (!std::isfinite(x))
                x = 0.0;
            sum += filterSample(x, ch);
        }
        return std::abs(sum / static_cast<double>(channelCount));
    }

    if (settings_.channelMode == DetectorChannelMode::linkedEnergy) {
        double energy = 0.0;
        for (int ch = 0; ch < channelCount; ++ch) {
            auto x = static_cast<double>(input[ch][i]);
            if (!std::isfinite(x))
                x = 0.0;
            x = filterSample(x, ch);
            energy += x * x;
        }
        return std::sqrt(energy / static_cast<double>(channelCount));
    }

    // stereoMax
    double peak = 0.0;
    for (int ch = 0; ch < channelCount; ++ch) {
        auto x = static_cast<double>(input[ch][i]);
        if (!std::isfinite(x))
            x = 0.0;
        x = filterSample(x, ch);
        peak = std::max(peak, std::abs(x));
    }
    return peak;
}

double EnvelopeDetector::processSample(
    const float* const* main,
    const float* const* sidechain,
    int channelCount,
    int sampleIndex) noexcept
{
    const float* const* src = main;
    if (settings_.source == DetectorSource::external) {
        src = sidechain;
        if (src == nullptr) {
            // Missing sidechain → treat as silence.
            const double silence = 0.0;
            if (silence < envelope_) {
                if (holdCounter_ > 0)
                    --holdCounter_;
                else
                    envelope_ = silence + (envelope_ - silence) * releaseCoeff_;
            }
            return envelope_;
        }
    } else if (src == nullptr) {
        return envelope_;
    }

    const auto raw = aggregate(src, channelCount, sampleIndex);

    double peakLike = raw;
    rmsState_ = raw * raw + (rmsState_ - raw * raw) * rmsCoeff_;
    const auto rms = std::sqrt(std::max(0.0, rmsState_));

    double detected = peakLike;
    if (settings_.mode == DetectorMode::rms)
        detected = rms;
    else if (settings_.mode == DetectorMode::hybrid)
        detected = std::max(peakLike, rms);

    detected = std::max(detected, 0.0);

    if (detected > envelope_) {
        envelope_ = detected + (envelope_ - detected) * attackCoeff_;
        holdCounter_ = holdSamples_;
    } else if (holdCounter_ > 0) {
        --holdCounter_;
    } else {
        envelope_ = detected + (envelope_ - detected) * releaseCoeff_;
    }

    if (!std::isfinite(envelope_))
        envelope_ = 0.0;
    return envelope_;
}

void EnvelopeDetector::processBlock(
    const float* const* main,
    const float* const* sidechain,
    int channelCount,
    int sampleCount,
    float* envelopeOut) noexcept
{
    for (int i = 0; i < sampleCount; ++i) {
        const auto e = processSample(main, sidechain, channelCount, i);
        if (envelopeOut != nullptr)
            envelopeOut[i] = static_cast<float>(e);
    }
}

double EnvelopeDetector::envelopeDb() const noexcept
{
    return gainToDb(std::max(settings_.silenceFloorLinear, envelope_));
}

} // namespace mastering::dsp
