#include "mastering/assistant/MixAdvisor.h"
#include "mastering/project/ProjectDocument.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ranges>

TEST_CASE("Project documents round trip without losing track state", "[project]")
{
    mastering::project::ProjectDocument project;
    project.id = "project-1";
    project.name = "Metalcore Mix";
    project.sampleRate = 48'000.0;

    mastering::project::TrackRecord track;
    track.id = "track-1";
    track.name = "Bass DI";
    track.audioPath = "stems/bass.wav";
    track.role = mastering::project::TrackRole::bass;
    track.metrics.rmsDbfs = -24.0;
    track.processing.saturation = 0.2;
    track.gainDb = 1.5;
    project.tracks.push_back(track);

    const auto restored = mastering::project::deserialize(
        mastering::project::serialize(project));

    REQUIRE(restored.has_value());
    CHECK(restored->name == project.name);
    REQUIRE(restored->tracks.size() == 1);
    CHECK(restored->tracks.front().role == mastering::project::TrackRole::bass);
    CHECK(restored->tracks.front().gainDb == Catch::Approx(1.5));
    CHECK(restored->tracks.front().processing.saturation == Catch::Approx(0.2));
}

TEST_CASE("Track role inference recognizes FL Studio stem names", "[project]")
{
    using mastering::project::TrackRole;
    CHECK(mastering::project::inferRoleFromFilename("05 Kick In.wav") == TrackRole::kick);
    CHECK(mastering::project::inferRoleFromFilename("Bass GRIT.wav") == TrackRole::bass);
    CHECK(mastering::project::inferRoleFromFilename("Lead Vox Print.wav") == TrackRole::cleanVocal);
    CHECK(mastering::project::inferRoleFromFilename("Rhythm GTR L.wav") == TrackRole::rhythmGuitar);
}

TEST_CASE("Mix advisor bounds automatic gain and reports sub conflict", "[assistant]")
{
    mastering::project::ProjectDocument project;
    project.id = "project-1";

    mastering::project::TrackRecord kick;
    kick.id = "kick";
    kick.role = mastering::project::TrackRole::kick;
    kick.metrics.rmsDbfs = -40.0;
    kick.metrics.spectrum.subDb = -20.0;

    mastering::project::TrackRecord bass;
    bass.id = "bass";
    bass.role = mastering::project::TrackRole::bass;
    bass.metrics.rmsDbfs = -18.0;
    bass.metrics.spectrum.subDb = -21.0;

    project.tracks = {kick, bass};
    const mastering::assistant::MixAdvisor advisor;
    const auto plan = advisor.createPlan(project);

    REQUIRE(plan.trackAdjustments.size() == 2);
    CHECK(plan.trackAdjustments.front().gainDeltaDb == Catch::Approx(9.0));
    CHECK_FALSE(plan.suggestions.empty());
}

TEST_CASE("Mix advisor reports vocal-to-bed imbalance", "[assistant]")
{
    mastering::project::ProjectDocument project;
    project.id = "project-2";

    mastering::project::TrackRecord vocal;
    vocal.id = "vocal";
    vocal.role = mastering::project::TrackRole::cleanVocal;
    vocal.metrics.rmsDbfs = -30.0;

    mastering::project::TrackRecord guitars;
    guitars.id = "guitars";
    guitars.role = mastering::project::TrackRole::rhythmGuitar;
    guitars.metrics.rmsDbfs = -20.0;

    project.tracks = {vocal, guitars};
    const mastering::assistant::MixAdvisor advisor;
    const auto plan = advisor.createPlan(project);

    const auto vocalSuggestion = std::ranges::find_if(plan.suggestions, [](const auto& suggestion) {
        return suggestion.title == "Raise vocal-to-bed clarity";
    });
    REQUIRE(vocalSuggestion != plan.suggestions.end());
}
