#pragma once

#include <array>
#include <cstddef>

namespace mastering::dsp {

class TruePeakLimiter {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setCeilingDb(double ceilingDb) noexcept;
    void setReleaseMs(double releaseMs) noexcept;

    void process(float* const* channels, int channelCount, int sampleCount) noexcept;

    [[nodiscard]] double ceilingDb() const noexcept { return ceilingDb_; }
    [[nodiscard]] double lastGainReductionDb() const noexcept { return lastReductionDb_; }

private:
    [[nodiscard]] static double interpolatePeak(
        double y0,
        double y1,
        double y2,
        double y3,
        double fraction) noexcept;

    double sampleRate_ {48'000.0};
    double ceilingDb_ {-1.0};
    double ceilingLinear_ {0.89125093813};
    double releaseCoefficient_ {0.999};
    double envelope_ {1.0};
    double lastReductionDb_ {0.0};
    std::array<std::array<double, 4>, 8> history_ {};
};

class DynamicSeparator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setDepthDb(double depthDb) noexcept;
    void setAttackMs(double attackMs) noexcept;
    void setReleaseMs(double releaseMs) noexcept;

    // Detects from the sidechain buffer and ducks the target buffer in-place.
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
