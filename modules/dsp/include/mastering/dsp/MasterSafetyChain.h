#pragma once

#include "mastering/dsp/DynamicTools.h"
#include "mastering/dsp/NonlinearProcessors.h"
#include "mastering/dsp/ParameterSmoother.h"

#include <cstdint>

namespace mastering::dsp {

enum class ClipMode {
    none = 0,
    soft,
    hard
};

struct MasterSafetySettings {
    double inputGainDb {0.0};
    double outputGainDb {0.0};

    bool saturationEnabled {false};
    SaturationSettings saturation {};

    ClipMode clipMode {ClipMode::none};
    SoftClipSettings softClip {};
    HardClipSettings hardClip {};

    TruePeakLimiterSettings limiter {};
    bool limiterEnabled {true};
};

struct MasterSafetyMeters {
    double inputPeakLinear {0.0};
    double outputPeakLinear {0.0};
    double limiterGrDb {0.0};
    double limiterMaxGrDb {0.0};
    double limiterAvgGrDb {0.0};
    std::uint64_t limiterActiveSamples {0};
    bool degraded {false};
};

// Input Gain → optional Saturation → optional Soft/Hard Clip → True-Peak Limiter.
class MasterSafetyChain {
public:
    void prepare(double sampleRate, int maxBlockSize, int channels, int defaultOsFactor = 4) noexcept;
    void reset() noexcept;
    void setSettings(const MasterSafetySettings& settings) noexcept;
    [[nodiscard]] const MasterSafetySettings& settings() const noexcept { return settings_; }

    void process(float* const* channels, int channelCount, int sampleCount) noexcept;
    void finalize(float* const* channels, int channelCount, int maxSamples) noexcept;

    [[nodiscard]] int latencySamples() const noexcept;
    [[nodiscard]] MasterSafetyMeters meters() const noexcept { return meters_; }
    void clearMeters() noexcept;

    TruePeakLimiter& limiter() noexcept { return limiter_; }
    const TruePeakLimiter& limiter() const noexcept { return limiter_; }

private:
    MasterSafetySettings settings_;
    SaturationProcessor saturation_;
    SoftClipper softClip_;
    HardClipper hardClip_;
    TruePeakLimiter limiter_;
    ParameterSmoother inputSm_;
    ParameterSmoother outputSm_;
    MasterSafetyMeters meters_ {};
    double sampleRate_ {48'000.0};
    int channels_ {2};
    int maxBlock_ {4096};
    int osFactor_ {4};
};

} // namespace mastering::dsp
