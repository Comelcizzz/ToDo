#include "mastering/analysis/AudioAnalyzer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

TEST_CASE("Audio analyzer reports stable sine metrics", "[analysis]")
{
    constexpr double sampleRate = 48'000.0;
    constexpr std::size_t frameCount = 48'000;
    std::vector<std::vector<float>> channels(2, std::vector<float>(frameCount));
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const auto value = static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * 1'000.0 * frame / sampleRate));
        channels[0][frame] = value;
        channels[1][frame] = value;
    }

    const mastering::analysis::AudioAnalyzer analyzer;
    const auto metrics = analyzer.analyze(channels, sampleRate);

    CHECK(metrics.samplePeakDbfs == Catch::Approx(-6.0206).margin(0.02));
    CHECK(metrics.rmsDbfs == Catch::Approx(-9.0309).margin(0.05));
    CHECK(metrics.crestFactorDb == Catch::Approx(3.0103).margin(0.05));
    CHECK(metrics.integratedLufs > -11.0);
    CHECK(metrics.integratedLufs < -6.0);
    CHECK(metrics.stereoCorrelation == Catch::Approx(1.0).margin(0.0001));
    CHECK(metrics.spectrum.midDb > metrics.spectrum.subDb + 20.0);
}

TEST_CASE("Realtime meter handles anti-correlated stereo", "[analysis]")
{
    constexpr int sampleCount = 512;
    std::vector<float> left(sampleCount, 0.25f);
    std::vector<float> right(sampleCount, -0.25f);
    const float* channels[] {left.data(), right.data()};

    mastering::analysis::RealtimeMeter meter;
    meter.prepare(48'000.0);
    meter.process(channels, 2, sampleCount);
    const auto metrics = meter.snapshot();

    CHECK(metrics.stereoCorrelation == Catch::Approx(-1.0).margin(0.0001));
    CHECK(metrics.samplePeakDbfs == Catch::Approx(-12.0412).margin(0.02));
}
