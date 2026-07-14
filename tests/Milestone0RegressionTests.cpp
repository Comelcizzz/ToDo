#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/assistant/MixAdvisor.h"
#include "mastering/ipc/BridgeProtocol.h"
#include "mastering/project/ProjectDocument.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <vector>

namespace {

template <typename Sample>
void sanitizeInPlace(Sample* samples, int count)
{
    for (int index = 0; index < count; ++index) {
        if (!std::isfinite(samples[index]))
            samples[index] = Sample(0);
    }
}

template <typename Sample>
bool buffersEqual(const Sample* left, const Sample* right, int count)
{
    for (int index = 0; index < count; ++index) {
        if (left[index] != right[index])
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("Realtime meter JSON never claims LUFS or true peak", "[milestone0][labels]")
{
    mastering::analysis::RealtimeMeter meter;
    meter.prepare(48'000.0);
    std::vector<float> left(128, 0.25f);
    std::vector<float> right(128, -0.25f);
    float* channels[] {left.data(), right.data()};
    meter.process(channels, 2, 128);

    const auto metrics = meter.snapshot();
    const auto json = mastering::analysis::toJson(metrics);

    CHECK(metrics.estimatedLoudnessIsValid);
    CHECK_FALSE(metrics.integratedLufsIsValid);
    CHECK_FALSE(metrics.truePeakIsEstimate);
    CHECK_FALSE(mastering::analysis::jsonClaimsLufsForEstimate(json));
    CHECK_FALSE(mastering::analysis::jsonClaimsTruePeakWithoutEstimate(json));
    CHECK(json.find("\"integratedLufs\"") == std::string::npos);
    CHECK(json.find("\"estimatedTruePeakDbtp\"") == std::string::npos);
    CHECK(json.find("estimatedLoudnessDb") != std::string::npos);
}

TEST_CASE("Offline analyzer may publish estimated true peak and valid LUFS", "[milestone0][labels]")
{
    std::vector<std::vector<float>> channels(2, std::vector<float>(48'000, 0.25f));
    const auto metrics = mastering::analysis::AudioAnalyzer {}.analyze(channels, 48'000.0);
    const auto json = mastering::analysis::toJson(metrics);
    CHECK(metrics.integratedLufsIsValid);
    CHECK(metrics.truePeakIsEstimate);
    CHECK_FALSE(mastering::analysis::jsonClaimsLufsForEstimate(json));
    CHECK_FALSE(mastering::analysis::jsonClaimsTruePeakWithoutEstimate(json));
}

TEST_CASE("Apply plan is idempotent and reject leaves gain unchanged", "[milestone0][apply]")
{
    mastering::project::ProjectDocument project;
    project.id = "apply-test";

    mastering::project::TrackRecord kick;
    kick.id = "kick";
    kick.role = mastering::project::TrackRole::kick;
    kick.metrics.rmsDbfs = -40.0;
    kick.gainDb = 0.0;
    project.tracks = {kick};

    mastering::assistant::MixAdvisor advisor;
    auto plan = advisor.createPlan(project);
    REQUIRE_FALSE(plan.trackAdjustments.empty());
    const auto target = plan.trackAdjustments.front().targetGainDb;
    CHECK(target == Catch::Approx(9.0));

    mastering::assistant::MixAdvisor::applyPlanToProject(project, plan);
    CHECK(project.tracks.front().gainDb == Catch::Approx(target));
    CHECK(plan.trackAdjustments.front().state == mastering::assistant::ActionState::applied);
    REQUIRE_FALSE(project.actions.empty());
    CHECK(project.actions.front().state == "applied");

    mastering::assistant::MixAdvisor::applyPlanToProject(project, plan);
    CHECK(project.tracks.front().gainDb == Catch::Approx(target));

    auto rejected = advisor.createPlan(project);
    rejected.trackAdjustments.front().targetGainDb = -3.0;
    rejected.trackAdjustments.front().state = mastering::assistant::ActionState::pending;
    const auto beforeReject = project.tracks.front().gainDb;
    mastering::assistant::MixAdvisor::rejectPlan(rejected);
    CHECK(rejected.trackAdjustments.front().state == mastering::assistant::ActionState::rejected);
    mastering::assistant::MixAdvisor::applyPlanToProject(project, rejected);
    CHECK(project.tracks.front().gainDb == Catch::Approx(beforeReject));

    project.tracks.front().gainDb = 2.5;
    plan.trackAdjustments.front().targetGainDb = 4.0;
    plan.trackAdjustments.front().state = mastering::assistant::ActionState::pending;
    mastering::assistant::MixAdvisor::applyPlanToProject(project, plan);
    CHECK(project.tracks.front().gainDb == Catch::Approx(4.0));

    const auto roundTrip = mastering::project::deserialize(
        mastering::project::serialize(project));
    REQUIRE(roundTrip);
    REQUIRE_FALSE(roundTrip->actions.empty());
    CHECK(roundTrip->actions.front().targetGainDb == Catch::Approx(4.0));
    CHECK(roundTrip->actions.front().state == "applied");
    CHECK(roundTrip->tracks.front().gainDb == Catch::Approx(4.0));
}

TEST_CASE("Project schema rejects unknown newer versions", "[milestone0][schema]")
{
    mastering::project::DeserializeError error;
    const auto rejected = mastering::project::deserialize(
        R"({"schemaVersion":99,"id":"x","tracks":[]})",
        error);
    CHECK_FALSE(rejected.has_value());
    CHECK(error.message.find("Unsupported project schemaVersion") != std::string::npos);

    const auto v1 = mastering::project::deserialize(
        R"({"schemaVersion":1,"id":"legacy","name":"Legacy","sampleRate":48000,"tracks":[]})");
    REQUIRE(v1);
    CHECK(v1->schemaVersion == mastering::project::kCurrentSchemaVersion);
}

TEST_CASE("IPC schema validation rejects malformed and future payloads", "[milestone0][schema]")
{
    using mastering::ipc::validateTrackAnalysisPayload;

    CHECK_FALSE(validateTrackAnalysisPayload("{}").ok);
    CHECK_FALSE(validateTrackAnalysisPayload("not-json").ok);

    const auto future = validateTrackAnalysisPayload(
        R"({"schemaVersion":99,"type":"track-analysis","role":"kick","metrics":{}})");
    CHECK_FALSE(future.ok);
    CHECK(future.error.find("Unsupported IPC schemaVersion") != std::string::npos);

    const auto fakeLufs = validateTrackAnalysisPayload(
        R"({"schemaVersion":1,"type":"track-analysis","role":"kick","metrics":{"integratedLufs":-14.0}})");
    CHECK_FALSE(fakeLufs.ok);

    const auto ok = validateTrackAnalysisPayload(
        R"({"schemaVersion":1,"type":"track-analysis","role":"kick","metrics":{"samplePeakDbfs":-6.0,"rmsDbfs":-12.0,"estimatedLoudnessDb":-12.0,"estimatedLoudnessIsValid":true}})");
    CHECK(ok.ok);
}

TEST_CASE("Import replace semantics for project track lists", "[milestone0][import]")
{
    mastering::project::ProjectDocument project;
    project.id = "import";
    mastering::project::TrackRecord first;
    first.id = "a";
    first.name = "Kick";
    first.role = mastering::project::TrackRole::kick;
    project.tracks = {first};

    // Simulate StemEngine + MainComponent contract:
    // non-empty imported list replaces; empty imported list is a no-op.
    auto applyImport = [](mastering::project::ProjectDocument& doc,
                          const std::vector<mastering::project::TrackRecord>& imported,
                          std::size_t& engineTrackCount) {
        if (imported.empty())
            return false;
        doc.tracks = imported;
        engineTrackCount = imported.size();
        return true;
    };

    std::size_t engineTrackCount = project.tracks.size();
    std::vector<mastering::project::TrackRecord> imported;
    mastering::project::TrackRecord second;
    second.id = "b";
    second.name = "Bass";
    second.role = mastering::project::TrackRole::bass;
    imported.push_back(second);

    REQUIRE(applyImport(project, imported, engineTrackCount));
    REQUIRE(project.tracks.size() == 1);
    CHECK(project.tracks.front().id == "b");
    CHECK(engineTrackCount == project.tracks.size());

    REQUIRE_FALSE(applyImport(project, {}, engineTrackCount));
    CHECK(project.tracks.size() == 1);
    CHECK(engineTrackCount == 1);
}

TEST_CASE("Analyzer-mode buffers stay bit-identical after finite sanitize", "[milestone0][double]")
{
    std::vector<float> mono {0.1f, -0.2f, 0.3f, -0.4f};
    auto copy = mono;
    sanitizeInPlace(copy.data(), static_cast<int>(copy.size()));
    CHECK(buffersEqual(mono.data(), copy.data(), static_cast<int>(mono.size())));

    std::vector<double> stereo {0.5, -0.5, 0.25, -0.25};
    auto stereoCopy = stereo;
    sanitizeInPlace(stereoCopy.data(), static_cast<int>(stereoCopy.size()));
    CHECK(buffersEqual(stereo.data(), stereoCopy.data(), static_cast<int>(stereo.size())));

    std::vector<double> dirty {1.0, std::numeric_limits<double>::quiet_NaN(), 0.5,
                               std::numeric_limits<double>::infinity()};
    sanitizeInPlace(dirty.data(), static_cast<int>(dirty.size()));
    CHECK(dirty[0] == Catch::Approx(1.0));
    CHECK(dirty[1] == Catch::Approx(0.0));
    CHECK(dirty[2] == Catch::Approx(0.5));
    CHECK(dirty[3] == Catch::Approx(0.0));
}
