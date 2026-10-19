// DEFLATE and zlib decompression (inflate.h).
#include "util/inflate.h"

#include <array>

#include "util/error.h"
#include "util/png_writer.h"

namespace tmport {

namespace {

constexpr std::array<uint16_t, 29> kLenBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLenExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistBase = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> d) : d_(d) {}

    void fill(int need) {
        while (cnt_ < need && cnt_ <= 24) {
            uint32_t b = 0;
            if (pos_ < d_.size()) {
                b = d_[pos_++];
            } else {
                ++over_;  // virtual zero bytes past the end (checked on consume)
            }
            buf_ |= b << cnt_;
            cnt_ += 8;
        }
    }
    uint32_t peek(int n) {
        fill(n);
        return buf_ & ((1u << n) - 1);
    }
    void consume(int n) {
        // The virtual zero bytes sit above the real bits; they may be peeked, never consumed.
        if (n > cnt_ - 8 * over_) throw FormatError("deflate: unexpected end of the compressed data");
        buf_ >>= n;
        cnt_ -= n;
    }
    uint32_t bits(int n) {
        if (n == 0) return 0;
        const uint32_t v = peek(n);
        consume(n);
        return v;
    }
    void alignToByte() { consume(cnt_ % 8); }
    // Real bytes consumed so far (call when byte aligned).
    size_t bytePos() const { return pos_ - static_cast<size_t>(cnt_ / 8 - over_); }

private:
    std::span<const uint8_t> d_;
    size_t pos_ = 0;
    uint32_t buf_ = 0;
    int cnt_ = 0;
    int over_ = 0;
};

struct Huffman {
    static constexpr int kFast = 10;
    std::array<uint16_t, 1 << kFast> fast{};  // (len << 9) | symbol, 0 = use the slow path
    std::array<uint16_t, 16> count{};
    std::vector<uint16_t> symbol;

    void build(const uint8_t* lengths, int n) {
        count.fill(0);
        fast.fill(0);
        for (int i = 0; i < n; ++i) count[lengths[i]]++;
        count[0] = 0;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= count[static_cast<size_t>(len)];
            if (left < 0) throw FormatError("deflate: over-subscribed Huffman code");
        }
        std::array<uint16_t, 16> offs{};
        for (int len = 1; len < 15; ++len)
            offs[static_cast<size_t>(len + 1)] = static_cast<uint16_t>(offs[static_cast<size_t>(len)] + count[static_cast<size_t>(len)]);
        symbol.assign(static_cast<size_t>(n), 0);
        for (int i = 0; i < n; ++i)
            if (lengths[i]) symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
        // Canonical codes (RFC 1951 §3.2.2) for the fast table.
        std::array<uint32_t, 16> next{};
        uint32_t code = 0;
        for (int len = 1; len < 16; ++len) {
            code = (code + count[static_cast<size_t>(len - 1)]) << 1;
            next[static_cast<size_t>(len)] = code;
        }
        for (int i = 0; i < n; ++i) {
            const int len = lengths[i];
            if (!len) continue;
            const uint32_t c = next[static_cast<size_t>(len)]++;
            if (len > kFast) continue;
            uint32_t r = 0;
            for (int b = 0; b < len; ++b) r |= ((c >> b) & 1u) << (len - 1 - b);
            for (uint32_t k = r; k < (1u << kFast); k += 1u << len) fast[k] = static_cast<uint16_t>((len << 9) | i);
        }
    }

    int decode(BitReader& br) const {
        const uint16_t e = fast[br.peek(kFast)];
        if (e) {
            br.consume(e >> 9);
            return e & 0x1FF;
        }
        // Slow canonical walk (codes longer than kFast bits).
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= static_cast<int>(br.bits(1));
            const int c = count[static_cast<size_t>(len)];
            if (code - c < first) return symbol[static_cast<size_t>(index + (code - first))];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        throw FormatError("deflate: invalid Huffman code");
    }
};

const Huffman& fixedLit() {
    static const Huffman h = [] {
        std::array<uint8_t, 288> l{};
        for (int i = 0; i < 144; ++i) l[static_cast<size_t>(i)] = 8;
        for (int i = 144; i < 256; ++i) l[static_cast<size_t>(i)] = 9;
        for (int i = 256; i < 280; ++i) l[static_cast<size_t>(i)] = 7;
        for (int i = 280; i < 288; ++i) l[static_cast<size_t>(i)] = 8;
        Huffman x;
        x.build(l.data(), 288);
        return x;
    }();
    return h;
}

const Huffman& fixedDist() {
    static const Huffman h = [] {
        std::array<uint8_t, 30> l{};
        l.fill(5);
        Huffman x;
        x.build(l.data(), 30);
        return x;
    }();
    return h;
}

// The output with its limit: put() returns false once a Stop limit is reached.
class Output {
public:
    Output(size_t maxOutput, InflateLimit limit) : max_(maxOutput), limit_(limit) {}

    std::vector<uint8_t> bytes;

    bool room(size_t n) {
        if (bytes.size() + n <= max_) return true;
        if (limit_ == InflateLimit::Throw) throw FormatError("deflate: output larger than expected");
        full_ = true;
        return false;
    }
    bool full() const { return full_; }
    size_t left() const { return max_ > bytes.size() ? max_ - bytes.size() : 0; }

private:
    size_t max_;
    InflateLimit limit_;
    bool full_ = false;
};

// One compressed block; false when the output limit stopped it.
bool inflateBlock(BitReader& br, Output& out, const Huffman& lit, const Huffman& dist) {
    std::vector<uint8_t>& o = out.bytes;
    for (;;) {
        const int sym = lit.decode(br);
        if (sym < 256) {
            if (!out.room(1)) return false;
            o.push_back(static_cast<uint8_t>(sym));
            continue;
        }
        if (sym == 256) return true;
        const int li = sym - 257;
        if (li >= 29) throw FormatError("deflate: bad length code");
        size_t len = kLenBase[static_cast<size_t>(li)] + br.bits(kLenExtra[static_cast<size_t>(li)]);
        const int ds = dist.decode(br);
        if (ds >= 30) throw FormatError("deflate: bad distance code");
        const size_t d = kDistBase[static_cast<size_t>(ds)] + br.bits(kDistExtra[static_cast<size_t>(ds)]);
        if (d > o.size()) throw FormatError("deflate: distance too far back");
        bool stop = false;
        if (!out.room(len)) {  // InflateLimit::Stop: keep the bytes that fit, then stop
            len = out.left();
            stop = true;
        }
        const size_t from = o.size() - d;
        for (size_t i = 0; i < len; ++i) o.push_back(o[from + i]);
        if (stop) return false;
    }
}

// The deflate blocks from `br` into `out` until the last block (or the output limit).
void inflateStream(BitReader& br, Output& out) {
    bool last = false;
    while (!last && !out.full()) {
        last = br.bits(1) != 0;
        const uint32_t type = br.bits(2);
        if (type == 0) {
            br.alignToByte();
            const uint32_t len = br.bits(16), nlen = br.bits(16);
            if ((len ^ 0xFFFF) != nlen) throw FormatError("deflate: bad stored block length");
            for (uint32_t i = 0; i < len; ++i) {
                if (!out.room(1)) return;
                out.bytes.push_back(static_cast<uint8_t>(br.bits(8)));
            }
        } else if (type == 1) {
            if (!inflateBlock(br, out, fixedLit(), fixedDist())) return;
        } else if (type == 2) {
            const int hlit = static_cast<int>(br.bits(5)) + 257, hdist = static_cast<int>(br.bits(5)) + 1,
                      hclen = static_cast<int>(br.bits(4)) + 4;
            static constexpr std::array<uint8_t, 19> order = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::array<uint8_t, 19> cl{};
            for (int i = 0; i < hclen; ++i) cl[order[static_cast<size_t>(i)]] = static_cast<uint8_t>(br.bits(3));
            Huffman clh;
            clh.build(cl.data(), 19);
            std::array<uint8_t, 320> lens{};
            int n = 0;
            while (n < hlit + hdist) {
                const int s = clh.decode(br);
                if (s < 16) {
                    lens[static_cast<size_t>(n++)] = static_cast<uint8_t>(s);
                    continue;
                }
                int rep = 0;
                uint8_t val = 0;
                if (s == 16) {
                    if (n == 0) throw FormatError("deflate: repeat with no previous length");
                    val = lens[static_cast<size_t>(n - 1)];
                    rep = 3 + static_cast<int>(br.bits(2));
                } else if (s == 17) {
                    rep = 3 + static_cast<int>(br.bits(3));
                } else {
                    rep = 11 + static_cast<int>(br.bits(7));
                }
                if (n + rep > hlit + hdist) throw FormatError("deflate: too many code lengths");
                while (rep--) lens[static_cast<size_t>(n++)] = val;
            }
            if (lens[256] == 0) throw FormatError("deflate: no end-of-block code");
            Huffman lit, dist;
            lit.build(lens.data(), hlit);
            dist.build(lens.data() + hlit, hdist);
            if (!inflateBlock(br, out, lit, dist)) return;
        } else {
            throw FormatError("deflate: invalid block type");
        }
    }
}

}  // namespace

std::vector<uint8_t> inflateRaw(std::span<const uint8_t> data, size_t maxOutput, InflateLimit limit) {
    BitReader br(data);
    Output out(maxOutput, limit);
    if (limit == InflateLimit::Stop) out.bytes.reserve(maxOutput);
    inflateStream(br, out);
    return std::move(out.bytes);
}

std::vector<uint8_t> inflateZlib(std::span<const uint8_t> data, size_t maxOutput) {
    if (data.size() < 6) throw FormatError("zlib: stream too short");
    const uint8_t cmf = data[0], flg = data[1];
    if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7) throw FormatError("zlib: not a deflate stream");
    if (((cmf << 8) | flg) % 31 != 0) throw FormatError("zlib: bad header check");
    if (flg & 0x20) throw FormatError("zlib: preset dictionaries are not supported");
    BitReader br(data.subspan(2));
    Output out(maxOutput, InflateLimit::Throw);
    inflateStream(br, out);
    br.alignToByte();
    const size_t p = 2 + br.bytePos();
    if (p + 4 > data.size()) throw FormatError("zlib: missing Adler-32 checksum");
    const uint32_t want = (static_cast<uint32_t>(data[p]) << 24) | (static_cast<uint32_t>(data[p + 1]) << 16) |
                          (static_cast<uint32_t>(data[p + 2]) << 8) | data[p + 3];
    if (png::adler32(out.bytes) != want) throw FormatError("zlib: Adler-32 checksum mismatch");
    return std::move(out.bytes);
}

}  // namespace tmport
