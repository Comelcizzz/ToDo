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
    json value {
        {"samplePeakDbfs", metrics.samplePeakDbfs},
        {"rmsDbfs", metrics.rmsDbfs},
        {"estimatedLoudnessDb", metrics.estimatedLoudnessDb},
        {"estimatedLoudnessIsValid", metrics.estimatedLoudnessIsValid},
        {"crestFactorDb", metrics.crestFactorDb},
        {"stereoCorrelation", metrics.stereoCorrelation},
        {"transientDensityHz", metrics.transientDensityHz},
        {"durationSeconds", metrics.durationSeconds},
        {"sampleRate", metrics.sampleRate},
        {"channels", metrics.channels},
        {"truePeakValid", metrics.truePeakValid},
        {"truePeakIsEstimate", metrics.truePeakIsEstimate},
        {"momentaryLufsIsValid", metrics.momentaryLufsIsValid},
        {"shortTermLufsIsValid", metrics.shortTermLufsIsValid},
        {"integratedLufsIsValid", metrics.integratedLufsIsValid},
        {"loudnessRangeIsValid", metrics.loudnessRangeIsValid},
        {"spectrum", spectrumToJson(metrics.spectrum)}
    };
    if (metrics.truePeakValid)
        value["truePeakDbtp"] = metrics.truePeakDbtp;
    if (metrics.truePeakIsEstimate)
        value["estimatedTruePeakDbtp"] = metrics.estimatedTruePeakDbtp;
    if (metrics.momentaryLufsIsValid)
        value["momentaryLufs"] = metrics.momentaryLufs;
    if (metrics.shortTermLufsIsValid)
        value["shortTermLufs"] = metrics.shortTermLufs;
    if (metrics.integratedLufsIsValid)
        value["integratedLufs"] = metrics.integratedLufs;
    if (metrics.loudnessRangeIsValid)
        value["loudnessRangeLu"] = metrics.loudnessRangeLu;
    return value;
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
    read(value, "truePeakDbtp", metrics.truePeakDbtp);
    read(value, "truePeakValid", metrics.truePeakValid);
    read(value, "estimatedTruePeakDbtp", metrics.estimatedTruePeakDbtp);
    read(value, "truePeakIsEstimate", metrics.truePeakIsEstimate);
    read(value, "rmsDbfs", metrics.rmsDbfs);
    read(value, "estimatedLoudnessDb", metrics.estimatedLoudnessDb);
    read(value, "estimatedLoudnessIsValid", metrics.estimatedLoudnessIsValid);
    read(value, "momentaryLufs", metrics.momentaryLufs);
    read(value, "momentaryLufsIsValid", metrics.momentaryLufsIsValid);
    read(value, "shortTermLufs", metrics.shortTermLufs);
    read(value, "shortTermLufsIsValid", metrics.shortTermLufsIsValid);
    read(value, "integratedLufs", metrics.integratedLufs);
    read(value, "integratedLufsIsValid", metrics.integratedLufsIsValid);
    read(value, "loudnessRangeLu", metrics.loudnessRangeLu);
    read(value, "loudnessRangeIsValid", metrics.loudnessRangeIsValid);
    read(value, "crestFactorDb", metrics.crestFactorDb);
    read(value, "stereoCorrelation", metrics.stereoCorrelation);
    read(value, "transientDensityHz", metrics.transientDensityHz);
    read(value, "durationSeconds", metrics.durationSeconds);
    read(value, "sampleRate", metrics.sampleRate);
    read(value, "channels", metrics.channels);
    // Legacy schema v1 files stored RMS-derived values as integratedLufs/true peak.
    // Keep numeric fields but do not mark them valid unless explicitly flagged.
    if (!value.contains("integratedLufsIsValid") && value.contains("integratedLufs"))
        metrics.integratedLufsIsValid = false;
    if (!value.contains("truePeakIsEstimate") && value.contains("estimatedTruePeakDbtp"))
        metrics.truePeakIsEstimate = false;
    if (!value.contains("truePeakValid") && value.contains("truePeakDbtp"))
        metrics.truePeakValid = false;
    if (!metrics.estimatedLoudnessIsValid && value.contains("rmsDbfs")) {
        metrics.estimatedLoudnessDb = metrics.rmsDbfs;
        metrics.estimatedLoudnessIsValid = true;
    }
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

std::string detectorSourceToString(dsp::DetectorSource source)
{
    switch (source) {
    case dsp::DetectorSource::external: return "external";
    case dsp::DetectorSource::internal: return "internal";
    }
    return "internal";
}

dsp::DetectorSource detectorSourceFromString(std::string_view value)
{
    return lower(value) == "external" ? dsp::DetectorSource::external
                                      : dsp::DetectorSource::internal;
}

json dynamicEqBandToJson(const dsp::DynamicEqBandState& band)
{
    return {
        {"bandId", band.bandId},
        {"enabled", band.enabled},
        {"filterType", static_cast<int>(band.filterType)},
        {"frequencyHz", band.frequencyHz},
        {"q", band.q},
        {"staticGainDb", band.staticGainDb},
        {"thresholdDb", band.thresholdDb},
        {"ratio", band.ratio},
        {"maxCutDb", band.maxCutDb},
        {"attackMs", band.attackMs},
        {"releaseMs", band.releaseMs},
        {"detectorBandPass", band.detectorBandPass},
        {"detectorFrequencyHz", band.detectorFrequencyHz},
        {"detectorQ", band.detectorQ},
        {"detectorSource", detectorSourceToString(band.detectorSource)},
        {"stereoMode", static_cast<int>(band.stereoMode)},
        {"targetTrackId", band.targetTrackId},
        {"sidechainSourceId", band.sidechainSourceId}
    };
}

json dynamicEqToJson(const dsp::DynamicEqState& state)
{
    json bands = json::array();
    for (int i = 0; i < state.bandCount && i < dsp::kMaxDynamicEqBands; ++i)
        bands.push_back(dynamicEqBandToJson(state.bands[static_cast<std::size_t>(i)]));
    return {
        {"schemaVersion", state.schemaVersion},
        {"processorRevision", state.processorRevision},
        {"bypass", state.bypass},
        {"wetDry", state.wetDry},
        {"outputGainDb", state.outputGainDb},
        {"bandCount", state.bandCount},
        {"bands", std::move(bands)}
    };
}

void readDynamicEqBand(const json& value, dsp::DynamicEqBandState& band)
{
    read(value, "bandId", band.bandId);
    read(value, "enabled", band.enabled);
    if (value.contains("filterType"))
        band.filterType = static_cast<dsp::DynamicEqFilterType>(value["filterType"].get<int>());
    read(value, "frequencyHz", band.frequencyHz);
    read(value, "q", band.q);
    read(value, "staticGainDb", band.staticGainDb);
    read(value, "thresholdDb", band.thresholdDb);
    read(value, "ratio", band.ratio);
    read(value, "maxCutDb", band.maxCutDb);
    read(value, "attackMs", band.attackMs);
    read(value, "releaseMs", band.releaseMs);
    read(value, "detectorBandPass", band.detectorBandPass);
    read(value, "detectorFrequencyHz", band.detectorFrequencyHz);
    read(value, "detectorQ", band.detectorQ);
    if (value.contains("detectorSource") && value["detectorSource"].is_string())
        band.detectorSource = detectorSourceFromString(value["detectorSource"].get<std::string>());
    if (value.contains("stereoMode"))
        band.stereoMode = static_cast<dsp::DynamicEqStereoMode>(value["stereoMode"].get<int>());
    read(value, "targetTrackId", band.targetTrackId);
    read(value, "sidechainSourceId", band.sidechainSourceId);
}

void readDynamicEq(const json& value, dsp::DynamicEqState& state)
{
    read(value, "schemaVersion", state.schemaVersion);
    read(value, "processorRevision", state.processorRevision);
    read(value, "bypass", state.bypass);
    read(value, "wetDry", state.wetDry);
    read(value, "outputGainDb", state.outputGainDb);
    read(value, "bandCount", state.bandCount);
    state.bandCount = std::clamp(state.bandCount, 1, dsp::kMaxDynamicEqBands);
    if (const auto bands = value.find("bands"); bands != value.end() && bands->is_array()) {
        for (std::size_t i = 0; i < bands->size() && i < static_cast<std::size_t>(dsp::kMaxDynamicEqBands); ++i)
            readDynamicEqBand((*bands)[i], state.bands[i]);
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
    case TrackRole::drumBus: return "drum-bus";
    case TrackRole::bass: return "bass";
    case TrackRole::bassBus: return "bass-bus";
    case TrackRole::rhythmGuitar: return "rhythm-guitar";
    case TrackRole::rhythmGuitarLeft: return "rhythm-guitar-left";
    case TrackRole::rhythmGuitarRight: return "rhythm-guitar-right";
    case TrackRole::leadGuitar: return "lead-guitar";
    case TrackRole::cleanGuitar: return "clean-guitar";
    case TrackRole::guitarBus: return "guitar-bus";
    case TrackRole::cleanVocal: return "clean-vocal";
    case TrackRole::screamVocal: return "scream-vocal";
    case TrackRole::backingVocal: return "backing-vocal";
    case TrackRole::vocalBus: return "vocal-bus";
    case TrackRole::synth: return "synth";
    case TrackRole::orchestra: return "orchestra";
    case TrackRole::effects: return "effects";
    case TrackRole::musicBus: return "music-bus";
    case TrackRole::master: return "master";
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
        TrackRole::drumBus,
        TrackRole::bass,
        TrackRole::bassBus,
        TrackRole::rhythmGuitar,
        TrackRole::rhythmGuitarLeft,
        TrackRole::rhythmGuitarRight,
        TrackRole::leadGuitar,
        TrackRole::cleanGuitar,
        TrackRole::guitarBus,
        TrackRole::cleanVocal,
        TrackRole::screamVocal,
        TrackRole::backingVocal,
        TrackRole::vocalBus,
        TrackRole::synth,
        TrackRole::orchestra,
        TrackRole::effects,
        TrackRole::musicBus,
        TrackRole::master
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
    if (contains("drum") && contains("bus")) return TrackRole::drumBus;
    if (contains("drum")) return TrackRole::drums;
    if (contains("bass") && contains("bus")) return TrackRole::bassBus;
    if (contains("bass")) return TrackRole::bass;
    if (contains("scream") || contains("growl")) return TrackRole::screamVocal;
    if (contains("backing") || contains("bgv") || contains("harmony")) return TrackRole::backingVocal;
    if (contains("vocal") && contains("bus")) return TrackRole::vocalBus;
    if (contains("vocal") || contains("vox") || contains("lead_v")) return TrackRole::cleanVocal;
    if (contains("lead") && (contains("gtr") || contains("guitar"))) return TrackRole::leadGuitar;
    if (contains("clean") && (contains("gtr") || contains("guitar"))) return TrackRole::cleanGuitar;
    if ((contains("gtr") || contains("guitar") || contains("rhythm"))
        && (contains("_l") || contains(" left") || contains("-l") || contains("_left")
            || contains(" l.") || contains(" l ") || contains("-l.") || name.ends_with(" l")
            || contains("left")))
        return TrackRole::rhythmGuitarLeft;
    if ((contains("gtr") || contains("guitar") || contains("rhythm"))
        && (contains("_r") || contains(" right") || contains("-r") || contains("_right")
            || contains(" r.") || contains(" r ") || contains("-r.") || name.ends_with(" r")
            || contains("right")))
        return TrackRole::rhythmGuitarRight;
    if (contains("guitar") && contains("bus")) return TrackRole::guitarBus;
    if (contains("gtr") || contains("guitar") || contains("rhythm")) return TrackRole::rhythmGuitar;
    if (contains("orch") || contains("string") || contains("brass")) return TrackRole::orchestra;
    if (contains("synth") || contains("pad") || contains("keys")) return TrackRole::synth;
    if (contains("fx") || contains("impact") || contains("riser")) return TrackRole::effects;
    if (contains("music") && contains("bus")) return TrackRole::musicBus;
    return TrackRole::custom;
}

std::string sectionKindToString(SectionKind kind)
{
    switch (kind) {
    case SectionKind::intro: return "intro";
    case SectionKind::verse: return "verse";
    case SectionKind::preChorus: return "pre-chorus";
    case SectionKind::chorus: return "chorus";
    case SectionKind::breakdown: return "breakdown";
    case SectionKind::bridge: return "bridge";
    case SectionKind::outro: return "outro";
    case SectionKind::custom: return "custom";
    }
    return "custom";
}

std::optional<SectionKind> sectionKindFromString(std::string_view value)
{
    const auto n = lower(value);
    if (n == "intro") return SectionKind::intro;
    if (n == "verse") return SectionKind::verse;
    if (n == "pre-chorus" || n == "prechorus") return SectionKind::preChorus;
    if (n == "chorus") return SectionKind::chorus;
    if (n == "breakdown") return SectionKind::breakdown;
    if (n == "bridge") return SectionKind::bridge;
    if (n == "outro") return SectionKind::outro;
    if (n == "custom") return SectionKind::custom;
    return std::nullopt;
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
        {"bpm", project.bpm},
        {"selectedVariant", project.selectedVariant},
        {"masterProcessing", processingToJson(project.masterProcessing)},
        {"masterDynamicEqEnabled", project.masterDynamicEqEnabled},
        {"sectionAutomationJson", project.sectionAutomationJson},
        {"virtualDrumBusEnabled", project.virtualDrumBusEnabled},
        {"actionBudgetJson", project.actionBudgetJson},
        {"analysisCacheVersion", project.analysisCacheVersion},
        {"renderIdentityJson", project.renderIdentityJson},
        {"tracks", json::array()},
        {"pairs", json::array()},
        {"buses", json::array()},
        {"sections", json::array()},
        {"mixPassActions", json::array()},
        {"actions", json::array()}
    };
    for (const auto& track : project.tracks) {
        value["tracks"].push_back({
            {"id", track.id},
            {"name", track.name},
            {"audioPath", track.audioPath},
            {"role", roleToString(track.role)},
            {"metrics", metricsToJson(track.metrics)},
            {"processing", processingToJson(track.processing)},
            {"dynamicEq", dynamicEqToJson(track.dynamicEq)},
            {"dynamicEqEnabled", track.dynamicEqEnabled},
            {"vocalRiderEnabled", track.vocalRiderEnabled},
            {"vocalRiderTargetDb", track.vocalRiderTargetDb},
            {"gainDb", track.gainDb},
            {"pan", track.pan},
            {"muted", track.muted},
            {"soloed", track.soloed},
            {"polarityInverted", track.polarityInverted},
            {"pairId", track.pairId},
            {"parentBusId", track.parentBusId},
            {"channelPosition", track.channelPosition},
            {"parallelEnabled", track.parallelEnabled},
            {"parallelWet", track.parallelWet},
            {"parallelThresholdDb", track.parallelThresholdDb},
            {"parallelRatio", track.parallelRatio},
            {"parallelAttackMs", track.parallelAttackMs},
            {"parallelReleaseMs", track.parallelReleaseMs},
            {"parallelMakeupDb", track.parallelMakeupDb},
            {"stereoWidthEnabled", track.stereoWidthEnabled},
            {"sideGainDb", track.sideGainDb},
            {"midGainDb", track.midGainDb},
            {"lowBandMonoHz", track.lowBandMonoHz}
        });
    }
    for (const auto& pair : project.pairs) {
        value["pairs"].push_back({
            {"id", pair.id},
            {"name", pair.name},
            {"leftTrackId", pair.leftTrackId},
            {"rightTrackId", pair.rightTrackId},
            {"parentBusId", pair.parentBusId},
            {"linkedProcessing", pair.linkedProcessing},
            {"stereoWidthEnabled", pair.stereoWidthEnabled},
            {"sideGainDb", pair.sideGainDb},
            {"midGainDb", pair.midGainDb},
            {"lowBandMonoHz", pair.lowBandMonoHz}
        });
    }
    for (const auto& bus : project.buses) {
        value["buses"].push_back({
            {"id", bus.id},
            {"name", bus.name},
            {"role", roleToString(bus.role)},
            {"childTrackIds", bus.childTrackIds},
            {"childPairIds", bus.childPairIds},
            {"processing", processingToJson(bus.processing)},
            {"dynamicEq", dynamicEqToJson(bus.dynamicEq)},
            {"dynamicEqEnabled", bus.dynamicEqEnabled},
            {"gainDb", bus.gainDb},
            {"parallelEnabled", bus.parallelEnabled},
            {"parallelWet", bus.parallelWet},
            {"parallelThresholdDb", bus.parallelThresholdDb},
            {"parallelRatio", bus.parallelRatio},
            {"parallelAttackMs", bus.parallelAttackMs},
            {"parallelReleaseMs", bus.parallelReleaseMs},
            {"parallelMakeupDb", bus.parallelMakeupDb},
            {"parallelDrumJson", bus.parallelDrumJson},
            {"stereoWidthEnabled", bus.stereoWidthEnabled},
            {"sideGainDb", bus.sideGainDb},
            {"midGainDb", bus.midGainDb},
            {"lowBandMonoHz", bus.lowBandMonoHz}
        });
    }
    for (const auto& section : project.sections) {
        value["sections"].push_back({
            {"id", section.id},
            {"kind", sectionKindToString(section.kind)},
            {"name", section.name},
            {"startSeconds", section.startSeconds},
            {"endSeconds", section.endSeconds}
        });
    }
    for (const auto& action : project.mixPassActions) {
        json a {
            {"actionId", action.actionId},
            {"actionVersion", action.actionVersion},
            {"problemType", action.problemType},
            {"targetTrackId", action.targetTrackId},
            {"targetPairId", action.targetPairId},
            {"targetBusId", action.targetBusId},
            {"processorId", action.processorId},
            {"parameterId", action.parameterId},
            {"currentValue", action.currentValue},
            {"proposedValue", action.proposedValue},
            {"allowedMin", action.allowedMin},
            {"allowedMax", action.allowedMax},
            {"globalCap", action.globalCap},
            {"roleCap", action.roleCap},
            {"confidenceAdjustedCap", action.confidenceAdjustedCap},
            {"cumulativeCap", action.cumulativeCap},
            {"confidence", action.confidence},
            {"evidenceScore", action.evidenceScore},
            {"evidenceLabel", action.evidenceLabel},
            {"explanation", action.explanation},
            {"sourceMetrics", action.sourceMetrics},
            {"evidence", action.evidence},
            {"decisionTrace", action.decisionTrace},
            {"processingLevel", action.processingLevel},
            {"sectionScope", action.sectionScope},
            {"state", action.state},
            {"origin", action.origin},
            {"priority", action.priority},
            {"orderIndex", action.orderIndex},
            {"conflictGroup", action.conflictGroup},
            {"supersedes", action.supersedes},
            {"prerequisite", action.prerequisite},
            {"hasProposedProcessing", action.hasProposedProcessing},
            {"hasProposedDynamicEq", action.hasProposedDynamicEq},
            {"vocalRiderEnabled", action.vocalRiderEnabled},
            {"vocalRiderTargetDb", action.vocalRiderTargetDb},
            {"hasPrevious", action.hasPrevious},
            {"previousDynamicEqEnabled", action.previousDynamicEqEnabled}
        };
        if (action.hasProposedProcessing)
            a["proposedProcessing"] = processingToJson(action.proposedProcessing);
        if (action.hasProposedDynamicEq)
            a["proposedDynamicEq"] = dynamicEqToJson(action.proposedDynamicEq);
        if (action.hasPrevious) {
            a["previousGainDb"] = action.previousGainDb;
            a["previousProcessing"] = processingToJson(action.previousProcessing);
            a["previousDynamicEq"] = dynamicEqToJson(action.previousDynamicEq);
        }
        value["mixPassActions"].push_back(std::move(a));
    }
    for (const auto& action : project.actions) {
        json actionJson {
            {"actionId", action.actionId},
            {"trackId", action.trackId},
            {"targetGainDb", action.targetGainDb},
            {"state", action.state},
            {"processing", processingToJson(action.processing)},
            {"hasPrevious", action.hasPrevious},
            {"problemType", action.problemType},
            {"processorId", action.processorId},
            {"sectionScope", action.sectionScope}
        };
        if (action.hasPrevious) {
            actionJson["previousGainDb"] = action.previousGainDb;
            actionJson["previousProcessing"] = processingToJson(action.previousProcessing);
        }
        value["actions"].push_back(std::move(actionJson));
    }
    return value.dump(2);
}

std::optional<ProjectDocument> deserialize(std::string_view source)
{
    DeserializeError error;
    return deserialize(source, error);
}

std::optional<ProjectDocument> deserialize(std::string_view source, DeserializeError& error)
{
    try {
        const auto value = json::parse(source);
        ProjectDocument project;
        read(value, "schemaVersion", project.schemaVersion);
        read(value, "id", project.id);
        read(value, "name", project.name);
        read(value, "referencePath", project.referencePath);
        read(value, "sampleRate", project.sampleRate);
        read(value, "bpm", project.bpm);
        read(value, "selectedVariant", project.selectedVariant);
        read(value, "sectionAutomationJson", project.sectionAutomationJson);
        read(value, "virtualDrumBusEnabled", project.virtualDrumBusEnabled);
        read(value, "actionBudgetJson", project.actionBudgetJson);
        read(value, "analysisCacheVersion", project.analysisCacheVersion);
        read(value, "renderIdentityJson", project.renderIdentityJson);

        if (project.schemaVersion < kMinSupportedSchemaVersion) {
            error.message = "Project schemaVersion is too old and unsupported";
            return std::nullopt;
        }
        if (project.schemaVersion > kCurrentSchemaVersion) {
            error.message = "Unsupported project schemaVersion "
                + std::to_string(project.schemaVersion)
                + " (max supported is "
                + std::to_string(kCurrentSchemaVersion)
                + ")";
            return std::nullopt;
        }
        if (project.id.empty()) {
            error.message = "Project id is required";
            return std::nullopt;
        }

        if (const auto iterator = value.find("masterProcessing"); iterator != value.end())
            readProcessing(*iterator, project.masterProcessing);
        read(value, "masterDynamicEqEnabled", project.masterDynamicEqEnabled);

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
                read(trackValue, "pairId", track.pairId);
                read(trackValue, "parentBusId", track.parentBusId);
                read(trackValue, "channelPosition", track.channelPosition);
                read(trackValue, "dynamicEqEnabled", track.dynamicEqEnabled);
                read(trackValue, "vocalRiderEnabled", track.vocalRiderEnabled);
                read(trackValue, "vocalRiderTargetDb", track.vocalRiderTargetDb);
                read(trackValue, "parallelEnabled", track.parallelEnabled);
                read(trackValue, "parallelWet", track.parallelWet);
                read(trackValue, "parallelThresholdDb", track.parallelThresholdDb);
                read(trackValue, "parallelRatio", track.parallelRatio);
                read(trackValue, "parallelAttackMs", track.parallelAttackMs);
                read(trackValue, "parallelReleaseMs", track.parallelReleaseMs);
                read(trackValue, "parallelMakeupDb", track.parallelMakeupDb);
                read(trackValue, "stereoWidthEnabled", track.stereoWidthEnabled);
                read(trackValue, "sideGainDb", track.sideGainDb);
                read(trackValue, "midGainDb", track.midGainDb);
                read(trackValue, "lowBandMonoHz", track.lowBandMonoHz);
                if (const auto role = roleFromString(trackValue.value("role", "custom")))
                    track.role = *role;
                if (const auto metrics = trackValue.find("metrics"); metrics != trackValue.end())
                    readMetrics(*metrics, track.metrics);
                if (const auto processing = trackValue.find("processing");
                    processing != trackValue.end()) {
                    readProcessing(*processing, track.processing);
                }
                if (const auto dyn = trackValue.find("dynamicEq"); dyn != trackValue.end())
                    readDynamicEq(*dyn, track.dynamicEq);
                if (track.id.empty())
                    track.id = makeProjectId();
                project.tracks.push_back(std::move(track));
            }
        }

        if (const auto iterator = value.find("pairs");
            iterator != value.end() && iterator->is_array()) {
            for (const auto& pairValue : *iterator) {
                PairRecord pair;
                read(pairValue, "id", pair.id);
                read(pairValue, "name", pair.name);
                read(pairValue, "leftTrackId", pair.leftTrackId);
                read(pairValue, "rightTrackId", pair.rightTrackId);
                read(pairValue, "parentBusId", pair.parentBusId);
                read(pairValue, "linkedProcessing", pair.linkedProcessing);
                read(pairValue, "stereoWidthEnabled", pair.stereoWidthEnabled);
                read(pairValue, "sideGainDb", pair.sideGainDb);
                read(pairValue, "midGainDb", pair.midGainDb);
                read(pairValue, "lowBandMonoHz", pair.lowBandMonoHz);
                if (pair.id.empty())
                    pair.id = makeProjectId();
                project.pairs.push_back(std::move(pair));
            }
        }

        if (const auto iterator = value.find("buses");
            iterator != value.end() && iterator->is_array()) {
            for (const auto& busValue : *iterator) {
                BusRecord bus;
                read(busValue, "id", bus.id);
                read(busValue, "name", bus.name);
                read(busValue, "gainDb", bus.gainDb);
                read(busValue, "dynamicEqEnabled", bus.dynamicEqEnabled);
                read(busValue, "parallelEnabled", bus.parallelEnabled);
                read(busValue, "parallelWet", bus.parallelWet);
                read(busValue, "parallelThresholdDb", bus.parallelThresholdDb);
                read(busValue, "parallelRatio", bus.parallelRatio);
                read(busValue, "parallelAttackMs", bus.parallelAttackMs);
                read(busValue, "parallelReleaseMs", bus.parallelReleaseMs);
                read(busValue, "parallelMakeupDb", bus.parallelMakeupDb);
                read(busValue, "parallelDrumJson", bus.parallelDrumJson);
                read(busValue, "stereoWidthEnabled", bus.stereoWidthEnabled);
                read(busValue, "sideGainDb", bus.sideGainDb);
                read(busValue, "midGainDb", bus.midGainDb);
                read(busValue, "lowBandMonoHz", bus.lowBandMonoHz);
                if (const auto role = roleFromString(busValue.value("role", "custom")))
                    bus.role = *role;
                if (busValue.contains("childTrackIds") && busValue["childTrackIds"].is_array())
                    bus.childTrackIds = busValue["childTrackIds"].get<std::vector<std::string>>();
                if (busValue.contains("childPairIds") && busValue["childPairIds"].is_array())
                    bus.childPairIds = busValue["childPairIds"].get<std::vector<std::string>>();
                if (const auto processing = busValue.find("processing"); processing != busValue.end())
                    readProcessing(*processing, bus.processing);
                if (const auto dyn = busValue.find("dynamicEq"); dyn != busValue.end())
                    readDynamicEq(*dyn, bus.dynamicEq);
                if (bus.id.empty())
                    bus.id = makeProjectId();
                project.buses.push_back(std::move(bus));
            }
        }

        if (const auto iterator = value.find("sections");
            iterator != value.end() && iterator->is_array()) {
            for (const auto& sectionValue : *iterator) {
                SectionMarker section;
                read(sectionValue, "id", section.id);
                read(sectionValue, "name", section.name);
                read(sectionValue, "startSeconds", section.startSeconds);
                read(sectionValue, "endSeconds", section.endSeconds);
                if (const auto kind = sectionKindFromString(sectionValue.value("kind", "custom")))
                    section.kind = *kind;
                if (section.id.empty())
                    section.id = makeProjectId();
                project.sections.push_back(std::move(section));
            }
        }

        if (const auto iterator = value.find("mixPassActions");
            iterator != value.end() && iterator->is_array()) {
            for (const auto& actionValue : *iterator) {
                MixPassAction action;
                read(actionValue, "actionId", action.actionId);
                read(actionValue, "actionVersion", action.actionVersion);
                read(actionValue, "problemType", action.problemType);
                read(actionValue, "targetTrackId", action.targetTrackId);
                read(actionValue, "targetPairId", action.targetPairId);
                read(actionValue, "targetBusId", action.targetBusId);
                read(actionValue, "processorId", action.processorId);
                read(actionValue, "parameterId", action.parameterId);
                read(actionValue, "currentValue", action.currentValue);
                read(actionValue, "proposedValue", action.proposedValue);
                read(actionValue, "allowedMin", action.allowedMin);
                read(actionValue, "allowedMax", action.allowedMax);
                read(actionValue, "globalCap", action.globalCap);
                read(actionValue, "roleCap", action.roleCap);
                read(actionValue, "confidenceAdjustedCap", action.confidenceAdjustedCap);
                read(actionValue, "cumulativeCap", action.cumulativeCap);
                read(actionValue, "confidence", action.confidence);
                read(actionValue, "evidenceScore", action.evidenceScore);
                read(actionValue, "evidenceLabel", action.evidenceLabel);
                read(actionValue, "explanation", action.explanation);
                read(actionValue, "sourceMetrics", action.sourceMetrics);
                read(actionValue, "evidence", action.evidence);
                read(actionValue, "decisionTrace", action.decisionTrace);
                read(actionValue, "processingLevel", action.processingLevel);
                read(actionValue, "sectionScope", action.sectionScope);
                read(actionValue, "state", action.state);
                read(actionValue, "origin", action.origin);
                read(actionValue, "priority", action.priority);
                read(actionValue, "orderIndex", action.orderIndex);
                read(actionValue, "conflictGroup", action.conflictGroup);
                read(actionValue, "supersedes", action.supersedes);
                read(actionValue, "prerequisite", action.prerequisite);
                read(actionValue, "hasProposedProcessing", action.hasProposedProcessing);
                read(actionValue, "hasProposedDynamicEq", action.hasProposedDynamicEq);
                read(actionValue, "vocalRiderEnabled", action.vocalRiderEnabled);
                read(actionValue, "vocalRiderTargetDb", action.vocalRiderTargetDb);
                read(actionValue, "hasPrevious", action.hasPrevious);
                read(actionValue, "previousGainDb", action.previousGainDb);
                read(actionValue, "previousDynamicEqEnabled", action.previousDynamicEqEnabled);
                if (const auto processing = actionValue.find("proposedProcessing");
                    processing != actionValue.end()) {
                    readProcessing(*processing, action.proposedProcessing);
                    action.hasProposedProcessing = true;
                }
                if (const auto dyn = actionValue.find("proposedDynamicEq"); dyn != actionValue.end()) {
                    readDynamicEq(*dyn, action.proposedDynamicEq);
                    action.hasProposedDynamicEq = true;
                }
                if (const auto previous = actionValue.find("previousProcessing");
                    previous != actionValue.end()) {
                    readProcessing(*previous, action.previousProcessing);
                }
                if (const auto previousDyn = actionValue.find("previousDynamicEq");
                    previousDyn != actionValue.end()) {
                    readDynamicEq(*previousDyn, action.previousDynamicEq);
                }
                project.mixPassActions.push_back(std::move(action));
            }
        }

        if (const auto iterator = value.find("actions");
            iterator != value.end() && iterator->is_array()) {
            for (const auto& actionValue : *iterator) {
                AppliedAction action;
                read(actionValue, "actionId", action.actionId);
                read(actionValue, "trackId", action.trackId);
                read(actionValue, "targetGainDb", action.targetGainDb);
                read(actionValue, "state", action.state);
                read(actionValue, "hasPrevious", action.hasPrevious);
                read(actionValue, "previousGainDb", action.previousGainDb);
                read(actionValue, "problemType", action.problemType);
                read(actionValue, "processorId", action.processorId);
                read(actionValue, "sectionScope", action.sectionScope);
                if (const auto processing = actionValue.find("processing");
                    processing != actionValue.end()) {
                    readProcessing(*processing, action.processing);
                }
                if (const auto previous = actionValue.find("previousProcessing");
                    previous != actionValue.end()) {
                    readProcessing(*previous, action.previousProcessing);
                }
                project.actions.push_back(std::move(action));
            }
        }

        // Migrated older projects become current schema on next save.
        project.schemaVersion = kCurrentSchemaVersion;
        return project;
    } catch (const json::exception& exception) {
        error.message = std::string("Malformed project JSON: ") + exception.what();
        return std::nullopt;
    }
}

} // namespace mastering::project
