// DOS edition text (src/dos/text): the font of SST.EXE, Font_DrawChar, Text_Draw / Text_Width with
// their control codes, Hud_DrawNumber, and the box geometry of ui.c. Synthetic fonts for the
// rules; the user's SST.EXE (TM_DATA_DOS) for the font and the box table (counts, geometry and
// hashes only).

#include <doctest/doctest.h>

#include <vector>

#include "dos/gfx/back_buffer.h"
#include "dos/gfx/blitter.h"
#include "dos/gfx/pak.h"
#include "dos/text/dos_font.h"
#include "dos/text/text_draw.h"
#include "dos/text/ui_draw.h"
#include "dos_test_data.h"
#include "mischief/util/hash.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

// 128 glyphs of height 2: width 4 with rows F0h/90h, except 'A' (8: FFh/81h), 'W' (16: FFFFh/8001h)
// and 'S' (width 4 but FFh/00h: bits past the width are drawn too).
DosFont testFont() {
    std::vector<uint8_t> widths(128, 4), data;
    widths['A'] = 8;
    widths['W'] = 16;
    std::vector<size_t> offsets;
    for (int c = 0; c < 128; ++c) {
        offsets.push_back(data.size());
        if (c == 'A') data.insert(data.end(), {0xFF, 0x81});
        else if (c == 'W') data.insert(data.end(), {0xFF, 0xFF, 0x80, 0x01});
        else if (c == 'S') data.insert(data.end(), {0xFF, 0x00});
        else data.insert(data.end(), {0xF0, 0x90});
    }
    std::vector<uint8_t> block = {7, 127, 2};
    block.insert(block.end(), widths.begin(), widths.end());
    for (size_t o : offsets) block.push_back(static_cast<uint8_t>(o));
    for (size_t o : offsets) block.push_back(static_cast<uint8_t>(o >> 8));
    block.insert(block.end(), data.begin(), data.end());
    std::vector<uint16_t> bpr(256);
    for (size_t w = 0; w < bpr.size(); ++w) bpr[w] = static_cast<uint16_t>(w <= 8 ? 1 : (w + 7) / 8);
    return DosFont::fromBlock(block, bpr);
}

}  // namespace

TEST_CASE("DOS font: Font_DrawChar expands whole bytes with the colour word's nibble positions") {
    const DosFont f = testFont();
    CHECK(f.count() == 128);
    CHECK(f.height() == 2);
    CHECK(f.advance('A') == 4);
    CHECK(f.bytesPerRow('W') == 2);
    BackBuffer b;
    CHECK(f.drawChar(b, 0x1234, 10, 5, 'A') == 4);
    CHECK(b.byteAt(10, 5) == 0x34);  // pixel 0 = bits 4..7, pixel 1 = bits 0..3 of the low byte
    CHECK(b.byteAt(11, 5) == 0x12);  // pixels 2, 3 from the high byte
    CHECK(b.byteAt(12, 5) == 0x34);
    CHECK(b.byteAt(13, 5) == 0x12);
    CHECK(b.byteAt(10, 6) == 0x30);  // clear bits keep the background
    CHECK(b.byteAt(13, 6) == 0x02);
    CHECK(b.byteAt(14, 5) == 0x00);
    b.clear(0x77);
    CHECK(f.drawChar(b, 0xCCCC, 0, 0, 'S') == 2);
    CHECK(b.byteAt(3, 0) == 0xCC);  // bits past the 4-pixel width are drawn
    CHECK(f.drawChar(b, DosFont::kMeasureOnly, 40, 40, 'A') == 4);
    CHECK(b.byteAt(40, 40) == 0x77);
    // A glyph past byte 159 continues at the start of the next row (linear addressing).
    b.clear(0);
    f.drawChar(b, 0xEEEE, 158, 0, 'W');
    CHECK(b.byteAt(159, 0) == 0xEE);
    CHECK(b.byteAt(0, 1) == 0xEE);
    CHECK(b.byteAt(5, 1) == 0xEE);
    CHECK(b.byteAt(158, 1) == 0xE0);  // row 1 of the glyph: first and last pixel
    CHECK(b.byteAt(5, 2) == 0x0E);
    CHECK(DosFont::expandNibble(8) == 0x00F0);
    CHECK(DosFont::expandNibble(1) == 0x0F00);
}

TEST_CASE("DOS text: Text_Draw control codes") {
    const DosFont f = testFont();
    BackBuffer b;
    TextStyle s{0x5555, 12};
    CHECK(textDraw(b, f, s, 10, 20, "ab\rc") == 2);
    CHECK(b.byteAt(10, 20) == 0x55);
    CHECK(b.byteAt(12, 20) == 0x55);
    CHECK(b.byteAt(10, 32) == 0x55);  // CR: back to the margin, one line height down
    CHECK(b.byteAt(12, 32) == 0x00);
    b.clear(0);
    CHECK(textDraw(b, f, s, 0, 0, "~030040x\ny") == 2);  // x = 30 / 2, y = 40; the margin moves too
    CHECK(b.byteAt(15, 40) == 0x55);
    CHECK(b.byteAt(15, 52) == 0x55);
    b.clear(0);
    textDraw(b, f, s, 0, 0, "`r`n");  // red, then the ink again
    CHECK(b.byteAt(0, 0) == 0x44);
    CHECK(b.byteAt(2, 0) == 0x55);
    b.clear(0);
    textDraw(b, f, s, 0, 0, "|12z|26z`r`q");  // |NN: byte (n << 4) | n; a back-quote off restores the ink
    CHECK(b.byteAt(0, 0) == 0xCC);
    CHECK(b.byteAt(2, 0) == 0xBA);
    CHECK(b.byteAt(4, 0) == 0x44);
    CHECK(b.byteAt(6, 0) == 0x55);
    b.clear(0);
    textDraw(b, f, s, 0, 0, "a\t05q\n\t10z");  // TAB: x += 10*d1 + d2
    CHECK(b.byteAt(7, 0) == 0x55);
    CHECK(b.byteAt(10, 12) == 0x55);
    b.clear(0);
    CHECK(textDraw(b, f, s, 0x96, 0, std::string(20, 'm')) == 1);  // clipped at x >= 0x9A
    CHECK(b.byteAt(0x98, 0) == 0x55);
    CHECK(b.byteAt(0x9A, 0) == 0x00);
    CHECK(textWidth(f, "ab\ncd\re") == 10);  // stops at CR, not LF; control bytes count as glyphs
    CHECK(textWidth(f, "A~") == 6);
}

TEST_CASE("DOS text: Hud_DrawNumber suppresses leading zeros but keeps their width") {
    const DosFont f = testFont();
    BackBuffer b;
    drawNumber(b, f, 20, 0, 0xCCCC, 5);
    CHECK(b.byteAt(20, 0) == 0x00);
    CHECK(b.byteAt(24, 0) == 0xCC);
    b.clear(0);
    drawNumber(b, f, 20, 0, 0xCCCC, 105);
    CHECK(b.byteAt(20, 0) == 0xCC);
    CHECK(b.byteAt(22, 0) == 0xCC);
    CHECK(b.byteAt(24, 0) == 0xCC);
    b.clear(0);
    drawNumber(b, f, 20, 0, 0xCCCC, 50);
    CHECK(b.byteAt(20, 0) == 0x00);
    CHECK(b.byteAt(22, 0) == 0xCC);
}

TEST_CASE("DOS UI drawing: frames, colour exchange, buttons") {
    const DosFont f = testFont();
    BackBuffer b;
    frameRect(b, 2, 2, 6, 8, 0xFF);
    CHECK(b.byteAt(2, 5) == 0xFF);
    CHECK(b.byteAt(4, 3) == 0xFF);  // two-row top
    CHECK(b.byteAt(4, 4) == 0x00);
    CHECK(b.byteAt(4, 7) == 0xFF);  // two-row bottom
    b.fillRect(10, 10, 12, 10, 0xFA);
    b.fillRect(13, 10, 13, 10, 0x33);
    exchangeColors(b, 10, 10, 13, 10, 0xFFFF, 0xAAAA);
    CHECK(b.byteAt(10, 10) == 0xAF);
    CHECK(b.byteAt(13, 10) == 0xAA);  // colour 3 ends up as the second colour (three replaces through 3)
    b.clear(0);
    drawButton(b, f, TextStyle{0xFFFF, 12}, "ab", 20, 30, false);
    CHECK(b.byteAt(22, 33) == 0xCC);      // face
    CHECK(b.byteAt(17, 33) == 0xCF);      // rounded left corner column
    CHECK(b.byteAt(20 + 0x14 + 3, 33) == 0xFC);
    b.clear(0);
    drawButton(b, f, TextStyle{0xFFFF, 12}, "ab", 20, 30, true);
    CHECK(b.byteAt(22, 33) == 0xFF);
    const BoxGeometry g = msgBoxGeometry(f, 1, -1, -1, std::string_view("ab"), "abc\rd");
    CHECK(g.width == 0x4B);
    CHECK(g.height == 2 * 12 + 0x0F + 0x0E + 0x0C + 0x13);
    CHECK(g.x1 == (0xA0 - 0x4B) / 2);
    CHECK(g.textY == g.y1 + 0x17);
    CHECK(g.yesX == g.x2 - 0x3A);
}

TEST_CASE("DOS font and boxes: the user's SST.EXE") {
    TM_REQUIRE_DOS_DATA();
    const DosFont f = DosFont::fromExe(*dosExe);
    CHECK(f.count() == 128);
    CHECK(f.height() == 10);
    CHECK(f.unknown0() == 7);
    std::vector<uint8_t> widths;
    for (int c = 0; c < f.count(); ++c) widths.push_back(static_cast<uint8_t>(f.width(c)));
    uint64_t h = mischief::fnv1a64(widths);
    for (int c = 0; c < f.count(); ++c) h = mischief::fnv1a64(f.glyph(c), h);
    CHECK(h == 0x7b7d14cae5d8c149ull);  // as tools/dos/dos_font.py
    CHECK(f.width(' ') == 7);
    // The expansion table 2005:8900 and the sprite mask 2005:8600 in the back-buffer segment.
    const auto expand = dosExe->at(0x2005, 0x8900);
    for (int n = 0; n < 16; ++n) CHECK((expand[2 * n] | (expand[2 * n + 1] << 8)) == DosFont::expandNibble(n));
    const auto mask = dosExe->at(0x2005, 0x8600);
    PakFrame one;
    one.widthBytes = 1;
    one.height = 1;
    bool masksMatch = true;
    for (int v = 0; v < 256; ++v) {
        BackBuffer b;
        b.clear(0xFF);
        one.rows = {static_cast<uint8_t>(v)};
        drawSprite(b, one, 0, 0, sprite_flags::kMasked | sprite_flags::kNoHotspot);
        masksMatch = masksMatch && b.byteAt(0, 0) == ((0xFF & mask[static_cast<size_t>(v)]) | v);
    }
    CHECK(masksMatch);

    // MsgBox_Draw geometry (code_ui.md §4.2, verified on the captures).
    const BoxTexts texts = BoxTexts::fromExe(*dosExe);
    struct Ref {
        int n, x1, y1, x2, y2;
    };
    for (const Ref& r : {Ref{0, 0, 13, 83, 95}, Ref{1, 26, 13, 104, 83}, Ref{7, 42, 58, 117, 142}, Ref{5, 26, 16, 134, 184},
                         Ref{15, 25, 40, 134, 160}}) {
        CAPTURE(r.n);
        BackBuffer b;
        int lines = 0;
        const BoxGeometry g = drawMsgBox(b, f, *dosExe, texts, r.n, std::nullopt, &lines);
        CHECK(g.x1 == r.x1);
        CHECK(g.y1 == r.y1);
        CHECK(g.x2 == r.x2);
        CHECK(g.y2 == r.y2);
        CHECK(lines >= 1);
    }
}
