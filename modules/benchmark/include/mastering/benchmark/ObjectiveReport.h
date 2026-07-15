#pragma once

#include "mastering/benchmark/BenchmarkSessionManifest.h"
#include "mastering/project/ProjectDocument.h"

#include <string>
#include <vector>

namespace mastering::benchmark {

struct ObjectiveTechnicalMetrics {
    double durationSeconds {0.0};
    double sampleRate {0.0};
    double lufsI {-120.0};
    double truePeakDbtp {-120.0};
    double samplePeakDbfs {-120.0};
    bool hasDc {false};
    bool hasNanInf {false};
    bool clipping {false};
    std::string integritySha256;
};

struct ObjectiveProcessingMetrics {
    int actionCount {0};
    int actionsPerTrackMax {0};
    double cumulativeEqCutDb {0.0};
    double maxDynEqCutDb {0.0};
    int autoEligibleCount {0};
    int previewOnlyCount {0};
    int rejectedCount {0};
};

struct ObjectiveReport {
    ObjectiveTechnicalMetrics technical {};
    ObjectiveProcessingMetrics processing {};
    std::string balanceNotes;
    std::string disclaimer {
        "Objective metrics are not a MixQualityScore and do not prove musical quality."};
};

[[nodiscard]] ObjectiveReport buildObjectiveReport(
    const std::vector<project::MixPassAction>& actions,
    const ObjectiveTechnicalMetrics& technical);

[[nodiscard]] std::string serializeObjectiveReport(const ObjectiveReport& report);

struct DetectionEvalRow {
    std::string expectedProblem;
    std::string detectedActionId;
    bool targetCorrect {false};
    bool sectionCorrect {false};
    bool processorReasonable {false};
    std::string result; // detected|partial|missed|false-positive|wrong-target|excessive
};

struct DetectionEvaluationReport {
    std::vector<DetectionEvalRow> rows;
    int detected {0};
    int missed {0};
    int falsePositives {0};
    double recall {0.0};
    std::string disclaimer {
        "Detection metrics are not a musical quality score."};
};

// Expected annotations must NOT be passed into generation — evaluation only.
[[nodiscard]] DetectionEvaluationReport evaluateExpectedProblems(
    const std::vector<ExpectedProblemAnnotation>& expected,
    const std::vector<project::MixPassAction>& actions);

[[nodiscard]] std::string serializeDetectionEvaluation(const DetectionEvaluationReport& report);

} // namespace mastering::benchmark
