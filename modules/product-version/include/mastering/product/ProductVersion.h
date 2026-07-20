#pragma once

#include <string>

namespace mastering::product {

// Unified build identity: semantic + milestone prerelease + short SHA.
// Example: 0.4.0-alpha.m4b+fe82d2b
struct ProductVersion {
    int major {0};
    int minor {4};
    int patch {0};
    std::string prerelease {"alpha.m4b"};
    std::string shortCommit {"unknown"};
    int profileSchemaVersion {1};
    int engineRevision {5}; // M4B reliability / release-candidate family
    int projectSchemaVersion {7};

    [[nodiscard]] std::string semanticCore() const;
    [[nodiscard]] std::string full() const;          // 0.4.0-alpha.m4a+abc1234
    [[nodiscard]] std::string display() const;       // same as full for UI
    [[nodiscard]] std::string toJson() const;
};

[[nodiscard]] ProductVersion currentProductVersion() noexcept;
void setBuildCommitSha(std::string shortSha) noexcept;

} // namespace mastering::product
