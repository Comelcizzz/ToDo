#pragma once

#include "mastering/project/ProjectDocument.h"

#include <optional>
#include <string>
#include <vector>

namespace mastering::assistant {

enum class SuggestionKind {
    gain,
    tone,
    dynamics,
    stereo,
    masking,
    qualityControl
};

enum class MixVariant {
    balanced,
    punchy,
    vocalForward
};

enum class ActionState {
    pending,
    applied,
    rejected
};

struct Suggestion {
    SuggestionKind kind {SuggestionKind::qualityControl};
    std::string trackId;
    std::string title;
    std::string explanation;
    double confidence {0.0};
};

// Absolute Action targets — Apply assigns exact values (never += / never accumulate).
// Reject semantics (Milestone 0 contract):
// - Reject is valid only while state == pending.
// - Reject after Apply does NOT revert DSP; applied state stays applied.
// - Revert requires a future Undo command that restores captured previous values.
struct TrackAdjustment {
    std::string actionId;
    std::string trackId;
    double targetGainDb {0.0};
    dsp::ProcessorSettings processing;
    ActionState state {ActionState::pending};
    // Captured on successful Apply for future Undo (not used by Reject).
    double previousGainDb {0.0};
    dsp::ProcessorSettings previousProcessing;
    bool hasPrevious {false};
};

struct MixPlan {
    MixVariant variant {MixVariant::balanced};
    std::string variantLabel {"balanced"};
    std::vector<TrackAdjustment> trackAdjustments;
    dsp::ProcessorSettings masterProcessing;
    std::vector<Suggestion> suggestions;
};

class MixAdvisor {
public:
    [[nodiscard]] MixPlan createPlan(
        const project::ProjectDocument& project,
        const std::optional<analysis::AudioMetrics>& reference = std::nullopt,
        MixVariant variant = MixVariant::balanced) const;

    [[nodiscard]] std::vector<MixPlan> createVariants(
        const project::ProjectDocument& project,
        const std::optional<analysis::AudioMetrics>& reference = std::nullopt) const;

    // Sets absolute targets; safe to call repeatedly (idempotent).
    // Unknown track IDs are skipped. Duplicate action IDs apply in order (last wins).
    static void applyPlanToProject(project::ProjectDocument& project, MixPlan& plan);
    // Marks pending actions rejected only; never reverts applied DSP.
    static void rejectPlan(MixPlan& plan) noexcept;

private:
    [[nodiscard]] static double targetRms(
        project::TrackRole role,
        MixVariant variant) noexcept;
    [[nodiscard]] static dsp::ProcessorSettings settingsForRole(
        project::TrackRole role,
        const analysis::AudioMetrics& metrics,
        MixVariant variant) noexcept;
};

[[nodiscard]] std::string suggestionKindToString(SuggestionKind kind);
[[nodiscard]] std::string mixVariantToString(MixVariant variant);
[[nodiscard]] std::optional<MixVariant> mixVariantFromString(std::string_view value);
[[nodiscard]] std::string actionStateToString(ActionState state);
[[nodiscard]] std::optional<ActionState> actionStateFromString(std::string_view value);
[[nodiscard]] std::string toJson(const MixPlan& plan);
[[nodiscard]] std::string toJson(const std::vector<MixPlan>& plans);

} // namespace mastering::assistant
