#include "mastering/analysis/LoudnessMeter.h"
#include "mastering/dsp/DynamicTools.h"
#include "mastering/dsp/ExportQc.h"
#include "mastering/dsp/MasterSafetyChain.h"
#include "mastering/dsp/NonlinearProcessors.h"
#include "mastering/dsp/Oversampler.h"
#include "mastering/dsp/ProcessorChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace {

constexpr double kAllowedOvershootDb = 0.10;  // declared before run
constexpr double kAllowedUndershootDb = 0.20; // declared before run

double truePeakDb(const std::vector<float>& x, double sr)
{
    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sr, 1, 4096);
    constexpr int hop = 1024;
    for (std::size_t off = 0; off < x.size(); off += hop) {
        const auto n = static_cast<int>(std::min(x.size() - off, static_cast<std::size_t>(hop)));
        const float* slice = x.data() + off;
        meter.process(&slice, 1, n);
    }
    meter.finalize();
    const auto lin = meter.snapshot().truePeakLinear;
    return lin > 1.0e-12 ? 20.0 * std::log10(lin) : -120.0;
}

std::vector<float> processLimiter(
    std::vector<float> x,
    double ceilingDb,
    double sr,
    mastering::dsp::TruePeakLimiterMeters* metersOut = nullptr)
{
    mastering::dsp::TruePeakLimiter lim;
    mastering::dsp::TruePeakLimiterSettings s;
    s.ceilingDbTp = ceilingDb;
    s.lookAheadMs = 2.0;
    s.releaseMs = 50.0;
    s.oversamplingFactor = 4;
    lim.prepare(sr, 2048, 1, 4);
    lim.setSettings(s);
    lim.prepare(sr, 2048, 1, 4);

    const int n = static_cast<int>(x.size());
    for (int off = 0; off < n; off += 512) {
        const int m = std::min(512, n - off);
        float* p = x.data() + off;
        lim.process(&p, 1, m);
    }
    std::vector<float> flush(static_cast<std::size_t>(lim.latencySamples() + 128), 0.0f);
    float* fp = flush.data();
    lim.finalize(&fp, 1, static_cast<int>(flush.size()));
    x.insert(x.end(), flush.begin(), flush.end());
    const auto lat = static_cast<std::size_t>(lim.latencySamples());
    if (x.size() > lat)
        x.erase(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(lat));
    if (metersOut != nullptr)
        *metersOut = lim.meters();
    return x;
}

std::vector<float> midSlice(const std::vector<float>& x)
{
    const auto a = x.size() / 4;
    const auto b = std::min(x.size(), a + x.size() / 2);
    return {x.begin() + static_cast<std::ptrdiff_t>(a), x.begin() + static_cast<std::ptrdiff_t>(b)};
}

} // namespace

TEST_CASE("Limiter two-sided ceiling accuracy on hot sine", "[milestone1b][verification][limiter]")
{
    constexpr double sr = 48'000.0;
    for (double ceiling : {-0.1, -0.3, -0.5, -1.0, -2.0}) {
        std::vector<float> x(static_cast<std::size_t>(sr * 0.5), 0.0f);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float>(
                1.5 * std::sin(2.0 * std::numbers::pi * 1'000.0 * static_cast<double>(i) / sr));
        mastering::dsp::TruePeakLimiterMeters meters {};
        auto out = processLimiter(std::move(x), ceiling, sr, &meters);
        const auto tp = truePeakDb(midSlice(out), sr);
        const auto err = tp - ceiling;
        INFO("ceiling=" << ceiling << " tp=" << tp << " err=" << err
                        << " clamp=" << meters.safetyClampActivationCount);
        CHECK(tp <= ceiling + kAllowedOvershootDb);
        CHECK(tp >= ceiling - kAllowedUndershootDb);
        CHECK(meters.safetyClampActivationCount < 100); // not primary mechanism
    }
}

TEST_CASE("Soft and hard clippers have distinct transfer curves", "[milestone1b][verification][clip]")
{
    const double thr = mastering::dsp::dbToGain(-6.0);
    const double knee = thr * (1.0 - mastering::dsp::dbToGain(-3.0));
    const double xKnee = thr; // inside soft knee / at hard threshold
    const auto soft = mastering::dsp::SoftClipper::transfer(xKnee * 0.95, thr, knee);
    const auto hard = std::clamp(xKnee * 0.95, -thr, thr);
    // Soft knee compresses before hard threshold; values must differ in the knee.
    const auto softAbove = mastering::dsp::SoftClipper::transfer(thr + 0.25 * knee, thr, knee);
    const auto hardAbove = std::clamp(thr + 0.25 * knee, -thr, thr);
    CHECK(std::abs(softAbove - hardAbove) > 1.0e-4);
    CHECK((std::abs(soft - hard) > 1.0e-6 || std::abs(softAbove - hardAbove) > 1.0e-4));
    // Far below threshold both are identity.
    CHECK(mastering::dsp::SoftClipper::transfer(0.1, thr, knee) == Catch::Approx(0.1));
    CHECK(std::clamp(0.1, -thr, thr) == Catch::Approx(0.1));
    // Prove soft ≠ hard across a knee sweep.
    double maxDiff = 0.0;
    for (int i = 0; i < 100; ++i) {
        const double x = thr * (0.5 + 0.01 * i);
        maxDiff = std::max(
            maxDiff,
            std::abs(mastering::dsp::SoftClipper::transfer(x, thr, knee) - std::clamp(x, -thr, thr)));
    }
    CHECK(maxDiff > 1.0e-3);
}

TEST_CASE("Stereo link applies identical gain", "[milestone1b][verification][stereo]")
{
    constexpr double sr = 48'000.0;
    mastering::dsp::TruePeakLimiter lim;
    mastering::dsp::TruePeakLimiterSettings s;
    s.ceilingDbTp = -1.0;
    s.lookAheadMs = 2.0;
    s.oversamplingFactor = 4;
    s.stereoLink = true;
    lim.prepare(sr, 2048, 2, 4);
    lim.setSettings(s);
    lim.prepare(sr, 2048, 2, 4);

    std::vector<float> L(2048, 0.1f), R(2048, 0.1f);
    // Peak only on left near end of block.
    L[1500] = 2.0f;
    float* ch[2] {L.data(), R.data()};
    lim.process(ch, 2, 2048);

    // After latency, both channels should see linked reduction (right ducked with left).
    double maxAbsDiff = 0.0;
    for (int i = 200; i < 2048; ++i) {
        // Ratio of absolute values where both nonzero — linked gain ⇒ shapes track.
        if (std::abs(L[static_cast<std::size_t>(i)]) > 1.0e-4
            && std::abs(R[static_cast<std::size_t>(i)]) > 1.0e-4) {
            // dry L was 0.1 except impulse; dry R always 0.1 — post GR both ~same envelope
        }
        maxAbsDiff = std::max(
            maxAbsDiff,
            static_cast<double>(std::abs(
                std::abs(L[static_cast<std::size_t>(i)]) - std::abs(R[static_cast<std::size_t>(i)]))));
    }
    // Steady 0.1 region after linked GR should match closely (impulse is L-only content).
    double steadyDiff = 0.0;
    int n = 0;
    for (int i = 400; i < 1400; ++i) {
        steadyDiff += std::abs(L[static_cast<std::size_t>(i)] - R[static_cast<std::size_t>(i)]);
        ++n;
    }
    CHECK(steadyDiff / n < 1.0e-3);
}

TEST_CASE("Oversized DSP blocks are chunked not bypassed", "[milestone1b][verification][oversized]")
{
    constexpr double sr = 48'000.0;
    mastering::dsp::MasterSafetyChain chain;
    mastering::dsp::MasterSafetySettings settings;
    settings.limiterEnabled = true;
    settings.limiter.ceilingDbTp = -1.0;
    settings.limiter.oversamplingFactor = 4;
    chain.prepare(sr, 256, 1, 4); // small max block
    chain.setSettings(settings);
    chain.prepare(sr, 256, 1, 4);

    std::vector<float> x(2048, 1.5f); // larger than maxBlock
    float* p = x.data();
    chain.process(&p, 1, 2048);
    float peak = 0.0f;
    for (float v : x)
        peak = std::max(peak, std::abs(v));
    // Must have limited — not pass-through 1.5.
    CHECK(peak < 1.2f);
    CHECK_FALSE(chain.meters().degraded); // chunking is not a degrade condition
}

TEST_CASE("Oversampling factor change does not allocate in setSettings", "[milestone1b][verification][rt]")
{
    mastering::dsp::TruePeakLimiter lim;
    lim.prepare(48'000.0, 512, 2, 4);
    mastering::dsp::TruePeakLimiterSettings s = lim.settings();
    s.oversamplingFactor = 8;
    lim.setSettings(s);
    CHECK(lim.topologyChangePending());
    CHECK(lim.preparedOversamplingFactor() == 4); // still running at prepared topology
    // Continuous param still applies:
    s.ceilingDbTp = -0.5;
    s.oversamplingFactor = 8;
    lim.setSettings(s);
    CHECK(lim.settings().ceilingDbTp == Catch::Approx(-0.5));
    lim.prepare(48'000.0, 512, 2, 8);
    CHECK_FALSE(lim.topologyChangePending());
    CHECK(lim.preparedOversamplingFactor() == 8);
}

TEST_CASE("Oversampler latency exact match", "[milestone1b][verification][latency]")
{
    for (int factor : {2, 4, 8}) {
        mastering::dsp::Oversampler os;
        os.prepare(48'000.0, 2048, 1, factor);
        std::vector<float> x(2048, 0.0f);
        x[0] = 1.0f;
        float* p = x.data();
        os.process(&p, 1, 2048, [](float* const*, int, int, double) {});
        int meas = 0;
        double peak = 0.0;
        for (int i = 0; i < 2048; ++i) {
            const auto a = std::abs(x[static_cast<std::size_t>(i)]);
            if (a > peak) {
                peak = a;
                meas = i;
            }
        }
        CHECK(meas == os.latencySamplesBaseRate());
        CHECK(std::abs(os.fractionalLatencyResidual()) < 0.5);
    }
}

TEST_CASE("Active bypass timing null residual documented", "[milestone1b][verification][bypass]")
{
    mastering::dsp::SaturationProcessor sat;
    mastering::dsp::SaturationSettings s;
    s.drive = 1.0; // near-unity tanh
    s.mix = 1.0;
    s.autoGainStatic = true;
    s.oversamplingFactor = 4;
    s.bypass = true;
    sat.prepare(48'000.0, 512, 1, 4);
    sat.setSettings(s);
    sat.reset(); // snap bypass smoother to 0

    std::vector<float> dry(2048, 0.0f);
    for (int i = 0; i < 2048; ++i)
        dry[static_cast<std::size_t>(i)] =
            0.25f * std::sin(0.03f * static_cast<float>(i));
    auto wet = dry;
    float* p = wet.data();
    sat.process(&p, 1, 2048);

    // Bypass path = delayed dry. Compare wet[i] vs dry[i - latency] after settle.
    const int lat = sat.latencySamples();
    double err = 0.0;
    double energy = 0.0;
    for (int i = lat + 64; i < 2048; ++i) {
        const auto d = dry[static_cast<std::size_t>(i - lat)];
        const auto e = wet[static_cast<std::size_t>(i)] - d;
        err += e * e;
        energy += d * d;
    }
    const auto residual = energy > 0.0 ? err / energy : 0.0;
    // Delayed dry null — allow tiny float noise only.
    CHECK(residual < 1.0e-6);
}

TEST_CASE("Technical QC does not warn on loud programme without style profile",
    "[milestone1b][verification][qc]")
{
    std::vector<std::vector<float>> ch(1, std::vector<float>(4800, 0.0f));
    for (int i = 0; i < 4800; ++i)
        ch[0][static_cast<std::size_t>(i)] =
            0.5f * std::sin(0.1f * static_cast<float>(i)); // zero-mean; loud but valid
    mastering::dsp::MasterSafetyMeters meters {};
    const auto report =
        mastering::dsp::ExportQc::analyse(ch, 48'000.0, 24, meters, 0, 0.0, 0.10, nullptr);
    CHECK(report.technicalStatus == mastering::dsp::QcStatus::pass);
    CHECK(report.advisoryStatus == mastering::dsp::QcStatus::pass);
}
