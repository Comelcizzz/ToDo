#include "mastering/analysis/AnalysisFingerprint.h"

#include "mastering/analysis/Sha256.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace mastering::analysis {
namespace {

constexpr std::size_t kProbeFrames = 2048;

std::string normalizeAssetId(std::string id)
{
    // Collapse backslashes; trim trailing separators for rename-stability of path form.
    for (char& c : id) {
        if (c == '\\')
            c = '/';
    }
    while (id.size() > 1 && (id.back() == '/' || id.back() == ' '))
        id.pop_back();
    return id;
}

} // namespace

std::string AnalysisFingerprint::cacheKey() const
{
    std::ostringstream oss;
    oss << normalizeAssetId(assetId) << '|'
        << fileSize << '|'
        << std::hex << mtimeHash << std::dec << '|'
        << sampleRate << '|'
        << channelCount << '|'
        << sampleCount << '|'
        << contentFingerprint << '|'
        << (fullSha256.empty() ? "-" : fullSha256) << '|'
        << "alg" << algorithmVersion << '|'
        << "schema" << schemaVersion << '|'
        << (settingsHash.empty() ? "-" : settingsHash) << '|'
        << (role.empty() ? "-" : role) << '|'
        << (sectionMapHash.empty() ? "-" : sectionMapHash);
    return oss.str();
}

std::string fastContentFingerprint(
    const float* samples,
    std::size_t sampleCount,
    int channelCount)
{
    if (samples == nullptr || sampleCount == 0)
        return sha256Hex(std::string_view {"empty"});

    const int ch = std::max(1, channelCount);
    const std::size_t frames = sampleCount / static_cast<std::size_t>(ch);
    const std::size_t probe = std::min(kProbeFrames, frames);

    std::vector<float> probeBuf;
    probeBuf.reserve(probe * 3 * static_cast<std::size_t>(ch));

    auto appendRange = [&](std::size_t startFrame) {
        const std::size_t end = std::min(frames, startFrame + probe);
        for (std::size_t f = startFrame; f < end; ++f) {
            for (int c = 0; c < ch; ++c)
                probeBuf.push_back(samples[f * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)]);
        }
    };

    appendRange(0);
    const std::size_t midStart = frames > probe ? (frames / 2) - (probe / 2) : 0;
    appendRange(midStart);
    const std::size_t tailStart = frames > probe ? frames - probe : 0;
    appendRange(tailStart);

    return sha256Hex(probeBuf.data(), probeBuf.size() * sizeof(float));
}

std::string hashSettingsBlob(const std::string& settingsJson)
{
    return sha256Hex(settingsJson);
}

std::string hashSectionMap(const std::string& sectionMapJson)
{
    return sha256Hex(sectionMapJson);
}

void AnalysisResultCache::clear()
{
    entries_.clear();
}

bool AnalysisResultCache::validate(const AnalysisCacheEntry& entry) const
{
    if (!entry.complete || entry.cacheKey.empty() || entry.payloadJson.empty())
        return false;
    if (entry.payloadSha256.empty())
        return false;
    return entry.payloadSha256 == sha256Hex(entry.payloadJson);
}

bool AnalysisResultCache::store(const AnalysisCacheEntry& entry)
{
    if (!entry.complete)
        return false; // refuse partial writes
    if (!validate(entry))
        return false;
    for (auto& existing : entries_) {
        if (existing.cacheKey == entry.cacheKey) {
            existing = entry;
            return true;
        }
    }
    entries_.push_back(entry);
    return true;
}

bool AnalysisResultCache::load(const std::string& cacheKey, AnalysisCacheEntry& out) const
{
    for (const auto& entry : entries_) {
        if (entry.cacheKey != cacheKey)
            continue;
        if (!validate(entry))
            return false; // corrupted
        out = entry;
        return true;
    }
    return false;
}

} // namespace mastering::analysis
