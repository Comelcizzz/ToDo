#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mastering::assistant {

struct RenderIdentityInputs {
    std::string schemaVersion {"5"};
    std::string renderGraphJson;
    std::string actionGraphJson;
    std::string processorStatesJson;
    std::string sectionAutomationJson;
    std::vector<std::string> sourceAssetPaths;
    std::vector<std::string> referencePaths;
    std::vector<std::string> sourceAssetHashes; // optional precomputed SHA-256 preferred
    std::vector<std::string> referenceHashes;   // optional precomputed SHA-256 preferred
    std::vector<std::string> sourceAssetFastFingerprints; // optional fast content fingerprints
    double loudnessMatchGainDb {0.0};
    std::string masterSafetySettingsJson;
    // Artifact bytes for integrity (WAV/JSON). Empty skips artifactSha256.
    std::string artifactBytes;
    std::string manifestJson;
};

struct RenderIdentity {
    std::string schemaVersion;
    // Deterministic internal graph identifiers — FNV-1a 64-bit.
    // NOT cryptographic and NOT collision-resistant against adversarial input.
    std::string graphId;
    std::string renderGraphId;
    std::string actionGraphId;
    std::string processorStatesId;
    std::string sectionAutomationId;
    std::string graphIdAlgorithm {"fnv1a-64"};

    // Artifact / source integrity — SHA-256.
    std::string artifactSha256;
    std::string manifestSha256;
    std::vector<std::string> sourceAssetSha256;
    std::vector<std::string> referenceSha256;
    std::vector<std::string> sourceAssetFastFingerprints;
    std::string integrityAlgorithm {"sha256"};
    std::string fastFingerprintAlgorithm {"sha256-head-mid-tail"};

    // Legacy aliases (same as *Id FNV fields) for older readers.
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
    // FNV-1a 64-bit over UTF-8 string bytes — fast deterministic graph ID only.
    [[nodiscard]] static std::uint64_t fnv1a64(const std::string& data) noexcept;
    [[nodiscard]] static std::uint64_t fnv1a64File(const std::string& path);

    [[nodiscard]] static std::string hashToHex(std::uint64_t hash);
    [[nodiscard]] static std::string sha256Hex(const std::string& data);
    [[nodiscard]] static std::string sha256File(const std::string& path);

    [[nodiscard]] static RenderIdentity build(const RenderIdentityInputs& inputs);
    [[nodiscard]] static std::string toJson(const RenderIdentity& identity);

    // Validate WAV/JSON pairing via SHA-256.
    [[nodiscard]] static bool validateArtifactSha256(
        const std::string& artifactBytes,
        const std::string& expectedSha256) noexcept;
};

} // namespace mastering::assistant
