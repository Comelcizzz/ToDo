#pragma once

#include "mastering/reliability/TypedError.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

struct AutosaveConfig {
    double intervalSeconds {60.0};
    double debounceSeconds {1.5};
    bool enabled {true};
};

enum class AutosaveStatus {
    idle,
    pending,
    saving,
    saved,
    failed,
    recovered
};

struct AutosaveState {
    AutosaveStatus status {AutosaveStatus::idle};
    std::string lastSavedIso;
    std::string lastAutosavePath;
    std::string lastError;
    bool dirty {false};
};

struct RecoveryCandidate {
    std::string originalPath;
    std::string autosavePath;
    std::string incompletePath;
    std::string originalModifiedIso;
    std::string autosaveModifiedIso;
    std::string projectVersion;
    std::string differencesSummary;
    bool hasNewerAutosave {false};
    bool hasIncompleteSave {false};
    bool hasInterruptedRender {false};
    bool hasIncompleteExperiment {false};
};

[[nodiscard]] std::string autosavePathFor(std::string_view projectPath);
[[nodiscard]] std::string incompletePathFor(std::string_view projectPath);
[[nodiscard]] std::string backupPathFor(std::string_view projectPath);

// Never persist temporary Preview as committed MixPass state.
[[nodiscard]] std::string stripTemporaryPreviewFromProjectJson(std::string_view projectJson);

[[nodiscard]] std::vector<RecoveryCandidate> scanRecoveryCandidates(
    std::string_view projectPath,
    std::string_view recoveryRoot = {});

[[nodiscard]] std::string serializeAutosaveState(const AutosaveState& state);
[[nodiscard]] std::string serializeRecoveryCandidate(const RecoveryCandidate& c);

} // namespace mastering::reliability
