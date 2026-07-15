#pragma once

#include "mastering/benchmark/BenchmarkSessionManifest.h"

#include <string>
#include <vector>

namespace mastering::benchmark {

enum class ImportIssueSeverity {
    pass,
    warning,
    error,
    userDecisionRequired
};

struct ImportIssue {
    ImportIssueSeverity severity {ImportIssueSeverity::warning};
    std::string code;
    std::string assetId;
    std::string message;
    std::string suggestedAction; // preserve-offset|apply-offset|pad|truncate|use-longest|manual
};

struct ImportValidationReport {
    ImportIssueSeverity overall {ImportIssueSeverity::pass};
    std::vector<ImportIssue> issues;
    double commonDurationSeconds {0.0};
    double longestDurationSeconds {0.0};
    bool alignmentAmbiguous {false};
};

// Validates session folder / manifest. Does NOT auto-align guitar doubles.
[[nodiscard]] ImportValidationReport validateImport(
    const BenchmarkSessionManifest& manifest,
    const std::string& sessionRoot);

[[nodiscard]] std::string severityToString(ImportIssueSeverity s) noexcept;
[[nodiscard]] std::string serializeImportReport(const ImportValidationReport& report);

// Build a non-copyrighted readiness fixture description (synthetic paths under testdata).
[[nodiscard]] BenchmarkSessionManifest makeReadinessFixtureManifest(const std::string& fixtureRoot);

} // namespace mastering::benchmark
