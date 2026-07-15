#include "mastering/dsp/ProcessorChain.h"

#include <algorithm>
#include <cmath>

namespace mastering::dsp {
namespace {

constexpr double pi = 3.14159265358979323846;

double clampFrequency(double sampleRate, double frequency) noexcept
{
    return std::clamp(frequency, 10.0, sampleRate * 0.45);
}

} // namespace

double gainToDb(double gain) noexcept
{
    return gain > 1.0e-12 ? 20.0 * std::log10(gain) : -120.0;
}

double dbToGain(double decibels) noexcept
{
    return std::pow(10.0, decibels / 20.0);
}

void ProcessorChain::prepare(double sampleRate, int) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    reset();
}

void ProcessorChain::reset() noexcept
{
    envelope_ = 0.0;
    highPassState_.fill({});
    lowShelfState_.fill({});
    presenceState_.fill({});
    highShelfState_.fill({});
}

void ProcessorChain::setSettings(const ProcessorSettings& settings) noexcept
{
    settings_ = settings;
    settings_.amount = std::clamp(settings_.amount, 0.0, 1.0);
    settings_.compressor.ratio = std::max(1.0, settings_.compressor.ratio);
    settings_.saturation = std::clamp(settings_.saturation, 0.0, 1.0);
}

const ProcessorSettings& ProcessorChain::settings() const noexcept
{
    return settings_;
}

double ProcessorChain::processFilter(
    double input,
    const Coefficients& coefficients,
    FilterState& state) noexcept
{
    const auto output =
        coefficients.b0 * input
        + coefficients.b1 * state.x1
        + coefficients.b2 * state.x2
        - coefficients.a1 * state.y1
        - coefficients.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = input;
    state.y2 = state.y1;
    state.y1 = output;
    return output;
}

ProcessorChain::Coefficients ProcessorChain::highPass(
    double sampleRate,
    double frequency) noexcept
{
    const auto omega = 2.0 * pi * clampFrequency(sampleRate, frequency) / sampleRate;
    const auto alpha = std::sin(omega) / (2.0 * 0.70710678118);
    const auto cosOmega = std::cos(omega);
    const auto a0 = 1.0 + alpha;
    return {
        (1.0 + cosOmega) * 0.5 / a0,
        -(1.0 + cosOmega) / a0,
        (1.0 + cosOmega) * 0.5 / a0,
        -2.0 * cosOmega / a0,
        (1.0 - alpha) / a0
    };
}

ProcessorChain::Coefficients ProcessorChain::lowShelf(
    double sampleRate,
    double frequency,
    double gainDb) noexcept
{
    const auto amplitude = std::pow(10.0, gainDb / 40.0);
    const auto omega = 2.0 * pi * clampFrequency(sampleRate, frequency) / sampleRate;
    const auto cosOmega = std::cos(omega);
    const auto sinOmega = std::sin(omega);
    const auto beta = std::sqrt(2.0 * amplitude) * sinOmega;
    const auto a0 = (amplitude + 1.0) + (amplitude - 1.0) * cosOmega + beta;
    return {
        amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosOmega + beta) / a0,
        2.0 * amplitude * ((amplitude - 1.0) - (amplitude + 1.0) * cosOmega) / a0,
        amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosOmega - beta) / a0,
        -2.0 * ((amplitude - 1.0) + (amplitude + 1.0) * cosOmega) / a0,
        ((amplitude + 1.0) + (amplitude - 1.0) * cosOmega - beta) / a0
    };
}

ProcessorChain::Coefficients ProcessorChain::peak(
    double sampleRate,
    double frequency,
    double gainDb) noexcept
{
    const auto amplitude = std::pow(10.0, gainDb / 40.0);
    const auto omega = 2.0 * pi * clampFrequency(sampleRate, frequency) / sampleRate;
    const auto alpha = std::sin(omega) / (2.0 * 0.8);
    const auto cosOmega = std::cos(omega);
    const auto a0 = 1.0 + alpha / amplitude;
    return {
        (1.0 + alpha * amplitude) / a0,
        -2.0 * cosOmega / a0,
        (1.0 - alpha * amplitude) / a0,
        -2.0 * cosOmega / a0,
        (1.0 - alpha / amplitude) / a0
    };
}

ProcessorChain::Coefficients ProcessorChain::highShelf(
    double sampleRate,
    double frequency,
    double gainDb) noexcept
{
    const auto amplitude = std::pow(10.0, gainDb / 40.0);
    const auto omega = 2.0 * pi * clampFrequency(sampleRate, frequency) / sampleRate;
    const auto cosOmega = std::cos(omega);
    const auto sinOmega = std::sin(omega);
    const auto beta = std::sqrt(2.0 * amplitude) * sinOmega;
    const auto a0 = (amplitude + 1.0) - (amplitude - 1.0) * cosOmega + beta;
    return {
        amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosOmega + beta) / a0,
        -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosOmega) / a0,
        amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosOmega - beta) / a0,
        2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosOmega) / a0,
        ((amplitude + 1.0) - (amplitude - 1.0) * cosOmega - beta) / a0
    };
}

void ProcessorChain::process(
    float* const* channels,
    int channelCount,
    int sampleCount) noexcept
{
    if (settings_.bypass || channels == nullptr || channelCount <= 0 || sampleCount <= 0)
        return;

    const auto activeChannels = std::min(channelCount, 8);
    const auto highPassCoefficients =
        highPass(sampleRate_, settings_.equalizer.highPassHz);
    const auto lowShelfCoefficients = lowShelf(
        sampleRate_,
        settings_.equalizer.lowShelfHz,
        settings_.equalizer.lowShelfGainDb);
    const auto presenceCoefficients = peak(
        sampleRate_,
        settings_.equalizer.presenceHz,
        settings_.equalizer.presenceGainDb);
    const auto highShelfCoefficients = highShelf(
        sampleRate_,
        settings_.equalizer.highShelfHz,
        settings_.equalizer.highShelfGainDb);

    const auto inputGain = dbToGain(settings_.inputGainDb);
    const auto outputGain =
        dbToGain(settings_.outputGainDb + settings_.compressor.makeupDb);
    const auto ceiling = dbToGain(settings_.clipCeilingDb);
    const auto attackCoefficient = std::exp(
        -1.0 / (0.001 * std::max(0.1, settings_.compressor.attackMs) * sampleRate_));
    const auto releaseCoefficient = std::exp(
        -1.0 / (0.001 * std::max(1.0, settings_.compressor.releaseMs) * sampleRate_));
    const auto drive = 1.0 + settings_.saturation * 8.0;
    const auto saturationNormalizer = 1.0 / std::tanh(drive);

    std::array<double, 8> dry {};
    std::array<double, 8> filtered {};
    for (int sample = 0; sample < sampleCount; ++sample) {
        double detector = 0.0;
        for (int channel = 0; channel < activeChannels; ++channel) {
            if (channels[channel] == nullptr)
                continue;
            dry[static_cast<std::size_t>(channel)] = channels[channel][sample];
            auto value = dry[static_cast<std::size_t>(channel)] * inputGain;
            value = processFilter(
                value,
                highPassCoefficients,
                highPassState_[static_cast<std::size_t>(channel)]);
            value = processFilter(
                value,
                lowShelfCoefficients,
                lowShelfState_[static_cast<std::size_t>(channel)]);
            value = processFilter(
                value,
                presenceCoefficients,
                presenceState_[static_cast<std::size_t>(channel)]);
            value = processFilter(
                value,
                highShelfCoefficients,
                highShelfState_[static_cast<std::size_t>(channel)]);
            filtered[static_cast<std::size_t>(channel)] = value;
            detector = std::max(detector, std::abs(value));
        }

        const auto coefficient = detector > envelope_ ? attackCoefficient : releaseCoefficient;
        envelope_ = coefficient * envelope_ + (1.0 - coefficient) * detector;
        const auto levelDb = gainToDb(envelope_);
        const auto overDb = std::max(0.0, levelDb - settings_.compressor.thresholdDb);
        const auto reductionDb =
            -overDb * (1.0 - 1.0 / settings_.compressor.ratio);
        const auto compressionGain = dbToGain(reductionDb);

        for (int channel = 0; channel < activeChannels; ++channel) {
            if (channels[channel] == nullptr)
                continue;
            auto wet = filtered[static_cast<std::size_t>(channel)] * compressionGain;
            wet = std::tanh(wet * drive) * saturationNormalizer;
            wet = std::clamp(wet * outputGain, -ceiling, ceiling);
            channels[channel][sample] = static_cast<float>(
                dry[static_cast<std::size_t>(channel)]
                + settings_.amount * (wet - dry[static_cast<std::size_t>(channel)]));
        }
    }
}

} // namespace mastering::dsp
