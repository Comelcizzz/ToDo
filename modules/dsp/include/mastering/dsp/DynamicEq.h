#pragma once

#include "mastering/dsp/EnvelopeDetector.h"
#include "mastering/dsp/ParameterSmoother.h"
#include "mastering/dsp/SvfFilter.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mastering::dsp {

inline constexpr int kMaxDynamicEqBands = 4;
inline constexpr int kDynamicEqSchemaVersion = 1;

enum class DynamicEqFilterType {
    bell = 0,
    lowShelf,
    highShelf
};

enum class DynamicEqStereoMode {
    linked = 0,
    independent
};

// Absolute, typed band state — Action-Graph ready (idempotent restore).
struct DynamicEqBandState {
    std::uint32_t bandId {0};
    bool enabled {true};
    DynamicEqFilterType filterType {DynamicEqFilterType::bell};

    double frequencyHz {1'000.0};
    double q {1.0};
    double staticGainDb {0.0};

    double thresholdDb {-24.0}; // detector level (dBFS)
    double ratio {2.0};        // >= 1; GR = overshoot * (1 - 1/ratio)
    double maxCutDb {12.0};    // maximum downward dynamic cut

    double attackMs {10.0};
    double releaseMs {100.0};

    // Detector (may differ from processing band).
    bool detectorBandPass {true};
    double detectorFrequencyHz {1'000.0};
    double detectorQ {1.0};
    DetectorSource detectorSource {DetectorSource::internal};
    DetectorMode detectorMode {DetectorMode::rms};

    DynamicEqStereoMode stereoMode {DynamicEqStereoMode::linked};

    // Placeholders for future MixAdvisor / Action Graph (not used by DSP).
    std::string targetTrackId {};
    std::string sidechainSourceId {};
};

struct DynamicEqState {
    int schemaVersion {kDynamicEqSchemaVersion};
    std::uint32_t processorRevision {1};
    bool bypass {false};
    double wetDry {1.0}; // 0..1
    double outputGainDb {0.0};
    int bandCount {1}; // 1..4; structural — applied in prepare/setState
    std::array<DynamicEqBandState, kMaxDynamicEqBands> bands {};
};

struct DynamicEqBandMeters {
    double currentGrDb {0.0};
    double maxGrDb {0.0};
    double avgGrDb {0.0};
    double detectorLevelDb {-120.0};
    double thresholdDb {-24.0};
    std::uint64_t activeSamples {0};
    // Bounded GR history for UI (ring).
    static constexpr int kHistory = 64;
    std::array<float, kHistory> grHistoryDb {};
    int grHistoryWrite {0};
    bool stale {false};
};

struct DynamicEqMeters {
    std::array<DynamicEqBandMeters, kMaxDynamicEqBands> bands {};
    bool degraded {false};
};

// Dynamic EQ: static filter gain + downward dynamic cut from detector.
// Latency: 0 samples (no look-ahead).
//
// Gain computer (per band, linked or per-channel):
//   levelDb = 20*log10(max(envelope, floor))
//   overshoot = max(0, levelDb - thresholdDb)
//   grDb = min(maxCutDb, overshoot * (1 - 1/ratio))
//   filterGainDb = staticGainDb - grDb
// Static and dynamic contributions are applied as a single filter gain (no double-count).
class DynamicEqProcessor {
public:
    void prepare(double sampleRate, int maxBlockSize, int channels) noexcept;
    void reset() noexcept;

    // Structural: bandCount / stereo topology applied here (may allocate scratch).
    void setState(const DynamicEqState& state) noexcept;
    [[nodiscard]] const DynamicEqState& state() const noexcept { return state_; }

    // Continuous realtime-safe updates (no alloc). Structural fields ignored until setState/prepare.
    void setContinuousParameters(const DynamicEqState& state) noexcept;

    void process(
        float* const* main,
        const float* const* sidechain, // optional; nullptr = silence for external bands
        int channelCount,
        int sampleCount) noexcept;

    [[nodiscard]] int latencySamples() const noexcept { return 0; }
    [[nodiscard]] DynamicEqMeters meters() const noexcept { return meters_; }
    void clearMeters() noexcept;

    // Serialization helpers (JSON via nlohmann in .cpp).
    [[nodiscard]] std::string toJson() const;
    void fromJson(const std::string& json);

private:
    struct BandRuntime {
        DynamicEqBandState settings {};
        EnvelopeDetector detector {};
        EnvelopeDetector detectorR {}; // independent R
        std::array<SvfFilter, 2> filter {};
        ParameterSmoother freqSm_ {};
        ParameterSmoother qSm_ {};
        ParameterSmoother staticGainSm_ {};
        ParameterSmoother thresholdSm_ {};
        ParameterSmoother grSmL_ {};
        ParameterSmoother grSmR_ {};
        double grSum {0.0};
        std::uint64_t grCount {0};
    };

    void processChunk(float* const* main, const float* const* sidechain, int ch, int n) noexcept;
    void refreshBandSmoothingTargets(int bandIndex) noexcept;
    [[nodiscard]] static double computeGrDb(double levelDb, double thresholdDb, double ratio, double maxCut)
        noexcept;

    DynamicEqState state_ {};
    std::array<BandRuntime, kMaxDynamicEqBands> bands_ {};
    ParameterSmoother wetSm_ {};
    ParameterSmoother outSm_ {};
    ParameterSmoother bypassSm_ {};
    DynamicEqMeters meters_ {};

    double sampleRate_ {48'000.0};
    int maxBlock_ {4096};
    int channels_ {2};

    std::array<std::vector<float>, 2> dryScratch_ {};
};

// Frequency-dependent sidechain ducking convenience wrapper around one Dynamic EQ band.
struct FrequencyDependentSidechainSettings {
    bool bypass {false};
    double detectorFrequencyHz {70.0};
    double detectorQ {2.0};
    double targetFrequencyHz {70.0};
    double targetQ {2.0};
    DynamicEqFilterType targetType {DynamicEqFilterType::bell};
    double thresholdDb {-18.0};
    double ratio {4.0};
    double maxCutDb {8.0};
    double attackMs {5.0};
    double releaseMs {120.0};
    DynamicEqStereoMode stereoMode {DynamicEqStereoMode::linked};
    DetectorMode detectorMode {DetectorMode::rms};
};

class FrequencyDependentSidechain {
public:
    void prepare(double sampleRate, int maxBlock, int channels) noexcept;
    void reset() noexcept;
    void setSettings(const FrequencyDependentSidechainSettings& settings) noexcept;
    [[nodiscard]] const FrequencyDependentSidechainSettings& settings() const noexcept
    {
        return settings_;
    }

    void process(
        float* const* target,
        const float* const* sidechain,
        int channelCount,
        int sampleCount) noexcept;

    [[nodiscard]] DynamicEqMeters meters() const noexcept { return eq_.meters(); }
    [[nodiscard]] int latencySamples() const noexcept { return 0; }

private:
    void syncState() noexcept;

    FrequencyDependentSidechainSettings settings_ {};
    DynamicEqProcessor eq_ {};
};

} // namespace mastering::dsp
