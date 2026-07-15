#pragma once

#include "mastering/dsp/ParameterSmoother.h"

#include <array>
#include <cstdint>

namespace mastering::dsp {

struct StereoWidthState {
    double midGainDb {0.0};
    double sideGainDb {0.0};
    double lowBandMonoHz {120.0};   // below this, force/attenuate side → 0
    double minCorrelation {-0.2};   // correlation guard floor
    double outputTrimDb {0.0};
    bool bypass {false};
};

struct StereoWidthMeters {
    double correlation {1.0};
    double sideGainDb {0.0};
    double midGainDb {0.0};
};

// M/S encode/decode stereo width with low-band mono and correlation guard.
// Mono input is pass-through. Latency: 0 samples.
class StereoWidth {
public:
    void prepare(double sampleRate, int maxBlock = 4096) noexcept;
    void reset() noexcept;

    void setState(const StereoWidthState& state) noexcept;
    [[nodiscard]] const StereoWidthState& state() const noexcept { return state_; }

    void process(float* const* channels, int channelCount, int sampleCount) noexcept;

    [[nodiscard]] int latencySamples() const noexcept { return 0; }
    [[nodiscard]] StereoWidthMeters meters() const noexcept { return meters_; }

private:
    void updateLowShelfCoeff() noexcept;

    StereoWidthState state_ {};
    StereoWidthMeters meters_ {};
    double sampleRate_ {48'000.0};

    ParameterSmoother midSm_;
    ParameterSmoother sideSm_;
    ParameterSmoother outSm_;
    ParameterSmoother bypassSm_;
    ParameterSmoother guardSm_; // correlation-driven side attenuation 0..1

    // One-pole low-pass on side for low-band mono (side HPF residual).
    double sideLpCoeff_ {0.0};
    std::array<double, 2> sideLpState_ {0.0, 0.0};

    double corrProduct_ {0.0};
    double corrLeftSq_ {0.0};
    double corrRightSq_ {0.0};
    std::uint64_t corrSamples_ {0};
};

} // namespace mastering::dsp
