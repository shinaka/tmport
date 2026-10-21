#include "text/text_renderer.h"

#include "gfx/frame_buffer.h"
#include "mischief/gfx/presenter.h"

namespace tmport::text {

// 10b0:0080 Text_Width
int TextRenderer::width(mischief::text::Bytes s) {
    if (s.empty() || s[0] == 0) return 0;
    return measure(s, {}, kScreenWidth).width;
}

// 10b0:0662 Text_DrawChar (always with the frame buffer, also when none was given for render)
int TextRenderer::drawChar(int x, int y, int lineH, uint8_t c, bool underline, bool flush) {
    mischief::text::TextEngine e(metrics_, &canvas_);
    return e.drawChar(x, y, lineH, c, underline, flush);
}

int TextRenderer::Canvas::drawGlyph(int x, int y, uint8_t c) { return font.drawChar(*fb, x, y, c); }

void TextRenderer::Canvas::underline(int x, int y, int w) { fb->hline(x, y, w); }

void TextRenderer::Canvas::setColour(int colour) { fb->setForeColor(colour); }

void TextRenderer::Canvas::flushGlyph(int x, int y, int w, int h) {
    if (presenter) presenter->blitToWindow(*fb, x, y, w, h);
}

}  // namespace tmport::text
