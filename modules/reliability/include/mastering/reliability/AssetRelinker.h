#pragma once

#include "mastering/reliability/TypedError.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

struct AssetIdentity {
    std::string assetId;
    std::string expectedPath;
    std::string contentFingerprint; // SHA-256 preferred
    double durationSeconds {0.0};
    double sampleRate {0.0};
    int channels {0};
    std::int64_t sampleCount {0};
};

enum class RelinkStatus {
    ok,
    missing,
    fingerprintMismatch,
    metadataMismatch,
    nameOnlyMatchRejected,
    unavailable,
    relinked
};

struct RelinkResult {
    RelinkStatus status {RelinkStatus::missing};
    std::string resolvedPath;
    std::string matchedFingerprint;
    bool analysisCacheInvalidated {false};
    std::optional<TypedError> error;
    std::string message;
};

struct RelinkSearchOptions {
    std::string projectFolder;
    std::string userSelectedFolder;
    bool allowNameOnlyMatch {false}; // must stay false by default
};

[[nodiscard]] RelinkResult verifyAsset(const AssetIdentity& asset);
[[nodiscard]] RelinkResult findByFingerprint(
    const AssetIdentity& asset,
    const RelinkSearchOptions& options);
[[nodiscard]] RelinkResult manualRelink(
    const AssetIdentity& asset,
    std::string_view newPath);
[[nodiscard]] std::string serializeRelinkResult(const RelinkResult& result);

} // namespace mastering::reliability
