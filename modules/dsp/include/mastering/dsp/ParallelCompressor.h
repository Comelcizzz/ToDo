#pragma once

#include "mastering/dsp/ParameterSmoother.h"

#include <array>
#include <vector>

namespace mastering::dsp {

struct ParallelCompressorState {
    double thresholdDb {-18.0};
    double ratio {4.0};
    double attackMs {10.0};
    double releaseMs {100.0};
    double makeupDb {0.0};
    double wetAmount {0.5};      // 0..1 dry/wet mix toward compressed path
    double saturationDrive {1.0}; // ≥1 when saturationEnabled
    bool saturationEnabled {false};
    bool bypass {false};
};

struct ParallelCompressorMeters {
    double gainReductionDb {0.0};
    double wetAmount {0.0};
};

// Real dry/wet parallel compressor. Latency: 0 samples (no look-ahead).
// Dry and wet share the same time base — no comb filtering from path delay mismatch.
class ParallelCompressor {
public:
    void prepare(double sampleRate, int maxBlock = 4096, int channels = 2) noexcept;
    void reset() noexcept;

    void setState(const ParallelCompressorState& state) noexcept;
    [[nodiscard]] const ParallelCompressorState& state() const noexcept { return state_; }

    void process(float* const* channels, int channelCount, int sampleCount) noexcept;

    // Documented: zero look-ahead → 0 sample latency.
    [[nodiscard]] int latencySamples() const noexcept { return 0; }
    [[nodiscard]] ParallelCompressorMeters meters() const noexcept { return meters_; }
    void clearMeters() noexcept;

private:
    [[nodiscard]] double computeGainDb(double levelDb) const noexcept;

    ParallelCompressorState state_ {};
    ParallelCompressorMeters meters_ {};
    double sampleRate_ {48'000.0};
    int channels_ {2};
    int maxBlock_ {4096};

    double envelope_ {0.0};
    double attackCoeff_ {0.0};
    double releaseCoeff_ {0.0};

    ParameterSmoother wetSm_;
    ParameterSmoother makeupSm_;
    ParameterSmoother bypassSm_;
    ParameterSmoother driveSm_;

    std::array<std::vector<float>, 2> dryScratch_ {};
};

} // namespace mastering::dsp
