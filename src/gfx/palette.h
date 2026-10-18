#pragma once
// Game palette. 256-colour mode: RRGB 900 (256 x RGB, logical indices are composed end to end).
// 16-colour mode: the Windows VGA device palette with PMAP nibble values as indices.
// Colour key: every index whose colour is RGB(0,0,0xAA) in 256-colour mode (logical 15 and 241,
// the colour the original keys on after remapping to physical indices), nibble 3 in 16-colour
// mode (re/docs/fmt_pmap.md §1, §6.2). UI pen colours are EGA numbers 0..15 (DS:07d8 / DS:0838).

#include <array>
#include <bitset>
#include <cstdint>
#include <span>

#include "util/rgb.h"

namespace tmport::gfx {

enum class ColorMode { Colour256, Colour16 };

using KeyTable = std::bitset<256>;  // indices that are transparent for a keyed draw

class Palette {
public:
    Palette();  // all black, 256-colour mode, no key

    // RRGB resource body: at least 768 bytes of R,G,B (the NE resource is padded to 1024).
    static Palette fromRrgb(std::span<const uint8_t> rrgb);
    // Windows VGA 16-colour device palette (indices 0..15; the rest black).
    static Palette vga16();

    ColorMode mode() const { return mode_; }
    const Rgb8& colour(uint8_t index) const { return colours_[index]; }
    const std::array<Rgb8, 256>& colours() const { return colours_; }
    int size() const { return mode_ == ColorMode::Colour16 ? 16 : 256; }
    // Packed little-endian RGBA bytes (R at the lowest address) for the canvas.
    const std::array<uint32_t, 256>& rgba() const { return rgba_; }

    // Default key (RGB 0000AA / nibble 3) and the white key used by the mirrored white-key draw
    // (RGB FFFFFF, i.e. logical 0 and 77 / nibble 15).
    const KeyTable& keyTable() const { return key_; }
    const KeyTable& whiteKeyTable() const { return whiteKey_; }

    // GetNearestPaletteIndex equivalent: smallest squared RGB distance, first index on ties.
    uint8_t nearestIndex(Rgb8 c) const;
    // True if both indices show the same colour (physical-index equality in the original).
    bool sameColour(uint8_t a, uint8_t b) const { return colours_[a] == colours_[b]; }

    // Pen colours: EGA number k -> device index. 256-colour: nearest index to ega[k];
    // 16-colour: vga16Index[k]. Must be set from ExeTables before drawing with pens.
    void setPens(const std::array<Rgb8, 16>& ega, const std::array<uint8_t, 16>& vga16Index);
    const std::array<uint8_t, 16>& pens() const { return pens_; }
    uint8_t pen(int k) const { return pens_[static_cast<size_t>(k & 15)]; }

    // Gfx_HighlightRect LUT (1058:19b2, PORTING_NOTES §4.4): every colour -> pen 14; colour of
    // pen 0 -> pen 1; pen 4 -> pen 6; pen 14 -> pen 1 (later rules win). Matching is by colour,
    // as the original works on physical indices where equal colours share one index.
    std::array<uint8_t, 256> highlightLut() const;

private:
    void rebuildDerived();

    ColorMode mode_ = ColorMode::Colour256;
    std::array<Rgb8, 256> colours_{};
    std::array<uint32_t, 256> rgba_{};
    KeyTable key_;
    KeyTable whiteKey_;
    std::array<uint8_t, 16> pens_{};
};

}  // namespace tmport::gfx
