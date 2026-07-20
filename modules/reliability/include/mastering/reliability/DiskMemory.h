#pragma once

#include "mastering/reliability/TypedError.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mastering::reliability {

struct DiskSpaceEstimate {
    std::uint64_t requiredTempBytes {0};
    std::uint64_t requiredFinalBytes {0};
    std::uint64_t availableBytes {0};
    bool sufficient {false};
    std::string warning;
};

[[nodiscard]] DiskSpaceEstimate estimateDiskSpace(
    std::string_view outputDirectory,
    std::uint64_t estimatedTempBytes,
    std::uint64_t estimatedFinalBytes);

[[nodiscard]] bool isPathTooLong(std::string_view path, std::size_t limit = 240) noexcept;
[[nodiscard]] bool isValidOutputFilename(std::string_view filename) noexcept;

struct MemoryBudgetConfig {
    std::size_t waveformCacheBytes {64ull * 1024 * 1024};
    std::size_t analysisCacheBytes {128ull * 1024 * 1024};
    std::size_t decodedChunkBytes {32ull * 1024 * 1024};
    std::size_t experimentResultBytes {64ull * 1024 * 1024};
    std::size_t renderBufferBytes {128ull * 1024 * 1024};
    std::size_t undoSnapshotBytes {64ull * 1024 * 1024};
};

struct CacheStats {
    std::size_t entries {0};
    std::size_t bytesUsed {0};
    std::size_t bytesBudget {0};
    std::size_t evictions {0};
    bool lowMemoryMode {false};
};

class LruByteCache {
public:
    explicit LruByteCache(std::size_t budgetBytes);

    bool put(const std::string& key, std::size_t bytes, bool pinned = false);
    bool touch(const std::string& key);
    bool erase(const std::string& key);
    void clear();
    void setLowMemoryMode(bool enabled);
    [[nodiscard]] CacheStats stats() const noexcept;

private:
    struct Entry {
        std::string key;
        std::size_t bytes {0};
        bool pinned {false};
        std::uint64_t lastUse {0};
    };
    std::size_t budget_ {0};
    std::size_t used_ {0};
    std::uint64_t clock_ {0};
    std::size_t evictions_ {0};
    bool lowMemory_ {false};
    std::vector<Entry> entries_;
    void evictIfNeeded();
};

[[nodiscard]] std::string serializeCacheStats(const CacheStats& stats);
[[nodiscard]] std::string serializeDiskEstimate(const DiskSpaceEstimate& e);

} // namespace mastering::reliability
