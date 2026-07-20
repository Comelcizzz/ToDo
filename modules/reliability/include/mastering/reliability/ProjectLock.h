#pragma once

#include "mastering/reliability/TypedError.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mastering::reliability {

struct ProjectLockInfo {
    std::string lockPath;
    std::string ownerProcess;
    std::string ownerHost;
    std::string acquiredIso;
    std::uint64_t pid {0};
    bool stale {false};
};

enum class LockOpenMode {
    exclusive,
    readOnly,
    forceCopy
};

struct LockAcquireResult {
    bool ok {false};
    LockOpenMode mode {LockOpenMode::exclusive};
    ProjectLockInfo info;
    std::optional<TypedError> error;
};

[[nodiscard]] std::string lockPathFor(std::string_view projectPath);
[[nodiscard]] LockAcquireResult tryAcquireProjectLock(
    std::string_view projectPath,
    std::uint64_t pid,
    std::string_view processName,
    bool forceStaleBreak = false);
[[nodiscard]] bool releaseProjectLock(std::string_view projectPath);
[[nodiscard]] bool isLockStale(const ProjectLockInfo& info, std::uint64_t currentPid) noexcept;

} // namespace mastering::reliability
