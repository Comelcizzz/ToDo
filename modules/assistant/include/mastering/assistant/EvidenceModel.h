#pragma once

#include <string>

namespace mastering::assistant {

struct EvidenceComponents {
    double roleCertainty {0.0};
    double signalDuration {0.0};
    double eventCount {0.0};
    double eventConsistency {0.0};
    double spectralProminence {0.0};
    double temporalOverlap {0.0};
    double sectionConsistency {0.0};
    double noiseFloor {0.0};
    double detectorAgreement {0.0};
    double referenceAgreement {0.0};

    [[nodiscard]] double score() const noexcept;
    [[nodiscard]] std::string breakdown() const;
    [[nodiscard]] std::string label() const; // Low|Medium|High evidence
};

struct SafeRangeDerivation {
    double globalCap {0.0};
    double roleCap {0.0};
    double evidenceCap {0.0};
    double sectionCap {0.0};
    double cumulativeCap {0.0};
    double finalMin {0.0};
    double finalMax {0.0};
    double finalProposal {0.0};
    std::string trace;
};

[[nodiscard]] SafeRangeDerivation deriveSafeRange(
    const std::string& processorId,
    const std::string& role,
    double evidenceScore,
    double cumulativeRemaining,
    double proposed);

} // namespace mastering::assistant
