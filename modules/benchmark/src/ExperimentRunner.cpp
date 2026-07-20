#include "mastering/benchmark/ExperimentRunner.h"

#include "mastering/assistant/MetalcoreMixPass.h"
#include "mastering/product/ProductVersion.h"

#include <chrono>

#include <nlohmann/json.hpp>

namespace mastering::benchmark {

ExperimentRunResult ExperimentRunner::runOnce(
    const project::ProjectDocument& project,
    const assistant::MetalcoreMixPass::AnalysisMap& analysis,
    const assistant::MetalcoreProfile& profile,
    const ExperimentDefinition& def,
    const std::string& sideLabel)
{
    ExperimentRunResult result;
    result.experimentId = def.experimentId;
    result.runId = def.experimentId + "-" + sideLabel;
    result.engineVersion = product::currentProductVersion().full();
    result.profileId = profile.profileId;
    result.profileRevision = profile.revision;
    result.outputFolder = def.outputFolder;
    result.cancelled = def.cancelled;
    if (def.cancelled)
        return result;

    assistant::MetalcoreMixPass mixPass;
    assistant::MetalcoreMixPass::Options options;
    options.bpm = project.bpm;
    options.allowSyntheticFrequencyFallback = false;
    options.profile = profile;

    std::vector<assistant::ReferenceProfile> refs;
    result.actions = mixPass.generateActions(project, analysis, refs, options);
    result.actionGraphJson = assistant::mixPassActionsToJson(result.actions);

    nlohmann::json metrics;
    int accepted = 0, previewOnly = 0, rejected = 0;
    for (const auto& a : result.actions) {
        if (a.state == "rejected")
            ++rejected;
        else if (a.autoApplyEligibility)
            ++accepted;
        else
            ++previewOnly;
    }
    metrics["actionCount"] = result.actions.size();
    metrics["autoEligible"] = accepted;
    metrics["previewOnly"] = previewOnly;
    metrics["rejected"] = rejected;
    metrics["profileId"] = profile.profileId;
    metrics["profileRevision"] = profile.revision;
    metrics["engineVersion"] = result.engineVersion;
    result.metricsJson = metrics.dump(2);
    return result;
}

ExperimentComparison ExperimentRunner::compareProfiles(
    const project::ProjectDocument& project,
    const assistant::MetalcoreMixPass::AnalysisMap& analysis,
    const ExperimentDefinition& def)
{
    ExperimentComparison cmp;
    auto profileA = assistant::defaultBalancedProfile();
    auto profileB = assistant::defaultAggressiveProfile();
    if (!def.profileJsonA.empty()) {
        if (auto p = assistant::deserializeProfile(def.profileJsonA))
            profileA = *p;
    }
    if (!def.profileJsonB.empty()) {
        if (auto p = assistant::deserializeProfile(def.profileJsonB))
            profileB = *p;
    }
    if (def.profileIdA == "modern-metalcore-aggressive")
        profileA = assistant::defaultAggressiveProfile();
    if (def.profileIdB == "modern-metalcore-balanced")
        profileB = assistant::defaultBalancedProfile();

    cmp.runA = runOnce(project, analysis, profileA, def, "A");
    cmp.runB = runOnce(project, analysis, profileB, def, "B");
    cmp.actionDiff = diffActionGraphs(cmp.runA.actions, cmp.runB.actions);

    nlohmann::json md;
    md["runA"] = nlohmann::json::parse(cmp.runA.metricsJson);
    md["runB"] = nlohmann::json::parse(cmp.runB.metricsJson);
    md["note"] = "Metric change is not automatically an improvement";
    cmp.metricsDiffJson = md.dump(2);

    nlohmann::json man;
    man["experimentId"] = def.experimentId;
    man["seed"] = def.randomSeed;
    man["profileA"] = profileA.profileId;
    man["profileB"] = profileB.profileId;
    man["engine"] = product::currentProductVersion().full();
    cmp.manifestJson = man.dump(2);

    // Deterministic rerun check on A.
    const auto rerun = runOnce(project, analysis, profileA, def, "A-rerun");
    cmp.runA.deterministicRerunMatch = (rerun.actionGraphJson == cmp.runA.actionGraphJson);
    return cmp;
}

std::string ExperimentRunner::buildReproducibilityManifest(
    const ExperimentRunResult& run,
    const BenchmarkSessionManifest& session,
    const assistant::MetalcoreProfile& profile)
{
    nlohmann::json j;
    j["engineVersion"] = run.engineVersion;
    j["product"] = nlohmann::json::parse(product::currentProductVersion().toJson());
    j["profileId"] = profile.profileId;
    j["profileRevision"] = profile.revision;
    j["profileSchemaVersion"] = profile.schemaVersion;
    j["sessionId"] = session.sessionId;
    j["projectSchema"] = session.schemaVersion;
    j["runId"] = run.runId;
    j["actionGraph"] = nlohmann::json::parse(
        run.actionGraphJson.empty() ? "[]" : run.actionGraphJson);
    j["sourceAssets"] = nlohmann::json::array();
    for (const auto& stem : session.stems) {
        j["sourceAssets"].push_back({
            {"assetId", stem.assetId},
            {"fingerprint", stem.contentFingerprint},
            {"path", stem.filePath}});
    }
    j["localOnly"] = session.localOnly;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    return j.dump(2);
}

} // namespace mastering::benchmark
