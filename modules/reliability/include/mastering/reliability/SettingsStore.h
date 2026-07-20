#pragma once

#include "mastering/reliability/AtomicFile.h"
#include "mastering/reliability/TypedError.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

struct SettingsLayers {
    std::string globalJson {"{}"};
    std::string projectJson {"{}"};
    std::string profileJson {"{}"};
    std::string experimentalJson {"{}"};
    std::string uiStateJson {"{}"};
};

struct SettingsLoadResult {
    bool ok {false};
    bool restoredDefaults {false};
    SettingsLayers layers;
    std::string backupPath;
    std::vector<std::string> warnings;
    std::optional<TypedError> error;
};

[[nodiscard]] SettingsLayers defaultSettingsLayers();
[[nodiscard]] SettingsLoadResult loadSettingsAtomic(std::string_view settingsPath);
[[nodiscard]] AtomicSaveResult saveSettingsAtomic(
    std::string_view settingsPath,
    const SettingsLayers& layers);

} // namespace mastering::reliability
