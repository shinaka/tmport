#include "dos/mods/dos_pictures.h"

#include <algorithm>
#include <format>
#include <set>
#include <unordered_map>

#include "dos/data/dos_exe.h"
#include "dos/gfx/pak.h"
#include "dos/gfx/palettes.h"
#include "dos/gfx/pan.h"
#include "mischief/mods/png_codec.h"
#include "mischief/util/error.h"

namespace tmport::dos {

namespace {

[[noreturn]] void fail(const std::string& name, const std::string& why) { throw mischief::FormatError(std::format("{}: {}", name, why)); }

void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) {
    v[at] = static_cast<uint8_t>(x & 0xFF);
    v[at + 1] = static_cast<uint8_t>(x >> 8);
}

void push16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

int distance2(const mischief::Rgb8& a, const mischief::Rgb8& b) {
    const int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

constexpr size_t kTileBytes = PanFile::kTileBytes;
using Tile = std::array<uint8_t, kTileBytes>;

struct TileHash {
    size_t operator()(const Tile& t) const {
        size_t h = 1469598103934665603ull;
        for (const uint8_t b : t) h = (h ^ b) * 1099511628211ull;
        return h;
    }
};

}  // namespace

DosColours dosPngColours(const DosExe& exe) { return lookPalette(exe, Look::Ega).colours; }

std::vector<uint8_t> logicalToPng(const LogicalImage& img, const DosColours& colours, bool sprite) {
    std::vector<uint8_t> alpha;
    if (sprite) {
        alpha.assign(colours.size(), 255);
        alpha[0] = 0;
    }
    return mischief::mods::encodePngIndexed(img.width, img.height, img.pixels, colours, alpha);
}

LogicalImage pngToLogical(std::span<const uint8_t> png, const DosColours& colours, bool sprite) {
    const mischief::mods::DecodedPng d = mischief::mods::decodePng(png);
    LogicalImage img;
    img.width = d.width;
    img.height = d.height;
    img.pixels.resize(static_cast<size_t>(d.width) * d.height);
    bool direct = d.indexed() && !d.palette.empty();
    for (size_t i = 0; direct && i < std::min<size_t>(d.palette.size(), colours.size()); ++i)
        if (!(d.palette[i] == colours[i])) direct = false;
    if (direct) {
        std::set<uint8_t> used;
        for (size_t i = 0; i < d.indices.size(); ++i) {
            const uint8_t v = d.indices[i];
            if (v >= colours.size())
                throw mischief::FormatError(std::format("pixel ({}, {}) uses palette entry {}: the DOS pictures have 16 colours (0-15)",
                                              i % static_cast<size_t>(d.width), i / static_cast<size_t>(d.width), v));
            img.pixels[i] = v;
            used.insert(v);
        }
        img.distinctColours = used.size();
        return img;
    }
    std::set<uint32_t> seen;
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const uint8_t* p = &d.rgba[i * 4];
        seen.insert(static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)));
        if (p[3] < 128) {
            if (!sprite)
                throw mischief::FormatError(std::format("pixel ({}, {}) is transparent: scenery pictures have no transparency",
                                              i % static_cast<size_t>(d.width), i / static_cast<size_t>(d.width)));
            img.pixels[i] = 0;
            continue;
        }
        const mischief::Rgb8 c{p[0], p[1], p[2]};
        int best = 0, bestD = distance2(c, colours[0]);
        for (int k = 1; k < static_cast<int>(colours.size()); ++k) {
            const int dk = distance2(c, colours[static_cast<size_t>(k)]);
            if (dk < bestD) {
                best = k;
                bestD = dk;
            }
        }
        if (bestD != 0) ++img.remapped;
        img.pixels[i] = static_cast<uint8_t>(best);
    }
    img.distinctColours = seen.size();
    return img;
}

// ---- PAK ------------------------------------------------------------------------------------------

LogicalImage pakFramePixels(const PakFrame& f) {
    LogicalImage img;
    img.width = f.widthPixels();
    img.height = f.height;
    img.pixels.resize(static_cast<size_t>(img.width) * img.height);
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < img.width; ++x) img.pixels[static_cast<size_t>(y) * img.width + x] = f.pixel(x, y);
    return img;
}

PakFrameHeader pakFrameHeader(const PakFrame& f) {
    return {f.word0, f.xOffset, f.yOffset, f.fieldA, f.fieldC, f.fieldE};
}

std::vector<uint8_t> encodePakFrame(const LogicalImage& img, const PakFrameHeader& h) {
    if (img.width <= 0 || img.height <= 0) throw mischief::FormatError("a frame needs at least one pixel");
    if (img.width % 2 != 0) throw mischief::FormatError(std::format("width {} is odd: frames are stored 2 pixels per byte", img.width));
    if (img.width / 2 > 0x7FFF || img.height > 0x7FFF) throw mischief::FormatError("frame too large");
    if (h.xOffset < -32768 || h.xOffset > 32767 || h.yOffset < -32768 || h.yOffset > 32767)
        throw mischief::FormatError("hotspot offset outside -32768..32767");
    const int wb = img.width / 2;
    std::vector<uint8_t> out;
    push16(out, h.word0);
    push16(out, static_cast<uint16_t>(wb));
    push16(out, static_cast<uint16_t>(img.height));
    push16(out, static_cast<uint16_t>(static_cast<int16_t>(h.xOffset)));
    push16(out, static_cast<uint16_t>(static_cast<int16_t>(h.yOffset)));
    push16(out, h.fieldA);
    push16(out, h.fieldC);
    push16(out, h.fieldE);
    std::vector<uint8_t> row(static_cast<size_t>(wb));
    std::vector<std::pair<uint8_t, uint8_t>> runs;
    for (int y = 0; y < img.height; ++y) {
        const uint8_t* px = &img.pixels[static_cast<size_t>(y) * img.width];
        for (int b = 0; b < wb; ++b) row[static_cast<size_t>(b)] = static_cast<uint8_t>((px[2 * b] & 15) << 4 | (px[2 * b + 1] & 15));
        runs.clear();
        for (size_t i = 0; i < row.size();) {
            size_t j = i;
            while (j < row.size() && row[j] == row[i] && j - i < 255) ++j;
            runs.emplace_back(static_cast<uint8_t>(j - i), row[i]);
            i = j;
        }
        const bool fitsRuns = runs.size() <= 63;
        if (fitsRuns && 2 * static_cast<int>(runs.size()) <= wb - 2) {
            out.push_back(static_cast<uint8_t>(2 * runs.size()));
            for (const auto& [n, v] : runs) {
                out.push_back(n);
                out.push_back(v);
            }
        } else if (wb <= 127) {
            out.push_back(static_cast<uint8_t>(0x80 | wb));
            out.insert(out.end(), row.begin(), row.end());
        } else {
            throw mischief::FormatError(std::format("row {} cannot be stored: {} bytes wide (raw rows hold 127) with {} colour runs "
                                          "(run rows hold 63)",
                                          y, wb, runs.size()));
        }
    }
    return out;
}

PakRebuild rebuildPak(std::span<const uint8_t> base, const std::map<int, PakFrameEdit>& edits, const std::string& name) {
    const PakFile pak = PakFile::load(base, name);
    const int count = pak.count();
    for (const auto& [k, e] : edits)
        if (k < 0 || k >= count) fail(name, std::format("frame {} does not exist (the pack has frames 0-{})", k, count - 1));
    PakRebuild r;
    const size_t first = pak.frame(0).fileOffset;
    r.file.assign(base.begin(), base.begin() + static_cast<std::ptrdiff_t>(first));
    for (int k = 0; k < count; ++k) {
        const PakFrame& f = pak.frame(k);
        const size_t next = k + 1 < count ? pak.frame(k + 1).fileOffset : base.size();
        if (r.file.size() % 16 != 0 || r.file.size() / 16 > 0xFFFF) fail(name, "the pack is larger than 1 MB");
        put16(r.file, 8 + 2 * static_cast<size_t>(k), static_cast<uint16_t>(r.file.size() / 16));
        const auto it = edits.find(k);
        std::vector<uint8_t> enc;
        if (it != edits.end()) {
            const PakFrameEdit& e = it->second;
            const LogicalImage pixels = pakFramePixels(f);
            const PakFrameHeader orig = pakFrameHeader(f);
            PakFrameHeader h = orig;
            if (e.xOffset) h.xOffset = *e.xOffset;
            if (e.yOffset) h.yOffset = *e.yOffset;
            if (e.fieldA) h.fieldA = *e.fieldA;
            if (e.fieldC) h.fieldC = *e.fieldC;
            if (e.fieldE) h.fieldE = *e.fieldE;
            const LogicalImage& img = e.image ? *e.image : pixels;
            const bool same = img == pixels && h.xOffset == orig.xOffset && h.yOffset == orig.yOffset && h.fieldA == orig.fieldA &&
                              h.fieldC == orig.fieldC && h.fieldE == orig.fieldE;
            if (!same) {
                try {
                    enc = encodePakFrame(img, h);
                } catch (const mischief::FormatError& ex) {
                    fail(name, std::format("frame {}: {}", k, ex.what()));
                }
            }
        }
        if (enc.empty()) {
            r.file.insert(r.file.end(), base.begin() + static_cast<std::ptrdiff_t>(f.fileOffset),
                          base.begin() + static_cast<std::ptrdiff_t>(next));
        } else {
            ++r.framesChanged;
            r.file.insert(r.file.end(), enc.begin(), enc.end());
            r.file.resize((r.file.size() + 15) / 16 * 16, 0);
        }
    }
    return r;
}

int pakFramesReproduced(std::span<const uint8_t> file, const std::string& name) {
    const PakFile pak = PakFile::load(file, name);
    int same = 0;
    for (const PakFrame& f : pak.frames()) {
        const auto enc = encodePakFrame(pakFramePixels(f), pakFrameHeader(f));
        if (enc.size() == f.usedBytes && std::equal(enc.begin(), enc.end(), file.begin() + static_cast<std::ptrdiff_t>(f.fileOffset)))
            ++same;
    }
    return same;
}

// ---- PAN ------------------------------------------------------------------------------------------

PanContent panContent(const PanFile& pan) {
    PanContent c;
    c.width = pan.widthTiles();
    c.height = pan.heightTiles();
    c.pictures = pan.pictureCount();
    c.map.resize(pan.mapWordCount());
    for (size_t i = 0; i < c.map.size(); ++i) c.map[i] = pan.mapWord(i);
    c.tiles.assign(pan.tileBuffer().begin(), pan.tileBuffer().begin() + static_cast<std::ptrdiff_t>(pan.tileByteCount()));
    return c;
}

LogicalImage panPicture(const PanContent& c, int k) {
    LogicalImage img;
    img.width = c.width * 8;
    img.height = c.height * 8;
    img.pixels.resize(static_cast<size_t>(img.width) * img.height);
    const size_t base = static_cast<size_t>(k) * c.width * c.height;
    for (int cx = 0; cx < c.width; ++cx)
        for (int ry = 0; ry < c.height; ++ry) {
            const size_t at = static_cast<size_t>(c.map[base + static_cast<size_t>(cx) * c.height + ry]) * 16;
            for (int py = 0; py < 8; ++py)
                for (int px = 0; px < 8; ++px) {
                    const size_t bi = at + static_cast<size_t>(py) * 4 + px / 2;
                    const uint8_t b = bi < c.tiles.size() ? c.tiles[bi] : 0;
                    img.pixels[static_cast<size_t>(ry * 8 + py) * img.width + cx * 8 + px] = (px & 1) ? (b & 15) : (b >> 4);
                }
        }
    return img;
}

std::vector<uint8_t> encodePan(const PanContent& c) {
    const size_t pictureWords = static_cast<size_t>(c.width) * c.height;
    if (c.map.size() != pictureWords * static_cast<size_t>(c.pictures)) throw mischief::FormatError("PAN map size does not match its pictures");
    std::vector<uint8_t> mapStream;
    std::map<std::vector<uint16_t>, size_t> firstRow;  // row entries -> word index of its first occurrence
    std::vector<uint16_t> row(static_cast<size_t>(c.width));
    std::vector<uint16_t> words;
    for (int p = 0; p < c.pictures; ++p)
        for (int r = 0; r < c.height; ++r) {
            const size_t start = static_cast<size_t>(p) * pictureWords + r;
            for (int col = 0; col < c.width; ++col) {
                const uint16_t v = c.map[start + static_cast<size_t>(col) * c.height];
                if (v >= 0x0FFF) throw mischief::FormatError(std::format("PAN map entry {:#x} is too large (tiles 0-2046)", v));
                row[static_cast<size_t>(col)] = v;
            }
            words.clear();
            for (size_t i = 0; i < row.size();) {
                size_t j = i;
                while (j < row.size() && row[j] == row[i] && j - i < 16) ++j;
                words.push_back(j - i >= 2 ? static_cast<uint16_t>((j - i - 1) << 12 | row[i]) : row[i]);
                i = j;
            }
            const auto prev = firstRow.find(row);
            if (c.width == 16 && prev != firstRow.end() && words.size() > 2 && prev->second <= 0xFFFF) {
                push16(mapStream, 0x0FFF);
                push16(mapStream, static_cast<uint16_t>(prev->second));
            } else {
                for (const uint16_t w : words) push16(mapStream, w);
            }
            firstRow.emplace(row, start);
        }
    std::vector<uint8_t> tileStream;
    for (size_t i = 0; i < c.tiles.size();) {
        size_t j = i;
        while (j < c.tiles.size() && c.tiles[j] == c.tiles[i] && j - i < 255) ++j;
        size_t n = j - i;
        const uint8_t v = c.tiles[i];
        if (n == 0xA5) --n;  // A5 A5 would mean a literal A5
        if (n >= 4) {
            tileStream.insert(tileStream.end(), {0xA5, static_cast<uint8_t>(n), v});
            i += n;
        } else {
            if (v == 0xA5) tileStream.push_back(0xA5);
            tileStream.push_back(v);
            ++i;
        }
    }
    if (mapStream.size() > 0xFFFF || tileStream.size() > 0xFFFF) throw mischief::FormatError("PAN packed data larger than 64 KB");
    std::vector<uint8_t> file;
    push16(file, static_cast<uint16_t>(mapStream.size()));
    push16(file, static_cast<uint16_t>(c.width));
    push16(file, static_cast<uint16_t>(c.height));
    push16(file, static_cast<uint16_t>(tileStream.size()));
    file.insert(file.end(), mapStream.begin(), mapStream.end());
    file.insert(file.end(), tileStream.begin(), tileStream.end());
    return file;
}

PanRebuild rebuildPan(std::span<const uint8_t> base, const std::map<int, LogicalImage>& pictures, const std::string& name) {
    const PanFile pan = PanFile::load(base, name);
    PanContent c = panContent(pan);
    PanRebuild r;
    std::vector<int> changed;
    for (const auto& [k, img] : pictures) {
        if (k < 0 || k >= c.pictures) fail(name, std::format("picture {} does not exist (the file has pictures 0-{})", k, c.pictures - 1));
        if (img.width != c.width * 8 || img.height != c.height * 8)
            fail(name, std::format("picture {} is {}x{}, it must be {}x{}", k, img.width, img.height, c.width * 8, c.height * 8));
        if (!(img == panPicture(c, k))) changed.push_back(k);
    }
    r.tiles = static_cast<int>(c.tiles.size() / kTileBytes);
    if (changed.empty()) {
        r.file.assign(base.begin(), base.end());
        return r;
    }

    // New map entries for the changed pictures: identical tiles are shared, new ones appended.
    std::unordered_map<Tile, int, TileHash> index;
    const int oldTiles = static_cast<int>(c.tiles.size() / kTileBytes);
    for (int t = 0; t < oldTiles; ++t) {
        Tile tile;
        std::copy_n(c.tiles.begin() + static_cast<std::ptrdiff_t>(t) * kTileBytes, kTileBytes, tile.begin());
        index.emplace(tile, t);
    }
    const size_t pictureWords = static_cast<size_t>(c.width) * c.height;
    for (const int k : changed) {
        const LogicalImage& img = pictures.at(k);
        for (int cx = 0; cx < c.width; ++cx)
            for (int ry = 0; ry < c.height; ++ry) {
                Tile tile;
                for (int py = 0; py < 8; ++py)
                    for (int b = 0; b < 4; ++b) {
                        const uint8_t* px = &img.pixels[static_cast<size_t>(ry * 8 + py) * img.width + cx * 8 + b * 2];
                        tile[static_cast<size_t>(py * 4 + b)] = static_cast<uint8_t>((px[0] & 15) << 4 | (px[1] & 15));
                    }
                auto [it, added] = index.emplace(tile, static_cast<int>(c.tiles.size() / kTileBytes));
                if (added) c.tiles.insert(c.tiles.end(), tile.begin(), tile.end());
                c.map[static_cast<size_t>(k) * pictureWords + static_cast<size_t>(cx) * c.height + ry] = static_cast<uint16_t>(it->second * 2);
            }
        ++r.picturesChanged;
    }

    // Drop the tiles no picture uses any more, keeping the order of the others.
    const int allTiles = static_cast<int>(c.tiles.size() / kTileBytes);
    std::vector<int> remap(static_cast<size_t>(allTiles), -1);
    for (const uint16_t v : c.map) remap[static_cast<size_t>(v / 2)] = 0;
    std::vector<uint8_t> kept;
    int n = 0;
    for (int t = 0; t < allTiles; ++t) {
        if (remap[static_cast<size_t>(t)] < 0) continue;
        remap[static_cast<size_t>(t)] = n++;
        kept.insert(kept.end(), c.tiles.begin() + static_cast<std::ptrdiff_t>(t) * kTileBytes,
                    c.tiles.begin() + static_cast<std::ptrdiff_t>(t + 1) * kTileBytes);
    }
    c.tiles = std::move(kept);
    for (uint16_t& v : c.map) v = static_cast<uint16_t>(remap[static_cast<size_t>(v / 2)] * 2);
    r.tiles = n;
    const size_t maxTiles = PanFile::kTileBufferSize / kTileBytes;
    if (static_cast<size_t>(n) > maxTiles)
        fail(name, std::format("{} different 8x8 tiles; the game's tile buffer holds {} (reuse tiles: repeat 8x8 squares)", n, maxTiles));

    try {
        r.file = encodePan(c);
    } catch (const mischief::FormatError& e) {
        fail(name, e.what());
    }
    // Unpack the result the way the game does and compare.
    PanContent back;
    try {
        back = panContent(PanFile::load(r.file, name));
    } catch (const mischief::FormatError& e) {
        fail(name, std::format("the game could not unpack the new file ({})", e.what()));
    }
    if (back.map != c.map || back.tiles != c.tiles)
        fail(name, "the packed data is too large to be unpacked in place in the game's buffers (fewer different tiles or "
                   "simpler pictures needed)");
    return r;
}

}  // namespace tmport::dos
