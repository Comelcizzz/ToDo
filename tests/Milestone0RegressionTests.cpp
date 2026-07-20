#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/analysis/PassThroughPolicy.h"
#include "mastering/assistant/MixAdvisor.h"
#include "mastering/ipc/BridgeProtocol.h"
#include "mastering/project/ProjectDocument.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace {

template <typename Sample>
std::vector<Sample> makeRandomFinite(int samples, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(-0.9, 0.9);
    std::vector<Sample> values(static_cast<std::size_t>(samples));
    for (auto& value : values)
        value = static_cast<Sample>(dist(rng));
    return values;
}

template <typename Sample>
void runPassThroughCase(int channels, int blockSize, unsigned seed)
{
    std::vector<std::vector<Sample>> buffers;
    std::vector<std::vector<Sample>> originals;
    std::vector<Sample*> pointers;
    buffers.reserve(static_cast<std::size_t>(channels));
    originals.reserve(static_cast<std::size_t>(channels));
    for (int channel = 0; channel < channels; ++channel) {
        buffers.push_back(makeRandomFinite<Sample>(blockSize, seed + static_cast<unsigned>(channel)));
        originals.push_back(buffers.back());
        pointers.push_back(buffers.back().data());
    }

    mastering::analysis::applyAnalyzerSanitizeOnly(pointers.data(), channels, blockSize);

    for (int channel = 0; channel < channels; ++channel) {
        CHECK(mastering::analysis::buffersBitIdentical(
            buffers[static_cast<std::size_t>(channel)].data(),
            originals[static_cast<std::size_t>(channel)].data(),
            blockSize));
    }
}

mastering::project::TrackRecord makeTrack(std::string id, double gainDb = 0.0)
{
    mastering::project::TrackRecord track;
    track.id = std::move(id);
    track.name = track.id;
    track.role = mastering::project::TrackRole::kick;
    track.gainDb = gainDb;
    track.metrics.rmsDbfs = -40.0;
    return track;
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
    // Reconstructed peak remains an estimate until official TP vectors pass (Variant B).
    CHECK_FALSE(mastering::analysis::jsonClaimsLufsForEstimate(json));
    CHECK(json.find("\"integratedLufs\"") == std::string::npos);
    CHECK(json.find("estimatedLoudnessDb") != std::string::npos);
}

TEST_CASE("Offline analyzer publishes BS.1770-style loudness and true peak", "[milestone0][labels]")
{
    std::vector<std::vector<float>> channels(2, std::vector<float>(48'000, 0.25f));
    const auto metrics = mastering::analysis::AudioAnalyzer {}.analyze(channels, 48'000.0);
    const auto json = mastering::analysis::toJson(metrics);
    CHECK(metrics.integratedLufsIsValid);
    CHECK(metrics.truePeakValid);
    CHECK_FALSE(metrics.truePeakIsEstimate);
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
    plan.trackAdjustments.front().hasPrevious = false;
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

TEST_CASE("Absolute Apply contract for gain and EQ processor state", "[milestone0][apply]")
{
    mastering::project::ProjectDocument project;
    project.id = "absolute-contract";
    project.tracks = {makeTrack("kick", -6.0)};
    project.tracks.front().processing.equalizer.presenceGainDb = 1.0;

    mastering::assistant::MixPlan plan;
    mastering::assistant::TrackAdjustment gainAction;
    gainAction.actionId = "gain-v1";
    gainAction.trackId = "kick";
    gainAction.targetGainDb = 3.0;
    gainAction.processing = project.tracks.front().processing;
    gainAction.processing.equalizer.presenceGainDb = 1.0;
    plan.trackAdjustments.push_back(gainAction);

    mastering::assistant::MixAdvisor::applyPlanToProject(project, plan);
    CHECK(project.tracks.front().gainDb == Catch::Approx(3.0));
    CHECK(plan.trackAdjustments.front().hasPrevious);
    CHECK(plan.trackAdjustments.front().previousGainDb == Catch::Approx(-6.0));

    // Apply gain twice — absolute, not accumulated.
    mastering::assistant::MixAdvisor::applyPlanToProject(project, plan);
    CHECK(project.tracks.front().gainDb == Catch::Approx(3.0));

    // Apply EQ action twice on synthetic processor state.
    mastering::assistant::MixPlan eqPlan;
    mastering::assistant::TrackAdjustment eqAction;
    eqAction.actionId = "eq-v1";
    eqAction.trackId = "kick";
    eqAction.targetGainDb = 3.0;
    eqAction.processing = project.tracks.front().processing;
    eqAction.processing.equalizer.presenceGainDb = -2.5;
    eqAction.processing.equalizer.presenceHz = 2'500.0;
    eqPlan.trackAdjustments.push_back(eqAction);

    mastering::assistant::MixAdvisor::applyPlanToProject(project, eqPlan);
    CHECK(project.tracks.front().processing.equalizer.presenceGainDb == Catch::Approx(-2.5));
    mastering::assistant::MixAdvisor::applyPlanToProject(project, eqPlan);
    CHECK(project.tracks.front().processing.equalizer.presenceGainDb == Catch::Approx(-2.5));
    CHECK(project.tracks.front().gainDb == Catch::Approx(3.0));

    // Apply → Edit → Apply (new absolute values).
    eqPlan.trackAdjustments.front().state = mastering::assistant::ActionState::pending;
    eqPlan.trackAdjustments.front().hasPrevious = false;
    eqPlan.trackAdjustments.front().targetGainDb = 1.5;
    eqPlan.trackAdjustments.front().processing.equalizer.presenceGainDb = 0.75;
    mastering::assistant::MixAdvisor::applyPlanToProject(project, eqPlan);
    CHECK(project.tracks.front().gainDb == Catch::Approx(1.5));
    CHECK(project.tracks.front().processing.equalizer.presenceGainDb == Catch::Approx(0.75));

    // Apply → Reject: Reject after Apply does not revert DSP.
    mastering::assistant::MixAdvisor::rejectPlan(eqPlan);
    CHECK(eqPlan.trackAdjustments.front().state == mastering::assistant::ActionState::applied);
    CHECK(project.tracks.front().gainDb == Catch::Approx(1.5));

    // Apply → Save → Reopen preserves applied absolute values.
    const auto restored = mastering::project::deserialize(
        mastering::project::serialize(project));
    REQUIRE(restored);
    CHECK(restored->tracks.front().gainDb == Catch::Approx(1.5));
    CHECK(restored->tracks.front().processing.equalizer.presenceGainDb == Catch::Approx(0.75));
    REQUIRE_FALSE(restored->actions.empty());
    CHECK(restored->actions.back().state == "applied");
    CHECK(restored->actions.back().hasPrevious);

    // Two different actions on different parameters (gain vs presence).
    mastering::assistant::MixPlan multi;
    mastering::assistant::TrackAdjustment a;
    a.actionId = "gain-v2";
    a.trackId = "kick";
    a.targetGainDb = -1.0;
    a.processing = project.tracks.front().processing;
    mastering::assistant::TrackAdjustment b = a;
    b.actionId = "eq-v2";
    b.targetGainDb = -1.0;
    b.processing.equalizer.highShelfGainDb = -3.0;
    multi.trackAdjustments = {a, b};
    mastering::assistant::MixAdvisor::applyPlanToProject(project, multi);
    CHECK(project.tracks.front().gainDb == Catch::Approx(-1.0));
    CHECK(project.tracks.front().processing.equalizer.highShelfGainDb == Catch::Approx(-3.0));

    // New version of the same logical action (new actionId, absolute replace).
    mastering::assistant::MixPlan v3;
    mastering::assistant::TrackAdjustment next = b;
    next.actionId = "eq-v3";
    next.processing.equalizer.highShelfGainDb = -1.0;
    next.state = mastering::assistant::ActionState::pending;
    next.hasPrevious = false;
    v3.trackAdjustments = {next};
    mastering::assistant::MixAdvisor::applyPlanToProject(project, v3);
    CHECK(project.tracks.front().processing.equalizer.highShelfGainDb == Catch::Approx(-1.0));

    // Duplicate action ID: applied in order; last absolute write wins.
    mastering::assistant::MixPlan dup;
    mastering::assistant::TrackAdjustment first = next;
    first.actionId = "dup";
    first.targetGainDb = 2.0;
    first.hasPrevious = false;
    first.state = mastering::assistant::ActionState::pending;
    mastering::assistant::TrackAdjustment second = first;
    second.targetGainDb = 5.0;
    dup.trackAdjustments = {first, second};
    mastering::assistant::MixAdvisor::applyPlanToProject(project, dup);
    CHECK(project.tracks.front().gainDb == Catch::Approx(5.0));

    // Unknown target ID is skipped; existing track unchanged.
    const auto beforeUnknown = project.tracks.front().gainDb;
    mastering::assistant::MixPlan unknown;
    mastering::assistant::TrackAdjustment missing;
    missing.actionId = "ghost";
    missing.trackId = "does-not-exist";
    missing.targetGainDb = 99.0;
    unknown.trackAdjustments = {missing};
    mastering::assistant::MixAdvisor::applyPlanToProject(project, unknown);
    CHECK(project.tracks.front().gainDb == Catch::Approx(beforeUnknown));

    // Reject pending action before Apply does not change DSP.
    project.tracks.front().gainDb = 0.25;
    mastering::assistant::MixPlan pendingReject;
    mastering::assistant::TrackAdjustment pending;
    pending.actionId = "pending-reject";
    pending.trackId = "kick";
    pending.targetGainDb = 12.0;
    pendingReject.trackAdjustments = {pending};
    mastering::assistant::MixAdvisor::rejectPlan(pendingReject);
    CHECK(pendingReject.trackAdjustments.front().state
        == mastering::assistant::ActionState::rejected);
    mastering::assistant::MixAdvisor::applyPlanToProject(project, pendingReject);
    CHECK(project.tracks.front().gainDb == Catch::Approx(0.25));
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

TEST_CASE("Analyzer float path is bit-transparent for finite audio", "[milestone0][bit-transparency][float]")
{
    SECTION("random stereo") { runPassThroughCase<float>(2, 128, 11); }
    SECTION("random mono") { runPassThroughCase<float>(1, 64, 22); }
    SECTION("silence")
    {
        std::vector<float> left(256, 0.0f);
        auto copy = left;
        float* channels[] {left.data()};
        mastering::analysis::applyAnalyzerSanitizeOnly(channels, 1, 256);
        CHECK(mastering::analysis::buffersBitIdentical(left.data(), copy.data(), 256));
    }
    SECTION("denormal-sized input stays unchanged in memory")
    {
        std::vector<float> samples {std::numeric_limits<float>::denorm_min(),
                                    -std::numeric_limits<float>::denorm_min(),
                                    1.0e-40f};
        auto copy = samples;
        float* channels[] {samples.data()};
        mastering::analysis::applyAnalyzerSanitizeOnly(channels, 1, static_cast<int>(samples.size()));
        CHECK(mastering::analysis::buffersBitIdentical(
            samples.data(), copy.data(), static_cast<int>(samples.size())));
    }
    SECTION("multiple block sizes")
    {
        for (int block : {32, 64, 128, 256, 512, 1024})
            runPassThroughCase<float>(2, block, 1000u + static_cast<unsigned>(block));
    }
}

TEST_CASE("Analyzer double path is bit-transparent for finite audio", "[milestone0][bit-transparency][double]")
{
    SECTION("random stereo") { runPassThroughCase<double>(2, 128, 31); }
    SECTION("random mono") { runPassThroughCase<double>(1, 96, 32); }
    SECTION("silence")
    {
        std::vector<double> left(128, 0.0);
        auto copy = left;
        double* channels[] {left.data()};
        mastering::analysis::applyAnalyzerSanitizeOnly(channels, 1, 128);
        CHECK(mastering::analysis::buffersBitIdentical(left.data(), copy.data(), 128));
    }
    SECTION("multiple block sizes")
    {
        for (int block : {32, 64, 128, 256, 512, 1024})
            runPassThroughCase<double>(2, block, 2000u + static_cast<unsigned>(block));
    }
}

TEST_CASE("Non-finite sanitization is separate from bit-transparency", "[milestone0][bit-transparency][sanitize]")
{
    std::vector<float> dirty {1.0f, std::numeric_limits<float>::quiet_NaN(), -0.5f,
                              std::numeric_limits<float>::infinity()};
    float* channels[] {dirty.data()};
    mastering::analysis::applyAnalyzerSanitizeOnly(channels, 1, 4);
    CHECK(dirty[0] == Catch::Approx(1.0f));
    CHECK(dirty[1] == Catch::Approx(0.0f));
    CHECK(dirty[2] == Catch::Approx(-0.5f));
    CHECK(dirty[3] == Catch::Approx(0.0f));

    std::vector<double> dirty64 {1.0, std::numeric_limits<double>::quiet_NaN(), 0.5,
                                 -std::numeric_limits<double>::infinity()};
    double* channels64[] {dirty64.data()};
    mastering::analysis::applyAnalyzerSanitizeOnly(channels64, 1, 4);
    CHECK(dirty64[0] == Catch::Approx(1.0));
    CHECK(dirty64[1] == Catch::Approx(0.0));
    CHECK(dirty64[2] == Catch::Approx(0.5));
    CHECK(dirty64[3] == Catch::Approx(0.0));
}
