#pragma once

#include "mastering/project/ProjectDocument.h"

#include <optional>
#include <string>
#include <vector>

namespace mastering::assistant {

// Metalcore Mix Pass V1 — generates typed MixPassActions with absolute DSP targets.
class MetalcoreMixPass {
public:
    struct Options {
        bool enableKickBass {true};
        bool enableGuitarBalance {true};
        bool enableVocalUnmask {true};
        bool enableSnareUnmask {true};
        bool enableDrumBus {true};
        double bpm {140.0};
    };

    [[nodiscard]] std::vector<project::MixPassAction> generateActions(
        const project::ProjectDocument& project,
        const std::optional<analysis::AudioMetrics>& reference,
        const Options& options) const;

    [[nodiscard]] std::vector<project::MixPassAction> generateActions(
        const project::ProjectDocument& project,
        const std::optional<analysis::AudioMetrics>& reference = std::nullopt) const
    {
        return generateActions(project, reference, Options {});
    }

    // Apply one action absolutely (idempotent for same actionId+version+proposedValue).
    static bool applyAction(
        project::ProjectDocument& project,
        project::MixPassAction& action);

    static bool rejectAction(project::MixPassAction& action) noexcept;
    static bool previewAction(project::MixPassAction& action) noexcept;
    static bool cancelPreview(project::MixPassAction& action) noexcept;

    // Static undo restores previous absolute snapshot captured on Apply.
    static bool undoAction(
        project::ProjectDocument& project,
        project::MixPassAction& action);

    // Edit proposed value within allowed safe range (marks state edited).
    static bool editAction(project::MixPassAction& action, double proposedValue) noexcept;

    // Ensure default buses/pairs for metalcore roles when missing.
    static void ensureHierarchy(project::ProjectDocument& project);

    [[nodiscard]] static const project::TrackRecord* findTrack(
        const project::ProjectDocument& project,
        project::TrackRole role);
    [[nodiscard]] static project::TrackRecord* findTrackMutable(
        project::ProjectDocument& project,
        const std::string& id);
};

[[nodiscard]] std::string mixPassActionToJson(const project::MixPassAction& action);
[[nodiscard]] std::string mixPassActionsToJson(const std::vector<project::MixPassAction>& actions);

} // namespace mastering::assistant
