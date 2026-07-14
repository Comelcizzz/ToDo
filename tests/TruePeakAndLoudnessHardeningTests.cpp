#include "mastering/analysis/LoudnessMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

void feed(
    mastering::analysis::LoudnessMeter& meter,
    const std::vector<std::vector<float>>& channels,
    int blockSize)
{
    const auto frames = channels.front().size();
    for (std::size_t start = 0; start < frames;) {
        const auto count = static_cast<int>(
            std::min(frames - start, static_cast<std::size_t>(std::max(1, blockSize))));
        std::vector<const float*> pointers(channels.size());
        for (std::size_t channel = 0; channel < channels.size(); ++channel)
            pointers[channel] = channels[channel].data() + start;
        meter.process(pointers.data(), static_cast<int>(channels.size()), count);
        start += static_cast<std::size_t>(count);
    }
}

std::vector<std::vector<float>> zeros(std::size_t channels, std::size_t frames)
{
    return {channels, std::vector<float>(frames, 0.0f)};
}

} // namespace

TEST_CASE("True peak continuity across atypical block sizes", "[milestone1a][truepeak]")
{
    constexpr double sampleRate = 48'000.0;
    // Phase-shifted near-Nyquist-ish tone that can produce ISP.
    constexpr std::size_t frames = 8'192;
    std::vector<std::vector<float>> stereo(2, std::vector<float>(frames));
    for (std::size_t i = 0; i < frames; ++i) {
        const auto t = static_cast<double>(i) / sampleRate;
        const auto value = static_cast<float>(0.9 * std::sin(2.0 * std::numbers::pi * 12'000.0 * t + 0.7));
        stereo[0][i] = value;
        stereo[1][i] = value;
    }

    double reference = 0.0;
    {
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(sampleRate, 2, 4096);
        feed(meter, stereo, 512);
        meter.finalize();
        reference = meter.snapshot().truePeakLinear;
        REQUIRE(meter.snapshot().truePeakValid);
    }

    for (int block : {1, 7, 16, 32, 64, 127, 128, 256, 511, 512, 1024}) {
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(sampleRate, 2, 4096);
        feed(meter, stereo, block);
        meter.finalize();
        CHECK(meter.snapshot().truePeakLinear == Catch::Approx(reference).margin(1.0e-5));
    }
}

TEST_CASE("True peak sample rates matrix", "[milestone1a][truepeak]")
{
    for (double sampleRate : {44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0}) {
        const auto frames = static_cast<std::size_t>(sampleRate * 0.25);
        std::vector<std::vector<float>> mono(1, std::vector<float>(frames));
        for (std::size_t i = 0; i < frames; ++i) {
            mono[0][i] = (i % 2 == 0) ? 0.95f : -0.95f;
        }
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(sampleRate, 1, 2048);
        feed(meter, mono, 128);
        meter.finalize();
        const auto reading = meter.snapshot();
        REQUIRE(reading.truePeakValid);
        CHECK(reading.truePeakLinear >= reading.samplePeakLinear - 1.0e-6);
    }
}

TEST_CASE("True peak edge cases", "[milestone1a][truepeak]")
{
    SECTION("silence")
    {
        auto channels = zeros(2, 2048);
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(48'000.0, 2, 1024);
        feed(meter, channels, 256);
        meter.finalize();
        CHECK(meter.snapshot().samplePeakLinear == Catch::Approx(0.0).margin(1.0e-12));
        CHECK(meter.snapshot().truePeakLinear == Catch::Approx(0.0).margin(1.0e-4));
    }

    SECTION("peak in first samples")
    {
        auto channels = zeros(1, 2048);
        channels[0][0] = 0.8f;
        channels[0][1] = -0.8f;
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(48'000.0, 1, 1024);
        feed(meter, channels, 64);
        meter.finalize();
        CHECK(meter.snapshot().samplePeakLinear == Catch::Approx(0.8).margin(1.0e-6));
        CHECK(meter.snapshot().truePeakLinear >= 0.8 - 1.0e-4);
    }

    SECTION("peak in last samples")
    {
        auto channels = zeros(1, 2048);
        channels[0][2046] = 0.75f;
        channels[0][2047] = -0.75f;
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(48'000.0, 1, 1024);
        feed(meter, channels, 100);
        meter.finalize();
        CHECK(meter.snapshot().samplePeakLinear == Catch::Approx(0.75).margin(1.0e-6));
    }

    SECTION("stereo peak only in one channel")
    {
        auto channels = zeros(2, 4096);
        for (std::size_t i = 100; i < 120; ++i)
            channels[1][i] = (i % 2 == 0) ? 0.9f : -0.9f;
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(48'000.0, 2, 1024);
        feed(meter, channels, 33);
        meter.finalize();
        CHECK(meter.snapshot().samplePeakLinear == Catch::Approx(0.9).margin(1.0e-6));
        CHECK(meter.snapshot().truePeakLinear >= meter.snapshot().samplePeakLinear - 1.0e-5);
    }

    SECTION("positive and negative peaks")
    {
        auto channels = zeros(1, 1024);
        channels[0][200] = 0.6f;
        channels[0][400] = -0.7f;
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(48'000.0, 1, 512);
        feed(meter, channels, 64);
        meter.finalize();
        CHECK(meter.snapshot().samplePeakLinear == Catch::Approx(0.7).margin(1.0e-6));
    }

    SECTION("reset between files")
    {
        auto loud = zeros(1, 2048);
        for (auto& s : loud[0])
            s = 0.5f;
        auto quiet = zeros(1, 2048);
        mastering::analysis::LoudnessMeter meter;
        meter.prepare(48'000.0, 1, 1024);
        feed(meter, loud, 128);
        meter.finalize();
        REQUIRE(meter.snapshot().samplePeakLinear == Catch::Approx(0.5).margin(1.0e-6));
        meter.reset();
        feed(meter, quiet, 128);
        meter.finalize();
        CHECK(meter.snapshot().samplePeakLinear == Catch::Approx(0.0).margin(1.0e-12));
    }

    SECTION("peak spanning two blocks")
    {
        auto channels = zeros(1, 512);
        channels[0][255] = 0.85f;
        channels[0][256] = -0.85f;
        mastering::analysis::LoudnessMeter a;
        a.prepare(48'000.0, 1, 512);
        feed(a, channels, 256);
        a.finalize();
        mastering::analysis::LoudnessMeter b;
        b.prepare(48'000.0, 1, 512);
        feed(b, channels, 512);
        b.finalize();
        CHECK(a.snapshot().truePeakLinear == Catch::Approx(b.snapshot().truePeakLinear).margin(1.0e-5));
    }
}

TEST_CASE("K-weighting frequency response key points", "[milestone1a][kweight]")
{
    mastering::analysis::LoudnessMeter meter;
    meter.prepare(48'000.0, 2, 1024);
    // High-pass attenuates deep bass; shelf boosts presence region.
    const auto at25 = meter.measureKWeightMagnitudeDb(25.0);
    const auto at1000 = meter.measureKWeightMagnitudeDb(1'000.0);
    const auto at3000 = meter.measureKWeightMagnitudeDb(3'000.0);
    CHECK(at25 < at1000 - 5.0);
    CHECK(at3000 > at1000);
}

TEST_CASE("Momentary and short-term validity windows", "[milestone1a][windows]")
{
    mastering::analysis::LoudnessMeter meter;
    meter.prepare(48'000.0, 2, 1024);
    auto channels = zeros(2, 48'000 * 4);
    for (std::size_t i = 0; i < channels[0].size(); ++i) {
        channels[0][i] = 0.1f;
        channels[1][i] = 0.1f;
    }

    // 200 ms — momentary still warming up
    {
        mastering::analysis::LoudnessMeter m;
        m.prepare(48'000.0, 2, 1024);
        std::vector<std::vector<float>> slice(2);
        slice[0].assign(channels[0].begin(), channels[0].begin() + 9'600);
        slice[1].assign(channels[1].begin(), channels[1].begin() + 9'600);
        feed(m, slice, 128);
        CHECK_FALSE(m.snapshot().momentaryValid);
        CHECK(m.snapshot().momentaryState == mastering::analysis::MetricAvailability::warmingUp);
        CHECK_FALSE(m.snapshot().shortTermValid);
    }

    // 400 ms+ — momentary valid, short-term still warming
    {
        mastering::analysis::LoudnessMeter m;
        m.prepare(48'000.0, 2, 1024);
        std::vector<std::vector<float>> slice(2);
        slice[0].assign(channels[0].begin(), channels[0].begin() + 20'000);
        slice[1].assign(channels[1].begin(), channels[1].begin() + 20'000);
        feed(m, slice, 256);
        CHECK(m.snapshot().momentaryValid);
        CHECK_FALSE(m.snapshot().shortTermValid);
        CHECK(m.snapshot().shortTermState == mastering::analysis::MetricAvailability::warmingUp);
    }

    // 3 s+ — short-term valid; integrated provisional until finalize
    {
        mastering::analysis::LoudnessMeter m;
        m.prepare(48'000.0, 2, 1024);
        feed(m, channels, 512);
        CHECK(m.snapshot().shortTermValid);
        if (m.snapshot().integratedValid) {
            CHECK(m.snapshot().integratedProvisional);
            CHECK(m.snapshot().integratedState == mastering::analysis::MetricAvailability::provisional);
        }
        m.finalize();
        if (m.snapshot().integratedValid) {
            CHECK_FALSE(m.snapshot().integratedProvisional);
            CHECK(m.snapshot().integratedState == mastering::analysis::MetricAvailability::valid);
        }
    }
}
