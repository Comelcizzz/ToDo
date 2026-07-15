#include "mastering/analysis/LoudnessMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {

void processAll(
    mastering::analysis::LoudnessMeter& meter,
    const std::vector<std::vector<float>>& channels,
    int blockSize)
{
    const auto frames = channels.front().size();
    std::vector<const float*> ptrs(channels.size());
    for (std::size_t start = 0; start < frames; start += static_cast<std::size_t>(blockSize)) {
        const auto n = static_cast<int>(std::min(frames - start, static_cast<std::size_t>(blockSize)));
        for (std::size_t c = 0; c < channels.size(); ++c)
            ptrs[c] = channels[c].data() + start;
        meter.process(ptrs.data(), static_cast<int>(channels.size()), n);
    }
}

std::vector<std::vector<float>> zeros(std::size_t frames, int channels = 1)
{
    return std::vector<std::vector<float>>(static_cast<std::size_t>(channels), std::vector<float>(frames, 0.0f));
}

} // namespace

TEST_CASE("FIR: impulse on first sample produces reconstructed peak after finalize", "[milestone1a][fir-tail]")
{
    constexpr double sr = 48'000.0;
    auto channels = zeros(2048);
    channels[0][0] = 1.0f;

    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sr, 1, 512);
    processAll(meter, channels, 256);
    meter.finalize();
    const auto r = meter.snapshot();
    REQUIRE(r.truePeakValid);
    CHECK(r.truePeakLinear == Catch::Approx(1.0).margin(0.15));
    CHECK(r.samplePeakLinear == Catch::Approx(1.0).margin(1.0e-6));
}

TEST_CASE("FIR: impulse on last sample requires finalize zero-tail", "[milestone1a][fir-tail]")
{
    constexpr double sr = 48'000.0;
    constexpr std::size_t frames = 1024;
    auto channels = zeros(frames);
    channels[0][frames - 1] = 1.0f;

    mastering::analysis::LoudnessMeter withoutFinalize;
    withoutFinalize.prepare(sr, 1, 512);
    processAll(withoutFinalize, channels, 256);
    const auto before = withoutFinalize.snapshot().truePeakLinear;

    mastering::analysis::LoudnessMeter withFinalize;
    withFinalize.prepare(sr, 1, 512);
    processAll(withFinalize, channels, 256);
    withFinalize.finalize();
    const auto after = withFinalize.snapshot().truePeakLinear;

    // Tail flush must not lose the late impulse relative to no-finalize (should be >=).
    CHECK(after >= before - 1.0e-9);
    CHECK(after == Catch::Approx(1.0).margin(0.2));
}

TEST_CASE("FIR: ISP-like alternating waveform near start", "[milestone1a][fir-tail]")
{
    constexpr double sr = 48'000.0;
    auto channels = zeros(4096);
    for (std::size_t i = 0; i < 64; ++i)
        channels[0][i] = (i % 2 == 0) ? 0.95f : -0.95f;

    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sr, 1, 512);
    processAll(meter, channels, 128);
    meter.finalize();
    const auto r = meter.snapshot();
    REQUIRE(r.truePeakValid);
    CHECK(r.truePeakLinear > r.samplePeakLinear);
}

TEST_CASE("FIR: ISP-like alternating waveform near end", "[milestone1a][fir-tail]")
{
    constexpr double sr = 48'000.0;
    constexpr std::size_t frames = 4096;
    auto channels = zeros(frames);
    for (std::size_t i = frames - 64; i < frames; ++i)
        channels[0][i] = (i % 2 == 0) ? 0.95f : -0.95f;

    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sr, 1, 512);
    processAll(meter, channels, 128);
    meter.finalize();
    const auto r = meter.snapshot();
    REQUIRE(r.truePeakValid);
    CHECK(r.truePeakLinear > r.samplePeakLinear - 1.0e-6);
}

TEST_CASE("FIR: trailing silence matches finalize zero-tail peak", "[milestone1a][fir-tail]")
{
    constexpr double sr = 48'000.0;
    constexpr std::size_t frames = 512;
    auto base = zeros(frames);
    base[0][frames - 1] = 1.0f;

    auto withSilence = zeros(frames + 64);
    withSilence[0][frames - 1] = 1.0f;

    mastering::analysis::LoudnessMeter a;
    a.prepare(sr, 1, 256);
    processAll(a, base, 128);
    a.finalize();

    mastering::analysis::LoudnessMeter b;
    b.prepare(sr, 1, 256);
    processAll(b, withSilence, 128);
    b.finalize();

    CHECK(a.snapshot().truePeakLinear == Catch::Approx(b.snapshot().truePeakLinear).margin(1.0e-4));
}

TEST_CASE("FIR: one buffer vs many buffers match after finalize", "[milestone1a][fir-tail]")
{
    constexpr double sr = 48'000.0;
    auto channels = zeros(3000);
    for (std::size_t i = 0; i < channels[0].size(); ++i)
        channels[0][i] = ((i % 7) == 0) ? 0.8f : ((i % 5) == 0 ? -0.7f : 0.1f);

    mastering::analysis::LoudnessMeter one;
    one.prepare(sr, 1, 4096);
    processAll(one, channels, 3000);
    one.finalize();

    mastering::analysis::LoudnessMeter many;
    many.prepare(sr, 1, 4096);
    processAll(many, channels, 64);
    many.finalize();

    CHECK(one.snapshot().truePeakLinear == Catch::Approx(many.snapshot().truePeakLinear).margin(1.0e-5));
    CHECK(one.snapshot().samplePeakLinear == Catch::Approx(many.snapshot().samplePeakLinear).margin(1.0e-9));
}

TEST_CASE("FIR: finalize once then idempotent second finalize", "[milestone1a][fir-tail]")
{
    constexpr double sr = 48'000.0;
    auto channels = zeros(1024);
    channels[0][100] = 0.9f;

    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sr, 1, 256);
    processAll(meter, channels, 128);
    meter.finalize();
    const auto first = meter.snapshot().truePeakLinear;
    meter.finalize();
    const auto second = meter.snapshot().truePeakLinear;
    CHECK(first == Catch::Approx(second).margin(0.0));
    CHECK(meter.snapshot().finalized);
}

TEST_CASE("FIR: reset after finalize clears history", "[milestone1a][fir-tail]")
{
    constexpr double sr = 48'000.0;
    auto loud = zeros(512);
    loud[0][10] = 1.0f;

    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sr, 1, 256);
    processAll(meter, loud, 128);
    meter.finalize();
    REQUIRE(meter.snapshot().truePeakLinear > 0.5);

    meter.reset();
    auto silence = zeros(512);
    processAll(meter, silence, 128);
    meter.finalize();
    CHECK(meter.snapshot().truePeakLinear == Catch::Approx(0.0).margin(1.0e-6));
    CHECK(meter.snapshot().samplePeakLinear == Catch::Approx(0.0).margin(1.0e-9));
}

TEST_CASE("Realtime path without finalize does not claim finalized programme", "[milestone1a][fir-tail]")
{
    constexpr double sr = 48'000.0;
    auto channels = zeros(48'000);
    channels[0][100] = 0.5f;

    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sr, 1, 512);
    processAll(meter, channels, 256);
    const auto r = meter.snapshot();
    CHECK_FALSE(r.finalized);
    CHECK_FALSE(r.truePeakIsEstimate);
}
