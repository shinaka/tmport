#include "dos/gfx/pan.h"

#include <algorithm>
#include <format>

#include "mischief/util/error.h"

namespace tmport::dos {

namespace {

uint16_t rd16(std::span<const uint8_t> b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }

[[noreturn]] void fail(const std::string& name, const std::string& why) {
    throw mischief::FormatError(std::format("{}: {}", name, why));
}

// 1000:2368 Map_Unpack with 1000:23b9 Map_UnpackRow, run in place on the map buffer exactly as the
// game does: the packed words sit at the end of the buffer, the decoded words are written from
// offset 0, and a 0xFFF row copy reads its 16 entries from the buffer. `written` marks decoded
// words (for the completeness check only). Returns the number of decoded words.
size_t unpackMap(std::vector<uint8_t>& buf, size_t packedSize, int width, int height, int& backRefs,
                 std::vector<bool>& written, const std::string& name) {
    const size_t stride = static_cast<size_t>(height) * 2;  // BP: one tile row = one word per column
    size_t si = buf.size() - packedSize;
    size_t pictureBase = 0;  // byte offset of the current picture (DS:3082 at a picture start)
    auto readWord = [&](size_t at) -> uint16_t {
        if (at + 2 > buf.size()) fail(name, "map stream runs past the end of its buffer");
        return rd16(buf, at);
    };
    auto writeWord = [&](size_t at, uint16_t v) {
        if (at + 2 > buf.size()) fail(name, "decoded map does not fit the 0xA400-byte buffer");
        buf[at] = static_cast<uint8_t>(v);
        buf[at + 1] = static_cast<uint8_t>(v >> 8);
        written[at / 2] = true;
    };
    while (si < buf.size()) {  // 1000:23ab: another picture while input is left
        size_t di = pictureBase, rowStart = pictureBase, endOfRow = pictureBase;
        for (int r = 0; r < height; ++r) {
            di = rowStart;
            int left = width;  // BX
            while (left != 0) {
                const uint16_t w = readWord(si);
                si += 2;
                const uint16_t v = w & 0x0FFF;
                const int n = w >> 12;
                if (n != 0) {  // run: the value n+1 times
                    if (n + 1 > left) fail(name, "map run crosses the end of a row");
                    for (int k = 0; k <= n; ++k, di += stride) writeWord(di, v);
                    left -= n + 1;
                } else if (v == 0x0FFF) {  // copy 16 entries of an earlier row (count fixed at 1000:23e9)
                    size_t src = static_cast<size_t>(readWord(si)) * 2;
                    si += 2;
                    for (int k = 0; k < 16; ++k, di += stride, src += stride) writeWord(di, readWord(src));
                    ++backRefs;
                    left = 0;
                } else {
                    writeWord(di, v);
                    di += stride;
                    --left;
                }
            }
            endOfRow = di;
            rowStart += 2;
        }
        // 1000:239d: the next picture starts where the last row ended, minus one row stride, plus 2.
        pictureBase = endOfRow - stride + 2;
    }
    if (si != buf.size()) fail(name, "map stream does not end at the end of its buffer");
    return pictureBase / 2;
}

// 1000:240e Tiles_Unpack, in place: byte RLE with escape A5h until the input pointer reaches the
// end of the buffer. Returns the number of decoded bytes.
size_t unpackTiles(std::vector<uint8_t>& buf, size_t packedSize, const std::string& name) {
    size_t si = buf.size() - packedSize, di = 0;
    auto in = [&]() -> uint8_t {
        if (si >= buf.size()) fail(name, "tile stream runs past the end of its buffer");
        return buf[si++];
    };
    auto out = [&](uint8_t v) {
        if (di >= buf.size()) fail(name, "decoded tiles do not fit the 0xE880-byte buffer");
        buf[di++] = v;
    };
    while (si != buf.size()) {
        const uint8_t b = in();
        if (b != 0xA5) {
            out(b);
            continue;
        }
        const uint8_t n = in();
        if (n == 0xA5) {
            out(0xA5);
            continue;
        }
        const uint8_t v = in();
        for (int k = 0; k < n; ++k) out(v);
    }
    return di;
}

}  // namespace

// 1460:05e8 Scene_LoadPan (reading and in-place decoding; the file index table is the caller's).
PanFile PanFile::load(std::span<const uint8_t> file, std::string name) {
    PanFile p;
    p.name_ = std::move(name);
    if (file.size() < 8) fail(p.name_, "file too short");
    p.mapPacked_ = rd16(file, 0);
    p.width_ = rd16(file, 2);
    p.height_ = rd16(file, 4);
    p.tilesPacked_ = rd16(file, 6);
    if (8 + p.mapPacked_ + p.tilesPacked_ != file.size())
        fail(p.name_, std::format("header sizes give {} bytes, the file has {}", 8 + p.mapPacked_ + p.tilesPacked_,
                                  file.size()));
    if (p.width_ <= 0 || p.height_ <= 0 || p.mapPacked_ % 2 != 0) fail(p.name_, "bad header");
    if (p.mapPacked_ > kMapBufferSize || p.tilesPacked_ > kTileBufferSize) fail(p.name_, "packed block larger than its buffer");

    p.mapBuf_.assign(kMapBufferSize, 0);
    std::copy_n(file.begin() + 8, p.mapPacked_, p.mapBuf_.end() - static_cast<std::ptrdiff_t>(p.mapPacked_));
    std::vector<bool> written(kMapBufferSize / 2, false);
    p.mapWords_ = unpackMap(p.mapBuf_, p.mapPacked_, p.width_, p.height_, p.backRefs_, written, p.name_);
    const size_t pictureWords = static_cast<size_t>(p.width_) * p.height_;
    if (p.mapWords_ == 0 || p.mapWords_ % pictureWords != 0) fail(p.name_, "map is not a whole number of pictures");
    if (!std::all_of(written.begin(), written.begin() + static_cast<std::ptrdiff_t>(p.mapWords_), [](bool b) { return b; }))
        fail(p.name_, "map has holes");
    p.pictures_ = static_cast<int>(p.mapWords_ / pictureWords);

    p.tileBuf_.assign(kTileBufferSize, 0);
    std::copy_n(file.begin() + 8 + static_cast<std::ptrdiff_t>(p.mapPacked_), p.tilesPacked_,
                p.tileBuf_.end() - static_cast<std::ptrdiff_t>(p.tilesPacked_));
    p.tileBytes_ = unpackTiles(p.tileBuf_, p.tilesPacked_, p.name_);
    if (p.tileBytes_ % kTileBytes != 0) fail(p.name_, "tile data is not a whole number of 32-byte tiles");
    for (size_t i = 0; i < p.mapWords_; ++i) {
        const uint16_t v = p.mapWord(i);
        if ((v & 1) != 0 || static_cast<size_t>(v) * 16 + kTileBytes > p.tileBytes_)
            fail(p.name_, std::format("map entry {:#x} does not address a tile", v));
    }
    return p;
}

uint16_t PanFile::mapWord(size_t wordIndex) const {
    if (wordIndex >= mapBuf_.size() / 2) return 0;
    return rd16(mapBuf_, wordIndex * 2);
}

std::span<const uint8_t, PanFile::kTileBytes> PanFile::tile(uint16_t entry) const {
    static const std::array<uint8_t, kTileBytes> kZero{};
    const size_t at = static_cast<size_t>(entry) * 16;
    if (at + kTileBytes > tileBuf_.size()) return std::span<const uint8_t, kTileBytes>(kZero);
    return std::span<const uint8_t, kTileBytes>(tileBuf_.data() + at, kTileBytes);
}

std::vector<uint8_t> PanFile::picturePixels(int k) const {
    const int w = width_ * 8, h = height_ * 8;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h);
    const size_t base = pictureWordIndex(k);
    for (int col = 0; col < width_; ++col)
        for (int row = 0; row < height_; ++row) {
            const auto t = tile(mapWord(base + static_cast<size_t>(col) * height_ + row));
            for (int y = 0; y < 8; ++y)
                for (int xb = 0; xb < 4; ++xb) {
                    const uint8_t b = t[static_cast<size_t>(y * 4 + xb)];
                    uint8_t* o = &px[static_cast<size_t>(row * 8 + y) * w + col * 8 + xb * 2];
                    o[0] = b >> 4;
                    o[1] = b & 15;
                }
        }
    return px;
}

}  // namespace tmport::dos
