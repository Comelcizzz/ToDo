#include "mastering/dsp/ExportQc.h"
#include "mastering/dsp/ProcessorChain.h"

#include <cmath>
#include <sstream>

namespace mastering::dsp {

ExportQcReport ExportQc::analyse(
    const std::vector<std::vector<float>>& channels,
    double sampleRate,
    int bitDepth,
    const MasterSafetyMeters& meters,
    int latencyCompensationSamples,
    double configuredCeilingDbtp,
    double ceilingToleranceDb)
{
    ExportQcReport report;
    report.sampleRate = sampleRate;
    report.bitDepth = bitDepth;
    report.channelCount = static_cast<int>(channels.size());
    report.renderLatencyCompensationSamples = latencyCompensationSamples;
    report.configuredCeilingDbtp = configuredCeilingDbtp;
    report.ceilingToleranceDb = ceilingToleranceDb;
    report.maxLimiterGrDb = meters.limiterMaxGrDb;
    report.avgLimiterGrDb = meters.limiterAvgGrDb;

    if (channels.empty() || sampleRate <= 0.0) {
        report.status = QcStatus::fail;
        report.summary = "unreadable output / empty buffer";
        return report;
    }

    const auto frames = channels.front().size();
    for (const auto& ch : channels) {
        if (ch.size() != frames) {
            report.status = QcStatus::fail;
            report.summary = "corrupted duration / channel length mismatch";
            return report;
        }
    }
    report.durationSeconds = static_cast<double>(frames) / sampleRate;
    report.limiterActiveSeconds =
        sampleRate > 0.0 ? static_cast<double>(meters.limiterActiveSamples) / sampleRate : 0.0;

    double samplePeak = 0.0;
    double dcSum = 0.0;
    std::uint64_t total = 0;
    for (const auto& ch : channels) {
        for (float s : ch) {
            if (std::isnan(s))
                ++report.nanCount;
            else if (std::isinf(s))
                ++report.infCount;
            else {
                samplePeak = std::max(samplePeak, static_cast<double>(std::abs(s)));
                dcSum += s;
                if (std::abs(s) >= 0.999)
                    ++report.clippedSampleCount;
            }
            ++total;
        }
    }
    report.dcOffset = total > 0 ? dcSum / static_cast<double>(total) : 0.0;
    report.samplePeakDbfs = gainToDb(samplePeak);

    mastering::analysis::LoudnessMeter meter;
    meter.prepare(sampleRate, static_cast<int>(channels.size()), 4096);
    std::vector<const float*> ptrs(channels.size());
    constexpr int hop = 2048;
    for (std::size_t offset = 0; offset < frames; offset += static_cast<std::size_t>(hop)) {
        const auto n = static_cast<int>(std::min(frames - offset, static_cast<std::size_t>(hop)));
        for (std::size_t c = 0; c < channels.size(); ++c)
            ptrs[c] = channels[c].data() + offset;
        meter.process(ptrs.data(), static_cast<int>(channels.size()), n);
    }
    meter.finalize();
    const auto reading = meter.snapshot();
    report.truePeakDbtp = gainToDb(reading.truePeakLinear);
    report.integratedLufs = reading.integratedLufs;
    report.loudnessRangeLu = reading.loudnessRangeLu;

    if (report.nanCount > 0 || report.infCount > 0) {
        report.status = QcStatus::fail;
        report.summary = "NaN/Inf present";
        return report;
    }
    if (report.truePeakDbtp > configuredCeilingDbtp + ceilingToleranceDb) {
        report.status = QcStatus::fail;
        report.summary = "true peak exceeds configured ceiling";
        return report;
    }
    if (report.durationSeconds <= 0.0) {
        report.status = QcStatus::fail;
        report.summary = "corrupted duration";
        return report;
    }

    if (report.maxLimiterGrDb > 12.0 || report.clippedSampleCount > frames / 10
        || std::abs(report.dcOffset) > 0.01 || report.integratedLufs < -50.0
        || report.integratedLufs > -5.0) {
        report.status = QcStatus::warning;
        report.summary = "render OK with warnings";
    } else {
        report.status = QcStatus::pass;
        report.summary = "PASS";
    }
    return report;
}

std::string ExportQc::toJson(const ExportQcReport& r)
{
    const char* st = r.status == QcStatus::pass ? "PASS"
        : r.status == QcStatus::warning         ? "WARNING"
                                                : "FAIL";
    std::ostringstream o;
    o << "{\n"
      << "  \"status\": \"" << st << "\",\n"
      << "  \"summary\": \"" << r.summary << "\",\n"
      << "  \"samplePeakDbfs\": " << r.samplePeakDbfs << ",\n"
      << "  \"truePeakDbtp\": " << r.truePeakDbtp << ",\n"
      << "  \"integratedLufs\": " << r.integratedLufs << ",\n"
      << "  \"loudnessRangeLu\": " << r.loudnessRangeLu << ",\n"
      << "  \"dcOffset\": " << r.dcOffset << ",\n"
      << "  \"nanCount\": " << r.nanCount << ",\n"
      << "  \"infCount\": " << r.infCount << ",\n"
      << "  \"clippedSampleCount\": " << r.clippedSampleCount << ",\n"
      << "  \"maxLimiterGrDb\": " << r.maxLimiterGrDb << ",\n"
      << "  \"avgLimiterGrDb\": " << r.avgLimiterGrDb << ",\n"
      << "  \"limiterActiveSeconds\": " << r.limiterActiveSeconds << ",\n"
      << "  \"durationSeconds\": " << r.durationSeconds << ",\n"
      << "  \"sampleRate\": " << r.sampleRate << ",\n"
      << "  \"bitDepth\": " << r.bitDepth << ",\n"
      << "  \"channelCount\": " << r.channelCount << ",\n"
      << "  \"renderLatencyCompensationSamples\": " << r.renderLatencyCompensationSamples << ",\n"
      << "  \"configuredCeilingDbtp\": " << r.configuredCeilingDbtp << "\n"
      << "}\n";
    return o.str();
}

std::string ExportQc::toMarkdown(const ExportQcReport& r)
{
    const char* st = r.status == QcStatus::pass ? "PASS"
        : r.status == QcStatus::warning         ? "WARNING"
                                                : "FAIL";
    std::ostringstream o;
    o << "# Export QC\n\n"
      << "- Status: **" << st << "** (" << r.summary << ")\n"
      << "- Sample peak: " << r.samplePeakDbfs << " dBFS\n"
      << "- True peak: " << r.truePeakDbtp << " dBTP (ceiling " << r.configuredCeilingDbtp << ")\n"
      << "- Integrated: " << r.integratedLufs << " LUFS\n"
      << "- LRA: " << r.loudnessRangeLu << " LU\n"
      << "- DC: " << r.dcOffset << "\n"
      << "- NaN/Inf: " << r.nanCount << "/" << r.infCount << "\n"
      << "- Max GR: " << r.maxLimiterGrDb << " dB\n"
      << "- Duration: " << r.durationSeconds << " s @ " << r.sampleRate << " Hz / "
      << r.bitDepth << "-bit / " << r.channelCount << " ch\n"
      << "- Latency compensation: " << r.renderLatencyCompensationSamples << " samples\n";
    return o.str();
}

} // namespace mastering::dsp
