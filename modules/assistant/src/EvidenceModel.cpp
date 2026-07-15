#include "mastering/assistant/EvidenceModel.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mastering::assistant {
namespace {

double clamp01(double v) noexcept
{
    return std::clamp(v, 0.0, 1.0);
}

double roleWeight(const std::string& role) noexcept
{
    if (role.find("kick") != std::string::npos || role.find("bass") != std::string::npos)
        return 1.0;
    if (role.find("vocal") != std::string::npos)
        return 0.95;
    if (role.find("snare") != std::string::npos)
        return 0.9;
    if (role.find("guitar") != std::string::npos)
        return 0.85;
    if (role.find("drum") != std::string::npos)
        return 0.8;
    return 0.7;
}

double processorGlobalCap(const std::string& processorId) noexcept
{
    if (processorId == "gain" || processorId == "outputGain")
        return 6.0;
    if (processorId == "dynamicEq" || processorId == "fdSidechain")
        return 8.0;
    if (processorId == "staticEq")
        return 6.0;
    if (processorId == "compressor" || processorId == "compressorPeak" || processorId == "parallel")
        return 8.0;
    if (processorId == "saturation" || processorId == "clipper")
        return 6.0;
    if (processorId == "vocalRider")
        return 6.0;
    if (processorId == "stereo" || processorId == "stereoWidth")
        return 6.0;
    return 6.0;
}

} // namespace

double EvidenceComponents::score() const noexcept
{
    // Weighted clamp 0..1 — deterministic evidence, not calibrated probability.
    const double s = 0.14 * clamp01(roleCertainty)
        + 0.10 * clamp01(signalDuration)
        + 0.12 * clamp01(eventCount)
        + 0.12 * clamp01(eventConsistency)
        + 0.12 * clamp01(spectralProminence)
        + 0.08 * clamp01(temporalOverlap)
        + 0.10 * clamp01(sectionConsistency)
        + 0.06 * clamp01(noiseFloor)
        + 0.08 * clamp01(detectorAgreement)
        + 0.08 * clamp01(referenceAgreement);
    return clamp01(s);
}

std::string EvidenceComponents::breakdown() const
{
    std::ostringstream oss;
    oss << "roleCertainty=" << roleCertainty
        << " signalDuration=" << signalDuration
        << " eventCount=" << eventCount
        << " eventConsistency=" << eventConsistency
        << " spectralProminence=" << spectralProminence
        << " temporalOverlap=" << temporalOverlap
        << " sectionConsistency=" << sectionConsistency
        << " noiseFloor=" << noiseFloor
        << " detectorAgreement=" << detectorAgreement
        << " referenceAgreement=" << referenceAgreement
        << " => score=" << score();
    return oss.str();
}

std::string EvidenceComponents::label() const
{
    const double s = score();
    if (s >= 0.65)
        return "High";
    if (s >= 0.35)
        return "Medium";
    return "Low";
}

SafeRangeDerivation deriveSafeRange(
    const std::string& processorId,
    const std::string& role,
    double evidenceScore,
    double cumulativeRemaining,
    double proposed)
{
    SafeRangeDerivation d;
    d.globalCap = processorGlobalCap(processorId);
    d.roleCap = d.globalCap * roleWeight(role);
    d.evidenceCap = d.roleCap * (0.35 + 0.65 * clamp01(evidenceScore));
    d.sectionCap = std::min(3.0, d.evidenceCap);
    d.cumulativeCap = std::max(0.0, cumulativeRemaining);

    const double cap = std::min({d.globalCap, d.roleCap, d.evidenceCap, d.cumulativeCap});
    // Symmetric around current (0 offset) unless proposed sign dictates direction.
    if (proposed >= 0.0) {
        d.finalMin = 0.0;
        d.finalMax = cap;
    } else {
        d.finalMin = -cap;
        d.finalMax = 0.0;
    }
    // Allow small opposite-side room for makeup/threshold style params.
    if (processorId == "compressor" || processorId == "vocalRider" || processorId == "gain") {
        d.finalMin = -cap;
        d.finalMax = cap;
    }

    d.finalProposal = std::clamp(proposed, d.finalMin, d.finalMax);

    std::ostringstream oss;
    oss << "processor=" << processorId
        << " role=" << role
        << " evidence=" << evidenceScore
        << " globalCap=" << d.globalCap
        << " roleCap=" << d.roleCap
        << " evidenceCap=" << d.evidenceCap
        << " sectionCap=" << d.sectionCap
        << " cumulativeRemaining=" << d.cumulativeCap
        << " proposed=" << proposed
        << " => [" << d.finalMin << "," << d.finalMax << "] proposal=" << d.finalProposal;
    d.trace = oss.str();
    return d;
}

} // namespace mastering::assistant
