#include "mastering/assistant/RenderIdentity.h"

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
        return fnv1a64(path); // path-only fallback when file unreadable

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

RenderIdentity RenderIdentityBuilder::build(const RenderIdentityInputs& inputs)
{
    RenderIdentity id;
    id.schemaVersion = inputs.schemaVersion.empty() ? "3" : inputs.schemaVersion;
    id.renderGraphHash = hashToHex(fnv1a64(inputs.renderGraphJson));
    id.actionGraphHash = hashToHex(fnv1a64(inputs.actionGraphJson));
    id.processorStatesHash = hashToHex(fnv1a64(inputs.processorStatesJson));
    id.sectionAutomationHash = hashToHex(fnv1a64(inputs.sectionAutomationJson));
    id.loudnessMatchGainDb = inputs.loudnessMatchGainDb;
    id.masterSafetySettings = inputs.masterSafetySettingsJson;

    if (!inputs.sourceAssetHashes.empty()) {
        id.sourceAssetHashes = inputs.sourceAssetHashes;
    } else {
        for (const auto& path : inputs.sourceAssetPaths)
            id.sourceAssetHashes.push_back(hashToHex(fnv1a64File(path)));
    }

    if (!inputs.referenceHashes.empty()) {
        id.referenceHashes = inputs.referenceHashes;
    } else {
        for (const auto& path : inputs.referencePaths)
            id.referenceHashes.push_back(hashToHex(fnv1a64File(path)));
    }

    id.identityJson = toJson(id);
    return id;
}

std::string RenderIdentityBuilder::toJson(const RenderIdentity& identity)
{
    nlohmann::json j;
    j["schemaVersion"] = identity.schemaVersion;
    j["renderGraphHash"] = identity.renderGraphHash;
    j["actionGraphHash"] = identity.actionGraphHash;
    j["processorStatesHash"] = identity.processorStatesHash;
    j["sectionAutomationHash"] = identity.sectionAutomationHash;
    j["sourceAssetHashes"] = identity.sourceAssetHashes;
    j["referenceHashes"] = identity.referenceHashes;
    j["loudnessMatchGainDb"] = identity.loudnessMatchGainDb;
    j["masterSafetySettings"] = identity.masterSafetySettings;
    return j.dump();
}

} // namespace mastering::assistant
