#include "mastering/analysis/AudioAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
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
    return (power > 1.0e-12
                ? 10.0 * std::log10(power / static_cast<double>(highBin - lowBin))
                : silenceDb)
        - 20.0 * std::log10(static_cast<double>(fftSize));
}

void applyLoudnessReading(AudioMetrics& metrics, const LoudnessReading& reading)
{
    metrics.samplePeakDbfs = toDb(reading.samplePeakLinear);
    metrics.truePeakDbtp = toDb(reading.truePeakLinear);
    metrics.truePeakValid = reading.truePeakValid;
    metrics.estimatedTruePeakDbtp = metrics.truePeakDbtp;
    metrics.truePeakIsEstimate = false;
    metrics.momentaryLufs = reading.momentaryLufs;
    metrics.momentaryLufsIsValid = reading.momentaryValid;
    metrics.shortTermLufs = reading.shortTermLufs;
    metrics.shortTermLufsIsValid = reading.shortTermValid;
    metrics.integratedLufs = reading.integratedLufs;
    metrics.integratedLufsIsValid = reading.integratedValid;
    metrics.integratedLufsIsProvisional = reading.integratedProvisional;
    metrics.loudnessRangeLu = reading.loudnessRangeLu;
    metrics.loudnessRangeIsValid = reading.loudnessRangeValid;
    metrics.momentaryState = metricAvailabilityToString(reading.momentaryState);
    metrics.shortTermState = metricAvailabilityToString(reading.shortTermState);
    metrics.integratedState = metricAvailabilityToString(reading.integratedState);
    metrics.truePeakState = metricAvailabilityToString(reading.truePeakState);
    metrics.droppedAnalysisFrames = reading.droppedAnalysisFrames;
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

    LoudnessMeter meter;
    meter.prepare(sampleRate, static_cast<int>(channels.size()), 1024);
    std::vector<const float*> pointers(channels.size());
    for (std::size_t channel = 0; channel < channels.size(); ++channel)
        pointers[channel] = channels[channel].data();

    constexpr int chunk = 512;
    for (std::size_t start = 0; start < frameCount; start += static_cast<std::size_t>(chunk)) {
        const auto count = static_cast<int>(
            std::min(frameCount - start, static_cast<std::size_t>(chunk)));
        std::vector<const float*> offsetPointers(channels.size());
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            offsetPointers[channel] = pointers[channel] + start;
        meter.process(offsetPointers.data(), static_cast<int>(channels.size()), count);
    }
    meter.finalize();
    applyLoudnessReading(result, meter.snapshot());

    double sumSquares = 0.0;
    for (const auto& channel : channels)
        for (std::size_t frame = 0; frame < frameCount; ++frame)
            sumSquares += static_cast<double>(channel[frame]) * channel[frame];
    const auto rms = std::sqrt(
        sumSquares / static_cast<double>(frameCount * channels.size()));
    result.rmsDbfs = toDb(rms);
    result.estimatedLoudnessDb = result.rmsDbfs;
    result.estimatedLoudnessIsValid = true;
    result.crestFactorDb = result.samplePeakDbfs - result.rmsDbfs;

    const auto mono = makeMono(channels);
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
    loudness_.prepare(sampleRate, 2, 4096);
    reset();
}

void RealtimeMeter::prepare(double sampleRate, int maximumBlockSize) noexcept
{
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    loudness_.prepare(sampleRate, 2, maximumBlockSize);
    reset();
}

void RealtimeMeter::reset() noexcept
{
    loudness_.reset();
    leftRightProduct_.store(0.0, std::memory_order_relaxed);
    leftSquares_.store(0.0, std::memory_order_relaxed);
    rightSquares_.store(0.0, std::memory_order_relaxed);
    sumSquares_.store(0.0, std::memory_order_relaxed);
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

    loudness_.process(channels, channelCount, sampleCount);

    double blockSquares = 0.0;
    double product = 0.0;
    double leftPower = 0.0;
    double rightPower = 0.0;
    for (int channel = 0; channel < channelCount; ++channel) {
        if (channels[channel] == nullptr)
            continue;
        for (int sample = 0; sample < sampleCount; ++sample) {
            const auto value = static_cast<double>(channels[channel][sample]);
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

void RealtimeMeter::noteDroppedAnalysisFrames(std::uint64_t count) noexcept
{
    loudness_.noteDroppedAnalysisFrames(count);
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

    applyLoudnessReading(metrics, loudness_.snapshot());
    metrics.rmsDbfs = toDb(rms);
    metrics.estimatedLoudnessDb = metrics.rmsDbfs;
    metrics.estimatedLoudnessIsValid = true;
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
           << R"(,"rmsDbfs":)" << metrics.rmsDbfs
           << R"(,"estimatedLoudnessDb":)" << metrics.estimatedLoudnessDb
           << R"(,"estimatedLoudnessIsValid":)" << (metrics.estimatedLoudnessIsValid ? "true" : "false")
           << R"(,"crestFactorDb":)" << metrics.crestFactorDb
           << R"(,"stereoCorrelation":)" << metrics.stereoCorrelation
           << R"(,"transientDensityHz":)" << metrics.transientDensityHz
           << R"(,"durationSeconds":)" << metrics.durationSeconds
           << R"(,"sampleRate":)" << metrics.sampleRate
           << R"(,"channels":)" << metrics.channels
           << R"(,"truePeakValid":)" << (metrics.truePeakValid ? "true" : "false")
           << R"(,"truePeakIsEstimate":)" << (metrics.truePeakIsEstimate ? "true" : "false")
           << R"(,"momentaryLufsIsValid":)" << (metrics.momentaryLufsIsValid ? "true" : "false")
           << R"(,"shortTermLufsIsValid":)" << (metrics.shortTermLufsIsValid ? "true" : "false")
           << R"(,"integratedLufsIsValid":)" << (metrics.integratedLufsIsValid ? "true" : "false")
           << R"(,"integratedLufsIsProvisional":)" << (metrics.integratedLufsIsProvisional ? "true" : "false")
           << R"(,"loudnessRangeIsValid":)" << (metrics.loudnessRangeIsValid ? "true" : "false")
           << R"(,"momentaryState":")" << metrics.momentaryState << '"'
           << R"(,"shortTermState":")" << metrics.shortTermState << '"'
           << R"(,"integratedState":")" << metrics.integratedState << '"'
           << R"(,"truePeakState":")" << metrics.truePeakState << '"'
           << R"(,"droppedAnalysisFrames":)" << metrics.droppedAnalysisFrames;
    if (metrics.truePeakValid)
        output << R"(,"truePeakDbtp":)" << metrics.truePeakDbtp;
    if (metrics.truePeakIsEstimate)
        output << R"(,"estimatedTruePeakDbtp":)" << metrics.estimatedTruePeakDbtp;
    if (metrics.momentaryLufsIsValid)
        output << R"(,"momentaryLufs":)" << metrics.momentaryLufs;
    if (metrics.shortTermLufsIsValid)
        output << R"(,"shortTermLufs":)" << metrics.shortTermLufs;
    if (metrics.integratedLufsIsValid)
        output << R"(,"integratedLufs":)" << metrics.integratedLufs;
    if (metrics.loudnessRangeIsValid)
        output << R"(,"loudnessRangeLu":)" << metrics.loudnessRangeLu;
    output << R"(,"spectrum":{"subDb":)" << metrics.spectrum.subDb
           << R"(,"bassDb":)" << metrics.spectrum.bassDb
           << R"(,"lowMidDb":)" << metrics.spectrum.lowMidDb
           << R"(,"midDb":)" << metrics.spectrum.midDb
           << R"(,"presenceDb":)" << metrics.spectrum.presenceDb
           << R"(,"airDb":)" << metrics.spectrum.airDb << "}}";
    return output.str();
}

bool jsonClaimsLufsForEstimate(std::string_view json)
{
    const auto hasLufsField = json.find("\"integratedLufs\"") != std::string_view::npos;
    const auto markedValid = json.find("\"integratedLufsIsValid\":true") != std::string_view::npos;
    return hasLufsField && !markedValid;
}

bool jsonClaimsTruePeakWithoutEstimate(std::string_view json)
{
    // New path publishes truePeakDbtp with truePeakValid.
    if (json.find("\"truePeakDbtp\"") != std::string_view::npos
        && json.find("\"truePeakValid\":true") == std::string_view::npos) {
        return true;
    }
    const auto hasTpField = json.find("\"estimatedTruePeakDbtp\"") != std::string_view::npos;
    const auto markedEstimate = json.find("\"truePeakIsEstimate\":true") != std::string_view::npos;
    return hasTpField && !markedEstimate;
}

} // namespace mastering::analysis
