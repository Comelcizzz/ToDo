#include "mastering/assistant/LoudnessMatch.h"

#include "mastering/analysis/LoudnessMeter.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace mastering::assistant {
namespace {

struct BufferStats {
    double integratedLufs {-120.0};
    double shortTermLufs {-120.0};
    bool integratedValid {false};
    bool shortTermValid {false};
    double rmsDb {-120.0};
    double truePeakLinear {0.0};
    bool truePeakValid {false};
};

double bufferDurationSeconds(const std::vector<std::vector<float>>& buf, double sampleRate)
{
    if (buf.empty() || sampleRate <= 0.0)
        return 0.0;
    return double(buf.front().size()) / sampleRate;
}

double rmsDbOf(const std::vector<std::vector<float>>& buf)
{
    if (buf.empty() || buf.front().empty())
        return -120.0;
    const auto n = buf.front().size();
    const auto ch = buf.size();
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t c = 0; c < ch; ++c) {
        for (std::size_t i = 0; i < n && i < buf[c].size(); ++i) {
            const double v = double(buf[c][i]);
            sum += v * v;
            ++count;
        }
    }
    if (count == 0)
        return -120.0;
    return 20.0 * std::log10(std::max(1.0e-12, std::sqrt(sum / double(count))));
}

BufferStats measure(const std::vector<std::vector<float>>& buf, double sampleRate)
{
    BufferStats stats;
    stats.rmsDb = rmsDbOf(buf);
    if (buf.empty() || sampleRate <= 0.0)
        return stats;

    const int channels = static_cast<int>(std::min<std::size_t>(buf.size(), 2));
    const int frames = static_cast<int>(buf.front().size());
    if (channels <= 0 || frames <= 0)
        return stats;

    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sampleRate, channels, 4096);

    constexpr int kBlock = 4096;
    std::vector<const float*>ptrs(static_cast<std::size_t>(channels));
    std::vector<std::vector<float>> scratch(static_cast<std::size_t>(channels));

    for (int offset = 0; offset < frames;) {
        const int n = std::min(kBlock, frames - offset);
        for (int c = 0; c < channels; ++c) {
            scratch[static_cast<std::size_t>(c)].assign(
                buf[static_cast<std::size_t>(c)].begin() + offset,
                buf[static_cast<std::size_t>(c)].begin() + offset + n);
            ptrs[static_cast<std::size_t>(c)] = scratch[static_cast<std::size_t>(c)].data();
        }
        meter.process(ptrs.data(), channels, n);
        offset += n;
    }
    meter.finalize();
    const auto reading = meter.snapshot();
    stats.integratedLufs = reading.integratedLufs;
    stats.shortTermLufs = reading.shortTermLufs;
    stats.integratedValid = reading.integratedValid;
    stats.shortTermValid = reading.shortTermValid;
    stats.truePeakLinear = reading.truePeakLinear;
    stats.truePeakValid = reading.truePeakValid;
    return stats;
}

} // namespace

LoudnessMatchResult matchBuffers(
    const std::vector<std::vector<float>>& source,
    const std::vector<std::vector<float>>& target,
    double sampleRate,
    double durationSeconds)
{
    LoudnessMatchResult result;
    if (sampleRate <= 0.0 || source.empty() || target.empty()) {
        result.notes = "Invalid input buffers or sample rate";
        return result;
    }

    const double srcDur = bufferDurationSeconds(source, sampleRate);
    const double tgtDur = bufferDurationSeconds(target, sampleRate);
    const double dur = durationSeconds > 0.0
        ? durationSeconds
        : std::min(srcDur, tgtDur);

    const auto src = measure(source, sampleRate);
    const auto tgt = measure(target, sampleRate);

    result.sourceIntegratedLufs = src.integratedLufs;
    result.targetIntegratedLufs = tgt.integratedLufs;

    if (dur >= 3.0 && src.integratedValid && tgt.integratedValid) {
        result.method = "integrated-lufs";
        result.usedIntegratedLufs = true;
        result.matchGainDb = tgt.integratedLufs - src.integratedLufs;
        result.valid = true;
        result.notes = "Matched using BS.1770 integrated LUFS (>=3s)";
    } else if (dur >= 1.0 && dur < 3.0 && src.shortTermValid && tgt.shortTermValid) {
        result.method = "short-term-lufs";
        result.usedIntegratedLufs = false;
        result.sourceIntegratedLufs = src.shortTermLufs;
        result.targetIntegratedLufs = tgt.shortTermLufs;
        result.matchGainDb = tgt.shortTermLufs - src.shortTermLufs;
        result.valid = true;
        result.notes = "Section 1-3s: short-term LUFS (not integrated)";
    } else if (dur >= 1.0 && (src.shortTermValid || tgt.shortTermValid)) {
        // Prefer short-term when only one side has integrated invalid.
        result.method = "short-term-lufs";
        result.usedIntegratedLufs = false;
        const double s = src.shortTermValid ? src.shortTermLufs : src.rmsDb;
        const double t = tgt.shortTermValid ? tgt.shortTermLufs : tgt.rmsDb;
        result.sourceIntegratedLufs = s;
        result.targetIntegratedLufs = t;
        result.matchGainDb = t - s;
        result.valid = true;
        result.notes = "Short-term LUFS with partial validity";
    } else {
        // <1s or insufficient loudness validity — bounded RMS. Never call this LUFS.
        result.method = "bounded-rms-short-preview";
        result.usedIntegratedLufs = false;
        result.sourceIntegratedLufs = src.rmsDb;
        result.targetIntegratedLufs = tgt.rmsDb;
        result.matchGainDb = std::clamp(tgt.rmsDb - src.rmsDb, -12.0, 12.0);
        result.valid = std::isfinite(result.matchGainDb);
        result.notes =
            "Preview <1s (or invalid LUFS windows): bounded RMS match — not LUFS";
    }

    // Estimate true peak after match gain.
    const double gainLin = std::pow(10.0, result.matchGainDb / 20.0);
    const double tpLin = src.truePeakValid ? src.truePeakLinear * gainLin : 0.0;
    result.truePeakAfterMatchDbtp = tpLin > 0.0
        ? 20.0 * std::log10(std::max(1.0e-12, tpLin))
        : src.rmsDb + result.matchGainDb;

    std::ostringstream oss;
    oss << result.notes << "; duration=" << dur << "s method=" << result.method
        << " gain=" << result.matchGainDb << " dB";
    result.notes = oss.str();
    return result;
}

} // namespace mastering::assistant
