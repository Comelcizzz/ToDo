#include "mastering/dsp/Oversampler.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {

int measureImpulseDelay(mastering::dsp::Oversampler& os, int channels, int factor)
{
    os.prepare(48'000.0, 2048, channels, factor);
    os.reset();
    const int frames = 2048;
    std::vector<float> inL(static_cast<std::size_t>(frames), 0.0f);
    std::vector<float> inR(static_cast<std::size_t>(frames), 0.0f);
    inL[0] = 1.0f;
    if (channels > 1)
        inR[0] = 1.0f;
    float* ch[] {inL.data(), inR.data()};
    os.process(ch, channels, frames, [](float* const*, int, int, double) {});
    int delay = -1;
    double peak = 0.0;
    for (int i = 0; i < frames; ++i) {
        const auto v = std::abs(inL[static_cast<std::size_t>(i)]);
        if (v > peak) {
            peak = v;
            delay = i;
        }
    }
    return delay;
}

} // namespace

TEST_CASE("Oversampler 1x is unity", "[milestone1b][oversampler]")
{
    mastering::dsp::Oversampler os;
    os.prepare(48'000.0, 512, 2, 1);
    std::vector<float> l(256), r(256);
    for (int i = 0; i < 256; ++i) {
        l[static_cast<std::size_t>(i)] = 0.25f * std::sin(0.01f * static_cast<float>(i));
        r[static_cast<std::size_t>(i)] = l[static_cast<std::size_t>(i)];
    }
    auto expected = l;
    float* ch[] {l.data(), r.data()};
    os.process(ch, 2, 256, [](float* const* c, int, int n, double) {
        for (int i = 0; i < n; ++i)
            c[0][i] = c[0][i]; // identity
    });
    for (int i = 0; i < 256; ++i)
        CHECK(l[static_cast<std::size_t>(i)] == Catch::Approx(expected[static_cast<std::size_t>(i)]).margin(1.0e-6));
    CHECK(os.latencySamplesBaseRate() == 0);
}

TEST_CASE("Oversampler reported latency matches impulse peak", "[milestone1b][oversampler][latency]")
{
    for (int factor : {2, 4, 8}) {
        mastering::dsp::Oversampler os;
        const auto measured = measureImpulseDelay(os, 1, factor);
        const auto reported = os.latencySamplesBaseRate();
        REQUIRE(measured >= 0);
        CHECK(std::abs(measured - reported) <= 2);
    }
}

TEST_CASE("Oversampler reset clears history", "[milestone1b][oversampler]")
{
    mastering::dsp::Oversampler os;
    os.prepare(48'000.0, 512, 1, 4);
    std::vector<float> x(256, 0.5f);
    float* ch[] {x.data()};
    os.process(ch, 1, 256, [](float* const* c, int, int n, double) {
        for (int i = 0; i < n; ++i)
            c[0][i] *= 0.5f;
    });
    os.reset();
    std::fill(x.begin(), x.end(), 0.0f);
    x[0] = 1.0f;
    os.process(ch, 1, 256, [](float* const*, int, int, double) {});
    // After reset, impulse response should again peak near reported latency.
    int delay = 0;
    double peak = 0.0;
    for (int i = 0; i < 256; ++i) {
        if (std::abs(x[static_cast<std::size_t>(i)]) > peak) {
            peak = std::abs(x[static_cast<std::size_t>(i)]);
            delay = i;
        }
    }
    CHECK(std::abs(delay - os.latencySamplesBaseRate()) <= 2);
}

TEST_CASE("Oversampler latency across sample rates", "[milestone1b][oversampler][latency]")
{
    for (double sr : {44'100.0, 48'000.0, 96'000.0}) {
        mastering::dsp::Oversampler os;
        os.prepare(sr, 1024, 2, 4);
        CHECK(os.latencySamplesBaseRate() > 0);
    }
}
