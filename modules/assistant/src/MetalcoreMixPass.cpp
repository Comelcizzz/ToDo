#include "mastering/assistant/MetalcoreMixPass.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>

namespace mastering::assistant {
namespace {

project::MixPassAction makeAction(
    const std::string& problem,
    const std::string& trackId,
    const std::string& processor,
    const std::string& parameter,
    double current,
    double proposed,
    double confidence,
    const std::string& explanation,
    const std::string& metrics)
{
    project::MixPassAction a;
    a.actionId = project::makeProjectId();
    a.problemType = problem;
    a.targetTrackId = trackId;
    a.processorId = processor;
    a.parameterId = parameter;
    a.currentValue = current;
    a.proposedValue = proposed;
    a.allowedMin = -24.0;
    a.allowedMax = 24.0;
    a.confidence = confidence;
    a.explanation = explanation;
    a.sourceMetrics = metrics;
    a.sectionScope = "full";
    a.state = "pending";
    a.origin = "mixpass";
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
    const std::optional<analysis::AudioMetrics>& reference,
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

    if (options.enableKickBass && kick != nullptr && bass != nullptr) {
        const auto kickSub = kick->metrics.spectrum.subDb;
        const auto bassSub = bass->metrics.spectrum.subDb;
        const auto conflict = std::abs(kickSub - bassSub);
        if (conflict < 6.0 || bassSub > kickSub - 3.0) {
            // Frequency-dependent duck on bass around kick fundamental — not broadband.
            project::MixPassAction a = makeAction(
                "kickBassMasking",
                bass->id,
                "dynamicEq",
                "maxCutDb",
                bass->dynamicEq.bands[0].maxCutDb,
                std::clamp(6.0 + (6.0 - std::min(6.0, conflict)), 4.0, 10.0),
                0.78,
                "Kick and bass compete in the same sub/bass region. Apply frequency-dependent "
                "kick→bass Dynamic EQ (detector ~55–80 Hz, target ~55–85 Hz) with bounded max cut "
                "so upper bass articulation is retained.",
                metricsSnippet(bass->metrics));
            a.hasProposedDynamicEq = true;
            a.proposedDynamicEq = bass->dynamicEq;
            a.proposedDynamicEq.bandCount = 1;
            a.proposedDynamicEq.bands[0].bandId = 1;
            a.proposedDynamicEq.bands[0].enabled = true;
            a.proposedDynamicEq.bands[0].filterType = dsp::DynamicEqFilterType::bell;
            a.proposedDynamicEq.bands[0].frequencyHz = 70.0;
            a.proposedDynamicEq.bands[0].q = 2.8;
            a.proposedDynamicEq.bands[0].staticGainDb = 0.0;
            a.proposedDynamicEq.bands[0].thresholdDb = -22.0;
            a.proposedDynamicEq.bands[0].ratio = 4.0;
            a.proposedDynamicEq.bands[0].maxCutDb = a.proposedValue;
            a.proposedDynamicEq.bands[0].attackMs = 5.0;
            a.proposedDynamicEq.bands[0].releaseMs = std::clamp(60'000.0 / std::max(60.0, options.bpm) * 0.35, 40.0, 180.0);
            a.proposedDynamicEq.bands[0].detectorBandPass = true;
            a.proposedDynamicEq.bands[0].detectorFrequencyHz = 65.0;
            a.proposedDynamicEq.bands[0].detectorQ = 3.0;
            a.proposedDynamicEq.bands[0].detectorSource = dsp::DetectorSource::external;
            a.proposedDynamicEq.bands[0].stereoMode = dsp::DynamicEqStereoMode::linked;
            a.proposedDynamicEq.bands[0].sidechainSourceId = kick->id;
            a.allowedMin = 0.0;
            a.allowedMax = 12.0;
            actions.push_back(std::move(a));

            // Complementary static EQ: slight kick body clarity vs bass shelf.
            auto kickEq = makeAction(
                "kickBassComplementaryEq",
                kick->id,
                "staticEq",
                "presenceGainDb",
                kick->processing.equalizer.presenceGainDb,
                1.5,
                0.62,
                "Give kick a small presence lift while bass owns the lowest sub energy.",
                metricsSnippet(kick->metrics));
            kickEq.hasProposedProcessing = true;
            kickEq.proposedProcessing = kick->processing;
            kickEq.proposedProcessing.equalizer.presenceHz = 4'000.0;
            kickEq.proposedProcessing.equalizer.presenceGainDb = 1.5;
            kickEq.proposedProcessing.equalizer.lowShelfGainDb = -0.5;
            actions.push_back(std::move(kickEq));
        }
    }

    if (options.enableGuitarBalance && gL != nullptr && gR != nullptr) {
        const auto delta = gL->metrics.rmsDbfs - gR->metrics.rmsDbfs;
        if (std::abs(delta) > 1.0) {
            const auto* quieter = delta > 0 ? gR : gL;
            const auto* louder = delta > 0 ? gL : gR;
            auto a = makeAction(
                "guitarLrBalance",
                quieter->id,
                "gain",
                "gainDb",
                quieter->gainDb,
                quieter->gainDb + std::clamp(std::abs(delta) * 0.5, 0.5, 3.0),
                0.8,
                "Rhythm Guitar L/R section level imbalance. Adjust the quieter side only; "
                "do not time-align or phase-align independent doubles.",
                metricsSnippet(quieter->metrics));
            a.allowedMin = -12.0;
            a.allowedMax = 12.0;
            actions.push_back(std::move(a));
            (void) louder;
        }

        // Low-mid mud on guitar bus / pair when lowMid hot.
        const auto mud = 0.5 * (gL->metrics.spectrum.lowMidDb + gR->metrics.spectrum.lowMidDb);
        if (mud > -18.0) {
            auto a = makeAction(
                "guitarLowMidMud",
                gL->id,
                "staticEq",
                "lowShelfGainDb",
                gL->processing.equalizer.lowShelfGainDb,
                -2.0,
                0.7,
                "Rhythm guitars show elevated low-mid density (palm-mute bloom / mud). "
                "Apply a modest low-shelf cut on each side (pair-linked proposal).",
                metricsSnippet(gL->metrics));
            a.targetPairId = gL->pairId;
            a.hasProposedProcessing = true;
            a.proposedProcessing = gL->processing;
            a.proposedProcessing.equalizer.lowShelfHz = 220.0;
            a.proposedProcessing.equalizer.lowShelfGainDb = -2.0;
            a.allowedMin = -6.0;
            a.allowedMax = 0.0;
            actions.push_back(std::move(a));
        }

        if (gL->metrics.spectrum.airDb > -12.0 || gR->metrics.spectrum.airDb > -12.0) {
            auto a = makeAction(
                "guitarHarshness",
                gR->id,
                "staticEq",
                "highShelfGainDb",
                gR->processing.equalizer.highShelfGainDb,
                -1.5,
                0.65,
                "High-frequency energy / fizz on rhythm guitars. Soft high-shelf cut; prefer bus "
                "or pair scope when image must stay linked.",
                metricsSnippet(gR->metrics));
            a.targetPairId = gR->pairId;
            a.hasProposedProcessing = true;
            a.proposedProcessing = gR->processing;
            a.proposedProcessing.equalizer.highShelfHz = 8'000.0;
            a.proposedProcessing.equalizer.highShelfGainDb = -1.5;
            actions.push_back(std::move(a));
        }
    }

    const auto* vocal = clean != nullptr ? clean : scream;
    if (options.enableVocalUnmask && vocal != nullptr && (gL != nullptr || gR != nullptr)) {
        const auto* guitar = gL != nullptr ? gL : gR;
        const auto presenceDelta = vocal->metrics.spectrum.presenceDb - guitar->metrics.spectrum.presenceDb;
        if (presenceDelta < 3.0) {
            auto a = makeAction(
                "vocalGuitarUnmask",
                guitar->id,
                "dynamicEq",
                "maxCutDb",
                0.0,
                5.0,
                0.74,
                "Vocal presence is masked by dense guitars. Frequency-dependent Dynamic EQ on "
                "guitars (detector on vocal presence ~2–4 kHz) with bounded cut.",
                metricsSnippet(vocal->metrics));
            a.targetPairId = guitar->pairId;
            a.hasProposedDynamicEq = true;
            a.proposedDynamicEq.bandCount = 1;
            a.proposedDynamicEq.bands[0].bandId = 1;
            a.proposedDynamicEq.bands[0].enabled = true;
            a.proposedDynamicEq.bands[0].frequencyHz = 2'800.0;
            a.proposedDynamicEq.bands[0].q = 2.2;
            a.proposedDynamicEq.bands[0].thresholdDb = -28.0;
            a.proposedDynamicEq.bands[0].ratio = 3.0;
            a.proposedDynamicEq.bands[0].maxCutDb = 5.0;
            a.proposedDynamicEq.bands[0].attackMs = 8.0;
            a.proposedDynamicEq.bands[0].releaseMs = 120.0;
            a.proposedDynamicEq.bands[0].detectorBandPass = true;
            a.proposedDynamicEq.bands[0].detectorFrequencyHz = 3'000.0;
            a.proposedDynamicEq.bands[0].detectorQ = 1.8;
            a.proposedDynamicEq.bands[0].detectorSource = dsp::DetectorSource::external;
            a.proposedDynamicEq.bands[0].sidechainSourceId = vocal->id;
            a.allowedMin = 0.0;
            a.allowedMax = 9.0;
            actions.push_back(std::move(a));
        }

        if (vocal->metrics.crestFactorDb > 16.0) {
            auto ride = makeAction(
                "vocalRiding",
                vocal->id,
                "compressor",
                "thresholdDb",
                vocal->processing.compressor.thresholdDb,
                -14.0,
                0.7,
                "Vocal crest is high — serial compression / riding proposal for level stability "
                "(clean and scream stay on separate roles).",
                metricsSnippet(vocal->metrics));
            ride.hasProposedProcessing = true;
            ride.proposedProcessing = vocal->processing;
            ride.proposedProcessing.compressor.thresholdDb = -14.0;
            ride.proposedProcessing.compressor.ratio = 3.0;
            ride.proposedProcessing.compressor.attackMs = 18.0;
            ride.proposedProcessing.compressor.releaseMs = 90.0;
            ride.proposedProcessing.compressor.makeupDb = 2.0;
            actions.push_back(std::move(ride));
        }
    }

    if (options.enableSnareUnmask && snare != nullptr && (gL != nullptr || gR != nullptr)) {
        const auto* guitar = gR != nullptr ? gR : gL;
        auto a = makeAction(
            "snareGuitarUnmask",
            guitar->id,
            "dynamicEq",
            "maxCutDb",
            0.0,
            4.0,
            0.68,
            "Snare body/crack competes with guitar midrange. Frequency-dependent Dynamic EQ on "
            "guitars keyed from snare (~180–250 Hz body / ~2–3 kHz crack region).",
            metricsSnippet(snare->metrics));
        a.targetPairId = guitar->pairId;
        a.hasProposedDynamicEq = true;
        a.proposedDynamicEq.bandCount = 1;
        a.proposedDynamicEq.bands[0].bandId = 1;
        a.proposedDynamicEq.bands[0].enabled = true;
        a.proposedDynamicEq.bands[0].frequencyHz = 2'200.0;
        a.proposedDynamicEq.bands[0].q = 2.5;
        a.proposedDynamicEq.bands[0].thresholdDb = -26.0;
        a.proposedDynamicEq.bands[0].ratio = 3.5;
        a.proposedDynamicEq.bands[0].maxCutDb = 4.0;
        a.proposedDynamicEq.bands[0].attackMs = 3.0;
        a.proposedDynamicEq.bands[0].releaseMs = 90.0;
        a.proposedDynamicEq.bands[0].detectorBandPass = true;
        a.proposedDynamicEq.bands[0].detectorFrequencyHz = 2'400.0;
        a.proposedDynamicEq.bands[0].detectorSource = dsp::DetectorSource::external;
        a.proposedDynamicEq.bands[0].sidechainSourceId = snare->id;
        a.allowedMin = 0.0;
        a.allowedMax = 8.0;
        actions.push_back(std::move(a));
    }

    if (options.enableDrumBus && snare != nullptr) {
        auto a = makeAction(
            "drumBusGlue",
            snare->id,
            "compressor",
            "ratio",
            snare->processing.compressor.ratio,
            2.5,
            0.55,
            "Light drum bus style glue proposal applied to snare stem as proxy when no discrete "
            "drum-bus audio stem is present. Crest-factor guardrail: keep makeup modest.",
            metricsSnippet(snare->metrics));
        a.hasProposedProcessing = true;
        a.proposedProcessing = snare->processing;
        a.proposedProcessing.compressor.thresholdDb = -12.0;
        a.proposedProcessing.compressor.ratio = 2.5;
        a.proposedProcessing.compressor.attackMs = 25.0;
        a.proposedProcessing.compressor.releaseMs = 140.0;
        actions.push_back(std::move(a));
    }

    // Section-relative vocal level offset example (chorus louder) when sections exist.
    for (const auto& section : project.sections) {
        if (section.kind == project::SectionKind::chorus && vocal != nullptr) {
            auto a = makeAction(
                "sectionVocalLevel",
                vocal->id,
                "gain",
                "gainDb",
                vocal->gainDb,
                vocal->gainDb + 1.0,
                0.6,
                "Chorus section: small relative vocal level offset (+1 dB) on top of global "
                "processing. Global chain remains the base.",
                metricsSnippet(vocal->metrics));
            a.sectionScope = section.id;
            a.allowedMin = -6.0;
            a.allowedMax = 6.0;
            actions.push_back(std::move(a));
            break;
        }
    }

    if (reference.has_value() && vocal != nullptr) {
        const auto bed = 0.5
            * ((gL ? gL->metrics.rmsDbfs : -30.0) + (gR ? gR->metrics.rmsDbfs : -30.0));
        const auto vocalToBed = vocal->metrics.rmsDbfs - bed;
        const auto refVocalToBed = 0.0; // synthetic placeholder when ref lacks stems
        (void) refVocalToBed;
        if (vocalToBed < -3.0) {
            auto a = makeAction(
                "referenceVocalToBed",
                vocal->id,
                "gain",
                "gainDb",
                vocal->gainDb,
                vocal->gainDb + std::clamp(-vocalToBed - 1.0, 0.5, 3.0),
                0.58,
                "After loudness-matched comparison, vocal-to-bed ratio is low vs typical metalcore "
                "targets. Raise vocal gain within safe range — do not copy reference EQ blindly.",
                metricsSnippet(vocal->metrics));
            actions.push_back(std::move(a));
        }
    }

    return actions;
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

    auto* track = findTrackMutable(project, action.targetTrackId);
    if (track == nullptr)
        return false;

    // Idempotent: same absolute values already present.
    if (action.state == "applied" && action.hasPrevious) {
        if (action.processorId == "gain"
            && std::abs(track->gainDb - action.proposedValue) < 1.0e-6)
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
    if (!action.targetPairId.empty()
        && (action.problemType == "guitarLowMidMud" || action.problemType == "guitarHarshness"
            || action.problemType == "vocalGuitarUnmask" || action.problemType == "snareGuitarUnmask")) {
        for (auto& other : project.tracks) {
            if (other.pairId == action.targetPairId && other.id != track->id) {
                if (action.hasProposedProcessing)
                    other.processing = action.proposedProcessing;
                if (action.hasProposedDynamicEq) {
                    other.dynamicEq = action.proposedDynamicEq;
                    other.dynamicEqEnabled = true;
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
    if (action.processorId == "gain")
        action.proposedValue = action.proposedValue;
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
    auto* track = findTrackMutable(project, action.targetTrackId);
    if (track == nullptr)
        return false;
    track->gainDb = action.previousGainDb;
    track->processing = action.previousProcessing;
    track->dynamicEq = action.previousDynamicEq;
    track->dynamicEqEnabled = action.previousDynamicEqEnabled;
    if (!action.targetPairId.empty()
        && (action.problemType == "guitarLowMidMud" || action.problemType == "guitarHarshness"
            || action.problemType == "vocalGuitarUnmask" || action.problemType == "snareGuitarUnmask")) {
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
        {"confidence", action.confidence},
        {"explanation", action.explanation},
        {"sourceMetrics", action.sourceMetrics},
        {"sectionScope", action.sectionScope},
        {"state", action.state},
        {"origin", action.origin}
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
