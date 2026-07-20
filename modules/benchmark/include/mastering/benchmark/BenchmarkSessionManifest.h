#pragma once

#include "mastering/project/ProjectDocument.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mastering::benchmark {

inline constexpr int kBenchmarkManifestSchemaVersion = 1;

struct BenchmarkStemEntry {
    std::string assetId;
    std::string filePath;
    std::string contentFingerprint;
    project::TrackRole role {project::TrackRole::custom};
    std::string subRole;
    bool mono {false};
    std::string pairId;
    std::string parentBusId;
    double gainDb {0.0};
    double pan {0.0};
    bool polarityInverted {false};
    double startOffsetSeconds {0.0};
    std::string userNotes;
    int channelCount {0};
    double sampleRate {0.0};
    double durationSeconds {0.0};
};

struct BenchmarkBusEntry {
    std::string id;
    std::string name;
    project::TrackRole role {project::TrackRole::custom};
    std::vector<std::string> childAssetIds;
    std::vector<std::string> childPairIds;
};

struct BenchmarkPairEntry {
    std::string id;
    std::string name;
    std::string leftAssetId;
    std::string rightAssetId;
    std::string parentBusId;
};

struct BenchmarkSectionEntry {
    std::string id;
    project::SectionKind kind {project::SectionKind::custom};
    std::string name;
    double startSeconds {0.0};
    double endSeconds {0.0};
};

struct ExpectedProblemAnnotation {
    std::string problemId;
    std::string category; // kick/bass|vocal-mask|guitar-harsh|snare-lost|width|chorus|bass-unstable|custom
    std::string targetAssetId;
    std::string sectionId; // empty = full
    std::string notes;
    // NEVER fed into Action generation — evaluation only.
};

struct BenchmarkSessionManifest {
    int schemaVersion {kBenchmarkManifestSchemaVersion};
    std::string sessionId;
    std::string projectName;
    std::string artistAlias;
    double bpm {140.0};
    std::string tempoMapJson; // optional
    double sampleRate {48'000.0};
    int bitDepth {24};
    double expectedDurationSeconds {0.0};
    std::string timeSignature {"4/4"};
    std::vector<BenchmarkStemEntry> stems;
    std::vector<BenchmarkPairEntry> pairs;
    std::vector<BenchmarkBusEntry> buses;
    std::vector<BenchmarkSectionEntry> sections;
    std::vector<std::string> referencePaths;
    std::string targetMixPath;
    std::string desiredCharacterNotes;
    std::vector<std::string> excludedProcessors;
    std::vector<ExpectedProblemAnnotation> expectedProblems; // evaluation-only
    bool localOnly {true};
    std::string profileId {"modern-metalcore-balanced"};
    std::string engineVersion;
    std::string createdAt;
};

[[nodiscard]] std::string serializeManifest(const BenchmarkSessionManifest& manifest);
[[nodiscard]] std::optional<BenchmarkSessionManifest> deserializeManifest(
    std::string_view json,
    std::string* error = nullptr);
[[nodiscard]] std::vector<std::string> validateManifestStructure(
    const BenchmarkSessionManifest& manifest);

// Role suggestion from filename — deterministic rules, NOT ML.
[[nodiscard]] project::TrackRole suggestRoleFromFilename(std::string_view filename);
[[nodiscard]] double roleSuggestionConfidence(std::string_view filename, project::TrackRole role);

} // namespace mastering::benchmark
