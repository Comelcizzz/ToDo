#include "mastering/assistant/SectionAutomation.h"

#include <algorithm>
#include <cmath>

namespace mastering::assistant {
namespace {

const project::SectionMarker* sectionAt(
    const project::ProjectDocument& project,
    double timeSeconds)
{
    const project::SectionMarker* best = nullptr;
    for (const auto& section : project.sections) {
        if (timeSeconds >= section.startSeconds && timeSeconds < section.endSeconds) {
            if (best == nullptr
                || (section.endSeconds - section.startSeconds)
                    < (best->endSeconds - best->startSeconds))
                best = &section;
        }
    }
    return best;
}

double edgeWeight(
    const project::SectionMarker& section,
    double timeSeconds,
    double crossfadeSeconds)
{
    if (crossfadeSeconds <= 0.0)
        return 1.0;
    const auto into = timeSeconds - section.startSeconds;
    const auto outOf = section.endSeconds - timeSeconds;
    double w = 1.0;
    if (into < crossfadeSeconds)
        w = std::min(w, into / crossfadeSeconds);
    if (outOf < crossfadeSeconds)
        w = std::min(w, outOf / crossfadeSeconds);
    return std::clamp(w, 0.0, 1.0);
}

} // namespace

double SectionAutomation::evaluateParameterOffset(
    const project::ProjectDocument& project,
    const SectionAutomationState& state,
    const std::string& trackId,
    const std::string& parameterId,
    double timeSeconds,
    double crossfadeSeconds) noexcept
{
    const auto* section = sectionAt(project, timeSeconds);
    if (section == nullptr)
        return 0.0;

    double offset = 0.0;
    for (const auto& lane : state.offsets) {
        if (lane.targetTrackId != trackId || lane.parameterId != parameterId)
            continue;
        if (lane.sectionId != section->id && lane.sectionId != "full")
            continue;
        const auto w = lane.sectionId == "full" ? 1.0 : edgeWeight(*section, timeSeconds, crossfadeSeconds);
        const auto value = std::clamp(lane.offset, lane.safeMin, lane.safeMax);
        offset += value * w;
    }
    return offset;
}

double SectionAutomation::evaluateGainOffset(
    const project::ProjectDocument& project,
    const SectionAutomationState& state,
    const std::string& trackId,
    double timeSeconds,
    double crossfadeSeconds) noexcept
{
    return evaluateParameterOffset(
        project,
        state,
        trackId,
        "gainDb",
        timeSeconds,
        crossfadeSeconds);
}

SectionAutomationState SectionAutomation::fromActions(
    const std::vector<project::MixPassAction>& actions)
{
    SectionAutomationState state;
    for (const auto& action : actions) {
        if (action.state == "rejected" || action.state == "cancelled")
            continue;
        if (action.sectionScope.empty() || action.sectionScope == "full")
            continue;
        if (action.processorId != "gain" && action.parameterId != "gainDb")
            continue;
        SectionOffset lane;
        lane.sectionId = action.sectionScope;
        lane.targetTrackId = action.targetTrackId;
        lane.parameterId = "gainDb";
        lane.offset = action.proposedValue - action.currentValue;
        lane.safeMin = action.allowedMin;
        lane.safeMax = action.allowedMax;
        state.offsets.push_back(lane);
    }
    return state;
}

} // namespace mastering::assistant
