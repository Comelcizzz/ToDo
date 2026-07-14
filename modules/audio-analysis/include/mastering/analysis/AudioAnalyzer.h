#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace mastering::analysis {

struct SpectrumProfile {
    double subDb {-120.0};
    double bassDb {-120.0};
    double lowMidDb {-120.0};
    double midDb {-120.0};
    double presenceDb {-120.0};
    double airDb {-120.0};
};

// Naming contract (Milestone 0):
// - samplePeakDbfs is always sample peak.
// - estimatedTruePeakDbtp is only meaningful when truePeakIsEstimate == true
//   (offline cubic estimate). Realtime meters leave this unset.
// - integratedLufs is only meaningful when integratedLufsIsValid == true.
// - estimatedLoudnessDb is an RMS-derived display estimate, NEVER labeled LUFS.
struct AudioMetrics {
    double samplePeakDbfs {-120.0};
    double estimatedTruePeakDbtp {-120.0};
    bool truePeakIsEstimate {false};
    double rmsDbfs {-120.0};
    double estimatedLoudnessDb {-120.0};
    bool estimatedLoudnessIsValid {false};
    double integratedLufs {-120.0};
    bool integratedLufsIsValid {false};
    double crestFactorDb {0.0};
    double stereoCorrelation {1.0};
    double transientDensityHz {0.0};
    SpectrumProfile spectrum;
    double durationSeconds {0.0};
    int sampleRate {0};
    std::size_t channels {0};
};

class AudioAnalyzer {
public:
    [[nodiscard]] AudioMetrics analyze(
        const std::vector<std::vector<float>>& channels,
        double sampleRate) const;

private:
    [[nodiscard]] static SpectrumProfile calculateSpectrum(
        std::span<const float> mono,
        double sampleRate);
};

class RealtimeMeter {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void process(const float* const* channels, int channelCount, int sampleCount) noexcept;

    [[nodiscard]] AudioMetrics snapshot() const noexcept;

private:
    std::atomic<double> peak_ {0.0};
    std::atomic<double> sumSquares_ {0.0};
    std::atomic<double> leftRightProduct_ {0.0};
    std::atomic<double> leftSquares_ {0.0};
    std::atomic<double> rightSquares_ {0.0};
    std::atomic<double> transientCount_ {0.0};
    std::atomic<double> previousEnvelope_ {0.0};
    std::atomic<std::uint64_t> sampleCount_ {0};
    std::atomic<int> channelCount_ {0};
    std::atomic<double> sampleRate_ {48'000.0};
};

[[nodiscard]] std::string toJson(const AudioMetrics& metrics);
[[nodiscard]] bool jsonClaimsLufsForEstimate(std::string_view json);
[[nodiscard]] bool jsonClaimsTruePeakWithoutEstimate(std::string_view json);

} // namespace mastering::analysis
