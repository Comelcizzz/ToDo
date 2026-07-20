#pragma once

#include "mastering/reliability/TypedError.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mastering::reliability {

struct IpcSecurityPolicy {
    bool localOnly {true};
    std::size_t maxMessageBytes {1ull * 1024 * 1024};
    std::uint32_t maxMessagesPerSecond {200};
    bool rejectUnknownSession {true};
    bool rejectPathTraversal {true};
    bool allowArbitraryFileAccess {false};
    bool allowCommandExecution {false};
};

struct IpcValidationResult {
    bool ok {false};
    std::optional<TypedError> error;
};

[[nodiscard]] IpcValidationResult validateIpcPayload(
    std::string_view payload,
    std::string_view expectedSessionId,
    const IpcSecurityPolicy& policy);

[[nodiscard]] bool containsPathTraversal(std::string_view text) noexcept;
[[nodiscard]] bool looksLikeCommandExecution(std::string_view text) noexcept;

class IpcRateLimiter {
public:
    explicit IpcRateLimiter(std::uint32_t maxPerSecond = 200);
    bool allow();
    void resetWindow(std::uint64_t nowMs);

private:
    std::uint32_t maxPerSecond_;
    std::uint32_t count_ {0};
    std::uint64_t windowStartMs_ {0};
};

} // namespace mastering::reliability
