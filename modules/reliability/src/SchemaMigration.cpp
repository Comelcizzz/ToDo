#include "mastering/reliability/SchemaMigration.h"

#include "mastering/reliability/AtomicFile.h"

#include <nlohmann/json.hpp>

namespace mastering::reliability {
namespace {

int readSchemaVersion(const nlohmann::json& j, int fallback)
{
    return j.value("schemaVersion", fallback);
}

} // namespace

MigrationStepResult migrateProjectStep(std::string_view json, int fromVersion)
{
    MigrationStepResult step;
    step.sourceVersion = fromVersion;
    step.targetVersion = fromVersion + 1;
    try {
        auto j = nlohmann::json::parse(json);
        // Deterministic forward fill used across M3A–M4A schemas.
        if (fromVersion < 3) {
            if (!j.contains("mixPassActions"))
                j["mixPassActions"] = nlohmann::json::array();
            step.warnings.push_back("Initialized mixPassActions for pre-M3A schema");
        }
        if (fromVersion < 4) {
            if (!j.contains("sections"))
                j["sections"] = nlohmann::json::array();
            step.warnings.push_back("Initialized sections for pre-M3B schema");
        }
        if (fromVersion < 5) {
            if (!j.contains("actionBudgetJson"))
                j["actionBudgetJson"] = "{}";
            step.warnings.push_back("Initialized actionBudgetJson for pre-M3C schema");
        }
        if (fromVersion < 6) {
            if (!j.contains("metalcoreProfileId"))
                j["metalcoreProfileId"] = "modern-metalcore-balanced";
            if (!j.contains("engineVersion"))
                j["engineVersion"] = "";
            step.warnings.push_back("Initialized MetalcoreProfile fields for pre-M4A schema");
        }
        if (fromVersion < 7) {
            if (!j.contains("reliability"))
                j["reliability"] = {{"autosaveExcludedPreview", true}, {"lockAware", true}};
            step.warnings.push_back("Initialized reliability metadata for M4B schema");
        }
        // Preserve unknown fields (nlohmann keeps them by default when not erased).
        j["schemaVersion"] = step.targetVersion;
        step.outputJson = j.dump(2);
        step.ok = true;
    } catch (const std::exception& ex) {
        step.error = makeError(
            ErrorKind::Migration,
            "MIGRATION_PARSE_FAILED",
            "Project migration failed to parse JSON.",
            ex.what(),
            false,
            "Restore from backup.");
    }
    return step;
}

MigrationStepResult migrateProfileStep(std::string_view json, int fromVersion)
{
    MigrationStepResult step;
    step.sourceVersion = fromVersion;
    step.targetVersion = std::max(1, fromVersion);
    try {
        auto j = nlohmann::json::parse(json);
        if (!j.contains("schemaVersion"))
            j["schemaVersion"] = 1;
        step.outputJson = j.dump(2);
        step.ok = true;
    } catch (const std::exception& ex) {
        step.error = makeError(
            ErrorKind::Profile,
            "PROFILE_MIGRATION_FAILED",
            "Profile migration failed.",
            ex.what(),
            false,
            "Use default balanced profile.");
    }
    return step;
}

MigrationStepResult migrateBenchmarkManifestStep(std::string_view json, int fromVersion)
{
    MigrationStepResult step;
    step.sourceVersion = fromVersion;
    step.targetVersion = std::max(1, fromVersion);
    try {
        auto j = nlohmann::json::parse(json);
        if (!j.contains("schemaVersion"))
            j["schemaVersion"] = 1;
        if (!j.contains("localOnly"))
            j["localOnly"] = true;
        step.outputJson = j.dump(2);
        step.ok = true;
    } catch (const std::exception& ex) {
        step.error = makeError(
            ErrorKind::Migration,
            "MANIFEST_MIGRATION_FAILED",
            "Benchmark manifest migration failed.",
            ex.what(),
            false,
            "Re-import session.");
    }
    return step;
}

MigrationPipelineResult migrateDocument(
    std::string_view documentKind,
    std::string_view json,
    int currentSchema,
    int minSupported,
    int maxSupported,
    std::string_view backupHintPath)
{
    MigrationPipelineResult result;
    result.targetVersion = maxSupported;
    try {
        const auto j = nlohmann::json::parse(json);
        result.sourceVersion = readSchemaVersion(j, currentSchema);
    } catch (const std::exception& ex) {
        result.error = makeError(
            ErrorKind::Migration,
            "MIGRATION_INVALID_JSON",
            "Document is not valid JSON.",
            ex.what(),
            false,
            "Open a backup copy.");
        return result;
    }

    if (result.sourceVersion > maxSupported) {
        result.unsupportedNewer = true;
        result.readOnly = true;
        result.error = makeError(
            ErrorKind::Migration,
            "MIGRATION_UNSUPPORTED_NEWER",
            "This project was saved with a newer schema and cannot be opened as a writable project.",
            "source=" + std::to_string(result.sourceVersion)
                + " max=" + std::to_string(maxSupported),
            false,
            "Upgrade Suite or open a copy read-only.");
        result.outputJson = std::string(json);
        return result;
    }
    if (result.sourceVersion < minSupported) {
        result.error = makeError(
            ErrorKind::Migration,
            "MIGRATION_TOO_OLD",
            "This project schema is older than supported.",
            "source=" + std::to_string(result.sourceVersion),
            false,
            "Restore from a newer backup.");
        return result;
    }

    if (!backupHintPath.empty()) {
        (void) atomicSaveText(
            std::string(backupHintPath) + ".pre-migrate.bak", json, result.sourceVersion, false);
        result.backupPath = std::string(backupHintPath) + ".pre-migrate.bak";
    }

    std::string cursor(json);
    int version = result.sourceVersion;
    while (version < maxSupported) {
        MigrationStepResult step;
        if (documentKind == "project")
            step = migrateProjectStep(cursor, version);
        else if (documentKind == "profile")
            step = migrateProfileStep(cursor, version);
        else
            step = migrateBenchmarkManifestStep(cursor, version);
        if (!step.ok) {
            result.error = step.error;
            return result;
        }
        for (const auto& w : step.warnings)
            result.warnings.push_back(w);
        cursor = step.outputJson;
        version = step.targetVersion;
    }
    result.outputJson = cursor;
    result.ok = true;
    return result;
}

} // namespace mastering::reliability
