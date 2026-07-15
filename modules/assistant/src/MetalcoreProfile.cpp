#include "mastering/assistant/MetalcoreProfile.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

namespace mastering::assistant {
namespace {

using json = nlohmann::json;

template <typename T>
void clampInPlace(T& value, T lo, T hi, const char* field, std::vector<std::string>& clamped)
{
    const T before = value;
    value = std::clamp(value, lo, hi);
    if (value != before)
        clamped.emplace_back(field);
}

json detectorsToJson(const DetectorThresholds& d)
{
    return {
        {"kickMinSpacingSeconds", d.kickMinSpacingSeconds},
        {"snareMinSpacingSeconds", d.snareMinSpacingSeconds},
        {"kickEvidenceGate", d.kickEvidenceGate},
        {"overlapEqEvidenceGate", d.overlapEqEvidenceGate},
        {"presenceLiftEvidenceGate", d.presenceLiftEvidenceGate},
        {"stabilityGate", d.stabilityGate},
        {"guitarBalanceRmsDeltaDb", d.guitarBalanceRmsDeltaDb},
        {"guitarBalanceEvidenceGate", d.guitarBalanceEvidenceGate},
        {"vocalActivityGate", d.vocalActivityGate},
        {"vocalCrestGateDb", d.vocalCrestGateDb},
        {"stereoCorrWide", d.stereoCorrWide},
        {"stereoCorrNarrow", d.stereoCorrNarrow},
        {"syntheticKickFallbackHz", d.syntheticKickFallbackHz},
        {"syntheticBassFallbackHz", d.syntheticBassFallbackHz}};
}

void detectorsFromJson(const json& j, DetectorThresholds& d)
{
    if (!j.is_object())
        return;
    d.kickMinSpacingSeconds = j.value("kickMinSpacingSeconds", d.kickMinSpacingSeconds);
    d.snareMinSpacingSeconds = j.value("snareMinSpacingSeconds", d.snareMinSpacingSeconds);
    d.kickEvidenceGate = j.value("kickEvidenceGate", d.kickEvidenceGate);
    d.overlapEqEvidenceGate = j.value("overlapEqEvidenceGate", d.overlapEqEvidenceGate);
    d.presenceLiftEvidenceGate = j.value("presenceLiftEvidenceGate", d.presenceLiftEvidenceGate);
    d.stabilityGate = j.value("stabilityGate", d.stabilityGate);
    d.guitarBalanceRmsDeltaDb = j.value("guitarBalanceRmsDeltaDb", d.guitarBalanceRmsDeltaDb);
    d.guitarBalanceEvidenceGate = j.value("guitarBalanceEvidenceGate", d.guitarBalanceEvidenceGate);
    d.vocalActivityGate = j.value("vocalActivityGate", d.vocalActivityGate);
    d.vocalCrestGateDb = j.value("vocalCrestGateDb", d.vocalCrestGateDb);
    d.stereoCorrWide = j.value("stereoCorrWide", d.stereoCorrWide);
    d.stereoCorrNarrow = j.value("stereoCorrNarrow", d.stereoCorrNarrow);
    d.syntheticKickFallbackHz = j.value("syntheticKickFallbackHz", d.syntheticKickFallbackHz);
    d.syntheticBassFallbackHz = j.value("syntheticBassFallbackHz", d.syntheticBassFallbackHz);
}

} // namespace

EngineHardSafetyCaps engineHardSafetyCaps() noexcept
{
    return {};
}

MetalcoreProfile defaultBalancedProfile()
{
    MetalcoreProfile p;
    p.profileId = "modern-metalcore-balanced";
    p.revision = "1";
    p.displayName = "Modern Metalcore Balanced";
    p.aggressiveness = 0.45;
    p.notes = "Default bounded modern-metalcore profile for M4A.";
    return p;
}

MetalcoreProfile defaultAggressiveProfile()
{
    MetalcoreProfile p = defaultBalancedProfile();
    p.profileId = "modern-metalcore-aggressive";
    p.revision = "1";
    p.displayName = "Modern Metalcore Aggressive";
    p.aggressiveness = 0.8;
    p.budget.maxActionsPerTrack = 8;
    p.budget.maxCumulativeEqCutDb = 9.0;
    p.budget.maxTotalUnmaskDb = 7.0;
    p.roleCaps.guitarDynEqCutDb = 7.0;
    p.roleCaps.vocalUnmaskCutDb = 6.0;
    p.drums.parallelWetDefault = 0.55;
    p.guitar.harshMaxCutDb = 6.0;
    p.autoApply.conservativeEvidence = 0.58;
    p.notes = "More assertive but still clamped by engine hard safety caps.";
    std::vector<std::string> clamped;
    clampProfileToHardCaps(p, clamped);
    return p;
}

MetalcoreProfile customProfileFrom(const MetalcoreProfile& base)
{
    MetalcoreProfile p = base;
    p.profileId = "custom";
    p.displayName = "Custom";
    return p;
}

bool clampProfileToHardCaps(MetalcoreProfile& profile, std::vector<std::string>& clampedFields)
{
    const auto hard = engineHardSafetyCaps();
    const auto before = clampedFields.size();
    clampInPlace(profile.budget.maxCumulativeEqCutDb, 0.0, hard.maxEqCutDb, "budget.maxCumulativeEqCutDb", clampedFields);
    clampInPlace(profile.budget.maxGainChangeDb, 0.0, hard.maxGainChangeDb, "budget.maxGainChangeDb", clampedFields);
    clampInPlace(profile.budget.maxSectionOffsetDb, 0.0, hard.maxSectionOffsetDb, "budget.maxSectionOffsetDb", clampedFields);
    clampInPlace(profile.budget.maxTotalUnmaskDb, 0.0, hard.maxUnmaskDb, "budget.maxTotalUnmaskDb", clampedFields);
    clampInPlace(profile.budget.maxActionsPerTrack, 1, hard.maxActionsPerTrack, "budget.maxActionsPerTrack", clampedFields);
    clampInPlace(profile.budget.maxDynEqBandsPerTrack, 1, hard.maxDynEqBandsPerTrack, "budget.maxDynEqBandsPerTrack", clampedFields);
    clampInPlace(profile.budget.minEvidence, hard.minEvidenceFloor, 1.0, "budget.minEvidence", clampedFields);
    clampInPlace(profile.roleCaps.guitarDynEqCutDb, 0.0, hard.maxEqCutDb, "roleCaps.guitarDynEqCutDb", clampedFields);
    clampInPlace(profile.roleCaps.vocalUnmaskCutDb, 0.0, hard.maxUnmaskDb, "roleCaps.vocalUnmaskCutDb", clampedFields);
    clampInPlace(profile.roleCaps.sectionGainDb, 0.0, hard.maxSectionOffsetDb, "roleCaps.sectionGainDb", clampedFields);
    clampInPlace(profile.drums.parallelWetDefault, 0.0, hard.maxParallelWet, "drums.parallelWetDefault", clampedFields);
    clampInPlace(profile.stereo.maxSideGainDb, 0.0, hard.maxSideGainDb, "stereo.maxSideGainDb", clampedFields);
    clampInPlace(profile.autoApply.lowTechnicalEvidence, hard.minEvidenceFloor, 1.0, "autoApply.lowTechnicalEvidence", clampedFields);
    clampInPlace(profile.aggressiveness, 0.0, 1.0, "aggressiveness", clampedFields);
    return clampedFields.size() > before;
}

std::vector<ProfileValidationError> validateProfile(const MetalcoreProfile& profile)
{
    std::vector<ProfileValidationError> errors;
    if (profile.schemaVersion != kMetalcoreProfileSchemaVersion) {
        errors.push_back({"schemaVersion", "Unsupported profile schemaVersion"});
    }
    if (profile.profileId.empty())
        errors.push_back({"profileId", "profileId required"});
    if (profile.budget.minEvidence < 0.0 || profile.budget.minEvidence > 1.0)
        errors.push_back({"budget.minEvidence", "must be in [0,1]"});
    if (profile.detectors.syntheticKickFallbackHz < 20.0 || profile.detectors.syntheticKickFallbackHz > 200.0)
        errors.push_back({"detectors.syntheticKickFallbackHz", "out of range"});

    MetalcoreProfile copy = profile;
    std::vector<std::string> clamped;
    if (clampProfileToHardCaps(copy, clamped)) {
        for (const auto& field : clamped)
            errors.push_back({field, "exceeds engine hard safety cap (will be clamped)"});
    }
    return errors;
}

std::string serializeProfile(const MetalcoreProfile& profile)
{
    json j;
    j["schemaVersion"] = profile.schemaVersion;
    j["profileId"] = profile.profileId;
    j["revision"] = profile.revision;
    j["displayName"] = profile.displayName;
    j["notes"] = profile.notes;
    j["aggressiveness"] = profile.aggressiveness;
    j["detectors"] = detectorsToJson(profile.detectors);
    j["evidence"] = {
        {"roleCertainty", profile.evidence.roleCertainty},
        {"signalDuration", profile.evidence.signalDuration},
        {"eventCount", profile.evidence.eventCount},
        {"eventConsistency", profile.evidence.eventConsistency},
        {"spectralProminence", profile.evidence.spectralProminence},
        {"temporalOverlap", profile.evidence.temporalOverlap},
        {"sectionConsistency", profile.evidence.sectionConsistency},
        {"noiseFloor", profile.evidence.noiseFloor},
        {"detectorAgreement", profile.evidence.detectorAgreement},
        {"referenceAgreement", profile.evidence.referenceAgreement},
        {"highLabelThreshold", profile.evidence.highLabelThreshold},
        {"mediumLabelThreshold", profile.evidence.mediumLabelThreshold}};
    j["roleCaps"] = {
        {"kickGainDb", profile.roleCaps.kickGainDb},
        {"bassGainDb", profile.roleCaps.bassGainDb},
        {"guitarDynEqCutDb", profile.roleCaps.guitarDynEqCutDb},
        {"vocalUnmaskCutDb", profile.roleCaps.vocalUnmaskCutDb},
        {"snareUnmaskCutDb", profile.roleCaps.snareUnmaskCutDb},
        {"drumBusParallelWet", profile.roleCaps.drumBusParallelWet},
        {"sectionGainDb", profile.roleCaps.sectionGainDb}};
    j["budget"] = {
        {"maxActionsPerTrack", profile.budget.maxActionsPerTrack},
        {"maxDynEqBandsPerTrack", profile.budget.maxDynEqBandsPerTrack},
        {"maxCumulativeEqCutDb", profile.budget.maxCumulativeEqCutDb},
        {"maxGainChangeDb", profile.budget.maxGainChangeDb},
        {"maxSectionOffsetDb", profile.budget.maxSectionOffsetDb},
        {"maxTotalUnmaskDb", profile.budget.maxTotalUnmaskDb},
        {"minEvidence", profile.budget.minEvidence},
        {"preferCorrectiveOverReference", profile.budget.preferCorrectiveOverReference},
        {"maxSectionActionsPerTrack", profile.budget.maxSectionActionsPerTrack}};
    j["autoApply"] = {
        {"lowTechnicalEvidence", profile.autoApply.lowTechnicalEvidence},
        {"conservativeEvidence", profile.autoApply.conservativeEvidence},
        {"allowMusicalAutoApply", profile.autoApply.allowMusicalAutoApply},
        {"musicalEvidence", profile.autoApply.musicalEvidence},
        {"allowReferenceAutoApply", profile.autoApply.allowReferenceAutoApply},
        {"maxCumulativeProcessingDb", profile.autoApply.maxCumulativeProcessingDb},
        {"respectConflictReject", profile.autoApply.respectConflictReject}};
    j["guitar"] = {
        {"mudBandHz", profile.guitar.mudBandHz},
        {"harshBandHz", profile.guitar.harshBandHz},
        {"fizzBandHz", profile.guitar.fizzBandHz},
        {"mudMaxCutDb", profile.guitar.mudMaxCutDb},
        {"harshMaxCutDb", profile.guitar.harshMaxCutDb},
        {"fizzMaxCutDb", profile.guitar.fizzMaxCutDb},
        {"dynEqThresholdDb", profile.guitar.dynEqThresholdDb}};
    j["vocal"] = {
        {"rideTargetDb", profile.vocal.rideTargetDb},
        {"deEssHz", profile.vocal.deEssHz},
        {"resonanceMaxCutDb", profile.vocal.resonanceMaxCutDb},
        {"unmaskMaxCutDb", profile.vocal.unmaskMaxCutDb}};
    j["drums"] = {
        {"parallelThresholdDb", profile.drums.parallelThresholdDb},
        {"parallelRatio", profile.drums.parallelRatio},
        {"parallelWetDefault", profile.drums.parallelWetDefault},
        {"transientGuardCrestDb", profile.drums.transientGuardCrestDb}};
    j["reference"] = {
        {"lowMidDeviationDb", profile.reference.lowMidDeviationDb},
        {"kickToBassDeviationDb", profile.reference.kickToBassDeviationDb},
        {"lowMidCutDb", profile.reference.lowMidCutDb},
        {"lowMidHz", profile.reference.lowMidHz},
        {"allowAutoApply", profile.reference.allowAutoApply}};
    j["loudness"] = {
        {"minIntegratedSeconds", profile.loudness.minIntegratedSeconds},
        {"shortTermMinSeconds", profile.loudness.shortTermMinSeconds},
        {"matchGainClampDb", profile.loudness.matchGainClampDb}};
    j["stereo"] = {
        {"defaultLowBandMonoHz", profile.stereo.defaultLowBandMonoHz},
        {"minCorrelation", profile.stereo.minCorrelation},
        {"maxSideGainDb", profile.stereo.maxSideGainDb}};
    j["masterSafety"] = {
        {"limiterEnabled", profile.masterSafety.limiterEnabled},
        {"ceilingDbTp", profile.masterSafety.ceilingDbTp},
        {"lookAheadMs", profile.masterSafety.lookAheadMs},
        {"oversamplingFactor", profile.masterSafety.oversamplingFactor}};
    return j.dump(2);
}

std::optional<MetalcoreProfile> deserializeProfile(std::string_view source, std::string* error)
{
    try {
        const auto j = json::parse(source);
        MetalcoreProfile p = defaultBalancedProfile();
        p.schemaVersion = j.value("schemaVersion", p.schemaVersion);
        p.profileId = j.value("profileId", p.profileId);
        p.revision = j.value("revision", p.revision);
        p.displayName = j.value("displayName", p.displayName);
        p.notes = j.value("notes", p.notes);
        p.aggressiveness = j.value("aggressiveness", p.aggressiveness);
        if (j.contains("detectors"))
            detectorsFromJson(j.at("detectors"), p.detectors);
        if (const auto it = j.find("budget"); it != j.end() && it->is_object()) {
            p.budget.maxActionsPerTrack = it->value("maxActionsPerTrack", p.budget.maxActionsPerTrack);
            p.budget.maxDynEqBandsPerTrack = it->value("maxDynEqBandsPerTrack", p.budget.maxDynEqBandsPerTrack);
            p.budget.maxCumulativeEqCutDb = it->value("maxCumulativeEqCutDb", p.budget.maxCumulativeEqCutDb);
            p.budget.maxGainChangeDb = it->value("maxGainChangeDb", p.budget.maxGainChangeDb);
            p.budget.maxSectionOffsetDb = it->value("maxSectionOffsetDb", p.budget.maxSectionOffsetDb);
            p.budget.maxTotalUnmaskDb = it->value("maxTotalUnmaskDb", p.budget.maxTotalUnmaskDb);
            p.budget.minEvidence = it->value("minEvidence", p.budget.minEvidence);
            p.budget.preferCorrectiveOverReference =
                it->value("preferCorrectiveOverReference", p.budget.preferCorrectiveOverReference);
            p.budget.maxSectionActionsPerTrack =
                it->value("maxSectionActionsPerTrack", p.budget.maxSectionActionsPerTrack);
        }
        if (const auto it = j.find("autoApply"); it != j.end() && it->is_object()) {
            p.autoApply.lowTechnicalEvidence = it->value("lowTechnicalEvidence", p.autoApply.lowTechnicalEvidence);
            p.autoApply.conservativeEvidence = it->value("conservativeEvidence", p.autoApply.conservativeEvidence);
            p.autoApply.allowMusicalAutoApply = it->value("allowMusicalAutoApply", p.autoApply.allowMusicalAutoApply);
            p.autoApply.musicalEvidence = it->value("musicalEvidence", p.autoApply.musicalEvidence);
            p.autoApply.allowReferenceAutoApply =
                it->value("allowReferenceAutoApply", p.autoApply.allowReferenceAutoApply);
            p.autoApply.maxCumulativeProcessingDb =
                it->value("maxCumulativeProcessingDb", p.autoApply.maxCumulativeProcessingDb);
        }
        if (const auto it = j.find("guitar"); it != j.end() && it->is_object()) {
            p.guitar.mudBandHz = it->value("mudBandHz", p.guitar.mudBandHz);
            p.guitar.harshBandHz = it->value("harshBandHz", p.guitar.harshBandHz);
            p.guitar.fizzBandHz = it->value("fizzBandHz", p.guitar.fizzBandHz);
            p.guitar.mudMaxCutDb = it->value("mudMaxCutDb", p.guitar.mudMaxCutDb);
            p.guitar.harshMaxCutDb = it->value("harshMaxCutDb", p.guitar.harshMaxCutDb);
            p.guitar.fizzMaxCutDb = it->value("fizzMaxCutDb", p.guitar.fizzMaxCutDb);
            p.guitar.dynEqThresholdDb = it->value("dynEqThresholdDb", p.guitar.dynEqThresholdDb);
        }
        // Unknown fields intentionally ignored (forward compatible).
        std::vector<std::string> clamped;
        clampProfileToHardCaps(p, clamped);
        return p;
    } catch (const std::exception& ex) {
        if (error != nullptr)
            *error = ex.what();
        return std::nullopt;
    }
}

MetalcoreProfile resolveProfileHierarchy(
    const MetalcoreProfile& defaults,
    const std::optional<MetalcoreProfile>& projectOverride,
    const std::optional<MetalcoreProfile>& sessionOverride)
{
    MetalcoreProfile resolved = defaults;
    if (projectOverride.has_value()) {
        // Project override replaces typed sections when provided as a full profile document.
        resolved = *projectOverride;
        if (resolved.profileId.empty())
            resolved.profileId = defaults.profileId;
    }
    if (sessionOverride.has_value()) {
        const auto& s = *sessionOverride;
        // Session experimental overrides: take session values for budget/autoApply/detectors.
        resolved.detectors = s.detectors;
        resolved.budget = s.budget;
        resolved.autoApply = s.autoApply;
        resolved.guitar = s.guitar;
        resolved.vocal = s.vocal;
        resolved.drums = s.drums;
        resolved.aggressiveness = s.aggressiveness;
        if (!s.revision.empty())
            resolved.revision = s.revision;
        resolved.notes = s.notes.empty() ? resolved.notes : s.notes;
    }
    std::vector<std::string> clamped;
    clampProfileToHardCaps(resolved, clamped);
    return resolved;
}

} // namespace mastering::assistant
