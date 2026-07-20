#pragma once

#include <string>
#include <vector>

namespace mastering::assistant {

struct LoudnessMatchResult {
    double targetIntegratedLufs {-23.0};
    double sourceIntegratedLufs {-23.0};
    double matchGainDb {0.0};
    bool usedIntegratedLufs {false}; // false => short-window / RMS fallback
    std::string method; // "integrated-lufs" | "short-term-lufs" | "bounded-rms-short-preview"
    double truePeakAfterMatchDbtp {-120.0};
    bool valid {false};
    std::string notes;
};

// Match source level toward target using LoudnessMeter policy:
//   duration >= 3s  → integrated LUFS
//   1s .. <3s       → short-term LUFS
//   < 1s            → bounded RMS (never labeled LUFS)
[[nodiscard]] LoudnessMatchResult matchBuffers(
    const std::vector<std::vector<float>>& source,
    const std::vector<std::vector<float>>& target,
    double sampleRate,
    double durationSeconds);

} // namespace mastering::assistant
