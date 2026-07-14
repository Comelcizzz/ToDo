#include "mastering/assistant/MixAdvisor.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>

namespace mastering::assistant {
namespace {

using project::TrackRole;

void addSuggestion(
    MixPlan& plan,
    SuggestionKind kind,
    const std::string& trackId,
    std::string title,
    std::string explanation,
    double confidence)
{
    plan.suggestions.push_back({
        kind,
        trackId,
        std::move(title),
        std::move(explanation),
        std::clamp(confidence, 0.0, 1.0)
    });
}

bool isVocal(TrackRole role) noexcept
{
    return role == TrackRole::cleanVocal
        || role == TrackRole::screamVocal
        || role == TrackRole::backingVocal;
}

} // namespace

double MixAdvisor::targetRms(TrackRole role) noexcept
{
    switch (role) {
    case TrackRole::kick: return -18.0;
    case TrackRole::snare: return -19.0;
    case TrackRole::drums: return -18.0;
    case TrackRole::bass: return -20.0;
    case TrackRole::rhythmGuitar: return -23.0;
    case TrackRole::leadGuitar: return -24.0;
    case TrackRole::cleanVocal: return -21.0;
    case TrackRole::screamVocal: return -22.0;
    case TrackRole::backingVocal: return -27.0;
    case TrackRole::synth: return -27.0;
    case TrackRole::orchestra: return -28.0;
    case TrackRole::effects: return -30.0;
    default: return -24.0;
    }
}

dsp::ProcessorSettings MixAdvisor::settingsForRole(
    TrackRole role,
    const analysis::AudioMetrics& metrics) noexcept
{
    dsp::ProcessorSettings settings;
    settings.amount = 0.72;
    settings.clipCeilingDb = -1.0;
    settings.compressor.thresholdDb = -14.0;
    settings.compressor.ratio = 2.0;
    settings.compressor.attackMs = 25.0;
    settings.compressor.releaseMs = 120.0;

    switch (role) {
    case TrackRole::kick:
        settings.equalizer.highPassHz = 28.0;
        settings.compressor.attackMs = 18.0;
        settings.compressor.releaseMs = 70.0;
        settings.saturation = 0.12;
        break;
    case TrackRole::snare:
        settings.equalizer.highPassHz = 75.0;
        settings.equalizer.presenceHz = 3'500.0;
        settings.compressor.attackMs = 22.0;
        settings.saturation = 0.10;
        break;
    case TrackRole::cymbals:
        settings.equalizer.highPassHz = 180.0;
        settings.equalizer.highShelfGainDb = metrics.spectrum.airDb > metrics.spectrum.midDb + 8.0
            ? -1.5
            : 0.0;
        settings.compressor.ratio = 1.5;
        settings.saturation = 0.0;
        break;
    case TrackRole::bass:
        settings.equalizer.highPassHz = 30.0;
        settings.compressor.thresholdDb = -18.0;
        settings.compressor.ratio = 3.0;
        settings.compressor.attackMs = 16.0;
        settings.compressor.releaseMs = 90.0;
        settings.saturation = 0.18;
        break;
    case TrackRole::rhythmGuitar:
        settings.equalizer.highPassHz = 72.0;
        settings.equalizer.highShelfHz = 9'000.0;
        settings.equalizer.highShelfGainDb = -0.5;
        settings.compressor.ratio = 1.5;
        settings.saturation = 0.08;
        break;
    case TrackRole::leadGuitar:
        settings.equalizer.highPassHz = 95.0;
        settings.equalizer.presenceGainDb = 0.8;
        settings.saturation = 0.05;
        break;
    case TrackRole::cleanVocal:
        settings.equalizer.highPassHz = 75.0;
        settings.equalizer.presenceHz = 2'800.0;
        settings.equalizer.presenceGainDb = 0.7;
        settings.equalizer.highShelfGainDb = 0.5;
        settings.compressor.thresholdDb = -20.0;
        settings.compressor.ratio = 3.0;
        settings.compressor.attackMs = 8.0;
        settings.compressor.releaseMs = 85.0;
        settings.saturation = 0.08;
        break;
    case TrackRole::screamVocal:
        settings.equalizer.highPassHz = 85.0;
        settings.compressor.thresholdDb = -22.0;
        settings.compressor.ratio = 4.0;
        settings.compressor.attackMs = 4.0;
        settings.compressor.releaseMs = 65.0;
        settings.saturation = 0.14;
        break;
    case TrackRole::backingVocal:
        settings.equalizer.highPassHz = 120.0;
        settings.equalizer.presenceGainDb = -1.0;
        settings.compressor.ratio = 3.0;
        settings.saturation = 0.08;
        break;
    case TrackRole::synth:
    case TrackRole::orchestra:
        settings.equalizer.highPassHz = 80.0;
        settings.equalizer.lowShelfGainDb = -1.0;
        settings.compressor.ratio = 1.5;
        settings.saturation = 0.04;
        break;
    case TrackRole::effects:
        settings.equalizer.highPassHz = 45.0;
        settings.compressor.ratio = 1.5;
        settings.saturation = 0.05;
        break;
    default:
        settings.equalizer.highPassHz = 30.0;
        settings.saturation = 0.03;
        break;
    }
    return settings;
}

MixPlan MixAdvisor::createPlan(
    const project::ProjectDocument& project,
    const std::optional<analysis::AudioMetrics>& reference) const
{
    MixPlan plan;
    const project::TrackRecord* kick = nullptr;
    const project::TrackRecord* bass = nullptr;
    const project::TrackRecord* leadVocal = nullptr;
    double bedRmsSum = 0.0;
    std::size_t bedCount = 0;

    for (const auto& track : project.tracks) {
        auto settings = settingsForRole(track.role, track.metrics);
        const auto usableRms = std::isfinite(track.metrics.rmsDbfs)
            ? track.metrics.rmsDbfs
            : targetRms(track.role);
        const auto gainDelta = std::clamp(
            targetRms(track.role) - usableRms,
            -9.0,
            9.0);
        plan.trackAdjustments.push_back({track.id, gainDelta, settings});

        if (std::abs(gainDelta) > 1.0) {
            addSuggestion(
                plan,
                SuggestionKind::gain,
                track.id,
                "Normalize the role before processing",
                "The suggested trim moves this stem toward a conservative role-relative "
                "working level while preserving headroom.",
                std::min(0.95, 0.55 + std::abs(gainDelta) / 20.0));
        }
        if (track.metrics.stereoCorrelation < -0.1) {
            addSuggestion(
                plan,
                SuggestionKind::stereo,
                track.id,
                "Check mono compatibility",
                "Negative stereo correlation can cancel when summed to mono. Narrow or "
                "rebuild only the affected wide layer.",
                0.95);
        }
        if (track.metrics.estimatedTruePeakDbtp > -0.1) {
            addSuggestion(
                plan,
                SuggestionKind::qualityControl,
                track.id,
                "Create peak headroom",
                "This stem reaches the digital ceiling before summing. Lower clip gain "
                "instead of relying on the master limiter.",
                0.98);
        }
        if (isVocal(track.role) && track.metrics.crestFactorDb > 18.0) {
            addSuggestion(
                plan,
                SuggestionKind::dynamics,
                track.id,
                "Use serial vocal control",
                "Large vocal crest suggests clip-gain riding before the fast peak stage; "
                "avoid one compressor doing all levelling.",
                0.82);
        }

        if (track.role == TrackRole::kick)
            kick = &track;
        if (track.role == TrackRole::bass)
            bass = &track;
        if (track.role == TrackRole::cleanVocal || track.role == TrackRole::screamVocal)
            leadVocal = &track;
        if (!isVocal(track.role)) {
            bedRmsSum += track.metrics.rmsDbfs;
            ++bedCount;
        }
    }

    if (kick != nullptr && bass != nullptr
        && std::abs(kick->metrics.spectrum.subDb - bass->metrics.spectrum.subDb) < 3.0) {
        addSuggestion(
            plan,
            SuggestionKind::masking,
            bass->id,
            "Choose a sub owner",
            "Kick and bass have similar sub energy. Keep the bass sustained foundation "
            "or let the kick own the deepest transient, then apply short band-limited ducking.",
            0.88);
    }
    if (leadVocal == nullptr) {
        addSuggestion(
            plan,
            SuggestionKind::qualityControl,
            {},
            "Confirm the lead-vocal role",
            "No stem is tagged as a lead vocal, so vocal-to-bed and presence guardrails "
            "cannot be evaluated.",
            0.75);
    } else if (bedCount > 0) {
        const auto bedAverage = bedRmsSum / static_cast<double>(bedCount);
        const auto vocalToBed = leadVocal->metrics.rmsDbfs - bedAverage;
        if (vocalToBed < -1.5) {
            addSuggestion(
                plan,
                SuggestionKind::masking,
                leadVocal->id,
                "Raise vocal-to-bed clarity",
                "Lead vocal sits below the average bed. Prefer dynamic guitar/synth carve "
                "and a small vocal ride instead of crushing the master bus.",
                0.84);
        } else if (vocalToBed > 6.0) {
            addSuggestion(
                plan,
                SuggestionKind::dynamics,
                leadVocal->id,
                "Blend the vocal into the wall",
                "Lead vocal is far above the bed. Keep intelligibility, but add shared "
                "ambience or lower dry level so the chorus still feels like one production.",
                0.78);
        }
    }

    plan.masterProcessing.amount = 0.65;
    plan.masterProcessing.equalizer.highPassHz = 20.0;
    plan.masterProcessing.compressor.thresholdDb = -12.0;
    plan.masterProcessing.compressor.ratio = 1.5;
    plan.masterProcessing.compressor.attackMs = 30.0;
    plan.masterProcessing.compressor.releaseMs = 120.0;
    plan.masterProcessing.saturation = 0.04;
    plan.masterProcessing.clipCeilingDb = -1.0;

    if (reference) {
        const auto lowDifference =
            reference->spectrum.bassDb - reference->spectrum.midDb;
        double mixLowDifference = 0.0;
        double mixLufs = 0.0;
        if (!project.tracks.empty()) {
            for (const auto& track : project.tracks) {
                mixLowDifference += track.metrics.spectrum.bassDb - track.metrics.spectrum.midDb;
                mixLufs += track.metrics.integratedLufs;
            }
            mixLowDifference /= static_cast<double>(project.tracks.size());
            mixLufs /= static_cast<double>(project.tracks.size());
        }
        plan.masterProcessing.equalizer.lowShelfGainDb =
            std::clamp((lowDifference - mixLowDifference) * 0.15, -1.5, 1.5);
        addSuggestion(
            plan,
            SuggestionKind::tone,
            {},
            "Reference direction applied conservatively",
            "The reference changes only the direction of the broad master contour. "
            "Correction is capped at 1.5 dB and must be judged with loudness-matched A/B.",
            0.72);
        addSuggestion(
            plan,
            SuggestionKind::qualityControl,
            {},
            "Match loudness before judging the reference",
            "Approximate stem-average loudness is "
                + std::to_string(mixLufs)
                + " LUFS versus reference "
                + std::to_string(reference->integratedLufs)
                + " LUFS. Level-match before deciding tonal changes.",
            0.8);
        if (reference->crestFactorDb + 2.0 < 6.0) {
            addSuggestion(
                plan,
                SuggestionKind::dynamics,
                {},
                "Do not chase crushed crest",
                "The reference is denser than a healthy transient target. Preserve kick/snare "
                "shape instead of matching crest factor.",
                0.86);
        }
    }

    return plan;
}

std::string suggestionKindToString(SuggestionKind kind)
{
    switch (kind) {
    case SuggestionKind::gain: return "gain";
    case SuggestionKind::tone: return "tone";
    case SuggestionKind::dynamics: return "dynamics";
    case SuggestionKind::stereo: return "stereo";
    case SuggestionKind::masking: return "masking";
    case SuggestionKind::qualityControl: return "quality-control";
    }
    return "quality-control";
}

std::string toJson(const MixPlan& plan)
{
    nlohmann::json value {
        {"suggestions", nlohmann::json::array()},
        {"trackAdjustments", nlohmann::json::array()}
    };
    for (const auto& suggestion : plan.suggestions) {
        value["suggestions"].push_back({
            {"kind", suggestionKindToString(suggestion.kind)},
            {"trackId", suggestion.trackId},
            {"title", suggestion.title},
            {"explanation", suggestion.explanation},
            {"confidence", suggestion.confidence}
        });
    }
    for (const auto& adjustment : plan.trackAdjustments) {
        value["trackAdjustments"].push_back({
            {"trackId", adjustment.trackId},
            {"gainDeltaDb", adjustment.gainDeltaDb},
            {"amount", adjustment.processing.amount},
            {"highPassHz", adjustment.processing.equalizer.highPassHz},
            {"saturation", adjustment.processing.saturation}
        });
    }
    return value.dump();
}

} // namespace mastering::assistant
