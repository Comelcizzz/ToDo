#pragma once

#include "mastering/reliability/TypedError.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

struct MigrationStepResult {
    int sourceVersion {0};
    int targetVersion {0};
    bool ok {false};
    std::vector<std::string> warnings;
    std::string outputJson;
    std::optional<TypedError> error;
};

struct MigrationPipelineResult {
    int sourceVersion {0};
    int targetVersion {0};
    bool ok {false};
    bool unsupportedNewer {false};
    bool readOnly {false};
    std::vector<std::string> warnings;
    std::string outputJson;
    std::string backupPath;
    std::optional<TypedError> error;
};

using MigrationFn = std::function<MigrationStepResult(std::string_view json, int fromVersion)>;

// Central migration for project / MixPass / processors / profiles / manifests.
[[nodiscard]] MigrationPipelineResult migrateDocument(
    std::string_view documentKind,
    std::string_view json,
    int currentSchema,
    int minSupported,
    int maxSupported,
    std::string_view backupHintPath = {});

[[nodiscard]] MigrationStepResult migrateProjectStep(std::string_view json, int fromVersion);
[[nodiscard]] MigrationStepResult migrateProfileStep(std::string_view json, int fromVersion);
[[nodiscard]] MigrationStepResult migrateBenchmarkManifestStep(std::string_view json, int fromVersion);

} // namespace mastering::reliability
