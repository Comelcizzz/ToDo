#include "mastering/reliability/AtomicFile.h"

#include "mastering/analysis/Sha256.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace mastering::reliability {
namespace {

std::string nowStamp()
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    return std::to_string(ms);
}

bool flushFile(std::ofstream& out)
{
    out.flush();
    return static_cast<bool>(out);
}

} // namespace

bool writeIncompleteTempForTest(std::string_view destinationPath, std::string_view contents)
{
    (void) nowStamp();
    const fs::path dest(destinationPath);
    const fs::path tmp = dest.string() + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return flushFile(out);
}

bool looksLikeIncompleteSave(std::string_view destinationPath)
{
    const fs::path tmp = fs::path(std::string(destinationPath) + ".tmp");
    return fs::exists(tmp);
}

AtomicSaveResult atomicSaveBytes(
    std::string_view destinationPath,
    const void* data,
    std::size_t size,
    int schemaVersion,
    bool keepBackup)
{
    AtomicSaveResult result;
    result.schemaVersion = schemaVersion;
    result.finalPath = std::string(destinationPath);
    try {
        const fs::path dest(destinationPath);
        if (dest.has_parent_path())
            fs::create_directories(dest.parent_path());
        const fs::path tmp = dest.string() + ".tmp";
        const fs::path bak = dest.string() + ".bak";

        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) {
                result.error = makeError(
                    ErrorKind::Disk,
                    "PROJECT_SAVE_OPEN_FAILED",
                    "Could not write temporary project file.",
                    tmp.string(),
                    true,
                    "Check disk space and folder permissions.");
                return result;
            }
            out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (!flushFile(out)) {
                result.error = makeError(
                    ErrorKind::Disk,
                    "PROJECT_SAVE_FLUSH_FAILED",
                    "Could not flush temporary project file.",
                    tmp.string(),
                    true,
                    "Free disk space and retry save.");
                return result;
            }
        }

        // Validate temp is readable and non-empty.
        {
            std::ifstream in(tmp, std::ios::binary);
            if (!in) {
                result.error = makeError(
                    ErrorKind::Project,
                    "PROJECT_SAVE_VALIDATE_FAILED",
                    "Temporary save could not be validated.",
                    tmp.string(),
                    true,
                    "Retry save. Previous project file was not overwritten.");
                return result;
            }
            std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (bytes.size() != size) {
                result.error = makeError(
                    ErrorKind::Project,
                    "PROJECT_SAVE_SIZE_MISMATCH",
                    "Temporary save size mismatch.",
                    "expected=" + std::to_string(size) + " got=" + std::to_string(bytes.size()),
                    true,
                    "Retry save.");
                return result;
            }
            result.checksumSha256 = analysis::sha256Hex(bytes);
        }

        if (keepBackup && fs::exists(dest)) {
            std::error_code ec;
            fs::copy_file(dest, bak, fs::copy_options::overwrite_existing, ec);
            if (!ec)
                result.backupPath = bak.string();
        }

        std::error_code ec;
        fs::rename(tmp, dest, ec);
        if (ec) {
            // Fallback: copy + remove tmp (Windows replace semantics).
            fs::copy_file(tmp, dest, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                result.error = makeError(
                    ErrorKind::Disk,
                    "PROJECT_SAVE_REPLACE_FAILED",
                    "Could not replace project file atomically.",
                    ec.message(),
                    true,
                    "Close other programs using the file and retry.");
                return result;
            }
            fs::remove(tmp, ec);
        }
        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = makeError(
            ErrorKind::Project,
            "PROJECT_SAVE_EXCEPTION",
            "Project save failed.",
            ex.what(),
            true,
            "Retry save. Check disk space.");
        return result;
    }
}

AtomicSaveResult atomicSaveText(
    std::string_view destinationPath,
    std::string_view contents,
    int schemaVersion,
    bool keepBackup)
{
    // Embed schema marker for consumers.
    std::string payload(contents);
    if (payload.find("\"schemaVersion\"") == std::string::npos
        && !payload.empty()
        && payload.front() == '{') {
        // Leave content as-is; schema is caller-owned for typed JSON.
        (void) schemaVersion;
    }
    return atomicSaveBytes(
        destinationPath, payload.data(), payload.size(), schemaVersion, keepBackup);
}

} // namespace mastering::reliability
