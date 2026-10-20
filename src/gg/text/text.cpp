#include "gg/text/text.h"

#include "gg/gfx/screen.h"
#include "gg/text/font.h"

namespace tmport::gg {

// 2b16:0418 Font_GetHeight
int FontMetrics::lineHeight() const { return fonts_.height(); }
// 2b16:03ea Font_CharWidth
int FontMetrics::charWidth(uint8_t c) const { return fonts_.charWidth(c); }
// 2b89:03ca Text_MeasureRun: the width table + 1 per byte (a space measures 1)
int FontMetrics::runWidth(uint8_t c) const { return fonts_.current().width(c) + 1; }

// 2b16:0368 Font_DrawChar
int FontCanvas::drawGlyph(int x, int y, uint8_t c) { return fonts_.drawChar(screen_, x, y, c); }
// 2b89:0466 Text_DrawChar's underline: Gfx_HLine in g_drawColor
void FontCanvas::underline(int x, int y, int w) { screen_.hLine(x, y, w); }
// 2b16:0243 Font_SetColor
void FontCanvas::setColour(int colour) { fonts_.setColor(colour); }

namespace {

// A canvas for measuring only: FF 03 still reaches Font_SetColor.
class MeasureCanvas final : public text::TextCanvas {
public:
    explicit MeasureCanvas(Fonts& fonts) : fonts_(fonts) {}
    int drawGlyph(int, int, uint8_t) override { return 0; }
    void underline(int, int, int) override {}
    void setColour(int colour) override { fonts_.setColor(colour); }

private:
    Fonts& fonts_;
};

}  // namespace

// 2b89:0000 Text_DrawBox
text::TextExtent textDrawBox(Screen& screen, Fonts& fonts, int x, int y, int w, std::span<const uint8_t> s) {
    FontMetrics metrics(fonts);
    FontCanvas canvas(screen, fonts);
    text::TextEngine engine(metrics, &canvas, text::TextDialect::gizmos());
    return engine.render(x, y, w, text::untilNul(s), {}, false, true);
}

// 2b89:002a Text_Measure
text::TextExtent textMeasure(Fonts& fonts, std::span<const uint8_t> s, int w) {
    FontMetrics metrics(fonts);
    MeasureCanvas canvas(fonts);
    text::TextEngine engine(metrics, &canvas, text::TextDialect::gizmos());
    return engine.render(0, 0, w, text::untilNul(s), {}, false, false);
}

// 2b89:0056 Text_Width
int textWidth(Fonts& fonts, std::span<const uint8_t> s) { return textMeasure(fonts, s, 320).width; }

}  // namespace tmport::gg
