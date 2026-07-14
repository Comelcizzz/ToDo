#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mastering::analysis {

struct LoudnessReading {
    double momentaryLufs {-120.0};
    double shortTermLufs {-120.0};
    double integratedLufs {-120.0};
    double loudnessRangeLu {0.0};
    bool momentaryValid {false};
    bool shortTermValid {false};
    bool integratedValid {false};
    bool loudnessRangeValid {false};
    double samplePeakLinear {0.0};
    double truePeakLinear {0.0};
    bool truePeakValid {false};
};

// Shared realtime/offline loudness + true-peak engine (Milestone 1A).
// prepare() may allocate; process() must not allocate or lock.
class LoudnessMeter {
public:
    void prepare(double sampleRate, int maximumChannels = 2) noexcept;
    void reset() noexcept;
    void process(const float* const* channels, int channelCount, int sampleCount) noexcept;
    [[nodiscard]] LoudnessReading snapshot() const noexcept;
    void finalize() noexcept;
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

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
    void recomputeIntegrated() noexcept;
    void recomputeLoudnessRange() noexcept;
    [[nodiscard]] static double powerToLufs(double meanSquarePower) noexcept;
    [[nodiscard]] static double channelWeight(int channel, int channelCount) noexcept;

    double sampleRate_ {48'000.0};
    int channelCount_ {0};
    int maximumChannels_ {2};
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

    static constexpr int kTruePeakPhases = 4;
    static constexpr int kTruePeakTaps = 12;
    std::array<std::array<double, kTruePeakTaps>, kTruePeakPhases> truePeakCoeffs_ {};
    std::array<std::array<double, kTruePeakTaps>, 8> truePeakHistory_ {};
    bool truePeakReady_ {false};

    LoudnessReading reading_ {};
};

} // namespace mastering::analysis
