#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

enum class ErrorKind {
    Project,
    Asset,
    AudioDecode,
    Analysis,
    Dsp,
    Render,
    Export,
    Migration,
    Profile,
    Cache,
    Protocol,
    Disk,
    Memory,
    Lock,
    Settings
};

struct TypedError {
    ErrorKind kind {ErrorKind::Project};
    std::string code;           // e.g. PROJECT_SAVE_DISK_FULL
    std::string userMessage;
    std::string technicalDetails;
    bool recoverable {true};
    std::string suggestedAction;
    std::string affectedProjectId;
    std::string affectedAssetId;
    std::string affectedJobId;
    std::string logReference;
};

[[nodiscard]] std::string errorKindToString(ErrorKind kind) noexcept;
[[nodiscard]] std::string serializeTypedError(const TypedError& error);
[[nodiscard]] TypedError makeError(
    ErrorKind kind,
    std::string code,
    std::string userMessage,
    std::string technicalDetails = {},
    bool recoverable = true,
    std::string suggestedAction = {});

} // namespace mastering::reliability
