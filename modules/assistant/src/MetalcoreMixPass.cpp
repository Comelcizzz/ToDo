#include "mastering/assistant/MetalcoreMixPass.h"
#include "mastering/assistant/ActionBudget.h"
#include "mastering/assistant/ActionResolver.h"
#include "mastering/assistant/EvidenceModel.h"
#include "mastering/assistant/MetalcoreAnalysis.h"
#include "mastering/assistant/SectionAutomation.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

namespace mastering::assistant {

std::string evidenceLabelFor(double evidenceScore) noexcept
{
    if (evidenceScore < 0.4)
        return "low";
    if (evidenceScore < 0.7)
        return "medium";
    return "high";
}

namespace {

bool isPairMirrorProblem(const std::string& problemType)
{
    return problemType == "guitarLowMidMud"
        || problemType == "guitarHarshness"
        || problemType == "guitarHarshResonance"
        || problemType == "guitarFizz"
        || problemType == "vocalGuitarUnmask"
        || problemType == "snareGuitarUnmask"
        || problemType == "vocalDeEss"
        || problemType == "vocalResonance"
        || problemType == "guitarStereoWidth";
}

void appendEvidenceBreakdown(project::MixPassAction& action, const EvidenceComponents& components)
{
    const auto text = components.breakdown();
    if (text.empty())
        return;
    if (!action.decisionTrace.empty())
        action.decisionTrace += " | ";
    action.decisionTrace += text;
    action.evidenceLabel = evidenceLabelFor(components.score() > 0.0 ? components.score() : action.evidenceScore);
}

void applySafeRangeToAction(
    project::MixPassAction& action,
    const std::string& role,
    double cumulativeRemaining,
    double proposedOffset)
{
    const auto range = deriveSafeRange(
        action.processorId,
        role,
        action.evidenceScore,
        cumulativeRemaining,
        proposedOffset);
    action.proposedValue = action.currentValue + range.finalProposal;
    action.allowedMin = action.currentValue + range.finalMin;
    action.allowedMax = action.currentValue + range.finalMax;
    action.globalCap = range.globalCap;
    action.roleCap = range.roleCap;
    action.confidenceAdjustedCap = range.evidenceCap;
    action.cumulativeCap = range.cumulativeCap;
    if (!action.decisionTrace.empty())
        action.decisionTrace += " | ";
    action.decisionTrace += range.trace;
}

bool vocalActivityAllowsUnmask(const TrackAnalysisExtras* extras, const VocalProfile& profile)
{
    if (extras != nullptr) {
        if (extras->vocalMaskActive)
            return true;
        if (extras->vocalActivityRatio > 0.08)
            return true;
        for (const auto& mask : extras->activityMasks) {
            if (mask.kind == "vocal" && mask.confidence > 0.35)
                return true;
        }
    }
    return profile.activityRatio > 0.08;
}

project::MixPassAction makeAction(
    const std::string& problem,
    const std::string& trackId,
    const std::string& processor,
    const std::string& parameter,
    double current,
    double proposed,
    double evidenceScore,
    const std::string& explanation,
    const std::string& metrics,
    const std::string& evidence = {},
    const std::string& decisionTrace = {},
    const std::string& processingLevel = "track")
{
    project::MixPassAction a;
    a.actionId = project::makeProjectId();
    a.problemType = problem;
    a.targetTrackId = trackId;
    a.processorId = processor;
    a.parameterId = parameter;
    a.currentValue = current;
    a.proposedValue = proposed;
    a.evidenceScore = std::clamp(evidenceScore, 0.0, 1.0);
    a.evidenceLabel = evidenceLabelFor(a.evidenceScore);
    a.confidence = a.evidenceScore; // legacy mirror
    a.globalCap = 24.0;
    a.roleCap = 12.0;
    a.confidenceAdjustedCap = std::clamp(a.roleCap * std::max(0.25, a.evidenceScore), 3.0, a.roleCap);
    a.cumulativeCap = a.confidenceAdjustedCap;
    a.allowedMin = -a.globalCap;
    a.allowedMax = a.globalCap;
    a.explanation = explanation;
    a.sourceMetrics = metrics;
    a.evidence = evidence;
    a.decisionTrace = decisionTrace;
    a.processingLevel = processingLevel;
    a.sectionScope = "full";
    a.state = "pending";
    a.origin = "mixpass";
    a.priority = ActionResolver::rolePriority(a);
    return a;
}

std::string metricsSnippet(const analysis::AudioMetrics& m)
{
    nlohmann::json j {
        {"rmsDbfs", m.rmsDbfs},
        {"crestFactorDb", m.crestFactorDb},
        {"stereoCorrelation", m.stereoCorrelation},
        {"spectrum", {
            {"subDb", m.spectrum.subDb},
            {"bassDb", m.spectrum.bassDb},
            {"lowMidDb", m.spectrum.lowMidDb},
            {"midDb", m.spectrum.midDb},
            {"presenceDb", m.spectrum.presenceDb},
            {"airDb", m.spectrum.airDb}
        }}
    };
    return j.dump();
}

project::BusRecord* findOrCreateBus(
    project::ProjectDocument& project,
    project::TrackRole role,
    const std::string& name)
{
    for (auto& bus : project.buses) {
        if (bus.role == role)
            return &bus;
    }
    project::BusRecord bus;
    bus.id = project::makeProjectId();
    bus.name = name;
    bus.role = role;
    project.buses.push_back(std::move(bus));
    return &project.buses.back();
}

const project::BusRecord* findBusByRole(
    const project::ProjectDocument& project,
    project::TrackRole role)
{
    for (const auto& bus : project.buses) {
        if (bus.role == role)
            return &bus;
    }
    return nullptr;
}

const project::TrackRecord* findTrackById(
    const project::ProjectDocument& project,
    const std::string& id)
{
    for (const auto& track : project.tracks) {
        if (track.id == id)
            return &track;
    }
    return nullptr;
}

const TrackAnalysisExtras* extrasFor(
    const MetalcoreMixPass::AnalysisMap& analysis,
    const std::string& trackId)
{
    const auto it = analysis.find(trackId);
    return it == analysis.end() ? nullptr : &it->second;
}

LowFrequencyProfile synthesizeLowFromMetrics(
    const analysis::AudioMetrics& m,
    bool kickLike,
    bool allowSyntheticFrequencyFallback)
{
    LowFrequencyProfile p;
    const double sub = m.spectrum.subDb;
    const double bass = m.spectrum.bassDb;
    const double energy = std::max(sub, bass);
    if (energy < -48.0) {
        if (allowSyntheticFrequencyFallback) {
            p.dominantLowHz = kickLike ? 65.0 : 70.0;
            p.bodyHz = p.dominantLowHz;
            p.stabilityScore = 0.35;
            p.eventCount = 4;
            p.evidence = "synthetic-frequency-fallback-65/70";
        }
        return p;
    }

    // Spectrum-weighted low centroid — not a fixed metalcore preset.
    const double wSub = std::pow(10.0, std::clamp(sub, -60.0, 0.0) / 20.0);
    const double wBass = std::pow(10.0, std::clamp(bass, -60.0, 0.0) / 20.0);
    double hz = (48.0 * wSub + 100.0 * wBass) / (wSub + wBass + 1.0e-12);
    if (kickLike)
        hz = std::clamp(hz, 40.0, 95.0);
    else
        hz = std::clamp(hz * 0.92, 35.0, 130.0);

    p.dominantLowHz = hz;
    p.bodyHz = hz;
    p.stabilityScore = std::clamp((energy + 45.0) / 40.0 * 0.55, 0.2, 0.55);
    p.eventCount = energy > -25.0 ? 8 : 3;
    p.evidence = "metrics-only-spectrum-weak";
    SpectralPeak peak;
    peak.frequencyHz = hz;
    peak.magnitudeDb = energy;
    peak.prominenceDb = std::max(1.0, energy + 30.0);
    p.fundamentalCandidates.push_back(peak);
    return p;
}

GuitarChannelProfile synthesizeGuitarFromMetrics(const analysis::AudioMetrics& m)
{
    GuitarChannelProfile g;
    g.longTermRmsDb = m.rmsDbfs;
    g.lowMidBuildDb = m.spectrum.lowMidDb;
    g.presenceDb = m.spectrum.presenceDb;
    g.fizzEnergyDb = m.spectrum.airDb;
    g.articulationDb = m.spectrum.midDb;
    g.transientDensityHz = m.transientDensityHz;
    g.spectralTiltDbPerOct = (m.spectrum.airDb - m.spectrum.bassDb) / 6.0;

    const double neighbor = 0.5 * (m.spectrum.bassDb + m.spectrum.midDb);
    g.mudLikely = m.spectrum.lowMidDb > neighbor + 3.0 && m.spectrum.lowMidDb > -22.0;
    if (g.mudLikely) {
        // low-mid centroid proxy when no spectral peak analysis is available
        g.evidence = "mud~250Hz-metrics";
    }

    if (m.spectrum.presenceDb > m.spectrum.midDb + 4.0) {
        g.harshLikely = true;
        g.harshPeakHz = 3'200.0;
        g.harshPeakDb = m.spectrum.presenceDb;
        g.harshQ = 1.8;
    }

    g.fizzLikely = m.spectrum.airDb > m.spectrum.presenceDb + 2.0 && m.spectrum.airDb > -18.0;
    std::ostringstream oss;
    oss << "metrics-guitar;mud=" << g.mudLikely << ";harsh=" << g.harshLikely
        << ";fizz=" << g.fizzLikely;
    g.evidence = oss.str();
    return g;
}

VocalProfile synthesizeVocalFromMetrics(
    const analysis::AudioMetrics& m,
    project::TrackRole role)
{
    VocalProfile v;
    v.crestDb = m.crestFactorDb;
    v.rideTargetDb = role == project::TrackRole::screamVocal ? -16.0 : -18.0;
    v.presenceCentroidHz = 2'500.0;
    v.sibilanceHz = 7'000.0;
    v.sibilanceEnergyDb = m.spectrum.airDb;
    v.resonanceHz = 1'200.0;
    v.resonanceDb = m.spectrum.midDb;
    // Metrics-only activity proxy: audible level implies singing activity.
    v.activityRatio = m.rmsDbfs > -40.0 ? std::clamp((-m.rmsDbfs) / 80.0 + 0.15, 0.05, 0.6) : 0.0;
    if (m.rmsDbfs > -35.0)
        v.activityRatio = std::max(v.activityRatio, 0.12);
    v.needsRide = v.crestDb > 14.0 || (v.activityRatio > 0.15 && m.rmsDbfs < -24.0);
    v.needsDeEss = role == project::TrackRole::cleanVocal && m.spectrum.airDb > -16.0;
    v.needsResonance = m.spectrum.midDb > m.spectrum.presenceDb + 2.0 && m.spectrum.midDb > -22.0;
    v.needsPeakComp = role == project::TrackRole::screamVocal
        ? (v.crestDb > 12.0)
        : (v.crestDb > 16.0);
    std::ostringstream oss;
    oss << "metrics-vocal;role=" << project::roleToString(role)
        << ";activity=" << v.activityRatio
        << ";ride=" << v.needsRide
        << ";peak=" << v.needsPeakComp;
    v.evidence = oss.str();
    return v;
}

SnareProfile synthesizeSnareFromMetrics(const analysis::AudioMetrics& m)
{
    SnareProfile s;
    s.bodyHz = 200.0;
    s.crackHz = 2'400.0;
    s.ringHz = 600.0;
    s.crackEnergyDb = m.spectrum.presenceDb;
    s.eventRateHz = m.transientDensityHz > 0.0 ? m.transientDensityHz : 2.0;
    s.evidence = "metrics-snare-weak";
    return s;
}

TrackAnalysisExtras buildExtrasFromTrack(const project::TrackRecord& track, bool allowSynthetic)
{
    TrackAnalysisExtras extras;
    extras.trackId = track.id;
    extras.role = track.role;
    extras.metrics = track.metrics;

    switch (track.role) {
    case project::TrackRole::kick:
        extras.low = synthesizeLowFromMetrics(track.metrics, true, allowSynthetic);
        break;
    case project::TrackRole::bass:
        extras.low = synthesizeLowFromMetrics(track.metrics, false, allowSynthetic);
        break;
    case project::TrackRole::rhythmGuitar:
    case project::TrackRole::rhythmGuitarLeft:
    case project::TrackRole::rhythmGuitarRight:
    case project::TrackRole::leadGuitar:
    case project::TrackRole::cleanGuitar:
        extras.guitar = synthesizeGuitarFromMetrics(track.metrics);
        break;
    case project::TrackRole::cleanVocal:
    case project::TrackRole::screamVocal:
    case project::TrackRole::backingVocal:
        extras.vocal = synthesizeVocalFromMetrics(track.metrics, track.role);
        break;
    case project::TrackRole::snare:
        extras.snare = synthesizeSnareFromMetrics(track.metrics);
        break;
    default:
        break;
    }
    return extras;
}

LowFrequencyProfile resolveLowProfile(
    const TrackAnalysisExtras* extras,
    const project::TrackRecord& track,
    bool kickLike,
    bool allowSynthetic)
{
    if (extras != nullptr && extras->low.dominantLowHz > 0.0)
        return extras->low;
    if (extras != nullptr && !extras->low.fundamentalCandidates.empty())
        return extras->low;
    return synthesizeLowFromMetrics(track.metrics, kickLike, allowSynthetic);
}

GuitarChannelProfile resolveGuitarProfile(
    const TrackAnalysisExtras* extras,
    const project::TrackRecord& track)
{
    if (extras != nullptr && (!extras->guitar.evidence.empty() || extras->guitar.longTermRmsDb > -119.0))
        return extras->guitar;
    return synthesizeGuitarFromMetrics(track.metrics);
}

VocalProfile resolveVocalProfile(
    const TrackAnalysisExtras* extras,
    const project::TrackRecord& track)
{
    if (extras != nullptr && (!extras->vocal.evidence.empty() || extras->vocal.activityRatio > 0.0))
        return extras->vocal;
    return synthesizeVocalFromMetrics(track.metrics, track.role);
}

SnareProfile resolveSnareProfile(
    const TrackAnalysisExtras* extras,
    const project::TrackRecord& track)
{
    if (extras != nullptr && (!extras->snare.evidence.empty() || extras->snare.crackHz > 0.0))
        return extras->snare;
    return synthesizeSnareFromMetrics(track.metrics);
}

const ReferenceProfile* findReferenceRole(
    const std::vector<ReferenceProfile>& references,
    const std::string& role)
{
    for (const auto& ref : references) {
        if (ref.role == role)
            return &ref;
    }
    return nullptr;
}

void appendVocalActions(
    std::vector<project::MixPassAction>& actions,
    const project::TrackRecord& vocal,
    const VocalProfile& profile,
    bool scream)
{
    if (profile.needsRide) {
        auto ride = makeAction(
            "vocalRiding",
            vocal.id,
            "vocalRider",
            "targetDb",
            vocal.vocalRiderTargetDb,
            profile.rideTargetDb,
            std::clamp(0.45 + profile.activityRatio, 0.4, 0.9),
            scream
                ? "Scream vocal level trajectory via vocal rider (not a compressor). "
                  "Target from vocal profile; keep peak control separate."
                : "Clean vocal level trajectory via vocal rider (not a compressor). "
                  "Target from vocal profile for sustained intelligibility.",
            metricsSnippet(vocal.metrics),
            profile.evidence,
            scream ? "scream:needsRide→vocalRider" : "clean:needsRide→vocalRider",
            "track");
        ride.vocalRiderEnabled = true;
        ride.vocalRiderTargetDb = profile.rideTargetDb;
        ride.allowedMin = -24.0;
        ride.allowedMax = -6.0;
        actions.push_back(std::move(ride));

        // Serial compressor for level control when ride alone is insufficient.
        if (profile.crestDb > 15.0 || profile.activityRatio > 0.25) {
            auto comp = makeAction(
                scream ? "screamVocalCompress" : "cleanVocalCompress",
                vocal.id,
                "compressor",
                "thresholdDb",
                vocal.processing.compressor.thresholdDb,
                scream ? -12.0 : -14.0,
                std::clamp(profile.crestDb / 24.0, 0.4, 0.85),
                "Serial compressor for vocal level control alongside rider.",
                metricsSnippet(vocal.metrics),
                profile.evidence,
                "needsRide+crest→comp1",
                "track");
            comp.hasProposedProcessing = true;
            comp.proposedProcessing = vocal.processing;
            comp.proposedProcessing.compressor.thresholdDb = scream ? -12.0 : -14.0;
            comp.proposedProcessing.compressor.ratio = scream ? 3.5 : 3.0;
            comp.proposedProcessing.compressor.attackMs = scream ? 12.0 : 18.0;
            comp.proposedProcessing.compressor.releaseMs = scream ? 70.0 : 90.0;
            comp.proposedProcessing.compressor.makeupDb = scream ? 2.5 : 2.0;
            comp.allowedMin = -24.0;
            comp.allowedMax = 0.0;
            actions.push_back(std::move(comp));
        }
    }

    if (profile.needsPeakComp) {
        auto peak = makeAction(
            scream ? "screamVocalPeakComp" : "vocalPeakComp",
            vocal.id,
            "compressorPeak",
            "thresholdDb",
            vocal.processing.compressor.thresholdDb,
            scream ? -8.0 : -10.0,
            std::clamp(profile.crestDb / 20.0, 0.45, 0.9),
            scream
                ? "Scream peaks need fast peak compression (more likely than clean)."
                : "Vocal crest indicates peak compression for transient control.",
            metricsSnippet(vocal.metrics),
            profile.evidence,
            "needsPeakComp→compressorPeak",
            "track");
        peak.hasProposedProcessing = true;
        peak.proposedProcessing = vocal.processing;
        peak.proposedProcessing.compressor.thresholdDb = scream ? -8.0 : -10.0;
        peak.proposedProcessing.compressor.ratio = scream ? 5.0 : 4.0;
        peak.proposedProcessing.compressor.attackMs = 2.0;
        peak.proposedProcessing.compressor.releaseMs = 40.0;
        peak.proposedProcessing.compressor.makeupDb = 1.0;
        peak.allowedMin = -24.0;
        peak.allowedMax = 0.0;
        actions.push_back(std::move(peak));
    }

    if (profile.needsDeEss && profile.sibilanceHz > 0.0) {
        const double evidence = std::clamp((profile.sibilanceEnergyDb + 30.0) / 30.0, 0.4, 0.9);
        auto deess = makeAction(
            "vocalDeEss",
            vocal.id,
            "dynamicEq",
            "maxCutDb",
            0.0,
            std::clamp(3.0 + evidence * 3.0, 2.0, 7.0),
            evidence,
            "De-ess DynEQ at detected sibilance frequency.",
            metricsSnippet(vocal.metrics),
            profile.evidence,
            "needsDeEss@" + std::to_string(profile.sibilanceHz),
            "track");
        deess.hasProposedDynamicEq = true;
        deess.proposedDynamicEq.bandCount = 1;
        deess.proposedDynamicEq.bands[0].bandId = 1;
        deess.proposedDynamicEq.bands[0].enabled = true;
        deess.proposedDynamicEq.bands[0].filterType = dsp::DynamicEqFilterType::bell;
        deess.proposedDynamicEq.bands[0].frequencyHz = profile.sibilanceHz;
        deess.proposedDynamicEq.bands[0].q = 2.5;
        deess.proposedDynamicEq.bands[0].thresholdDb = -28.0;
        deess.proposedDynamicEq.bands[0].ratio = 3.0;
        deess.proposedDynamicEq.bands[0].maxCutDb = deess.proposedValue;
        deess.proposedDynamicEq.bands[0].attackMs = 5.0;
        deess.proposedDynamicEq.bands[0].releaseMs = 60.0;
        deess.proposedDynamicEq.bands[0].detectorBandPass = true;
        deess.proposedDynamicEq.bands[0].detectorFrequencyHz = profile.sibilanceHz;
        deess.proposedDynamicEq.bands[0].detectorQ = 2.0;
        deess.proposedDynamicEq.bands[0].detectorSource = dsp::DetectorSource::internal;
        deess.allowedMin = 0.0;
        deess.allowedMax = 9.0;
        actions.push_back(std::move(deess));
    }

    if (profile.needsResonance && profile.resonanceHz > 0.0) {
        const double evidence = std::clamp((profile.resonanceDb + 28.0) / 28.0, 0.4, 0.85);
        auto res = makeAction(
            "vocalResonance",
            vocal.id,
            "dynamicEq",
            "maxCutDb",
            0.0,
            std::clamp(2.5 + evidence * 2.5, 2.0, 6.0),
            evidence,
            "Resonance DynEQ at detected vocal resonance frequency.",
            metricsSnippet(vocal.metrics),
            profile.evidence,
            "needsResonance@" + std::to_string(profile.resonanceHz),
            "track");
        res.hasProposedDynamicEq = true;
        res.proposedDynamicEq.bandCount = 1;
        res.proposedDynamicEq.bands[0].bandId = 1;
        res.proposedDynamicEq.bands[0].enabled = true;
        res.proposedDynamicEq.bands[0].filterType = dsp::DynamicEqFilterType::bell;
        res.proposedDynamicEq.bands[0].frequencyHz = profile.resonanceHz;
        res.proposedDynamicEq.bands[0].q = 4.0;
        res.proposedDynamicEq.bands[0].thresholdDb = -26.0;
        res.proposedDynamicEq.bands[0].ratio = 3.5;
        res.proposedDynamicEq.bands[0].maxCutDb = res.proposedValue;
        res.proposedDynamicEq.bands[0].attackMs = 8.0;
        res.proposedDynamicEq.bands[0].releaseMs = 100.0;
        res.proposedDynamicEq.bands[0].detectorBandPass = true;
        res.proposedDynamicEq.bands[0].detectorFrequencyHz = profile.resonanceHz;
        res.proposedDynamicEq.bands[0].detectorSource = dsp::DetectorSource::internal;
        res.allowedMin = 0.0;
        res.allowedMax = 8.0;
        actions.push_back(std::move(res));
    }

    // Scream: optional bounded saturation when harsh crest + energy.
    if (scream && profile.crestDb > 11.0 && vocal.metrics.spectrum.presenceDb > -20.0) {
        const double sat = std::clamp(0.08 + (profile.crestDb - 11.0) * 0.02, 0.05, 0.22);
        auto satAction = makeAction(
            "screamVocalSaturation",
            vocal.id,
            "saturation",
            "amount",
            vocal.processing.saturation,
            sat,
            0.55,
            "Optional bounded saturation for scream density (kept modest).",
            metricsSnippet(vocal.metrics),
            profile.evidence,
            "scream:optionalSat",
            "track");
        satAction.hasProposedProcessing = true;
        satAction.proposedProcessing = vocal.processing;
        satAction.proposedProcessing.saturation = sat;
        satAction.allowedMin = 0.0;
        satAction.allowedMax = 0.3;
        actions.push_back(std::move(satAction));
    }
}

} // namespace

const project::TrackRecord* MetalcoreMixPass::findTrack(
    const project::ProjectDocument& project,
    project::TrackRole role)
{
    for (const auto& track : project.tracks) {
        if (track.role == role)
            return &track;
    }
    return nullptr;
}

project::TrackRecord* MetalcoreMixPass::findTrackMutable(
    project::ProjectDocument& project,
    const std::string& id)
{
    for (auto& track : project.tracks) {
        if (track.id == id)
            return &track;
    }
    return nullptr;
}

void MetalcoreMixPass::ensureHierarchy(project::ProjectDocument& project)
{
    findOrCreateBus(project, project::TrackRole::guitarBus, "Guitar Bus");
    findOrCreateBus(project, project::TrackRole::drumBus, "Drum Bus");
    findOrCreateBus(project, project::TrackRole::bassBus, "Bass Bus");
    findOrCreateBus(project, project::TrackRole::vocalBus, "Vocal Bus");
    findOrCreateBus(project, project::TrackRole::musicBus, "Music Bus");

    auto busByRole = [&](project::TrackRole role) -> project::BusRecord* {
        for (auto& bus : project.buses) {
            if (bus.role == role)
                return &bus;
        }
        return nullptr;
    };
    auto* guitarBus = busByRole(project::TrackRole::guitarBus);
    auto* drumBus = busByRole(project::TrackRole::drumBus);
    auto* bassBus = busByRole(project::TrackRole::bassBus);
    auto* vocalBus = busByRole(project::TrackRole::vocalBus);
    if (guitarBus == nullptr || drumBus == nullptr || bassBus == nullptr || vocalBus == nullptr)
        return;

    // Collect track indices first — avoid dangling TrackRecord* across mutations.
    std::optional<std::size_t> leftIndex;
    std::optional<std::size_t> rightIndex;
    for (std::size_t i = 0; i < project.tracks.size(); ++i) {
        auto& track = project.tracks[i];
        if (track.role == project::TrackRole::rhythmGuitarLeft
            || (track.role == project::TrackRole::rhythmGuitar
                && (track.channelPosition == "L" || track.name.find("Left") != std::string::npos
                    || track.name.find("_L") != std::string::npos))) {
            track.role = project::TrackRole::rhythmGuitarLeft;
            track.channelPosition = "L";
            track.pan = -1.0;
            leftIndex = i;
        } else if (track.role == project::TrackRole::rhythmGuitarRight
            || (track.role == project::TrackRole::rhythmGuitar
                && (track.channelPosition == "R" || track.name.find("Right") != std::string::npos
                    || track.name.find("_R") != std::string::npos))) {
            track.role = project::TrackRole::rhythmGuitarRight;
            track.channelPosition = "R";
            track.pan = 1.0;
            rightIndex = i;
        } else if (track.role == project::TrackRole::kick || track.role == project::TrackRole::snare
            || track.role == project::TrackRole::drums || track.role == project::TrackRole::toms
            || track.role == project::TrackRole::cymbals) {
            track.parentBusId = drumBus->id;
            if (std::find(drumBus->childTrackIds.begin(), drumBus->childTrackIds.end(), track.id)
                == drumBus->childTrackIds.end())
                drumBus->childTrackIds.push_back(track.id);
        } else if (track.role == project::TrackRole::bass) {
            track.parentBusId = bassBus->id;
            if (std::find(bassBus->childTrackIds.begin(), bassBus->childTrackIds.end(), track.id)
                == bassBus->childTrackIds.end())
                bassBus->childTrackIds.push_back(track.id);
        } else if (track.role == project::TrackRole::cleanVocal
            || track.role == project::TrackRole::screamVocal
            || track.role == project::TrackRole::backingVocal) {
            track.parentBusId = vocalBus->id;
            if (std::find(vocalBus->childTrackIds.begin(), vocalBus->childTrackIds.end(), track.id)
                == vocalBus->childTrackIds.end())
                vocalBus->childTrackIds.push_back(track.id);
        }
    }

    if (leftIndex.has_value() && rightIndex.has_value()) {
        auto& left = project.tracks[*leftIndex];
        auto& right = project.tracks[*rightIndex];
        project::PairRecord* existing = nullptr;
        for (auto& pair : project.pairs) {
            if ((pair.leftTrackId == left.id && pair.rightTrackId == right.id)
                || (pair.leftTrackId == right.id && pair.rightTrackId == left.id)) {
                existing = &pair;
                break;
            }
        }
        if (existing == nullptr) {
            project::PairRecord pair;
            pair.id = project::makeProjectId();
            pair.name = "Rhythm Guitar Pair";
            pair.leftTrackId = left.id;
            pair.rightTrackId = right.id;
            pair.parentBusId = guitarBus->id;
            pair.linkedProcessing = true;
            project.pairs.push_back(pair);
            existing = &project.pairs.back();
        }
        left.pairId = existing->id;
        right.pairId = existing->id;
        left.parentBusId = guitarBus->id;
        right.parentBusId = guitarBus->id;
        if (std::find(guitarBus->childPairIds.begin(), guitarBus->childPairIds.end(), existing->id)
            == guitarBus->childPairIds.end())
            guitarBus->childPairIds.push_back(existing->id);
    }
}

std::vector<project::MixPassAction> MetalcoreMixPass::generateActions(
    const project::ProjectDocument& project,
    const AnalysisMap& analysis,
    const std::vector<ReferenceProfile>& references,
    const Options& options) const
{
    std::vector<project::MixPassAction> actions;
    const auto* kick = findTrack(project, project::TrackRole::kick);
    const auto* bass = findTrack(project, project::TrackRole::bass);
    const auto* snare = findTrack(project, project::TrackRole::snare);
    const auto* gL = findTrack(project, project::TrackRole::rhythmGuitarLeft);
    const auto* gR = findTrack(project, project::TrackRole::rhythmGuitarRight);
    const auto* clean = findTrack(project, project::TrackRole::cleanVocal);
    const auto* scream = findTrack(project, project::TrackRole::screamVocal);
    const auto* drumBusTrack = findTrack(project, project::TrackRole::drumBus);
    const auto* drumBus = findBusByRole(project, project::TrackRole::drumBus);

    // --- Kick / bass ---
    if (options.enableKickBass && kick != nullptr && bass != nullptr) {
        const auto* kickExtras = extrasFor(analysis, kick->id);
        const auto* bassExtras = extrasFor(analysis, bass->id);
        auto kickLow = resolveLowProfile(kickExtras, *kick, true, options.allowSyntheticFrequencyFallback);
        auto bassLow = resolveLowProfile(bassExtras, *bass, false, options.allowSyntheticFrequencyFallback);

        const bool kickMissingPeaks = kickLow.fundamentalCandidates.empty() && kickLow.dominantLowHz <= 0.0;
        const bool bassMissingPeaks = bassLow.fundamentalCandidates.empty() && bassLow.dominantLowHz <= 0.0;
        if (options.allowSyntheticFrequencyFallback && (kickMissingPeaks || bassMissingPeaks)) {
            if (kickMissingPeaks) {
                kickLow.dominantLowHz = 65.0;
                kickLow.bodyHz = 65.0;
                kickLow.stabilityScore = std::max(kickLow.stabilityScore, 0.35);
                kickLow.eventCount = std::max(kickLow.eventCount, 4);
                kickLow.evidence = "synthetic-frequency-fallback-detector65";
            }
            if (bassMissingPeaks) {
                bassLow.dominantLowHz = 70.0;
                bassLow.bodyHz = 70.0;
                bassLow.stabilityScore = std::max(bassLow.stabilityScore, 0.35);
                bassLow.eventCount = std::max(bassLow.eventCount, 4);
                bassLow.evidence = "synthetic-frequency-fallback-target70";
            }
        }

        const auto decision = MetalcoreAnalysis::decideSubOwnership(
            kickLow, bassLow, kick->metrics, bass->metrics);

        const bool emitMasking =
            (decision.ownership == SubOwnership::sharedButSeparated
                || (decision.ownership == SubOwnership::bassOwnsSub && decision.overlapHz > 0.0))
            && decision.evidenceScore >= 0.4;

        if (emitMasking) {
            const double detectorHz = decision.kickDominantHz > 0.0
                ? decision.kickDominantHz
                : (kickLow.dominantLowHz > 0.0 ? kickLow.dominantLowHz : decision.overlapHz);
            const double targetHz = decision.overlapHz > 0.0
                ? decision.overlapHz
                : (bassLow.dominantLowHz > 0.0 ? bassLow.dominantLowHz : detectorHz);
            const double releaseMs = std::clamp(
                60'000.0 / std::max(60.0, options.bpm) * 0.35, 40.0, 180.0);
            const double cutCap = std::clamp(
                12.0 * std::max(0.25, decision.evidenceScore), 3.0, 12.0);
            const double maxCut = std::clamp(4.0 + decision.evidenceScore * 6.0, 4.0, cutCap);

            auto a = makeAction(
                "kickBassMasking",
                bass->id,
                "dynamicEq",
                "maxCutDb",
                bass->dynamicEq.bands[0].maxCutDb,
                maxCut,
                decision.evidenceScore,
                "Kick and bass compete in the same sub/bass region. Frequency-dependent "
                "kick→bass Dynamic EQ uses detected kick dominant as detector and overlap "
                "region as target so upper bass articulation is retained.",
                metricsSnippet(bass->metrics),
                kickLow.evidence + "|" + bassLow.evidence,
                decision.decisionTrace,
                "track");
            applySafeRangeToAction(a, "bass", cutCap, maxCut - a.currentValue);
            a.proposedValue = std::clamp(a.proposedValue, 0.0, cutCap);
            a.hasProposedDynamicEq = true;
            a.proposedDynamicEq = bass->dynamicEq;
            a.proposedDynamicEq.bandCount = 1;
            a.proposedDynamicEq.bands[0].bandId = 1;
            a.proposedDynamicEq.bands[0].enabled = true;
            a.proposedDynamicEq.bands[0].filterType = dsp::DynamicEqFilterType::bell;
            a.proposedDynamicEq.bands[0].frequencyHz = targetHz;
            a.proposedDynamicEq.bands[0].q = 2.8;
            a.proposedDynamicEq.bands[0].staticGainDb = 0.0;
            a.proposedDynamicEq.bands[0].thresholdDb = -22.0;
            a.proposedDynamicEq.bands[0].ratio = 4.0;
            a.proposedDynamicEq.bands[0].maxCutDb = a.proposedValue;
            a.proposedDynamicEq.bands[0].attackMs = std::clamp(60'000.0 / std::max(60.0, options.bpm) * 0.02, 3.0, 12.0);
            a.proposedDynamicEq.bands[0].releaseMs = releaseMs;
            a.proposedDynamicEq.bands[0].detectorBandPass = true;
            a.proposedDynamicEq.bands[0].detectorFrequencyHz = detectorHz;
            a.proposedDynamicEq.bands[0].detectorQ = 3.0;
            a.proposedDynamicEq.bands[0].detectorSource = dsp::DetectorSource::external;
            a.proposedDynamicEq.bands[0].stereoMode = dsp::DynamicEqStereoMode::linked;
            a.proposedDynamicEq.bands[0].sidechainSourceId = kick->id;
            a.allowedMin = 0.0;
            a.allowedMax = cutCap;
            {
                EvidenceComponents ev;
                ev.roleCertainty = 0.9;
                ev.spectralProminence = decision.evidenceScore;
                ev.detectorAgreement = decision.evidenceScore;
                ev.eventConsistency = kickLow.stabilityScore;
                ev.eventCount = std::clamp(kickLow.eventCount / 16.0, 0.0, 1.0);
                appendEvidenceBreakdown(a, ev);
            }
            actions.push_back(std::move(a));

            // Section-scoped DynEQ offsets when streaming section summaries differ.
            const auto* kickExtrasFull = kickExtras;
            const auto* bassExtrasFull = bassExtras;
            if (kickExtrasFull != nullptr && !kickExtrasFull->sectionSummaries.empty()
                && project.sections.size() > 1) {
                double globalFund = kickLow.dominantLowHz;
                for (const auto& summary : kickExtrasFull->sectionSummaries) {
                    if (summary.sectionId.empty() || summary.medianFundamentalHz <= 0.0)
                        continue;
                    const double deltaHz = std::abs(summary.medianFundamentalHz - globalFund);
                    if (deltaHz < 4.0)
                        continue;
                    const double sectionCut = std::clamp(
                        maxCut * (0.85 + 0.05 * (deltaHz / 10.0)), 2.0, cutCap);
                    auto sectionAction = makeAction(
                        "kickBassMaskingSection",
                        bass->id,
                        "dynamicEq",
                        "dynMaxCutDb",
                        maxCut,
                        sectionCut,
                        std::clamp(decision.evidenceScore * 0.9, 0.4, 0.85),
                        "Section-specific kick/bass DynEQ max-cut offset where section "
                        "fundamental differs from global analysis.",
                        metricsSnippet(bass->metrics),
                        "sectionFund=" + std::to_string(summary.medianFundamentalHz),
                        "section=" + summary.sectionId + ";deltaHz=" + std::to_string(deltaHz),
                        "track");
                    sectionAction.sectionScope = summary.sectionId;
                    applySafeRangeToAction(
                        sectionAction,
                        "bass",
                        3.0,
                        sectionCut - maxCut);
                    sectionAction.allowedMin = -3.0;
                    sectionAction.allowedMax = 3.0;
                    actions.push_back(std::move(sectionAction));
                }
            }
            (void) bassExtrasFull;
        }

        const bool stableOverlap = kickLow.stabilityScore >= 0.35 && bassLow.stabilityScore >= 0.35
            && decision.overlapHz > 0.0;
        if (stableOverlap && decision.evidenceScore >= 0.55) {
            const double shelfHz = bassLow.dominantLowHz > 0.0 ? bassLow.dominantLowHz : decision.overlapHz;
            auto kickEq = makeAction(
                "kickBassComplementaryEq",
                kick->id,
                "staticEq",
                "lowShelfGainDb",
                kick->processing.equalizer.lowShelfGainDb,
                -1.0,
                decision.evidenceScore,
                "Complementary EQ: small CUT on kick low-shelf near bass region. "
                "Presence boost is optional only at high evidence.",
                metricsSnippet(kick->metrics),
                kickLow.evidence,
                decision.decisionTrace + ";complementary=cutPreferred",
                "track");
            kickEq.hasProposedProcessing = true;
            kickEq.proposedProcessing = kick->processing;
            kickEq.proposedProcessing.equalizer.lowShelfHz = std::clamp(shelfHz, 40.0, 120.0);
            kickEq.proposedProcessing.equalizer.lowShelfGainDb = -1.0;
            if (decision.evidenceScore >= 0.75) {
                kickEq.proposedProcessing.equalizer.presenceHz = 4'000.0;
                kickEq.proposedProcessing.equalizer.presenceGainDb = 1.0;
                kickEq.explanation += " Optional presence lift included (evidence≥0.75).";
                kickEq.parameterId = "presenceGainDb";
                kickEq.proposedValue = 1.0;
            }
            kickEq.allowedMin = -6.0;
            kickEq.allowedMax = 3.0;
            actions.push_back(std::move(kickEq));
        }
    }

    // --- Guitar L/R ---
    if (options.enableGuitarBalance && (gL != nullptr || gR != nullptr)) {
        GuitarChannelProfile leftProfile {};
        GuitarChannelProfile rightProfile {};
        if (gL != nullptr)
            leftProfile = resolveGuitarProfile(extrasFor(analysis, gL->id), *gL);
        if (gR != nullptr)
            rightProfile = resolveGuitarProfile(extrasFor(analysis, gR->id), *gR);

        if (gL != nullptr && gR != nullptr) {
            const double delta = leftProfile.longTermRmsDb - rightProfile.longTermRmsDb;
            const double balanceEvidence = std::clamp(
                0.55 + std::abs(delta) / 10.0
                    + (leftProfile.longTermRmsDb > -40.0 && rightProfile.longTermRmsDb > -40.0 ? 0.15 : 0.0),
                0.0,
                0.95);
            if (std::abs(delta) > 1.5 && balanceEvidence >= 0.7) {
                const auto* quieter = delta > 0.0 ? gR : gL;
                const double quieterRms = delta > 0.0 ? rightProfile.longTermRmsDb : leftProfile.longTermRmsDb;
                const std::string level = delta > 0.0 ? "track-right" : "track-left";
                auto a = makeAction(
                    "guitarLrBalance",
                    quieter->id,
                    "gain",
                    "gainDb",
                    quieter->gainDb,
                    quieter->gainDb + std::clamp(std::abs(delta) * 0.5, 0.5, 3.0),
                    balanceEvidence,
                    "Rhythm Guitar L/R section level imbalance. Adjust the quieter side only; "
                    "do not time-align or phase-align independent doubles.",
                    metricsSnippet(quieter->metrics),
                    "rmsDelta=" + std::to_string(delta) + ";quieterRms=" + std::to_string(quieterRms),
                    "guitarBalance:|Δrms|>1.5 && evidence high → quieter side only",
                    level);
                a.allowedMin = -12.0;
                a.allowedMax = 12.0;
                actions.push_back(std::move(a));
            }
        }

        const bool mudL = gL != nullptr && leftProfile.mudLikely;
        const bool mudR = gR != nullptr && rightProfile.mudLikely;
        if (mudL || mudR) {
            const auto* target = mudL ? gL : gR;
            const auto& profile = mudL ? leftProfile : rightProfile;
            double mudHz = 250.0;
            // Prefer analysis mud peak proxy encoded in evidence / harsh unused — use lowMid centroid.
            if (profile.lowMidBuildDb > -40.0)
                mudHz = 250.0;
            const bool both = mudL && mudR;
            const double evidence = both ? 0.72 : 0.6;
            auto a = makeAction(
                "guitarLowMidMud",
                target->id,
                "staticEq",
                "lowShelfGainDb",
                target->processing.equalizer.lowShelfGainDb,
                -2.0,
                evidence,
                "Rhythm guitars show elevated low-mid density (palm-mute bloom / mud). "
                "Static EQ cut at mud peak / low-mid centroid.",
                metricsSnippet(target->metrics),
                profile.evidence,
                both ? "mudBoth→pair" : "mudOne→track",
                both ? "pair" : "track");
            if (both && !target->pairId.empty())
                a.targetPairId = target->pairId;
            a.hasProposedProcessing = true;
            a.proposedProcessing = target->processing;
            a.proposedProcessing.equalizer.lowShelfHz = mudHz;
            a.proposedProcessing.equalizer.lowShelfGainDb = -2.0;
            a.allowedMin = -6.0;
            a.allowedMax = 0.0;
            actions.push_back(std::move(a));
        }

        const bool harshL = gL != nullptr && leftProfile.harshLikely;
        const bool harshR = gR != nullptr && rightProfile.harshLikely;
        if (harshL || harshR) {
            // Right-only stays on Right (not bus). Prefer narrower DynEQ when Q known.
            const bool rightOnly = harshR && !harshL;
            const auto* target = rightOnly ? gR : (harshL ? gL : gR);
            const auto& profile = rightOnly ? rightProfile : (harshL ? leftProfile : rightProfile);
            const double hz = profile.harshPeakHz > 0.0 ? profile.harshPeakHz : 3'200.0;
            const double q = profile.harshQ > 0.0 ? profile.harshQ : 2.0;
            const double evidence = std::clamp((profile.harshPeakDb + 25.0) / 25.0, 0.45, 0.85);
            const std::string level = rightOnly
                ? "track-right"
                : (harshL && !harshR ? "track-left" : processingLevelFor("guitarHarshness", harshL && !harshR, rightOnly, harshL && harshR, false));

            if (q >= 2.5 || rightOnly) {
                auto a = makeAction(
                    "guitarHarshResonance",
                    target->id,
                    "dynamicEq",
                    "maxCutDb",
                    0.0,
                    std::clamp(2.0 + evidence * 3.0, 2.0, 5.0),
                    evidence,
                    "Narrow dynamic cut at guitar harsh resonance peak.",
                    metricsSnippet(target->metrics),
                    profile.evidence,
                    "harshPeak=" + std::to_string(hz) + ";Q=" + std::to_string(q),
                    level);
                a.hasProposedDynamicEq = true;
                a.proposedDynamicEq.bandCount = 1;
                a.proposedDynamicEq.bands[0].bandId = 1;
                a.proposedDynamicEq.bands[0].enabled = true;
                a.proposedDynamicEq.bands[0].filterType = dsp::DynamicEqFilterType::bell;
                a.proposedDynamicEq.bands[0].frequencyHz = hz;
                a.proposedDynamicEq.bands[0].q = q;
                a.proposedDynamicEq.bands[0].thresholdDb = -24.0;
                a.proposedDynamicEq.bands[0].ratio = 3.0;
                a.proposedDynamicEq.bands[0].maxCutDb = a.proposedValue;
                a.proposedDynamicEq.bands[0].attackMs = 6.0;
                a.proposedDynamicEq.bands[0].releaseMs = 90.0;
                a.proposedDynamicEq.bands[0].detectorBandPass = true;
                a.proposedDynamicEq.bands[0].detectorFrequencyHz = hz;
                a.proposedDynamicEq.bands[0].detectorQ = q;
                a.proposedDynamicEq.bands[0].detectorSource = dsp::DetectorSource::internal;
                if (!rightOnly && harshL && harshR && !target->pairId.empty())
                    a.targetPairId = target->pairId;
                a.allowedMin = 0.0;
                a.allowedMax = 8.0;
                actions.push_back(std::move(a));
            } else {
                auto a = makeAction(
                    "guitarHarshness",
                    target->id,
                    "staticEq",
                    "presenceGainDb",
                    target->processing.equalizer.presenceGainDb,
                    -1.5,
                    evidence,
                    "Static narrow-ish presence cut for guitar harshness.",
                    metricsSnippet(target->metrics),
                    profile.evidence,
                    "harshStatic@" + std::to_string(hz),
                    level);
                a.hasProposedProcessing = true;
                a.proposedProcessing = target->processing;
                a.proposedProcessing.equalizer.presenceHz = hz;
                a.proposedProcessing.equalizer.presenceGainDb = -1.5;
                if (!rightOnly && harshL && harshR && !target->pairId.empty())
                    a.targetPairId = target->pairId;
                a.allowedMin = -6.0;
                a.allowedMax = 0.0;
                actions.push_back(std::move(a));
            }
        }

        const bool fizzL = gL != nullptr && leftProfile.fizzLikely;
        const bool fizzR = gR != nullptr && rightProfile.fizzLikely;
        if (fizzL || fizzR) {
            const bool both = fizzL && fizzR;
            const auto* target = fizzR ? gR : gL;
            const auto& profile = fizzR ? rightProfile : leftProfile;
            auto a = makeAction(
                "guitarFizz",
                target->id,
                "staticEq",
                "highShelfGainDb",
                target->processing.equalizer.highShelfGainDb,
                -1.5,
                0.65,
                "Guitar fizz: high-shelf cut near ~10 kHz (LP-ish top control).",
                metricsSnippet(target->metrics),
                profile.evidence,
                "fizzLikely→highShelf@10k",
                both ? "pair" : (fizzL && !fizzR ? "track-left" : "track-right"));
            if (both && !target->pairId.empty())
                a.targetPairId = target->pairId;
            a.hasProposedProcessing = true;
            a.proposedProcessing = target->processing;
            a.proposedProcessing.equalizer.highShelfHz = 10'000.0;
            a.proposedProcessing.equalizer.highShelfGainDb = -1.5;
            a.allowedMin = -6.0;
            a.allowedMax = 0.0;
            actions.push_back(std::move(a));
        }
    }

    // --- Vocals (clean vs scream separate) ---
    if (clean != nullptr) {
        appendVocalActions(
            actions,
            *clean,
            resolveVocalProfile(extrasFor(analysis, clean->id), *clean),
            false);
    }
    if (scream != nullptr) {
        appendVocalActions(
            actions,
            *scream,
            resolveVocalProfile(extrasFor(analysis, scream->id), *scream),
            true);
    }

    // --- Vocal / guitar unmask ---
    const auto* vocalForUnmask = clean != nullptr ? clean : scream;
    if (options.enableVocalUnmask && vocalForUnmask != nullptr && (gL != nullptr || gR != nullptr)) {
        const auto* vocalExtras = extrasFor(analysis, vocalForUnmask->id);
        const auto vocalProfile = resolveVocalProfile(vocalExtras, *vocalForUnmask);
        const bool active = vocalActivityAllowsUnmask(vocalExtras, vocalProfile)
            || vocalForUnmask->metrics.rmsDbfs > -35.0;
        if (active) {
            GuitarChannelProfile leftProfile {};
            GuitarChannelProfile rightProfile {};
            if (gL != nullptr)
                leftProfile = resolveGuitarProfile(extrasFor(analysis, gL->id), *gL);
            if (gR != nullptr)
                rightProfile = resolveGuitarProfile(extrasFor(analysis, gR->id), *gR);

            const double presenceHz = vocalProfile.presenceCentroidHz > 0.0
                ? vocalProfile.presenceCentroidHz
                : 2'800.0;
            const double vocalPresence = vocalForUnmask->metrics.spectrum.presenceDb;
            const double overlapL = gL != nullptr
                ? (leftProfile.presenceDb - vocalPresence)
                : -1.0e9;
            const double overlapR = gR != nullptr
                ? (rightProfile.presenceDb - vocalPresence)
                : -1.0e9;

            const project::TrackRecord* target = nullptr;
            std::string level = "track";
            bool usePair = false;
            if (gL != nullptr && gR != nullptr) {
                const double diff = std::abs(overlapL - overlapR);
                if (diff < 1.5) {
                    target = gL;
                    level = "pair";
                    usePair = true;
                } else if (overlapL > overlapR) {
                    target = gL;
                    level = "track-left";
                } else {
                    target = gR;
                    level = "track-right";
                }
            } else {
                target = gL != nullptr ? gL : gR;
                level = gL != nullptr ? "track-left" : "track-right";
            }

            const double guitarPresence = target == gL ? leftProfile.presenceDb : rightProfile.presenceDb;
            if (vocalPresence - guitarPresence < 3.0 && target != nullptr) {
                const double evidence = std::clamp(
                    0.5 + vocalProfile.activityRatio + (3.0 - (vocalPresence - guitarPresence)) * 0.05,
                    0.45,
                    0.9);
                auto a = makeAction(
                    "vocalGuitarUnmask",
                    target->id,
                    "dynamicEq",
                    "maxCutDb",
                    0.0,
                    std::clamp(3.0 + evidence * 3.0, 3.0, 7.0),
                    evidence,
                    "Vocal presence is masked by dense guitars. Frequency-dependent Dynamic EQ on "
                    "guitars keyed from vocal presence centroid with bounded cut.",
                    metricsSnippet(vocalForUnmask->metrics),
                    vocalProfile.evidence,
                    "activity=" + std::to_string(vocalProfile.activityRatio)
                        + ";presenceHz=" + std::to_string(presenceHz)
                        + ";level=" + level,
                    level);
                if (usePair && !target->pairId.empty())
                    a.targetPairId = target->pairId;
                a.hasProposedDynamicEq = true;
                a.proposedDynamicEq.bandCount = 1;
                a.proposedDynamicEq.bands[0].bandId = 1;
                a.proposedDynamicEq.bands[0].enabled = true;
                a.proposedDynamicEq.bands[0].frequencyHz = presenceHz;
                a.proposedDynamicEq.bands[0].q = 2.2;
                a.proposedDynamicEq.bands[0].thresholdDb = -28.0;
                a.proposedDynamicEq.bands[0].ratio = 3.0;
                a.proposedDynamicEq.bands[0].maxCutDb = a.proposedValue;
                a.proposedDynamicEq.bands[0].attackMs = 8.0;
                a.proposedDynamicEq.bands[0].releaseMs = 120.0;
                a.proposedDynamicEq.bands[0].detectorBandPass = true;
                a.proposedDynamicEq.bands[0].detectorFrequencyHz = presenceHz;
                a.proposedDynamicEq.bands[0].detectorQ = 1.8;
                a.proposedDynamicEq.bands[0].detectorSource = dsp::DetectorSource::external;
                a.proposedDynamicEq.bands[0].sidechainSourceId = vocalForUnmask->id;
                a.allowedMin = 0.0;
                a.allowedMax = 9.0;
                actions.push_back(std::move(a));
            }
        }
    }

    // --- Snare / guitar (event-based, not always-on) ---
    if (options.enableSnareUnmask && snare != nullptr && (gL != nullptr || gR != nullptr)) {
        const auto snareProfile = resolveSnareProfile(extrasFor(analysis, snare->id), *snare);
        const auto* guitar = gR != nullptr ? gR : gL;
        const auto guitarProfile = resolveGuitarProfile(extrasFor(analysis, guitar->id), *guitar);
        const bool crackEnergy = snareProfile.crackEnergyDb > -35.0 && snareProfile.crackHz > 0.0;
        const bool presenceConflict = guitarProfile.presenceDb > snareProfile.crackEnergyDb - 8.0
            && guitarProfile.presenceDb > -28.0;
        if (crackEnergy && presenceConflict) {
            const double crackHz = snareProfile.crackHz;
            const double evidence = std::clamp(
                (snareProfile.crackEnergyDb + 35.0) / 35.0 * 0.5
                    + (guitarProfile.presenceDb + 30.0) / 40.0 * 0.4,
                0.45,
                0.85);
            auto a = makeAction(
                "snareGuitarUnmask",
                guitar->id,
                "dynamicEq",
                "maxCutDb",
                0.0,
                std::clamp(2.5 + evidence * 3.0, 2.5, 6.0),
                evidence,
                "Event-based snare crack vs guitar presence conflict. Short attack/release "
                "DynEQ on guitars keyed from snare crack region — not always-on broadband duck.",
                metricsSnippet(snare->metrics),
                snareProfile.evidence + "|" + guitarProfile.evidence,
                "crackHz=" + std::to_string(crackHz) + ";eventBased=true",
                !guitar->pairId.empty() ? "pair" : "track");
            if (!guitar->pairId.empty())
                a.targetPairId = guitar->pairId;
            a.hasProposedDynamicEq = true;
            a.proposedDynamicEq.bandCount = 1;
            a.proposedDynamicEq.bands[0].bandId = 1;
            a.proposedDynamicEq.bands[0].enabled = true;
            a.proposedDynamicEq.bands[0].frequencyHz = crackHz;
            a.proposedDynamicEq.bands[0].q = 2.5;
            a.proposedDynamicEq.bands[0].thresholdDb = -26.0;
            a.proposedDynamicEq.bands[0].ratio = 3.5;
            a.proposedDynamicEq.bands[0].maxCutDb = a.proposedValue;
            a.proposedDynamicEq.bands[0].attackMs = 3.0;
            a.proposedDynamicEq.bands[0].releaseMs = 70.0;
            a.proposedDynamicEq.bands[0].detectorBandPass = true;
            a.proposedDynamicEq.bands[0].detectorFrequencyHz = crackHz;
            a.proposedDynamicEq.bands[0].detectorSource = dsp::DetectorSource::external;
            a.proposedDynamicEq.bands[0].sidechainSourceId = snare->id;
            a.allowedMin = 0.0;
            a.allowedMax = 8.0;
            actions.push_back(std::move(a));
        }
    }

    // --- Drum bus ---
    if (options.enableDrumBus) {
        bool hasAudioChildren = false;
        if (drumBus != nullptr) {
            for (const auto& childId : drumBus->childTrackIds) {
                if (findTrackById(project, childId) != nullptr) {
                    hasAudioChildren = true;
                    break;
                }
            }
        }
        const bool busAvailable = drumBusTrack != nullptr
            || (drumBus != nullptr && hasAudioChildren);

        if (!busAvailable) {
            auto advisory = makeAction(
                "drumBusUnavailable",
                snare != nullptr ? snare->id : std::string {},
                "none",
                "none",
                0.0,
                0.0,
                0.3,
                "No drum-bus audio stem and no bus children with audio — skipping drum-bus "
                "glue. Reject this advisory if intentional.",
                snare != nullptr ? metricsSnippet(snare->metrics) : "{}",
                "drumBusUnavailable",
                "noDrumBusStem && noAudioChildren → advisory",
                "none");
            advisory.state = "pending";
            actions.push_back(std::move(advisory));
        } else {
            const std::string busId = drumBusTrack != nullptr
                ? drumBusTrack->id
                : (drumBus != nullptr ? drumBus->id : std::string {});
            // Prefer a real track target for Apply path; annotate bus via targetBusId.
            const std::string applyTrackId = drumBusTrack != nullptr
                ? drumBusTrack->id
                : (drumBus != nullptr && !drumBus->childTrackIds.empty()
                    ? drumBus->childTrackIds.front()
                    : (snare != nullptr ? snare->id : std::string {}));
            const project::TrackRecord* glueTrack = findTrackById(project, applyTrackId);
            if (glueTrack != nullptr) {
                auto glue = makeAction(
                    "drumBusGlue",
                    applyTrackId,
                    "compressor",
                    "ratio",
                    glueTrack->processing.compressor.ratio,
                    2.5,
                    0.6,
                    "Light drum-bus glue compressor. Targeted at drum bus (not a snare proxy).",
                    metricsSnippet(glueTrack->metrics),
                    "drumBusGlue",
                    "busAvailable→glue",
                    "bus");
                glue.targetBusId = busId;
                glue.hasProposedProcessing = true;
                glue.proposedProcessing = glueTrack->processing;
                glue.proposedProcessing.compressor.thresholdDb = -12.0;
                glue.proposedProcessing.compressor.ratio = 2.5;
                glue.proposedProcessing.compressor.attackMs = 25.0;
                glue.proposedProcessing.compressor.releaseMs = 140.0;
                glue.allowedMin = 1.0;
                glue.allowedMax = 4.0;
                actions.push_back(std::move(glue));

                // Real parallel compressor Action (ParallelCompressor DSP path).
                const bool virtualBus = drumBusTrack == nullptr && drumBus != nullptr;
                auto parallel = makeAction(
                    "drumBusParallel",
                    applyTrackId,
                    "parallelCompressor",
                    "wetAmount",
                    glueTrack->parallelWet,
                    0.45,
                    0.55,
                    virtualBus
                        ? "Virtual drum-bus routing: ParallelCompressor wet/dry on drum hierarchy "
                          "(no dedicated drum-bus stem)."
                        : "Drum-bus parallel compression via ParallelCompressor (latency-aligned dry/wet).",
                    metricsSnippet(glueTrack->metrics),
                    virtualBus ? "virtualDrumBus=true" : "parallelCompressor",
                    "drumBusParallel:parallelCompressor",
                    "bus");
                parallel.targetBusId = busId;
                parallel.allowedMin = 0.0;
                parallel.allowedMax = 1.0;
                applySafeRangeToAction(parallel, "drum-bus", 8.0, 0.45 - parallel.currentValue);
                parallel.proposedValue = std::clamp(parallel.proposedValue, 0.0, 1.0);
                {
                    EvidenceComponents ev;
                    ev.roleCertainty = 0.8;
                    ev.signalDuration = 0.7;
                    ev.spectralProminence = 0.55;
                    appendEvidenceBreakdown(parallel, ev);
                }
                actions.push_back(std::move(parallel));
            }
        }
    }

    // --- Guitar stereo width (pair/bus only — never mono L or R alone) ---
    if (gL != nullptr && gR != nullptr && !gL->pairId.empty() && gL->pairId == gR->pairId) {
        const double corr = 0.5 * (gL->metrics.stereoCorrelation + gR->metrics.stereoCorrelation);
        // Hard-panned mono pairs often report high correlation on each file; use combined cue.
        const double widthCue = std::abs(gL->metrics.spectrum.presenceDb - gR->metrics.spectrum.presenceDb);
        const bool narrow = corr > 0.85 && widthCue < 1.5;
        const bool unstable = corr < 0.15;
        if (narrow || unstable) {
            const double sideDb = narrow ? 1.5 : -1.5;
            const double evidence = narrow ? 0.55 : 0.5;
            auto width = makeAction(
                "guitarStereoWidth",
                gL->id,
                "stereoWidth",
                "sideGainDb",
                0.0,
                sideDb,
                evidence,
                narrow
                    ? "Rhythm guitar pair reads narrow/correlated — modest side gain via StereoWidth "
                      "on the pair (not mono L or R alone)."
                    : "Rhythm guitar pair correlation is unstable — reduce side gain and keep low "
                      "band mono via StereoWidth on the pair.",
                metricsSnippet(gL->metrics),
                "corr=" + std::to_string(corr) + ";widthCue=" + std::to_string(widthCue),
                "stereoWidth:pair",
                "pair");
            width.targetPairId = gL->pairId;
            applySafeRangeToAction(width, "guitar", 6.0, sideDb);
            width.allowedMin = -6.0;
            width.allowedMax = 6.0;
            {
                EvidenceComponents ev;
                ev.roleCertainty = 0.85;
                ev.spectralProminence = evidence;
                ev.temporalOverlap = 0.6;
                appendEvidenceBreakdown(width, ev);
            }
            actions.push_back(std::move(width));
        }
    }

    // --- Section offsets (caller may apply SectionAutomation::fromActions) ---
    const auto* sectionVocal = clean != nullptr ? clean : scream;
    for (const auto& section : project.sections) {
        if (section.kind == project::SectionKind::chorus && sectionVocal != nullptr) {
            auto a = makeAction(
                "sectionVocalLevel",
                sectionVocal->id,
                "gain",
                "gainDb",
                sectionVocal->gainDb,
                sectionVocal->gainDb + 1.0,
                0.6,
                "Chorus section: small relative vocal level offset (+1 dB) on top of global "
                "processing. Global chain remains the base. Apply via SectionAutomation::fromActions.",
                metricsSnippet(sectionVocal->metrics),
                "sectionOffset",
                "section=" + section.id + ";kind=chorus",
                "track");
            a.sectionScope = section.id;
            a.allowedMin = -6.0;
            a.allowedMax = 6.0;
            actions.push_back(std::move(a));
            break;
        }
    }

    // --- Reference (loudness-matched assumption; no blind EQ copy) ---
    if (options.enableReference && !references.empty() && sectionVocal != nullptr) {
        const auto* vocalBal = findReferenceRole(references, "vocal-balance");
        const auto* overall = findReferenceRole(references, "overall");
        const auto* lowEnd = findReferenceRole(references, "low-end");
        const double bed = 0.5
            * ((gL ? gL->metrics.rmsDbfs : -30.0) + (gR ? gR->metrics.rmsDbfs : -30.0));
        const double vocalToBed = sectionVocal->metrics.rmsDbfs - bed;
        const double refVocalToBed = vocalBal != nullptr
            ? vocalBal->vocalToBedDb
            : (overall != nullptr ? overall->vocalToBedDb : 0.0);

        if (vocalToBed + 1.0 < refVocalToBed || (refVocalToBed == 0.0 && vocalToBed < -3.0)) {
            const double lift = std::clamp(
                (refVocalToBed == 0.0 ? (-vocalToBed - 1.0) : (refVocalToBed - vocalToBed)),
                0.5,
                3.0);
            auto a = makeAction(
                "referenceVocalToBed",
                sectionVocal->id,
                "gain",
                "gainDb",
                sectionVocal->gainDb,
                sectionVocal->gainDb + lift,
                0.58,
                "After loudness-matched comparison, vocal-to-bed ratio is low vs reference "
                "vocal-balance profile. Raise vocal gain within safe range — do not copy "
                "reference EQ blindly.",
                metricsSnippet(sectionVocal->metrics),
                "loudnessMatched=assumed;refRole=vocal-balance",
                "vocalToBed=" + std::to_string(vocalToBed)
                    + ";ref=" + std::to_string(refVocalToBed),
                "track");
            a.allowedMin = -6.0;
            a.allowedMax = 6.0;
            actions.push_back(std::move(a));
        }

        if (kick != nullptr && bass != nullptr && lowEnd != nullptr) {
            const double kickToBass = kick->metrics.rmsDbfs - bass->metrics.rmsDbfs;
            if (kickToBass + 1.5 < lowEnd->kickToBassDb) {
                auto a = makeAction(
                    "referenceKickToBass",
                    kick->id,
                    "gain",
                    "gainDb",
                    kick->gainDb,
                    kick->gainDb + std::clamp(lowEnd->kickToBassDb - kickToBass, 0.5, 2.5),
                    0.52,
                    "Loudness-matched kick-to-bass balance is low vs reference low-end profile. "
                    "Bounded kick gain only — no blind EQ copy.",
                    metricsSnippet(kick->metrics),
                    lowEnd->notes,
                    "kickToBass=" + std::to_string(kickToBass)
                        + ";ref=" + std::to_string(lowEnd->kickToBassDb),
                    "track");
                a.allowedMin = -6.0;
                a.allowedMax = 6.0;
                actions.push_back(std::move(a));
            }
        }

        if (overall != nullptr && gL != nullptr) {
            const double lowMid = 0.5
                * (gL->metrics.spectrum.lowMidDb
                    + (gR ? gR->metrics.spectrum.lowMidDb : gL->metrics.spectrum.lowMidDb));
            if (lowMid > overall->lowMidDensityDb + 3.0 && overall->lowMidDensityDb > -80.0) {
                auto a = makeAction(
                    "referenceLowMidDensity",
                    gL->id,
                    "staticEq",
                    "lowShelfGainDb",
                    gL->processing.equalizer.lowShelfGainDb,
                    -1.5,
                    0.5,
                    "After loudness-matched comparison, low-mid density exceeds reference. "
                    "Modest cut only — never blind-copy reference EQ curves.",
                    metricsSnippet(gL->metrics),
                    overall->notes,
                    "lowMid=" + std::to_string(lowMid)
                        + ";ref=" + std::to_string(overall->lowMidDensityDb),
                    !gL->pairId.empty() ? "pair" : "track");
                if (!gL->pairId.empty())
                    a.targetPairId = gL->pairId;
                a.hasProposedProcessing = true;
                a.proposedProcessing = gL->processing;
                a.proposedProcessing.equalizer.lowShelfHz = 220.0;
                a.proposedProcessing.equalizer.lowShelfGainDb = -1.5;
                a.allowedMin = -6.0;
                a.allowedMax = 0.0;
                actions.push_back(std::move(a));
            }
        }
    }

    auto resolved = ActionResolver::resolve(std::move(actions));
    lastConflicts_ = std::move(resolved.conflicts);

    ActionBudget budget;
    std::vector<std::string> rejectedReasons;
    auto budgeted = applyBudget(std::move(resolved.actions), budget, rejectedReasons);

    // Stable display order: kept (by resolver orderIndex) then rejected.
    std::stable_sort(budgeted.begin(), budgeted.end(), [](const auto& a, const auto& b) {
        const bool aRej = a.state == "rejected";
        const bool bRej = b.state == "rejected";
        if (aRej != bRej)
            return !aRej && bRej;
        return a.orderIndex < b.orderIndex;
    });
    for (std::size_t i = 0; i < budgeted.size(); ++i)
        budgeted[i].orderIndex = static_cast<int>(i);

    for (auto& action : budgeted) {
        if (action.state == "rejected")
            continue;
        // Ensure DynEQ / gain proposals carry a SafeRangeDerivation.trace when missing.
        if ((action.processorId == "dynamicEq" || action.processorId == "gain"
                || action.processorId == "outputGain")
            && action.decisionTrace.find("globalCap=") == std::string::npos) {
            const std::string role = action.processingLevel == "bus" ? "drum-bus" : "track";
            applySafeRangeToAction(
                action,
                role,
                action.cumulativeCap > 0.0 ? action.cumulativeCap : 8.0,
                action.proposedValue - action.currentValue);
        }
        if (action.decisionTrace.find("=> score=") == std::string::npos) {
            EvidenceComponents ev;
            ev.roleCertainty = action.evidenceScore;
            ev.spectralProminence = action.evidenceScore;
            ev.detectorAgreement = std::clamp(action.evidenceScore * 0.9, 0.0, 1.0);
            appendEvidenceBreakdown(action, ev);
        }
    }
    return budgeted;
}

std::vector<project::MixPassAction> MetalcoreMixPass::generateActions(
    const project::ProjectDocument& project,
    const std::optional<analysis::AudioMetrics>& reference,
    const Options& options) const
{
    AnalysisMap analysis;
    analysis.reserve(project.tracks.size());
    for (const auto& track : project.tracks)
        analysis.emplace(track.id, buildExtrasFromTrack(track, options.allowSyntheticFrequencyFallback));

    std::vector<ReferenceProfile> references;
    if (reference.has_value()) {
        const auto* vocal = findTrack(project, project::TrackRole::cleanVocal);
        if (vocal == nullptr)
            vocal = findTrack(project, project::TrackRole::screamVocal);
        const auto* gL = findTrack(project, project::TrackRole::rhythmGuitarLeft);
        const auto* gR = findTrack(project, project::TrackRole::rhythmGuitarRight);
        const auto* kick = findTrack(project, project::TrackRole::kick);
        const auto* bass = findTrack(project, project::TrackRole::bass);
        const double bed = 0.5
            * ((gL ? gL->metrics.rmsDbfs : -30.0) + (gR ? gR->metrics.rmsDbfs : -30.0));
        const double vocalToBed = vocal != nullptr ? vocal->metrics.rmsDbfs - bed : 0.0;
        const double kickToBass = (kick != nullptr && bass != nullptr)
            ? kick->metrics.rmsDbfs - bass->metrics.rmsDbfs
            : 0.0;

        // Reference metrics are loudness-matched mix-level; stem ratios are project-side cues.
        references.push_back(
            MetalcoreAnalysis::buildReferenceProfile(*reference, "overall", vocalToBed, kickToBass));
        references.push_back(
            MetalcoreAnalysis::buildReferenceProfile(*reference, "vocal-balance", vocalToBed, kickToBass));
        references.push_back(
            MetalcoreAnalysis::buildReferenceProfile(*reference, "low-end", vocalToBed, kickToBass));
        references.push_back(
            MetalcoreAnalysis::buildReferenceProfile(*reference, "density", vocalToBed, kickToBass));
    }

    return generateActions(project, analysis, references, options);
}

bool MetalcoreMixPass::previewAction(project::MixPassAction& action) noexcept
{
    if (action.state == "applied" || action.state == "rejected")
        return false;
    action.state = "previewing";
    return true;
}

bool MetalcoreMixPass::cancelPreview(project::MixPassAction& action) noexcept
{
    if (action.state != "previewing")
        return false;
    action.state = "pending";
    return true;
}

bool MetalcoreMixPass::rejectAction(project::MixPassAction& action) noexcept
{
    if (action.state != "pending" && action.state != "previewing" && action.state != "edited")
        return false;
    action.state = "rejected";
    return true;
}

bool MetalcoreMixPass::applyAction(
    project::ProjectDocument& project,
    project::MixPassAction& action)
{
    if (action.state == "rejected" || action.state == "cancelled")
        return false;

    // Bus-level actions: update bus processing when targetBusId is set and level is bus.
    if (!action.targetBusId.empty() && action.processingLevel == "bus") {
        project::BusRecord* bus = nullptr;
        for (auto& b : project.buses) {
            if (b.id == action.targetBusId) {
                bus = &b;
                break;
            }
        }
        if (bus != nullptr) {
            if (!action.hasPrevious) {
                action.previousGainDb = bus->gainDb;
                action.previousProcessing = bus->processing;
                action.previousDynamicEq = bus->dynamicEq;
                action.previousDynamicEqEnabled = bus->dynamicEqEnabled;
                action.hasPrevious = true;
            }
            if (action.processorId == "gain")
                bus->gainDb = action.proposedValue;
            if (action.hasProposedProcessing)
                bus->processing = action.proposedProcessing;
            if (action.hasProposedDynamicEq) {
                bus->dynamicEq = action.proposedDynamicEq;
                bus->dynamicEqEnabled = true;
            }
            if (action.processorId == "parallelCompressor"
                || action.processorId == "parallel") {
                bus->parallelEnabled = true;
                bus->parallelWet = std::clamp(action.proposedValue, 0.0, 1.0);
                bus->parallelThresholdDb = -18.0;
                bus->parallelRatio = 4.0;
                bus->parallelAttackMs = 10.0;
                bus->parallelReleaseMs = 100.0;
                if (action.problemType == "drumBusParallel"
                    && findTrack(project, project::TrackRole::drumBus) == nullptr) {
                    project.virtualDrumBusEnabled = true;
                }
                // Mirror onto child tracks so StemEngine can process without a bus stem.
                for (const auto& childId : bus->childTrackIds) {
                    if (auto* child = findTrackMutable(project, childId)) {
                        child->parallelEnabled = true;
                        child->parallelWet = bus->parallelWet;
                        child->parallelThresholdDb = bus->parallelThresholdDb;
                        child->parallelRatio = bus->parallelRatio;
                        child->parallelAttackMs = bus->parallelAttackMs;
                        child->parallelReleaseMs = bus->parallelReleaseMs;
                    }
                }
            }
            if (action.processorId == "stereoWidth" || action.processorId == "stereo") {
                bus->stereoWidthEnabled = true;
                bus->sideGainDb = action.proposedValue;
                bus->lowBandMonoHz = 120.0;
            }
            action.state = "applied";
            return true;
        }
    }

    // Pair-level stereo width (never applied to mono L/R alone without pair).
    if (action.processorId == "stereoWidth" || action.processorId == "stereo") {
        if (!action.targetPairId.empty()) {
            project::PairRecord* pair = nullptr;
            for (auto& p : project.pairs) {
                if (p.id == action.targetPairId) {
                    pair = &p;
                    break;
                }
            }
            if (pair != nullptr) {
                pair->stereoWidthEnabled = true;
                pair->sideGainDb = action.proposedValue;
                pair->lowBandMonoHz = 120.0;
                // Enable on stereo pair members only when channel is Stereo; for L/R mono
                // stems, StemEngine applies width after pair sum via pair flags on both.
                for (auto& track : project.tracks) {
                    if (track.id == pair->leftTrackId || track.id == pair->rightTrackId) {
                        track.stereoWidthEnabled = true;
                        track.sideGainDb = action.proposedValue;
                        track.lowBandMonoHz = pair->lowBandMonoHz;
                        track.midGainDb = pair->midGainDb;
                    }
                }
                action.state = "applied";
                return true;
            }
        }
    }

    auto* track = findTrackMutable(project, action.targetTrackId);
    if (track == nullptr)
        return false;

    // Idempotent: same absolute values already present.
    if (action.state == "applied" && action.hasPrevious) {
        if (action.processorId == "gain"
            && std::abs(track->gainDb - action.proposedValue) < 1.0e-6)
            return true;
        if (action.processorId == "vocalRider"
            && track->vocalRiderEnabled == action.vocalRiderEnabled
            && std::abs(track->vocalRiderTargetDb - action.vocalRiderTargetDb) < 1.0e-6)
            return true;
        if (action.hasProposedProcessing
            && track->processing.equalizer.lowShelfGainDb
                == action.proposedProcessing.equalizer.lowShelfGainDb)
            return true;
    }

    if (!action.hasPrevious) {
        action.previousGainDb = track->gainDb;
        action.previousProcessing = track->processing;
        action.previousDynamicEq = track->dynamicEq;
        action.previousDynamicEqEnabled = track->dynamicEqEnabled;
        action.hasPrevious = true;
    }

    if (action.processorId == "gain") {
        track->gainDb = action.proposedValue;
    } else if (action.processorId == "vocalRider") {
        track->vocalRiderEnabled = true;
        track->vocalRiderTargetDb = action.vocalRiderEnabled
            ? action.vocalRiderTargetDb
            : action.proposedValue;
    } else if (action.processorId == "parallelCompressor"
        || action.processorId == "parallel") {
        track->parallelEnabled = true;
        track->parallelWet = std::clamp(action.proposedValue, 0.0, 1.0);
        track->parallelThresholdDb = -18.0;
        track->parallelRatio = 4.0;
        track->parallelAttackMs = 10.0;
        track->parallelReleaseMs = 100.0;
        if (!action.targetBusId.empty()) {
            for (auto& bus : project.buses) {
                if (bus.id == action.targetBusId) {
                    bus.parallelEnabled = true;
                    bus.parallelWet = track->parallelWet;
                    break;
                }
            }
        }
    } else if (action.processorId == "stereoWidth" || action.processorId == "stereo") {
        // Refuse mono L/R-only width without a pair target.
        const bool monoSide = track->channelPosition == "L" || track->channelPosition == "R"
            || track->channelPosition == "Mono";
        if (monoSide && action.targetPairId.empty()) {
            return false;
        }
        track->stereoWidthEnabled = true;
        track->sideGainDb = action.proposedValue;
        track->lowBandMonoHz = 120.0;
    } else if (action.hasProposedProcessing) {
        track->processing = action.proposedProcessing;
        if (action.processorId == "gain")
            track->gainDb = action.proposedValue;
    }
    if (action.hasProposedDynamicEq) {
        track->dynamicEq = action.proposedDynamicEq;
        track->dynamicEqEnabled = true;
    }

    // Linked pair: mirror processing to the other side when pairId set and problem is pair-scope.
    if (!action.targetPairId.empty() && isPairMirrorProblem(action.problemType)) {
        for (auto& other : project.tracks) {
            if (other.pairId == action.targetPairId && other.id != track->id) {
                if (action.hasProposedProcessing)
                    other.processing = action.proposedProcessing;
                if (action.hasProposedDynamicEq) {
                    other.dynamicEq = action.proposedDynamicEq;
                    other.dynamicEqEnabled = true;
                }
                if (action.processorId == "stereoWidth" || action.processorId == "stereo") {
                    other.stereoWidthEnabled = true;
                    other.sideGainDb = action.proposedValue;
                    other.lowBandMonoHz = track->lowBandMonoHz;
                }
            }
        }
    }

    action.state = "applied";

    // Maintain thin legacy actions index.
    project::AppliedAction thin;
    thin.actionId = action.actionId;
    thin.trackId = action.targetTrackId;
    thin.targetGainDb = track->gainDb;
    thin.state = "applied";
    thin.processing = track->processing;
    thin.previousGainDb = action.previousGainDb;
    thin.previousProcessing = action.previousProcessing;
    thin.hasPrevious = action.hasPrevious;
    thin.problemType = action.problemType;
    thin.processorId = action.processorId;
    thin.sectionScope = action.sectionScope;
    auto existing = std::find_if(
        project.actions.begin(),
        project.actions.end(),
        [&](const auto& a) { return a.actionId == action.actionId; });
    if (existing != project.actions.end())
        *existing = thin;
    else
        project.actions.push_back(thin);

    return true;
}

bool MetalcoreMixPass::editAction(project::MixPassAction& action, double proposedValue) noexcept
{
    if (action.state == "rejected" || action.state == "cancelled")
        return false;
    action.proposedValue = std::clamp(proposedValue, action.allowedMin, action.allowedMax);
    if (action.hasProposedDynamicEq && action.parameterId == "maxCutDb")
        action.proposedDynamicEq.bands[0].maxCutDb = action.proposedValue;
    if (action.processorId == "vocalRider" && action.parameterId == "targetDb") {
        action.vocalRiderTargetDb = action.proposedValue;
        action.vocalRiderEnabled = true;
    }
    if (action.state != "applied")
        action.state = "edited";
    return true;
}

bool MetalcoreMixPass::undoAction(
    project::ProjectDocument& project,
    project::MixPassAction& action)
{
    if (!action.hasPrevious || action.state != "applied")
        return false;

    if (!action.targetBusId.empty() && action.processingLevel == "bus") {
        for (auto& bus : project.buses) {
            if (bus.id == action.targetBusId) {
                bus.gainDb = action.previousGainDb;
                bus.processing = action.previousProcessing;
                bus.dynamicEq = action.previousDynamicEq;
                bus.dynamicEqEnabled = action.previousDynamicEqEnabled;
                action.state = "pending";
                return true;
            }
        }
    }

    auto* track = findTrackMutable(project, action.targetTrackId);
    if (track == nullptr)
        return false;
    track->gainDb = action.previousGainDb;
    track->processing = action.previousProcessing;
    track->dynamicEq = action.previousDynamicEq;
    track->dynamicEqEnabled = action.previousDynamicEqEnabled;
    if (action.processorId == "vocalRider") {
        track->vocalRiderEnabled = false;
        track->vocalRiderTargetDb = -18.0;
    }
    if (!action.targetPairId.empty() && isPairMirrorProblem(action.problemType)) {
        for (auto& other : project.tracks) {
            if (other.pairId == action.targetPairId && other.id != track->id) {
                other.processing = action.previousProcessing;
                other.dynamicEq = action.previousDynamicEq;
                other.dynamicEqEnabled = action.previousDynamicEqEnabled;
            }
        }
    }
    action.state = "pending";
    return true;
}

std::string mixPassActionToJson(const project::MixPassAction& action)
{
    nlohmann::json j {
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
        {"vocalRiderEnabled", action.vocalRiderEnabled},
        {"vocalRiderTargetDb", action.vocalRiderTargetDb}
    };
    return j.dump(2);
}

std::string mixPassActionsToJson(const std::vector<project::MixPassAction>& actions)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& a : actions)
        arr.push_back(nlohmann::json::parse(mixPassActionToJson(a)));
    return arr.dump(2);
}

} // namespace mastering::assistant
