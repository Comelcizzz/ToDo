#include "mastering/dsp/DynamicTools.h"
#include "mastering/dsp/ProcessorChain.h"

#include <algorithm>
#include <cmath>

namespace mastering::dsp {

void TruePeakLimiter::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    setReleaseMs(80.0);
    reset();
}

void TruePeakLimiter::reset() noexcept
{
    envelope_ = 1.0;
    lastReductionDb_ = 0.0;
    for (auto& channelHistory : history_)
        channelHistory.fill(0.0);
}

void TruePeakLimiter::setCeilingDb(double ceilingDb) noexcept
{
    ceilingDb_ = std::clamp(ceilingDb, -12.0, -0.1);
    ceilingLinear_ = dbToGain(ceilingDb_);
}

void TruePeakLimiter::setReleaseMs(double releaseMs) noexcept
{
    releaseCoefficient_ = std::exp(
        -1.0 / (0.001 * std::max(5.0, releaseMs) * sampleRate_));
}

double TruePeakLimiter::interpolatePeak(
    double y0,
    double y1,
    double y2,
    double y3,
    double fraction) noexcept
{
    const auto a0 = y3 - y2 - y0 + y1;
    const auto a1 = y0 - y1 - a0;
    const auto a2 = y2 - y0;
    return a0 * fraction * fraction * fraction + a1 * fraction * fraction + a2 * fraction + y1;
}

void TruePeakLimiter::process(
    float* const* channels,
    int channelCount,
    int sampleCount) noexcept
{
    if (channels == nullptr || channelCount <= 0 || sampleCount <= 0)
        return;

    const auto activeChannels = std::min(channelCount, 8);
    double deepest = 1.0;

    for (int sample = 0; sample < sampleCount; ++sample) {
        double peak = 0.0;
        for (int channel = 0; channel < activeChannels; ++channel) {
            if (channels[channel] == nullptr)
                continue;
            auto& history = history_[static_cast<std::size_t>(channel)];
            history[0] = history[1];
            history[1] = history[2];
            history[2] = history[3];
            history[3] = channels[channel][sample];
            peak = std::max(peak, std::abs(history[1]));
            for (const auto fraction : {0.25, 0.5, 0.75}) {
                peak = std::max(
                    peak,
                    std::abs(interpolatePeak(
                        history[0],
                        history[1],
                        history[2],
                        history[3],
                        fraction)));
            }
        }

        const auto required = peak > 1.0e-12
            ? std::min(1.0, ceilingLinear_ / peak)
            : 1.0;
        if (required < envelope_)
            envelope_ = required;
        else
            envelope_ = releaseCoefficient_ * envelope_
                + (1.0 - releaseCoefficient_) * required;

        deepest = std::min(deepest, envelope_);
        for (int channel = 0; channel < activeChannels; ++channel) {
            if (channels[channel] == nullptr)
                continue;
            channels[channel][sample] = static_cast<float>(
                history_[static_cast<std::size_t>(channel)][1] * envelope_);
        }
    }

    lastReductionDb_ = gainToDb(deepest);
}

void DynamicSeparator::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    setAttackMs(4.0);
    setReleaseMs(70.0);
    reset();
}

void DynamicSeparator::reset() noexcept
{
    envelope_ = 0.0;
}

void DynamicSeparator::setDepthDb(double depthDb) noexcept
{
    depthDb_ = std::clamp(depthDb, 0.0, 12.0);
}

void DynamicSeparator::setAttackMs(double attackMs) noexcept
{
    attackCoefficient_ = std::exp(
        -1.0 / (0.001 * std::max(0.1, attackMs) * sampleRate_));
}

void DynamicSeparator::setReleaseMs(double releaseMs) noexcept
{
    releaseCoefficient_ = std::exp(
        -1.0 / (0.001 * std::max(1.0, releaseMs) * sampleRate_));
}

void DynamicSeparator::process(
    const float* const* sidechain,
    float* const* target,
    int channelCount,
    int sampleCount) noexcept
{
    if (sidechain == nullptr || target == nullptr || channelCount <= 0 || sampleCount <= 0)
        return;

    const auto activeChannels = std::min(channelCount, 8);
    for (int sample = 0; sample < sampleCount; ++sample) {
        double detector = 0.0;
        for (int channel = 0; channel < activeChannels; ++channel) {
            if (sidechain[channel] == nullptr)
                continue;
            detector = std::max(
                detector,
                std::abs(static_cast<double>(sidechain[channel][sample])));
        }

        const auto coefficient =
            detector > envelope_ ? attackCoefficient_ : releaseCoefficient_;
        envelope_ = coefficient * envelope_ + (1.0 - coefficient) * detector;
        const auto duckAmount = std::clamp(envelope_ * 4.0, 0.0, 1.0);
        const auto gain = dbToGain(-depthDb_ * duckAmount);

        for (int channel = 0; channel < activeChannels; ++channel) {
            if (target[channel] == nullptr)
                continue;
            target[channel][sample] = static_cast<float>(target[channel][sample] * gain);
        }
    }
}

} // namespace mastering::dsp
