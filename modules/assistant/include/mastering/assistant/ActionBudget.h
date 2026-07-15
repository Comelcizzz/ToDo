#pragma once

#include "mastering/project/ProjectDocument.h"

#include <string>
#include <vector>

namespace mastering::assistant {

struct ActionBudget {
    int maxActionsPerTrack {6};
    int maxDynEqBandsPerTrack {2};
    double maxCumulativeEqCutDb {10.0};
    double maxGainChangeDb {6.0};
    double maxSectionOffsetDb {3.0};
    double maxTotalUnmaskDb {7.0};
    double minEvidence {0.35};
};

// Sort by evidenceScore*priority (desc), keep within limits, reject rest with reason.
[[nodiscard]] std::vector<project::MixPassAction> applyBudget(
    std::vector<project::MixPassAction> actions,
    const ActionBudget& budget,
    std::vector<std::string>& rejectedReasons);

} // namespace mastering::assistant
