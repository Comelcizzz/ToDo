#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mastering::assistant {

struct RenderIdentityInputs {
    std::string schemaVersion {"3"};
    std::string renderGraphJson;
    std::string actionGraphJson;
    std::string processorStatesJson;
    std::string sectionAutomationJson;
    std::vector<std::string> sourceAssetPaths;
    std::vector<std::string> referencePaths;
    std::vector<std::string> sourceAssetHashes; // optional precomputed
    std::vector<std::string> referenceHashes;   // optional precomputed
    double loudnessMatchGainDb {0.0};
    std::string masterSafetySettingsJson;
};

struct RenderIdentity {
    std::string schemaVersion;
    std::string renderGraphHash;
    std::string actionGraphHash;
    std::string processorStatesHash;
    std::string sectionAutomationHash;
    std::vector<std::string> sourceAssetHashes;
    std::vector<std::string> referenceHashes;
    double loudnessMatchGainDb {0.0};
    std::string masterSafetySettings;
    std::string identityJson;
};

class RenderIdentityBuilder {
public:
    // FNV-1a 64-bit over UTF-8 string bytes.
    [[nodiscard]] static std::uint64_t fnv1a64(const std::string& data) noexcept;
    [[nodiscard]] static std::uint64_t fnv1a64File(const std::string& path);

    [[nodiscard]] static std::string hashToHex(std::uint64_t hash);

    [[nodiscard]] static RenderIdentity build(const RenderIdentityInputs& inputs);
    [[nodiscard]] static std::string toJson(const RenderIdentity& identity);
};

} // namespace mastering::assistant
