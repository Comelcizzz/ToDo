#pragma once

#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/dsp/DynamicEq.h"
#include "mastering/dsp/ProcessorChain.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::project {

inline constexpr int kCurrentSchemaVersion = 4;
inline constexpr int kMinSupportedSchemaVersion = 1;

enum class TrackRole {
    custom,
    drums,
    kick,
    snare,
    toms,
    cymbals,
    drumBus,
    bass,
    bassBus,
    rhythmGuitar,
    rhythmGuitarLeft,
    rhythmGuitarRight,
    leadGuitar,
    cleanGuitar,
    guitarBus,
    cleanVocal,
    screamVocal,
    backingVocal,
    vocalBus,
    synth,
    orchestra,
    effects,
    musicBus,
    master
};

enum class SectionKind {
    intro,
    verse,
    preChorus,
    chorus,
    breakdown,
    bridge,
    outro,
    custom
};

struct TrackRecord {
    std::string id;
    std::string name;
    std::string audioPath;
    TrackRole role {TrackRole::custom};
    analysis::AudioMetrics metrics;
    dsp::ProcessorSettings processing;
    dsp::DynamicEqState dynamicEq {};
    bool dynamicEqEnabled {false};
    bool vocalRiderEnabled {false};
    double vocalRiderTargetDb {-18.0};
    double gainDb {0.0};
    double pan {0.0};
    bool muted {false};
    bool soloed {false};
    bool polarityInverted {false};
    std::string pairId;
    std::string parentBusId;
    std::string channelPosition {"Stereo"}; // Mono|L|R|C|Stereo
};

struct PairRecord {
    std::string id;
    std::string name;
    std::string leftTrackId;
    std::string rightTrackId;
    std::string parentBusId;
    bool linkedProcessing {true};
};

struct BusRecord {
    std::string id;
    std::string name;
    TrackRole role {TrackRole::custom};
    std::vector<std::string> childTrackIds;
    std::vector<std::string> childPairIds;
    dsp::ProcessorSettings processing;
    dsp::DynamicEqState dynamicEq {};
    bool dynamicEqEnabled {false};
    double gainDb {0.0};
};

struct SectionMarker {
    std::string id;
    SectionKind kind {SectionKind::custom};
    std::string name;
    double startSeconds {0.0};
    double endSeconds {0.0};
};

// Typed Mix Pass Action — absolute DSP targets (idempotent Apply).
struct MixPassAction {
    std::string actionId;
    std::uint32_t actionVersion {1};
    std::string problemType;
    std::string targetTrackId;
    std::string targetPairId;
    std::string targetBusId;
    std::string processorId;  // gain|staticEq|dynamicEq|fdSidechain|compressor|compressorPeak|saturation|vocalRider|outputGain
    std::string parameterId;
    double currentValue {0.0};
    double proposedValue {0.0};
    double allowedMin {-24.0};
    double allowedMax {24.0};
    double globalCap {24.0};
    double roleCap {12.0};
    double confidenceAdjustedCap {12.0};
    double cumulativeCap {12.0};
    double confidence {0.0}; // legacy field; prefer evidenceScore
    double evidenceScore {0.0}; // 0..1 deterministic evidence, NOT calibrated probability
    std::string evidenceLabel {"low"}; // low|medium|high
    std::string explanation;
    std::string sourceMetrics;
    std::string evidence;
    std::string decisionTrace;
    std::string processingLevel {"track"}; // track|track-left|track-right|pair|bus|music-bus|master|none
    std::string sectionScope {"full"};
    std::string state {"pending"}; // pending|previewing|applied|rejected|cancelled|superseded|edited
    std::string origin {"mixpass"};
    int priority {50};
    int orderIndex {0};
    std::string conflictGroup;
    std::string supersedes;
    std::string prerequisite;

    dsp::ProcessorSettings proposedProcessing {};
    dsp::DynamicEqState proposedDynamicEq {};
    bool hasProposedDynamicEq {false};
    bool hasProposedProcessing {false};
    bool vocalRiderEnabled {false};
    double vocalRiderTargetDb {-18.0};

    double previousGainDb {0.0};
    dsp::ProcessorSettings previousProcessing {};
    dsp::DynamicEqState previousDynamicEq {};
    bool previousDynamicEqEnabled {false};
    bool hasPrevious {false};
};

struct AppliedAction {
    std::string actionId;
    std::string trackId;
    double targetGainDb {0.0};
    std::string state {"pending"};
    dsp::ProcessorSettings processing;
    double previousGainDb {0.0};
    dsp::ProcessorSettings previousProcessing;
    bool hasPrevious {false};
    // M3A extensions (optional in older files).
    std::string problemType;
    std::string processorId;
    std::string sectionScope {"full"};
};

struct ProjectDocument {
    int schemaVersion {kCurrentSchemaVersion};
    std::string id;
    std::string name {"Untitled Mix"};
    std::string referencePath;
    double sampleRate {48'000.0};
    double bpm {140.0};
    std::vector<TrackRecord> tracks;
    std::vector<PairRecord> pairs;
    std::vector<BusRecord> buses;
    std::vector<SectionMarker> sections;
    std::vector<MixPassAction> mixPassActions;
    dsp::ProcessorSettings masterProcessing;
    dsp::DynamicEqState masterDynamicEq {};
    bool masterDynamicEqEnabled {false};
    std::vector<AppliedAction> actions; // legacy + thin index
    std::string selectedVariant {"balanced"};
    // M3B section automation lanes (relative offsets).
    std::string sectionAutomationJson; // serialized SectionAutomationState
};

struct DeserializeError {
    std::string message;
};

[[nodiscard]] std::string roleToString(TrackRole role);
[[nodiscard]] std::optional<TrackRole> roleFromString(std::string_view role);
[[nodiscard]] TrackRole inferRoleFromFilename(std::string_view filename);
[[nodiscard]] std::string sectionKindToString(SectionKind kind);
[[nodiscard]] std::optional<SectionKind> sectionKindFromString(std::string_view value);
[[nodiscard]] std::string makeProjectId();
[[nodiscard]] std::string serialize(const ProjectDocument& project);
[[nodiscard]] std::optional<ProjectDocument> deserialize(std::string_view json);
[[nodiscard]] std::optional<ProjectDocument> deserialize(
    std::string_view json,
    DeserializeError& error);

} // namespace mastering::project
