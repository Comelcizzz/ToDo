#include "mastering/reliability/TypedError.h"

#include <nlohmann/json.hpp>

namespace mastering::reliability {

std::string errorKindToString(ErrorKind kind) noexcept
{
    switch (kind) {
    case ErrorKind::Project: return "ProjectError";
    case ErrorKind::Asset: return "AssetError";
    case ErrorKind::AudioDecode: return "AudioDecodeError";
    case ErrorKind::Analysis: return "AnalysisError";
    case ErrorKind::Dsp: return "DspError";
    case ErrorKind::Render: return "RenderError";
    case ErrorKind::Export: return "ExportError";
    case ErrorKind::Migration: return "MigrationError";
    case ErrorKind::Profile: return "ProfileError";
    case ErrorKind::Cache: return "CacheError";
    case ErrorKind::Protocol: return "ProtocolError";
    case ErrorKind::Disk: return "DiskError";
    case ErrorKind::Memory: return "MemoryError";
    case ErrorKind::Lock: return "LockError";
    case ErrorKind::Settings: return "SettingsError";
    }
    return "ProjectError";
}

TypedError makeError(
    ErrorKind kind,
    std::string code,
    std::string userMessage,
    std::string technicalDetails,
    bool recoverable,
    std::string suggestedAction)
{
    TypedError e;
    e.kind = kind;
    e.code = std::move(code);
    e.userMessage = std::move(userMessage);
    e.technicalDetails = std::move(technicalDetails);
    e.recoverable = recoverable;
    e.suggestedAction = std::move(suggestedAction);
    return e;
}

std::string serializeTypedError(const TypedError& error)
{
    nlohmann::json j;
    j["kind"] = errorKindToString(error.kind);
    j["code"] = error.code;
    j["userMessage"] = error.userMessage;
    j["technicalDetails"] = error.technicalDetails;
    j["recoverable"] = error.recoverable;
    j["suggestedAction"] = error.suggestedAction;
    j["affectedProjectId"] = error.affectedProjectId;
    j["affectedAssetId"] = error.affectedAssetId;
    j["affectedJobId"] = error.affectedJobId;
    j["logReference"] = error.logReference;
    return j.dump(2);
}

} // namespace mastering::reliability
