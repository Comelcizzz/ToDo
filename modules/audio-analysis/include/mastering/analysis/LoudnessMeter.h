#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mastering::analysis {

enum class MetricAvailability {
    unavailable, // no audio yet / reset
    warmingUp,   // collecting window
    valid,       // full window satisfied and standards-validated where required
    provisional, // usable estimate, not finalized (integrated streaming)
    unverified,  // numeric available but official vectors not passed / estimate only
    stale,       // cleared after reset/switch; UI must not show prior number
    degraded,    // analysis overflow / dropped frames / capacity exceeded
    invalidInput // non-finite input was sanitized
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
    // Until official TP vectors pass: reconstructed peak is an estimate only.
    bool truePeakIsEstimate {true};
    bool programmeCapacityExceeded {false};
    bool finalized {false};

    std::uint64_t framesProcessed {0};
    std::uint64_t droppedAnalysisFrames {0};
    // Documented max programme duration for integrated storage (seconds).
    double integratedCapacitySeconds {0.0};
};

// Shared realtime/offline loudness + true-peak engine (Milestone 1A).
// prepare() may allocate; process() must not allocate, lock, or I/O.
class LoudnessMeter {
public:
    // M1A officially supports mono and stereo only.
    void prepare(double sampleRate, int maximumChannels = 2, int maximumBlockSize = 4096) noexcept;
    void reset() noexcept;
    void process(const float* const* channels, int channelCount, int sampleCount) noexcept;
    [[nodiscard]] LoudnessReading snapshot() const noexcept;
    // Offline/file analysis: flush hop remainder + true-peak FIR zero-tail.
    // Safe/idempotent: second call is a no-op.
    void finalize() noexcept;

    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int maximumChannels() const noexcept { return maximumChannels_; }
    [[nodiscard]] bool programmeCapacityExceeded() const noexcept { return programmeCapacityExceeded_; }

    [[nodiscard]] double measureKWeightMagnitudeDb(double frequencyHz) const noexcept;
    void dumpTruePeakCoefficients(std::vector<double>& interleavedPhases) const;
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
    void accumulateSamplePeak(const float* const* channels, int channelCount, int sampleCount) noexcept;
    void reconstructTruePeak(const float* const* channels, int channelCount, int sampleCount) noexcept;
    void flushTruePeakTail() noexcept;
    void emitHop() noexcept;
    void pushBlockMeanSquare(double meanSquare) noexcept;
    void pushShortTermLoudnessSample() noexcept;
    void recomputeIntegrated(bool finalized) noexcept;
    void recomputeLoudnessRange() noexcept;
    void refreshAvailabilityFlags() noexcept;
    [[nodiscard]] static double powerToLufs(double meanSquarePower) noexcept;
    // Mono/stereo only — surround weights are intentionally unused in M1A.
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
    bool programmeCapacityExceeded_ {false};
    std::uint64_t droppedAnalysisFrames_ {0};
    bool sawInvalidInput_ {false};

    std::array<ChannelState, 2> channels_ {}; // mono/stereo only
    std::vector<double> hopEnergyRing_;
    std::size_t hopWrite_ {0};
    std::size_t hopsFilled_ {0};

    std::vector<double> blockMeanSquares_;
    std::size_t blockWrite_ {0};
    std::size_t blockCount_ {0};
    std::size_t blockCapacity_ {0};

    std::vector<double> shortTermEnergyRing_;
    std::size_t shortTermWrite_ {0};
    std::size_t shortTermFilled_ {0};
    double shortTermEnergySum_ {0.0};

    // Short-term loudness history sampled ~every hop (≤100 ms) for LRA (Tech 3342).
    std::vector<double> shortTermLoudnessHistory_;
    std::size_t shortTermHistoryWrite_ {0};
    std::size_t shortTermHistoryCount_ {0};

    // Reconstructed peak (4× polyphase windowed-sinc) — Estimated / Unverified until
    // official Tech 3341 signals 15–23 pass.
    static constexpr int kTruePeakPhases = 4;
    static constexpr int kTruePeakTaps = 24;
    static constexpr int kTruePeakWarmup = 24;
    std::array<std::array<double, kTruePeakTaps>, kTruePeakPhases> truePeakCoeffs_ {};
    std::array<std::array<double, kTruePeakTaps>, 2> truePeakHistory_ {};
    std::size_t truePeakSamplesSeen_ {0};
    bool truePeakReady_ {false};
    bool truePeakTailFlushed_ {false};

    std::vector<double> lraShortTermScratch_;
    std::vector<double> lraGatedScratch_;

    LoudnessReading reading_ {};
};

[[nodiscard]] const char* metricAvailabilityToString(MetricAvailability state) noexcept;

} // namespace mastering::analysis
