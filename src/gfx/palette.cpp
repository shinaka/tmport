#include "gfx/palette.h"

#include "util/error.h"

namespace tmport::gfx {

namespace {

// Windows VGA driver colours (generic Windows palette, not game data; fmt_pmap.md §1).
constexpr std::array<Rgb8, 16> kVga16 = {{
    {0x00, 0x00, 0x00}, {0x80, 0x00, 0x00}, {0x00, 0x80, 0x00}, {0x80, 0x80, 0x00},
    {0x00, 0x00, 0x80}, {0x80, 0x00, 0x80}, {0x00, 0x80, 0x80}, {0x80, 0x80, 0x80},
    {0xC0, 0xC0, 0xC0}, {0xFF, 0x00, 0x00}, {0x00, 0xFF, 0x00}, {0xFF, 0xFF, 0x00},
    {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0xFF}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF},
}};

constexpr Rgb8 kKeyColour{0x00, 0x00, 0xAA};    // 1048:008b, PALETTERGB(0,0,0xAA)
constexpr Rgb8 kWhiteKeyColour{0xFF, 0xFF, 0xFF};  // 1058:0cf3 -> 1048:00b7(255,255,255)
constexpr uint8_t kKeyNibble = 3;
constexpr uint8_t kWhiteKeyNibble = 15;

}  // namespace

Palette::Palette() {
    for (size_t i = 0; i < 16; ++i) pens_[i] = static_cast<uint8_t>(i);
    rebuildDerived();
}

Palette Palette::fromRrgb(std::span<const uint8_t> rrgb) {
    if (rrgb.size() < 768) throw FormatError("RRGB palette shorter than 768 bytes");
    Palette p;
    p.mode_ = ColorMode::Colour256;
    for (size_t i = 0; i < 256; ++i) p.colours_[i] = Rgb8{rrgb[i * 3], rrgb[i * 3 + 1], rrgb[i * 3 + 2]};
    p.rebuildDerived();
    return p;
}

Palette Palette::vga16() {
    Palette p;
    p.mode_ = ColorMode::Colour16;
    for (size_t i = 0; i < 16; ++i) p.colours_[i] = kVga16[i];
    p.rebuildDerived();
    return p;
}

void Palette::rebuildDerived() {
    key_.reset();
    whiteKey_.reset();
    for (size_t i = 0; i < 256; ++i) {
        const Rgb8& c = colours_[i];
        rgba_[i] = static_cast<uint32_t>(c.r) | (static_cast<uint32_t>(c.g) << 8) | (static_cast<uint32_t>(c.b) << 16) |
                   0xFF000000u;
        if (mode_ == ColorMode::Colour256) {
            if (c == kKeyColour) key_.set(i);
            if (c == kWhiteKeyColour) whiteKey_.set(i);
        }
    }
    if (mode_ == ColorMode::Colour16) {
        key_.set(kKeyNibble);
        whiteKey_.set(kWhiteKeyNibble);
    }
}

uint8_t Palette::nearestIndex(Rgb8 c) const {
    const int n = size();
    int best = 0;
    long bestD = -1;
    for (int i = 0; i < n; ++i) {
        const Rgb8& p = colours_[static_cast<size_t>(i)];
        const long dr = static_cast<long>(p.r) - c.r, dg = static_cast<long>(p.g) - c.g, db = static_cast<long>(p.b) - c.b;
        const long d = dr * dr + dg * dg + db * db;
        if (bestD < 0 || d < bestD) {
            bestD = d;
            best = i;
            if (d == 0) break;
        }
    }
    return static_cast<uint8_t>(best);
}

void Palette::setPens(const std::array<Rgb8, 16>& ega, const std::array<uint8_t, 16>& vga16Index) {
    for (size_t k = 0; k < 16; ++k)
        pens_[k] = mode_ == ColorMode::Colour16 ? static_cast<uint8_t>(vga16Index[k] & 0x0F) : nearestIndex(ega[k]);
}

std::array<uint8_t, 256> Palette::highlightLut() const {
    std::array<uint8_t, 256> lut{};
    const uint8_t yellow = pen(14), blue = pen(1), brown = pen(6);
    for (size_t i = 0; i < 256; ++i) {
        const auto idx = static_cast<uint8_t>(i);
        uint8_t v = yellow;
        if (sameColour(idx, pen(0))) v = blue;
        if (sameColour(idx, pen(4))) v = brown;
        if (sameColour(idx, pen(14))) v = blue;
        lut[i] = v;
    }
    return lut;
}

}  // namespace tmport::gfx
