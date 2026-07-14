#pragma once

#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/dsp/ProcessorChain.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::project {

inline constexpr int kCurrentSchemaVersion = 2;
inline constexpr int kMinSupportedSchemaVersion = 1;

enum class TrackRole {
    custom,
    drums,
    kick,
    snare,
    toms,
    cymbals,
    bass,
    rhythmGuitar,
    leadGuitar,
    cleanVocal,
    screamVocal,
    backingVocal,
    synth,
    orchestra,
    effects
};

struct TrackRecord {
    std::string id;
    std::string name;
    std::string audioPath;
    TrackRole role {TrackRole::custom};
    analysis::AudioMetrics metrics;
    dsp::ProcessorSettings processing;
    double gainDb {0.0};
    double pan {0.0};
    bool muted {false};
    bool soloed {false};
    bool polarityInverted {false};
};

struct AppliedAction {
    std::string actionId;
    std::string trackId;
    double targetGainDb {0.0};
    std::string state {"pending"};
};

struct ProjectDocument {
    int schemaVersion {kCurrentSchemaVersion};
    std::string id;
    std::string name {"Untitled Mix"};
    std::string referencePath;
    double sampleRate {48'000.0};
    std::vector<TrackRecord> tracks;
    dsp::ProcessorSettings masterProcessing;
    std::vector<AppliedAction> actions;
    std::string selectedVariant {"balanced"};
};

struct DeserializeError {
    std::string message;
};

[[nodiscard]] std::string roleToString(TrackRole role);
[[nodiscard]] std::optional<TrackRole> roleFromString(std::string_view role);
[[nodiscard]] TrackRole inferRoleFromFilename(std::string_view filename);
[[nodiscard]] std::string makeProjectId();
[[nodiscard]] std::string serialize(const ProjectDocument& project);
[[nodiscard]] std::optional<ProjectDocument> deserialize(std::string_view json);
[[nodiscard]] std::optional<ProjectDocument> deserialize(
    std::string_view json,
    DeserializeError& error);

} // namespace mastering::project
