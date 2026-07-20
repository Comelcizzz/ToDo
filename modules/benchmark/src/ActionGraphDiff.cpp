#include "mastering/benchmark/ActionGraphDiff.h"

#include <map>
#include <sstream>

#include <nlohmann/json.hpp>

namespace mastering::benchmark {
namespace {

std::string actionKey(const project::MixPassAction& a)
{
    return a.actionId.empty() ? (a.problemType + ":" + a.targetTrackId + ":" + a.processorId)
                              : a.actionId;
}

} // namespace

ActionGraphDiffReport diffActionGraphs(
    const std::vector<project::MixPassAction>& a,
    const std::vector<project::MixPassAction>& b)
{
    ActionGraphDiffReport report;
    std::map<std::string, const project::MixPassAction*> left;
    std::map<std::string, const project::MixPassAction*> right;
    for (const auto& x : a)
        left[actionKey(x)] = &x;
    for (const auto& x : b)
        right[actionKey(x)] = &x;

    for (const auto& [id, pa] : left) {
        const auto it = right.find(id);
        if (it == right.end()) {
            report.entries.push_back({"removed", id, pa->processorId, ""});
            ++report.removed;
            continue;
        }
        const auto* pb = it->second;
        auto add = [&](const char* type, const std::string& before, const std::string& after) {
            if (before == after)
                return;
            report.entries.push_back({type, id, before, after});
            ++report.changed;
        };
        add("target", pa->targetTrackId, pb->targetTrackId);
        add("processor", pa->processorId, pb->processorId);
        add("amount", std::to_string(pa->proposedValue), std::to_string(pb->proposedValue));
        add("evidence", std::to_string(pa->evidenceScore), std::to_string(pb->evidenceScore));
        add("risk", pa->riskLevel, pb->riskLevel);
        add("auto", pa->autoApplyEligibility ? "1" : "0", pb->autoApplyEligibility ? "1" : "0");
        add("section", pa->sectionScope, pb->sectionScope);
        if (pa->hasProposedDynamicEq && pb->hasProposedDynamicEq) {
            add(
                "frequency",
                std::to_string(pa->proposedDynamicEq.bands[0].frequencyHz),
                std::to_string(pb->proposedDynamicEq.bands[0].frequencyHz));
            add(
                "q",
                std::to_string(pa->proposedDynamicEq.bands[0].q),
                std::to_string(pb->proposedDynamicEq.bands[0].q));
        }
    }
    for (const auto& [id, pb] : right) {
        if (left.find(id) == left.end()) {
            report.entries.push_back({"added", id, "", pb->processorId});
            ++report.added;
        }
    }
    return report;
}

std::string serializeActionGraphDiff(const ActionGraphDiffReport& report)
{
    nlohmann::json j;
    j["added"] = report.added;
    j["removed"] = report.removed;
    j["changed"] = report.changed;
    j["entries"] = nlohmann::json::array();
    for (const auto& e : report.entries) {
        j["entries"].push_back({
            {"changeType", e.changeType},
            {"actionId", e.actionId},
            {"before", e.before},
            {"after", e.after}});
    }
    return j.dump(2);
}

} // namespace mastering::benchmark
