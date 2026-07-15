#include "mastering/assistant/ActionBudget.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace mastering::assistant {
namespace {

double evidencePriority(const project::MixPassAction& a) noexcept
{
    const double ev = a.evidenceScore > 0.0 ? a.evidenceScore : a.confidence;
    return ev * double(std::max(1, a.priority));
}

bool isDynEq(const project::MixPassAction& a) noexcept
{
    return a.processorId == "dynamicEq" || a.processorId == "fdSidechain"
        || a.hasProposedDynamicEq;
}

bool isEqCut(const project::MixPassAction& a) noexcept
{
    if (a.hasProposedDynamicEq) {
        const int n = std::clamp(a.proposedDynamicEq.bandCount, 0, 4);
        for (int i = 0; i < n; ++i) {
            if (a.proposedDynamicEq.bands[static_cast<std::size_t>(i)].enabled
                && a.proposedDynamicEq.bands[static_cast<std::size_t>(i)].maxCutDb > 0.0)
                return true;
        }
    }
    if (a.processorId == "staticEq" || a.processorId == "dynamicEq") {
        const double delta = a.proposedValue - a.currentValue;
        return delta < 0.0;
    }
    return false;
}

double eqCutAmount(const project::MixPassAction& a) noexcept
{
    if (a.hasProposedDynamicEq) {
        double sum = 0.0;
        const int n = std::clamp(a.proposedDynamicEq.bandCount, 0, 4);
        for (int i = 0; i < n; ++i) {
            const auto& band = a.proposedDynamicEq.bands[static_cast<std::size_t>(i)];
            if (band.enabled)
                sum += std::abs(band.maxCutDb);
        }
        return sum;
    }
    const double delta = a.proposedValue - a.currentValue;
    return delta < 0.0 ? -delta : 0.0;
}

double gainChangeAmount(const project::MixPassAction& a) noexcept
{
    if (a.processorId == "gain" || a.parameterId == "gainDb")
        return std::abs(a.proposedValue - a.currentValue);
    return 0.0;
}

bool isUnmask(const project::MixPassAction& a) noexcept
{
    // Vocal/snare guitar unmask neighborhood only — kickBassMasking uses EQ-cut budget.
    return a.problemType.find("Unmask") != std::string::npos
        || a.problemType.find("unmask") != std::string::npos;
}

std::string trackKey(const project::MixPassAction& a)
{
    if (!a.targetTrackId.empty())
        return a.targetTrackId;
    if (!a.targetBusId.empty())
        return a.targetBusId;
    return a.targetPairId.empty() ? std::string {"_"} : a.targetPairId;
}

} // namespace

std::vector<project::MixPassAction> applyBudget(
    std::vector<project::MixPassAction> actions,
    const ActionBudget& budget,
    std::vector<std::string>& rejectedReasons)
{
    rejectedReasons.clear();
    std::sort(actions.begin(), actions.end(), [](const auto& a, const auto& b) {
        const double sa = evidencePriority(a);
        const double sb = evidencePriority(b);
        if (std::abs(sa - sb) > 1.0e-12)
            return sa > sb;
        return a.actionId < b.actionId;
    });

    std::vector<project::MixPassAction> kept;
    std::map<std::string, int> actionsPerTrack;
    std::map<std::string, int> dynEqPerTrack;
    std::map<std::string, double> eqCutPerTrack;
    std::map<std::string, double> gainPerTrack;
    std::map<std::string, double> sectionPerTrack;
    double totalUnmask = 0.0;

    for (auto& action : actions) {
        const auto key = trackKey(action);
        const double ev = action.evidenceScore > 0.0 ? action.evidenceScore : action.confidence;

        if (ev < budget.minEvidence) {
            std::ostringstream oss;
            oss << action.actionId << ": evidence " << ev << " < minEvidence "
                << budget.minEvidence;
            rejectedReasons.push_back(oss.str());
            action.state = "rejected";
            continue;
        }

        if (actionsPerTrack[key] >= budget.maxActionsPerTrack) {
            rejectedReasons.push_back(
                action.actionId + ": maxActionsPerTrack (" + std::to_string(budget.maxActionsPerTrack)
                + ") exceeded for " + key);
            action.state = "rejected";
            continue;
        }

        if (isDynEq(action) && dynEqPerTrack[key] >= budget.maxDynEqBandsPerTrack) {
            rejectedReasons.push_back(
                action.actionId + ": maxDynEqBandsPerTrack exceeded for " + key);
            action.state = "rejected";
            continue;
        }

        if (isEqCut(action)) {
            const double cut = eqCutAmount(action);
            if (eqCutPerTrack[key] + cut > budget.maxCumulativeEqCutDb) {
                rejectedReasons.push_back(
                    action.actionId + ": maxCumulativeEqCutDb exceeded for " + key);
                action.state = "rejected";
                continue;
            }
        }

        const double gainDelta = gainChangeAmount(action);
        if (gainDelta > 0.0 && gainPerTrack[key] + gainDelta > budget.maxGainChangeDb) {
            rejectedReasons.push_back(
                action.actionId + ": maxGainChangeDb exceeded for " + key);
            action.state = "rejected";
            continue;
        }

        if (action.sectionScope != "full" && !action.sectionScope.empty()) {
            const double offset = std::abs(action.proposedValue - action.currentValue);
            if (sectionPerTrack[key] + offset > budget.maxSectionOffsetDb) {
                rejectedReasons.push_back(
                    action.actionId + ": maxSectionOffsetDb exceeded for " + key);
                action.state = "rejected";
                continue;
            }
        }

        if (isUnmask(action)) {
            const double cut = eqCutAmount(action);
            if (totalUnmask + cut > budget.maxTotalUnmaskDb) {
                rejectedReasons.push_back(
                    action.actionId + ": maxTotalUnmaskDb exceeded");
                action.state = "rejected";
                continue;
            }
            totalUnmask += cut;
        }

        // Accept.
        ++actionsPerTrack[key];
        if (isDynEq(action))
            ++dynEqPerTrack[key];
        if (isEqCut(action))
            eqCutPerTrack[key] += eqCutAmount(action);
        if (gainDelta > 0.0)
            gainPerTrack[key] += gainDelta;
        if (action.sectionScope != "full" && !action.sectionScope.empty())
            sectionPerTrack[key] += std::abs(action.proposedValue - action.currentValue);

        kept.push_back(std::move(action));
    }

    // Preserve rejected actions (with state=rejected) for UI / decision traces.
    for (auto& action : actions) {
        if (action.state == "rejected")
            kept.push_back(std::move(action));
    }

    return kept;
}

} // namespace mastering::assistant
