#include "mastering/reliability/ProjectLock.h"

#include "mastering/reliability/AtomicFile.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace mastering::reliability {

std::string lockPathFor(std::string_view projectPath)
{
    return std::string(projectPath) + ".lock";
}

bool isLockStale(const ProjectLockInfo& info, std::uint64_t currentPid) noexcept
{
    if (info.pid == 0)
        return true;
    // Same-process re-entry is not stale; different PID without live probe treated
    // as potentially stale only when force break is requested by caller.
    (void) currentPid;
    return info.stale;
}

LockAcquireResult tryAcquireProjectLock(
    std::string_view projectPath,
    std::uint64_t pid,
    std::string_view processName,
    bool forceStaleBreak)
{
    LockAcquireResult result;
    result.info.lockPath = lockPathFor(projectPath);
    const fs::path lock = result.info.lockPath;
    if (fs::exists(lock)) {
        try {
            std::ifstream in(lock);
            nlohmann::json j;
            in >> j;
            result.info.pid = j.value("pid", 0ull);
            result.info.ownerProcess = j.value("process", "");
            result.info.ownerHost = j.value("host", "");
            result.info.acquiredIso = j.value("acquiredIso", "");
            result.info.stale = j.value("stale", false);
        } catch (...) {
            result.info.stale = true;
        }

        if (!forceStaleBreak && result.info.pid != pid) {
            result.mode = LockOpenMode::readOnly;
            result.error = makeError(
                ErrorKind::Lock,
                "PROJECT_LOCKED",
                "This project is open in another Suite instance.",
                "pid=" + std::to_string(result.info.pid) + " process=" + result.info.ownerProcess,
                true,
                "Open read-only, force-open a copy, or close the other instance.");
            return result;
        }
    }

    nlohmann::json j {
        {"pid", pid},
        {"process", processName},
        {"host", "local"},
        {"acquiredIso", "now"},
        {"stale", false}};
    const auto saved = atomicSaveText(lock.string(), j.dump(2), 1, false);
    if (!saved.ok) {
        result.error = saved.error;
        return result;
    }
    result.ok = true;
    result.mode = LockOpenMode::exclusive;
    result.info.pid = pid;
    result.info.ownerProcess = std::string(processName);
    return result;
}

bool releaseProjectLock(std::string_view projectPath)
{
    std::error_code ec;
    return fs::remove(lockPathFor(projectPath), ec) || !fs::exists(lockPathFor(projectPath));
}

} // namespace mastering::reliability
