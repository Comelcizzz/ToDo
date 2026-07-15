#pragma once

#include "mastering/project/ProjectDocument.h"

#include <string>

namespace mastering::assistant {

enum class ActionRiskLevel {
    lowTechnical,
    conservativeCorrective,
    musicalCreative
};

struct AutoApplyPolicyConfig {
    double lowTechnicalEvidence {0.35};
    double conservativeEvidence {0.62};
    // Musical/creative never auto-applies unless explicitly enabled.
    bool allowMusicalAutoApply {false};
    double musicalEvidence {0.95};
    bool allowReferenceAutoApply {false};
    double maxCumulativeProcessingDb {12.0};
    bool respectConflictReject {true};
};

struct AutoApplyDecision {
    ActionRiskLevel riskLevel {ActionRiskLevel::conservativeCorrective};
    bool eligible {false};
    std::string reason;
    double requiredEvidence {0.0};
    double actualEvidence {0.0};
};

[[nodiscard]] std::string riskLevelToString(ActionRiskLevel level) noexcept;
[[nodiscard]] ActionRiskLevel classifyActionRisk(const project::MixPassAction& action) noexcept;
[[nodiscard]] double requiredEvidenceFor(
    ActionRiskLevel level,
    const AutoApplyPolicyConfig& config) noexcept;

// Annotate action risk fields and decide AUTO eligibility (does not apply).
[[nodiscard]] AutoApplyDecision evaluateAutoApply(
    project::MixPassAction& action,
    const AutoApplyPolicyConfig& config = {},
    double cumulativeProcessingDb = 0.0);

// Apply eligibility annotations to a whole action list (post-budget).
void annotateAutoApplyEligibility(
    std::vector<project::MixPassAction>& actions,
    const AutoApplyPolicyConfig& config = {});

} // namespace mastering::assistant
