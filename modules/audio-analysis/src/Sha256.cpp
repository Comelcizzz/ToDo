#include "mastering/analysis/Sha256.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace mastering::analysis {
namespace {

constexpr std::uint32_t rotr(std::uint32_t x, std::uint32_t n) noexcept
{
    return (x >> n) | (x << (32 - n));
}

void sha256Transform(std::uint32_t state[8], const std::uint8_t block[64]) noexcept
{
    static constexpr std::uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16)
            | (std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t temp1 = h + S1 + ch + k[i] + w[i];
        const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

class Sha256Hasher {
public:
    void update(const std::uint8_t* data, std::size_t len) noexcept
    {
        for (std::size_t i = 0; i < len; ++i) {
            block_[blockLen_++] = data[i];
            if (blockLen_ == 64) {
                sha256Transform(state_.data(), block_.data());
                bitLen_ += 512;
                blockLen_ = 0;
            }
        }
    }

    [[nodiscard]] std::array<std::uint8_t, 32> finalize() noexcept
    {
        std::uint64_t totalBits = bitLen_ + std::uint64_t(blockLen_) * 8ull;
        block_[blockLen_++] = 0x80;
        if (blockLen_ > 56) {
            while (blockLen_ < 64)
                block_[blockLen_++] = 0;
            sha256Transform(state_.data(), block_.data());
            blockLen_ = 0;
        }
        while (blockLen_ < 56)
            block_[blockLen_++] = 0;
        for (int i = 7; i >= 0; --i)
            block_[blockLen_++] = static_cast<std::uint8_t>((totalBits >> (i * 8)) & 0xffu);
        sha256Transform(state_.data(), block_.data());

        std::array<std::uint8_t, 32> out {};
        for (int i = 0; i < 8; ++i) {
            out[static_cast<std::size_t>(i * 4)] = static_cast<std::uint8_t>((state_[static_cast<std::size_t>(i)] >> 24) & 0xffu);
            out[static_cast<std::size_t>(i * 4 + 1)] = static_cast<std::uint8_t>((state_[static_cast<std::size_t>(i)] >> 16) & 0xffu);
            out[static_cast<std::size_t>(i * 4 + 2)] = static_cast<std::uint8_t>((state_[static_cast<std::size_t>(i)] >> 8) & 0xffu);
            out[static_cast<std::size_t>(i * 4 + 3)] = static_cast<std::uint8_t>(state_[static_cast<std::size_t>(i)] & 0xffu);
        }
        return out;
    }

private:
    std::array<std::uint32_t, 8> state_ {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> block_ {};
    std::size_t blockLen_ {0};
    std::uint64_t bitLen_ {0};
};

std::string toHex(const std::array<std::uint8_t, 32>& digest)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : digest)
        oss << std::setw(2) << static_cast<unsigned>(b);
    return oss.str();
}

} // namespace

std::string sha256Hex(const void* data, std::size_t size)
{
    Sha256Hasher h;
    if (data != nullptr && size > 0)
        h.update(static_cast<const std::uint8_t*>(data), size);
    return toHex(h.finalize());
}

std::string sha256Hex(std::string_view data)
{
    return sha256Hex(data.data(), data.size());
}

std::string sha256Hex(const std::vector<std::uint8_t>& data)
{
    return sha256Hex(data.data(), data.size());
}

std::string sha256FileHex(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return sha256Hex(path);

    Sha256Hasher h;
    std::array<std::uint8_t, 4096> buf {};
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const auto n = static_cast<std::size_t>(in.gcount());
        if (n > 0)
            h.update(buf.data(), n);
    }
    return toHex(h.finalize());
}

} // namespace mastering::analysis
