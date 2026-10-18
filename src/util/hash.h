#pragma once
// FNV-1a 64-bit hash (used by tests and debug output to compare data without storing it).

#include <cstdint>
#include <span>

namespace tmport {

inline constexpr uint64_t kFnv64Offset = 0xcbf29ce484222325ull;
inline constexpr uint64_t kFnv64Prime = 0x100000001b3ull;

inline uint64_t fnv1a64(std::span<const uint8_t> data, uint64_t h = kFnv64Offset) {
    for (uint8_t b : data) {
        h ^= b;
        h *= kFnv64Prime;
    }
    return h;
}

// Hashes the little-endian bytes of a 16-bit value.
inline uint64_t fnv1a64U16(uint16_t v, uint64_t h) {
    const uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
    return fnv1a64(b, h);
}

}  // namespace tmport
