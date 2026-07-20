#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace mastering::dsp {

// One-pole / linear ramp smoother for realtime parameters (no allocation).
class ParameterSmoother {
public:
    void prepare(double sampleRate, double timeMs = 20.0) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
        setTimeMs(timeMs);
        current_ = target_;
    }

    void setTimeMs(double timeMs) noexcept
    {
        const auto seconds = std::max(1.0e-4, timeMs * 0.001);
        coefficient_ = std::exp(-1.0 / (seconds * sampleRate_));
    }

    void setTarget(double target) noexcept { target_ = target; }

    void reset(double value) noexcept
    {
        target_ = value;
        current_ = value;
    }

    [[nodiscard]] double next() noexcept
    {
        current_ = target_ + (current_ - target_) * coefficient_;
        if (std::abs(current_ - target_) < 1.0e-9)
            current_ = target_;
        return current_;
    }

    [[nodiscard]] double current() const noexcept { return current_; }
    [[nodiscard]] double target() const noexcept { return target_; }
    [[nodiscard]] bool isSettled() const noexcept { return current_ == target_; }

private:
    double sampleRate_ {48'000.0};
    double coefficient_ {0.0};
    double current_ {0.0};
    double target_ {0.0};
};

[[nodiscard]] inline double gainToDbSafe(double gain) noexcept
{
    const auto g = std::max(1.0e-12, std::abs(gain));
    return 20.0 * std::log10(g);
}

[[nodiscard]] inline double dbToGainSafe(double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

} // namespace mastering::dsp
