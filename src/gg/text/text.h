#pragma once
// The text renderer of Gizmos & Gadgets (ovl03 2b89; re/gg/docs/code_ui.md §4): Text_Render and its
// wrappers, on the shared TLC text engine (text/text_engine.h) with TextDialect::gizmos().
//
// Text_Render measures and draws in one routine: CR/LF break the line, a space is the only wrap
// point (wrap when pen + 3 + Text_MeasureRun(next word) > x + w - 1; the measure adds the raw width
// + 1 per byte, not the advance, so with font 9 a word draws one pixel per character wider than it
// measures), FF 02 .. FF toggles the underline (drawn with Gfx_HLine in g_drawColor), FF 03 c .. FF
// sets the font colour c - 1 (also when only measuring; not restored), FF 05 .. FF toggles centring,
// FF 06 .. FF is a comment, FF 01 n substitutes (no caller passes a table); nothing is clipped. The
// extent is (right-most pen - x, last line - y + line height). The selected font draws (Fonts).

#include <cstdint>
#include <span>
#include <string_view>

#include "text/text_engine.h"

namespace tmport::gg {

class Fonts;
class Screen;

// The selected font as Text_Render sees it: Font_GetHeight, Font_CharWidth and Text_MeasureRun's
// raw width + 1 (2b89:03ca).
class FontMetrics final : public text::TextFont {
public:
    explicit FontMetrics(const Fonts& fonts) : fonts_(fonts) {}
    int lineHeight() const override;
    int charWidth(uint8_t c) const override;
    int runWidth(uint8_t c) const override;

private:
    const Fonts& fonts_;
};

// The screen as Text_Render draws on it: Font_DrawChar, the underline in g_drawColor, Font_SetColor.
class FontCanvas final : public text::TextCanvas {
public:
    FontCanvas(Screen& screen, Fonts& fonts) : screen_(screen), fonts_(fonts) {}
    int drawGlyph(int x, int y, uint8_t c) override;
    void underline(int x, int y, int w) override;
    void setColour(int colour) override;

private:
    Screen& screen_;
    Fonts& fonts_;
};

// 2b89:0000 Text_DrawBox(x, y, w, s) = Text_Render(x, y, w, s, NULL, 0, NULL, NULL, 1); returns the
// extent the original computes (and discards).
text::TextExtent textDrawBox(Screen& screen, Fonts& fonts, int x, int y, int w, std::span<const uint8_t> s);
// 2b89:002a Text_Measure(s, w, &outW, &outH) = Text_Render(0, 0, w, s, ..., 0): FF 03 still changes
// the font colour, as in the original.
text::TextExtent textMeasure(Fonts& fonts, std::span<const uint8_t> s, int w);
// 2b89:0056 Text_Width(s) = the width of Text_Measure(s, 320).
int textWidth(Fonts& fonts, std::span<const uint8_t> s);

inline std::span<const uint8_t> textBytesOf(std::string_view s) { return text::asBytes(s); }

}  // namespace tmport::gg
