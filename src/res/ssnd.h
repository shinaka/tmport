#pragma once
// SSND header (re/docs/fmt_sound.md §3.2): u32 reserved (0), u32 sample rate, u32 sample count,
// then 8-bit unsigned mono PCM.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tmport {

struct SsndInfo {
    uint32_t reserved = 0;
    uint32_t sampleRate = 0;
    uint32_t sampleCount = 0;   // as stored
    size_t pcmOffset = 12;
    size_t pcmLength = 0;       // min(sampleCount, bytes available)
    bool truncated = false;     // sampleCount exceeds the resource

    std::span<const uint8_t> pcm(std::span<const uint8_t> resource) const {
        return resource.subspan(pcmOffset, pcmLength);
    }
};

// nullopt if the resource is shorter than the 12-byte header.
std::optional<SsndInfo> parseSsnd(std::span<const uint8_t> resource);

}  // namespace tmport
