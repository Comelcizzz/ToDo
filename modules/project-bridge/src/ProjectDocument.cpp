#include "mastering/project/ProjectDocument.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

namespace mastering::project {
namespace {

using json = nlohmann::json;

std::string lower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

json spectrumToJson(const analysis::SpectrumProfile& spectrum)
{
    return {
        {"subDb", spectrum.subDb},
        {"bassDb", spectrum.bassDb},
        {"lowMidDb", spectrum.lowMidDb},
        {"midDb", spectrum.midDb},
        {"presenceDb", spectrum.presenceDb},
        {"airDb", spectrum.airDb}
    };
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
        {"spectrum", spectrumToJson(metrics.spectrum)}
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
            {"lowShelfHz", settings.equalizer.lowShelfHz},
            {"lowShelfGainDb", settings.equalizer.lowShelfGainDb},
            {"presenceHz", settings.equalizer.presenceHz},
            {"presenceGainDb", settings.equalizer.presenceGainDb},
            {"highShelfHz", settings.equalizer.highShelfHz},
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

template <typename Type>
void read(const json& value, const char* key, Type& destination)
{
    if (const auto iterator = value.find(key); iterator != value.end() && !iterator->is_null())
        iterator->get_to(destination);
}

void readMetrics(const json& value, analysis::AudioMetrics& metrics)
{
    read(value, "samplePeakDbfs", metrics.samplePeakDbfs);
    read(value, "estimatedTruePeakDbtp", metrics.estimatedTruePeakDbtp);
    read(value, "rmsDbfs", metrics.rmsDbfs);
    read(value, "integratedLufs", metrics.integratedLufs);
    read(value, "crestFactorDb", metrics.crestFactorDb);
    read(value, "stereoCorrelation", metrics.stereoCorrelation);
    read(value, "transientDensityHz", metrics.transientDensityHz);
    read(value, "durationSeconds", metrics.durationSeconds);
    read(value, "sampleRate", metrics.sampleRate);
    read(value, "channels", metrics.channels);
    if (const auto iterator = value.find("spectrum"); iterator != value.end()) {
        read(*iterator, "subDb", metrics.spectrum.subDb);
        read(*iterator, "bassDb", metrics.spectrum.bassDb);
        read(*iterator, "lowMidDb", metrics.spectrum.lowMidDb);
        read(*iterator, "midDb", metrics.spectrum.midDb);
        read(*iterator, "presenceDb", metrics.spectrum.presenceDb);
        read(*iterator, "airDb", metrics.spectrum.airDb);
    }
}

void readProcessing(const json& value, dsp::ProcessorSettings& settings)
{
    read(value, "bypass", settings.bypass);
    read(value, "inputGainDb", settings.inputGainDb);
    read(value, "amount", settings.amount);
    read(value, "saturation", settings.saturation);
    read(value, "clipCeilingDb", settings.clipCeilingDb);
    read(value, "outputGainDb", settings.outputGainDb);
    if (const auto iterator = value.find("equalizer"); iterator != value.end()) {
        read(*iterator, "highPassHz", settings.equalizer.highPassHz);
        read(*iterator, "lowShelfHz", settings.equalizer.lowShelfHz);
        read(*iterator, "lowShelfGainDb", settings.equalizer.lowShelfGainDb);
        read(*iterator, "presenceHz", settings.equalizer.presenceHz);
        read(*iterator, "presenceGainDb", settings.equalizer.presenceGainDb);
        read(*iterator, "highShelfHz", settings.equalizer.highShelfHz);
        read(*iterator, "highShelfGainDb", settings.equalizer.highShelfGainDb);
    }
    if (const auto iterator = value.find("compressor"); iterator != value.end()) {
        read(*iterator, "thresholdDb", settings.compressor.thresholdDb);
        read(*iterator, "ratio", settings.compressor.ratio);
        read(*iterator, "attackMs", settings.compressor.attackMs);
        read(*iterator, "releaseMs", settings.compressor.releaseMs);
        read(*iterator, "makeupDb", settings.compressor.makeupDb);
    }
}

} // namespace

std::string roleToString(TrackRole role)
{
    switch (role) {
    case TrackRole::drums: return "drums";
    case TrackRole::kick: return "kick";
    case TrackRole::snare: return "snare";
    case TrackRole::toms: return "toms";
    case TrackRole::cymbals: return "cymbals";
    case TrackRole::bass: return "bass";
    case TrackRole::rhythmGuitar: return "rhythm-guitar";
    case TrackRole::leadGuitar: return "lead-guitar";
    case TrackRole::cleanVocal: return "clean-vocal";
    case TrackRole::screamVocal: return "scream-vocal";
    case TrackRole::backingVocal: return "backing-vocal";
    case TrackRole::synth: return "synth";
    case TrackRole::orchestra: return "orchestra";
    case TrackRole::effects: return "effects";
    case TrackRole::custom: return "custom";
    }
    return "custom";
}

std::optional<TrackRole> roleFromString(std::string_view role)
{
    const std::array roles {
        TrackRole::custom,
        TrackRole::drums,
        TrackRole::kick,
        TrackRole::snare,
        TrackRole::toms,
        TrackRole::cymbals,
        TrackRole::bass,
        TrackRole::rhythmGuitar,
        TrackRole::leadGuitar,
        TrackRole::cleanVocal,
        TrackRole::screamVocal,
        TrackRole::backingVocal,
        TrackRole::synth,
        TrackRole::orchestra,
        TrackRole::effects
    };
    const auto normalized = lower(role);
    const auto match = std::ranges::find_if(roles, [&normalized](const auto value) {
        return roleToString(value) == normalized;
    });
    return match != roles.end() ? std::optional {*match} : std::nullopt;
}

TrackRole inferRoleFromFilename(std::string_view filename)
{
    const auto name = lower(filename);
    const auto contains = [&name](std::string_view token) {
        return name.find(token) != std::string::npos;
    };

    if (contains("kick")) return TrackRole::kick;
    if (contains("snare")) return TrackRole::snare;
    if (contains("tom")) return TrackRole::toms;
    if (contains("overhead") || contains("cymbal") || contains("_oh")) return TrackRole::cymbals;
    if (contains("drum")) return TrackRole::drums;
    if (contains("bass")) return TrackRole::bass;
    if (contains("scream") || contains("growl")) return TrackRole::screamVocal;
    if (contains("backing") || contains("bgv") || contains("harmony")) return TrackRole::backingVocal;
    if (contains("vocal") || contains("vox") || contains("lead_v")) return TrackRole::cleanVocal;
    if (contains("lead") && (contains("gtr") || contains("guitar"))) return TrackRole::leadGuitar;
    if (contains("gtr") || contains("guitar") || contains("rhythm")) return TrackRole::rhythmGuitar;
    if (contains("orch") || contains("string") || contains("brass")) return TrackRole::orchestra;
    if (contains("synth") || contains("pad") || contains("keys")) return TrackRole::synth;
    if (contains("fx") || contains("impact") || contains("riser")) return TrackRole::effects;
    return TrackRole::custom;
}

std::string makeProjectId()
{
    std::random_device randomDevice;
    std::mt19937_64 generator(
        randomDevice() ^ static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<unsigned long long> distribution;
    const auto first = distribution(generator);
    const auto second = distribution(generator);
    std::ostringstream output;
    output << std::hex << std::setfill('0')
           << std::setw(16) << first << std::setw(16) << second;
    return output.str();
}

std::string serialize(const ProjectDocument& project)
{
    json value {
        {"schemaVersion", project.schemaVersion},
        {"id", project.id},
        {"name", project.name},
        {"referencePath", project.referencePath},
        {"sampleRate", project.sampleRate},
        {"masterProcessing", processingToJson(project.masterProcessing)},
        {"tracks", json::array()}
    };
    for (const auto& track : project.tracks) {
        value["tracks"].push_back({
            {"id", track.id},
            {"name", track.name},
            {"audioPath", track.audioPath},
            {"role", roleToString(track.role)},
            {"metrics", metricsToJson(track.metrics)},
            {"processing", processingToJson(track.processing)},
            {"gainDb", track.gainDb},
            {"pan", track.pan},
            {"muted", track.muted},
            {"soloed", track.soloed},
            {"polarityInverted", track.polarityInverted}
        });
    }
    return value.dump(2);
}

std::optional<ProjectDocument> deserialize(std::string_view source)
{
    try {
        const auto value = json::parse(source);
        ProjectDocument project;
        read(value, "schemaVersion", project.schemaVersion);
        read(value, "id", project.id);
        read(value, "name", project.name);
        read(value, "referencePath", project.referencePath);
        read(value, "sampleRate", project.sampleRate);
        if (project.schemaVersion != 1 || project.id.empty())
            return std::nullopt;

        if (const auto iterator = value.find("masterProcessing"); iterator != value.end())
            readProcessing(*iterator, project.masterProcessing);

        if (const auto iterator = value.find("tracks");
            iterator != value.end() && iterator->is_array()) {
            for (const auto& trackValue : *iterator) {
                TrackRecord track;
                read(trackValue, "id", track.id);
                read(trackValue, "name", track.name);
                read(trackValue, "audioPath", track.audioPath);
                read(trackValue, "gainDb", track.gainDb);
                read(trackValue, "pan", track.pan);
                read(trackValue, "muted", track.muted);
                read(trackValue, "soloed", track.soloed);
                read(trackValue, "polarityInverted", track.polarityInverted);
                if (const auto role = roleFromString(trackValue.value("role", "custom")))
                    track.role = *role;
                if (const auto metrics = trackValue.find("metrics"); metrics != trackValue.end())
                    readMetrics(*metrics, track.metrics);
                if (const auto processing = trackValue.find("processing");
                    processing != trackValue.end()) {
                    readProcessing(*processing, track.processing);
                }
                if (track.id.empty())
                    track.id = makeProjectId();
                project.tracks.push_back(std::move(track));
            }
        }
        return project;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

} // namespace mastering::project
