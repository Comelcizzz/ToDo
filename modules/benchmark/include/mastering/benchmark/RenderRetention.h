#pragma once

#include <string>

namespace mastering::benchmark {

struct RenderRetentionPolicy {
    int maxWavFilesPerSession {40};
    int maxAblationVariants {8};
    int keepDays {14};
    bool keepAnswerKeys {true};
    bool keepReproducibilityManifests {true};
};

[[nodiscard]] RenderRetentionPolicy defaultRenderRetentionPolicy() noexcept;
[[nodiscard]] std::string serializeRenderRetentionPolicy(const RenderRetentionPolicy& policy);

} // namespace mastering::benchmark
