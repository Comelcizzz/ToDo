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

// Absolute gain target — Apply sets gainDb = targetGainDb (never +=).
struct TrackAdjustment {
    std::string actionId;
    std::string trackId;
    double targetGainDb {0.0};
    dsp::ProcessorSettings processing;
    ActionState state {ActionState::pending};
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

    // Sets absolute targets; safe to call repeatedly.
    static void applyPlanToProject(project::ProjectDocument& project, MixPlan& plan);
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
