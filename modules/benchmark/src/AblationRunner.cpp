#include "mastering/benchmark/AblationRunner.h"

#include <algorithm>

namespace mastering::benchmark {

bool actionMatchesCategory(
    const project::MixPassAction& action,
    AblationCategory category) noexcept
{
    const auto& p = action.problemType;
    switch (category) {
    case AblationCategory::kickBass:
        return p.find("kick") != std::string::npos || p.find("Kick") != std::string::npos
            || p.find("bass") != std::string::npos || p.find("Bass") != std::string::npos;
    case AblationCategory::guitars:
        return p.find("guitar") != std::string::npos || p.find("Guitar") != std::string::npos
            || p.find("harsh") != std::string::npos || p.find("fizz") != std::string::npos
            || p.find("Mud") != std::string::npos;
    case AblationCategory::vocals:
        return p.find("vocal") != std::string::npos || p.find("Vocal") != std::string::npos
            || p.find("DeEss") != std::string::npos;
    case AblationCategory::drums:
        return p.find("drum") != std::string::npos || p.find("Drum") != std::string::npos
            || p.find("snare") != std::string::npos || p.find("Parallel") != std::string::npos;
    case AblationCategory::sections:
        return action.sectionScope != "full" && !action.sectionScope.empty();
    case AblationCategory::referenceDerived:
        return p.find("reference") != std::string::npos || p.find("Reference") != std::string::npos
            || action.origin.find("reference") != std::string::npos;
    case AblationCategory::stereo:
        return action.processorId == "stereo" || p.find("Width") != std::string::npos
            || p.find("width") != std::string::npos;
    case AblationCategory::masterSafety:
        return p.find("Master") != std::string::npos || p.find("safety") != std::string::npos
            || action.processorId == "outputGain";
    case AblationCategory::none:
    default:
        return false;
    }
}

AblationPlan buildAblationPlan(
    const std::vector<project::MixPassAction>& acceptedActions,
    const AblationRequest& request)
{
    AblationPlan plan;
    plan.fullAuto.variantId = "full-auto";
    plan.fullAuto.description = "Full AUTO accepted graph";
    for (const auto& a : acceptedActions) {
        if (a.state != "rejected")
            plan.fullAuto.actions.push_back(a);
    }

    std::vector<project::MixPassAction> candidates;
    for (const auto& a : plan.fullAuto.actions) {
        if (a.processorId == "none")
            continue;
        candidates.push_back(a);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& x, const auto& y) {
        return x.evidenceScore > y.evidenceScore;
    });

    int variants = 0;
    auto stillUnderCap = [&] {
        return variants < std::max(1, request.maxVariants);
    };

    for (const auto& id : request.removeActionIds) {
        if (!stillUnderCap())
            break;
        AblationVariant v;
        v.variantId = "without-" + id;
        v.description = "AUTO without action " + id;
        for (const auto& a : plan.fullAuto.actions) {
            if (a.actionId == id)
                continue;
            if (request.keepMasterSafety && actionMatchesCategory(a, AblationCategory::masterSafety)) {
                v.actions.push_back(a);
                continue;
            }
            v.actions.push_back(a);
        }
        // Ensure master safety retained when requested.
        if (request.keepMasterSafety) {
            for (const auto& a : plan.fullAuto.actions) {
                if (!actionMatchesCategory(a, AblationCategory::masterSafety))
                    continue;
                bool present = false;
                for (const auto& x : v.actions) {
                    if (x.actionId == a.actionId)
                        present = true;
                }
                if (!present)
                    v.actions.push_back(a);
            }
        }
        plan.withoutAction.push_back(std::move(v));
        ++variants;

        if (!stillUnderCap())
            break;
        AblationVariant only;
        only.variantId = "only-" + id;
        only.description = "AUTO only action " + id;
        for (const auto& a : plan.fullAuto.actions) {
            if (a.actionId == id
                || (request.keepMasterSafety
                    && actionMatchesCategory(a, AblationCategory::masterSafety)))
                only.actions.push_back(a);
        }
        plan.onlyAction.push_back(std::move(only));
        ++variants;
    }

    // Top-N by evidence if no explicit IDs.
    if (request.removeActionIds.empty()) {
        const int topN = std::min(5, static_cast<int>(candidates.size()));
        for (int i = 0; i < topN && stillUnderCap(); ++i) {
            AblationRequest sub = request;
            sub.removeActionIds = {candidates[static_cast<std::size_t>(i)].actionId};
            sub.maxVariants = request.maxVariants - variants;
            auto partial = buildAblationPlan(acceptedActions, sub);
            for (auto& v : partial.withoutAction) {
                if (!stillUnderCap())
                    break;
                plan.withoutAction.push_back(std::move(v));
                ++variants;
            }
        }
    }

    if (request.removeCategory != AblationCategory::none && stillUnderCap()) {
        AblationVariant v;
        v.variantId = "without-category";
        v.description = "AUTO without category";
        for (const auto& a : plan.fullAuto.actions) {
            if (actionMatchesCategory(a, request.removeCategory)) {
                if (request.keepMasterSafety
                    && actionMatchesCategory(a, AblationCategory::masterSafety)
                    && request.removeCategory != AblationCategory::masterSafety) {
                    v.actions.push_back(a);
                }
                continue;
            }
            v.actions.push_back(a);
        }
        plan.withoutCategory.push_back(std::move(v));
    }

    return plan;
}

} // namespace mastering::benchmark
