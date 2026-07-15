#pragma once

#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/analysis/SpectralAnalysis.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mastering::analysis {

// Maximum supported programme duration for streaming analysis (30 minutes).
inline constexpr double kMaxAnalysisSeconds = 1800.0;

/*
 * Window configurations (documented @ 48 kHz reference; scaled with sample rate):
 *
 *   lowFreq   — 4096 frames, hop 1024  (sub/bass fundamentals, occupancy)
 *   transient —  512 frames, hop  128  (kick/snare onsets)
 *   presence  — 2048 frames, hop  512  (vocal/guitar presence)
 *   fizz      — 1024 frames, hop  256  (harsh/fizz energy)
 */

struct AnalysisWindowConfig {
    int sizeFrames {2048};
    int hopFrames {512};
};

struct AnalysisWindowPresets {
    AnalysisWindowConfig lowFreq {4096, 1024};
    AnalysisWindowConfig transient {512, 128};
    AnalysisWindowConfig presence {2048, 512};
    AnalysisWindowConfig fizz {1024, 256};

    // Scale preset sizes from 48 kHz reference to the active sample rate.
    [[nodiscard]] static AnalysisWindowPresets forSampleRate(double sampleRate) noexcept;
};

struct KickEvent {
    double timeSeconds {0.0};
    double confidence {0.0};
    double fundamentalHz {0.0};
    double bodyHz {0.0};
    double clickHz {0.0};
    double truePeakDb {-120.0};
    double crestDb {0.0};
    std::string sectionId;
};

struct ActivityMask {
    std::string kind; // vocal|guitar|kick|snare|bass|presence|fizz|silence
    double startSeconds {0.0};
    double endSeconds {0.0};
    double confidence {0.0};
};

struct BassOccupancy {
    std::vector<double> binHz;     // 30–180 Hz bin centres
    std::vector<double> energyDb;  // per-bin energy
    double stableFundamentalHz {0.0};
    double subOccupancyDb {-120.0};
    std::string evidence;
};

// Lightweight mirrors of assistant profile types (audio-analysis must not depend on assistant).
struct LowFrequencyProfile {
    std::vector<SpectralPeak> fundamentalCandidates;
    double dominantLowHz {0.0};
    double bodyHz {0.0};
    double clickHz {0.0};
    double sustainSeconds {0.0};
    double lowDecaySeconds {0.0};
    double stabilityScore {0.0};
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
    double activityRatio {0.0};
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

struct SectionSummary {
    std::string sectionId;
    double medianFundamentalHz {0.0};
    int kickEventCount {0};
    int snareEventCount {0};
    double meanRmsDb {-120.0};
};

struct StreamingAnalysisResult {
    std::vector<KickEvent> kickEvents;
    std::vector<KickEvent> snareEvents;
    BassOccupancy bass;
    std::vector<ActivityMask> masks;
    LowFrequencyProfile kickLow;
    LowFrequencyProfile bassLow;
    GuitarChannelProfile guitar;
    VocalProfile vocal;
    SnareProfile snare;
    AudioMetrics metrics {};
    double progress {0.0};
    std::string cacheKey;
    std::vector<SectionSummary> sectionSummaries;
    std::vector<SpectralPeak> spectralPeakCandidates;
    bool cancelled {false};
    bool truncated {false}; // hit kMaxAnalysisSeconds
};

struct AnalysisSectionMarker {
    std::string id;
    double startSeconds {0.0};
    double endSeconds {0.0};
};

// Chunked full-track analysis (up to kMaxAnalysisSeconds).
// Reads via callback `bool readChunk(float* mono, int maxFrames, int& framesRead)`
// OR processes supplied mono chunks / buffers.
class StreamingAnalyzer {
public:
    using ReadChunkFn = std::function<bool(float* mono, int maxFrames, int& framesRead)>;

    void reset() noexcept;
    void setCancelFlag(std::atomic<bool>* flag) noexcept { cancelFlag_ = flag; }
    void setTrackIdentity(
        std::string trackId,
        std::uint64_t fileSize,
        std::uint64_t mtimeHash);
    void setSectionMarkers(std::vector<AnalysisSectionMarker> sections);

    [[nodiscard]] double progress() const noexcept { return progress_; }
    [[nodiscard]] bool isCancelled() const noexcept;
    [[nodiscard]] std::string cacheKey() const;

    // Full pass via pull callback. Callback returns false on EOF / error.
    // framesRead must be set to samples written (0..maxFrames).
    [[nodiscard]] StreamingAnalysisResult analyze(ReadChunkFn reader, double sampleRate);

    // Process a complete mono buffer (still chunked internally).
    [[nodiscard]] StreamingAnalysisResult analyzeBuffer(
        const std::vector<float>& mono,
        double sampleRate);

    // Incremental API: feed chunks then finalize.
    void begin(double sampleRate);
    void processChunk(const float* mono, int frames);
    [[nodiscard]] StreamingAnalysisResult finalize();

private:
    void accumulateChunk(const float* mono, int frames, double timeOffsetSeconds);
    void rebuildBassOccupancy();
    void finalizeProfiles(StreamingAnalysisResult& out);
    void buildActivityMasks(StreamingAnalysisResult& out);
    void buildSectionSummaries(StreamingAnalysisResult& out);
    [[nodiscard]] std::string sectionIdAt(double timeSeconds) const;
    void updateProgress(double secondsProcessed, double estimatedTotal);

    std::atomic<bool>* cancelFlag_ {nullptr};
    std::string trackId_;
    std::uint64_t fileSize_ {0};
    std::uint64_t mtimeHash_ {0};
    std::vector<AnalysisSectionMarker> sections_;

    double sampleRate_ {48'000.0};
    double progress_ {0.0};
    std::uint64_t framesProcessed_ {0};
    AnalysisWindowPresets windows_ {};

    // Accumulators
    std::vector<KickEvent> kickEvents_;
    std::vector<KickEvent> snareEvents_;
    std::vector<SpectralPeak> peakCandidates_;
    std::vector<double> bassBinHz_;
    std::vector<double> bassEnergySum_;
    std::vector<int> bassEnergyCount_;
    std::vector<double> fundamentalSamples_;
    std::vector<float> overlapBuffer_; // for windowed analysis across chunks

    double sumSquares_ {0.0};
    double peakAbs_ {0.0};
    double lastKickTime_ {-1.0e9};
    double lastSnareTime_ {-1.0e9};
    double presenceEnergySum_ {0.0};
    double fizzEnergySum_ {0.0};
    double lowMidEnergySum_ {0.0};
    int activeFrames_ {0};
    int totalAnalysisFrames_ {0};

    // Activity tracking (frame-level flags collapsed at finalize)
    std::vector<std::uint8_t> activityKick_;
    std::vector<std::uint8_t> activitySnare_;
    std::vector<std::uint8_t> activityPresence_;
    std::vector<std::uint8_t> activityFizz_;
};

} // namespace mastering::analysis
