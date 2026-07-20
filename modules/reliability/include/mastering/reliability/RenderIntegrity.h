#pragma once

#include "mastering/reliability/JobSystem.h"
#include "mastering/reliability/TypedError.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

struct RenderIntegrityReport {
    bool ok {false};
    bool readable {false};
    double durationSeconds {0.0};
    double sampleRate {0.0};
    int channels {0};
    bool finiteSamples {true};
    bool hasNanInf {false};
    bool unexpectedSilence {false};
    double truePeakDbtp {-120.0};
    bool clipping {false};
    bool tailTruncation {false};
    bool latencyCompensated {true};
    std::string sha256;
    std::string renderGraphId;
    std::vector<std::string> failures;
};

struct RenderJobSpec {
    std::string jobId;
    std::string projectId;
    std::string snapshotJson;
    std::string renderGraphHash;
    std::string outputPath;
    std::string tempPath;
    JobType type {JobType::exportMaster};
};

// Validates render output before promoting temp → final.
[[nodiscard]] RenderIntegrityReport validateRenderFile(
    std::string_view path,
    std::string_view expectedRenderGraphId = {},
    double expectedSampleRate = 0.0,
    int expectedChannels = 0);

// On cancel or failure: do not leave temp as a completed artifact.
[[nodiscard]] bool discardIncompleteRender(std::string_view tempPath, std::string_view finalPath);
[[nodiscard]] bool promoteValidatedRender(
    std::string_view tempPath,
    std::string_view finalPath,
    const RenderIntegrityReport& report);

[[nodiscard]] std::string serializeIntegrityReport(const RenderIntegrityReport& report);

} // namespace mastering::reliability
