#include "mastering/dsp/DynamicEq.h"
#include "mastering/dsp/DynamicTools.h"
#include "mastering/dsp/ProcessorChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

std::vector<float> sine(double sr, double seconds, double hz, double amp)
{
    const int n = static_cast<int>(sr * seconds);
    std::vector<float> x(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        x[static_cast<std::size_t>(i)] =
            static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * t));
    }
    return x;
}

double bandRms(const std::vector<float>& x, double sr, double loHz, double hiHz)
{
    // Crude one-pole band energy proxy via cascaded HP+LP.
    const double aHp = std::exp(-2.0 * std::numbers::pi * loHz / sr);
    const double aLp = std::exp(-2.0 * std::numbers::pi * hiHz / sr);
    double hp = 0.0, lp = 0.0, prev = 0.0;
    double acc = 0.0;
    for (float s : x) {
        hp = aHp * (hp + static_cast<double>(s) - prev);
        prev = s;
        lp = (1.0 - aLp) * hp + aLp * lp;
        acc += lp * lp;
    }
    return x.empty() ? 0.0 : std::sqrt(acc / static_cast<double>(x.size()));
}

double rms(const std::vector<float>& x)
{
    double a = 0.0;
    for (float v : x)
        a += static_cast<double>(v) * v;
    return x.empty() ? 0.0 : std::sqrt(a / static_cast<double>(x.size()));
}

} // namespace

TEST_CASE("Dynamic EQ static path when threshold unreachable", "[milestone1c][dynamiceq]")
{
    constexpr double sr = 48'000.0;
    mastering::dsp::DynamicEqProcessor eq;
    mastering::dsp::DynamicEqState state;
    state.bandCount = 1;
    state.bands[0].enabled = true;
    state.bands[0].filterType = mastering::dsp::DynamicEqFilterType::bell;
    state.bands[0].frequencyHz = 1'000.0;
    state.bands[0].q = 2.0;
    state.bands[0].staticGainDb = -6.0;
    state.bands[0].thresholdDb = 40.0; // unreachable
    state.bands[0].maxCutDb = 12.0;
    state.bands[0].detectorBandPass = true;
    state.bands[0].detectorFrequencyHz = 1'000.0;
    eq.prepare(sr, 512, 1);
    eq.setState(state);

    auto x = sine(sr, 0.25, 1'000.0, 0.5);
    auto dry = x;
    float* p = x.data();
    eq.process(&p, nullptr, 1, static_cast<int>(x.size()));
    // Expect attenuation near 1 kHz (static cut).
    CHECK(rms(x) < rms(dry) * 0.85);
    CHECK(eq.latencySamples() == 0);
}

TEST_CASE("Dynamic EQ threshold engages cut", "[milestone1c][dynamiceq]")
{
    constexpr double sr = 48'000.0;
    mastering::dsp::DynamicEqProcessor eq;
    mastering::dsp::DynamicEqState state;
    state.bandCount = 1;
    state.bands[0].frequencyHz = 1'000.0;
    state.bands[0].q = 4.0;
    state.bands[0].staticGainDb = 0.0;
    state.bands[0].thresholdDb = -20.0;
    state.bands[0].ratio = 4.0;
    state.bands[0].maxCutDb = 12.0;
    state.bands[0].attackMs = 1.0;
    state.bands[0].releaseMs = 50.0;
    state.bands[0].detectorFrequencyHz = 1'000.0;
    state.bands[0].detectorQ = 4.0;
    eq.prepare(sr, 512, 1);
    eq.setState(state);

    auto quiet = sine(sr, 0.2, 1'000.0, 0.02);
    auto loud = sine(sr, 0.2, 1'000.0, 0.6);
    auto q1 = quiet;
    auto l1 = loud;
    float* pq = q1.data();
    float* pl = l1.data();
    eq.process(&pq, nullptr, 1, static_cast<int>(q1.size()));
    eq.reset();
    eq.setState(state);
    eq.process(&pl, nullptr, 1, static_cast<int>(l1.size()));

    // Loud tone should be reduced more relatively than quiet (below/near threshold).
    const auto quietDelta = rms(quiet) - rms(q1);
    const auto loudDelta = rms(loud) - rms(l1);
    CHECK(loudDelta > quietDelta);
    CHECK(eq.meters().bands[0].maxGrDb > 0.5);
    CHECK(eq.meters().bands[0].maxGrDb <= state.bands[0].maxCutDb + 0.1);
}

TEST_CASE("Frequency-dependent sidechain ducks low not high", "[milestone1c][fdsidechain]")
{
    constexpr double sr = 48'000.0;
    // Target: low fundamental + high articulation.
    auto bass = sine(sr, 0.5, 70.0, 0.5);
    auto high = sine(sr, 0.5, 2'000.0, 0.35);
    std::vector<float> target(bass.size());
    for (std::size_t i = 0; i < target.size(); ++i)
        target[i] = bass[i] + high[i];
    auto raw = target;

    // Kick-like sidechain bursts at ~70 Hz region energy.
    std::vector<float> kick(target.size(), 0.0f);
    for (std::size_t i = 0; i < kick.size(); ++i) {
        const double t = static_cast<double>(i) / sr;
        if (std::fmod(t, 0.5) < 0.04)
            kick[i] = static_cast<float>(0.95 * std::exp(-40.0 * std::fmod(t, 0.5))
                * std::sin(2.0 * std::numbers::pi * 60.0 * t));
    }

    mastering::dsp::FrequencyDependentSidechain fd;
    mastering::dsp::FrequencyDependentSidechainSettings s;
    s.detectorFrequencyHz = 70.0;
    s.detectorQ = 3.0;
    s.targetFrequencyHz = 70.0;
    s.targetQ = 3.0;
    s.thresholdDb = -25.0;
    s.ratio = 8.0;
    s.maxCutDb = 12.0;
    s.attackMs = 2.0;
    s.releaseMs = 80.0;
    fd.prepare(sr, 512, 1);
    fd.setSettings(s);

    auto fdOut = target;
    float* tp = fdOut.data();
    const float* sc = kick.data();
    for (int off = 0; off < static_cast<int>(fdOut.size()); off += 256) {
        const int n = std::min(256, static_cast<int>(fdOut.size()) - off);
        float* tSlice = fdOut.data() + off;
        const float* sSlice = kick.data() + off;
        fd.process(&tSlice, &sSlice, 1, n);
    }

    // Broadband separator comparison.
    mastering::dsp::DynamicSeparator broad;
    broad.prepare(sr);
    broad.setDepthDb(8.0);
    broad.setAttackMs(2.0);
    broad.setReleaseMs(80.0);
    auto bbOut = raw;
    for (int off = 0; off < static_cast<int>(bbOut.size()); off += 256) {
        const int n = std::min(256, static_cast<int>(bbOut.size()) - off);
        float* tSlice = bbOut.data() + off;
        const float* sSlice = kick.data() + off;
        broad.process(&sSlice, &tSlice, 1, n);
    }

    const auto lowRaw = bandRms(raw, sr, 40.0, 120.0);
    const auto highRaw = bandRms(raw, sr, 1'500.0, 4'000.0);
    const auto lowFd = bandRms(fdOut, sr, 40.0, 120.0);
    const auto highFd = bandRms(fdOut, sr, 1'500.0, 4'000.0);
    const auto lowBb = bandRms(bbOut, sr, 40.0, 120.0);
    const auto highBb = bandRms(bbOut, sr, 1'500.0, 4'000.0);

    const auto lowFdRed = (lowRaw - lowFd) / std::max(1.0e-9, lowRaw);
    const auto highFdRed = (highRaw - highFd) / std::max(1.0e-9, highRaw);
    const auto highBbRed = (highRaw - highBb) / std::max(1.0e-9, highRaw);

    INFO("lowFdRed=" << lowFdRed << " highFdRed=" << highFdRed << " highBbRed=" << highBbRed);
    CHECK(lowFdRed > 0.05);
    CHECK(highFdRed < lowFdRed * 0.5); // high retained relative to low duck
    CHECK(highFdRed < highBbRed + 0.05); // FD should not duck highs as much as broadband
    CHECK(rms(bbOut) <= rms(raw));
}

TEST_CASE("Dynamic EQ absolute state is idempotent", "[milestone1c][dynamiceq][idempotent]")
{
    constexpr double sr = 48'000.0;
    mastering::dsp::DynamicEqProcessor eq;
    mastering::dsp::DynamicEqState state;
    state.bandCount = 2;
    state.bands[0].bandId = 10;
    state.bands[0].frequencyHz = 200.0;
    state.bands[0].staticGainDb = -3.0;
    state.bands[0].thresholdDb = 50.0;
    state.bands[1].bandId = 11;
    state.bands[1].frequencyHz = 3'000.0;
    state.bands[1].staticGainDb = 2.0;
    state.bands[1].thresholdDb = 50.0;
    eq.prepare(sr, 256, 2);
    eq.setState(state);
    const auto json1 = eq.toJson();
    eq.setState(state);
    eq.setState(state);
    const auto json2 = eq.toJson();
    CHECK(json1 == json2);

    auto x = sine(sr, 0.1, 440.0, 0.2);
    std::vector<float> r = x;
    float* ch[2] {x.data(), r.data()};
    eq.process(ch, nullptr, 2, static_cast<int>(x.size()));
    auto after1 = x;
    eq.setState(state);
    x = after1;
    r = after1;
    eq.process(ch, nullptr, 2, static_cast<int>(x.size()));
    // Re-applying identical absolute state then processing again is deterministic; compare
    // fresh process from same input.
    auto y = sine(sr, 0.1, 440.0, 0.2);
    std::vector<float> yr = y;
    float* ch2[2] {y.data(), yr.data()};
    mastering::dsp::DynamicEqProcessor eq2;
    eq2.prepare(sr, 256, 2);
    eq2.setState(state);
    eq2.process(ch2, nullptr, 2, static_cast<int>(y.size()));
    double err = 0.0;
    for (std::size_t i = 0; i < after1.size(); ++i)
        err += std::abs(after1[i] - y[i]);
    CHECK(err / after1.size() < 1.0e-6);
}

TEST_CASE("Linked stereo applies matching GR", "[milestone1c][dynamiceq][stereo]")
{
    constexpr double sr = 48'000.0;
    mastering::dsp::DynamicEqProcessor eq;
    mastering::dsp::DynamicEqState state;
    state.bandCount = 1;
    state.bands[0].stereoMode = mastering::dsp::DynamicEqStereoMode::linked;
    state.bands[0].frequencyHz = 1'000.0;
    state.bands[0].q = 2.0;
    state.bands[0].thresholdDb = -18.0;
    state.bands[0].ratio = 6.0;
    state.bands[0].maxCutDb = 10.0;
    state.bands[0].detectorFrequencyHz = 1'000.0;
    eq.prepare(sr, 512, 2);
    eq.setState(state);

    auto L = sine(sr, 0.2, 1'000.0, 0.5);
    auto R = sine(sr, 0.2, 1'000.0, 0.5);
    float* ch[2] {L.data(), R.data()};
    eq.process(ch, nullptr, 2, static_cast<int>(L.size()));
    double diff = 0.0;
    for (std::size_t i = 256; i < L.size(); ++i)
        diff += std::abs(L[i] - R[i]);
    CHECK(diff / (L.size() - 256) < 1.0e-5);
}

TEST_CASE("External sidechain silence does not crash", "[milestone1c][dynamiceq][sidechain]")
{
    mastering::dsp::DynamicEqProcessor eq;
    mastering::dsp::DynamicEqState state;
    state.bandCount = 1;
    state.bands[0].detectorSource = mastering::dsp::DetectorSource::external;
    state.bands[0].thresholdDb = -30.0;
    eq.prepare(48'000.0, 128, 2);
    eq.setState(state);
    std::vector<float> L(256, 0.2f), R(256, 0.2f);
    float* ch[2] {L.data(), R.data()};
    eq.process(ch, nullptr, 2, 256); // missing SC
    for (float v : L)
        CHECK(std::isfinite(v));
}

TEST_CASE("Oversized blocks are chunked", "[milestone1c][dynamiceq][safety]")
{
    mastering::dsp::DynamicEqProcessor eq;
    mastering::dsp::DynamicEqState state;
    state.bandCount = 1;
    state.bands[0].thresholdDb = 100.0;
    eq.prepare(48'000.0, 64, 1);
    eq.setState(state);
    auto x = sine(48'000.0, 0.05, 440.0, 0.3);
    float* p = x.data();
    eq.process(&p, nullptr, 1, static_cast<int>(x.size())); // > maxBlock
    CHECK(rms(x) > 0.01);
    for (float v : x)
        CHECK(std::isfinite(v));
}
