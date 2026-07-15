#pragma once

#include "mastering/dsp/ParameterSmoother.h"
#include "mastering/dsp/SvfFilter.h"

#include <array>
#include <cstdint>

namespace mastering::dsp {

enum class DetectorMode {
    peak = 0,
    rms,
    hybrid // max(peak, rms)
};

enum class DetectorChannelMode {
    monoSum = 0,
    stereoMax,
    linkedEnergy // sqrt((L^2+R^2)/n)
};

enum class DetectorSource {
    internal = 0,
    external
};

struct EnvelopeDetectorSettings {
    DetectorMode mode {DetectorMode::peak};
    DetectorChannelMode channelMode {DetectorChannelMode::stereoMax};
    DetectorSource source {DetectorSource::internal};

    double attackMs {5.0};
    double releaseMs {80.0};
    double holdMs {0.0}; // 0 = off

    // Optional band-pass around detector frequency (0 = bypass detector filter).
    bool bandPassEnabled {false};
    double bandPassHz {1'000.0};
    double bandPassQ {1.0};

    // Optional sidechain HPF/LPF (0 Hz LPF disable / HPF disable via flags).
    bool highPassEnabled {false};
    double highPassHz {20.0};
    bool lowPassEnabled {false};
    double lowPassHz {20'000.0};

    double silenceFloorLinear {1.0e-6};
};

// Reusable envelope detector for Dynamic EQ / FD sidechain.
// Detection uses a filtered copy of the input — never the Dynamic EQ output (no feedback).
class EnvelopeDetector {
public:
    void prepare(double sampleRate, int maxBlock, int channels) noexcept;
    void reset() noexcept;
    void setSettings(const EnvelopeDetectorSettings& settings) noexcept;
    [[nodiscard]] const EnvelopeDetectorSettings& settings() const noexcept { return settings_; }

    // Process one sample frame. sidechain may be nullptr when source=internal (uses main).
    // Returns linear envelope (not dB).
    [[nodiscard]] double processSample(
        const float* const* main,
        const float* const* sidechain,
        int channelCount,
        int sampleIndex) noexcept;

    void processBlock(
        const float* const* main,
        const float* const* sidechain,
        int channelCount,
        int sampleCount,
        float* envelopeOut) noexcept; // optional per-sample envelope (may be nullptr)

    [[nodiscard]] double envelopeLinear() const noexcept { return envelope_; }
    [[nodiscard]] double envelopeDb() const noexcept;

private:
    void updateCoefficients() noexcept;
    [[nodiscard]] double filterSample(double x, int channel) noexcept;
    [[nodiscard]] double aggregate(const float* const* input, int channelCount, int i) noexcept;

    EnvelopeDetectorSettings settings_;
    double sampleRate_ {48'000.0};
    int channels_ {2};
    int maxBlock_ {4096};

    double envelope_ {0.0};
    double attackCoeff_ {0.0};
    double releaseCoeff_ {0.0};
    int holdSamples_ {0};
    int holdCounter_ {0};

    std::array<SvfFilter, 2> bandPass_ {};
    std::array<SvfFilter, 2> highPass_ {};
    std::array<SvfFilter, 2> lowPass_ {};

    // RMS smoother state (one-pole on x^2).
    double rmsState_ {0.0};
    double rmsCoeff_ {0.0};
};

} // namespace mastering::dsp
