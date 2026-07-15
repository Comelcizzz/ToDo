#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mastering::analysis {

// Analyzer / schema version baked into cache keys (bump when algorithm changes).
inline constexpr int kAnalysisAlgorithmVersion = 3;
inline constexpr int kAnalysisSchemaVersion = 5;

struct AnalysisFingerprint {
    std::string assetId;          // normalized path or stable track id
    std::uint64_t fileSize {0};
    std::uint64_t mtimeHash {0};
    double sampleRate {0.0};
    int channelCount {0};
    std::int64_t sampleCount {0};
    std::string contentFingerprint; // fast start/mid/end hash (hex)
    std::string fullSha256;         // optional strict / offline
    int algorithmVersion {kAnalysisAlgorithmVersion};
    int schemaVersion {kAnalysisSchemaVersion};
    std::string settingsHash;       // analysis settings
    std::string role;
    std::string sectionMapHash;

    [[nodiscard]] std::string cacheKey() const;
};

// Fast content fingerprint: SHA-256 over head + mid + tail sample bytes.
[[nodiscard]] std::string fastContentFingerprint(
    const float* samples,
    std::size_t sampleCount,
    int channelCount = 1);

[[nodiscard]] std::string hashSettingsBlob(const std::string& settingsJson);
[[nodiscard]] std::string hashSectionMap(const std::string& sectionMapJson);

// Simple analysis-result cache with corruption / partial-write detection.
struct AnalysisCacheEntry {
    std::string cacheKey;
    std::string payloadJson;
    std::string payloadSha256;
    bool complete {false};
};

class AnalysisResultCache {
public:
    void clear();
    [[nodiscard]] bool store(const AnalysisCacheEntry& entry);
    [[nodiscard]] bool load(const std::string& cacheKey, AnalysisCacheEntry& out) const;
    [[nodiscard]] bool validate(const AnalysisCacheEntry& entry) const;

private:
    std::vector<AnalysisCacheEntry> entries_;
};

} // namespace mastering::analysis
