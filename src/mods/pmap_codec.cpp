#include "mods/pmap_codec.h"

#include <format>
#include <unordered_map>

#include "mods/mod_common.h"
#include "mods/png_codec.h"
#include "util/error.h"

namespace tmport::mods {

namespace {

constexpr int kMaxSide = 4096;

class Quantiser {
public:
    explicit Quantiser(const gfx::Palette& p) : pal_(p), n_(p.size()) {
        for (int i = 0; i < n_; ++i)
            if (p.keyTable()[static_cast<size_t>(i)]) {
                key_ = i;
                break;
            }
    }

    int keyIndex() const { return key_; }
    bool isKey(int i) const { return pal_.keyTable()[static_cast<size_t>(i)]; }

    uint8_t key() const {
        if (key_ < 0) throw FormatError("the game palette has no colour-key entry (RGB 0,0,170): transparent pixels are impossible");
        return static_cast<uint8_t>(key_);
    }

    // Colour -> index (alpha already handled by the caller).
    uint8_t map(Rgb8 c, bool& exact) {
        if (c.r == 0 && c.g == 0 && c.b == 0xAA) {
            exact = true;
            return key();
        }
        const uint32_t k = (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.b;
        if (auto it = cache_.find(k); it != cache_.end()) {
            exact = it->second.second;
            return it->second.first;
        }
        int best = -1;
        long bestD = 0;
        for (int i = 0; i < n_; ++i) {
            if (isKey(i)) continue;
            const Rgb8 q = pal_.colour(static_cast<uint8_t>(i));
            const long dr = c.r - q.r, dg = c.g - q.g, db = c.b - q.b;
            const long d = dr * dr + dg * dg + db * db;
            if (best < 0 || d < bestD) {
                best = i;
                bestD = d;
            }
        }
        if (best < 0) throw FormatError("the game palette has no usable colour");
        exact = bestD == 0;
        cache_[k] = {static_cast<uint8_t>(best), exact};
        return static_cast<uint8_t>(best);
    }

private:
    const gfx::Palette& pal_;
    int n_;
    int key_ = -1;
    std::unordered_map<uint32_t, std::pair<uint8_t, bool>> cache_;
};

}  // namespace

PmapImage pngToIndices(std::span<const uint8_t> pngFile, const gfx::Palette& palette) {
    const DecodedPng png = decodePng(pngFile);
    if (png.width > kMaxSide || png.height > kMaxSide)
        throw FormatError(std::format("the picture is {}x{}; at most {}x{} is supported", png.width, png.height, kMaxSide, kMaxSide));
    PmapImage img;
    img.width = png.width;
    img.height = png.height;
    const size_t n = static_cast<size_t>(png.width) * static_cast<size_t>(png.height);
    img.indices.resize(n);
    Quantiser q(palette);
    const int palSize = palette.size();

    if (png.indexed()) {
        bool direct = static_cast<int>(png.palette.size()) <= palSize;
        for (size_t i = 0; direct && i < png.palette.size(); ++i)
            direct = png.palette[i] == palette.colour(static_cast<uint8_t>(i));
        // Per-entry mapping table.
        std::vector<uint8_t> map(png.palette.size());
        std::vector<uint8_t> isKey(png.palette.size(), 0), inexact(png.palette.size(), 0);
        for (size_t i = 0; i < png.palette.size(); ++i) {
            const bool transparent = png.paletteAlpha[i] < 128;
            if (direct) {
                if (transparent && !q.isKey(static_cast<int>(i))) {
                    map[i] = q.key();
                } else {
                    map[i] = static_cast<uint8_t>(i);
                }
                isKey[i] = q.isKey(map[i]);
            } else if (transparent) {
                map[i] = q.key();
                isKey[i] = 1;
            } else {
                bool exact = false;
                map[i] = q.map(png.palette[i], exact);
                isKey[i] = q.isKey(map[i]);
                inexact[i] = !exact;
            }
        }
        for (size_t i = 0; i < n; ++i) {
            const uint8_t e = png.indices[i];
            img.indices[i] = map[e];
            img.keyPixels += isKey[e];
            img.remapped += inexact[e];
        }
        img.direct = direct;
        return img;
    }

    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = png.rgba.data() + i * 4;
        if (p[3] < 128) {
            img.indices[i] = q.key();
            ++img.keyPixels;
            continue;
        }
        bool exact = false;
        img.indices[i] = q.map(Rgb8{p[0], p[1], p[2]}, exact);
        if (q.isKey(img.indices[i])) ++img.keyPixels;
        if (!exact) ++img.remapped;
    }
    return img;
}

std::vector<uint8_t> encodePmapBody(const PmapImage& img, int depth, gfx::PixEncoding encoding) {
    if (img.width <= 0 || img.height <= 0 || img.width > 0xFFFF || img.height > 0xFFFF)
        throw FormatError(std::format("bad picture size {}x{}", img.width, img.height));
    std::vector<uint8_t> out;
    auto header = [&] {
        out.push_back(static_cast<uint8_t>(img.width & 0xFF));
        out.push_back(static_cast<uint8_t>(img.width >> 8));
        out.push_back(static_cast<uint8_t>(img.height & 0xFF));
        out.push_back(static_cast<uint8_t>(img.height >> 8));
    };
    if (depth == 4) {
        header();
        const size_t stride = (static_cast<size_t>(img.width) + 1) / 2;
        for (int y = 0; y < img.height; ++y) {
            const uint8_t* row = img.indices.data() + static_cast<size_t>(y) * static_cast<size_t>(img.width);
            for (size_t b = 0; b < stride; ++b) {
                const size_t x = b * 2;
                const uint8_t hi = row[x] & 0x0F;
                const uint8_t lo = x + 1 < static_cast<size_t>(img.width) ? (row[x + 1] & 0x0F) : 0;
                out.push_back(static_cast<uint8_t>((hi << 4) | lo));
            }
        }
    } else if (encoding == gfx::PixEncoding::Raw8) {
        header();
        out.insert(out.end(), img.indices.begin(), img.indices.end());
    } else {
        out = gfx::rundCompress(img.indices, img.width, img.height);
    }
    padToNe(out);
    return out;
}

std::vector<uint8_t> pixmapToEditablePng(const gfx::PixMap& pm, const gfx::Palette& palette) {
    const int n = palette.size();
    std::vector<Rgb8> colours(palette.colours().begin(), palette.colours().begin() + n);
    std::vector<uint8_t> alpha(static_cast<size_t>(n), 255);
    for (int i = 0; i < n; ++i)
        if (palette.keyTable()[static_cast<size_t>(i)]) alpha[static_cast<size_t>(i)] = 0;
    return encodePngIndexed(pm.width, pm.height, pm.pixels, colours, alpha);
}

}  // namespace tmport::mods
