#pragma once

#include "mastering/benchmark/BenchmarkSessionManifest.h"
#include "mastering/project/ProjectDocument.h"

#include <string>
#include <vector>

namespace mastering::benchmark {

struct ActionGraphDiffEntry {
    std::string changeType; // added|removed|target|processor|frequency|q|amount|evidence|risk|auto|section
    std::string actionId;
    std::string before;
    std::string after;
};

struct ActionGraphDiffReport {
    std::vector<ActionGraphDiffEntry> entries;
    int added {0};
    int removed {0};
    int changed {0};
};

[[nodiscard]] ActionGraphDiffReport diffActionGraphs(
    const std::vector<project::MixPassAction>& a,
    const std::vector<project::MixPassAction>& b);

[[nodiscard]] std::string serializeActionGraphDiff(const ActionGraphDiffReport& report);

} // namespace mastering::benchmark
