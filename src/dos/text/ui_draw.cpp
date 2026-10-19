#include "dos/text/ui_draw.h"

#include <algorithm>

#include "dos/data/dgroup_offsets.h"
#include "dos/data/dos_exe.h"
#include "dos/gfx/back_buffer.h"
#include "dos/text/dos_font.h"
#include "dos/text/text_draw.h"

namespace tmport::dos {

namespace {

constexpr uint16_t kDsBoxTable = dg::kMsgBoxes;  // 23 records of 16 bytes
constexpr uint16_t kDsMenuX0 = 0x5BC7, kDsMenuX1 = 0x5BCD;   // 3 words each
constexpr uint16_t kDsMenuGlyph = dg::kMenuTitleGlyph1, kDsMenuKeyGlyph = dg::kMenuTitleGlyph2;  // 6 words each (off, on)

// String behind a far pointer stored in DGROUP (nullopt for a NULL pointer).
std::optional<std::string_view> farString(const DosExe& exe, uint16_t dsOff) {
    if (exe.ds16(dsOff) == 0 && exe.ds16(static_cast<uint16_t>(dsOff + 2)) == 0) return std::nullopt;
    const auto b = exe.farPtrAtDs(dsOff);
    const auto nul = std::find(b.begin(), b.end(), uint8_t{0});
    return std::string_view(reinterpret_cast<const char*>(b.data()), static_cast<size_t>(nul - b.begin()));
}

}  // namespace

void hline(BackBuffer& buf, int x, int y, int w, uint8_t fill) { buf.fillRect(x, y, x + w - 1, y, fill); }
void vline(BackBuffer& buf, int x, int y, int h, uint8_t fill) { buf.fillRect(x, y, x, y + h - 1, fill); }

void frameRect(BackBuffer& buf, int x1, int y1, int x2, int y2, uint8_t fill) {
    buf.fillRect(x1, y1, x1, y2, fill);
    buf.fillRect(x1, y1, x2, y1 + 1, fill);
    buf.fillRect(x2, y1, x2, y2, fill);
    buf.fillRect(x1, y2 - 1, x2, y2, fill);
}

void exchangeColors(BackBuffer& buf, int x1, int y1, int x2, int y2, uint16_t a, uint16_t b) {
    buf.replaceColor(x1, y1, x2, y2, a, 0x3333);
    buf.replaceColor(x1, y1, x2, y2, b, a);
    buf.replaceColor(x1, y1, x2, y2, 0x3333, b);
}

BoxTexts BoxTexts::fromExe(const DosExe& exe) {
    BoxTexts t;
    t.escFooter = farString(exe, 0x5256).value_or("");
    t.enterLeft = farString(exe, 0x525A).value_or("");
    t.enterFooter = farString(exe, 0x525E).value_or("");
    t.no = exe.dsString(0x5D00);
    t.yes = exe.dsString(0x5D03);
    t.type5Left = exe.dsString(0x5D07);
    t.type5Right = exe.dsString(0x5D09);
    return t;
}

// 43cb:0592 MsgBox_Draw, the measuring part.
BoxGeometry msgBoxGeometry(const DosFont& font, int type, int x1, int y1, std::optional<std::string_view> title,
                           std::string_view text) {
    BoxGeometry g;
    g.type = type;
    int width = (title ? textWidth(font, *title) : 0) + 0x18;
    // Lines end at NUL, CR or LF; every byte & 0x7F of a line counts as a glyph (43cb:0650).
    int lines = 0;
    size_t i = 0;
    char term = text.empty() ? '\0' : text[0];
    while (term != '\0') {
        int lw = 0;
        for (;; ++i) {
            term = i < text.size() ? text[i] : '\0';
            if (term == '\0' || term == '\r' || term == '\n') break;
            lw += font.advance(static_cast<uint8_t>(term) & 0x7F);
        }
        width = std::max(width, lw + 0x0F);
        ++i;
        ++lines;
    }
    width = std::max(width, type == 3 ? 0x5C : 0x4B);
    int height = lines * 12 + 0x0F;
    if (title) height += 0x0E;
    if (type == 1 || type == 3) height += 0x0C;
    if (type != 7) height += 0x13;
    if (x1 < 0) x1 = (0xA0 - width) / 2;
    if (y1 < 0) y1 = (200 - height) / 2;
    g.x1 = x1;
    g.y1 = y1;
    g.x2 = x1 + width;
    g.y2 = y1 + height;
    g.width = width;
    g.height = height;
    g.textY = y1 + (title ? 0x17 : 9);
    g.yesX = g.x2 - 0x3A;
    g.noX = g.x2 - 0x1C;
    g.buttonY = g.y2 - 0x23;
    return g;
}

void drawBoxFrame(BackBuffer& buf, const DosFont& font, const TextStyle& style, const BoxTexts& texts, int type, int x1,
                  int y1, int x2, int y2, std::optional<std::string_view> title) {
    buf.fillRect(x1, y1 - 2, x2, y2, 0xCC);
    frameRect(buf, x1 + 1, y1, x2 - 2, y2 - 4, 0xFF);
    buf.fillRect(x2 - 1, y1 + 2, x2 - 1, y2 - 2, 0xDD);  // shadow
    buf.fillRect(x1 + 2, y2 - 3, x2 - 2, y2 - 2, 0xDD);
    if (title) {
        hline(buf, x1 + 1, y1 + 0x0F, (x2 - x1) - 2, 0xFF);
        textDraw(buf, font, style, x2 - (x2 - x1) / 2 - textWidth(font, *title) / 2, y1 + 4, *title);
        if (type == 5) {  // unused by the box table
            vline(buf, x1 + 0x0B, y1 + 1, 0x0F, 0xFF);
            vline(buf, x2 - 0x0C, y1 + 1, 0x0F, 0xFF);
            textDraw(buf, font, style, x1 + 3, y1 + 4, texts.type5Left);
            textDraw(buf, font, style, x2 - 10, y1 + 4, texts.type5Right);
        }
    }
    // Footer by type (switch table CS:09DF).
    std::string_view left, centre;
    int offset = 0;
    switch (type) {
        case 0: case 1: case 5: case 6: centre = texts.escFooter; offset = 0x12; break;
        case 3: centre = texts.escFooter; left = texts.enterLeft; offset = 0x12; break;
        case 2: offset = 0x15; break;
        case 4: centre = texts.enterFooter; offset = 0x12; break;
        default: break;  // 7: nothing
    }
    if (offset != 0) hline(buf, x1 + 1, y2 - offset, (x2 - x1) - 2, 0xFF);
    textDraw(buf, font, style, x1 + 7, y2 - 0x20, left);
    textDraw(buf, font, style, x2 - (x2 - x1) / 2 - textWidth(font, centre) / 2, y2 - 0x10, centre);
}

void drawButton(BackBuffer& buf, const DosFont& font, const TextStyle& style, std::string_view label, int x, int y, bool selected) {
    uint8_t face = 0xCC, leftEdge = 0xFC, rightEdge = 0xCF;
    TextStyle s = style;
    if (selected) {
        face = leftEdge = rightEdge = 0xFF;
        s.ink = 0xCCCC;
    }
    const int tw = textWidth(font, label);
    const int bw = tw < 0x14 ? 0x14 : 0x33;
    const int xr = x + bw;
    buf.fillRect(x - 2, y + 2, xr + 2, y + 0x0C, 0xFF);
    vline(buf, x - 3, y + 3, 9, 0xCF);  // rounded corners
    vline(buf, xr + 3, y + 3, 9, 0xFC);
    buf.fillRect(x - 1, y + 3, xr + 1, y + 0x0B, face);
    vline(buf, x - 2, y + 3, 9, leftEdge);
    vline(buf, xr + 2, y + 3, 9, rightEdge);
    textDraw(buf, font, s, x + (bw - tw) / 2, y + 3, label);
}

BoxGeometry drawMsgBox(BackBuffer& buf, const DosFont& font, const DosExe& exe, const BoxTexts& texts, int n,
                       std::optional<int> selectedButton, int* lines) {
    const auto rec = static_cast<uint16_t>(kDsBoxTable + 16 * n);
    const int type = exe.ds8(rec);
    const auto title = farString(exe, static_cast<uint16_t>(rec + 8));
    const std::string_view text = farString(exe, static_cast<uint16_t>(rec + 12)).value_or("");
    const BoxGeometry g = msgBoxGeometry(font, type, exe.dsS16(static_cast<uint16_t>(rec + 2)),
                                         exe.dsS16(static_cast<uint16_t>(rec + 4)), title, text);
    const TextStyle style{0xFFFF, 12};
    drawBoxFrame(buf, font, style, texts, type, g.x1, g.y1, g.x2, g.y2, title);
    if (type == 1) {
        const int sel = selectedButton.value_or(exe.ds8(static_cast<uint16_t>(rec + 6)));
        drawButton(buf, font, style, texts.no, g.noX, g.buttonY, sel == 0);
        drawButton(buf, font, style, texts.yes, g.yesX, g.buttonY, sel == 1);
    }
    const int count = textDraw(buf, font, style, g.x1 + 7, g.textY, text);
    if (lines) *lines = count;
    return g;
}

void drawMenuTitle(BackBuffer& buf, const DosFont& font, TextStyle& style, const DosExe& exe, int i, bool on) {
    const int x0 = exe.ds16(static_cast<uint16_t>(kDsMenuX0 + 2 * i));
    const int x1 = exe.ds16(static_cast<uint16_t>(kDsMenuX1 + 2 * i));
    int k = 0;
    if (!on) {
        buf.fillRect(x0, 0, x1, 10, 0xFF);
        buf.fillRect(x0, 1, x1 - 1, 10, 0xDD);
        buf.fillRect(x0 + 1, 0, x1, 9, 0xCC);
        style.ink = 0xFFFF;
    } else {
        buf.fillRect(x0, 0, x1, 10, 0xFF);
        if (i < 2) buf.fillRect(x1, 1, x1, 10, 0xFD);
        buf.fillRect(x0, 1, x1 - 1, 10, 0x33);
        style.ink = 0xCCCC;
        k = 3;
    }
    font.drawChar(buf, style.ink, x0, 0, exe.ds16(static_cast<uint16_t>(kDsMenuGlyph + 2 * (i + k))));
    font.drawChar(buf, style.ink, x0 + 0x12, 0, exe.ds16(static_cast<uint16_t>(kDsMenuKeyGlyph + 2 * (i + k))));
}

void highlightMenuItem(BackBuffer& buf, const DosExe& exe, int menu, int item, const BoxGeometry& box) {
    const int mx = exe.dsS16(static_cast<uint16_t>(kDsBoxTable + 16 * menu + 2));
    const int x1 = mx + 2, y1 = item * 12 + box.textY - 1;
    const int x2 = mx + box.width - 3, y2 = y1 + 12;
    buf.replaceColor(x1, y1, x2, y2, 0xCCCC, 0x3333);
    buf.replaceColor(x1, y1, x2, y2, 0xFFFF, 0xCCCC);
    buf.replaceColor(x1, y1, x2, y2, 0x3333, 0xFFFF);
}

}  // namespace tmport::dos
