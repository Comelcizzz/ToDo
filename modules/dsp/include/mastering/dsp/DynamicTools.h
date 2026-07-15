#pragma once

#include "mastering/dsp/Oversampler.h"
#include "mastering/dsp/ParameterSmoother.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mastering::dsp {

struct TruePeakLimiterSettings {
    bool bypass {false};
    double ceilingDbTp {-1.0};
    double lookAheadMs {1.5};
    double releaseMs {80.0};
    double inputGainDb {0.0};
    double outputTrimDb {0.0};
    int oversamplingFactor {4};
    bool stereoLink {true}; // linked is the only mode in M1B
    double maxGainReductionDb {24.0};
};

struct TruePeakLimiterMeters {
    double lastGainReductionDb {0.0};
    double maxGainReductionDb {0.0};
    double averageGainReductionDb {0.0};
    std::uint64_t activeSampleCount {0};
    std::uint64_t safetyClampActivationCount {0};
    double maxPreClampOvershootLinear {0.0}; // max |x|-ceiling before clamp
    double maxPerSampleGainJump {0.0};       // max |Δgain| per OS sample
};

class TruePeakLimiter {
public:
    void prepare(double sampleRate, int maxBlockSize = 4096, int channels = 2, int factor = 4) noexcept;
    void prepare(double sampleRate) noexcept { prepare(sampleRate, 4096, 2, 4); }

    void reset() noexcept;

    // Realtime-safe: updates continuous parameters only. Topology (OS factor, look-ahead
    // length) is deferred until the next prepare() — never allocates here.
    void setSettings(const TruePeakLimiterSettings& settings) noexcept;
    [[nodiscard]] const TruePeakLimiterSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] bool topologyChangePending() const noexcept { return topologyChangePending_; }
    [[nodiscard]] int preparedOversamplingFactor() const noexcept { return preparedFactor_; }

    void setCeilingDb(double ceilingDb) noexcept;
    void setReleaseMs(double releaseMs) noexcept;

    void process(float* const* channels, int channelCount, int sampleCount) noexcept;
    void finalize(float* const* channels, int channelCount, int maxSamplesToWrite) noexcept;

    [[nodiscard]] double ceilingDb() const noexcept { return settings_.ceilingDbTp; }
    [[nodiscard]] double lastGainReductionDb() const noexcept { return meters_.lastGainReductionDb; }
    [[nodiscard]] double maxGainReductionDb() const noexcept { return meters_.maxGainReductionDb; }
    [[nodiscard]] double averageGainReductionDb() const noexcept
    {
        return meters_.averageGainReductionDb;
    }
    [[nodiscard]] std::uint64_t activeSampleCount() const noexcept { return meters_.activeSampleCount; }
    [[nodiscard]] std::uint64_t safetyClampActivationCount() const noexcept
    {
        return meters_.safetyClampActivationCount;
    }
    [[nodiscard]] TruePeakLimiterMeters meters() const noexcept { return meters_; }
    [[nodiscard]] int latencySamples() const noexcept { return totalLatency_; }
    [[nodiscard]] int lookAheadSamplesBaseRate() const noexcept { return lookAheadBase_; }
    void clearMeters() noexcept;

    // Optional GR envelope capture (offline / tests). Disabled by default (no alloc in process
    // beyond preallocated capacity). Call reserveGainEnvelope before processing.
    void reserveGainEnvelope(std::size_t capacity) noexcept;
    [[nodiscard]] const std::vector<float>& gainEnvelopeDb() const noexcept { return grEnvelopeDb_; }

private:
    void rebuildLatency() noexcept;
    void ensureLookAheadCapacity() noexcept;
    void processOsLookAhead(float* const* os, int chCount, int osSamples) noexcept;
    void processChunk(float* const* channels, int channelCount, int sampleCount) noexcept;
    void updateReleaseCoeff() noexcept;

    TruePeakLimiterSettings settings_;
    TruePeakLimiterSettings pendingSettings_ {};
    bool topologyChangePending_ {false};
    int preparedFactor_ {4};

    Oversampler oversampler_;
    ParameterSmoother ceilingSm_;
    ParameterSmoother inputSm_;
    ParameterSmoother outSm_;
    ParameterSmoother bypassSm_;

    double sampleRate_ {48'000.0};
    int channels_ {2};
    int maxBlock_ {4096};
    int lookAheadBase_ {0};
    int lookAheadOs_ {0};
    int totalLatency_ {0};

    // OS-rate look-ahead ring (per channel) — stores upcoming samples for detection + delay.
    std::array<std::vector<float>, 2> lookAheadOsRing_ {};
    std::array<std::size_t, 2> laOsWrite_ {};
    int laOsLength_ {0};

    // Bypass alignment delay (base-rate, length = totalLatency_).
    std::array<std::vector<float>, 2> bypassDelay_ {};
    std::array<std::size_t, 2> bypassWrite_ {};

    double envelope_ {1.0};
    double releaseCoeff_ {0.999};
    double grSumDb_ {0.0};
    std::uint64_t grCount_ {0};
    TruePeakLimiterMeters meters_ {};

    std::array<std::vector<float>, 2> dryScratch_ {};
    std::vector<float> grEnvelopeDb_ {};
    std::size_t grEnvelopeWrite_ {0};
    bool captureEnvelope_ {false};
};

// Sidechain ducking utility (unchanged role for kick→bass).
class DynamicSeparator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setDepthDb(double depthDb) noexcept;
    void setAttackMs(double attackMs) noexcept;
    void setReleaseMs(double releaseMs) noexcept;

    void process(
        const float* const* sidechain,
        float* const* target,
        int channelCount,
        int sampleCount) noexcept;

private:
    double sampleRate_ {48'000.0};
    double depthDb_ {3.0};
    double attackCoefficient_ {0.0};
    double releaseCoefficient_ {0.0};
    double envelope_ {0.0};
};

} // namespace mastering::dsp
