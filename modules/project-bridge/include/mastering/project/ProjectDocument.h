#pragma once

#include "mastering/analysis/AudioAnalyzer.h"
#include "mastering/dsp/ProcessorChain.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::project {

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

struct ProjectDocument {
    int schemaVersion {1};
    std::string id;
    std::string name {"Untitled Mix"};
    std::string referencePath;
    double sampleRate {48'000.0};
    std::vector<TrackRecord> tracks;
    dsp::ProcessorSettings masterProcessing;
};

[[nodiscard]] std::string roleToString(TrackRole role);
[[nodiscard]] std::optional<TrackRole> roleFromString(std::string_view role);
[[nodiscard]] TrackRole inferRoleFromFilename(std::string_view filename);
[[nodiscard]] std::string makeProjectId();
[[nodiscard]] std::string serialize(const ProjectDocument& project);
[[nodiscard]] std::optional<ProjectDocument> deserialize(std::string_view json);

} // namespace mastering::project
