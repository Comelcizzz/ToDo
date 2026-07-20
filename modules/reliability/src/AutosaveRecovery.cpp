#include "mastering/reliability/AutosaveRecovery.h"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace mastering::reliability {

std::string autosavePathFor(std::string_view projectPath)
{
    return std::string(projectPath) + ".autosave.masuite";
}

std::string incompletePathFor(std::string_view projectPath)
{
    return std::string(projectPath) + ".tmp";
}

std::string backupPathFor(std::string_view projectPath)
{
    return std::string(projectPath) + ".bak";
}

std::string stripTemporaryPreviewFromProjectJson(std::string_view projectJson)
{
    try {
        auto j = nlohmann::json::parse(projectJson);
        if (j.contains("mixPassActions") && j["mixPassActions"].is_array()) {
            for (auto& action : j["mixPassActions"]) {
                if (!action.is_object())
                    continue;
                const auto state = action.value("state", "");
                if (state == "preview" || state == "previewing") {
                    // Revert preview to pending — never commit temporary Preview.
                    action["state"] = "pending";
                    action.erase("previewActive");
                }
            }
        }
        j["autosaveExcludedPreview"] = true;
        return j.dump(2);
    } catch (...) {
        return std::string(projectJson);
    }
}

std::vector<RecoveryCandidate> scanRecoveryCandidates(
    std::string_view projectPath,
    std::string_view recoveryRoot)
{
    std::vector<RecoveryCandidate> out;
    RecoveryCandidate c;
    c.originalPath = std::string(projectPath);
    c.autosavePath = autosavePathFor(projectPath);
    c.incompletePath = incompletePathFor(projectPath);
    c.hasNewerAutosave = fs::exists(c.autosavePath);
    c.hasIncompleteSave = fs::exists(c.incompletePath);
    if (!recoveryRoot.empty()) {
        const fs::path root(recoveryRoot);
        c.hasInterruptedRender = fs::exists(root / "renders" / ".interrupted");
        c.hasIncompleteExperiment = fs::exists(root / "experiments" / ".incomplete");
    }
    if (c.hasNewerAutosave || c.hasIncompleteSave || c.hasInterruptedRender
        || c.hasIncompleteExperiment) {
        c.differencesSummary =
            "Newer autosave and/or incomplete save/render/experiment detected. "
            "Original project will not be overwritten automatically.";
        if (fs::exists(c.originalPath)) {
            c.originalModifiedIso = std::to_string(
                static_cast<long long>(fs::last_write_time(c.originalPath).time_since_epoch().count()));
        }
        if (fs::exists(c.autosavePath)) {
            c.autosaveModifiedIso = std::to_string(
                static_cast<long long>(fs::last_write_time(c.autosavePath).time_since_epoch().count()));
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::string serializeAutosaveState(const AutosaveState& state)
{
    nlohmann::json j;
    j["status"] = static_cast<int>(state.status);
    j["lastSavedIso"] = state.lastSavedIso;
    j["lastAutosavePath"] = state.lastAutosavePath;
    j["lastError"] = state.lastError;
    j["dirty"] = state.dirty;
    return j.dump(2);
}

std::string serializeRecoveryCandidate(const RecoveryCandidate& c)
{
    nlohmann::json j;
    j["originalPath"] = c.originalPath;
    j["autosavePath"] = c.autosavePath;
    j["incompletePath"] = c.incompletePath;
    j["originalModifiedIso"] = c.originalModifiedIso;
    j["autosaveModifiedIso"] = c.autosaveModifiedIso;
    j["projectVersion"] = c.projectVersion;
    j["differencesSummary"] = c.differencesSummary;
    j["hasNewerAutosave"] = c.hasNewerAutosave;
    j["hasIncompleteSave"] = c.hasIncompleteSave;
    j["hasInterruptedRender"] = c.hasInterruptedRender;
    j["hasIncompleteExperiment"] = c.hasIncompleteExperiment;
    j["actions"] = nlohmann::json::array(
        {"Recover", "Open original", "Save recovered copy", "Delete recovery data"});
    return j.dump(2);
}

} // namespace mastering::reliability
