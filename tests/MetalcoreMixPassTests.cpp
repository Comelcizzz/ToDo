#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mastering/assistant/MetalcoreMixPass.h"
#include "mastering/project/ProjectDocument.h"

using mastering::assistant::MetalcoreMixPass;
using mastering::project::ProjectDocument;
using mastering::project::SectionKind;
using mastering::project::SectionMarker;
using mastering::project::TrackRecord;
using mastering::project::TrackRole;

namespace {

TrackRecord makeTrack(const char* id, TrackRole role, double rms, double sub, double lowMid,
    double presence, double air, double crest)
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

ProjectDocument metalcoreProject()
{
    ProjectDocument project;
    project.id = "m3a-test";
    project.name = "Synthetic Metalcore";
    project.bpm = 150.0;
    project.tracks = {
        makeTrack("kick", TrackRole::kick, -18.0, -16.0, -28.0, -35.0, -40.0, 14.0),
        makeTrack("snare", TrackRole::snare, -20.0, -40.0, -22.0, -18.0, -25.0, 16.0),
        makeTrack("bass", TrackRole::bass, -16.0, -15.0, -20.0, -30.0, -40.0, 10.0),
        makeTrack("gtrL", TrackRole::rhythmGuitarLeft, -14.0, -35.0, -12.0, -16.0, -10.0, 8.0),
        makeTrack("gtrR", TrackRole::rhythmGuitarRight, -17.0, -35.0, -13.0, -15.0, -11.0, 8.0),
        makeTrack("vox", TrackRole::cleanVocal, -22.0, -45.0, -28.0, -14.0, -20.0, 18.0),
        makeTrack("scream", TrackRole::screamVocal, -20.0, -45.0, -26.0, -12.0, -18.0, 14.0),
    };
    SectionMarker chorus;
    chorus.id = "sec-chorus";
    chorus.kind = SectionKind::chorus;
    chorus.name = "Chorus";
    chorus.startSeconds = 32.0;
    chorus.endSeconds = 48.0;
    project.sections.push_back(chorus);
    return project;
}

} // namespace

TEST_CASE("Metalcore Mix Pass builds track/pair/bus hierarchy", "[milestone3a]")
{
    auto project = metalcoreProject();
    MetalcoreMixPass::ensureHierarchy(project);

    REQUIRE_FALSE(project.buses.empty());
    REQUIRE_FALSE(project.pairs.empty());

    const auto* left = MetalcoreMixPass::findTrack(project, TrackRole::rhythmGuitarLeft);
    const auto* right = MetalcoreMixPass::findTrack(project, TrackRole::rhythmGuitarRight);
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    CHECK(left->id == "gtrL");
    CHECK(right->id == "gtrR");
    CHECK(left->pairId == right->pairId);
    CHECK_FALSE(left->pairId.empty());
    CHECK(left->channelPosition == "L");
    CHECK(right->channelPosition == "R");
    CHECK(left->pan == Catch::Approx(-1.0));
    CHECK(right->pan == Catch::Approx(1.0));

    bool hasGuitarBus = false;
    bool hasDrumBus = false;
    bool hasVocalBus = false;
    for (const auto& bus : project.buses) {
        if (bus.role == TrackRole::guitarBus)
            hasGuitarBus = true;
        if (bus.role == TrackRole::drumBus)
            hasDrumBus = true;
        if (bus.role == TrackRole::vocalBus)
            hasVocalBus = true;
    }
    CHECK(hasGuitarBus);
    CHECK(hasDrumBus);
    CHECK(hasVocalBus);
}

TEST_CASE("Metalcore Mix Pass generates real kick/bass Dynamic EQ Actions", "[milestone3a]")
{
    auto project = metalcoreProject();
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    MetalcoreMixPass::Options options;
    options.bpm = project.bpm;
    auto actions = pass.generateActions(project, std::nullopt, options);

    REQUIRE_FALSE(actions.empty());
    const auto kickBass = std::find_if(actions.begin(), actions.end(), [](const auto& a) {
        return a.problemType == "kickBassMasking";
    });
    REQUIRE(kickBass != actions.end());
    CHECK(kickBass->processorId == "dynamicEq");
    CHECK(kickBass->hasProposedDynamicEq);
    CHECK(kickBass->proposedDynamicEq.bands[0].detectorSource
        == mastering::dsp::DetectorSource::external);
    CHECK(kickBass->proposedDynamicEq.bands[0].sidechainSourceId == "kick");
    CHECK(kickBass->proposedDynamicEq.bands[0].maxCutDb > 0.0);
    CHECK(kickBass->proposedDynamicEq.bands[0].maxCutDb
        <= kickBass->allowedMax + 1.0e-9);
}

TEST_CASE("Metalcore Mix Pass generates vocal and snare unmask Actions", "[milestone3a]")
{
    auto project = metalcoreProject();
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    auto actions = pass.generateActions(project);

    const auto vocal = std::find_if(actions.begin(), actions.end(), [](const auto& a) {
        return a.problemType == "vocalGuitarUnmask";
    });
    const auto snare = std::find_if(actions.begin(), actions.end(), [](const auto& a) {
        return a.problemType == "snareGuitarUnmask";
    });
    const auto balance = std::find_if(actions.begin(), actions.end(), [](const auto& a) {
        return a.problemType == "guitarLrBalance";
    });
    REQUIRE(vocal != actions.end());
    REQUIRE(snare != actions.end());
    REQUIRE(balance != actions.end());
    CHECK(vocal->hasProposedDynamicEq);
    CHECK(snare->hasProposedDynamicEq);
    CHECK(balance->processorId == "gain");
}

TEST_CASE("Mix Pass Preview Apply Reject Edit Undo and idempotent Apply", "[milestone3a]")
{
    auto project = metalcoreProject();
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    project.mixPassActions = pass.generateActions(project);
    REQUIRE_FALSE(project.mixPassActions.empty());

    auto& action = project.mixPassActions.front();
    REQUIRE(MetalcoreMixPass::previewAction(action));
    CHECK(action.state == "previewing");
    REQUIRE(MetalcoreMixPass::cancelPreview(action));
    CHECK(action.state == "pending");

    REQUIRE(MetalcoreMixPass::editAction(action, action.proposedValue));
    CHECK(action.state == "edited");

    const auto beforeGain = MetalcoreMixPass::findTrackMutable(project, action.targetTrackId)->gainDb;
    REQUIRE(MetalcoreMixPass::applyAction(project, action));
    CHECK(action.state == "applied");
    REQUIRE(MetalcoreMixPass::applyAction(project, action)); // idempotent
    CHECK(action.state == "applied");

    if (action.processorId == "gain") {
        CHECK(MetalcoreMixPass::findTrackMutable(project, action.targetTrackId)->gainDb
            == Catch::Approx(action.proposedValue));
    }

    REQUIRE(MetalcoreMixPass::undoAction(project, action));
    CHECK(action.state == "pending");
    if (action.processorId == "gain") {
        CHECK(MetalcoreMixPass::findTrackMutable(project, action.targetTrackId)->gainDb
            == Catch::Approx(beforeGain));
    }

    auto rejectableIt = std::find_if(
        project.mixPassActions.begin(),
        project.mixPassActions.end(),
        [](const auto& a) {
            return a.state == "pending" || a.state == "edited" || a.state == "previewing";
        });
    REQUIRE(rejectableIt != project.mixPassActions.end());
    auto rejectable = *rejectableIt;
    REQUIRE(MetalcoreMixPass::rejectAction(rejectable));
    CHECK(rejectable.state == "rejected");
    CHECK_FALSE(MetalcoreMixPass::applyAction(project, rejectable));
}

TEST_CASE("Mix Pass project save/reopen restores MixPlan and DynEQ state", "[milestone3a]")
{
    auto project = metalcoreProject();
    MetalcoreMixPass::ensureHierarchy(project);
    MetalcoreMixPass pass;
    project.mixPassActions = pass.generateActions(project);
    REQUIRE_FALSE(project.mixPassActions.empty());

    auto* dynAction = &*std::find_if(
        project.mixPassActions.begin(),
        project.mixPassActions.end(),
        [](const auto& a) { return a.hasProposedDynamicEq; });
    REQUIRE(MetalcoreMixPass::applyAction(project, *dynAction));

    const auto json = mastering::project::serialize(project);
    const auto restored = mastering::project::deserialize(json);
    REQUIRE(restored.has_value());
    CHECK(restored->schemaVersion == mastering::project::kCurrentSchemaVersion);
    CHECK(restored->bpm == Catch::Approx(150.0));
    REQUIRE(restored->pairs.size() == project.pairs.size());
    REQUIRE(restored->buses.size() == project.buses.size());
    REQUIRE(restored->sections.size() == 1);
    REQUIRE(restored->mixPassActions.size() == project.mixPassActions.size());

    const auto* bass = MetalcoreMixPass::findTrack(*restored, TrackRole::bass);
    REQUIRE(bass != nullptr);
    if (dynAction->targetTrackId == "bass") {
        CHECK(bass->dynamicEqEnabled);
        CHECK(bass->dynamicEq.bands[0].maxCutDb
            == Catch::Approx(dynAction->proposedDynamicEq.bands[0].maxCutDb));
        CHECK(bass->dynamicEq.bands[0].sidechainSourceId == "kick");
    }
}

TEST_CASE("Role inference keeps Guitar L/R separate", "[milestone3a][project]")
{
    using mastering::project::inferRoleFromFilename;
    CHECK(inferRoleFromFilename("Rhythm GTR L.wav") == TrackRole::rhythmGuitarLeft);
    CHECK(inferRoleFromFilename("Rhythm GTR R.wav") == TrackRole::rhythmGuitarRight);
    CHECK(inferRoleFromFilename("Gtr_Left.wav") == TrackRole::rhythmGuitarLeft);
    CHECK(inferRoleFromFilename("Gtr_Right.wav") == TrackRole::rhythmGuitarRight);
}
