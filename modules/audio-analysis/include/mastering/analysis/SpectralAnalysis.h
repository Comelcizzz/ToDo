#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mastering::analysis {

struct SpectralPeak {
    double frequencyHz {0.0};
    double magnitudeDb {-120.0};
    double prominenceDb {0.0};
    double bandwidthHz {0.0};
};

// Shared spectral helpers (Goertzel / peak / onset). Kept in audio-analysis so
// assistant code can reuse without circular module dependencies.
class SpectralAnalysis {
public:
    // DFT magnitude at a single frequency (Goertzel), dB relative to window energy.
    [[nodiscard]] static double toneMagnitudeDb(
        const float* mono,
        std::size_t frames,
        double sampleRate,
        double frequencyHz) noexcept;

    [[nodiscard]] static double toneMagnitudeDb(
        const std::vector<float>& mono,
        double sampleRate,
        double frequencyHz) noexcept;

    [[nodiscard]] static std::vector<SpectralPeak> findPeaksInBand(
        const float* mono,
        std::size_t frames,
        double sampleRate,
        double lowHz,
        double highHz,
        int candidates = 5);

    [[nodiscard]] static std::vector<SpectralPeak> findPeaksInBand(
        const std::vector<float>& mono,
        double sampleRate,
        double lowHz,
        double highHz,
        int candidates = 5);

    // Envelope-rise onset times (seconds). minSpacingSeconds rejects clustered detections.
    [[nodiscard]] static std::vector<double> countOnsets(
        const float* mono,
        std::size_t frames,
        double sampleRate,
        double minSpacingSeconds = 0.2,
        double riseRatio = 1.8,
        double floorLinear = 0.05);

    [[nodiscard]] static std::vector<double> countOnsets(
        const std::vector<float>& mono,
        double sampleRate,
        double minSpacingSeconds = 0.2,
        double riseRatio = 1.8,
        double floorLinear = 0.05);

    [[nodiscard]] static double median(std::vector<double> values);
};

} // namespace mastering::analysis
