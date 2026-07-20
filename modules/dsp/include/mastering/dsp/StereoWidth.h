#pragma once

#include "mastering/dsp/ParameterSmoother.h"

#include <array>
#include <cstdint>

namespace mastering::dsp {

struct StereoWidthState {
    double midGainDb {0.0};
    double sideGainDb {0.0};       // high-band side gain (width)
    double width {1.0};            // 0 = mono (side→0), 1 = unity side scale
    double lowBandMonoHz {120.0};  // complementary split cutoff
    bool lowBandMonoEnabled {true}; // when true, low-band side width forced to 0
    double minCorrelation {-0.2};  // correlation guard floor
    double outputTrimDb {0.0};
    bool bypass {false};
};

struct StereoWidthMeters {
    double correlation {1.0};
    double sideGainDb {0.0};
    double midGainDb {0.0};
};

/*
 * Stereo width with complementary one-pole side crossover.
 *
 * Split (exact complementary on the Side signal):
 *   sideLow  = LPF(side)                 // one-pole low-pass
 *   sideHigh = side - sideLow            // complementary high residual
 *   ⇒ sideLow + sideHigh == side (machine precision aside)
 *
 * Reconstruction:
 *   sideOut = sideLow * lowWidth + sideHigh * highWidth
 *   where lowWidth = 0 when lowBandMonoEnabled, else width
 *         highWidth = width * sideGainLinear * correlationGuard
 *
 * Unity mode (documented):
 *   width=1, midGainDb=0, sideGainDb=0, lowBandMonoEnabled=false
 *   ⇒ sideOut == side, mid unchanged ⇒ exact M/S round-trip identity
 *   (flat magnitude / zero residual vs input within float noise).
 *
 * Low-band mono mode (default product setting):
 *   lowWidth=0 discards sideLow only. This is intentional mono-bass, not a
 *   full-band unity path. Complementary split prevents non-complementary
 *   notches from overlapping LPF/HPF pairs; discarding low side removes LF
 *   anti-phase energy without boosting Mid.
 *
 * Phase: first-order complementary split has a smooth, predictable relative
 * phase (same pole); not linear-phase. Latency: 0 samples.
 */
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
    void updateCrossoverCoeff() noexcept;

    StereoWidthState state_ {};
    StereoWidthMeters meters_ {};
    double sampleRate_ {48'000.0};

    ParameterSmoother midSm_;
    ParameterSmoother sideSm_;
    ParameterSmoother widthSm_;
    ParameterSmoother lowWidthSm_;
    ParameterSmoother outSm_;
    ParameterSmoother bypassSm_;
    ParameterSmoother guardSm_;

    // One-pole LPF state for complementary side split.
    double sideLpCoeff_ {0.0};
    double sideLpState_ {0.0};

    double corrProduct_ {0.0};
    double corrLeftSq_ {0.0};
    double corrRightSq_ {0.0};
    std::uint64_t corrSamples_ {0};
};

} // namespace mastering::dsp
