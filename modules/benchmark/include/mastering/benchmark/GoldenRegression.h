#pragma once

#include "mastering/project/ProjectDocument.h"

#include <string>
#include <vector>

namespace mastering::benchmark {

struct GoldenExpectation {
    std::string sessionId;
    std::vector<std::string> expectedProblemCategories;
    std::vector<std::string> forbiddenActionSubstrings; // e.g. hardcoded fallback markers
    int maxActionCount {40};
    bool forbidSnareAsDrumBus {true};
    bool forbidRawAutoIdentityClaim {true};
    bool forbidLowEvidenceMusicalAuto {true};
    bool forbidGlobalSectionMisapply {true};
    double minKickHz {40.0};
    double maxKickHz {90.0};
    double minBassHz {45.0};
    double maxBassHz {100.0};
};

struct GoldenViolation {
    std::string code;
    std::string message;
};

struct GoldenCheckResult {
    bool passed {true};
    std::vector<GoldenViolation> violations;
};

[[nodiscard]] GoldenExpectation defaultSyntheticGolden();
[[nodiscard]] GoldenCheckResult checkGoldenExpectations(
    const GoldenExpectation& expect,
    const std::vector<project::MixPassAction>& actions,
    bool rawEqualsAuto);

} // namespace mastering::benchmark
