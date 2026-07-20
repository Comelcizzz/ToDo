#include "mastering/reliability/SettingsStore.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace mastering::reliability {

SettingsLayers defaultSettingsLayers()
{
    SettingsLayers s;
    s.globalJson = R"({"autosaveIntervalSeconds":60,"maxConcurrentJobs":2})";
    s.projectJson = "{}";
    s.profileJson = "{}";
    s.experimentalJson = "{}";
    s.uiStateJson = R"({"recentProjects":[]})";
    return s;
}

SettingsLoadResult loadSettingsAtomic(std::string_view settingsPath)
{
    SettingsLoadResult result;
    const fs::path path(settingsPath);
    if (!fs::exists(path)) {
        result.layers = defaultSettingsLayers();
        result.restoredDefaults = true;
        result.ok = true;
        result.warnings.push_back("Settings missing; defaults restored");
        return result;
    }
    try {
        std::ifstream in(path);
        nlohmann::json j;
        in >> j;
        result.layers.globalJson = j.value("global", nlohmann::json::object()).dump();
        result.layers.projectJson = j.value("project", nlohmann::json::object()).dump();
        result.layers.profileJson = j.value("profile", nlohmann::json::object()).dump();
        result.layers.experimentalJson = j.value("experimental", nlohmann::json::object()).dump();
        result.layers.uiStateJson = j.value("uiState", nlohmann::json::object()).dump();
        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        const auto bak = path.string() + ".corrupt.bak";
        std::error_code ec;
        fs::copy_file(path, bak, fs::copy_options::overwrite_existing, ec);
        result.backupPath = bak;
        result.layers = defaultSettingsLayers();
        result.restoredDefaults = true;
        result.ok = true;
        result.warnings.push_back("Malformed settings backed up; defaults restored");
        result.error = makeError(
            ErrorKind::Settings,
            "SETTINGS_MALFORMED",
            "Settings were malformed and defaults were restored.",
            ex.what(),
            true,
            "Review the .corrupt.bak file if needed.");
        return result;
    }
}

AtomicSaveResult saveSettingsAtomic(std::string_view settingsPath, const SettingsLayers& layers)
{
    nlohmann::json j;
    j["schemaVersion"] = 1;
    j["global"] = nlohmann::json::parse(layers.globalJson.empty() ? "{}" : layers.globalJson);
    j["project"] = nlohmann::json::parse(layers.projectJson.empty() ? "{}" : layers.projectJson);
    j["profile"] = nlohmann::json::parse(layers.profileJson.empty() ? "{}" : layers.profileJson);
    j["experimental"] =
        nlohmann::json::parse(layers.experimentalJson.empty() ? "{}" : layers.experimentalJson);
    j["uiState"] = nlohmann::json::parse(layers.uiStateJson.empty() ? "{}" : layers.uiStateJson);
    return atomicSaveText(settingsPath, j.dump(2), 1, true);
}

} // namespace mastering::reliability
