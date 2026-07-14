#include "mastering/analysis/AudioAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <limits>
#include <numeric>
#include <ranges>
#include <sstream>

namespace mastering::analysis {
namespace {

constexpr double silenceDb = -120.0;
constexpr double pi = 3.14159265358979323846;

double toDb(double value) noexcept
{
    return value > 1.0e-12 ? 20.0 * std::log10(value) : silenceDb;
}

double powerToDb(double value) noexcept
{
    return value > 1.0e-12 ? 10.0 * std::log10(value) : silenceDb;
}

std::vector<float> makeMono(const std::vector<std::vector<float>>& channels)
{
    if (channels.empty())
        return {};

    const auto frameCount = std::ranges::min(
        channels | std::views::transform([](const auto& channel) { return channel.size(); }));
    std::vector<float> mono(frameCount, 0.0f);

    for (const auto& channel : channels)
        for (std::size_t frame = 0; frame < frameCount; ++frame)
            mono[frame] += channel[frame] / static_cast<float>(channels.size());

    return mono;
}

struct Biquad {
    double b0 {1.0};
    double b1 {0.0};
    double b2 {0.0};
    double a1 {0.0};
    double a2 {0.0};
    double z1 {0.0};
    double z2 {0.0};

    double process(double input) noexcept
    {
        const auto output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }
};

Biquad highPass(double sampleRate, double frequency, double q)
{
    const auto omega = 2.0 * pi * frequency / sampleRate;
    const auto alpha = std::sin(omega) / (2.0 * q);
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

Biquad highShelf(double sampleRate, double frequency, double gainDb, double q)
{
    const auto amplitude = std::pow(10.0, gainDb / 40.0);
    const auto omega = 2.0 * pi * frequency / sampleRate;
    const auto cosOmega = std::cos(omega);
    const auto sinOmega = std::sin(omega);
    const auto alpha = sinOmega / (2.0 * q);
    const auto beta = 2.0 * std::sqrt(amplitude) * alpha;
    const auto a0 = (amplitude + 1.0) - (amplitude - 1.0) * cosOmega + beta;

    return {
        amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosOmega + beta) / a0,
        -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosOmega) / a0,
        amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosOmega - beta) / a0,
        2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosOmega) / a0,
        ((amplitude + 1.0) - (amplitude - 1.0) * cosOmega - beta) / a0
    };
}

double integratedLufs(const std::vector<std::vector<float>>& channels, double sampleRate)
{
    if (channels.empty() || sampleRate <= 0.0)
        return silenceDb;

    const auto frameCount = std::ranges::min(
        channels | std::views::transform([](const auto& channel) { return channel.size(); }));
    const auto blockFrames = static_cast<std::size_t>(std::max(1.0, sampleRate * 0.4));
    const auto hopFrames = std::max<std::size_t>(1, blockFrames / 4);
    if (frameCount < blockFrames)
        return silenceDb;

    std::vector<std::vector<double>> weighted(channels.size(), std::vector<double>(frameCount));
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        auto shelf = highShelf(sampleRate, 1'681.974, 4.0, 0.707);
        auto filter = highPass(sampleRate, 38.135, 0.500);
        for (std::size_t frame = 0; frame < frameCount; ++frame)
            weighted[channel][frame] = filter.process(shelf.process(channels[channel][frame]));
    }

    std::vector<double> blockPowers;
    for (std::size_t start = 0; start + blockFrames <= frameCount; start += hopFrames) {
        double power = 0.0;
        for (const auto& channel : weighted) {
            double channelPower = 0.0;
            for (std::size_t frame = start; frame < start + blockFrames; ++frame)
                channelPower += channel[frame] * channel[frame];
            power += channelPower / static_cast<double>(blockFrames);
        }
        if (-0.691 + powerToDb(power) >= -70.0)
            blockPowers.push_back(power);
    }

    if (blockPowers.empty())
        return silenceDb;

    const auto ungatedPower =
        std::reduce(blockPowers.begin(), blockPowers.end()) / static_cast<double>(blockPowers.size());
    const auto relativeGate = -0.691 + powerToDb(ungatedPower) - 10.0;

    double gatedPower = 0.0;
    std::size_t gatedCount = 0;
    for (const auto power : blockPowers) {
        if (-0.691 + powerToDb(power) >= relativeGate) {
            gatedPower += power;
            ++gatedCount;
        }
    }

    return gatedCount == 0
        ? silenceDb
        : -0.691 + powerToDb(gatedPower / static_cast<double>(gatedCount));
}

double cubicInterpolate(double y0, double y1, double y2, double y3, double position)
{
    const auto a0 = y3 - y2 - y0 + y1;
    const auto a1 = y0 - y1 - a0;
    const auto a2 = y2 - y0;
    return a0 * position * position * position + a1 * position * position + a2 * position + y1;
}

double estimatedTruePeak(const std::vector<std::vector<float>>& channels)
{
    double peak = 0.0;
    for (const auto& channel : channels) {
        if (channel.size() < 4)
            continue;
        for (std::size_t frame = 1; frame + 2 < channel.size(); ++frame) {
            peak = std::max(peak, std::abs(static_cast<double>(channel[frame])));
            for (const auto fraction : {0.25, 0.5, 0.75}) {
                peak = std::max(peak, std::abs(cubicInterpolate(
                    channel[frame - 1],
                    channel[frame],
                    channel[frame + 1],
                    channel[frame + 2],
                    fraction)));
            }
        }
    }
    return peak;
}

double transientDensity(std::span<const float> mono, double sampleRate)
{
    const auto frameSize = static_cast<std::size_t>(std::max(1.0, sampleRate * 0.01));
    if (mono.size() < frameSize * 2)
        return 0.0;

    double previous = 1.0e-9;
    std::size_t transients = 0;
    for (std::size_t start = 0; start + frameSize <= mono.size(); start += frameSize) {
        double power = 0.0;
        for (std::size_t index = start; index < start + frameSize; ++index)
            power += static_cast<double>(mono[index]) * mono[index];
        const auto rms = std::sqrt(power / static_cast<double>(frameSize));
        if (rms > previous * 1.8 && rms > 0.001)
            ++transients;
        previous = 0.75 * previous + 0.25 * rms;
    }

    const auto duration = static_cast<double>(mono.size()) / sampleRate;
    return duration > 0.0 ? static_cast<double>(transients) / duration : 0.0;
}

void fft(std::vector<std::complex<double>>& values)
{
    const auto size = values.size();
    for (std::size_t index = 1, reversed = 0; index < size; ++index) {
        auto bit = size >> 1;
        for (; reversed & bit; bit >>= 1)
            reversed ^= bit;
        reversed ^= bit;
        if (index < reversed)
            std::swap(values[index], values[reversed]);
    }

    for (std::size_t length = 2; length <= size; length <<= 1) {
        const auto angle = -2.0 * pi / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < size; start += length) {
            std::complex<double> rotation(1.0, 0.0);
            for (std::size_t offset = 0; offset < length / 2; ++offset) {
                const auto even = values[start + offset];
                const auto odd = values[start + offset + length / 2] * rotation;
                values[start + offset] = even + odd;
                values[start + offset + length / 2] = even - odd;
                rotation *= step;
            }
        }
    }
}

double bandLevel(
    const std::vector<std::complex<double>>& bins,
    double sampleRate,
    double lowFrequency,
    double highFrequency)
{
    const auto fftSize = bins.size();
    const auto lowBin = static_cast<std::size_t>(lowFrequency * fftSize / sampleRate);
    const auto highBin = std::min(
        fftSize / 2,
        static_cast<std::size_t>(std::ceil(highFrequency * fftSize / sampleRate)));
    if (lowBin >= highBin)
        return silenceDb;

    double power = 0.0;
    for (auto bin = lowBin; bin < highBin; ++bin)
        power += std::norm(bins[bin]);
    return powerToDb(power / static_cast<double>(highBin - lowBin)) - 20.0 * std::log10(fftSize);
}

} // namespace

AudioMetrics AudioAnalyzer::analyze(
    const std::vector<std::vector<float>>& channels,
    double sampleRate) const
{
    AudioMetrics result;
    if (channels.empty() || sampleRate <= 0.0)
        return result;

    const auto frameCount = std::ranges::min(
        channels | std::views::transform([](const auto& channel) { return channel.size(); }));
    if (frameCount == 0)
        return result;

    double peak = 0.0;
    double sumSquares = 0.0;
    for (const auto& channel : channels) {
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            peak = std::max(peak, std::abs(static_cast<double>(channel[frame])));
            sumSquares += static_cast<double>(channel[frame]) * channel[frame];
        }
    }

    const auto rms = std::sqrt(
        sumSquares / static_cast<double>(frameCount * channels.size()));
    const auto mono = makeMono(channels);

    result.samplePeakDbfs = toDb(peak);
    result.estimatedTruePeakDbtp = toDb(estimatedTruePeak(channels));
    result.rmsDbfs = toDb(rms);
    result.integratedLufs = integratedLufs(channels, sampleRate);
    result.crestFactorDb = result.samplePeakDbfs - result.rmsDbfs;
    result.transientDensityHz = transientDensity(mono, sampleRate);
    result.spectrum = calculateSpectrum(mono, sampleRate);
    result.durationSeconds = static_cast<double>(frameCount) / sampleRate;
    result.sampleRate = static_cast<int>(std::lround(sampleRate));
    result.channels = channels.size();

    if (channels.size() >= 2) {
        double product = 0.0;
        double leftPower = 0.0;
        double rightPower = 0.0;
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            product += channels[0][frame] * channels[1][frame];
            leftPower += channels[0][frame] * channels[0][frame];
            rightPower += channels[1][frame] * channels[1][frame];
        }
        const auto denominator = std::sqrt(leftPower * rightPower);
        result.stereoCorrelation = denominator > 1.0e-12 ? product / denominator : 1.0;
    }

    return result;
}

SpectrumProfile AudioAnalyzer::calculateSpectrum(std::span<const float> mono, double sampleRate)
{
    constexpr std::size_t fftSize = 4'096;
    std::vector<std::complex<double>> bins(fftSize);
    if (mono.empty())
        return {};

    const auto available = std::min(fftSize, mono.size());
    const auto start = mono.size() > available ? (mono.size() - available) / 2 : 0;
    for (std::size_t index = 0; index < available; ++index) {
        const auto window = 0.5 - 0.5 * std::cos(
            2.0 * pi * static_cast<double>(index) / static_cast<double>(fftSize - 1));
        bins[index] = mono[start + index] * window;
    }
    fft(bins);

    return {
        bandLevel(bins, sampleRate, 20.0, 60.0),
        bandLevel(bins, sampleRate, 60.0, 250.0),
        bandLevel(bins, sampleRate, 250.0, 500.0),
        bandLevel(bins, sampleRate, 500.0, 2'000.0),
        bandLevel(bins, sampleRate, 2'000.0, 8'000.0),
        bandLevel(bins, sampleRate, 8'000.0, 20'000.0)
    };
}

void RealtimeMeter::prepare(double sampleRate) noexcept
{
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    reset();
}

void RealtimeMeter::reset() noexcept
{
    peak_.store(0.0, std::memory_order_relaxed);
    sumSquares_.store(0.0, std::memory_order_relaxed);
    leftRightProduct_.store(0.0, std::memory_order_relaxed);
    leftSquares_.store(0.0, std::memory_order_relaxed);
    rightSquares_.store(0.0, std::memory_order_relaxed);
    transientCount_.store(0.0, std::memory_order_relaxed);
    previousEnvelope_.store(0.0, std::memory_order_relaxed);
    sampleCount_.store(0, std::memory_order_relaxed);
    channelCount_.store(0, std::memory_order_relaxed);
}

void RealtimeMeter::process(
    const float* const* channels,
    int channelCount,
    int sampleCount) noexcept
{
    if (channels == nullptr || channelCount <= 0 || sampleCount <= 0)
        return;

    double blockPeak = 0.0;
    double blockSquares = 0.0;
    double product = 0.0;
    double leftPower = 0.0;
    double rightPower = 0.0;

    for (int channel = 0; channel < channelCount; ++channel) {
        if (channels[channel] == nullptr)
            continue;
        for (int sample = 0; sample < sampleCount; ++sample) {
            const auto value = static_cast<double>(channels[channel][sample]);
            blockPeak = std::max(blockPeak, std::abs(value));
            blockSquares += value * value;
        }
    }

    if (channelCount >= 2 && channels[0] != nullptr && channels[1] != nullptr) {
        for (int sample = 0; sample < sampleCount; ++sample) {
            const auto left = static_cast<double>(channels[0][sample]);
            const auto right = static_cast<double>(channels[1][sample]);
            product += left * right;
            leftPower += left * left;
            rightPower += right * right;
        }
    }

    auto currentPeak = peak_.load(std::memory_order_relaxed);
    while (blockPeak > currentPeak
           && !peak_.compare_exchange_weak(currentPeak, blockPeak, std::memory_order_relaxed)) {
    }
    sumSquares_.fetch_add(blockSquares, std::memory_order_relaxed);
    leftRightProduct_.fetch_add(product, std::memory_order_relaxed);
    leftSquares_.fetch_add(leftPower, std::memory_order_relaxed);
    rightSquares_.fetch_add(rightPower, std::memory_order_relaxed);

    const auto blockRms = std::sqrt(
        blockSquares / static_cast<double>(sampleCount * channelCount));
    const auto previous = previousEnvelope_.exchange(
        0.8 * previousEnvelope_.load(std::memory_order_relaxed) + 0.2 * blockRms,
        std::memory_order_relaxed);
    if (blockRms > previous * 1.8 && blockRms > 0.001)
        transientCount_.fetch_add(1.0, std::memory_order_relaxed);

    sampleCount_.fetch_add(static_cast<std::uint64_t>(sampleCount), std::memory_order_relaxed);
    channelCount_.store(channelCount, std::memory_order_relaxed);
}

AudioMetrics RealtimeMeter::snapshot() const noexcept
{
    AudioMetrics metrics;
    const auto samples = sampleCount_.load(std::memory_order_relaxed);
    const auto channels = channelCount_.load(std::memory_order_relaxed);
    const auto sampleRate = sampleRate_.load(std::memory_order_relaxed);
    const auto totalValues = static_cast<double>(samples) * std::max(1, channels);
    const auto rms = totalValues > 0.0
        ? std::sqrt(sumSquares_.load(std::memory_order_relaxed) / totalValues)
        : 0.0;

    metrics.samplePeakDbfs = toDb(peak_.load(std::memory_order_relaxed));
    metrics.estimatedTruePeakDbtp = metrics.samplePeakDbfs;
    metrics.rmsDbfs = toDb(rms);
    metrics.integratedLufs = metrics.rmsDbfs - 0.691;
    metrics.crestFactorDb = metrics.samplePeakDbfs - metrics.rmsDbfs;
    metrics.durationSeconds = sampleRate > 0.0 ? static_cast<double>(samples) / sampleRate : 0.0;
    metrics.transientDensityHz = metrics.durationSeconds > 0.0
        ? transientCount_.load(std::memory_order_relaxed) / metrics.durationSeconds
        : 0.0;
    metrics.sampleRate = static_cast<int>(std::lround(sampleRate));
    metrics.channels = static_cast<std::size_t>(std::max(0, channels));

    const auto denominator = std::sqrt(
        leftSquares_.load(std::memory_order_relaxed)
        * rightSquares_.load(std::memory_order_relaxed));
    metrics.stereoCorrelation = denominator > 1.0e-12
        ? leftRightProduct_.load(std::memory_order_relaxed) / denominator
        : 1.0;
    return metrics;
}

std::string toJson(const AudioMetrics& metrics)
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(4)
           << R"({"samplePeakDbfs":)" << metrics.samplePeakDbfs
           << R"(,"estimatedTruePeakDbtp":)" << metrics.estimatedTruePeakDbtp
           << R"(,"rmsDbfs":)" << metrics.rmsDbfs
           << R"(,"integratedLufs":)" << metrics.integratedLufs
           << R"(,"crestFactorDb":)" << metrics.crestFactorDb
           << R"(,"stereoCorrelation":)" << metrics.stereoCorrelation
           << R"(,"transientDensityHz":)" << metrics.transientDensityHz
           << R"(,"durationSeconds":)" << metrics.durationSeconds
           << R"(,"sampleRate":)" << metrics.sampleRate
           << R"(,"channels":)" << metrics.channels
           << R"(,"spectrum":{"subDb":)" << metrics.spectrum.subDb
           << R"(,"bassDb":)" << metrics.spectrum.bassDb
           << R"(,"lowMidDb":)" << metrics.spectrum.lowMidDb
           << R"(,"midDb":)" << metrics.spectrum.midDb
           << R"(,"presenceDb":)" << metrics.spectrum.presenceDb
           << R"(,"airDb":)" << metrics.spectrum.airDb << "}}";
    return output.str();
}

} // namespace mastering::analysis
