#include "mastering/research/ResearchExample.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

TEST_CASE("Research export strips identifying project data", "[research]")
{
    mastering::project::ProjectDocument project;
    project.id = "private-project-id";
    project.name = "Unreleased Song";

    mastering::project::TrackRecord track;
    track.id = "private-track-id";
    track.name = "Artist Bass DI";
    track.audioPath = "C:/Secret Album/Stems/Bass.wav";
    track.role = mastering::project::TrackRole::bass;
    track.metrics.rmsDbfs = -21.0;
    track.metrics.spectrum.bassDb = -12.0;
    track.gainDb = 1.5;
    track.processing.compressor.ratio = 3.0;
    project.tracks.push_back(track);

    const auto source = mastering::research::serializeExample(project, {"punchy", true});
    const auto value = nlohmann::json::parse(source);

    CHECK(mastering::research::isResearchExample(source));
    CHECK_FALSE(source.contains(project.name));
    CHECK_FALSE(source.contains(track.name));
    CHECK_FALSE(source.contains(track.audioPath));
    CHECK_FALSE(source.contains(project.id));
    REQUIRE(value["project"]["tracks"].size() == 1);
    CHECK(value["project"]["tracks"][0]["role"] == "bass");
    CHECK(value["project"]["tracks"][0]["approvedSettings"]["gainDb"] == 1.5);
    CHECK(value["privacy"]["audioIncluded"] == false);
}

TEST_CASE("Research export validator rejects arbitrary JSON", "[research]")
{
    CHECK_FALSE(mastering::research::isResearchExample("{}"));
    CHECK_FALSE(mastering::research::isResearchExample("not-json"));
}
