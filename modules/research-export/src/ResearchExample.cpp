#include "mastering/research/ResearchExample.h"

#include <cstdint>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace mastering::research {
namespace {

using json = nlohmann::json;

std::string anonymize(std::string_view value)
{
    constexpr std::uint64_t offset = 14'695'981'039'346'656'037ULL;
    constexpr std::uint64_t prime = 1'099'511'628'211ULL;
    auto hash = offset;
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= prime;
    }
    std::ostringstream result;
    result << std::hex << std::setfill('0') << std::setw(16) << hash;
    return result.str();
}

json metricsToJson(const analysis::AudioMetrics& metrics)
{
    return {
        {"samplePeakDbfs", metrics.samplePeakDbfs},
        {"estimatedTruePeakDbtp", metrics.estimatedTruePeakDbtp},
        {"rmsDbfs", metrics.rmsDbfs},
        {"integratedLufs", metrics.integratedLufs},
        {"crestFactorDb", metrics.crestFactorDb},
        {"stereoCorrelation", metrics.stereoCorrelation},
        {"transientDensityHz", metrics.transientDensityHz},
        {"durationSeconds", metrics.durationSeconds},
        {"sampleRate", metrics.sampleRate},
        {"channels", metrics.channels},
        {"spectrum", {
            {"subDb", metrics.spectrum.subDb},
            {"bassDb", metrics.spectrum.bassDb},
            {"lowMidDb", metrics.spectrum.lowMidDb},
            {"midDb", metrics.spectrum.midDb},
            {"presenceDb", metrics.spectrum.presenceDb},
            {"airDb", metrics.spectrum.airDb}
        }}
    };
}

json processingToJson(const dsp::ProcessorSettings& settings)
{
    return {
        {"bypass", settings.bypass},
        {"inputGainDb", settings.inputGainDb},
        {"amount", settings.amount},
        {"saturation", settings.saturation},
        {"clipCeilingDb", settings.clipCeilingDb},
        {"outputGainDb", settings.outputGainDb},
        {"equalizer", {
            {"highPassHz", settings.equalizer.highPassHz},
            {"lowShelfGainDb", settings.equalizer.lowShelfGainDb},
            {"presenceGainDb", settings.equalizer.presenceGainDb},
            {"highShelfGainDb", settings.equalizer.highShelfGainDb}
        }},
        {"compressor", {
            {"thresholdDb", settings.compressor.thresholdDb},
            {"ratio", settings.compressor.ratio},
            {"attackMs", settings.compressor.attackMs},
            {"releaseMs", settings.compressor.releaseMs},
            {"makeupDb", settings.compressor.makeupDb}
        }}
    };
}

} // namespace

std::string serializeExample(
    const project::ProjectDocument& project,
    const ExportOptions& options)
{
    json value {
        {"schemaVersion", 1},
        {"kind", "mastering-audio-research-example"},
        {"privacy", {
            {"audioIncluded", false},
            {"pathsIncluded", false},
            {"namesIncluded", false},
            {"exportIsUserInitiated", true}
        }},
        {"project", {
            {"anonymousId", anonymize(project.id)},
            {"sampleRate", project.sampleRate},
            {"trackCount", project.tracks.size()},
            {"selectedVariant", options.selectedVariant},
            {"userApproved", options.userApproved},
            {"tracks", json::array()}
        }}
    };

    auto& tracks = value["project"]["tracks"];
    for (std::size_t index = 0; index < project.tracks.size(); ++index) {
        const auto& track = project.tracks[index];
        tracks.push_back({
            {"index", index},
            {"role", project::roleToString(track.role)},
            {"metrics", metricsToJson(track.metrics)},
            {"approvedSettings", {
                {"gainDb", track.gainDb},
                {"pan", track.pan},
                {"polarityInverted", track.polarityInverted},
                {"processing", processingToJson(track.processing)}
            }}
        });
    }
    return value.dump(2);
}

bool isResearchExample(std::string_view source)
{
    try {
        const auto value = json::parse(source);
        return value.value("schemaVersion", 0) == 1
            && value.value("kind", "") == "mastering-audio-research-example"
            && value.contains("project")
            && value["project"].contains("tracks");
    } catch (const json::exception&) {
        return false;
    }
}

} // namespace mastering::research
