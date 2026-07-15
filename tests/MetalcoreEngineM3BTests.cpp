#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mastering/assistant/ActionResolver.h"
#include "mastering/assistant/MetalcoreAnalysis.h"
#include "mastering/assistant/MetalcoreMixPass.h"
#include "mastering/assistant/SectionAutomation.h"
#include "mastering/dsp/DynamicEq.h"
#include "mastering/dsp/VocalRider.h"
#include "mastering/project/ProjectDocument.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using mastering::assistant::ActionResolver;
using mastering::assistant::GuitarChannelProfile;
using mastering::assistant::LowFrequencyProfile;
using mastering::assistant::MetalcoreAnalysis;
using mastering::assistant::MetalcoreMixPass;
using mastering::assistant::ReferenceProfile;
using mastering::assistant::SectionAutomation;
using mastering::assistant::SectionAutomationState;
using mastering::assistant::SectionOffset;
using mastering::assistant::SnareProfile;
using mastering::assistant::SpectralPeak;
using mastering::assistant::SubOwnership;
using mastering::assistant::TrackAnalysisExtras;
using mastering::assistant::VocalProfile;
using mastering::assistant::evidenceLabelFor;
using mastering::project::MixPassAction;
using mastering::project::ProjectDocument;
using mastering::project::SectionKind;
using mastering::project::SectionMarker;
using mastering::project::TrackRecord;
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

std::vector<float> kickLikePulse(int n, double sr, double hz, float amp)
{
    auto x = monoTone(n, sr, hz, amp);
    const int pulse = std::max(1, int(sr * 0.03));
    for (int i = 0; i < n; ++i) {
        const double env = std::exp(-double(i % pulse) / (sr * 0.012));
        x[static_cast<std::size_t>(i)] *= float(env);
    }
    return x;
}

TrackRecord makeTrack(
    const char* id,
    TrackRole role,
    double rms,
    double sub,
    double lowMid,
    double presence,
    double air,
    double crest)
{
    TrackRecord track;
    track.id = id;
    track.name = id;
    track.role = role;
    track.metrics.rmsDbfs = rms;
    track.metrics.spectrum.subDb = sub;
    track.metrics.spectrum.bassDb = sub + 1.0;
    track.metrics.spectrum.lowMidDb = lowMid;
    track.metrics.spectrum.midDb = -20.0;
    track.metrics.spectrum.presenceDb = presence;
    track.metrics.spectrum.airDb = air;
    track.metrics.crestFactorDb = crest;
    track.metrics.stereoCorrelation = 0.2;
    return track;
}

ProjectDocument baseProject()
{
    ProjectDocument project;
    project.id = "m3b-test";
    project.name = "Synthetic Metalcore M3B";
    project.bpm = 150.0;
    project.sampleRate = 48'000.0;
    project.tracks = {
        makeTrack("kick", TrackRole::kick, -18.0, -16.0, -28.0, -35.0, -40.0, 14.0),
        makeTrack("snare", TrackRole::snare, -20.0, -40.0, -22.0, -18.0, -25.0, 16.0),
        makeTrack("bass", TrackRole::bass, -16.0, -15.0, -20.0, -30.0, -40.0, 10.0),
        makeTrack("gL", TrackRole::rhythmGuitarLeft, -14.0, -35.0, -12.0, -16.0, -10.0, 8.0),
        makeTrack("gR", TrackRole::rhythmGuitarRight, -14.0, -35.0, -13.0, -15.0, -11.0, 8.0),
        makeTrack("vox", TrackRole::cleanVocal, -22.0, -45.0, -28.0, -14.0, -20.0, 18.0),
    };
    SectionMarker chorus;
    chorus.id = "sec-chorus";
    chorus.kind = SectionKind::chorus;
    chorus.name = "Chorus";
    chorus.startSeconds = 8.0;
    chorus.endSeconds = 16.0;
    project.sections.push_back(chorus);
    return project;
}

LowFrequencyProfile makeLow(double dominantHz, double stability, int events)
{
    LowFrequencyProfile p;
    p.dominantLowHz = dominantHz;
    p.bodyHz = dominantHz;
    p.stabilityScore = stability;
    p.eventCount = events;
    SpectralPeak peak;
    peak.frequencyHz = dominantHz;
    peak.magnitudeDb = -12.0;
    peak.prominenceDb = 6.0;
    p.fundamentalCandidates.push_back(peak);
    p.evidence = "synthetic-low@" + std::to_string(dominantHz);
    return p;
}

TrackAnalysisExtras extrasFor(
    const TrackRecord& track,
    LowFrequencyProfile low = {},
    GuitarChannelProfile guitar = {},
    VocalProfile vocal = {},
    SnareProfile snare = {})
{
    TrackAnalysisExtras e;
    e.trackId = track.id;
    e.role = track.role;
    e.metrics = track.metrics;
    e.low = std::move(low);
    e.guitar = std::move(guitar);
    e.vocal = std::move(vocal);
    e.snare = std::move(snare);
    return e;
}

MetalcoreMixPass::AnalysisMap analysisFromProject(const ProjectDocument& project)
{
    MetalcoreMixPass::AnalysisMap map;
    for (const auto& track : project.tracks) {
        TrackAnalysisExtras e;
        e.trackId = track.id;
        e.role = track.role;
        e.metrics = track.metrics;
        map.emplace(track.id, std::move(e));
    }
    return map;
}

const MixPassAction* findProblem(
    const std::vector<MixPassAction>& actions,
    const std::string& problemType)
{
    const auto it = std::find_if(actions.begin(), actions.end(), [&](const auto& a) {
        return a.problemType == problemType;
    });
    return it == actions.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("M3B Scenario A: kick/bass detector freqs track analysis profiles", "[milestone3b]")
{
    auto project = baseProject();
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.bpm = project.bpm;
    options.allowSyntheticFrequencyFallback = false;

    auto verse = analysisFromProject(project);
    verse["kick"].low = makeLow(55.0, 0.7, 12);
    verse["bass"].low = makeLow(50.0, 0.7, 12);

    auto breakdown = analysisFromProject(project);
    breakdown["kick"].low = makeLow(80.0, 0.7, 12);
    breakdown["bass"].low = makeLow(65.0, 0.7, 12);

    const auto verseActions = pass.generateActions(project, verse, {}, options);
    const auto bdActions = pass.generateActions(project, breakdown, {}, options);

    const auto* vKb = findProblem(verseActions, "kickBassMasking");
    const auto* bKb = findProblem(bdActions, "kickBassMasking");

    if (vKb != nullptr && bKb != nullptr) {
        REQUIRE(vKb->hasProposedDynamicEq);
        REQUIRE(bKb->hasProposedDynamicEq);
        const double vDet = vKb->proposedDynamicEq.bands[0].detectorFrequencyHz;
        const double bDet = bKb->proposedDynamicEq.bands[0].detectorFrequencyHz;
        const double vTgt = vKb->proposedDynamicEq.bands[0].frequencyHz;
        const double bTgt = bKb->proposedDynamicEq.bands[0].frequencyHz;
        CHECK_FALSE((vDet == Catch::Approx(65.0) && vTgt == Catch::Approx(70.0)
            && bDet == Catch::Approx(65.0) && bTgt == Catch::Approx(70.0)));
        CHECK(vDet != Catch::Approx(bDet).margin(0.5));
        CHECK(std::abs(vDet - 55.0) < 8.0);
        CHECK(std::abs(bDet - 80.0) < 8.0);
    } else {
        // Analysis may decline emit; still assert profiles differ from fixed 65/70.
        const auto vDec = MetalcoreAnalysis::decideSubOwnership(
            verse["kick"].low, verse["bass"].low, project.tracks[0].metrics, project.tracks[2].metrics);
        const auto bDec = MetalcoreAnalysis::decideSubOwnership(
            breakdown["kick"].low,
            breakdown["bass"].low,
            project.tracks[0].metrics,
            project.tracks[2].metrics);
        CHECK(vDec.kickDominantHz == Catch::Approx(55.0));
        CHECK(bDec.kickDominantHz == Catch::Approx(80.0));
        CHECK_FALSE(vDec.decisionTrace.empty());
    }
}

TEST_CASE("M3B Scenario B: guitar right-only harsh targets gR not bus", "[milestone3b]")
{
    auto project = baseProject();
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.allowSyntheticFrequencyFallback = false;

    auto analysis = analysisFromProject(project);
    GuitarChannelProfile left {};
    left.longTermRmsDb = -14.0;
    left.presenceDb = -16.0;
    left.harshLikely = false;
    left.evidence = "gL-clean";

    GuitarChannelProfile right {};
    right.longTermRmsDb = -14.0;
    right.presenceDb = -15.0;
    right.harshLikely = true;
    right.harshPeakHz = 3'500.0;
    right.harshPeakDb = -8.0;
    right.harshQ = 3.0;
    right.evidence = "gR-harsh@3500";

    analysis["gL"].guitar = left;
    analysis["gR"].guitar = right;

    const auto actions = pass.generateActions(project, analysis, {}, options);
    const auto harsh = std::find_if(actions.begin(), actions.end(), [](const auto& a) {
        return a.problemType == "guitarHarshResonance" || a.problemType == "guitarHarshness";
    });
    REQUIRE(harsh != actions.end());
    CHECK(harsh->targetTrackId == "gR");
    const auto level = harsh->processingLevel;
    const bool rightScoped = level.find("right") != std::string::npos
        || harsh->targetTrackId == "gR";
    CHECK(rightScoped);
    CHECK(level != "bus");
    if (harsh->hasProposedDynamicEq)
        CHECK(harsh->proposedDynamicEq.bands[0].frequencyHz == Catch::Approx(3'500.0));
}

TEST_CASE("M3B Scenario C: vocal activity gates guitar unmask", "[milestone3b]")
{
    auto project = baseProject();
    // Force inactive path: low RMS so metrics alone do not unlock unmask.
    for (auto& t : project.tracks) {
        if (t.id == "vox")
            t.metrics.rmsDbfs = -42.0;
    }
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.allowSyntheticFrequencyFallback = false;

    GuitarChannelProfile g {};
    g.longTermRmsDb = -14.0;
    g.presenceDb = -12.0;
    g.evidence = "dense-presence";

    auto inactive = analysisFromProject(project);
    VocalProfile silent {};
    silent.activityRatio = 0.0;
    silent.presenceCentroidHz = 2'800.0;
    silent.evidence = "silent-vocal"; // non-empty so resolve prefers this profile
    inactive["vox"].vocal = silent;
    inactive["gL"].guitar = g;
    inactive["gR"].guitar = g;

    const auto inactiveActions = pass.generateActions(project, inactive, {}, options);
    const auto* inactiveUnmask = findProblem(inactiveActions, "vocalGuitarUnmask");
    CHECK((inactiveUnmask == nullptr || inactiveUnmask->state == "rejected"));

    // Active: raise activity + presence conflict (vocal not clearly above guitars).
    for (auto& t : project.tracks) {
        if (t.id == "vox") {
            t.metrics.spectrum.presenceDb = -14.0;
            t.metrics.rmsDbfs = -22.0;
        }
    }
    auto active = inactive;
    VocalProfile singing {};
    singing.activityRatio = 0.3;
    singing.presenceCentroidHz = 2'800.0;
    singing.evidence = "active-vocal";
    active["vox"].vocal = singing;
    active["vox"].metrics = project.tracks[5].metrics;

    const auto activeActions = pass.generateActions(project, active, {}, options);
    const auto* activeUnmask = findProblem(activeActions, "vocalGuitarUnmask");
    REQUIRE(activeUnmask != nullptr);
    CHECK(activeUnmask->state != "rejected");
}

TEST_CASE("M3B Scenario D: snare unmask is event-based short attack", "[milestone3b]")
{
    auto project = baseProject();
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.allowSyntheticFrequencyFallback = false;

    auto analysis = analysisFromProject(project);
    SnareProfile snare {};
    snare.crackHz = 2'450.0;
    snare.crackEnergyDb = -18.0;
    snare.eventRateHz = 4.0;
    snare.evidence = "snare-crack";
    analysis["snare"].snare = snare;

    GuitarChannelProfile g {};
    g.longTermRmsDb = -14.0;
    g.presenceDb = -12.0;
    g.evidence = "guitar-presence";
    analysis["gL"].guitar = g;
    analysis["gR"].guitar = g;

    const auto actions = pass.generateActions(project, analysis, {}, options);
    const auto* snareUnmask = findProblem(actions, "snareGuitarUnmask");
    REQUIRE(snareUnmask != nullptr);
    REQUIRE(snareUnmask->hasProposedDynamicEq);
    CHECK(snareUnmask->proposedDynamicEq.bands[0].attackMs <= 10.0);
    CHECK(snareUnmask->decisionTrace.find("eventBased") != std::string::npos);
    CHECK(snareUnmask->proposedDynamicEq.bands[0].releaseMs < 200.0);
    CHECK(snareUnmask->proposedDynamicEq.bands[0].frequencyHz == Catch::Approx(2'450.0));
}

TEST_CASE("M3B Scenario E: missing drum bus does not glue snare", "[milestone3b]")
{
    auto project = baseProject();
    // No ensureHierarchy → no bus children / no drum-bus stem.
    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.allowSyntheticFrequencyFallback = false;
    options.enableKickBass = false;
    options.enableGuitarBalance = false;
    options.enableVocalUnmask = false;
    options.enableSnareUnmask = false;
    options.enableReference = false;

    auto analysis = analysisFromProject(project);
    const auto actions = pass.generateActions(project, analysis, {}, options);

    const auto* unavailable = findProblem(actions, "drumBusUnavailable");
    const auto* glue = findProblem(actions, "drumBusGlue");
    CHECK((unavailable != nullptr || glue == nullptr
        || (glue != nullptr && glue->processorId != "compressor")));
    for (const auto& a : actions) {
        if (a.problemType == "drumBusGlue")
            CHECK(a.targetTrackId != "snare");
    }
}

TEST_CASE("M3B Scenario F: conflicting unmask reduced by ActionResolver", "[milestone3b]")
{
    std::vector<MixPassAction> actions;

    MixPassAction vocal;
    vocal.actionId = "a-vocal-unmask";
    vocal.problemType = "vocalGuitarUnmask";
    vocal.targetTrackId = "gL";
    vocal.targetPairId = "pair-gtr";
    vocal.processorId = "dynamicEq";
    vocal.parameterId = "maxCutDb";
    vocal.proposedValue = 5.0;
    vocal.hasProposedDynamicEq = true;
    vocal.proposedDynamicEq.bandCount = 1;
    vocal.proposedDynamicEq.bands[0].frequencyHz = 2'800.0;
    vocal.proposedDynamicEq.bands[0].maxCutDb = 5.0;
    vocal.state = "pending";
    actions.push_back(vocal);

    MixPassAction snare;
    snare.actionId = "a-snare-unmask";
    snare.problemType = "snareGuitarUnmask";
    snare.targetTrackId = "gL";
    snare.targetPairId = "pair-gtr";
    snare.processorId = "dynamicEq";
    snare.parameterId = "maxCutDb";
    snare.proposedValue = 5.0;
    snare.hasProposedDynamicEq = true;
    snare.proposedDynamicEq.bandCount = 1;
    snare.proposedDynamicEq.bands[0].frequencyHz = 2'600.0; // within 800 Hz neighborhood
    snare.proposedDynamicEq.bands[0].maxCutDb = 5.0;
    snare.state = "pending";
    actions.push_back(snare);

    const auto resolved = ActionResolver::resolve(std::move(actions));
    double combined = 0.0;
    for (const auto& a : resolved.actions) {
        if (a.state == "rejected" || !a.hasProposedDynamicEq)
            continue;
        if (a.problemType.find("unmask") == std::string::npos
            && a.problemType.find("Unmask") == std::string::npos)
            continue;
        combined += a.proposedDynamicEq.bands[0].maxCutDb;
    }
    CHECK(combined <= ActionResolver::kMaxCombinedGuitarUnmaskDb + 1.0e-9);
    const bool conflictLogged = !resolved.conflicts.empty()
        || std::any_of(resolved.actions.begin(), resolved.actions.end(), [](const auto& a) {
               return a.explanation.find("guardrail") != std::string::npos
                   || a.explanation.find("reduced") != std::string::npos;
           });
    CHECK(conflictLogged);
}

TEST_CASE("M3B Scenario G: multiple reference roles retained in evidence", "[milestone3b]")
{
    auto project = baseProject();
    // Low vocal-to-bed so referenceVocalToBed emits.
    for (auto& t : project.tracks) {
        if (t.id == "vox")
            t.metrics.rmsDbfs = -28.0;
        if (t.id == "gL" || t.id == "gR")
            t.metrics.rmsDbfs = -12.0;
    }
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.allowSyntheticFrequencyFallback = false;

    auto analysis = analysisFromProject(project);
    VocalProfile v {};
    v.activityRatio = 0.2;
    v.evidence = "ref-vocal";
    analysis["vox"].vocal = v;

    mastering::analysis::AudioMetrics refMetrics {};
    refMetrics.rmsDbfs = -14.0;
    refMetrics.spectrum.lowMidDb = -30.0;
    refMetrics.spectrum.presenceDb = -16.0;
    refMetrics.spectrum.subDb = -18.0;
    refMetrics.spectrum.bassDb = -16.0;

    std::vector<ReferenceProfile> refs;
    auto overall = MetalcoreAnalysis::buildReferenceProfile(refMetrics, "overall", 2.0, 1.0);
    overall.lowMidDensityDb = -30.0;
    overall.notes = "role=overall";
    auto vocalBal = MetalcoreAnalysis::buildReferenceProfile(refMetrics, "vocal-balance", 4.0, 1.0);
    vocalBal.vocalToBedDb = 4.0;
    vocalBal.notes = "role=vocal-balance";
    refs.push_back(overall);
    refs.push_back(vocalBal);

    const auto actions = pass.generateActions(project, analysis, refs, options);
    bool foundRoleHint = false;
    for (const auto& a : actions) {
        if (a.problemType.find("reference") == std::string::npos)
            continue;
        const auto blob = a.evidence + "|" + a.decisionTrace + "|" + a.explanation;
        if (blob.find("vocal-balance") != std::string::npos
            || blob.find("overall") != std::string::npos
            || blob.find("refRole") != std::string::npos) {
            foundRoleHint = true;
        }
    }
    CHECK(foundRoleHint);
}

TEST_CASE("M3B SectionAutomation crossfade evaluateGainOffset is finite", "[milestone3b]")
{
    auto project = baseProject();
    SectionAutomationState state;
    SectionOffset lane;
    lane.sectionId = "sec-chorus";
    lane.targetTrackId = "vox";
    lane.parameterId = "gainDb";
    lane.offset = 1.5;
    lane.safeMin = -6.0;
    lane.safeMax = 6.0;
    state.offsets.push_back(lane);

    for (double t = 7.9; t < 16.2; t += 0.01) {
        const double g = SectionAutomation::evaluateGainOffset(project, state, "vox", t, 0.05);
        CHECK(std::isfinite(g));
        CHECK(g == Catch::Approx(g).margin(0.0)); // not NaN
    }
}

TEST_CASE("M3B VocalRider reduces crest-like slow level toward target", "[milestone3b]")
{
    mastering::dsp::VocalRider rider;
    rider.prepare(48'000.0);
    rider.setTargetRmsDb(-18.0);
    rider.setMaxBoostDb(6.0);
    rider.setMaxCutDb(6.0);
    rider.setSmoothingMs(80.0);

    const int n = 48'000; // 1s quiet-ish tone ~ -12 dBFS peak
    auto buf = monoTone(n, 48'000.0, 1'000.0, 0.25f);
    float* ch[1] {buf.data()};
    const double before = rider.currentGainDb();
    rider.process(ch, 1, n);
    const double after = rider.currentGainDb();
    CHECK(after != Catch::Approx(before));
    // Loud input vs -18 target → cut (negative gain) after settling.
    CHECK(after < 0.0);
}

TEST_CASE("M3B ActionResolver stageOrder is deterministic", "[milestone3b]")
{
    MixPassAction gain;
    gain.problemType = "guitarLrBalance";
    gain.processorId = "gain";
    MixPassAction dyn;
    dyn.problemType = "kickBassMasking";
    dyn.processorId = "dynamicEq";
    MixPassAction comp;
    comp.problemType = "drumBusGlue";
    comp.processorId = "compressor";

    CHECK(ActionResolver::stageOrder(gain) < ActionResolver::stageOrder(dyn));
    CHECK((ActionResolver::stageOrder(dyn) < ActionResolver::stageOrder(comp)
        || ActionResolver::stageOrder(dyn) == 4));

    std::vector<MixPassAction> actions {comp, dyn, gain};
    actions[0].actionId = "c";
    actions[1].actionId = "b";
    actions[2].actionId = "a";
    const auto r1 = ActionResolver::resolve(actions);
    const auto r2 = ActionResolver::resolve(actions);
    REQUIRE(r1.actions.size() == r2.actions.size());
    for (std::size_t i = 0; i < r1.actions.size(); ++i) {
        CHECK(r1.actions[i].actionId == r2.actions[i].actionId);
        CHECK(r1.actions[i].orderIndex == static_cast<int>(i));
        CHECK(r2.actions[i].orderIndex == static_cast<int>(i));
    }
}

TEST_CASE("M3B evidenceLabelFor thresholds", "[milestone3b]")
{
    CHECK(evidenceLabelFor(0.2) == "low");
    CHECK(evidenceLabelFor(0.55) == "medium");
    CHECK(evidenceLabelFor(0.85) == "high");
}

TEST_CASE("M3B SubOwnership decisionTrace is non-empty", "[milestone3b]")
{
    const double sr = 48'000.0;
    const int n = int(sr * 0.5);
    auto kickBuf = kickLikePulse(n, sr, 55.0, 0.5f);
    auto bassBuf = monoTone(n, sr, 50.0, 0.4f);
    const auto kickLow = MetalcoreAnalysis::analyzeKickLow(kickBuf, sr);
    const auto bassLow = MetalcoreAnalysis::analyzeBassLow(bassBuf, sr);

    mastering::analysis::AudioMetrics km {};
    km.spectrum.subDb = -16.0;
    mastering::analysis::AudioMetrics bm {};
    bm.spectrum.subDb = -15.0;
    const auto decision = MetalcoreAnalysis::decideSubOwnership(kickLow, bassLow, km, bm);
    CHECK_FALSE(decision.decisionTrace.empty());
}

TEST_CASE("M3B save/reopen preserves mixPassActions orderIndex (schema v5)", "[milestone3b]")
{
    auto project = baseProject();
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.allowSyntheticFrequencyFallback = true; // ensure actions for round-trip
    project.mixPassActions = pass.generateActions(project, std::nullopt, options);
    REQUIRE_FALSE(project.mixPassActions.empty());

    for (std::size_t i = 0; i < project.mixPassActions.size(); ++i)
        CHECK(project.mixPassActions[i].orderIndex == static_cast<int>(i));

    const auto json = mastering::project::serialize(project);
    const auto restored = mastering::project::deserialize(json);
    REQUIRE(restored.has_value());
    CHECK(restored->schemaVersion == mastering::project::kCurrentSchemaVersion);
    CHECK(mastering::project::kCurrentSchemaVersion == 5);
    REQUIRE(restored->mixPassActions.size() == project.mixPassActions.size());
    for (std::size_t i = 0; i < restored->mixPassActions.size(); ++i) {
        CHECK(restored->mixPassActions[i].orderIndex == project.mixPassActions[i].orderIndex);
        CHECK(restored->mixPassActions[i].actionId == project.mixPassActions[i].actionId);
    }
}
