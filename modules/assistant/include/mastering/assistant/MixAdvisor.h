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

struct Suggestion {
    SuggestionKind kind {SuggestionKind::qualityControl};
    std::string trackId;
    std::string title;
    std::string explanation;
    double confidence {0.0};
};

struct TrackAdjustment {
    std::string trackId;
    double gainDeltaDb {0.0};
    dsp::ProcessorSettings processing;
};

struct MixPlan {
    std::vector<TrackAdjustment> trackAdjustments;
    dsp::ProcessorSettings masterProcessing;
    std::vector<Suggestion> suggestions;
};

class MixAdvisor {
public:
    [[nodiscard]] MixPlan createPlan(
        const project::ProjectDocument& project,
        const std::optional<analysis::AudioMetrics>& reference = std::nullopt) const;

private:
    [[nodiscard]] static double targetRms(project::TrackRole role) noexcept;
    [[nodiscard]] static dsp::ProcessorSettings settingsForRole(
        project::TrackRole role,
        const analysis::AudioMetrics& metrics) noexcept;
};

[[nodiscard]] std::string suggestionKindToString(SuggestionKind kind);
[[nodiscard]] std::string toJson(const MixPlan& plan);

} // namespace mastering::assistant
