#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mastering::benchmark {

struct ListeningVariantInput {
    std::string identity; // RAW|AUTO_A|AUTO_B|CURRENT|TARGET|REF — never shown to evaluator
    std::string sourcePath;
};

struct ListeningPackageV2 {
    std::string packageId;
    std::string sessionId;
    std::vector<std::string> labels; // A..E randomized
    std::vector<std::string> labeledPaths;
    std::string answerKeyJson; // separate; not in evaluator-facing folder
    std::string formCategoriesJson;
    std::uint64_t seed {0};
};

struct ListeningEvaluation {
    std::string evaluationId;
    std::string sessionId;
    std::string packageId;
    std::vector<std::string> anonymousLabels;
    std::string evaluatorAlias;
    std::string headphonesOptional;
    std::string playbackEnvironmentOptional;
    std::string categoryScoresJson;
    std::string pairwisePreferenceJson;
    double confidence {0.0};
    std::string notes;
    bool fatigueFlag {false};
    std::string timestamp;
    bool answerKeyRevealed {false};
    std::string revealedAnswerKeyJson;
};

[[nodiscard]] ListeningPackageV2 buildListeningPackageV2(
    const std::string& sessionId,
    const std::vector<ListeningVariantInput>& variants,
    const std::string& outputFolder,
    std::uint64_t seed);

[[nodiscard]] std::string serializeListeningEvaluation(const ListeningEvaluation& evaluation);
[[nodiscard]] bool listeningPackageLeaksIdentity(const ListeningPackageV2& package);

inline constexpr const char* kListeningFormCategories[] = {
    "kick impact",
    "bass clarity",
    "low-end stability",
    "guitar weight",
    "guitar clarity",
    "harshness",
    "fizz",
    "vocal presence",
    "scream intelligibility",
    "snare cut",
    "drum energy",
    "chorus lift",
    "breakdown weight",
    "stereo image",
    "mono compatibility",
    "fatigue",
    "overall preference"};

} // namespace mastering::benchmark
