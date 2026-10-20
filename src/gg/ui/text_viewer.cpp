#include "gg/ui/text_viewer.h"

#include <algorithm>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"

namespace tmport::gg {

namespace {

uint8_t at(std::span<const uint8_t> t, size_t i) { return i < t.size() ? t[i] : 0; }

// Skips an escape: from the FF at i to just past the next FF (or the end).
size_t skipEscape(std::span<const uint8_t> t, size_t i) {
    ++i;
    while (i < t.size() && t[i] != 0xFF) ++i;
    return i + 1;
}

Zone zoneAt(const GgExe& exe, uint16_t off) { return Zone::fromRecord(exe.dsBytes(off, Zone::kRecordSize)); }

}  // namespace

// 4140:01ff TextView_WordWidth
int TextViewLayout::wordWidth(const Fonts& fonts, std::span<const uint8_t> text, size_t i) {
    int w = 0;
    for (;;) {
        const uint8_t c = at(text, i);
        if (c == 0 || c == 0x09 || c == 0x0A || c == 0x0D || c == 0x20) return w;
        if (c == 0xFF) {
            i = skipEscape(text, i);
            continue;
        }
        w += fonts.charWidth(c);
        ++i;
    }
}

// 4140:01bc TextView_GlyphVisible
bool TextViewLayout::glyphVisible(const Fonts& fonts, int x, int y, uint8_t c) {
    if (x < kLeft || y < kTop) return false;
    if (x + fonts.charWidth(c) >= kRight) return false;
    return y + fonts.height() - 1 < kBottom;
}

// 4140:03d7 TextView_CountLines
int TextViewLayout::countLines(const Fonts& fonts, std::span<const uint8_t> text) {
    int lines = 0, x = kLeft;
    bool wordStart = false;
    size_t i = 0;
    for (;;) {
        const uint8_t c = at(text, i);
        if (c == 0) return lines;
        if (c == 0x20) {
            x += fonts.charWidth(0x20);
            wordStart = true;
        } else if (c == 0x0A || c == 0x0D) {
            ++lines;
            x = kLeft;
            wordStart = true;
        } else if (c == 0xFF) {
            i = skipEscape(text, i);
            continue;
        } else {
            if (wordStart) {
                const int ww = wordWidth(fonts, text, i);
                if (x + ww >= kRight && ww < kLongWord) {  // wrap; the byte is examined again
                    ++lines;
                    x = kLeft;
                    continue;
                }
            }
            wordStart = false;
            x += fonts.charWidth(c);  // no visibility test here (a glyph-less code adds the spacing)
        }
        ++i;
    }
}

// 4140:027a TextView_DrawText
void TextViewLayout::drawText(Screen& screen, const Fonts& fonts, std::span<const uint8_t> text, int first) {
    int line = 0, x = kLeft, y = kTop;
    bool wordStart = false;
    size_t i = 0;
    for (;;) {
        const uint8_t c = at(text, i);
        if (c == 0) return;
        if (c == 0x20) {
            x += fonts.charWidth(0x20);
            wordStart = true;
        } else if (c == 0x0A || c == 0x0D) {
            if (++line > first) y += fonts.height();
            x = kLeft;
            wordStart = true;
        } else if (c == 0xFF) {
            i = skipEscape(text, i);  // every escape is skipped, FF 03 colours included
            continue;
        } else {
            if (wordStart) {
                const int ww = wordWidth(fonts, text, i);
                if (x + ww >= kRight && ww < kLongWord) {  // wrap; the byte is examined again
                    if (++line > first) y += fonts.height();
                    x = kLeft;
                    continue;
                }
            }
            wordStart = false;
            if (fonts.charWidth(c) != 0 && glyphVisible(fonts, x, y, c)) {
                if (line >= first) fonts.drawChar(screen, x, y, c);
                x += fonts.charWidth(c);
            }
        }
        ++i;
    }
}

TextViewer::TextViewer(const GgExe& exe, const Archive& archive, std::span<const uint8_t> text)
    : text_(text),
      arrows_(Bundle::load(archive, kTextViewArrows)),  // Res_LoadCopy
      panel_(archive.read(kTextViewPanel)),              // Res_Read
      upZone_(zoneAt(exe, dg::kViewUpZone)),
      downZone_(zoneAt(exe, dg::kViewDownZone)),
      textZone_(zoneAt(exe, dg::kViewTextZone)),
      closeZone_(zoneAt(exe, dg::kViewCloseZone)),
      pageUpZone_(zoneAt(exe, dg::kViewPageUpZone)),
      thumbZone_(zoneAt(exe, dg::kViewThumbZone)),
      pageDownZone_(zoneAt(exe, dg::kViewPageDownZone)) {}

// 4140:0667 TextView_Run, the set-up
void TextViewer::open(const UiCanvas& ui, ClickZones* zones) {
    zones_ = zones;
    ui.screen.getImage(0, 28, 129, 138, saved_);  // into g_resBuffer + 30000 in the original
    ui.screen.setDrawColor(0);
    ui.fonts.select(1);
    ui.fonts.setColor(0);
    if (zones) {
        zones->addLocal(closeZone_);
        zones->addLocal(textZone_);
        zones->addLocal(upZone_);
        zones->addLocal(downZone_);
    }
    scrollMax_ = std::max(TextViewLayout::countLines(ui.fonts, text_) - 10, 1);
    if (zones && scrollMax_ != 1) {
        zones->addLocal(pageUpZone_);
        zones->addLocal(thumbZone_);
        zones->addLocal(pageDownZone_);
    }
    thumbStep_ = 104 / scrollMax_;  // 4140:00f3 TextView_SetThumbStep
    setThumbY(45);
    line_ = 0;
    redraw(ui, false, false);
}

// 4140:04e5 TextView_DrawArrow
void TextViewer::drawArrow(const UiCanvas& ui, bool down, bool pressed) {
    const size_t member = (pressed ? 2 : 0) + (down ? 1 : 0);
    ui.screen.lpxDrawKeyed(117, down ? 149 : 29, arrows_.member(member).payload, 0);
}

// 4140:052f TextView_DrawPanel
void TextViewer::drawPanel(const UiCanvas& ui, bool upPressed, bool downPressed) {
    ui.screen.lpxDrawKeyed(0, 28, panel_, 0);
    drawArrow(ui, false, upPressed);
    drawArrow(ui, true, downPressed);
}

// 4140:016e TextView_DrawBar
void TextViewer::drawBar(const UiCanvas& ui) {
    Screen& s = ui.screen;
    s.setDrawColor(15);
    s.fillRect(118, 45, 9, 104);
    s.fillRect(118, thumbY_, 9, 11);
    s.setDrawColor(0);
    s.frameRect(118, thumbY_, 9, 11);
    ui.dirtyFlush();
    if (zones_) updateBarZones(*zones_);
}

// 4140:0605 TextView_Redraw
void TextViewer::redraw(const UiCanvas& ui, bool upPressed, bool downPressed) {
    drawPanel(ui, upPressed, downPressed);
    TextViewLayout::drawText(ui.screen, ui.fonts, text_, line_);
    if (scrollMax_ != 1) drawBar(ui);
    ui.dirtyFlush();
}

void TextViewer::close(const UiCanvas& ui) {
    ui.screen.putImage(0, 28, saved_);
    ui.dirtyFlush();
}

// 4140:063d TextView_ScrollBy
void TextViewer::scrollBy(int delta) {
    line_ += delta;
    if (line_ < 0) line_ = 0;
    if (line_ >= scrollMax_) line_ = scrollMax_ - 1;
}

// 4140:00b1 TextView_ThumbDown
void TextViewer::thumbDown() {
    thumbY_ += thumbStep_;
    if (138 - thumbStep_ < thumbY_) thumbY_ = 138;
}

// 4140:00d3 TextView_ThumbUp
void TextViewer::thumbUp() {
    thumbY_ -= thumbStep_;
    if (thumbStep_ + 45 > thumbY_) thumbY_ = 45;
}

// 4140:0081 TextView_SetThumbY
void TextViewer::setThumbY(int y) { thumbY_ = std::clamp(y, 45, 138); }

// 4140:0102 TextView_UpdateBarZones
void TextViewer::updateBarZones(ClickZones& zones) {
    zones.removeLocal(&pageUpZone_);
    zones.removeLocal(&pageDownZone_);
    zones.removeLocal(&thumbZone_);
    if (thumbY_ != 45) {
        pageUpZone_.h = static_cast<int16_t>(thumbY_ - 45);
        zones.addLocal(pageUpZone_);
    }
    if (thumbY_ != 138) {
        pageDownZone_.y = static_cast<int16_t>(thumbY_ + 11);
        pageDownZone_.h = static_cast<int16_t>(138 - thumbY_);
        zones.addLocal(pageDownZone_);
    }
    thumbZone_.y = static_cast<int16_t>(thumbY_);
    zones.addLocal(thumbZone_);
}

}  // namespace tmport::gg
