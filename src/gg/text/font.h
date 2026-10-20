#pragma once
// The fonts of Gizmos & Gadgets (ovl01 2b16; re/gg/docs/code_ui.md §3, fmt_graphics.md §5): the
// 'fnt' 0x1A resources 9 (the UI font, with key-cap glyphs) and 10 (the small font), loaded by
// Font_Init at start-up, and the selection state the text code draws with.
//
//   +0  'fnt' 0x1A    +4 u16 flags (bit 0: 1-bpp glyphs)    +6 u16 records    +8 u16 height
//   +10 u16 spacing   +12 per character code: u16 y offset, u16 width; if width != 0: u16 size, then
//   size bytes of glyph {u8 full bytes per row, u8 bits in a partial byte, u16 rows, rows}, bit 0 =
//   leftmost pixel (a picture for fonts without flag bit 0, drawn with Lpx_DrawKeyed; none shipped).
//
// Each font record keeps its own colour (+0x0A, Font_SetColor); selecting a font makes its colour
// the glyph colour g_glyphColor DS:1f6c. No font routine marks dirty tiles: text reaches the screen
// with the marks of whatever was drawn under it (a box fill, a background).

#include <cstdint>
#include <span>
#include <vector>

#include "gg/gfx/screen.h"

namespace tmport::gg {

class Archive;

// A loaded font record (Font_Load 2b16:0000, 0x24 bytes in the original).
class GgFont {
public:
    // Parses a fnt payload (copied). Throws tmport::FormatError for a broken one.
    static GgFont parse(std::span<const uint8_t> payload);

    uint16_t flags() const { return flags_; }        // +0x00
    int records() const { return records_; }          // +0x02
    int height() const { return height_; }            // +0x04
    int spacing() const { return spacing_; }          // +0x06
    // The story page and the energy puzzle patch +0x06 through Font_GetCurrent (code_ui.md §10.3).
    void setSpacing(int s) { spacing_ = s; }
    int colour() const { return colour_; }            // +0x0A (uninitialised by Font_Load; 0 here)
    void setColour(int c) { colour_ = c; }

    // The width table +0x10 and y offsets +0x14; codes outside the records give 0 (the original
    // indexes the tables with the signed byte and no range check).
    int width(uint8_t c) const { return c < width_.size() ? width_[c] : 0; }
    int yOffset(uint8_t c) const { return c < yOffset_.size() ? yOffset_[c] : 0; }
    // The glyph +0x1C[c] (empty when the width is 0 or the code is out of range).
    std::span<const uint8_t> glyph(uint8_t c) const;
    bool oneBit() const { return (flags_ & 1) != 0; }

    // 2b16:03ea Font_CharWidth: 3 for a space, else width + spacing (also for glyph-less codes).
    int charWidth(uint8_t c) const { return c == 0x20 ? 3 : width(c) + spacing_; }
    // 2b16:0418 Font_GetHeight: height + 1.
    int lineHeight() const { return height_ + 1; }

private:
    std::vector<uint8_t> bytes_;  // +0x0C: the private copy
    uint16_t flags_ = 0;
    int records_ = 0;
    int height_ = 0;
    int spacing_ = 0;
    int colour_ = 0;
    std::vector<int> width_, yOffset_;
    std::vector<std::pair<size_t, size_t>> glyphs_;  // offset and size of each glyph in bytes_
};

// The two fonts and the selection state of ovl01 (g_fonts DS:a040, g_fontIndex DS:1f66, g_font
// DS:1f68, g_glyphColor DS:1f6c).
class Fonts {
public:
    static constexpr uint16_t kUiFontId = 0x0009;
    static constexpr uint16_t kSmallFontId = 0x000A;

    // 2b16:021c Font_Init: fonts 9 and 10, font 0 selected.
    static Fonts load(const Archive& archive);
    Fonts(GgFont ui, GgFont small);

    // 2b16:0256 Font_Select(n): the font, its colour as the glyph colour; returns the previous index.
    int select(int n);
    int index() const { return index_; }
    // 2b16:0243 Font_SetColor(c): the selected font's colour and the glyph colour.
    void setColor(int c);
    uint8_t glyphColor() const { return static_cast<uint8_t>(glyphColor_); }
    // 2b16:0426 Font_GetCurrent.
    GgFont& current() { return fonts_[static_cast<size_t>(index_)]; }
    const GgFont& current() const { return fonts_[static_cast<size_t>(index_)]; }
    GgFont& font(int n) { return fonts_[static_cast<size_t>(n & 1)]; }

    // 2b16:03ea Font_CharWidth, 2b16:0418 Font_GetHeight of the selected font.
    int charWidth(uint8_t c) const { return current().charWidth(c); }
    int height() const { return current().lineHeight(); }
    // 2b16:0368 Font_DrawChar(x, y, c): a space draws nothing and returns 3; a code whose width is 0
    // draws nothing and returns 0; else the glyph at (x, y + y offset) in the glyph colour
    // (Gfx_DrawGlyph; Lpx_DrawKeyed mode 0 for a picture font), returning width + spacing.
    int drawChar(Screen& screen, int x, int y, uint8_t c) const;
    // 2b16:029c Font_DrawString(x, y, s): bytes until NUL (or the span's end); CR or LF: y += height +
    // 2, x = the start x; a space: x += 3; a byte >= 0x80, a code past the records or without a glyph:
    // skipped, no advance; else the glyph and x += width + spacing. No escapes, no wrapping.
    void drawString(Screen& screen, int x, int y, std::span<const uint8_t> s) const;

private:
    GgFont fonts_[2];
    int index_ = 0;
    int glyphColor_ = 0;
};

}  // namespace tmport::gg
