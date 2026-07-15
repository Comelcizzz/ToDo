#pragma once

#include <array>
#include <span>

namespace mastering::dsp {

struct EqualizerSettings {
    double highPassHz {25.0};
    double lowShelfHz {110.0};
    double lowShelfGainDb {0.0};
    double presenceHz {3'000.0};
    double presenceGainDb {0.0};
    double highShelfHz {10'000.0};
    double highShelfGainDb {0.0};
};

struct CompressorSettings {
    double thresholdDb {-10.0};
    double ratio {2.0};
    double attackMs {30.0};
    double releaseMs {120.0};
    double makeupDb {0.0};
};

struct ProcessorSettings {
    bool bypass {false};
    double inputGainDb {0.0};
    EqualizerSettings equalizer;
    CompressorSettings compressor;
    double saturation {0.0};
    double clipCeilingDb {-1.0};
    double outputGainDb {0.0};
    double amount {1.0};
};

class ProcessorChain {
public:
    void prepare(double sampleRate, int maximumChannels = 2) noexcept;
    void reset() noexcept;
    void setSettings(const ProcessorSettings& settings) noexcept;
    [[nodiscard]] const ProcessorSettings& settings() const noexcept;

    void process(float* const* channels, int channelCount, int sampleCount) noexcept;

private:
    struct FilterState {
        double x1 {0.0};
        double x2 {0.0};
        double y1 {0.0};
        double y2 {0.0};
    };

    struct Coefficients {
        double b0 {1.0};
        double b1 {0.0};
        double b2 {0.0};
        double a1 {0.0};
        double a2 {0.0};
    };

    [[nodiscard]] static double processFilter(
        double input,
        const Coefficients& coefficients,
        FilterState& state) noexcept;
    [[nodiscard]] static Coefficients highPass(double sampleRate, double frequency) noexcept;
    [[nodiscard]] static Coefficients lowShelf(
        double sampleRate,
        double frequency,
        double gainDb) noexcept;
    [[nodiscard]] static Coefficients peak(
        double sampleRate,
        double frequency,
        double gainDb) noexcept;
    [[nodiscard]] static Coefficients highShelf(
        double sampleRate,
        double frequency,
        double gainDb) noexcept;

    ProcessorSettings settings_;
    double sampleRate_ {48'000.0};
    double envelope_ {0.0};
    std::array<FilterState, 8> highPassState_;
    std::array<FilterState, 8> lowShelfState_;
    std::array<FilterState, 8> presenceState_;
    std::array<FilterState, 8> highShelfState_;
};

[[nodiscard]] double gainToDb(double gain) noexcept;
[[nodiscard]] double dbToGain(double decibels) noexcept;

} // namespace mastering::dsp
