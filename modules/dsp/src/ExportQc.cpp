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
    double ceilingToleranceDb,
    const StyleQcProfile* styleProfile)
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
    report.safetyClampActivationCount = meters.safetyClampActivationCount;

    if (channels.empty() || sampleRate <= 0.0) {
        report.technicalStatus = QcStatus::fail;
        report.status = QcStatus::fail;
        report.technicalSummary = "unreadable output / empty buffer";
        report.summary = report.technicalSummary;
        return report;
    }

    const auto frames = channels.front().size();
    for (const auto& ch : channels) {
        if (ch.size() != frames) {
            report.technicalStatus = QcStatus::fail;
            report.status = QcStatus::fail;
            report.technicalSummary = "corrupted duration / channel length mismatch";
            report.summary = report.technicalSummary;
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

    // --- Technical QC (universal) ---
    if (report.nanCount > 0 || report.infCount > 0) {
        report.technicalStatus = QcStatus::fail;
        report.technicalSummary = "NaN/Inf present";
    } else if (report.durationSeconds <= 0.0) {
        report.technicalStatus = QcStatus::fail;
        report.technicalSummary = "corrupted duration";
    } else if (report.truePeakDbtp > configuredCeilingDbtp + ceilingToleranceDb) {
        report.technicalStatus = QcStatus::fail;
        report.technicalSummary = "true peak exceeds configured ceiling";
    } else if (std::abs(report.dcOffset) > 0.05) {
        report.technicalStatus = QcStatus::fail;
        report.technicalSummary = "excessive DC offset";
    } else if (std::abs(report.dcOffset) > 0.01) {
        report.technicalStatus = QcStatus::warning;
        report.technicalSummary = "elevated DC offset";
    } else {
        report.technicalStatus = QcStatus::pass;
        report.technicalSummary = "technical PASS";
    }

    // --- Style / profile advisory (optional; never upgrades FAIL from silence) ---
    report.advisoryStatus = QcStatus::pass;
    report.advisorySummary = "no style profile";
    if (styleProfile != nullptr) {
        report.advisorySummary = "style profile: " + styleProfile->name;
        bool warn = false;
        if (styleProfile->minIntegratedLufs
            && report.integratedLufs < *styleProfile->minIntegratedLufs)
            warn = true;
        if (styleProfile->maxIntegratedLufs
            && report.integratedLufs > *styleProfile->maxIntegratedLufs)
            warn = true;
        if (styleProfile->maxLimiterGrDb && report.maxLimiterGrDb > *styleProfile->maxLimiterGrDb)
            warn = true;
        if (warn) {
            report.advisoryStatus = QcStatus::warning;
            report.advisorySummary += " — advisory WARNING";
        } else {
            report.advisorySummary += " — advisory PASS";
        }
    }

    if (report.technicalStatus == QcStatus::fail)
        report.status = QcStatus::fail;
    else if (report.technicalStatus == QcStatus::warning
        || report.advisoryStatus == QcStatus::warning)
        report.status = QcStatus::warning;
    else
        report.status = QcStatus::pass;

    report.summary = report.technicalSummary;
    if (styleProfile != nullptr)
        report.summary += "; " + report.advisorySummary;
    return report;
}

namespace {
const char* statusName(QcStatus s)
{
    return s == QcStatus::pass ? "PASS" : s == QcStatus::warning ? "WARNING" : "FAIL";
}
} // namespace

std::string ExportQc::toJson(const ExportQcReport& r)
{
    std::ostringstream o;
    o << "{\n"
      << "  \"status\": \"" << statusName(r.status) << "\",\n"
      << "  \"technicalStatus\": \"" << statusName(r.technicalStatus) << "\",\n"
      << "  \"advisoryStatus\": \"" << statusName(r.advisoryStatus) << "\",\n"
      << "  \"summary\": \"" << r.summary << "\",\n"
      << "  \"technicalSummary\": \"" << r.technicalSummary << "\",\n"
      << "  \"advisorySummary\": \"" << r.advisorySummary << "\",\n"
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
      << "  \"safetyClampActivationCount\": " << r.safetyClampActivationCount << ",\n"
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
    std::ostringstream o;
    o << "# Export QC\n\n"
      << "- Overall: **" << statusName(r.status) << "**\n"
      << "- Technical: **" << statusName(r.technicalStatus) << "** (" << r.technicalSummary << ")\n"
      << "- Advisory: **" << statusName(r.advisoryStatus) << "** (" << r.advisorySummary << ")\n"
      << "- Sample peak: " << r.samplePeakDbfs << " dBFS\n"
      << "- True peak: " << r.truePeakDbtp << " dBTP (ceiling " << r.configuredCeilingDbtp << ")\n"
      << "- Integrated: " << r.integratedLufs << " LUFS\n"
      << "- LRA: " << r.loudnessRangeLu << " LU\n"
      << "- DC: " << r.dcOffset << "\n"
      << "- NaN/Inf: " << r.nanCount << "/" << r.infCount << "\n"
      << "- Max GR: " << r.maxLimiterGrDb << " dB\n"
      << "- Safety clamp activations: " << r.safetyClampActivationCount << "\n"
      << "- Duration: " << r.durationSeconds << " s @ " << r.sampleRate << " Hz / "
      << r.bitDepth << "-bit / " << r.channelCount << " ch\n"
      << "- Latency compensation: " << r.renderLatencyCompensationSamples << " samples\n";
    return o.str();
}

} // namespace mastering::dsp
