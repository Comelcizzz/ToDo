#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::reliability {

enum class ExportBitDepth {
    pcm24,
    float32
};

enum class DitherPolicy {
    none,
    tpdf
};

struct ExportWorkflowState {
    std::string sourceMode {"current"}; // raw|auto|current
    double sampleRate {48000.0};
    ExportBitDepth bitDepth {ExportBitDepth::pcm24};
    bool ditherEnabled {false};
    DitherPolicy dither {DitherPolicy::none};
    std::uint64_t ditherSeed {0};
    double ceilingDbTp {-1.0};
    std::string loudnessStatus;
    std::string outputPath;
    bool overwriteAllowed {false};
    std::uint64_t estimatedSizeBytes {0};
    std::string qcStatus {"pending"};
    bool creativeLimiterAlreadyApplied {false};
};

// Dither only for integer bit-depth reduction; never for 32-bit float.
[[nodiscard]] bool shouldApplyDither(ExportBitDepth depth) noexcept;
[[nodiscard]] DitherPolicy resolveDitherPolicy(ExportBitDepth depth, bool userRequested) noexcept;
[[nodiscard]] std::string ditherPolicyDocumented() noexcept;
[[nodiscard]] std::string serializeExportWorkflow(const ExportWorkflowState& state);

struct BatchExportRoute {
    std::string name; // master|instrumental|vocals|drums|guitars|bass|excerpt|stem
    std::vector<std::string> includeRoles;
    std::vector<std::string> includeBusIds;
    bool requireValidRouting {true};
};

[[nodiscard]] bool validateBatchExportRouting(
    const BatchExportRoute& route,
    const std::vector<std::string>& availableRoles,
    const std::vector<std::string>& availableBusIds,
    std::string* error = nullptr);

} // namespace mastering::reliability
