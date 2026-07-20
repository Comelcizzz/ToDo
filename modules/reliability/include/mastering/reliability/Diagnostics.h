#pragma once

#include "mastering/reliability/TypedError.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

struct DiagnosticBundle {
    std::string appVersion;
    std::string commit;
    std::string os;
    std::string cpu;
    std::string memory;
    int projectSchema {0};
    int profileSchema {0};
    std::string jobSummariesJson {"[]"};
    std::string sanitizedLogs;
    std::vector<std::string> errorCodes;
    std::string pluginVersionsJson {"{}"};
    std::string renderManifestsJson {"[]"};
    std::string cacheStatsJson {"{}"};
};

[[nodiscard]] DiagnosticBundle buildDiagnosticBundle(
    std::string_view appVersion,
    std::string_view commit,
    const std::vector<std::string>& errorCodes = {},
    std::string_view jobSummariesJson = "[]",
    std::string_view cacheStatsJson = "{}");

[[nodiscard]] std::string serializeDiagnosticBundle(const DiagnosticBundle& bundle);

// Redact absolute paths / filenames that may identify personal stems.
[[nodiscard]] std::string redactPathForDiagnostics(std::string_view path);
[[nodiscard]] bool diagnosticBundleContainsAudioPayload(std::string_view bundleJson) noexcept;

enum class LogLevel { trace, debug, info, warn, error, fatal };

struct LogEvent {
    LogLevel level {LogLevel::info};
    std::string category;
    std::string message;
    std::string timestampIso;
    std::string threadOrJobId;
    std::string projectIdAlias;
    std::string correlationId;
};

// Non-audio-thread structured log sink with bounded rotation metadata.
class StructuredLogger {
public:
    explicit StructuredLogger(std::size_t maxEvents = 2000);

    void log(LogLevel level, std::string category, std::string message,
        std::string jobId = {}, std::string projectAlias = {}, std::string correlationId = {});
    // Realtime-safe: only increments counters / ring (no file IO).
    void recordRealtimeFault(std::string_view code) noexcept;

    [[nodiscard]] std::vector<LogEvent> events() const;
    [[nodiscard]] std::string toJson() const;
    [[nodiscard]] std::uint64_t realtimeFaultCount(std::string_view code) const noexcept;
    void clear();

private:
    std::size_t maxEvents_;
    std::vector<LogEvent> events_;
    std::vector<std::pair<std::string, std::uint64_t>> realtimeFaults_;
};

} // namespace mastering::reliability
