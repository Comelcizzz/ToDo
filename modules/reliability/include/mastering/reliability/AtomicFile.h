#pragma once

#include "mastering/reliability/TypedError.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mastering::reliability {

struct AtomicSaveResult {
    bool ok {false};
    std::string finalPath;
    std::string backupPath;
    std::string checksumSha256;
    int schemaVersion {0};
    std::optional<TypedError> error;
};

// Serialize → temp → validate → flush → atomic replace → keep previous backup.
// Works for .masuite, manifests, profiles, experiments, evaluations, edit logs.
[[nodiscard]] AtomicSaveResult atomicSaveText(
    std::string_view destinationPath,
    std::string_view contents,
    int schemaVersion,
    bool keepBackup = true);

[[nodiscard]] AtomicSaveResult atomicSaveBytes(
    std::string_view destinationPath,
    const void* data,
    std::size_t size,
    int schemaVersion,
    bool keepBackup = true);

// Simulate interrupted write for tests: leave .tmp without replacing destination.
[[nodiscard]] bool writeIncompleteTempForTest(
    std::string_view destinationPath,
    std::string_view contents);

[[nodiscard]] bool looksLikeIncompleteSave(std::string_view destinationPath);

} // namespace mastering::reliability
