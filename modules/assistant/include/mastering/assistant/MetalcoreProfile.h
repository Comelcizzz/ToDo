#pragma once

#include "mastering/assistant/ActionBudget.h"
#include "mastering/assistant/AutoApplyPolicy.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::assistant {

inline constexpr int kMetalcoreProfileSchemaVersion = 1;

// Hard engine safety caps — profiles cannot exceed these.
struct EngineHardSafetyCaps {
    double maxEqCutDb {12.0};
    double maxGainChangeDb {8.0};
    double maxSectionOffsetDb {4.0};
    double maxUnmaskDb {9.0};
    double maxParallelWet {0.85};
    double maxSideGainDb {6.0};
    double minEvidenceFloor {0.20};
    int maxActionsPerTrack {10};
    int maxDynEqBandsPerTrack {4};
};

struct DetectorThresholds {
    double kickMinSpacingSeconds {0.2};
    double snareMinSpacingSeconds {0.12};
    double kickEvidenceGate {0.4};
    double overlapEqEvidenceGate {0.55};
    double presenceLiftEvidenceGate {0.75};
    double stabilityGate {0.35};
    double guitarBalanceRmsDeltaDb {1.5};
    double guitarBalanceEvidenceGate {0.7};
    double vocalActivityGate {0.08};
    double vocalCrestGateDb {15.0};
    double stereoCorrWide {0.85};
    double stereoCorrNarrow {0.15};
    double syntheticKickFallbackHz {65.0}; // only when allowSyntheticFrequencyFallback
    double syntheticBassFallbackHz {70.0};
};

struct EvidenceWeights {
    double roleCertainty {0.14};
    double signalDuration {0.10};
    double eventCount {0.10};
    double eventConsistency {0.10};
    double spectralProminence {0.14};
    double temporalOverlap {0.08};
    double sectionConsistency {0.08};
    double noiseFloor {0.08};
    double detectorAgreement {0.10};
    double referenceAgreement {0.08};
    double highLabelThreshold {0.65};
    double mediumLabelThreshold {0.35};
};

struct RoleSafeCaps {
    double kickGainDb {4.0};
    double bassGainDb {4.0};
    double guitarDynEqCutDb {6.0};
    double vocalUnmaskCutDb {5.0};
    double snareUnmaskCutDb {4.0};
    double drumBusParallelWet {0.65};
    double sectionGainDb {3.0};
};

struct GuitarPolicy {
    double mudBandHz {220.0};
    double harshBandHz {3500.0};
    double fizzBandHz {7500.0};
    double mudMaxCutDb {4.0};
    double harshMaxCutDb {5.0};
    double fizzMaxCutDb {4.0};
    double dynEqThresholdDb {-24.0};
};

struct VocalPolicy {
    double rideTargetDb {-18.0};
    double deEssHz {7500.0};
    double resonanceMaxCutDb {4.0};
    double unmaskMaxCutDb {5.0};
};

struct DrumPolicy {
    double parallelThresholdDb {-20.0};
    double parallelRatio {4.0};
    double parallelWetDefault {0.45};
    double transientGuardCrestDb {12.0};
};

struct ReferencePolicy {
    double lowMidDeviationDb {3.0};
    double kickToBassDeviationDb {1.5};
    double lowMidCutDb {-1.5};
    double lowMidHz {220.0};
    bool allowAutoApply {false};
};

struct LoudnessComparePolicy {
    double minIntegratedSeconds {3.0};
    double shortTermMinSeconds {1.0};
    double matchGainClampDb {12.0};
};

struct StereoPolicy {
    double defaultLowBandMonoHz {120.0};
    double minCorrelation {-0.2};
    double maxSideGainDb {3.0};
};

struct MasterSafetyPolicy {
    bool limiterEnabled {true};
    double ceilingDbTp {-1.0};
    double lookAheadMs {1.5};
    int oversamplingFactor {4};
};

struct MetalcoreProfile {
    int schemaVersion {kMetalcoreProfileSchemaVersion};
    std::string profileId {"modern-metalcore-balanced"};
    std::string revision {"1"};
    std::string displayName {"Modern Metalcore Balanced"};
    std::string notes;

    DetectorThresholds detectors {};
    EvidenceWeights evidence {};
    RoleSafeCaps roleCaps {};
    ActionBudget budget {};
    AutoApplyPolicyConfig autoApply {};
    GuitarPolicy guitar {};
    VocalPolicy vocal {};
    DrumPolicy drums {};
    ReferencePolicy reference {};
    LoudnessComparePolicy loudness {};
    StereoPolicy stereo {};
    MasterSafetyPolicy masterSafety {};

    // Character: balanced vs aggressive scales some caps within hard limits.
    double aggressiveness {0.5}; // 0..1
};

struct ProfileValidationError {
    std::string field;
    std::string message;
};

[[nodiscard]] EngineHardSafetyCaps engineHardSafetyCaps() noexcept;
[[nodiscard]] MetalcoreProfile defaultBalancedProfile();
[[nodiscard]] MetalcoreProfile defaultAggressiveProfile();
[[nodiscard]] MetalcoreProfile customProfileFrom(const MetalcoreProfile& base);

// Clamp profile to hard safety caps (mutates). Returns false if any field was clamped.
bool clampProfileToHardCaps(MetalcoreProfile& profile, std::vector<std::string>& clampedFields);

[[nodiscard]] std::vector<ProfileValidationError> validateProfile(const MetalcoreProfile& profile);
[[nodiscard]] std::string serializeProfile(const MetalcoreProfile& profile);
[[nodiscard]] std::optional<MetalcoreProfile> deserializeProfile(
    std::string_view json,
    std::string* error = nullptr);

// Merge: hard caps ← defaults ← project ← session experimental (later wins, then hard clamp).
[[nodiscard]] MetalcoreProfile resolveProfileHierarchy(
    const MetalcoreProfile& defaults,
    const std::optional<MetalcoreProfile>& projectOverride,
    const std::optional<MetalcoreProfile>& sessionOverride);

} // namespace mastering::assistant
