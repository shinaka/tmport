#pragma once
// The TLC text engine, shared by both games: Text_Render of Treasure Mountain Windows (10b0:00b5,
// re/docs/code_1098-10d8.md §7; text/text_renderer.h) and of Gizmos & Gadgets (2b89:007a, the same
// module in the same function order; re/gg/docs/code_ui.md §4, §20). One routine measures and
// draws, so box sizes, centring and line breaks agree everywhere.
//
// Strings are raw bytes (never transcoded), NUL-terminated in the originals. Here a string is a
// span; the end of the span acts as the terminating NUL. Control bytes (both games unless noted):
//   00         end of string
//   0A, 0D     new line: y += line height; x = x0, or the centred start of the next line
//   20         word-wrap point: wrap when x + spaceW + run(next word) > x0 + w - 1; the space is not
//              drawn then, and the spaces and TABs after it are skipped (also in G&G, which has no
//              TAB code: a leftover of TM's code)
//   FF 01 n FF substitute subst[n-1] (glyphs only, wrap at its spaces); G&G never passes a table
//   FF 02 FF   toggle underline
//   FF 03 n FF colour n-1 (also while only measuring, as in the originals; not restored)
//   FF 05 FF   toggle centring; switching it on re-centres the current line at once
//   FF 06 ..FF comment (skipped)
//   FF k       k = 0 or k > 6 (G&G: also k = 4): only k is consumed
//   TM only (TextDialect::tabStops, marginReturn):
//   09 TAB     x = first tab stop x0 + tab[k] > x (default 40, 80, ... 320); unchanged if none
//   FE         x = x0 (no line feed)
//   FF 04 ..FF up to 8 tab stops, a u16 read at every byte position (quirk kept)
// There is no clipping and no typewriter delay (TM's Timer_Wait(6) waits on a never-armed slot).
//
// The font (TextFont: metrics) and the drawing target (TextCanvas: glyphs, the underline, the colour
// of FF 03, TM's per-glyph present) are interfaces; the differences between the games' versions of
// the routine are a TextDialect.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace tmport::text {

using Bytes = std::span<const uint8_t>;
// The caller-supplied substitution table of FF 01 n FF (char far* far*; empty = NULL).
using SubstTable = std::span<const Bytes>;

inline Bytes asBytes(std::string_view s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
// The part of b before its first NUL (the whole span if it has none).
Bytes untilNul(Bytes b);

struct TextExtent {
    int width = 0;   // TM: maxX - minX over the glyphs placed (1 - w if none); G&G: maxX - x0 (0 if none)
    int height = 0;  // last line y - y0 + line height
    friend bool operator==(const TextExtent&, const TextExtent&) = default;
};

// What differs between the games' Text_Render (re/gg/docs/code_ui.md §20). The defaults are TM's.
struct TextDialect {
    bool tabStops = true;          // TM: 09 jumps to a tab stop, FF 04 .. FF sets them; G&G: 09 is a glyph, FF 04 a no-op
    bool marginReturn = true;      // TM: FE sets x = x0; G&G: FE is a glyph
    bool underscoreBlank = true;   // TM: an underlined '_' is an underlined blank; G&G: a glyph
    bool centreSetsExtent = false; // G&G: a centred line start counts for the width (maxX = max(maxX, x))
    bool extentFromX0 = false;     // G&G: width = maxX - x0; TM: maxX - minX
    // Text_MeasureRun inside a substitution entered from the main text: TM stops at the delimiter;
    // G&G does not (it clears AH, 2b89:03ca), except in the substitution the run starts in.
    bool substRunStopsAtDelimiter = true;

    static TextDialect treasureMountain() { return {}; }
    static TextDialect gizmos() { return {false, false, false, true, true, false}; }
};

// Font metrics as Text_Render uses them.
class TextFont {
public:
    virtual ~TextFont() = default;
    // The line height (TM: Font_GetHeight 1068:00d9; G&G: Font_GetHeight 2b16:0418 = height + 1).
    virtual int lineHeight() const = 0;
    // The advance of byte c when only measuring (TM: Font_CharWidth 1068:00dd; G&G: Font_CharWidth
    // 2b16:03ea: 3 for a space, width + spacing otherwise, also for codes without a glyph).
    virtual int charWidth(uint8_t c) const = 0;
    // The advance Text_MeasureRun adds per byte (TM: charWidth; G&G 2b89:03ca: the raw width + 1).
    virtual int runWidth(uint8_t c) const { return charWidth(c); }
};

// The drawing target.
class TextCanvas {
public:
    virtual ~TextCanvas() = default;
    // Draws glyph c with its cell's top-left at (x, y) and returns the advance (TM: Font_DrawChar
    // 1068:00f6 in the pen colour; G&G: Font_DrawChar 2b16:0368 in the font's colour, 3 for a space
    // and 0 for a code without a glyph).
    virtual int drawGlyph(int x, int y, uint8_t c) = 0;
    // The underline of a cell: a horizontal line of w pixels at (x, y) (TM: Gfx_DrawHLine 1058:090f
    // in the pen colour; G&G: Gfx_HLine in g_drawColor).
    virtual void underline(int x, int y, int w) = 0;
    // FF 03 n FF: colour n - 1 (TM: Pal_SetForeColor; G&G: Font_SetColor 2b16:0243).
    virtual void setColour(int colour) = 0;
    // TM's per-glyph present (Gfx_BlitToWindow 1058:05bc) when the caller asked for it.
    virtual void flushGlyph(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {}
};

class TextEngine {
public:
    // `font` and `canvas` (nullptr: measure only) must outlive the engine.
    explicit TextEngine(const TextFont& font, TextCanvas* canvas = nullptr, const TextDialect& dialect = {})
        : font_(font), canvas_(canvas), dialect_(dialect) {}

    const TextFont& font() const { return font_; }
    TextCanvas* canvas() const { return canvas_; }
    const TextDialect& dialect() const { return dialect_; }

    // Text_Render: lays out (and with `draw` and a canvas, draws) s in a box of width wrapW at
    // (x0, y0). Returns the extent the original writes to outW/outH. `flush`: TM's per-glyph present.
    TextExtent render(int x0, int y0, int wrapW, Bytes s, SubstTable subst = {}, bool flush = false, bool draw = true);

    // Text_MeasureRun (TM 10b0:053c, G&G 2b89:03ca): width of the bytes from s[pos] up to `stop`,
    // NUL, LF or CR, following FF 01 substitutions (other escapes add nothing). With inSubst the
    // scan starts inside substStr at substPos and resumes in s at pos when the substitution ends.
    int measureRun(uint8_t stop, Bytes s, size_t pos, SubstTable subst = {}, bool inSubst = false, Bytes substStr = {},
                   size_t substPos = 0) const;

    // Text_DrawChar (TM 10b0:0662, G&G 2b89:0466): the glyph (or TM's underlined blank for '_'), the
    // underline on the cell's last row, the optional present. Returns the advance. Needs a canvas.
    int drawChar(int x, int y, int lineH, uint8_t c, bool underline, bool flush);

    // Debug/test hook: called for every glyph placed (drawn or measured) with its byte, cell
    // position and advance.
    using GlyphObserver = std::function<void(uint8_t c, int x, int y, int advance)>;
    void setGlyphObserver(GlyphObserver f) { observer_ = std::move(f); }

private:
    const TextFont& font_;
    TextCanvas* canvas_;
    TextDialect dialect_;
    GlyphObserver observer_;
};

}  // namespace tmport::text
