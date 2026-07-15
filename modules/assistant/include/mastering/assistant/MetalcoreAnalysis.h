#pragma once

#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/project/ProjectDocument.h"

#include <optional>
#include <string>
#include <vector>

namespace mastering::assistant {

enum class SubOwnership {
    kickOwnsSub,
    bassOwnsSub,
    sharedButSeparated,
    uncertain
};

[[nodiscard]] inline std::string subOwnershipToString(SubOwnership value)
{
    switch (value) {
    case SubOwnership::kickOwnsSub: return "Kick owns sub";
    case SubOwnership::bassOwnsSub: return "Bass owns sub";
    case SubOwnership::sharedButSeparated: return "Shared but separated";
    case SubOwnership::uncertain: return "Uncertain — manual decision advised";
    }
    return "Uncertain — manual decision advised";
}

struct SpectralPeak {
    double frequencyHz {0.0};
    double magnitudeDb {-120.0};
    double prominenceDb {0.0};
    double bandwidthHz {0.0};
};

struct LowFrequencyProfile {
    std::vector<SpectralPeak> fundamentalCandidates;
    double dominantLowHz {0.0};
    double bodyHz {0.0};
    double clickHz {0.0};
    double sustainSeconds {0.0};
    double lowDecaySeconds {0.0};
    double stabilityScore {0.0}; // 0..1 evidence, not calibrated probability
    int eventCount {0};
    std::string evidence;
};

struct GuitarChannelProfile {
    double longTermRmsDb {-120.0};
    double lowMidBuildDb {-120.0};
    double presenceDb {-120.0};
    double harshPeakHz {0.0};
    double harshPeakDb {-120.0};
    double harshQ {2.0};
    double fizzEnergyDb {-120.0};
    double articulationDb {-120.0};
    double spectralTiltDbPerOct {0.0};
    double transientDensityHz {0.0};
    bool mudLikely {false};
    bool harshLikely {false};
    bool fizzLikely {false};
    std::string evidence;
};

struct VocalProfile {
    double rideTargetDb {-18.0};
    double activityRatio {0.0}; // fraction of frames above noise
    double presenceCentroidHz {0.0};
    double sibilanceHz {0.0};
    double sibilanceEnergyDb {-120.0};
    double resonanceHz {0.0};
    double resonanceDb {-120.0};
    double crestDb {0.0};
    bool needsRide {false};
    bool needsDeEss {false};
    bool needsResonance {false};
    bool needsPeakComp {false};
    std::string evidence;
};

struct SnareProfile {
    double crackHz {0.0};
    double bodyHz {0.0};
    double ringHz {0.0};
    double eventRateHz {0.0};
    double crackEnergyDb {-120.0};
    std::string evidence;
};

struct TrackAnalysisExtras {
    std::string trackId;
    project::TrackRole role {project::TrackRole::custom};
    LowFrequencyProfile low;
    GuitarChannelProfile guitar;
    VocalProfile vocal;
    SnareProfile snare;
    analysis::AudioMetrics metrics {};
    // M3C streaming analysis extensions (optional; empty when metrics-only).
    struct ActivityMaskInfo {
        std::string kind;
        double startSeconds {0.0};
        double endSeconds {0.0};
        double confidence {0.0};
    };
    struct SectionSummaryInfo {
        std::string sectionId;
        double medianFundamentalHz {0.0};
        int kickEventCount {0};
        int snareEventCount {0};
        double meanRmsDb {-120.0};
    };
    std::vector<ActivityMaskInfo> activityMasks;
    std::vector<SectionSummaryInfo> sectionSummaries;
    double vocalActivityRatio {0.0}; // from streaming vocal.activityRatio or masks
    bool vocalMaskActive {false};
    std::string analysisCacheKey;
};

struct SubOwnershipDecision {
    SubOwnership ownership {SubOwnership::uncertain};
    double kickDominantHz {0.0};
    double bassDominantHz {0.0};
    double overlapHz {0.0};
    double overlapWidthHz {0.0};
    double evidenceScore {0.0};
    std::string decisionTrace;
};

struct ReferenceProfile {
    std::string role {"overall"}; // overall|low-end|vocal-balance|drum-impact|density|section-contrast
    analysis::AudioMetrics metrics {};
    double subBassRatioDb {0.0};
    double vocalToBedDb {0.0};
    double kickToBassDb {0.0};
    double lowMidDensityDb {0.0};
    double presenceDb {0.0};
    double highEnergyDb {0.0};
    double crestDb {0.0};
    double transientDensityHz {0.0};
    double stereoCorrelation {1.0};
    std::string notes;
};

// Adaptive analysis from mono/stereo sample buffers (not fixed metalcore presets).
class MetalcoreAnalysis {
public:
    [[nodiscard]] static LowFrequencyProfile analyzeKickLow(
        const std::vector<float>& mono,
        double sampleRate);
    [[nodiscard]] static LowFrequencyProfile analyzeBassLow(
        const std::vector<float>& mono,
        double sampleRate);
    [[nodiscard]] static GuitarChannelProfile analyzeGuitar(
        const std::vector<float>& mono,
        double sampleRate,
        const analysis::AudioMetrics& metrics);
    [[nodiscard]] static VocalProfile analyzeVocal(
        const std::vector<float>& mono,
        double sampleRate,
        const analysis::AudioMetrics& metrics,
        project::TrackRole role);
    [[nodiscard]] static SnareProfile analyzeSnare(
        const std::vector<float>& mono,
        double sampleRate,
        const analysis::AudioMetrics& metrics);

    [[nodiscard]] static SubOwnershipDecision decideSubOwnership(
        const LowFrequencyProfile& kick,
        const LowFrequencyProfile& bass,
        const analysis::AudioMetrics& kickMetrics,
        const analysis::AudioMetrics& bassMetrics);

    [[nodiscard]] static ReferenceProfile buildReferenceProfile(
        const analysis::AudioMetrics& metrics,
        const std::string& role,
        double vocalToBedDb = 0.0,
        double kickToBassDb = 0.0);

    // DFT magnitude at a single frequency (Goertzel-style), dBFS-ish relative.
    [[nodiscard]] static double toneMagnitudeDb(
        const std::vector<float>& mono,
        double sampleRate,
        double frequencyHz);

    [[nodiscard]] static std::vector<SpectralPeak> findPeaksInBand(
        const std::vector<float>& mono,
        double sampleRate,
        double lowHz,
        double highHz,
        int candidates = 5);

    [[nodiscard]] static double evidenceScore(
        int eventCount,
        double stability,
        double snrProxyDb,
        double sectionConsistency = 1.0) noexcept;
};

[[nodiscard]] std::vector<float> mixToMono(const std::vector<std::vector<float>>& channels);

} // namespace mastering::assistant
