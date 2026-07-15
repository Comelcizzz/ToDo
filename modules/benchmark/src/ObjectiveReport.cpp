#include "mastering/benchmark/ObjectiveReport.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

namespace mastering::benchmark {

ObjectiveReport buildObjectiveReport(
    const std::vector<project::MixPassAction>& actions,
    const ObjectiveTechnicalMetrics& technical)
{
    ObjectiveReport report;
    report.technical = technical;
    report.processing.actionCount = static_cast<int>(actions.size());
    for (const auto& a : actions) {
        if (a.state == "rejected") {
            ++report.processing.rejectedCount;
            continue;
        }
        if (a.autoApplyEligibility)
            ++report.processing.autoEligibleCount;
        else
            ++report.processing.previewOnlyCount;
        if (a.hasProposedDynamicEq) {
            report.processing.maxDynEqCutDb = std::max(
                report.processing.maxDynEqCutDb,
                a.proposedDynamicEq.bands[0].maxCutDb);
            report.processing.cumulativeEqCutDb += a.proposedDynamicEq.bands[0].maxCutDb;
        }
    }
    report.balanceNotes =
        "Balance/dynamics fields populated by render analysis when available; "
        "no single MixQualityScore is produced.";
    return report;
}

std::string serializeObjectiveReport(const ObjectiveReport& report)
{
    nlohmann::json j;
    j["disclaimer"] = report.disclaimer;
    j["technical"] = {
        {"durationSeconds", report.technical.durationSeconds},
        {"sampleRate", report.technical.sampleRate},
        {"lufsI", report.technical.lufsI},
        {"truePeakDbtp", report.technical.truePeakDbtp},
        {"samplePeakDbfs", report.technical.samplePeakDbfs},
        {"hasDc", report.technical.hasDc},
        {"hasNanInf", report.technical.hasNanInf},
        {"clipping", report.technical.clipping},
        {"integritySha256", report.technical.integritySha256}};
    j["processing"] = {
        {"actionCount", report.processing.actionCount},
        {"autoEligibleCount", report.processing.autoEligibleCount},
        {"previewOnlyCount", report.processing.previewOnlyCount},
        {"rejectedCount", report.processing.rejectedCount},
        {"cumulativeEqCutDb", report.processing.cumulativeEqCutDb},
        {"maxDynEqCutDb", report.processing.maxDynEqCutDb}};
    j["balanceNotes"] = report.balanceNotes;
    return j.dump(2);
}

DetectionEvaluationReport evaluateExpectedProblems(
    const std::vector<ExpectedProblemAnnotation>& expected,
    const std::vector<project::MixPassAction>& actions)
{
    DetectionEvaluationReport report;
    for (const auto& exp : expected) {
        DetectionEvalRow row;
        row.expectedProblem = exp.category + ":" + exp.problemId;
        bool found = false;
        for (const auto& a : actions) {
            if (a.state == "rejected")
                continue;
            const bool catHit = a.problemType.find(exp.category) != std::string::npos
                || (exp.category == "kick/bass"
                    && (a.problemType.find("kick") != std::string::npos
                        || a.problemType.find("bass") != std::string::npos));
            if (!catHit)
                continue;
            found = true;
            row.detectedActionId = a.actionId;
            row.targetCorrect = exp.targetAssetId.empty()
                || a.targetTrackId == exp.targetAssetId;
            row.sectionCorrect = exp.sectionId.empty()
                || a.sectionScope == exp.sectionId
                || (exp.sectionId.empty() && (a.sectionScope == "full" || a.sectionScope.empty()));
            row.processorReasonable = a.processorId != "none";
            if (row.targetCorrect && row.sectionCorrect)
                row.result = "detected";
            else if (row.targetCorrect || row.sectionCorrect)
                row.result = "partial";
            else
                row.result = "wrong-target";
            break;
        }
        if (!found) {
            row.result = "missed";
            ++report.missed;
        } else if (row.result == "detected" || row.result == "partial") {
            ++report.detected;
        }
        report.rows.push_back(std::move(row));
    }
    // Crude FP: actions with no matching expected category when expectations exist.
    if (!expected.empty()) {
        for (const auto& a : actions) {
            if (a.state == "rejected" || a.processorId == "none")
                continue;
            bool matched = false;
            for (const auto& exp : expected) {
                if (a.problemType.find(exp.category) != std::string::npos
                    || (exp.category == "kick/bass"
                        && (a.problemType.find("kick") != std::string::npos
                            || a.problemType.find("bass") != std::string::npos)))
                    matched = true;
            }
            if (!matched)
                ++report.falsePositives;
        }
    }
    const int total = static_cast<int>(expected.size());
    report.recall = total > 0 ? double(report.detected) / double(total) : 0.0;
    return report;
}

std::string serializeDetectionEvaluation(const DetectionEvaluationReport& report)
{
    nlohmann::json j;
    j["disclaimer"] = report.disclaimer;
    j["detected"] = report.detected;
    j["missed"] = report.missed;
    j["falsePositives"] = report.falsePositives;
    j["recall"] = report.recall;
    j["rows"] = nlohmann::json::array();
    for (const auto& r : report.rows) {
        j["rows"].push_back({
            {"expectedProblem", r.expectedProblem},
            {"detectedAction", r.detectedActionId},
            {"targetCorrect", r.targetCorrect},
            {"sectionCorrect", r.sectionCorrect},
            {"processorReasonable", r.processorReasonable},
            {"result", r.result}});
    }
    return j.dump(2);
}

} // namespace mastering::benchmark
