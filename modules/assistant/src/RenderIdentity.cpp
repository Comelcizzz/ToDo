#include "mastering/assistant/RenderIdentity.h"

#include "mastering/analysis/Sha256.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace mastering::assistant {

std::uint64_t RenderIdentityBuilder::fnv1a64(const std::string& data) noexcept
{
    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;
    for (unsigned char c : data) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= kPrime;
    }
    return hash;
}

std::uint64_t RenderIdentityBuilder::fnv1a64File(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return fnv1a64(path);

    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;
    char buffer[4096];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const auto n = static_cast<std::size_t>(in.gcount());
        for (std::size_t i = 0; i < n; ++i) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(buffer[i]));
            hash *= kPrime;
        }
    }
    return hash;
}

std::string RenderIdentityBuilder::hashToHex(std::uint64_t hash)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

std::string RenderIdentityBuilder::sha256Hex(const std::string& data)
{
    return analysis::sha256Hex(data);
}

std::string RenderIdentityBuilder::sha256File(const std::string& path)
{
    return analysis::sha256FileHex(path);
}

bool RenderIdentityBuilder::validateArtifactSha256(
    const std::string& artifactBytes,
    const std::string& expectedSha256) noexcept
{
    if (expectedSha256.empty())
        return false;
    return analysis::sha256Hex(artifactBytes) == expectedSha256;
}

RenderIdentity RenderIdentityBuilder::build(const RenderIdentityInputs& inputs)
{
    RenderIdentity id;
    id.schemaVersion = inputs.schemaVersion.empty() ? "5" : inputs.schemaVersion;

    id.renderGraphId = hashToHex(fnv1a64(inputs.renderGraphJson));
    id.actionGraphId = hashToHex(fnv1a64(inputs.actionGraphJson));
    id.processorStatesId = hashToHex(fnv1a64(inputs.processorStatesJson));
    id.sectionAutomationId = hashToHex(fnv1a64(inputs.sectionAutomationJson));
    id.graphId = hashToHex(fnv1a64(
        inputs.renderGraphJson + "|" + inputs.actionGraphJson + "|"
        + inputs.processorStatesJson + "|" + inputs.sectionAutomationJson));
    id.graphIdAlgorithm = "fnv1a-64";

    // Legacy aliases.
    id.renderGraphHash = id.renderGraphId;
    id.actionGraphHash = id.actionGraphId;
    id.processorStatesHash = id.processorStatesId;
    id.sectionAutomationHash = id.sectionAutomationId;

    id.loudnessMatchGainDb = inputs.loudnessMatchGainDb;
    id.masterSafetySettings = inputs.masterSafetySettingsJson;
    id.integrityAlgorithm = "sha256";
    id.fastFingerprintAlgorithm = "sha256-head-mid-tail";

    if (!inputs.artifactBytes.empty())
        id.artifactSha256 = analysis::sha256Hex(inputs.artifactBytes);
    if (!inputs.manifestJson.empty())
        id.manifestSha256 = analysis::sha256Hex(inputs.manifestJson);

    id.sourceAssetFastFingerprints = inputs.sourceAssetFastFingerprints;

    if (!inputs.sourceAssetHashes.empty()) {
        id.sourceAssetSha256 = inputs.sourceAssetHashes;
    } else {
        for (const auto& path : inputs.sourceAssetPaths)
            id.sourceAssetSha256.push_back(analysis::sha256FileHex(path));
    }

    if (!inputs.referenceHashes.empty()) {
        id.referenceSha256 = inputs.referenceHashes;
    } else {
        for (const auto& path : inputs.referencePaths)
            id.referenceSha256.push_back(analysis::sha256FileHex(path));
    }

    id.sourceAssetHashes = id.sourceAssetSha256;
    id.referenceHashes = id.referenceSha256;

    id.identityJson = toJson(id);
    if (id.manifestSha256.empty())
        id.manifestSha256 = analysis::sha256Hex(id.identityJson);
    return id;
}

std::string RenderIdentityBuilder::toJson(const RenderIdentity& identity)
{
    nlohmann::json j;
    j["schemaVersion"] = identity.schemaVersion;
    j["graphId"] = identity.graphId;
    j["graphIdAlgorithm"] = identity.graphIdAlgorithm;
    j["graphIdNote"] =
        "FNV-1a-64 is a fast deterministic internal identifier only; "
        "it is not cryptographic and not collision-resistant.";
    j["renderGraphId"] = identity.renderGraphId;
    j["actionGraphId"] = identity.actionGraphId;
    j["processorStatesId"] = identity.processorStatesId;
    j["sectionAutomationId"] = identity.sectionAutomationId;
    j["artifactSha256"] = identity.artifactSha256;
    j["manifestSha256"] = identity.manifestSha256;
    j["integrityAlgorithm"] = identity.integrityAlgorithm;
    j["sourceAssetSha256"] = identity.sourceAssetSha256;
    j["referenceSha256"] = identity.referenceSha256;
    j["sourceAssetFastFingerprints"] = identity.sourceAssetFastFingerprints;
    j["fastFingerprintAlgorithm"] = identity.fastFingerprintAlgorithm;
    // Legacy keys for older tooling.
    j["renderGraphHash"] = identity.renderGraphHash;
    j["actionGraphHash"] = identity.actionGraphHash;
    j["processorStatesHash"] = identity.processorStatesHash;
    j["sectionAutomationHash"] = identity.sectionAutomationHash;
    j["sourceAssetHashes"] = identity.sourceAssetHashes;
    j["referenceHashes"] = identity.referenceHashes;
    j["loudnessMatchGainDb"] = identity.loudnessMatchGainDb;
    j["masterSafetySettings"] = identity.masterSafetySettings;
    return j.dump(2);
}

} // namespace mastering::assistant
