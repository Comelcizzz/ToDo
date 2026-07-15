#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mastering/analysis/AnalysisFingerprint.h"
#include "mastering/analysis/Sha256.h"
#include "mastering/analysis/StreamingAnalyzer.h"
#include "mastering/assistant/ActionBudget.h"
#include "mastering/assistant/ActionResolver.h"
#include "mastering/assistant/AutoApplyPolicy.h"
#include "mastering/assistant/LoudnessMatch.h"
#include "mastering/assistant/RealtimeOfflineCompare.h"
#include "mastering/assistant/RenderIdentity.h"
#include "mastering/dsp/StereoWidth.h"
#include "mastering/project/ProjectDocument.h"

#include <cmath>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using mastering::analysis::AnalysisCacheEntry;
using mastering::analysis::AnalysisFingerprint;
using mastering::analysis::AnalysisResultCache;
using mastering::analysis::StreamingAnalyzer;
using mastering::analysis::fastContentFingerprint;
using mastering::analysis::hashSettingsBlob;
using mastering::analysis::kAnalysisAlgorithmVersion;
using mastering::analysis::sha256Hex;
using mastering::assistant::ActionBudget;
using mastering::assistant::ActionResolver;
using mastering::assistant::AutoApplyPolicyConfig;
using mastering::assistant::RenderIdentityBuilder;
using mastering::assistant::RenderIdentityInputs;
using mastering::assistant::applyBudget;
using mastering::assistant::classifyActionRisk;
using mastering::assistant::evaluateAutoApply;
using mastering::assistant::matchBuffers;
using mastering::assistant::runSuiteConsistencyBattery;
using mastering::assistant::ActionRiskLevel;
using mastering::dsp::StereoWidth;
using mastering::dsp::StereoWidthState;
using mastering::project::MixPassAction;

namespace {

std::vector<float> tone(int n, double sr, double hz, float amp)
{
    std::vector<float> x(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t>(i)] = float(
            amp * std::sin(2.0 * std::numbers::pi * hz * (double(i) / sr)));
    return x;
}

MixPassAction act(
    const std::string& id,
    const std::string& processor,
    const std::string& problem,
    double evidence)
{
    MixPassAction a;
    a.actionId = id;
    a.targetTrackId = "t1";
    a.processorId = processor;
    a.parameterId = "gainDb";
    a.problemType = problem;
    a.evidenceScore = evidence;
    a.confidence = evidence;
    a.state = "pending";
    a.sectionScope = "full";
    a.currentValue = 0.0;
    a.proposedValue = 1.0;
    return a;
}

} // namespace

TEST_CASE("M3C verification cache fingerprint detects same size+mtime content change", "[milestone3c]")
{
    auto a = tone(8192, 48'000.0, 440.0, 0.2f);
    auto b = a;
    // Change middle while keeping size.
    b[4096] = 0.9f;

    AnalysisFingerprint fa;
    fa.assetId = "kick.wav";
    fa.fileSize = a.size() * sizeof(float);
    fa.mtimeHash = 1234;
    fa.sampleRate = 48'000.0;
    fa.channelCount = 1;
    fa.sampleCount = static_cast<std::int64_t>(a.size());
    fa.contentFingerprint = fastContentFingerprint(a.data(), a.size());
    fa.settingsHash = hashSettingsBlob("{}");

    AnalysisFingerprint fb = fa;
    fb.contentFingerprint = fastContentFingerprint(b.data(), b.size());
    REQUIRE(fa.cacheKey() != fb.cacheKey());
}

TEST_CASE("M3C verification cache fingerprint detects middle change", "[milestone3c]")
{
    auto a = tone(16'384, 48'000.0, 220.0, 0.1f);
    auto b = a;
    for (int i = 7000; i < 9000; ++i)
        b[static_cast<std::size_t>(i)] *= -1.0f;
    REQUIRE(fastContentFingerprint(a.data(), a.size())
        != fastContentFingerprint(b.data(), b.size()));
}

TEST_CASE("M3C verification renamed identical file keeps content fingerprint", "[milestone3c]")
{
    auto samples = tone(4096, 48'000.0, 100.0, 0.15f);
    const auto content = fastContentFingerprint(samples.data(), samples.size());
    AnalysisFingerprint fa;
    fa.assetId = "/proj/stems/kick.wav";
    fa.fileSize = samples.size() * 4;
    fa.mtimeHash = 99;
    fa.contentFingerprint = content;
    AnalysisFingerprint fb = fa;
    fb.assetId = "/proj/stems/kick_renamed.wav";
    // Path differs → key differs (asset identity), but content fingerprint equal.
    REQUIRE(fa.contentFingerprint == fb.contentFingerprint);
    REQUIRE(fa.cacheKey() != fb.cacheKey());
}

TEST_CASE("M3C verification different track ID changes key", "[milestone3c]")
{
    AnalysisFingerprint fa;
    fa.assetId = "track-A";
    fa.fileSize = 100;
    fa.mtimeHash = 1;
    fa.contentFingerprint = "abc";
    AnalysisFingerprint fb = fa;
    fb.assetId = "track-B";
    REQUIRE(fa.cacheKey() != fb.cacheKey());
}

TEST_CASE("M3C verification algorithm version invalidates cache", "[milestone3c]")
{
    AnalysisFingerprint fa;
    fa.assetId = "x";
    fa.contentFingerprint = "c";
    fa.algorithmVersion = kAnalysisAlgorithmVersion;
    AnalysisFingerprint fb = fa;
    fb.algorithmVersion = kAnalysisAlgorithmVersion + 1;
    REQUIRE(fa.cacheKey() != fb.cacheKey());
}

TEST_CASE("M3C verification settings hash invalidates cache", "[milestone3c]")
{
    AnalysisFingerprint fa;
    fa.assetId = "x";
    fa.contentFingerprint = "c";
    fa.settingsHash = hashSettingsBlob("{\"q\":1}");
    AnalysisFingerprint fb = fa;
    fb.settingsHash = hashSettingsBlob("{\"q\":2}");
    REQUIRE(fa.cacheKey() != fb.cacheKey());
}

TEST_CASE("M3C verification corrupted cache entry rejected", "[milestone3c]")
{
    AnalysisResultCache cache;
    AnalysisCacheEntry entry;
    entry.cacheKey = "k1";
    entry.payloadJson = R"({"ok":true})";
    entry.payloadSha256 = sha256Hex(entry.payloadJson);
    entry.complete = true;
    REQUIRE(cache.store(entry));

    AnalysisCacheEntry corrupted = entry;
    corrupted.payloadJson = R"({"ok":false})"; // SHA mismatch
    REQUIRE_FALSE(cache.validate(corrupted));

    AnalysisCacheEntry loaded;
    REQUIRE(cache.load("k1", loaded));
    REQUIRE(loaded.payloadJson == entry.payloadJson);
}

TEST_CASE("M3C verification partial cache write refused", "[milestone3c]")
{
    AnalysisResultCache cache;
    AnalysisCacheEntry partial;
    partial.cacheKey = "k2";
    partial.payloadJson = R"({"partial":true})";
    partial.payloadSha256 = sha256Hex(partial.payloadJson);
    partial.complete = false;
    REQUIRE_FALSE(cache.store(partial));
    AnalysisCacheEntry loaded;
    REQUIRE_FALSE(cache.load("k2", loaded));
}

TEST_CASE("M3C verification render identity graphId vs artifactSha256", "[milestone3c]")
{
    RenderIdentityInputs in;
    in.actionGraphJson = R"({"actions":[{"id":"a1","v":1}]})";
    in.renderGraphJson = "{}";
    in.processorStatesJson = "{}";
    in.sectionAutomationJson = "[]";
    in.sourceAssetHashes = {sha256Hex(std::string("src"))};
    in.artifactBytes = std::string(256, '\x01');
    const auto id = RenderIdentityBuilder::build(in);
    REQUIRE(id.graphIdAlgorithm == "fnv1a-64");
    REQUIRE(id.integrityAlgorithm == "sha256");
    REQUIRE(id.graphId.size() == 16);
    REQUIRE(id.artifactSha256.size() == 64);
    REQUIRE(id.identityJson.find("not cryptographic") != std::string::npos);

    RenderIdentityInputs changed = in;
    changed.actionGraphJson = R"({"actions":[{"id":"a2","v":1}]})";
    const auto id2 = RenderIdentityBuilder::build(changed);
    REQUIRE(id.graphId != id2.graphId);
    REQUIRE(id.actionGraphId != id2.actionGraphId);

    // One sample change → artifact SHA changes.
    RenderIdentityInputs wav = in;
    wav.artifactBytes[100] = '\x02';
    const auto id3 = RenderIdentityBuilder::build(wav);
    REQUIRE(id.artifactSha256 != id3.artifactSha256);

    REQUIRE(RenderIdentityBuilder::validateArtifactSha256(in.artifactBytes, id.artifactSha256));
    REQUIRE_FALSE(RenderIdentityBuilder::validateArtifactSha256(wav.artifactBytes, id.artifactSha256));
}

TEST_CASE("M3C verification StereoWidth complementary unity and mono", "[milestone3c]")
{
    constexpr double sr = 48'000.0;
    constexpr int n = 16'384;
    std::vector<float> left(static_cast<std::size_t>(n));
    std::vector<float> right(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / sr;
        left[static_cast<std::size_t>(i)] = float(0.25 * std::sin(2.0 * std::numbers::pi * 1'000.0 * t));
        right[static_cast<std::size_t>(i)] = float(0.25 * std::sin(2.0 * std::numbers::pi * 1'000.0 * t + 0.7));
    }
    auto inL = left;
    auto inR = right;

    StereoWidth width;
    width.prepare(sr, n);
    StereoWidthState st;
    st.width = 1.0;
    st.midGainDb = 0.0;
    st.sideGainDb = 0.0;
    st.lowBandMonoEnabled = false; // true unity
    st.minCorrelation = -1.0;
    width.setState(st);
    // Warm smoothers
    float* ch[2] {left.data(), right.data()};
    width.process(ch, 2, n);
    left = inL;
    right = inR;
    width.reset();
    width.setState(st);
    width.process(ch, 2, n);

    double maxErr = 0.0;
    for (int i = n / 4; i < n; ++i) {
        maxErr = std::max(maxErr, std::abs(double(left[static_cast<std::size_t>(i)] - inL[static_cast<std::size_t>(i)])));
        maxErr = std::max(maxErr, std::abs(double(right[static_cast<std::size_t>(i)] - inR[static_cast<std::size_t>(i)])));
    }
    REQUIRE(maxErr < 1.0e-3);

    // width=0 → mono (L≈R)
    left = inL;
    right = inR;
    st.width = 0.0;
    st.lowBandMonoEnabled = false;
    width.reset();
    width.setState(st);
    width.process(ch, 2, n);
    double monoErr = 0.0;
    for (int i = n / 4; i < n; ++i)
        monoErr = std::max(
            monoErr,
            std::abs(double(left[static_cast<std::size_t>(i)] - right[static_cast<std::size_t>(i)])));
    REQUIRE(monoErr < 5.0e-3);

    // Anti-phase LF with low-band mono: side attenuated, no DC blow-up.
    for (int i = 0; i < n; ++i) {
        const float s = float(0.3 * std::sin(2.0 * std::numbers::pi * 40.0 * (double(i) / sr)));
        left[static_cast<std::size_t>(i)] = s;
        right[static_cast<std::size_t>(i)] = -s;
    }
    double sideIn = 0.0;
    for (int i = 0; i < n; ++i) {
        const double side = 0.5
            * (double(left[static_cast<std::size_t>(i)]) - double(right[static_cast<std::size_t>(i)]));
        sideIn += side * side;
    }
    st.width = 1.0;
    st.lowBandMonoEnabled = true;
    st.lowBandMonoHz = 200.0; // well above 40 Hz test tone
    width.reset();
    width.setState(st);
    width.process(ch, 2, n);
    // Second pass after filter settle.
    for (int i = 0; i < n; ++i) {
        const float s = float(0.3 * std::sin(2.0 * std::numbers::pi * 40.0 * (double(i) / sr)));
        left[static_cast<std::size_t>(i)] = s;
        right[static_cast<std::size_t>(i)] = -s;
    }
    width.process(ch, 2, n);
    double sideOut = 0.0;
    double dc = 0.0;
    for (int i = n / 2; i < n; ++i) {
        const double side = 0.5
            * (double(left[static_cast<std::size_t>(i)]) - double(right[static_cast<std::size_t>(i)]));
        sideOut += side * side;
        dc += double(left[static_cast<std::size_t>(i)]) + double(right[static_cast<std::size_t>(i)]);
    }
    const double sideInHalf = sideIn * 0.5;
    REQUIRE(sideOut < sideInHalf * 0.25); // ≥12 dB side attenuation below cutoff
    REQUIRE(std::abs(dc / double(n / 2)) < 1.0e-3);
}

TEST_CASE("M3C verification StereoWidth measurement grid", "[milestone3c]")
{
    const double srs[] = {44'100.0, 48'000.0, 96'000.0};
    const double cutoffs[] = {80.0, 120.0, 200.0, 300.0};
    for (double sr : srs) {
        for (double cutoff : cutoffs) {
            const int n = static_cast<int>(sr * 0.25);
            std::vector<float> left(static_cast<std::size_t>(n));
            std::vector<float> right(static_cast<std::size_t>(n));
            // Pink-ish: multi-sine
            for (int i = 0; i < n; ++i) {
                const double t = double(i) / sr;
                left[static_cast<std::size_t>(i)] = float(
                    0.1 * std::sin(2.0 * std::numbers::pi * 100.0 * t)
                    + 0.08 * std::sin(2.0 * std::numbers::pi * 1'000.0 * t)
                    + 0.05 * std::sin(2.0 * std::numbers::pi * 5'000.0 * t));
                right[static_cast<std::size_t>(i)] = float(
                    0.1 * std::sin(2.0 * std::numbers::pi * 100.0 * t + 0.4)
                    + 0.08 * std::sin(2.0 * std::numbers::pi * 1'000.0 * t - 0.2)
                    + 0.05 * std::sin(2.0 * std::numbers::pi * 5'000.0 * t + 1.1));
            }
            StereoWidth width;
            width.prepare(sr, 512);
            StereoWidthState st;
            st.width = 1.0;
            st.sideGainDb = 0.0;
            st.midGainDb = 0.0;
            st.lowBandMonoHz = cutoff;
            st.lowBandMonoEnabled = false;
            st.minCorrelation = -1.0;
            width.setState(st);
            float* ch[2] {left.data(), right.data()};
            auto refL = left;
            auto refR = right;
            width.process(ch, 2, n);
            left = refL;
            right = refR;
            width.reset();
            width.setState(st);
            width.process(ch, 2, n);
            double err = 0.0;
            double energy = 0.0;
            for (int i = n / 4; i < n; ++i) {
                const double dl = double(left[static_cast<std::size_t>(i)] - refL[static_cast<std::size_t>(i)]);
                const double dr = double(right[static_cast<std::size_t>(i)] - refR[static_cast<std::size_t>(i)]);
                err += dl * dl + dr * dr;
                energy += double(refL[static_cast<std::size_t>(i)]) * double(refL[static_cast<std::size_t>(i)])
                    + double(refR[static_cast<std::size_t>(i)]) * double(refR[static_cast<std::size_t>(i)]);
            }
            const double magDevDb = 10.0 * std::log10(std::max(1.0e-24, err) / std::max(1.0e-24, energy));
            // Unity mode residual should be very low (smoother settle).
            REQUIRE(magDevDb < -40.0);
            INFO("SR=" << sr << " cutoff=" << cutoff << " magDevDb=" << magDevDb);
        }
    }
}

TEST_CASE("M3C verification AUTO risk policy not universal 0.45", "[milestone3c]")
{
    AutoApplyPolicyConfig cfg;

    auto creative = act("sat", "saturation", "SaturationLift", 0.80);
    auto d1 = evaluateAutoApply(creative, cfg);
    REQUIRE(d1.riskLevel == ActionRiskLevel::musicalCreative);
    REQUIRE_FALSE(d1.eligible);
    REQUIRE(creative.riskLevel == "musicalCreative");
    REQUIRE(creative.requiredEvidence == Catch::Approx(cfg.musicalEvidence));

    auto corrective = act("kickBal", "gain", "kickBassBalance", 0.70);
    auto d2 = evaluateAutoApply(corrective, cfg);
    REQUIRE(d2.riskLevel == ActionRiskLevel::conservativeCorrective);
    REQUIRE(d2.eligible);
    REQUIRE(corrective.autoApplyEligibility);

    auto mediumCreative = act("width", "stereo", "WidthEnhance", 0.50);
    auto d3 = evaluateAutoApply(mediumCreative, cfg);
    REQUIRE_FALSE(d3.eligible); // must NOT auto via 0.45

    auto technical = act("dc", "outputGain", "technical-DC-protect", 0.40);
    auto d4 = evaluateAutoApply(technical, cfg);
    REQUIRE(d4.riskLevel == ActionRiskLevel::lowTechnical);
    REQUIRE(d4.eligible);
}

TEST_CASE("M3C verification ActionBudget skips rejected conflicts and prefers corrective", "[milestone3c]")
{
    std::vector<MixPassAction> actions;
    auto rej = act("dup", "gain", "kickBassBalance", 0.9);
    rej.state = "rejected";
    actions.push_back(rej);
    actions.push_back(act("corrective", "gain", "kickBassBalance", 0.7));
    auto ref = act("referenceKickToBass", "gain", "referenceKickToBass", 0.95);
    ref.origin = "reference";
    actions.push_back(ref);

    ActionBudget budget;
    budget.maxActionsPerTrack = 1;
    budget.preferCorrectiveOverReference = true;
    std::vector<std::string> reasons;
    const auto kept = applyBudget(std::move(actions), budget, reasons);
    int accepted = 0;
    std::string acceptedId;
    for (const auto& a : kept) {
        if (a.state != "rejected") {
            ++accepted;
            acceptedId = a.actionId;
        }
    }
    REQUIRE(accepted == 1);
    REQUIRE(acceptedId == "corrective");
}

TEST_CASE("M3C verification suite realtime/offline consistency battery", "[milestone3c]")
{
    const auto results = runSuiteConsistencyBattery(48'000.0, 1.5);
    REQUIRE(results.size() >= 8);
    for (const auto& r : results) {
        INFO(r.scenario << " rms=" << r.rmsDeltaDb << " peak=" << r.peakDeltaDb
                        << " null=" << r.nullRejectionDb);
        REQUIRE(r.passed);
    }
}

TEST_CASE("M3C verification loudness match production path methods", "[milestone3c]")
{
    constexpr double sr = 48'000.0;
    auto make = [&](double dur, float amp) {
        const int n = static_cast<int>(sr * dur);
        auto m = tone(n, sr, 1'000.0, amp);
        return std::vector<std::vector<float>> {m, m};
    };

    const auto raw = make(4.0, 0.05f);
    const auto autoBuf = make(4.0, 0.08f);
    const auto current = make(4.0, 0.07f);
    const auto ref = make(4.0, 0.1f);

    const auto mRaw = matchBuffers(raw, ref, sr, 4.0);
    const auto mAuto = matchBuffers(autoBuf, ref, sr, 4.0);
    const auto mCur = matchBuffers(current, ref, sr, 4.0);
    REQUIRE(mRaw.usedIntegratedLufs);
    REQUIRE(mAuto.method == "integrated-lufs");
    REQUIRE(mCur.valid);
    REQUIRE(std::isfinite(mRaw.matchGainDb));
    REQUIRE(std::isfinite(mRaw.truePeakAfterMatchDbtp));

    const auto silence = matchBuffers(
        {std::vector<float>(4800, 0.0f)},
        {std::vector<float>(4800, 0.0f)},
        sr,
        0.1);
    REQUIRE(silence.method == "bounded-rms-short-preview");

    const auto shortPrev = matchBuffers(make(0.5, 0.1f), make(0.5, 0.2f), sr, 0.5);
    REQUIRE_FALSE(shortPrev.usedIntegratedLufs);

    const auto midPrev = matchBuffers(make(2.0, 0.1f), make(2.0, 0.2f), sr, 2.0);
    REQUIRE(midPrev.valid);
    const bool midOk = midPrev.method == "short-term-lufs"
        || midPrev.method == "bounded-rms-short-preview";
    REQUIRE(midOk);
    REQUIRE_FALSE(midPrev.method == "integrated-lufs");
}

TEST_CASE("M3C verification truncation fields on StreamingAnalysisResult", "[milestone3c]")
{
    StreamingAnalyzer analyzer;
    analyzer.setOriginalDurationSeconds(2000.0); // > 30 min claim
    auto mono = tone(4800, 48'000.0, 100.0, 0.05f);
    const auto result = analyzer.analyzeBuffer(mono, 48'000.0);
    REQUIRE(result.analyzedDurationSeconds == Catch::Approx(0.1).margin(0.02));
    REQUIRE(result.originalDurationSeconds == Catch::Approx(2000.0));
    REQUIRE(result.truncated);
    REQUIRE(result.evidencePenalty > 0.0);
}

TEST_CASE("M3C verification artifact integrity metalcore validation files", "[milestone3c]")
{
    const std::string root = "artifacts/metalcore_engine_validation";
    const std::vector<std::string> wavs = {
        root + "/raw.wav",
        root + "/auto.wav",
        root + "/current.wav"};
    for (const auto& path : wavs) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            continue; // optional if not generated in this workspace
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(bytes.size() > 44);
        REQUIRE(bytes.substr(0, 4) == "RIFF");
        const auto digest = sha256Hex(bytes);
        REQUIRE(digest.size() == 64);
        REQUIRE(digest != sha256Hex(std::string {}));
    }

    std::ifstream identity(root + "/render_identity.json");
    if (identity) {
        std::string json(
            (std::istreambuf_iterator<char>(identity)),
            std::istreambuf_iterator<char>());
        REQUIRE(json.find("schemaVersion") != std::string::npos);
        REQUIRE_FALSE(json.empty());
    }
}
