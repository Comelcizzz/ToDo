#include "mastering/assistant/AutoApplyPolicy.h"

#include <algorithm>
#include <cmath>

namespace mastering::assistant {
namespace {

bool containsCi(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

bool isReferenceDerived(const project::MixPassAction& action)
{
    return containsCi(action.problemType, "reference")
        || containsCi(action.origin, "reference")
        || containsCi(action.actionId, "reference");
}

bool isMusicalCreative(const project::MixPassAction& action)
{
    if (action.processorId == "saturation" || action.processorId == "stereo"
        || action.processorId == "clipper")
        return true;
    if (action.parameterId == "stereoWidth" || action.parameterId == "sideGainDb"
        || action.parameterId == "parallelWet" || action.parameterId == "wetAmount")
        return true;
    if (containsCi(action.problemType, "Width") || containsCi(action.problemType, "width")
        || containsCi(action.problemType, "Saturation") || containsCi(action.problemType, "Lift")
        || containsCi(action.problemType, "Parallel") || containsCi(action.problemType, "Density")
        || containsCi(action.problemType, "density"))
        return true;
    if (action.sectionScope != "full" && !action.sectionScope.empty()
        && (action.processorId == "gain" || action.processorId == "saturation"))
        return true;
    return isReferenceDerived(action);
}

bool isLowTechnical(const project::MixPassAction& action)
{
    return containsCi(action.problemType, "technical")
        || containsCi(action.problemType, "NaN")
        || containsCi(action.problemType, "Inf")
        || containsCi(action.problemType, "DC")
        || containsCi(action.problemType, "polarity")
        || containsCi(action.problemType, "safety")
        || (action.processorId == "outputGain" && containsCi(action.problemType, "protect"));
}

} // namespace

std::string riskLevelToString(ActionRiskLevel level) noexcept
{
    switch (level) {
    case ActionRiskLevel::lowTechnical:
        return "lowTechnical";
    case ActionRiskLevel::musicalCreative:
        return "musicalCreative";
    case ActionRiskLevel::conservativeCorrective:
    default:
        return "conservativeCorrective";
    }
}

ActionRiskLevel classifyActionRisk(const project::MixPassAction& action) noexcept
{
    if (isLowTechnical(action))
        return ActionRiskLevel::lowTechnical;
    if (isMusicalCreative(action))
        return ActionRiskLevel::musicalCreative;
    return ActionRiskLevel::conservativeCorrective;
}

double requiredEvidenceFor(
    ActionRiskLevel level,
    const AutoApplyPolicyConfig& config) noexcept
{
    switch (level) {
    case ActionRiskLevel::lowTechnical:
        return config.lowTechnicalEvidence;
    case ActionRiskLevel::musicalCreative:
        return config.musicalEvidence;
    case ActionRiskLevel::conservativeCorrective:
    default:
        return config.conservativeEvidence;
    }
}

AutoApplyDecision evaluateAutoApply(
    project::MixPassAction& action,
    const AutoApplyPolicyConfig& config,
    double cumulativeProcessingDb)
{
    AutoApplyDecision d;
    d.riskLevel = classifyActionRisk(action);
    d.actualEvidence = action.evidenceScore > 0.0 ? action.evidenceScore : action.confidence;
    d.requiredEvidence = requiredEvidenceFor(d.riskLevel, config);

    action.riskLevel = riskLevelToString(d.riskLevel);
    action.requiredEvidence = d.requiredEvidence;
    action.actualEvidence = d.actualEvidence;

    if (config.respectConflictReject && action.state == "rejected") {
        d.eligible = false;
        d.reason = "rejected-by-conflict-or-budget";
        action.autoApplyEligibility = false;
        action.autoApplyReason = d.reason;
        return d;
    }

    if (action.processorId == "none") {
        d.eligible = false;
        d.reason = "informational-action";
        action.autoApplyEligibility = false;
        action.autoApplyReason = d.reason;
        return d;
    }

    if (cumulativeProcessingDb > config.maxCumulativeProcessingDb) {
        d.eligible = false;
        d.reason = "cumulative-processing-cap";
        action.autoApplyEligibility = false;
        action.autoApplyReason = d.reason;
        return d;
    }

    if (d.riskLevel == ActionRiskLevel::musicalCreative && !config.allowMusicalAutoApply) {
        d.eligible = false;
        d.reason = "musical-creative-requires-preview";
        action.autoApplyEligibility = false;
        action.autoApplyReason = d.reason;
        return d;
    }

    if (isReferenceDerived(action) && !config.allowReferenceAutoApply
        && d.riskLevel != ActionRiskLevel::lowTechnical) {
        d.eligible = false;
        d.reason = "reference-derived-requires-preview";
        action.autoApplyEligibility = false;
        action.autoApplyReason = d.reason;
        return d;
    }

    if (d.actualEvidence < d.requiredEvidence) {
        d.eligible = false;
        d.reason = "evidence-below-risk-threshold";
        action.autoApplyEligibility = false;
        action.autoApplyReason = d.reason;
        return d;
    }

    d.eligible = true;
    d.reason = std::string("auto-eligible:") + riskLevelToString(d.riskLevel)
        + ":evidence>=" + std::to_string(d.requiredEvidence);
    action.autoApplyEligibility = true;
    action.autoApplyReason = d.reason;
    return d;
}

void annotateAutoApplyEligibility(
    std::vector<project::MixPassAction>& actions,
    const AutoApplyPolicyConfig& config)
{
    double cumulative = 0.0;
    for (auto& action : actions) {
        if (action.state == "rejected") {
            (void) evaluateAutoApply(action, config, cumulative);
            continue;
        }
        (void) evaluateAutoApply(action, config, cumulative);
        if (action.autoApplyEligibility) {
            cumulative += std::abs(action.proposedValue - action.currentValue);
        }
    }
}

} // namespace mastering::assistant
