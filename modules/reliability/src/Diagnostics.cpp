#include "mastering/reliability/Diagnostics.h"

#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>

namespace mastering::reliability {

std::string redactPathForDiagnostics(std::string_view path)
{
    if (path.empty())
        return {};
    const auto pos = path.find_last_of("/\\");
    const std::string name = pos == std::string_view::npos
        ? std::string(path)
        : std::string(path.substr(pos + 1));
    return "<redacted>/" + name;
}

bool diagnosticBundleContainsAudioPayload(std::string_view bundleJson) noexcept
{
    // Heuristic: reject raw wav/aiff payloads or large base64 audio blobs.
    if (bundleJson.find("RIFF") != std::string_view::npos)
        return true;
    if (bundleJson.find(".wav\"") != std::string_view::npos
        && bundleJson.find("redacted") == std::string_view::npos)
        return true;
    return false;
}

DiagnosticBundle buildDiagnosticBundle(
    std::string_view appVersion,
    std::string_view commit,
    const std::vector<std::string>& errorCodes,
    std::string_view jobSummariesJson,
    std::string_view cacheStatsJson)
{
    DiagnosticBundle b;
    b.appVersion = std::string(appVersion);
    b.commit = std::string(commit);
#if defined(_WIN32)
    b.os = "Windows";
#else
    b.os = "Linux";
#endif
    b.cpu = "unspecified";
    b.memory = "unspecified";
    b.projectSchema = 7;
    b.profileSchema = 1;
    b.jobSummariesJson = std::string(jobSummariesJson);
    b.cacheStatsJson = std::string(cacheStatsJson);
    b.errorCodes = errorCodes;
    b.sanitizedLogs = "[]";
    return b;
}

std::string serializeDiagnosticBundle(const DiagnosticBundle& bundle)
{
    nlohmann::json j;
    j["appVersion"] = bundle.appVersion;
    j["commit"] = bundle.commit;
    j["os"] = bundle.os;
    j["cpu"] = bundle.cpu;
    j["memory"] = bundle.memory;
    j["projectSchema"] = bundle.projectSchema;
    j["profileSchema"] = bundle.profileSchema;
    j["jobSummaries"] = nlohmann::json::parse(
        bundle.jobSummariesJson.empty() ? "[]" : bundle.jobSummariesJson);
    j["sanitizedLogs"] = nlohmann::json::parse(
        bundle.sanitizedLogs.empty() ? "[]" : bundle.sanitizedLogs);
    j["errorCodes"] = bundle.errorCodes;
    j["pluginVersions"] = nlohmann::json::parse(
        bundle.pluginVersionsJson.empty() ? "{}" : bundle.pluginVersionsJson);
    j["renderManifests"] = nlohmann::json::parse(
        bundle.renderManifestsJson.empty() ? "[]" : bundle.renderManifestsJson);
    j["cacheStats"] = nlohmann::json::parse(
        bundle.cacheStatsJson.empty() ? "{}" : bundle.cacheStatsJson);
    j["policy"] = "No stems, references, rendered audio, or unreacted private filenames";
    return j.dump(2);
}

StructuredLogger::StructuredLogger(std::size_t maxEvents)
    : maxEvents_(std::max<std::size_t>(16, maxEvents))
{
}

void StructuredLogger::log(
    LogLevel level,
    std::string category,
    std::string message,
    std::string jobId,
    std::string projectAlias,
    std::string correlationId)
{
    LogEvent e;
    e.level = level;
    e.category = std::move(category);
    e.message = std::move(message);
    e.timestampIso = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    e.threadOrJobId = std::move(jobId);
    e.projectIdAlias = std::move(projectAlias);
    e.correlationId = std::move(correlationId);
    events_.push_back(std::move(e));
    while (events_.size() > maxEvents_)
        events_.erase(events_.begin());
}

void StructuredLogger::recordRealtimeFault(std::string_view code) noexcept
{
    for (auto& f : realtimeFaults_) {
        if (f.first == code) {
            ++f.second;
            return;
        }
    }
    if (realtimeFaults_.size() < 64)
        realtimeFaults_.emplace_back(std::string(code), 1);
}

std::vector<LogEvent> StructuredLogger::events() const
{
    return events_;
}

std::string StructuredLogger::toJson() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : events_) {
        arr.push_back({
            {"level", static_cast<int>(e.level)},
            {"category", e.category},
            {"message", e.message},
            {"timestampIso", e.timestampIso},
            {"threadOrJobId", e.threadOrJobId},
            {"projectIdAlias", e.projectIdAlias},
            {"correlationId", e.correlationId}});
    }
    return arr.dump(2);
}

std::uint64_t StructuredLogger::realtimeFaultCount(std::string_view code) const noexcept
{
    for (const auto& f : realtimeFaults_) {
        if (f.first == code)
            return f.second;
    }
    return 0;
}

void StructuredLogger::clear()
{
    events_.clear();
    realtimeFaults_.clear();
}

} // namespace mastering::reliability
