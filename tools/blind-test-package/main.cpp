#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::uint64_t fnv1a64(const std::string& s)
{
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

bool copyFile(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

} // namespace

// Package RAW/AUTO/CURRENT renders into a blind A/B/C set with hidden answer key.
// Usage:
//   blind_test_package <outDir> [raw.wav] [auto.wav] [current.wav]
// Defaults: reads from cwd artifacts or provided outDir parent.
int main(int argc, char** argv)
{
    const fs::path outDir = argc > 1 ? fs::path(argv[1]) : fs::path("artifacts/metalcore_engine_validation/blind_test");
    const fs::path rawPath = argc > 2 ? fs::path(argv[2]) : outDir.parent_path() / "raw.wav";
    const fs::path autoPath = argc > 3 ? fs::path(argv[3]) : outDir.parent_path() / "auto.wav";
    const fs::path currentPath = argc > 4 ? fs::path(argv[4]) : outDir.parent_path() / "current.wav";

    if (!fs::exists(rawPath) || !fs::exists(autoPath) || !fs::exists(currentPath)) {
        std::cerr << "blind_test_package: missing raw/auto/current inputs\n"
                  << "  raw=" << rawPath << "\n"
                  << "  auto=" << autoPath << "\n"
                  << "  current=" << currentPath << "\n";
        return 1;
    }

    fs::create_directories(outDir);

    std::array<std::string, 3> labels {"RAW", "AUTO", "CURRENT"};
    const std::array<fs::path, 3> sources {rawPath, autoPath, currentPath};
    const std::string seedMaterial = rawPath.string() + "|" + autoPath.string() + "|" + currentPath.string();
    std::mt19937 rng(static_cast<std::uint32_t>(fnv1a64(seedMaterial) & 0xffffffffu));
    std::shuffle(labels.begin(), labels.end(), rng);

    nlohmann::json answerKey {
        {"disclaimer", "answer_key.json is for evaluators only — do not show to blind listeners."},
        {"mapping", nlohmann::json::object()},
        {"sources", {
            {"RAW", rawPath.string()},
            {"AUTO", autoPath.string()},
            {"CURRENT", currentPath.string()}
        }}
    };

    const char* letters[] = {"A", "B", "C"};
    for (int i = 0; i < 3; ++i) {
        const std::string& label = labels[static_cast<std::size_t>(i)];
        int src = 0;
        if (label == "AUTO")
            src = 1;
        else if (label == "CURRENT")
            src = 2;
        const fs::path dest = outDir / (std::string(letters[i]) + ".wav");
        if (!copyFile(sources[static_cast<std::size_t>(src)], dest)) {
            std::cerr << "blind_test_package: failed to copy to " << dest << "\n";
            return 1;
        }
        answerKey["mapping"][letters[i]] = label;
    }

    {
        std::ofstream(outDir / "answer_key.json") << answerKey.dump(2);
        std::ofstream(outDir / "README.md")
            << "# Blind test package\n\n"
            << "Listen to `A.wav`, `B.wav`, `C.wav` without opening `answer_key.json`.\n"
            << "Files are randomized among RAW / AUTO / CURRENT.\n"
            << "Synthetic or production audio still ≠ automatic musical quality proof.\n";
    }

    std::cout << "blind_test_package wrote " << outDir
              << " mapping=" << answerKey["mapping"].dump() << "\n";
    return 0;
}
