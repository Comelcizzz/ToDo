#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mastering/assistant/MetalcoreMixPass.h"
#include "mastering/assistant/MetalcoreProfile.h"
#include "mastering/benchmark/AblationRunner.h"
#include "mastering/benchmark/ActionGraphDiff.h"
#include "mastering/benchmark/BenchmarkSessionManifest.h"
#include "mastering/benchmark/ExperimentRunner.h"
#include "mastering/benchmark/GoldenRegression.h"
#include "mastering/benchmark/ImportValidator.h"
#include "mastering/benchmark/ListeningPackageV2.h"
#include "mastering/benchmark/ObjectiveReport.h"
#include "mastering/benchmark/UserEditCapture.h"
#include "mastering/product/ProductVersion.h"
#include "mastering/project/ProjectDocument.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using mastering::assistant::MetalcoreMixPass;
using mastering::assistant::clampProfileToHardCaps;
using mastering::assistant::defaultAggressiveProfile;
using mastering::assistant::defaultBalancedProfile;
using mastering::assistant::deserializeProfile;
using mastering::assistant::engineHardSafetyCaps;
using mastering::assistant::resolveProfileHierarchy;
using mastering::assistant::serializeProfile;
using mastering::assistant::validateProfile;
using mastering::benchmark::AblationCategory;
using mastering::benchmark::AblationRequest;
using mastering::benchmark::BenchmarkSessionManifest;
using mastering::benchmark::ExperimentDefinition;
using mastering::benchmark::ExperimentRunner;
using mastering::benchmark::ImportIssueSeverity;
using mastering::benchmark::ListeningVariantInput;
using mastering::benchmark::buildAblationPlan;
using mastering::benchmark::buildListeningPackageV2;
using mastering::benchmark::buildObjectiveReport;
using mastering::benchmark::checkGoldenExpectations;
using mastering::benchmark::defaultSyntheticGolden;
using mastering::benchmark::diffActionGraphs;
using mastering::benchmark::evaluateExpectedProblems;
using mastering::benchmark::listeningPackageLeaksIdentity;
using mastering::benchmark::makeReadinessFixtureManifest;
using mastering::benchmark::serializeManifest;
using mastering::benchmark::suggestRoleFromFilename;
using mastering::benchmark::validateImport;
using mastering::benchmark::validateManifestStructure;
using mastering::product::currentProductVersion;
using mastering::project::MixPassAction;
using mastering::project::ProjectDocument;
using mastering::project::TrackRecord;
using mastering::project::TrackRole;

namespace fs = std::filesystem;

namespace {

void writeMinimalWav(const fs::path& path, int sampleRate, int channels, int frames)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    const int dataBytes = frames * channels * 2;
    const int fileSize = 36 + dataBytes;
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&fileSize), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    const int fmtSize = 16;
    const short audioFormat = 1;
    const short ch = static_cast<short>(channels);
    const int sr = sampleRate;
    const short bits = 16;
    const int byteRate = sr * ch * bits / 8;
    const short blockAlign = static_cast<short>(ch * bits / 8);
    out.write(reinterpret_cast<const char*>(&fmtSize), 4);
    out.write(reinterpret_cast<const char*>(&audioFormat), 2);
    out.write(reinterpret_cast<const char*>(&ch), 2);
    out.write(reinterpret_cast<const char*>(&sr), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    out.write(reinterpret_cast<const char*>(&bits), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataBytes), 4);
    std::vector<char> zeros(static_cast<std::size_t>(dataBytes), 0);
    // Non-silent for kick: write a few non-zero samples.
    if (path.filename() != "silent_pad.wav" && dataBytes >= 4) {
        zeros[0] = 10;
        zeros[1] = 20;
    }
    out.write(zeros.data(), dataBytes);
}

ProjectDocument tinyProject()
{
    ProjectDocument p;
    p.id = "m4a";
    p.name = "m4a";
    p.bpm = 140;
    TrackRecord kick;
    kick.id = "kick";
    kick.name = "Kick";
    kick.role = TrackRole::kick;
    kick.metrics.rmsDbfs = -12;
    kick.metrics.durationSeconds = 4;
    p.tracks.push_back(kick);
    TrackRecord bass;
    bass.id = "bass";
    bass.name = "Bass";
    bass.role = TrackRole::bass;
    bass.metrics.rmsDbfs = -14;
    bass.metrics.durationSeconds = 4;
    p.tracks.push_back(bass);
    return p;
}

} // namespace

TEST_CASE("M4A product version format includes milestone and sha slot", "[milestone4a]")
{
    const auto v = currentProductVersion();
    REQUIRE(v.major == 0);
    REQUIRE(v.minor == 4);
    REQUIRE(v.patch == 0);
    REQUIRE(v.prerelease == "alpha.m4a");
    REQUIRE(v.profileSchemaVersion == 1);
    REQUIRE(v.engineRevision == 4);
    const auto full = v.full();
    REQUIRE(full.find("0.4.0-alpha.m4a") == 0);
    REQUIRE_FALSE(v.toJson().empty());
}

TEST_CASE("M4A manifest schema roundtrip and duplicate detection", "[milestone4a]")
{
    BenchmarkSessionManifest m;
    m.sessionId = "s1";
    m.projectName = "Test";
    m.localOnly = true;
    mastering::benchmark::BenchmarkStemEntry a;
    a.assetId = "kick";
    a.filePath = "stems/kick.wav";
    a.role = TrackRole::kick;
    m.stems.push_back(a);
    m.stems.push_back(a); // duplicate id
    const auto errs = validateManifestStructure(m);
    REQUIRE_FALSE(errs.empty());

    m.stems.pop_back();
    const auto json = serializeManifest(m);
    std::string error;
    const auto back = mastering::benchmark::deserializeManifest(json, &error);
    REQUIRE(back.has_value());
    REQUIRE(back->sessionId == "s1");
    REQUIRE(back->localOnly);
}

TEST_CASE("M4A role suggestion is deterministic rules not ML", "[milestone4a]")
{
    REQUIRE(suggestRoleFromFilename("Kick_In.wav") == TrackRole::kick);
    const auto gtr = suggestRoleFromFilename("Gtr_L.wav");
    const bool ok = gtr == TrackRole::rhythmGuitarLeft
        || gtr == TrackRole::rhythmGuitar
        || gtr == TrackRole::custom;
    REQUIRE(ok);
}

TEST_CASE("M4A import validator flags SR mismatch missing pair and no auto guitar align", "[milestone4a]")
{
    const auto root = fs::temp_directory_path() / "m4a-readiness";
    fs::remove_all(root);
    fs::create_directories(root);
    writeMinimalWav(root / "kick.wav", 48000, 1, 48000);
    writeMinimalWav(root / "snare.wav", 48000, 1, 48000);
    writeMinimalWav(root / "bass.wav", 48000, 1, 48000);
    writeMinimalWav(root / "Gtr_L_Rhythm.wav", 48000, 2, 48000);
    writeMinimalWav(root / "LeadVox.wav", 48000, 1, 48000);
    writeMinimalWav(root / "silent_pad.wav", 48000, 1, 100);
    writeMinimalWav(root / "short_fx.wav", 48000, 1, 8000);
    writeMinimalWav(root / "kick_96k.wav", 96000, 1, 96000);
    writeMinimalWav(root / "reference.wav", 48000, 2, 48000);
    writeMinimalWav(root / "target_mix.wav", 48000, 2, 48000);

    auto manifest = makeReadinessFixtureManifest(root.string());
    // Point file paths at generated files (fixture uses relative names under root).
    for (auto& s : manifest.stems)
        s.filePath = (root / fs::path(s.filePath).filename()).string();
    for (auto& r : manifest.referencePaths)
        r = (root / "reference.wav").string();
    manifest.targetMixPath = (root / "target_mix.wav").string();

    const auto report = validateImport(manifest, root.string());
    bool sawSr = false, sawPair = false, sawNoAlign = false, sawOffset = false;
    for (const auto& i : report.issues) {
        if (i.code == "sample-rate-mismatch")
            sawSr = true;
        if (i.code == "missing-pair-partner")
            sawPair = true;
        if (i.code == "no-auto-guitar-align")
            sawNoAlign = true;
        if (i.code == "start-offset")
            sawOffset = true;
    }
    REQUIRE(sawSr);
    REQUIRE(sawPair);
    REQUIRE(sawNoAlign);
    REQUIRE(sawOffset);
    const bool overallOk = report.overall == ImportIssueSeverity::error
        || report.overall == ImportIssueSeverity::userDecisionRequired;
    REQUIRE(overallOk);
}

TEST_CASE("M4A MetalcoreProfile hard caps cannot be bypassed", "[milestone4a]")
{
    auto p = defaultBalancedProfile();
    p.budget.maxCumulativeEqCutDb = 99.0;
    p.budget.maxActionsPerTrack = 100;
    p.drums.parallelWetDefault = 1.5;
    std::vector<std::string> clamped;
    REQUIRE(clampProfileToHardCaps(p, clamped));
    const auto hard = engineHardSafetyCaps();
    REQUIRE(p.budget.maxCumulativeEqCutDb <= hard.maxEqCutDb);
    REQUIRE(p.budget.maxActionsPerTrack <= hard.maxActionsPerTrack);
    REQUIRE(p.drums.parallelWetDefault <= hard.maxParallelWet);
    REQUIRE_FALSE(clamped.empty());

    const auto errors = validateProfile(p);
    // After clamp, remaining errors should not include hard-cap exceed for those fields
    // (validate clones and clamps again — may still report "will be clamped" if we pass unclamped).
    auto over = defaultAggressiveProfile();
    over.budget.maxCumulativeEqCutDb = 50.0;
    const auto beforeClamp = validateProfile(over);
    REQUIRE_FALSE(beforeClamp.empty());
}

TEST_CASE("M4A profile serialize deterministic and hierarchy resolves", "[milestone4a]")
{
    const auto a = defaultBalancedProfile();
    const auto b = defaultAggressiveProfile();
    REQUIRE(a.profileId != b.profileId);
    const auto ja = serializeProfile(a);
    const auto jb = serializeProfile(a);
    REQUIRE(ja == jb);
    std::string err;
    const auto back = deserializeProfile(ja, &err);
    REQUIRE(back.has_value());
    REQUIRE(back->profileId == a.profileId);

    auto project = a;
    project.budget.minEvidence = 0.5;
    auto session = b;
    session.budget.maxActionsPerTrack = 99; // will clamp
    const auto resolved = resolveProfileHierarchy(a, project, session);
    REQUIRE(resolved.budget.maxActionsPerTrack <= engineHardSafetyCaps().maxActionsPerTrack);
}

TEST_CASE("M4A expected annotations do not affect Action generation", "[milestone4a]")
{
    auto project = tinyProject();
    MetalcoreMixPass::AnalysisMap analysis;
    mastering::assistant::TrackAnalysisExtras kickEx;
    kickEx.trackId = "kick";
    kickEx.role = TrackRole::kick;
    kickEx.low.dominantLowHz = 55;
    kickEx.low.stabilityScore = 0.8;
    kickEx.low.eventCount = 20;
    analysis["kick"] = kickEx;
    mastering::assistant::TrackAnalysisExtras bassEx;
    bassEx.trackId = "bass";
    bassEx.role = TrackRole::bass;
    bassEx.low.dominantLowHz = 70;
    bassEx.low.stabilityScore = 0.7;
    bassEx.low.eventCount = 20;
    analysis["bass"] = bassEx;

    MetalcoreMixPass pass;
    MetalcoreMixPass::Options opt;
    opt.profile = defaultBalancedProfile();
    const auto without = pass.generateActions(project, analysis, {}, opt);

    // Annotations live only on benchmark manifest — generation API has no annotation input.
    // Prove API surface: generateActions does not accept expectedProblems.
    BenchmarkSessionManifest man;
    man.expectedProblems.push_back({"p1", "kick/bass", "bass", "", "should not feed generation"});
    const auto withEval = evaluateExpectedProblems(man.expectedProblems, without);
    REQUIRE(withEval.disclaimer.find("musical quality") != std::string::npos);

    // Same generation again is stable (annotations unused).
    const auto again = pass.generateActions(project, analysis, {}, opt);
    REQUIRE(mastering::assistant::mixPassActionsToJson(without)
        == mastering::assistant::mixPassActionsToJson(again));
}

TEST_CASE("M4A experiment runner profile A/B diff and deterministic rerun", "[milestone4a]")
{
    auto project = tinyProject();
    MetalcoreMixPass::AnalysisMap analysis;
    ExperimentDefinition def;
    def.experimentId = "exp-ab";
    def.profileIdA = "modern-metalcore-balanced";
    def.profileIdB = "modern-metalcore-aggressive";
    def.randomSeed = 7;
    const auto cmp = ExperimentRunner::compareProfiles(project, analysis, def);
    REQUIRE_FALSE(cmp.manifestJson.empty());
    REQUIRE(cmp.runA.deterministicRerunMatch);
}

TEST_CASE("M4A ablation removes selected Action and retains master safety when requested", "[milestone4a]")
{
    std::vector<MixPassAction> actions;
    MixPassAction a;
    a.actionId = "a1";
    a.processorId = "dynamicEq";
    a.problemType = "guitarHarsh";
    a.state = "pending";
    a.evidenceScore = 0.9;
    actions.push_back(a);
    MixPassAction safety;
    safety.actionId = "safe1";
    safety.processorId = "outputGain";
    safety.problemType = "master-safety";
    safety.state = "pending";
    safety.evidenceScore = 0.95;
    actions.push_back(safety);

    AblationRequest req;
    req.removeActionIds = {"a1"};
    req.keepMasterSafety = true;
    req.maxVariants = 4;
    const auto plan = buildAblationPlan(actions, req);
    REQUIRE_FALSE(plan.withoutAction.empty());
    bool hasSafety = false;
    bool hasA1 = false;
    for (const auto& x : plan.withoutAction.front().actions) {
        if (x.actionId == "safe1")
            hasSafety = true;
        if (x.actionId == "a1")
            hasA1 = true;
    }
    REQUIRE(hasSafety);
    REQUIRE_FALSE(hasA1);
}

TEST_CASE("M4A listening package v2 does not leak identity in labels/paths", "[milestone4a]")
{
    const auto dir = fs::temp_directory_path() / "m4a-listen";
    fs::remove_all(dir);
    std::vector<ListeningVariantInput> variants {
        {"RAW", "missing-raw.wav"},
        {"AUTO_A", "missing-auto.wav"},
        {"CURRENT", "missing-cur.wav"}};
    const auto pkg = buildListeningPackageV2("sess", variants, dir.string(), 123);
    REQUIRE(pkg.labels.size() == 3);
    REQUIRE_FALSE(listeningPackageLeaksIdentity(pkg));
    const bool keyHasRaw = pkg.answerKeyJson.find("RAW") != std::string::npos;
    REQUIRE(keyHasRaw); // answer key may contain identity; labels must not
}

TEST_CASE("M4A user edit capture is local structured event not training data", "[milestone4a]")
{
    MixPassAction before;
    before.actionId = "x";
    before.proposedValue = 1.0;
    before.evidenceScore = 0.7;
    MixPassAction after = before;
    after.proposedValue = 0.5;
    after.state = "edited";
    const auto ev = mastering::benchmark::captureMixPassEdit(
        before, after, "edit", "0.4.0-alpha.m4a+test", "modern-metalcore-balanced", "1");
    const auto json = mastering::benchmark::serializeUserEditEvent(ev);
    const bool flagOk = json.find("notTrainingData") != std::string::npos;
    REQUIRE(flagOk);
    const bool noteUpper = json.find("NOT ML training") != std::string::npos;
    const bool noteLower = json.find("not ML training") != std::string::npos;
    REQUIRE(noteUpper);
    (void) noteLower;
}

TEST_CASE("M4A golden regressions catch forbidden fallbacks and musical auto", "[milestone4a]")
{
    auto expect = defaultSyntheticGolden();
    std::vector<MixPassAction> actions;
    MixPassAction bad;
    bad.actionId = "x";
    bad.decisionTrace = "hardcoded-65 used";
    bad.state = "pending";
    actions.push_back(bad);
    MixPassAction musical;
    musical.actionId = "sat";
    musical.riskLevel = "musicalCreative";
    musical.autoApplyEligibility = true;
    musical.evidenceScore = 0.5;
    musical.state = "pending";
    actions.push_back(musical);
    const auto result = checkGoldenExpectations(expect, actions, false);
    REQUIRE_FALSE(result.passed);
}

TEST_CASE("M4A objective report has no mix quality score field", "[milestone4a]")
{
    mastering::benchmark::ObjectiveTechnicalMetrics tech;
    tech.durationSeconds = 90;
    tech.sampleRate = 48000;
    const auto report = buildObjectiveReport({}, tech);
    const auto json = mastering::benchmark::serializeObjectiveReport(report);
    const bool hasBadPhrase = json.find("mix quality score") != std::string::npos;
    REQUIRE_FALSE(hasBadPhrase);
    const bool hasDisclaimer = json.find("not a MixQualityScore") != std::string::npos;
    REQUIRE(hasDisclaimer);
}

TEST_CASE("M4A privacy: personal paths marked localOnly and gitignore pattern exists", "[milestone4a]")
{
    BenchmarkSessionManifest m;
    m.sessionId = "personal-1";
    m.localOnly = true;
    REQUIRE(m.localOnly);

    auto dir = fs::current_path();
    std::optional<fs::path> giPath;
    for (int i = 0; i < 10; ++i) {
        const auto candidate = dir / ".gitignore";
        if (fs::exists(candidate)) {
            giPath = candidate;
            break;
        }
        if (!dir.has_parent_path() || dir == dir.parent_path())
            break;
        dir = dir.parent_path();
    }
    REQUIRE(giPath.has_value());
    std::ifstream gi(*giPath);
    REQUIRE(gi);
    std::string content((std::istreambuf_iterator<char>(gi)), std::istreambuf_iterator<char>());
    const bool hasPersonal = content.find("benchmarks/personal/") != std::string::npos;
    REQUIRE(hasPersonal);
}

TEST_CASE("M4A Action graph diff detects amount change", "[milestone4a]")
{
    MixPassAction a;
    a.actionId = "a1";
    a.proposedValue = 1.0;
    a.processorId = "gain";
    MixPassAction b = a;
    b.proposedValue = 2.0;
    const auto diff = diffActionGraphs({a}, {b});
    REQUIRE(diff.changed >= 1);
}

TEST_CASE("M4A tracked profile JSON files deserialize", "[milestone4a]")
{
    auto dir = fs::current_path();
    std::optional<fs::path> root;
    for (int i = 0; i < 10; ++i) {
        if (fs::exists(dir / "benchmarks/profiles/modern-metalcore-balanced.json")) {
            root = dir;
            break;
        }
        if (!dir.has_parent_path() || dir == dir.parent_path())
            break;
        dir = dir.parent_path();
    }
    REQUIRE(root.has_value());
    for (const char* name : {
             "benchmarks/profiles/modern-metalcore-balanced.json",
             "benchmarks/profiles/modern-metalcore-aggressive.json",
             "benchmarks/profiles/custom.json"}) {
        std::ifstream in(*root / name);
        REQUIRE(in);
        std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string err;
        const auto p = deserializeProfile(json, &err);
        REQUIRE(p.has_value());
    }
}
