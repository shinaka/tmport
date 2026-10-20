#pragma once
// The method-3 compression of the MRn archives (re/gg/docs/fmt_data.md §3): a static canonical
// Huffman code over literals and match lengths, a second one over distance high parts, and LZ77
// copies within the output. Lzh_Decompress 1b48:02aa (bit reader 1b48:0080 / 00af, code lengths
// 1b48:0268, default distance lengths 1b48:021f, explicit ones 1b48:028e, code builder 1b48:0109).
//
// Stream: u32 size word (bits 0..29 the unpacked size, bits 30..31 set in every entry), then an
// MSB-first bit stream: 1 window bit (W = 12 + bit, D = 2^(W-6) distance codes), 16 ignored bits
// (the token count in every entry), 286 literal/length code lengths (1 bit present, then 4 bits
// length - 1), 1 bit explicit distances (D x 4 bits) or the default row W - 12 of DS:13D0, then
// tokens until `size` bytes are out: symbol 0..255 literal, 256..284 length s - 253, 285 length
// 32 + 8 bits; distance = code * 64 + 6 bits + 1.
//
// The game builds each code as a tree and fails to terminate on an incomplete code; this decoder
// uses an equivalent canonical table decoder and rejects such codes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tmport::gg {

class GgExe;

// The default distance code lengths, DS:13D0 (g_lzhDefaultDistLengths): 3 rows of 16 words, row
// W - 12. Word 0 of a row is the length of distance code 0; every following word is a code number
// at which the length grows by one. Read from the user's ssg.exe (never stored in the engine).
struct LzhDistanceRows {
    static constexpr uint16_t kDsOffset = 0x13D0;
    static constexpr size_t kRows = 3;
    static constexpr size_t kWordsPerRow = 16;
    std::array<uint16_t, kRows * kWordsPerRow> words{};

    static LzhDistanceRows fromExe(const GgExe& exe);
    // 1b48:021f: one length per distance code of window W (12 or 13). The scan of the "grows here"
    // words runs on through the following rows as the original's does (the zero words that end a
    // row never match a code above 0).
    std::vector<uint8_t> lengths(int windowBits) const;
};

// What a stream held (for the self-test, the CLI and the tests).
struct LzhInfo {
    uint32_t size = 0;            // unpacked size (size word & 0x3FFFFFFF)
    uint8_t sizeHighBits = 0;     // size word bits 30..31 (3 in every entry)
    int windowBits = 0;           // 12 or 13 (13 in every entry)
    uint16_t ignoredWord = 0;     // the 16 bits the game skips (= tokens mod 65536 in every entry)
    bool explicitDistances = false;
    size_t usedBytes = 0;         // bytes of the stream used, size word included
    uint32_t literals = 0;
    uint32_t matches = 0;
    uint32_t maxDistance = 0;
};

// Decodes one method-3 stream (the payload of an entry after its type and method bytes). Throws
// tmport::FormatError for a stream that the original would misread or never finish: an incomplete
// or over-full code, a match before the start of the output or past the stated size, a stream
// that reads past its end, or a size above kLzhMaxSize.
inline constexpr uint32_t kLzhMaxSize = 0x1000000;  // the original's buffer is 0xFFFF bytes; mods may be larger
std::vector<uint8_t> lzhDecompress(std::span<const uint8_t> stream, const LzhDistanceRows& rows,
                                   LzhInfo* info = nullptr);

}  // namespace tmport::gg
