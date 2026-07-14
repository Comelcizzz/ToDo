#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mastering::ipc {

inline constexpr int kCurrentSchemaVersion = 1;
inline constexpr int kBridgePort = 58'432;
inline constexpr unsigned int kBridgeMagic = 0x4d415542;

struct ValidationResult {
    bool ok {false};
    std::string error;
};

// Validates analyzer→suite JSON payloads. Rejects missing/newer schema versions
// and malformed required fields without throwing.
[[nodiscard]] ValidationResult validateTrackAnalysisPayload(std::string_view json);

} // namespace mastering::ipc
