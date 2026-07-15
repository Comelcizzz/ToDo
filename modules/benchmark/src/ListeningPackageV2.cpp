#include "mastering/benchmark/ListeningPackageV2.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

namespace mastering::benchmark {
namespace {

namespace fs = std::filesystem;

char labelForIndex(int i)
{
    return static_cast<char>('A' + i);
}

} // namespace

ListeningPackageV2 buildListeningPackageV2(
    const std::string& sessionId,
    const std::vector<ListeningVariantInput>& variants,
    const std::string& outputFolder,
    std::uint64_t seed)
{
    ListeningPackageV2 pkg;
    pkg.packageId = "listen-" + sessionId;
    pkg.sessionId = sessionId;
    pkg.seed = seed;

    auto order = variants;
    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
    std::shuffle(order.begin(), order.end(), rng);

    fs::create_directories(outputFolder);
    nlohmann::json answer;
    answer["packageId"] = pkg.packageId;
    answer["sessionId"] = sessionId;
    answer["seed"] = seed;
    answer["mapping"] = nlohmann::json::array();

    nlohmann::json cats = nlohmann::json::array();
    for (const char* c : kListeningFormCategories)
        cats.push_back(c);
    pkg.formCategoriesJson = cats.dump(2);

    for (std::size_t i = 0; i < order.size() && i < 5; ++i) {
        const std::string label(1, labelForIndex(static_cast<int>(i)));
        pkg.labels.push_back(label);
        const auto dest = fs::path(outputFolder) / (label + ".wav");
        // Copy bytes if source exists; otherwise write placeholder marker file for tests.
        std::ifstream in(order[i].sourcePath, std::ios::binary);
        std::ofstream out(dest, std::ios::binary);
        if (in && out)
            out << in.rdbuf();
        else if (out)
            out << "LISTENING_PLACEHOLDER";
        pkg.labeledPaths.push_back(dest.string());
        answer["mapping"].push_back({{"label", label}, {"identity", order[i].identity}});
    }
    pkg.answerKeyJson = answer.dump(2);

    // Write evaluator README without identities.
    std::ofstream readme(fs::path(outputFolder) / "README.md");
    readme << "# Blind listening package v2\n\n"
           << "Labels are randomized. Do not infer identity from filenames.\n"
           << "Answer key is stored separately and must not be shown before completion.\n";
    return pkg;
}

std::string serializeListeningEvaluation(const ListeningEvaluation& e)
{
    nlohmann::json j;
    j["evaluationId"] = e.evaluationId;
    j["sessionId"] = e.sessionId;
    j["packageId"] = e.packageId;
    j["anonymousLabels"] = e.anonymousLabels;
    j["evaluatorAlias"] = e.evaluatorAlias;
    j["headphonesOptional"] = e.headphonesOptional;
    j["playbackEnvironmentOptional"] = e.playbackEnvironmentOptional;
    j["categoryScoresJson"] = e.categoryScoresJson;
    j["pairwisePreferenceJson"] = e.pairwisePreferenceJson;
    j["confidence"] = e.confidence;
    j["notes"] = e.notes;
    j["fatigueFlag"] = e.fatigueFlag;
    j["timestamp"] = e.timestamp;
    j["answerKeyRevealed"] = e.answerKeyRevealed;
    if (e.answerKeyRevealed)
        j["revealedAnswerKeyJson"] = e.revealedAnswerKeyJson;
    return j.dump(2);
}

bool listeningPackageLeaksIdentity(const ListeningPackageV2& package)
{
    const std::string hay = package.answerKeyJson; // answer key may contain identity — OK
    for (const auto& path : package.labeledPaths) {
        const auto name = fs::path(path).filename().string();
        if (name.find("RAW") != std::string::npos || name.find("AUTO") != std::string::npos
            || name.find("CURRENT") != std::string::npos || name.find("TARGET") != std::string::npos
            || name.find("REF") != std::string::npos)
            return true;
    }
    for (const auto& label : package.labels) {
        if (label.find("RAW") != std::string::npos)
            return true;
    }
    (void) hay;
    return false;
}

} // namespace mastering::benchmark
