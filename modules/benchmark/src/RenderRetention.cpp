#include "mastering/benchmark/RenderRetention.h"

#include <nlohmann/json.hpp>

namespace mastering::benchmark {

RenderRetentionPolicy defaultRenderRetentionPolicy() noexcept
{
    return {};
}

std::string serializeRenderRetentionPolicy(const RenderRetentionPolicy& policy)
{
    nlohmann::json j {
        {"maxWavFilesPerSession", policy.maxWavFilesPerSession},
        {"maxAblationVariants", policy.maxAblationVariants},
        {"keepDays", policy.keepDays},
        {"keepAnswerKeys", policy.keepAnswerKeys},
        {"keepReproducibilityManifests", policy.keepReproducibilityManifests}};
    return j.dump(2);
}

} // namespace mastering::benchmark
