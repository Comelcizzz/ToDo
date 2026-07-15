#pragma once

#include "mastering/assistant/MetalcoreMixPass.h"
#include "mastering/assistant/MetalcoreProfile.h"
#include "mastering/benchmark/ActionGraphDiff.h"
#include "mastering/benchmark/BenchmarkSessionManifest.h"
#include "mastering/project/ProjectDocument.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mastering::benchmark {

struct ExperimentDefinition {
    std::string experimentId;
    std::vector<std::string> sessionIds;
    std::string engineVersionA;
    std::string engineVersionB;
    std::string profileIdA {"modern-metalcore-balanced"};
    std::string profileIdB {"modern-metalcore-aggressive"};
    std::string profileJsonA;
    std::string profileJsonB;
    std::uint64_t randomSeed {42};
    std::vector<std::string> renderModes {"raw", "auto", "current"};
    std::string outputFolder;
    bool cancelled {false};
};

struct ExperimentRunResult {
    std::string experimentId;
    std::string runId;
    std::string engineVersion;
    std::string profileId;
    std::string profileRevision;
    std::vector<project::MixPassAction> actions;
    std::string actionGraphJson;
    std::string reproducibilityJson;
    std::string metricsJson;
    std::string outputFolder;
    bool cancelled {false};
    bool deterministicRerunMatch {false};
};

struct ExperimentComparison {
    ExperimentRunResult runA;
    ExperimentRunResult runB;
    ActionGraphDiffReport actionDiff;
    std::string metricsDiffJson;
    std::string listeningPackageFolder;
    std::string manifestJson;
};

// Headless experiment runner (no FL Studio). Uses MetalcoreMixPass + profiles.
class ExperimentRunner {
public:
    [[nodiscard]] static ExperimentRunResult runOnce(
        const project::ProjectDocument& project,
        const assistant::MetalcoreMixPass::AnalysisMap& analysis,
        const assistant::MetalcoreProfile& profile,
        const ExperimentDefinition& def,
        const std::string& sideLabel);

    [[nodiscard]] static ExperimentComparison compareProfiles(
        const project::ProjectDocument& project,
        const assistant::MetalcoreMixPass::AnalysisMap& analysis,
        const ExperimentDefinition& def);

    [[nodiscard]] static std::string buildReproducibilityManifest(
        const ExperimentRunResult& run,
        const BenchmarkSessionManifest& session,
        const assistant::MetalcoreProfile& profile);
};

} // namespace mastering::benchmark
