#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mastering::dsp {

// Linear-phase polyphase FIR oversampler (windowed-sinc, DC-normalized phases).
// Factors: 1, 2, 4, 8. prepare() may allocate; process paths must not.
class Oversampler {
public:
    static constexpr int kMaxFactor = 8;
    static constexpr int kTapsPerPhase = 48; // prototype length = factor * taps
    static constexpr int kMaxChannels = 2;

    void prepare(
        double sampleRate,
        int maximumBlockSize,
        int channelCount,
        int factor) noexcept;
    void reset() noexcept;

    // Upsample `sampleCount` base-rate frames → factor * sampleCount OS frames
    // into preallocated channel-major buffers (via processBlock callback style).
    void processUp(
        const float* const* input,
        float* const* upsampled,
        int channelCount,
        int sampleCount) noexcept;

    void processDown(
        const float* const* upsampled,
        float* const* output,
        int channelCount,
        int sampleCount) noexcept;

    // Convenience: upsample → callback on OS buffers → downsample.
    template <typename NonlinearFn>
    void process(
        float* const* channels,
        int channelCount,
        int sampleCount,
        NonlinearFn&& nonlinear) noexcept
    {
        if (factor_ <= 1) {
            nonlinear(channels, channelCount, sampleCount, 1.0);
            return;
        }
        processUp(channels, upChannelPointers_.data(), channelCount, sampleCount);
        nonlinear(upChannelPointers_.data(), channelCount, sampleCount * factor_, static_cast<double>(factor_));
        processDown(upChannelPointers_.data(), channels, channelCount, sampleCount);
    }

    [[nodiscard]] int factor() const noexcept { return factor_; }
    // End-to-end up+down group delay in base-rate samples (integer, ceil).
    [[nodiscard]] int latencySamplesBaseRate() const noexcept { return latencySamples_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int maximumBlockSize() const noexcept { return maximumBlockSize_; }

    // Scratch accessors for external nonlinear stages that own the OS loop.
    [[nodiscard]] float* upChannel(int channel) noexcept
    {
        return upBuffers_[static_cast<std::size_t>(channel)].data();
    }

private:
    void designKernels() noexcept;

    double sampleRate_ {48'000.0};
    int factor_ {1};
    int channelCount_ {2};
    int maximumBlockSize_ {4096};
    int latencySamples_ {0};

    // coeffs_[phase][tap]
    std::array<std::array<double, kTapsPerPhase>, kMaxFactor> coeffs_ {};
    // history per channel for up and down (taps)
    std::array<std::array<double, kTapsPerPhase>, kMaxChannels> upHistory_ {};
    std::array<std::vector<double>, kMaxChannels> downOsHistory_ {};
    std::array<double, kMaxFactor * kTapsPerPhase> downPrototype_ {};

    std::array<std::vector<float>, kMaxChannels> upBuffers_ {};
    std::array<float*, kMaxChannels> upChannelPointers_ {};
};

} // namespace mastering::dsp
