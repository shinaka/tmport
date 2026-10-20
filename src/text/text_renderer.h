#pragma once
// Text_Render (10b0:00b5) and its wrappers: the game's word-wrapping proportional text engine
// (re/docs/code_1098-10d8.md §7, fmt_bndl_ffnt.md §4-5). Measuring and drawing share one
// routine, exactly as in the original, so box sizes, centring and line breaks agree everywhere.
// The routine is the shared core's TextEngine (text/text_engine.h, TextDialect::treasureMountain())
// with the FFNT font and the frame buffer behind its interfaces.
//
// Strings are raw bytes (never transcoded), NUL-terminated in the original. Here a string is a
// span; the end of the span acts as the terminating NUL. Control bytes:
//   00         end of string
//   09 TAB     x = first tab stop x0 + tab[k] > x (default 40, 80, ... 320); unchanged if none
//   0A, 0D     new line: y += line height; x = x0, or the centred start of the next line
//   20         word-wrap point: wrap when x + spaceW + width(next word) > x0 + w - 1
//   FE         x = x0 (no line feed)
//   FF 01 n FF substitute subst[n-1] (glyphs only, wrap at its spaces)
//   FF 02 FF   toggle underline ('_' becomes an underlined blank)
//   FF 03 n FF pen colour n-1 (EGA number; also while only measuring, as in the original)
//   FF 04 ..FF up to 8 tab stops, a u16 read at every byte position (quirk kept)
//   FF 05 FF   toggle centring; switching it on re-centres the current line at once
//   FF 06 ..FF comment (skipped)
//   FF k       k = 0 or k > 6: only k is consumed
// There is no clipping (pixels outside the frame buffer are skipped for memory safety only) and
// no typewriter delay (timer slot 6 is never armed).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include "text/font.h"
#include "text/text_engine.h"

namespace tmport::gfx {
class FrameBuffer;
class Presenter;
}  // namespace tmport::gfx

namespace tmport::text {

class TextRenderer {
public:
    static constexpr int kScreenWidth = 512;  // box width of Text_Draw / Text_Width

    // fb: drawing target and owner of the current pen colour (nullptr: measure only).
    // presenter: target of the per-glyph present when `flush` is set (nullptr: none).
    explicit TextRenderer(const Font& font, gfx::FrameBuffer* fb = nullptr, gfx::Presenter* presenter = nullptr)
        : metrics_(font), canvas_(font, fb, presenter), engine_(metrics_, fb ? &canvas_ : nullptr) {}
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    const Font& font() const { return metrics_.font; }
    gfx::FrameBuffer* frameBuffer() const { return canvas_.fb; }

    // 10b0:0000 Text_Draw: box width 512, no substitution, no flush.
    void draw(int x, int y, Bytes s) { render(x, y, kScreenWidth, s, {}, false, true); }
    // 10b0:0023 Text_DrawBox
    void drawBox(int x, int y, int wrapW, Bytes s, SubstTable subst = {}, bool flush = false) {
        render(x, y, wrapW, s, subst, flush, true);
    }
    // 10b0:0050 Text_Measure: layout at (0,0) without drawing.
    TextExtent measure(Bytes s, SubstTable subst = {}, int wrapW = kScreenWidth) {
        return render(0, 0, wrapW, s, subst, false, false);
    }
    // 10b0:0080 Text_Width: 0 for an empty string, else the measured width in a 512 box.
    int width(Bytes s);

    // 10b0:00b5 Text_Render: the core. Returns the extent the original writes to outW/outH.
    TextExtent render(int x0, int y0, int wrapW, Bytes s, SubstTable subst, bool flush, bool draw) {
        return engine_.render(x0, y0, wrapW, s, subst, flush, draw);
    }

    // 10b0:053c Text_MeasureRun: width of the bytes from s[pos] up to `stop`, NUL, LF or CR,
    // following FF 01 substitutions (other escapes add nothing; TAB and FE add their glyph
    // widths). With inSubst the scan starts inside substStr at substPos and resumes in s at pos
    // when the substitution ends.
    int measureRun(uint8_t stop, Bytes s, size_t pos, SubstTable subst, bool inSubst = false, Bytes substStr = {},
                   size_t substPos = 0) const {
        return engine_.measureRun(stop, s, pos, subst, inSubst, substStr, substPos);
    }

    // 10b0:0662 Text_DrawChar: glyph (or an underlined blank for '_' while underlining), the
    // underline on the cell's last row, the optional present. Returns the advance.
    int drawChar(int x, int y, int lineH, uint8_t c, bool underline, bool flush);

    // Debug/test hook: called for every glyph placed (drawn or measured) with its byte, cell
    // position and advance.
    using GlyphObserver = TextEngine::GlyphObserver;
    void setGlyphObserver(GlyphObserver f) { engine_.setGlyphObserver(std::move(f)); }

private:
    // The FFNT font's metrics (1068:00d9 Font_GetHeight, 1068:00dd Font_CharWidth).
    struct Metrics final : TextFont {
        explicit Metrics(const Font& f) : font(f) {}
        int lineHeight() const override { return font.height(); }
        int charWidth(uint8_t c) const override { return font.charWidth(c); }
        const Font& font;
    };
    // The frame buffer's pen and the presenter (Font_DrawChar 1068:00f6, Gfx_DrawHLine 1058:090f,
    // Pal_SetForeColor 1048:02fd, Gfx_BlitToWindow 1058:05bc).
    struct Canvas final : TextCanvas {
        Canvas(const Font& f, gfx::FrameBuffer* b, gfx::Presenter* p) : font(f), fb(b), presenter(p) {}
        int drawGlyph(int x, int y, uint8_t c) override;
        void underline(int x, int y, int w) override;
        void setColour(int colour) override;
        void flushGlyph(int x, int y, int w, int h) override;
        const Font& font;
        gfx::FrameBuffer* fb;
        gfx::Presenter* presenter;
    };

    Metrics metrics_;
    Canvas canvas_;
    TextEngine engine_;
};

}  // namespace tmport::text
