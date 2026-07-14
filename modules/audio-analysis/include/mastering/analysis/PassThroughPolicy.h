#pragma once

#include <cmath>
#include <cstddef>
#include <span>

namespace mastering::analysis {

// Analyzer audio policy (Milestone 0):
// - Finite samples are bit-transparent (never rewritten).
// - Non-finite samples (NaN/Inf) are replaced with 0.
// - Sanitization does NOT mean arbitrary audio is altered.
// Do not claim bit-transparency without tests that cover these cases.

template <typename Sample>
inline void sanitizeNonFiniteInPlace(Sample* samples, int count) noexcept
{
    for (int index = 0; index < count; ++index) {
        if (!std::isfinite(samples[index]))
            samples[index] = Sample(0);
    }
}

template <typename Sample>
inline void sanitizeNonFiniteInPlace(std::span<Sample> samples) noexcept
{
    sanitizeNonFiniteInPlace(samples.data(), static_cast<int>(samples.size()));
}

template <typename Sample>
[[nodiscard]] inline bool buffersBitIdentical(
    const Sample* left,
    const Sample* right,
    int count) noexcept
{
    for (int index = 0; index < count; ++index) {
        if (left[index] != right[index])
            return false;
    }
    return true;
}

// Mirrors Analyzer float processBlock audio mutation policy (metering is read-only).
template <typename Sample>
inline void applyAnalyzerSanitizeOnly(Sample* const* channels, int channelCount, int sampleCount) noexcept
{
    for (int channel = 0; channel < channelCount; ++channel)
        sanitizeNonFiniteInPlace(channels[channel], sampleCount);
}

} // namespace mastering::analysis
