#pragma once

#include "mastering/project/ProjectDocument.h"

#include <string>
#include <vector>

namespace mastering::assistant {

struct SectionOffset {
    std::string sectionId;
    std::string targetTrackId;
    std::string parameterId; // gainDb|dynMaxCutDb|compAmount|satMix
    double offset {0.0};
    double safeMin {-6.0};
    double safeMax {6.0};
};

struct SectionAutomationState {
    std::vector<SectionOffset> offsets;
};

// Evaluates base + relative section offsets with linear crossfade at boundaries.
class SectionAutomation {
public:
    [[nodiscard]] static double evaluateGainOffset(
        const project::ProjectDocument& project,
        const SectionAutomationState& state,
        const std::string& trackId,
        double timeSeconds,
        double crossfadeSeconds = 0.05) noexcept;

    [[nodiscard]] static double evaluateParameterOffset(
        const project::ProjectDocument& project,
        const SectionAutomationState& state,
        const std::string& trackId,
        const std::string& parameterId,
        double timeSeconds,
        double crossfadeSeconds = 0.05) noexcept;

    [[nodiscard]] static SectionAutomationState fromActions(
        const std::vector<project::MixPassAction>& actions);
};

} // namespace mastering::assistant
