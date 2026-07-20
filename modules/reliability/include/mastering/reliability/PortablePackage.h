#pragma once

#include "mastering/reliability/TypedError.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

enum class PackageMode {
    metadataOnly,
    fullLocal
};

struct PortablePackageRequest {
    std::string projectPath;
    std::string destinationDirectory;
    PackageMode mode {PackageMode::metadataOnly};
    bool includeStems {false};
    bool includeReferences {false};
    bool explicitAudioConsent {false};
    std::string projectJson;
    std::vector<std::string> stemPaths;
    std::vector<std::string> referencePaths;
};

struct PortablePackageResult {
    bool ok {false};
    std::string packageRoot;
    std::string manifestPath;
    std::vector<std::string> copiedFiles;
    std::string checksumSha256;
    std::optional<TypedError> error;
};

// Blocks path traversal: copied paths must stay under destinationDirectory.
[[nodiscard]] bool isSafePackageRelativePath(std::string_view relativePath) noexcept;
[[nodiscard]] PortablePackageResult exportPortablePackage(const PortablePackageRequest& request);
[[nodiscard]] PortablePackageResult validatePortablePackage(std::string_view packageRoot);
[[nodiscard]] PortablePackageResult importPortablePackage(
    std::string_view packageRoot,
    std::string_view restoreDirectory);

} // namespace mastering::reliability
