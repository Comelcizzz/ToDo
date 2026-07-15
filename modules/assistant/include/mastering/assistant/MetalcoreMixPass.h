#pragma once

#include "mastering/assistant/MetalcoreAnalysis.h"
#include "mastering/assistant/ActionResolver.h"
#include "mastering/assistant/MetalcoreProfile.h"
#include "mastering/assistant/SectionAutomation.h"
#include "mastering/project/ProjectDocument.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mastering::assistant {

// Metalcore Mix Pass V2 (M3B+) — adaptive evidence-based Actions + profile-driven calibration (M4A).
class MetalcoreMixPass {
public:
    struct Options {
        bool enableKickBass {true};
        bool enableGuitarBalance {true};
        bool enableVocalUnmask {true};
        bool enableSnareUnmask {true};
        bool enableDrumBus {true};
        bool enableReference {true};
        double bpm {140.0};
        // Fixed 65/70 Hz allowed only as synthetic-test fallback when analysis empty.
        bool allowSyntheticFrequencyFallback {false};
        // M4A: calibratable profile (defaults to balanced when unset).
        std::optional<MetalcoreProfile> profile;
    };

    using AnalysisMap = std::unordered_map<std::string, TrackAnalysisExtras>;

    [[nodiscard]] std::vector<project::MixPassAction> generateActions(
        const project::ProjectDocument& project,
        const AnalysisMap& analysis,
        const std::vector<ReferenceProfile>& references,
        const Options& options) const;

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

    static bool applyAction(
        project::ProjectDocument& project,
        project::MixPassAction& action);

    static bool rejectAction(project::MixPassAction& action) noexcept;
    static bool previewAction(project::MixPassAction& action) noexcept;
    static bool cancelPreview(project::MixPassAction& action) noexcept;
    static bool undoAction(
        project::ProjectDocument& project,
        project::MixPassAction& action);
    static bool editAction(project::MixPassAction& action, double proposedValue) noexcept;

    static void ensureHierarchy(project::ProjectDocument& project);

    [[nodiscard]] static const project::TrackRecord* findTrack(
        const project::ProjectDocument& project,
        project::TrackRole role);
    [[nodiscard]] static project::TrackRecord* findTrackMutable(
        project::ProjectDocument& project,
        const std::string& id);

    // Last resolver conflicts (filled by generateActions).
    [[nodiscard]] const std::vector<ActionConflict>& lastConflicts() const { return lastConflicts_; }

private:
    mutable std::vector<ActionConflict> lastConflicts_;
};

[[nodiscard]] std::string mixPassActionToJson(const project::MixPassAction& action);
[[nodiscard]] std::string mixPassActionsToJson(const std::vector<project::MixPassAction>& actions);
[[nodiscard]] std::string evidenceLabelFor(double evidenceScore) noexcept;

} // namespace mastering::assistant
