#include "mastering/analysis/LoudnessMeter.h"
#include "mastering/dsp/DynamicTools.h"
#include "mastering/dsp/ProcessorChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

double truePeakDb(const std::vector<float>& x, double sr)
{
    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sr, 1, 4096);
    const float* p = x.data();
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
    double sr = 48'000.0)
{
    mastering::dsp::TruePeakLimiter lim;
    mastering::dsp::TruePeakLimiterSettings s;
    s.ceilingDbTp = ceilingDb;
    s.lookAheadMs = 2.0;
    s.releaseMs = 50.0;
    s.oversamplingFactor = 4;
    lim.prepare(sr, 2048, 1, 4);
    lim.setSettings(s);

    const int n = static_cast<int>(x.size());
    for (int off = 0; off < n; off += 512) {
        const int m = std::min(512, n - off);
        float* p = x.data() + off;
        lim.process(&p, 1, m);
    }
    // Flush
    std::vector<float> flush(static_cast<std::size_t>(lim.latencySamples() + 128), 0.0f);
    float* fp = flush.data();
    lim.finalize(&fp, 1, static_cast<int>(flush.size()));
    x.insert(x.end(), flush.begin(), flush.end());

    // Trim latency for measurement of steady content.
    const auto lat = static_cast<std::size_t>(lim.latencySamples());
    if (x.size() > lat)
        x.erase(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(lat));
    return x;
}

} // namespace

TEST_CASE("Look-ahead TP limiter respects ceiling on hot sine", "[milestone1b][limiter]")
{
    constexpr double sr = 48'000.0;
    constexpr double ceiling = -1.0;
    constexpr double tol = 0.15; // declared before run
    std::vector<float> x(static_cast<std::size_t>(sr * 0.5), 0.0f);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(1.5 * std::sin(2.0 * std::numbers::pi * 1'000.0 * static_cast<double>(i) / sr));

    auto out = processLimiter(std::move(x), ceiling, sr);
    // Measure mid section to avoid fade-in/out.
    const auto mid = out.size() / 4;
    std::vector<float> slice(out.begin() + static_cast<std::ptrdiff_t>(mid),
        out.begin() + static_cast<std::ptrdiff_t>(mid + out.size() / 2));
    const auto tp = truePeakDb(slice, sr);
    CHECK(tp <= ceiling + tol);
}

TEST_CASE("Limiter ceilings matrix", "[milestone1b][limiter]")
{
    constexpr double sr = 48'000.0;
    constexpr double tol = 0.15;
    for (double ceiling : {-0.1, -0.3, -1.0, -2.0}) {
        std::vector<float> x(static_cast<std::size_t>(sr * 0.3), 0.0f);
        for (std::size_t i = 0; i < x.size(); ++i) {
            // Alternating extremes → ISP-like.
            x[i] = (i % 2 == 0) ? 0.99f : -0.99f;
        }
        auto out = processLimiter(std::move(x), ceiling, sr);
        const auto mid = out.size() / 5;
        std::vector<float> slice(
            out.begin() + static_cast<std::ptrdiff_t>(mid),
            out.begin() + static_cast<std::ptrdiff_t>(std::min(out.size(), mid + out.size() / 2)));
        const auto tp = truePeakDb(slice, sr);
        INFO("ceiling=" << ceiling << " tp=" << tp);
        CHECK(tp <= ceiling + tol);
    }
}

TEST_CASE("Limiter reported latency matches impulse", "[milestone1b][limiter][latency]")
{
    mastering::dsp::TruePeakLimiter lim;
    mastering::dsp::TruePeakLimiterSettings s;
    s.lookAheadMs = 1.5;
    s.oversamplingFactor = 4;
    s.ceilingDbTp = -0.1;
    lim.prepare(48'000.0, 2048, 1, 4);
    lim.setSettings(s);

    std::vector<float> x(4096, 0.0f);
    x[0] = 0.5f;
    float* p = x.data();
    lim.process(&p, 1, static_cast<int>(x.size()));
    int delay = 0;
    double peak = 0.0;
    for (int i = 0; i < static_cast<int>(x.size()); ++i) {
        if (std::abs(x[static_cast<std::size_t>(i)]) > peak) {
            peak = std::abs(x[static_cast<std::size_t>(i)]);
            delay = i;
        }
    }
    CHECK(std::abs(delay - lim.latencySamples()) <= 3);
}

TEST_CASE("Limiter reset clears delay state", "[milestone1b][limiter]")
{
    mastering::dsp::TruePeakLimiter lim;
    lim.prepare(48'000.0, 1024, 1, 4);
    std::vector<float> x(1024, 0.8f);
    float* p = x.data();
    lim.process(&p, 1, 1024);
    lim.reset();
    std::fill(x.begin(), x.end(), 0.0f);
    lim.process(&p, 1, 1024);
    double peak = 0.0;
    for (float v : x)
        peak = std::max(peak, static_cast<double>(std::abs(v)));
    CHECK(peak < 1.0e-3);
}
