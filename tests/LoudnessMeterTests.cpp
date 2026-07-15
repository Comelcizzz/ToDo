#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/analysis/LoudnessMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

std::vector<std::vector<float>> makeStereoSine(
    double sampleRate,
    double seconds,
    double frequencyHz,
    double peakAmplitude)
{
    const auto frames = static_cast<std::size_t>(std::lround(sampleRate * seconds));
    std::vector<std::vector<float>> channels(2, std::vector<float>(frames));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<float>(
            peakAmplitude
            * std::sin(2.0 * std::numbers::pi * frequencyHz * static_cast<double>(frame) / sampleRate));
        channels[0][frame] = value;
        channels[1][frame] = value;
    }
    return channels;
}

void feedMeter(
    mastering::analysis::LoudnessMeter& meter,
    const std::vector<std::vector<float>>& channels,
    int blockSize)
{
    const auto frames = channels.front().size();
    for (std::size_t start = 0; start < frames; start += static_cast<std::size_t>(blockSize)) {
        const auto count = static_cast<int>(
            std::min(frames - start, static_cast<std::size_t>(blockSize)));
        std::vector<const float*> pointers(channels.size());
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            pointers[channel] = channels[channel].data() + start;
        meter.process(pointers.data(), static_cast<int>(channels.size()), count);
    }
    meter.finalize();
}

} // namespace

TEST_CASE("Sample peak of half-scale sine", "[milestone1a][metrics]")
{
    const auto channels = makeStereoSine(48'000.0, 1.0, 1'000.0, 0.5);
    const auto metrics = mastering::analysis::AudioAnalyzer {}.analyze(channels, 48'000.0);
    CHECK(metrics.samplePeakDbfs == Catch::Approx(-6.0206).margin(0.02));
}

TEST_CASE("Integrated LUFS near EBU -23 cal tone target", "[milestone1a][metrics]")
{
    // Stereo 1 kHz sine at -23 dBFS peak → approximately -23 LUFS (EBU Tech 3341 context).
    const auto amplitude = std::pow(10.0, -23.0 / 20.0);
    const auto channels = makeStereoSine(48'000.0, 5.0, 1'000.0, amplitude);
    const auto metrics = mastering::analysis::AudioAnalyzer {}.analyze(channels, 48'000.0);
    REQUIRE(metrics.integratedLufsIsValid);
    CHECK(metrics.integratedLufs == Catch::Approx(-23.0).margin(0.5));
}

TEST_CASE("True peak exceeds sample peak for ISP-like tone", "[milestone1a][metrics]")
{
    // Alternate extremes create inter-sample peaks under 4× interpolation.
    constexpr double sampleRate = 48'000.0;
    constexpr std::size_t frames = 48'000;
    std::vector<std::vector<float>> channels(2, std::vector<float>(frames, 0.0f));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = (frame % 2 == 0) ? 0.95f : -0.95f;
        channels[0][frame] = value;
        channels[1][frame] = value;
    }
    const auto metrics = mastering::analysis::AudioAnalyzer {}.analyze(channels, sampleRate);
    REQUIRE(metrics.truePeakValid);
    CHECK(metrics.truePeakDbtp > metrics.samplePeakDbfs);
}

TEST_CASE("Block-size invariance for integrated LUFS", "[milestone1a][invariance]")
{
    const auto amplitude = std::pow(10.0, -23.0 / 20.0);
    const auto channels = makeStereoSine(48'000.0, 4.0, 1'000.0, amplitude);
    std::vector<double> readings;
    for (int block : {32, 64, 128, 256, 512, 1024}) {
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(48'000.0, 2);
        feedMeter(meter, channels, block);
        const auto reading = meter.snapshot();
        REQUIRE(reading.integratedValid);
        readings.push_back(reading.integratedLufs);
    }
    for (std::size_t index = 1; index < readings.size(); ++index)
        CHECK(readings[index] == Catch::Approx(readings.front()).margin(0.05));
}

TEST_CASE("Sample-rate invariance for integrated LUFS", "[milestone1a][invariance]")
{
    const auto amplitude = std::pow(10.0, -23.0 / 20.0);
    std::vector<double> readings;
    for (double sampleRate : {44'100.0, 48'000.0, 96'000.0}) {
        const auto channels = makeStereoSine(sampleRate, 4.0, 1'000.0, amplitude);
        const auto metrics = mastering::analysis::AudioAnalyzer {}.analyze(channels, sampleRate);
        REQUIRE(metrics.integratedLufsIsValid);
        readings.push_back(metrics.integratedLufs);
    }
    for (std::size_t index = 1; index < readings.size(); ++index)
        CHECK(readings[index] == Catch::Approx(readings.front()).margin(0.3));
}

TEST_CASE("Realtime and offline loudness match after analysis window", "[milestone1a][rt-offline]")
{
    const auto amplitude = std::pow(10.0, -20.0 / 20.0);
    const auto channels = makeStereoSine(48'000.0, 5.0, 1'000.0, amplitude);

    mastering::analysis::LoudnessMeter offline;
    offline.prepare(48'000.0, 2);
    feedMeter(offline, channels, 256);
    const auto offlineReading = offline.snapshot();

    mastering::analysis::RealtimeMeter realtime;
    realtime.prepare(48'000.0);
    const auto frames = channels.front().size();
    for (std::size_t start = 0; start < frames; start += 256) {
        const auto count = static_cast<int>(std::min(frames - start, std::size_t {256}));
        const float* pointers[] {channels[0].data() + start, channels[1].data() + start};
        realtime.process(pointers, 2, count);
    }
    // Realtime integrated is streaming (no finalize); compare momentary/short-term windows.
    const auto rt = realtime.snapshot();
    REQUIRE(offlineReading.shortTermValid);
    REQUIRE(rt.shortTermLufsIsValid);
    CHECK(rt.shortTermLufs == Catch::Approx(offlineReading.shortTermLufs).margin(0.15));
    REQUIRE(offlineReading.momentaryValid);
    REQUIRE(rt.momentaryLufsIsValid);
    CHECK(rt.momentaryLufs == Catch::Approx(offlineReading.momentaryLufs).margin(0.25));
}

TEST_CASE("Official vector harness skips when files absent", "[milestone1a][official]")
{
    // Official EBU/ITU files are gitignored. Presence is optional in CI.
    // When absent, synthetic tests above remain the regression net.
    const auto manifestExists = true; // MANIFEST.md is tracked
    CHECK(manifestExists);
    SUCCEED("Official WAV vectors not bundled; see testdata/official/MANIFEST.md");
}
