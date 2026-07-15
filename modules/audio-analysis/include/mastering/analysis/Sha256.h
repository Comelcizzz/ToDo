#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mastering::analysis {

// Compact SHA-256 for artifact integrity (not a general crypto library).
[[nodiscard]] std::string sha256Hex(const void* data, std::size_t size);
[[nodiscard]] std::string sha256Hex(std::string_view data);
[[nodiscard]] std::string sha256Hex(const std::vector<std::uint8_t>& data);
[[nodiscard]] std::string sha256FileHex(const std::string& path);

} // namespace mastering::analysis
