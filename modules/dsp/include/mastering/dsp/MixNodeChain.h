#pragma once

#include "mastering/dsp/DynamicEq.h"
#include "mastering/dsp/NonlinearProcessors.h"
#include "mastering/dsp/ParameterSmoother.h"
#include "mastering/dsp/SvfFilter.h"

#include <array>
#include <cstdint>
#include <string>

namespace mastering::dsp {

inline constexpr int kMixNodeChainSchemaVersion = 1;

// Vertical-slice Mix Node chain (shared DSP):
//   Input Gain → Static EQ (1 bell) → Dynamic EQ → optional Saturation → Output Gain
// No per-node true-peak limiter in M2A.
struct MixNodeStaticEqState {
    bool enabled {true};
    bool bypass {false};
    double frequencyHz {1'000.0};
    double gainDb {0.0};
    double q {1.0};
};

struct MixNodeChainState {
    int schemaVersion {kMixNodeChainSchemaVersion};
    std::uint32_t revision {1};
    bool bypass {false};

    double inputGainDb {0.0};
    MixNodeStaticEqState staticEq {};
    DynamicEqState dynamicEq {}; // typically bandCount=1 for vertical slice
    bool saturationEnabled {false};
    SaturationSettings saturation {};
    double outputGainDb {0.0};
};

struct MixNodeChainMeters {
    float inputPeakL {0.0f};
    float inputPeakR {0.0f};
    float outputPeakL {0.0f};
    float outputPeakR {0.0f};
    float sidechainPeak {0.0f};
    DynamicEqMeters dynamicEq {};
    bool degraded {false};
};

class MixNodeChain {
public:
    void prepare(double sampleRate, int maxBlockSize, int channels) noexcept;
    void reset() noexcept;

    // Structural / absolute state (may touch OS prepare for saturation factor).
    void setState(const MixNodeChainState& state) noexcept;
    [[nodiscard]] const MixNodeChainState& state() const noexcept { return state_; }

    // Continuous realtime-safe updates (no alloc). Saturation OS factor deferred.
    void setContinuousParameters(const MixNodeChainState& state) noexcept;

    void process(
        float* const* main,
        const float* const* sidechain,
        int channelCount,
        int sampleCount) noexcept;

    [[nodiscard]] int latencySamples() const noexcept;
    [[nodiscard]] MixNodeChainMeters meters() const noexcept { return meters_; }
    void clearMeters() noexcept;

    [[nodiscard]] std::string toJson() const;
    [[nodiscard]] bool fromJson(std::string_view json) noexcept;

private:
    void applySaturationPrepareIfNeeded() noexcept;
    void processStaticEq(float* const* channels, int channelCount, int sampleCount) noexcept;
    void processGain(
        float* const* channels,
        int channelCount,
        int sampleCount,
        ParameterSmoother& smoother) noexcept;

    MixNodeChainState state_ {};
    double sampleRate_ {48'000.0};
    int maxBlock_ {512};
    int channels_ {2};
    int pendingOsFactor_ {1};

    ParameterSmoother inputGainSm_;
    ParameterSmoother outputGainSm_;
    ParameterSmoother bypassSm_;
    ParameterSmoother staticFreqSm_;
    ParameterSmoother staticGainSm_;
    ParameterSmoother staticQSm_;

    std::array<SvfFilter, 2> staticEqFilters_ {};
    DynamicEqProcessor dynamicEq_;
    SaturationProcessor saturation_;

    MixNodeChainMeters meters_ {};
    std::array<std::vector<float>, 2> dryScratch_ {};
};

} // namespace mastering::dsp
