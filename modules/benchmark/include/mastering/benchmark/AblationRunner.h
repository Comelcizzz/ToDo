#pragma once

#include "mastering/project/ProjectDocument.h"

#include <string>
#include <vector>

namespace mastering::benchmark {

enum class AblationCategory {
    none,
    kickBass,
    guitars,
    vocals,
    drums,
    sections,
    referenceDerived,
    stereo,
    masterSafety
};

struct AblationRequest {
    std::string runId;
    std::vector<std::string> removeActionIds; // top-N / user-selected
    AblationCategory removeCategory {AblationCategory::none};
    bool keepMasterSafety {true};
    int maxVariants {8};
};

struct AblationVariant {
    std::string variantId;
    std::string description;
    std::vector<project::MixPassAction> actions;
};

struct AblationPlan {
    AblationVariant fullAuto;
    std::vector<AblationVariant> withoutAction;
    std::vector<AblationVariant> onlyAction;
    std::vector<AblationVariant> withoutCategory;
};

[[nodiscard]] AblationPlan buildAblationPlan(
    const std::vector<project::MixPassAction>& acceptedActions,
    const AblationRequest& request);

[[nodiscard]] bool actionMatchesCategory(
    const project::MixPassAction& action,
    AblationCategory category) noexcept;

} // namespace mastering::benchmark
