#pragma once

#include "mastering/project/ProjectDocument.h"

#include <string>
#include <vector>

namespace mastering::assistant {

struct ActionConflict {
    std::string actionA;
    std::string actionB;
    std::string conflictType;
    std::string resolution; // merge|reduce|sequence|reject|manual-review
    std::string reason;
};

struct ResolveResult {
    std::vector<project::MixPassAction> actions;
    std::vector<ActionConflict> conflicts;
};

// Deterministic ordering + conflict resolution for Mix Pass Actions.
class ActionResolver {
public:
    // Technical → balance → static EQ → dynamic unmask → compression → sat → clip → stereo → section → master
    static int stageOrder(const project::MixPassAction& action) noexcept;
    static int rolePriority(const project::MixPassAction& action) noexcept;

    [[nodiscard]] static ResolveResult resolve(std::vector<project::MixPassAction> actions);

    // Cumulative DynEQ cut guardrail on the same target track within a frequency neighborhood.
    static constexpr double kMaxCombinedGuitarUnmaskDb = 7.0;
    static constexpr double kFrequencyNeighborhoodHz = 800.0;
};

[[nodiscard]] std::string processingLevelFor(
    const std::string& problemType,
    bool leftOnly,
    bool rightOnly,
    bool bothSimilar,
    bool busLevelCause);

} // namespace mastering::assistant
