#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mastering::analysis {

enum class MetricAvailability {
    unavailable, // no audio yet / reset
    warmingUp,   // collecting window
    valid,       // full window satisfied
    provisional, // usable estimate, not finalized (integrated streaming)
    stale,       // cleared after reset/switch; UI must not show prior number
    degraded     // analysis overflow / dropped frames
};

struct LoudnessReading {
    double momentaryLufs {-120.0};
    double shortTermLufs {-120.0};
    double integratedLufs {-120.0};
    double loudnessRangeLu {0.0};
    MetricAvailability momentaryState {MetricAvailability::unavailable};
    MetricAvailability shortTermState {MetricAvailability::unavailable};
    MetricAvailability integratedState {MetricAvailability::unavailable};
    MetricAvailability loudnessRangeState {MetricAvailability::unavailable};
    MetricAvailability truePeakState {MetricAvailability::unavailable};
    MetricAvailability samplePeakState {MetricAvailability::unavailable};

    // Convenience mirrors (derived from states).
    bool momentaryValid {false};
    bool shortTermValid {false};
    bool integratedValid {false};
    bool integratedProvisional {false};
    bool loudnessRangeValid {false};
    double samplePeakLinear {0.0};
    double truePeakLinear {0.0};
    bool truePeakValid {false};

    std::uint64_t framesProcessed {0};
    std::uint64_t droppedAnalysisFrames {0};
};

// Shared realtime/offline loudness + true-peak engine (Milestone 1A).
// prepare() may allocate; process() must not allocate, lock, or I/O.
class LoudnessMeter {
public:
    void prepare(double sampleRate, int maximumChannels = 2, int maximumBlockSize = 4096) noexcept;
    void reset() noexcept;
    void process(const float* const* channels, int channelCount, int sampleCount) noexcept;
    [[nodiscard]] LoudnessReading snapshot() const noexcept;
    void finalize() noexcept;

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int maximumChannels() const noexcept { return maximumChannels_; }

    // Debug/test: expose K-weight impulse/frequency helpers.
    [[nodiscard]] double measureKWeightMagnitudeDb(double frequencyHz) const noexcept;
    void dumpTruePeakCoefficients(std::vector<double>& interleavedPhases) const;

    // Analysis-only overflow counter (does not affect audio I/O).
    void noteDroppedAnalysisFrames(std::uint64_t count) noexcept;

private:
    struct Biquad {
        double b0 {1.0};
        double b1 {0.0};
        double b2 {0.0};
        double a1 {0.0};
        double a2 {0.0};
        double z1 {0.0};
        double z2 {0.0};

        void reset() noexcept
        {
            z1 = 0.0;
            z2 = 0.0;
        }

        [[nodiscard]] double process(double input) noexcept
        {
            const auto output = b0 * input + z1;
            z1 = b1 * input - a1 * output + z2;
            z2 = b2 * input - a2 * output;
            return output;
        }
    };

    struct ChannelState {
        Biquad shelf;
        Biquad highPass;
        double weight {1.0};
    };

    void configureFilters() noexcept;
    void processTruePeak(const float* const* channels, int channelCount, int sampleCount) noexcept;
    void emitHop() noexcept;
    void pushBlockMeanSquare(double meanSquare) noexcept;
    void recomputeIntegrated(bool finalized) noexcept;
    void recomputeLoudnessRange() noexcept;
    void refreshAvailabilityFlags() noexcept;
    [[nodiscard]] static double powerToLufs(double meanSquarePower) noexcept;
    [[nodiscard]] static double channelWeight(int channel, int channelCount) noexcept;

    double sampleRate_ {48'000.0};
    int channelCount_ {0};
    int lastChannelCount_ {0};
    int maximumChannels_ {2};
    int maximumBlockSize_ {4096};
    std::size_t blockFrames_ {0};
    std::size_t hopFrames_ {0};
    std::size_t hopsPerBlock_ {4};
    std::size_t shortTermFrames_ {0};
    std::size_t framesInHop_ {0};
    std::size_t totalFrames_ {0};
    double hopEnergy_ {0.0};
    double samplePeak_ {0.0};
    double truePeak_ {0.0};
    bool finalized_ {false};
    std::uint64_t droppedAnalysisFrames_ {0};

    std::array<ChannelState, 8> channels_ {};
    std::vector<double> hopEnergyRing_;
    std::size_t hopWrite_ {0};
    std::size_t hopsFilled_ {0};

    std::vector<double> blockMeanSquares_;
    std::size_t blockWrite_ {0};
    std::size_t blockCount_ {0};

    std::vector<double> shortTermEnergyRing_;
    std::size_t shortTermWrite_ {0};
    std::size_t shortTermFilled_ {0};
    double shortTermEnergySum_ {0.0};

    // True-peak: 4× polyphase windowed-sinc approximation (not ITU published taps).
    static constexpr int kTruePeakPhases = 4;
    static constexpr int kTruePeakTaps = 24; // 96-tap prototype at 4×
    static constexpr int kTruePeakWarmup = 24;
    std::array<std::array<double, kTruePeakTaps>, kTruePeakPhases> truePeakCoeffs_ {};
    std::array<std::array<double, kTruePeakTaps>, 8> truePeakHistory_ {};
    std::size_t truePeakSamplesSeen_ {0};
    bool truePeakReady_ {false};

    // Preallocated LRA scratch (finalize only — no heap growth in process).
    std::vector<double> lraShortTermScratch_;
    std::vector<double> lraGatedScratch_;

    LoudnessReading reading_ {};
};

[[nodiscard]] const char* metricAvailabilityToString(MetricAvailability state) noexcept;

} // namespace mastering::analysis
