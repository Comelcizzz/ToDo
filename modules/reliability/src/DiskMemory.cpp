#include "mastering/reliability/DiskMemory.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace mastering::reliability {

DiskSpaceEstimate estimateDiskSpace(
    std::string_view outputDirectory,
    std::uint64_t estimatedTempBytes,
    std::uint64_t estimatedFinalBytes)
{
    DiskSpaceEstimate e;
    e.requiredTempBytes = estimatedTempBytes;
    e.requiredFinalBytes = estimatedFinalBytes;
    std::error_code ec;
    const auto space = fs::space(outputDirectory.empty() ? fs::current_path() : fs::path(outputDirectory), ec);
    if (ec) {
        e.availableBytes = 0;
        e.sufficient = false;
        e.warning = "Could not query free disk space";
        return e;
    }
    e.availableBytes = static_cast<std::uint64_t>(space.available);
    const auto need = estimatedTempBytes + estimatedFinalBytes;
    e.sufficient = e.availableBytes > need + (16ull * 1024 * 1024);
    if (!e.sufficient)
        e.warning = "Insufficient disk space for temporary and final outputs";
    return e;
}

bool isPathTooLong(std::string_view path, std::size_t limit) noexcept
{
    return path.size() > limit;
}

bool isValidOutputFilename(std::string_view filename) noexcept
{
    if (filename.empty() || filename == "." || filename == "..")
        return false;
    const std::string_view bad = "<>:\"|?*";
    for (char c : filename) {
        if (bad.find(c) != std::string_view::npos)
            return false;
        if (static_cast<unsigned char>(c) < 32)
            return false;
    }
    return true;
}

LruByteCache::LruByteCache(std::size_t budgetBytes)
    : budget_(budgetBytes)
{
}

void LruByteCache::evictIfNeeded()
{
    const std::size_t limit = lowMemory_ ? budget_ / 2 : budget_;
    while (used_ > limit && !entries_.empty()) {
        auto it = std::min_element(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            if (a.pinned != b.pinned)
                return !a.pinned && b.pinned;
            return a.lastUse < b.lastUse;
        });
        if (it == entries_.end() || it->pinned)
            break;
        used_ -= it->bytes;
        entries_.erase(it);
        ++evictions_;
    }
}

bool LruByteCache::put(const std::string& key, std::size_t bytes, bool pinned)
{
    erase(key);
    if (bytes > budget_)
        return false;
    entries_.push_back(Entry {key, bytes, pinned, ++clock_});
    used_ += bytes;
    evictIfNeeded();
    return true;
}

bool LruByteCache::touch(const std::string& key)
{
    for (auto& e : entries_) {
        if (e.key == key) {
            e.lastUse = ++clock_;
            return true;
        }
    }
    return false;
}

bool LruByteCache::erase(const std::string& key)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->key == key) {
            used_ -= it->bytes;
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

void LruByteCache::clear()
{
    entries_.clear();
    used_ = 0;
}

void LruByteCache::setLowMemoryMode(bool enabled)
{
    lowMemory_ = enabled;
    evictIfNeeded();
}

CacheStats LruByteCache::stats() const noexcept
{
    CacheStats s;
    s.entries = entries_.size();
    s.bytesUsed = used_;
    s.bytesBudget = budget_;
    s.evictions = evictions_;
    s.lowMemoryMode = lowMemory_;
    return s;
}

std::string serializeCacheStats(const CacheStats& stats)
{
    nlohmann::json j {
        {"entries", stats.entries},
        {"bytesUsed", stats.bytesUsed},
        {"bytesBudget", stats.bytesBudget},
        {"evictions", stats.evictions},
        {"lowMemoryMode", stats.lowMemoryMode}};
    return j.dump(2);
}

std::string serializeDiskEstimate(const DiskSpaceEstimate& e)
{
    nlohmann::json j {
        {"requiredTempBytes", e.requiredTempBytes},
        {"requiredFinalBytes", e.requiredFinalBytes},
        {"availableBytes", e.availableBytes},
        {"sufficient", e.sufficient},
        {"warning", e.warning}};
    return j.dump(2);
}

} // namespace mastering::reliability
