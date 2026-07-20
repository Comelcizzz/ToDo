#include "mastering/assistant/ActionResolver.h"

#include <algorithm>
#include <cmath>

namespace mastering::assistant {

int ActionResolver::stageOrder(const project::MixPassAction& action) noexcept
{
    const auto& p = action.problemType;
    if (p.find("technical") != std::string::npos || p.find("polarity") != std::string::npos)
        return 1;
    if (action.processorId == "gain" || p.find("Balance") != std::string::npos
        || p.find("balance") != std::string::npos || p.find("Riding") != std::string::npos
        || p.find("riding") != std::string::npos)
        return 2;
    if (action.processorId == "staticEq" || p.find("Complementary") != std::string::npos
        || p.find("Mud") != std::string::npos || p.find("Harsh") != std::string::npos
        || p.find("Fizz") != std::string::npos)
        return 3;
    if (action.processorId == "dynamicEq" || p.find("Unmask") != std::string::npos
        || p.find("Masking") != std::string::npos || p.find("DeEss") != std::string::npos
        || p.find("Resonance") != std::string::npos)
        return 4;
    if (action.processorId == "compressor" || action.processorId == "compressorPeak"
        || p.find("Glue") != std::string::npos || p.find("Parallel") != std::string::npos)
        return 5;
    if (action.processorId == "saturation")
        return 6;
    if (action.processorId == "clipper")
        return 7;
    if (action.processorId == "stereo")
        return 8;
    if (action.sectionScope != "full" && action.sectionScope != "")
        return 9;
    if (action.targetBusId.find("master") != std::string::npos || p.find("Master") != std::string::npos)
        return 10;
    return 5;
}

int ActionResolver::rolePriority(const project::MixPassAction& action) noexcept
{
    if (action.problemType.find("kickBass") != std::string::npos)
        return 100;
    if (action.problemType.find("vocal") != std::string::npos)
        return 90;
    if (action.problemType.find("snare") != std::string::npos)
        return 80;
    if (action.problemType.find("guitar") != std::string::npos)
        return 70;
    if (action.problemType.find("drum") != std::string::npos)
        return 60;
    return 50;
}

std::string processingLevelFor(
    const std::string& problemType,
    bool leftOnly,
    bool rightOnly,
    bool bothSimilar,
    bool busLevelCause)
{
    (void) problemType;
    if (busLevelCause)
        return "bus";
    if (leftOnly)
        return "track-left";
    if (rightOnly)
        return "track-right";
    if (bothSimilar)
        return "pair";
    return "track";
}

ResolveResult ActionResolver::resolve(std::vector<project::MixPassAction> actions)
{
    ResolveResult result;
    // Sort deterministically: stage → priority → actionId
    std::sort(actions.begin(), actions.end(), [](const auto& a, const auto& b) {
        const auto sa = stageOrder(a);
        const auto sb = stageOrder(b);
        if (sa != sb)
            return sa < sb;
        const auto pa = rolePriority(a);
        const auto pb = rolePriority(b);
        if (pa != pb)
            return pa > pb;
        return a.actionId < b.actionId;
    });

    // Assign order indices + conflict groups.
    for (std::size_t i = 0; i < actions.size(); ++i) {
        actions[i].priority = rolePriority(actions[i]);
        actions[i].orderIndex = static_cast<int>(i);
        if (actions[i].conflictGroup.empty())
            actions[i].conflictGroup = actions[i].targetTrackId + ":" + actions[i].processorId;
    }

    // Cumulative guitar unmask DynEQ cuts in neighborhood.
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (!actions[i].hasProposedDynamicEq)
            continue;
        if (actions[i].problemType.find("Unmask") == std::string::npos
            && actions[i].problemType.find("unmask") == std::string::npos)
            continue;
        double cumulative = actions[i].proposedDynamicEq.bands[0].maxCutDb;
        for (std::size_t j = 0; j < i; ++j) {
            if (actions[j].state == "rejected" || !actions[j].hasProposedDynamicEq)
                continue;
            if (actions[j].targetTrackId != actions[i].targetTrackId
                && actions[j].targetPairId != actions[i].targetPairId)
                continue;
            if (actions[j].problemType.find("Unmask") == std::string::npos
                && actions[j].problemType.find("unmask") == std::string::npos)
                continue;
            const auto fa = actions[j].proposedDynamicEq.bands[0].frequencyHz;
            const auto fb = actions[i].proposedDynamicEq.bands[0].frequencyHz;
            if (std::abs(fa - fb) > kFrequencyNeighborhoodHz)
                continue;
            cumulative += actions[j].proposedDynamicEq.bands[0].maxCutDb;
            if (cumulative > kMaxCombinedGuitarUnmaskDb) {
                const auto allowed = std::max(
                    0.0,
                    kMaxCombinedGuitarUnmaskDb
                        - (cumulative - actions[i].proposedDynamicEq.bands[0].maxCutDb));
                ActionConflict c;
                c.actionA = actions[j].actionId;
                c.actionB = actions[i].actionId;
                c.conflictType = "cumulative-unmask-cut";
                c.resolution = "reduce";
                c.reason = "Combined vocal/snare guitar unmask exceeds "
                    + std::to_string(kMaxCombinedGuitarUnmaskDb) + " dB guardrail";
                result.conflicts.push_back(c);
                actions[i].proposedValue = allowed;
                actions[i].proposedDynamicEq.bands[0].maxCutDb = allowed;
                actions[i].explanation += " [reduced by ActionResolver cumulative guardrail]";
                cumulative = kMaxCombinedGuitarUnmaskDb;
            }
        }
    }

    // Same parameter collisions: keep higher priority, reject lower.
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (actions[i].state == "rejected")
            continue;
        for (std::size_t j = i + 1; j < actions.size(); ++j) {
            if (actions[j].state == "rejected")
                continue;
            if (actions[i].targetTrackId != actions[j].targetTrackId)
                continue;
            if (actions[i].processorId != actions[j].processorId)
                continue;
            if (actions[i].parameterId != actions[j].parameterId)
                continue;
            // Prefer earlier stage / higher priority (already sorted).
            ActionConflict c;
            c.actionA = actions[i].actionId;
            c.actionB = actions[j].actionId;
            c.conflictType = "same-parameter";
            c.resolution = "reject";
            c.reason = "Duplicate parameter target; keeping earlier deterministic action";
            result.conflicts.push_back(c);
            actions[j].state = "rejected";
            actions[j].explanation += " [rejected by ActionResolver: duplicate parameter]";
            actions[i].supersedes = actions[j].actionId;
        }
    }

    // Static EQ + DynEQ same region: sequence (static first already), reduce static if DynEQ present.
    for (auto& action : actions) {
        if (action.processorId != "staticEq" || action.state == "rejected")
            continue;
        for (const auto& other : actions) {
            if (!other.hasProposedDynamicEq || other.state == "rejected")
                continue;
            if (other.targetTrackId != action.targetTrackId)
                continue;
            ActionConflict c;
            c.actionA = action.actionId;
            c.actionB = other.actionId;
            c.conflictType = "static-vs-dynamic-eq";
            c.resolution = "sequence";
            c.reason = "Static EQ before Dynamic EQ (ordering); prefer cut over boost";
            result.conflicts.push_back(c);
            if (action.hasProposedProcessing
                && action.proposedProcessing.equalizer.presenceGainDb > 0.0) {
                action.proposedProcessing.equalizer.presenceGainDb = 0.0;
                action.proposedValue = 0.0;
                action.explanation += " [boost suppressed; prefer dynamic/cut]";
            }
        }
    }

    result.actions = std::move(actions);
    return result;
}

} // namespace mastering::assistant
