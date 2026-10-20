#include "gg/data/lzh.h"

#include <algorithm>
#include <format>

#include "gg/data/gg_exe.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr int kLitLenSymbols = 0x11E;  // 286 (1b48:0268 loops to 0x11E)
constexpr int kSymLong = 0x11D;        // 285: length 32 + 8 bits
constexpr int kLenBias = 0xFD;         // length = symbol - 0xFD for 256..285
constexpr int kDistLowBits = 6;        // distance = code * 64 + 6 bits + 1
constexpr int kMaxCodeLength = 16;
constexpr int kFastBits = 9;

[[noreturn]] void bad(const std::string& why) { throw FormatError("compressed resource: " + why); }

// MSB-first bit reader (the game's bit buffer 87ba/87bc and its 4000-byte refill 1b48:0051).
// Bytes past the end read as zero; consuming any of their bits is an error (checked at the end).
class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> data) : data_(data) {}

    uint32_t peek(int k) {
        fill(k);
        return static_cast<uint32_t>(acc_ >> (avail_ - k)) & ((1u << k) - 1u);
    }
    void skip(int k) {
        avail_ -= k;
        acc_ &= (avail_ == 0) ? 0 : ((uint64_t{1} << avail_) - 1u);
    }
    uint32_t bits(int k) {
        if (k == 0) return 0;
        const uint32_t v = peek(k);
        skip(k);
        return v;
    }
    size_t consumedBits() const { return fetched_ * 8u - static_cast<size_t>(avail_); }

private:
    void fill(int k) {
        while (avail_ < k) {
            acc_ = (acc_ << 8) | (fetched_ < data_.size() ? data_[fetched_] : 0u);
            ++fetched_;
            avail_ += 8;
        }
    }

    std::span<const uint8_t> data_;
    size_t fetched_ = 0;
    uint64_t acc_ = 0;
    int avail_ = 0;
};

// Canonical Huffman decoder (the code of 1b48:0109 for a complete code): symbols ordered by
// length, then by number; a 9-bit table for short codes, a canonical walk for longer ones.
class Huffman {
public:
    explicit Huffman(std::span<const uint8_t> lengths, const char* what) {
        uint32_t kraft = 0;
        for (const uint8_t len : lengths) {
            if (len > kMaxCodeLength) bad(std::format("{} code length {}", what, len));
            if (len) {
                ++count_[len];
                kraft += 1u << (kMaxCodeLength - len);
            }
        }
        if (kraft != (1u << kMaxCodeLength)) bad(std::format("{} code is not complete", what));
        std::array<uint16_t, kMaxCodeLength + 2> offs{};
        for (int len = 1; len <= kMaxCodeLength; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + count_[len]);
        symbols_.resize(offs[kMaxCodeLength + 1]);
        for (size_t s = 0; s < lengths.size(); ++s)
            if (lengths[s]) symbols_[offs[lengths[s]]++] = static_cast<uint16_t>(s);

        // Fast table: walk the canonical codes of up to kFastBits bits.
        fast_.fill(0);
        uint32_t code = 0;
        size_t index = 0;
        for (int len = 1; len <= kFastBits; ++len) {
            for (int i = 0; i < count_[len]; ++i, ++code, ++index) {
                const uint32_t first = code << (kFastBits - len), n = 1u << (kFastBits - len);
                for (uint32_t j = 0; j < n; ++j)
                    fast_[first + j] = static_cast<uint32_t>(symbols_[index]) | (static_cast<uint32_t>(len) << 16);
            }
            code <<= 1;
        }
    }

    int decode(BitReader& br) const {
        const uint32_t e = fast_[br.peek(kFastBits)];
        if (e != 0) {
            br.skip(static_cast<int>(e >> 16));
            return static_cast<int>(e & 0xFFFF);
        }
        // Canonical walk (puff style): code, first code and index of each length.
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxCodeLength; ++len) {
            code |= static_cast<int>(br.bits(1));
            const int n = count_[len];
            if (code - first < n) return symbols_[static_cast<size_t>(index + code - first)];
            index += n;
            first = (first + n) << 1;
            code <<= 1;
        }
        bad("bad code");  // unreachable for a complete code
    }

private:
    std::array<uint16_t, kMaxCodeLength + 1> count_{};
    std::vector<uint16_t> symbols_;
    std::array<uint32_t, 1u << kFastBits> fast_{};
};

}  // namespace

LzhDistanceRows LzhDistanceRows::fromExe(const GgExe& exe) {
    LzhDistanceRows r;
    for (size_t i = 0; i < r.words.size(); ++i) r.words[i] = exe.ds16(static_cast<uint16_t>(kDsOffset + 2 * i));
    return r;
}

std::vector<uint8_t> LzhDistanceRows::lengths(int windowBits) const {
    if (windowBits < 12 || windowBits > 14) bad(std::format("window of {} bits", windowBits));
    const size_t row = static_cast<size_t>(windowBits - 12) * kWordsPerRow;
    const size_t n = size_t{1} << (windowBits - kDistLowBits);
    std::vector<uint8_t> out(n);
    unsigned cur = words[row];
    size_t j = row + 1;
    for (size_t s = 0; s < n; ++s) {
        while (j < words.size() && words[j] == s) {
            ++cur;
            ++j;
        }
        out[s] = static_cast<uint8_t>(cur);
    }
    return out;
}

std::vector<uint8_t> lzhDecompress(std::span<const uint8_t> stream, const LzhDistanceRows& rows, LzhInfo* infoOut) {
    if (stream.size() < 4) bad("shorter than its size word");
    LzhInfo info;
    const uint32_t sizeWord = static_cast<uint32_t>(stream[0]) | (static_cast<uint32_t>(stream[1]) << 8) |
                              (static_cast<uint32_t>(stream[2]) << 16) | (static_cast<uint32_t>(stream[3]) << 24);
    info.size = sizeWord & 0x3FFFFFFFu;  // 1b48:02cf: AND 0x3FFF on the high word
    info.sizeHighBits = static_cast<uint8_t>(sizeWord >> 30);
    if (info.size > kLzhMaxSize) bad(std::format("unpacked size {} too large", info.size));

    BitReader br(stream.subspan(4));
    info.windowBits = 12 + static_cast<int>(br.bits(1));
    const uint32_t distCodes = 1u << (info.windowBits - kDistLowBits);
    info.ignoredWord = static_cast<uint16_t>(br.bits(16));

    std::array<uint8_t, kLitLenSymbols> litLen{};
    for (auto& len : litLen) len = br.bits(1) ? static_cast<uint8_t>(br.bits(4) + 1) : uint8_t{0};
    const Huffman lit(litLen, "literal/length");

    info.explicitDistances = br.bits(1) != 0;
    std::vector<uint8_t> distLen;
    if (info.explicitDistances) {
        distLen.resize(distCodes);
        for (auto& len : distLen) len = static_cast<uint8_t>(br.bits(4));
    } else {
        distLen = rows.lengths(info.windowBits);
    }
    const Huffman dist(distLen, "distance");

    std::vector<uint8_t> out;
    out.reserve(info.size);
    while (out.size() < info.size) {
        int s = lit.decode(br);
        if (s < 256) {
            out.push_back(static_cast<uint8_t>(s));
            ++info.literals;
            continue;
        }
        if (s == kSymLong) s += static_cast<int>(br.bits(8));
        const size_t length = static_cast<size_t>(s - kLenBias);
        const auto distCode = static_cast<uint32_t>(dist.decode(br));  // the code first, then its 6 low bits
        const uint32_t distance = distCode * 64u + br.bits(kDistLowBits) + 1u;
        if (distance > out.size()) bad("a match reaches before the start");
        if (length > info.size - out.size()) bad("a match runs past the end");
        ++info.matches;
        info.maxDistance = std::max(info.maxDistance, distance);
        const size_t from = out.size() - distance;
        for (size_t i = 0; i < length; ++i) out.push_back(out[from + i]);  // byte by byte: overlaps repeat
    }
    const size_t usedBits = br.consumedBits();
    if (usedBits > (stream.size() - 4) * 8u) bad("the stream reads past its end");
    info.usedBytes = 4 + (usedBits + 7) / 8;
    if (infoOut) *infoOut = info;
    return out;
}

}  // namespace tmport::gg
