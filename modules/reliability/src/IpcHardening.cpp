#include "mastering/reliability/IpcHardening.h"

#include <chrono>

namespace mastering::reliability {

bool containsPathTraversal(std::string_view text) noexcept
{
    return text.find("..") != std::string_view::npos
        || text.find("../") != std::string_view::npos
        || text.find("..\\") != std::string_view::npos;
}

bool looksLikeCommandExecution(std::string_view text) noexcept
{
    return text.find("\"cmd\"") != std::string_view::npos
        || text.find("shellExecute") != std::string_view::npos
        || text.find("/bin/sh") != std::string_view::npos
        || text.find("powershell") != std::string_view::npos;
}

IpcValidationResult validateIpcPayload(
    std::string_view payload,
    std::string_view expectedSessionId,
    const IpcSecurityPolicy& policy)
{
    IpcValidationResult result;
    if (policy.localOnly == false) {
        result.error = makeError(
            ErrorKind::Protocol,
            "IPC_NON_LOCAL_FORBIDDEN",
            "Non-local IPC binding is not allowed.",
            {},
            false,
            "Use localhost-only Suite bridge.");
        return result;
    }
    if (payload.size() > policy.maxMessageBytes) {
        result.error = makeError(
            ErrorKind::Protocol,
            "IPC_MESSAGE_TOO_LARGE",
            "IPC message exceeded size cap.",
            "size=" + std::to_string(payload.size()),
            true,
            "Reduce payload size.");
        return result;
    }
    if (policy.rejectPathTraversal && containsPathTraversal(payload)) {
        result.error = makeError(
            ErrorKind::Protocol,
            "IPC_PATH_TRAVERSAL",
            "IPC payload contained a path traversal attempt.",
            {},
            false,
            "Reject message.");
        return result;
    }
    if (!policy.allowCommandExecution && looksLikeCommandExecution(payload)) {
        result.error = makeError(
            ErrorKind::Protocol,
            "IPC_COMMAND_EXEC_REJECTED",
            "IPC payload attempted command execution.",
            {},
            false,
            "Reject message.");
        return result;
    }
    if (!policy.allowArbitraryFileAccess
        && (payload.find("\"fileAccess\"") != std::string_view::npos
            || payload.find("\"readFile\"") != std::string_view::npos)) {
        result.error = makeError(
            ErrorKind::Protocol,
            "IPC_FILE_ACCESS_REJECTED",
            "IPC payload requested arbitrary file access.",
            {},
            false,
            "Reject message.");
        return result;
    }
    if (policy.rejectUnknownSession && !expectedSessionId.empty()) {
        if (payload.find(expectedSessionId) == std::string_view::npos
            && payload.find("\"sessionId\"") != std::string_view::npos) {
            result.error = makeError(
                ErrorKind::Protocol,
                "IPC_SESSION_REJECTED",
                "IPC session id did not match.",
                {},
                true,
                "Reconnect plugin to Suite session.");
            return result;
        }
    }
    result.ok = true;
    return result;
}

IpcRateLimiter::IpcRateLimiter(std::uint32_t maxPerSecond)
    : maxPerSecond_(maxPerSecond)
{
    windowStartMs_ = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void IpcRateLimiter::resetWindow(std::uint64_t nowMs)
{
    windowStartMs_ = nowMs;
    count_ = 0;
}

bool IpcRateLimiter::allow()
{
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    if (now - windowStartMs_ >= 1000) {
        resetWindow(now);
    }
    if (count_ >= maxPerSecond_)
        return false;
    ++count_;
    return true;
}

} // namespace mastering::reliability
