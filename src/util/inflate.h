#pragma once
// DEFLATE decompression (RFC 1951) and the zlib wrapper (RFC 1950): the PNG reader of the mod layer
// (mods/png_codec.h) and the DOSBox-X save-state reader (dos/save/dosbox_state.h, a zip archive of
// deflated entries) share it.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tmport {

// What happens when the output would grow past maxOutput.
enum class InflateLimit : uint8_t {
    Throw,  // FormatError: the stream is larger than the caller allows
    Stop,   // return the first maxOutput bytes (reading only the start of a large entry)
};

// A raw deflate stream -> bytes. Throws FormatError on corrupt data (and, with InflateLimit::Throw,
// when the output would exceed maxOutput bytes).
std::vector<uint8_t> inflateRaw(std::span<const uint8_t> data, size_t maxOutput = 256u << 20,
                                InflateLimit limit = InflateLimit::Throw);

// A zlib stream (2-byte header, deflate data, Adler-32 of the output) -> bytes. Throws FormatError
// on a bad header, corrupt data, a checksum mismatch or an output larger than maxOutput.
std::vector<uint8_t> inflateZlib(std::span<const uint8_t> data, size_t maxOutput = 256u << 20);

}  // namespace tmport
