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
    bool stereoLink {true};
    double maxGainReductionDb {24.0};
};

class TruePeakLimiter {
public:
    void prepare(double sampleRate, int maxBlockSize = 4096, int channels = 2, int factor = 4) noexcept;
    // Backward-compatible overload used by older call sites.
    void prepare(double sampleRate) noexcept { prepare(sampleRate, 4096, 2, 4); }

    void reset() noexcept;
    void setSettings(const TruePeakLimiterSettings& settings) noexcept;
    [[nodiscard]] const TruePeakLimiterSettings& settings() const noexcept { return settings_; }

    void setCeilingDb(double ceilingDb) noexcept;
    void setReleaseMs(double releaseMs) noexcept;

    void process(float* const* channels, int channelCount, int sampleCount) noexcept;
    // Flush look-ahead + OS tail with zeros (offline).
    void finalize(float* const* channels, int channelCount, int maxSamplesToWrite) noexcept;

    [[nodiscard]] double ceilingDb() const noexcept { return settings_.ceilingDbTp; }
    [[nodiscard]] double lastGainReductionDb() const noexcept { return lastReductionDb_; }
    [[nodiscard]] double maxGainReductionDb() const noexcept { return maxReductionDb_; }
    [[nodiscard]] double averageGainReductionDb() const noexcept
    {
        return grCount_ > 0 ? grSumDb_ / static_cast<double>(grCount_) : 0.0;
    }
    [[nodiscard]] std::uint64_t activeSampleCount() const noexcept { return activeSamples_; }
    [[nodiscard]] int latencySamples() const noexcept { return totalLatency_; }
    void clearMeters() noexcept;

private:
    void rebuildLatency() noexcept;
    [[nodiscard]] double detectOsPeak(const float* const* os, int chCount, int index) const noexcept;
    void processOsGain(float* const* os, int chCount, int osSamples) noexcept;

    TruePeakLimiterSettings settings_;
    Oversampler oversampler_;
    ParameterSmoother ceilingSm_;
    ParameterSmoother inputSm_;
    ParameterSmoother outSm_;
    ParameterSmoother bypassSm_;

    double sampleRate_ {48'000.0};
    int channels_ {2};
    int maxBlock_ {4096};
    int lookAheadSamples_ {0};
    int totalLatency_ {0};

    // Look-ahead delay at OS rate (stereo) + parallel gain delay.
    std::array<std::vector<float>, 2> lookAhead_ {};
    std::array<std::size_t, 2> laWrite_ {};
    std::vector<double> gainDelay_ {};
    std::size_t gainWrite_ {0};

    // Bypass alignment delay (same length as totalLatency_).
    std::array<std::vector<float>, 2> bypassDelay_ {};
    std::array<std::size_t, 2> bypassWrite_ {};

    // Peak envelope at OS rate (linear gain ≤ 1).
    double envelope_ {1.0};
    double releaseCoeff_ {0.999};
    double lastReductionDb_ {0.0};
    double maxReductionDb_ {0.0};
    double grSumDb_ {0.0};
    std::uint64_t grCount_ {0};
    std::uint64_t activeSamples_ {0};

    // Dry copies / OS scratch owned after prepare.
    std::array<std::vector<float>, 2> dryScratch_ {};
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
