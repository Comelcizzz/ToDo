#pragma once

#include "mastering/analysis/LoudnessMeter.h"
#include "mastering/dsp/MasterSafetyChain.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mastering::dsp {

enum class QcStatus {
    pass,
    warning,
    fail
};

// Style/profile advisory targets — optional; never affect technical FAIL.
struct StyleQcProfile {
    std::optional<double> minIntegratedLufs;
    std::optional<double> maxIntegratedLufs;
    std::optional<double> maxLimiterGrDb;
    std::string name {"none"};
};

struct ExportQcReport {
    QcStatus technicalStatus {QcStatus::pass};
    QcStatus advisoryStatus {QcStatus::pass};
    QcStatus status {QcStatus::pass}; // worst of technical + advisory
    std::string summary;
    std::string technicalSummary;
    std::string advisorySummary;

    double samplePeakDbfs {-120.0};
    double truePeakDbtp {-120.0};
    double integratedLufs {-120.0};
    double loudnessRangeLu {0.0};
    double dcOffset {0.0};
    std::uint64_t nanCount {0};
    std::uint64_t infCount {0};
    std::uint64_t clippedSampleCount {0};

    double maxLimiterGrDb {0.0};
    double avgLimiterGrDb {0.0};
    double limiterActiveSeconds {0.0};
    std::uint64_t safetyClampActivationCount {0};

    double durationSeconds {0.0};
    double sampleRate {0.0};
    int bitDepth {0};
    int channelCount {0};
    int renderLatencyCompensationSamples {0};

    double configuredCeilingDbtp {-1.0};
    double ceilingToleranceDb {0.10};
};

class ExportQc {
public:
    // Technical QC only (NaN/Inf/corruption/TP ceiling/DC). Style profile is advisory.
    [[nodiscard]] static ExportQcReport analyse(
        const std::vector<std::vector<float>>& channels,
        double sampleRate,
        int bitDepth,
        const MasterSafetyMeters& meters,
        int latencyCompensationSamples,
        double configuredCeilingDbtp,
        double ceilingToleranceDb = 0.10,
        const StyleQcProfile* styleProfile = nullptr);

    [[nodiscard]] static std::string toJson(const ExportQcReport& report);
    [[nodiscard]] static std::string toMarkdown(const ExportQcReport& report);
};

} // namespace mastering::dsp
