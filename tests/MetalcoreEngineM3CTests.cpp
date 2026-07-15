#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mastering/analysis/StreamingAnalyzer.h"
#include "mastering/assistant/ActionBudget.h"
#include "mastering/assistant/EvidenceModel.h"
#include "mastering/assistant/LoudnessMatch.h"
#include "mastering/assistant/RenderIdentity.h"
#include "mastering/assistant/SectionAutomation.h"
#include "mastering/dsp/ParallelCompressor.h"
#include "mastering/dsp/StereoWidth.h"
#include "mastering/project/ProjectDocument.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using mastering::analysis::StreamingAnalyzer;
using mastering::assistant::ActionBudget;
using mastering::assistant::EvidenceComponents;
using mastering::assistant::RenderIdentityBuilder;
using mastering::assistant::RenderIdentityInputs;
using mastering::assistant::SectionAutomation;
using mastering::assistant::SectionAutomationState;
using mastering::assistant::applyBudget;
using mastering::assistant::deriveSafeRange;
using mastering::assistant::matchBuffers;
using mastering::dsp::ParallelCompressor;
using mastering::dsp::ParallelCompressorState;
using mastering::dsp::StereoWidth;
using mastering::dsp::StereoWidthState;
using mastering::project::MixPassAction;
using mastering::project::ProjectDocument;
using mastering::project::SectionKind;
using mastering::project::SectionMarker;
using mastering::project::TrackRole;

namespace {

std::vector<float> monoTone(int n, double sr, double hz, float amp)
{
    std::vector<float> x(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t>(i)] = float(
            amp * std::sin(2.0 * std::numbers::pi * hz * (double(i) / sr)));
    return x;
}

void addKickPulses(std::vector<float>& x, double sr, double hz, float amp, double periodSec)
{
    const int n = static_cast<int>(x.size());
    const int period = std::max(1, int(sr * periodSec));
    const int len = std::max(1, int(sr * 0.04));
    for (int start = 0; start < n; start += period) {
        for (int j = 0; j < len && start + j < n; ++j) {
            const double env = std::exp(-j / (sr * 0.015));
            x[static_cast<std::size_t>(start + j)] += float(
                amp * env * std::sin(2.0 * std::numbers::pi * hz * (double(j) / sr)));
        }
    }
}

double rmsOf(const std::vector<float>& x)
{
    double a = 0.0;
    for (float v : x)
        a += double(v) * v;
    return x.empty() ? 0.0 : std::sqrt(a / double(x.size()));
}

double energyOf(const std::vector<float>& x)
{
    double a = 0.0;
    for (float v : x)
        a += double(v) * v;
    return a;
}

MixPassAction makeAction(
    const std::string& id,
    const std::string& track,
    double evidence,
    int priority,
    const std::string& processor = "gain")
{
    MixPassAction a;
    a.actionId = id;
    a.targetTrackId = track;
    a.processorId = processor;
    a.parameterId = "gainDb";
    a.currentValue = 0.0;
    a.proposedValue = 1.0;
    a.evidenceScore = evidence;
    a.confidence = evidence;
    a.priority = priority;
    a.state = "pending";
    a.sectionScope = "full";
    return a;
}

} // namespace

TEST_CASE("M3C StreamingAnalyzer processes >60s synthetic without crash", "[milestone3c]")
{
    constexpr double sr = 16'000.0;
    constexpr double dur = 90.0;
    const int n = static_cast<int>(sr * dur);
    auto mono = monoTone(n, sr, 110.0, 0.08f);
    addKickPulses(mono, sr, 55.0, 0.4f, 0.5);

    StreamingAnalyzer analyzer;
    analyzer.setTrackIdentity("stream-90s", 12345, 0xabcdu);
    const auto result = analyzer.analyzeBuffer(mono, sr);

    REQUIRE_FALSE(result.cancelled);
    REQUIRE(result.progress == Catch::Approx(1.0).margin(1.0e-9));
    REQUIRE(result.metrics.durationSeconds == Catch::Approx(dur).margin(0.05));
    REQUIRE(result.cacheKey == "stream-90s|12345|abcd");
}

TEST_CASE("M3C StreamingAnalyzer cancel mid-way", "[milestone3c]")
{
    constexpr double sr = 16'000.0;
    constexpr double dur = 90.0;
    const int n = static_cast<int>(sr * dur);
    auto mono = monoTone(n, sr, 100.0, 0.05f);

    std::atomic<bool> cancel {false};
    StreamingAnalyzer analyzer;
    analyzer.setCancelFlag(&cancel);
    analyzer.setTrackIdentity("cancel-test", 1, 1);

    analyzer.begin(sr);
    const int chunk = 4096;
    int offset = 0;
    int chunks = 0;
    while (offset < n) {
        const int m = std::min(chunk, n - offset);
        analyzer.processChunk(mono.data() + offset, m);
        offset += m;
        ++chunks;
        if (chunks == 8) {
            cancel.store(true);
            break;
        }
    }
    const auto result = analyzer.finalize();
    REQUIRE(result.cancelled);
    REQUIRE(result.progress < 1.0);
    REQUIRE(result.progress > 0.0);
}

TEST_CASE("M3C StreamingAnalyzer cache key stable for same inputs", "[milestone3c]")
{
    StreamingAnalyzer a;
    a.setTrackIdentity("track-A", 999, 0x55aa);
    StreamingAnalyzer b;
    b.setTrackIdentity("track-A", 999, 0x55aa);
    REQUIRE(a.cacheKey() == b.cacheKey());
    REQUIRE(a.cacheKey() == "track-A|999|55aa");

    StreamingAnalyzer c;
    c.setTrackIdentity("track-A", 998, 0x55aa);
    REQUIRE(c.cacheKey() != a.cacheKey());
}

TEST_CASE("M3C kick-like pulses every 0.5s yield expected events + min spacing", "[milestone3c]")
{
    constexpr double sr = 16'000.0;
    constexpr double dur = 8.0;
    constexpr double period = 0.5;
    const int n = static_cast<int>(sr * dur);
    std::vector<float> mono(static_cast<std::size_t>(n), 0.0f);
    // Longer low body so F0 resolves inside the analyzer's low window.
    const int pulseLen = std::max(1, int(sr * 0.06));
    const int periodFrames = std::max(1, int(sr * period));
    for (int start = 0; start < n; start += periodFrames) {
        for (int j = 0; j < pulseLen && start + j < n; ++j) {
            const double env = std::exp(-j / (sr * 0.02));
            mono[static_cast<std::size_t>(start + j)] += float(
                0.85 * env * std::sin(2.0 * std::numbers::pi * 60.0 * (double(j) / sr)));
            mono[static_cast<std::size_t>(start + j)] += float(
                0.3 * env * std::sin(2.0 * std::numbers::pi * 100.0 * (double(j) / sr)));
        }
    }

    StreamingAnalyzer analyzer;
    const auto result = analyzer.analyzeBuffer(mono, sr);
    const int expected = static_cast<int>(std::lround(dur / period));
    REQUIRE(result.kickEvents.size() >= static_cast<std::size_t>(expected / 2));
    REQUIRE(result.kickEvents.size() <= static_cast<std::size_t>(expected + 4));

    for (std::size_t i = 1; i < result.kickEvents.size(); ++i) {
        const double dt = result.kickEvents[i].timeSeconds - result.kickEvents[i - 1].timeSeconds;
        REQUIRE(dt >= 0.2 - 1.0e-6);
    }
}

TEST_CASE("M3C quiet noise yields few or zero kick false positives", "[milestone3c]")
{
    constexpr double sr = 16'000.0;
    constexpr double dur = 4.0;
    const int n = static_cast<int>(sr * dur);
    std::vector<float> mono(static_cast<std::size_t>(n), 0.0f);
    std::mt19937 rng(42);
    std::normal_distribution<float> dist(0.0f, 0.002f);
    for (auto& s : mono)
        s = dist(rng);

    StreamingAnalyzer analyzer;
    const auto result = analyzer.analyzeBuffer(mono, sr);
    REQUIRE(result.kickEvents.size() <= 2);
}

TEST_CASE("M3C bass occupancy sees 50Hz and 80Hz energy / stableFundamental", "[milestone3c]")
{
    constexpr double sr = 48'000.0;
    constexpr double dur = 4.0;
    const int n = static_cast<int>(sr * dur);
    std::vector<float> mono(static_cast<std::size_t>(n), 0.0f);
    const int half = n / 2;
    for (int i = 0; i < half; ++i)
        mono[static_cast<std::size_t>(i)] = float(
            0.4 * std::sin(2.0 * std::numbers::pi * 50.0 * (double(i) / sr)));
    for (int i = half; i < n; ++i)
        mono[static_cast<std::size_t>(i)] = float(
            0.4 * std::sin(2.0 * std::numbers::pi * 80.0 * (double(i) / sr)));

    StreamingAnalyzer analyzer;
    const auto result = analyzer.analyzeBuffer(mono, sr);
    REQUIRE_FALSE(result.bass.binHz.empty());
    REQUIRE(result.bass.binHz.size() == result.bass.energyDb.size());
    REQUIRE(result.bass.stableFundamentalHz > 0.0);

    auto nearestEnergy = [&](double hz) {
        std::size_t best = 0;
        double bestDist = 1.0e9;
        for (std::size_t i = 0; i < result.bass.binHz.size(); ++i) {
            const double d = std::abs(result.bass.binHz[i] - hz);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        return result.bass.energyDb[best];
    };

    const double e50 = nearestEnergy(50.0);
    const double e80 = nearestEnergy(80.0);
    const double e140 = nearestEnergy(140.0);
    REQUIRE(e50 > e140 - 6.0);
    REQUIRE(e80 > e140 - 6.0);
    REQUIRE(e50 > -60.0);
    REQUIRE(e80 > -60.0);
}

TEST_CASE("M3C SectionAutomation multi-param dynMaxCutDb section scope", "[milestone3c]")
{
    ProjectDocument project;
    project.id = "m3c-section";
    SectionMarker verse;
    verse.id = "verse";
    verse.kind = SectionKind::verse;
    verse.name = "Verse";
    verse.startSeconds = 2.0;
    verse.endSeconds = 6.0;
    project.sections.push_back(verse);

    MixPassAction action;
    action.actionId = "dyn-cut-verse";
    action.targetTrackId = "gtr";
    action.processorId = "dynamicEq";
    action.parameterId = "dynMaxCutDb";
    action.currentValue = 0.0;
    action.proposedValue = 4.0;
    action.allowedMin = -6.0;
    action.allowedMax = 6.0;
    action.sectionScope = "verse";
    action.state = "pending";

    const auto state = SectionAutomation::fromActions({action});
    REQUIRE_FALSE(state.offsets.empty());
    REQUIRE(state.offsets.front().parameterId == "dynMaxCutDb");

    const double inside = SectionAutomation::evaluateParameterOffset(
        project, state, "gtr", "dynMaxCutDb", 4.0, 0.05);
    const double outside = SectionAutomation::evaluateParameterOffset(
        project, state, "gtr", "dynMaxCutDb", 0.5, 0.05);
    const double edge = SectionAutomation::evaluateParameterOffset(
        project, state, "gtr", "dynMaxCutDb", 2.025, 0.05);

    REQUIRE(inside == Catch::Approx(4.0).margin(0.05));
    REQUIRE(std::abs(outside) < 1.0e-9);
    REQUIRE(edge > 0.0);
    REQUIRE(edge < inside);
}

TEST_CASE("M3C ParallelCompressor wet/dry/bypass", "[milestone3c]")
{
    constexpr double sr = 48'000.0;
    constexpr int n = 48'000;
    auto signal = monoTone(n, sr, 200.0, 0.6f);
    // Add envelope peaks so compressor engages.
    for (int i = 0; i < n; i += 2400) {
        for (int j = 0; j < 200 && i + j < n; ++j)
            signal[static_cast<std::size_t>(i + j)] *= 1.8f;
    }

    auto processCopy = [&](ParallelCompressorState st) {
        auto buf = signal;
        ParallelCompressor comp;
        comp.prepare(sr, 512, 1);
        comp.setState(st);
        for (int off = 0; off < n; off += 512) {
            const int block = std::min(512, n - off);
            float* ch[1] {buf.data() + off};
            comp.process(ch, 1, block);
        }
        return buf;
    };

    ParallelCompressorState drySt;
    drySt.thresholdDb = -24.0;
    drySt.ratio = 6.0;
    drySt.wetAmount = 0.0;
    drySt.makeupDb = 0.0;
    const auto dry = processCopy(drySt);

    ParallelCompressorState wetSt = drySt;
    wetSt.wetAmount = 1.0;
    const auto wet = processCopy(wetSt);

    ParallelCompressorState bypassSt = wetSt;
    bypassSt.bypass = true;
    const auto bypassed = processCopy(bypassSt);

    const double dryRms = rmsOf(dry);
    const double wetRms = rmsOf(wet);
    const double bypassRms = rmsOf(bypassed);
    const double inputRms = rmsOf(signal);

    REQUIRE(std::abs(dryRms - inputRms) / inputRms < 0.02);
    REQUIRE(std::abs(wetRms - dryRms) / dryRms > 0.02);
    REQUIRE(std::abs(bypassRms - inputRms) / inputRms < 0.05);
}

TEST_CASE("M3C StereoWidth mid/side roundtrip, lowBandMono, mono passthrough", "[milestone3c]")
{
    constexpr double sr = 48'000.0;
    constexpr int n = 8'000;

    // High-frequency stereo content above low-band mono cutoff.
    std::vector<float> left(static_cast<std::size_t>(n), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / sr;
        left[static_cast<std::size_t>(i)] = float(0.3 * std::sin(2.0 * std::numbers::pi * 1'000.0 * t));
        right[static_cast<std::size_t>(i)] = float(0.3 * std::sin(2.0 * std::numbers::pi * 1'000.0 * t + 1.2));
    }
    const double inEnergy = energyOf(left) + energyOf(right);

    StereoWidth width;
    width.prepare(sr, n);
    StereoWidthState st;
    st.midGainDb = 0.0;
    st.sideGainDb = 0.0;
    st.lowBandMonoHz = 120.0;
    st.minCorrelation = -1.0; // disable guard for this check
    width.setState(st);
    float* ch[2] {left.data(), right.data()};
    width.process(ch, 2, n);
    const double outEnergy = energyOf(left) + energyOf(right);
    REQUIRE(outEnergy == Catch::Approx(inEnergy).margin(inEnergy * 0.15));

    // Low-band side should be attenuated by lowBandMono.
    std::vector<float> lowL(static_cast<std::size_t>(n), 0.0f);
    std::vector<float> lowR(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / sr;
        const float s = float(0.4 * std::sin(2.0 * std::numbers::pi * 60.0 * t));
        lowL[static_cast<std::size_t>(i)] = s;
        lowR[static_cast<std::size_t>(i)] = -s; // pure side
    }
    auto sideEnergy = [](const std::vector<float>& l, const std::vector<float>& r) {
        double e = 0.0;
        for (std::size_t i = 0; i < l.size(); ++i) {
            const double side = 0.5 * (double(l[i]) - double(r[i]));
            e += side * side;
        }
        return e;
    };
    const double sideIn = sideEnergy(lowL, lowR);
    StereoWidth width2;
    width2.prepare(sr, n);
    width2.setState(st);
    float* lowCh[2] {lowL.data(), lowR.data()};
    // Warm filter then measure steady-state side.
    width2.process(lowCh, 2, n);
    // Re-generate and process once more after filter settled conceptually — use second half.
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / sr;
        const float s = float(0.4 * std::sin(2.0 * std::numbers::pi * 60.0 * t));
        lowL[static_cast<std::size_t>(i)] = s;
        lowR[static_cast<std::size_t>(i)] = -s;
    }
    width2.process(lowCh, 2, n);
    const double sideOut = sideEnergy(lowL, lowR);
    REQUIRE(sideOut < sideIn * 0.35);

    // Mono input unchanged.
    auto mono = monoTone(n, sr, 440.0, 0.2f);
    auto monoCopy = mono;
    StereoWidth width3;
    width3.prepare(sr, n);
    width3.setState(st);
    float* monoCh[1] {mono.data()};
    width3.process(monoCh, 1, n);
    REQUIRE(mono == monoCopy);
}

TEST_CASE("M3C LoudnessMatch integrated path within 0.5dB for 4s", "[milestone3c]")
{
    constexpr double sr = 48'000.0;
    constexpr double dur = 4.0;
    const int n = static_cast<int>(sr * dur);
    auto quiet = monoTone(n, sr, 1'000.0, 0.05f);
    auto loud = monoTone(n, sr, 1'000.0, 0.2f); // +12 dB linear amp ≈ +12 dB

    const auto result = matchBuffers({quiet, quiet}, {loud, loud}, sr, dur);
    REQUIRE(result.valid);
    REQUIRE(result.usedIntegratedLufs);
    REQUIRE(result.method == "integrated-lufs");
    const double expectedDb = 20.0 * std::log10(0.2 / 0.05);
    REQUIRE(result.matchGainDb == Catch::Approx(expectedDb).margin(0.5));
}

TEST_CASE("M3C LoudnessMatch short <1s uses bounded-rms not LUFS", "[milestone3c]")
{
    constexpr double sr = 48'000.0;
    constexpr double dur = 0.5;
    const int n = static_cast<int>(sr * dur);
    auto a = monoTone(n, sr, 440.0, 0.1f);
    auto b = monoTone(n, sr, 440.0, 0.2f);
    const auto result = matchBuffers({a}, {b}, sr, dur);
    REQUIRE(result.valid);
    REQUIRE_FALSE(result.usedIntegratedLufs);
    REQUIRE(result.method == "bounded-rms-short-preview");
    REQUIRE(result.method.find("lufs") == std::string::npos);
}

TEST_CASE("M3C ActionBudget reduces 20 candidates keeping high evidence", "[milestone3c]")
{
    std::vector<MixPassAction> actions;
    for (int i = 0; i < 20; ++i) {
        actions.push_back(makeAction(
            "a" + std::to_string(i),
            "kick",
            0.2 + 0.04 * double(i), // 0.2 .. 0.96
            50));
    }
    ActionBudget budget;
    budget.maxActionsPerTrack = 6;
    budget.minEvidence = 0.35;
    std::vector<std::string> rejected;
    const auto kept = applyBudget(std::move(actions), budget, rejected);

    // applyBudget returns accepted + rejected (state=rejected) for UI traces.
    std::vector<MixPassAction> accepted;
    for (const auto& a : kept) {
        if (a.state != "rejected")
            accepted.push_back(a);
    }
    REQUIRE(accepted.size() <= 6);
    REQUIRE(accepted.size() >= 1);
    REQUIRE_FALSE(rejected.empty());
    REQUIRE(rejected.size() >= 10);
    const double minKept = std::min_element(
        accepted.begin(),
        accepted.end(),
        [](const auto& a, const auto& b) { return a.evidenceScore < b.evidenceScore; })
                                ->evidenceScore;
    REQUIRE(minKept >= 0.7);
}

TEST_CASE("M3C EvidenceModel breakdown and labels", "[milestone3c]")
{
    EvidenceComponents low {};
    REQUIRE_FALSE(low.breakdown().empty());
    REQUIRE(low.label() == "Low");

    EvidenceComponents mid {};
    mid.roleCertainty = 0.8;
    mid.signalDuration = 0.8;
    mid.eventCount = 0.5;
    mid.eventConsistency = 0.5;
    mid.spectralProminence = 0.5;
    mid.sectionConsistency = 0.5;
    mid.detectorAgreement = 0.5;
    mid.referenceAgreement = 0.5;
    REQUIRE(mid.label() == "Medium");
    REQUIRE(mid.breakdown().find("score=") != std::string::npos);

    EvidenceComponents high {};
    high.roleCertainty = 1.0;
    high.signalDuration = 1.0;
    high.eventCount = 1.0;
    high.eventConsistency = 1.0;
    high.spectralProminence = 1.0;
    high.temporalOverlap = 1.0;
    high.sectionConsistency = 1.0;
    high.noiseFloor = 1.0;
    high.detectorAgreement = 1.0;
    high.referenceAgreement = 1.0;
    REQUIRE(high.label() == "High");
}

TEST_CASE("M3C RenderIdentity hash changes with action graph", "[milestone3c]")
{
    RenderIdentityInputs inA;
    inA.actionGraphJson = R"({"actions":[{"id":"a1"}]})";
    inA.renderGraphJson = "{}";
    inA.processorStatesJson = "{}";
    inA.sectionAutomationJson = "[]";
    inA.sourceAssetHashes = {"deadbeef"};
    inA.referenceHashes = {"cafebabe"};

    RenderIdentityInputs inB = inA;
    inB.actionGraphJson = R"({"actions":[{"id":"a2"}]})";

    const auto idA = RenderIdentityBuilder::build(inA);
    const auto idB = RenderIdentityBuilder::build(inB);
    REQUIRE(idA.actionGraphHash != idB.actionGraphHash);
    REQUIRE_FALSE(idA.identityJson.empty());
}

TEST_CASE("M3C deriveSafeRange clamps finalProposal within caps", "[milestone3c]")
{
    const auto d = deriveSafeRange("dynamicEq", "guitar", 0.8, 5.0, 20.0);
    REQUIRE(d.finalProposal <= d.finalMax);
    REQUIRE(d.finalProposal >= d.finalMin);
    REQUIRE(d.finalProposal <= d.globalCap);
    REQUIRE(d.finalProposal <= d.evidenceCap);
    REQUIRE(d.finalProposal < 20.0);
    REQUIRE_FALSE(d.trace.empty());
}
