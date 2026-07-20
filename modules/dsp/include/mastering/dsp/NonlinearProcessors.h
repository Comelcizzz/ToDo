#pragma once

#include "mastering/dsp/Oversampler.h"
#include "mastering/dsp/ParameterSmoother.h"

#include <array>
#include <cmath>

namespace mastering::dsp {

enum class SaturationMode {
    tanh = 0
};

struct SaturationSettings {
    bool bypass {false};
    double drive {1.0};       // ≥ 1
    double mix {1.0};         // 0..1
    double outputTrimDb {0.0};
    int oversamplingFactor {4};
    SaturationMode mode {SaturationMode::tanh};
    bool autoGainStatic {true}; // analytical 1/tanh(drive) style
};

class SaturationProcessor {
public:
    void prepare(double sampleRate, int maxBlock, int channels, int factor) noexcept;
    void reset() noexcept;
    void setSettings(const SaturationSettings& settings) noexcept;
    [[nodiscard]] const SaturationSettings& settings() const noexcept { return settings_; }
    void process(float* const* channels, int channelCount, int sampleCount) noexcept;
    [[nodiscard]] int latencySamples() const noexcept;
    void finalize(float* const* channels, int channelCount, int sampleCount) noexcept;

private:
    SaturationSettings settings_;
    Oversampler oversampler_;
    ParameterSmoother driveSm_;
    ParameterSmoother mixSm_;
    ParameterSmoother outSm_;
    ParameterSmoother bypassSm_; // 0=active wet path weight toward dry delayed
    std::array<std::vector<float>, 2> delayLine_ {};
    std::array<std::size_t, 2> delayWrite_ {};
    std::array<std::vector<float>, 2> dryScratch_ {};
    int delayLength_ {0};
    int channels_ {2};
};

struct SoftClipSettings {
    bool bypass {false};
    double thresholdDb {-1.0};
    double kneeDb {3.0}; // soft knee width
    double driveDb {0.0};
    double mix {1.0};
    double outputTrimDb {0.0};
    int oversamplingFactor {4};
};

class SoftClipper {
public:
    void prepare(double sampleRate, int maxBlock, int channels, int factor) noexcept;
    void reset() noexcept;
    void setSettings(const SoftClipSettings& settings) noexcept;
    [[nodiscard]] const SoftClipSettings& settings() const noexcept { return settings_; }
    void process(float* const* channels, int channelCount, int sampleCount) noexcept;
    [[nodiscard]] int latencySamples() const noexcept;
    [[nodiscard]] static double transfer(double x, double thresholdLin, double kneeLin) noexcept;

private:
    SoftClipSettings settings_;
    Oversampler oversampler_;
    ParameterSmoother threshSm_;
    ParameterSmoother kneeSm_;
    ParameterSmoother driveSm_;
    ParameterSmoother mixSm_;
    ParameterSmoother outSm_;
    ParameterSmoother bypassSm_;
    std::array<std::vector<float>, 2> delayLine_ {};
    std::array<std::size_t, 2> delayWrite_ {};
    std::array<std::vector<float>, 2> dryScratch_ {};
    int delayLength_ {0};
    int channels_ {2};
};

struct HardClipSettings {
    bool bypass {false};
    double ceilingDb {-1.0};
    double driveDb {0.0};
    double outputTrimDb {0.0};
    int oversamplingFactor {4};
};

class HardClipper {
public:
    void prepare(double sampleRate, int maxBlock, int channels, int factor) noexcept;
    void reset() noexcept;
    void setSettings(const HardClipSettings& settings) noexcept;
    [[nodiscard]] const HardClipSettings& settings() const noexcept { return settings_; }
    void process(float* const* channels, int channelCount, int sampleCount) noexcept;
    [[nodiscard]] int latencySamples() const noexcept;

private:
    HardClipSettings settings_;
    Oversampler oversampler_;
    ParameterSmoother ceilSm_;
    ParameterSmoother driveSm_;
    ParameterSmoother outSm_;
    ParameterSmoother bypassSm_;
    std::array<std::vector<float>, 2> delayLine_ {};
    std::array<std::size_t, 2> delayWrite_ {};
    std::array<std::vector<float>, 2> dryScratch_ {};
    int delayLength_ {0};
    int channels_ {2};
};

} // namespace mastering::dsp
