// Tests of the G&G fonts and text renderer (src/gg/text): the fnt loader against digests of
// tools/gg/fnt.py, the Font_* semantics of code_ui.md §3, Text_Render's G&G rules (§4: the raw
// width + 1 measure, FF 03 while measuring, centring, the underline in g_drawColor) and, with
// GG_CAPTURES, the first story page composed as the attract draws it against the DOSBox-X frame.
// No game data or text is stored here: counts, sizes and CRC-32 digests only; the strings the
// synthetic tests set are ordinary letters.

#include <doctest/doctest.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

#include "gg/data/resources.h"
#include "gg/gfx/video.h"
#include "gg/text/font.h"
#include "gg/text/text.h"
#include "gg_test_data.h"
#include "mods/png_codec.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

std::span<const uint8_t> bytesOf(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

uint32_t glyphDigest(const GgFont& f, int* glyphs) {
    std::vector<uint8_t> acc;
    *glyphs = 0;
    for (int c = 0; c < f.records(); ++c) {
        const auto g = f.glyph(static_cast<uint8_t>(c));
        if (g.empty()) continue;
        ++*glyphs;
        const uint32_t crc = png::crc32(g);
        for (int i = 0; i < 4; ++i) acc.push_back(static_cast<uint8_t>(crc >> (8 * i)));
    }
    return png::crc32(acc);
}

}  // namespace

TEST_CASE("text data: the two fonts load as tools/gg/fnt.py reads them") {
    GG_REQUIRE_DATA();
    const Fonts fonts = Fonts::load(ggFiles->archive());
    Fonts f = fonts;
    const GgFont& ui = f.font(0);
    const GgFont& small = f.font(1);
    int glyphs = 0;
    CHECK(ui.records() == 128);
    CHECK(ui.height() == 9);
    CHECK(ui.spacing() == 2);
    CHECK(ui.lineHeight() == 10);
    CHECK(ui.oneBit());
    CHECK(glyphDigest(ui, &glyphs) == 0xd0bedee7u);  // fnt.py: CRC-32 of each glyph record
    CHECK(glyphs == 103);
    CHECK(small.records() == 127);
    CHECK(small.spacing() == 1);
    CHECK(glyphDigest(small, &glyphs) == 0xad1caa3eu);
    CHECK(glyphs == 96);
    // Key caps of font 9: 0x11, 0x17..0x1D, 0x1F, 16..62 pixels wide and 10 rows high.
    for (const uint8_t c : std::initializer_list<uint8_t>{0x11, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1F}) {
        CAPTURE(int{c});
        const auto g = ui.glyph(c);
        REQUIRE(g.size() >= 4);
        CHECK((g[2] | (g[3] << 8)) == 10);
        CHECK(ui.width(c) >= 16);
        CHECK(ui.width(c) <= 62);
    }
    // Font 10 has only two glyphs below 0x20.
    int low = 0;
    for (int c = 0; c < 0x20; ++c) low += !small.glyph(static_cast<uint8_t>(c)).empty();
    CHECK(low == 2);
    CHECK(small.width(0x18) == 40);
    CHECK(small.width(0x1D) == 9);
}

TEST_CASE("text data: Font_CharWidth, Font_DrawChar, Font_Select and Font_SetColor") {
    GG_REQUIRE_DATA();
    Fonts fonts = Fonts::load(ggFiles->archive());
    Screen s;
    CHECK(fonts.index() == 0);
    CHECK(fonts.charWidth(' ') == 3);
    CHECK(fonts.drawChar(s, 0, 0, ' ') == 3);
    // A code without a glyph: the width is the spacing, Font_DrawChar draws nothing and returns 0.
    CHECK(fonts.current().glyph(0x0C).empty());
    CHECK(fonts.charWidth(0x0C) == 2);
    CHECK(fonts.drawChar(s, 0, 0, 0x0C) == 0);
    CHECK(fonts.charWidth(0x80) == 2);  // out of range: glyph-less
    CHECK(fonts.charWidth('A') == fonts.current().width('A') + 2);
    // The colour belongs to the font: selecting the other font switches the glyph colour.
    fonts.setColor(15);
    CHECK(fonts.glyphColor() == 15);
    CHECK(fonts.select(1) == 0);
    fonts.setColor(4);
    CHECK(fonts.glyphColor() == 4);
    fonts.select(0);
    CHECK(fonts.glyphColor() == 15);
    // 0x7F is past the 127 records of font 10: glyph-less there.
    fonts.select(1);
    CHECK(fonts.drawChar(s, 0, 0, 0x7F) == 0);
    // A drawn glyph lands at y + its y offset, in the glyph colour, and marks nothing.
    fonts.setColor(9);
    const int adv = fonts.drawChar(s, 10, 20, 'H');
    CHECK(adv == fonts.current().width('H') + 1);
    int ink = 0;
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 30; ++x) ink += s.pixel(x, y) == 9;
    CHECK(ink > 0);
    CHECK(s.dirty().pending() == 0);
    // Font_DrawString: CR/LF moves down height + 2 = 11 rows and back to the start x.
    Screen t;
    fonts.drawString(t, 0, 0, bytesOf("H\nH"));
    int top = 0, bottom = 0;
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 20; ++x) top += t.pixel(x, y) == 9;
    for (int y = 11; y < 22; ++y)
        for (int x = 0; x < 20; ++x) bottom += t.pixel(x, y) == 9;
    CHECK(top == bottom);
}

TEST_CASE("text data: Text_Render's measure rule, FF 03, centring and the underline") {
    GG_REQUIRE_DATA();
    Fonts fonts = Fonts::load(ggFiles->archive());
    const int wA = fonts.current().width('A'), wB = fonts.current().width('B');
    // "A B": after "A" the pen is at wA + 2; the space wraps when pen + 3 + (wB + 1) > w - 1. The
    // run measures the raw width + 1 (not the advance wB + 2): the line still fits at w = the drawn
    // width, and wraps one pixel narrower.
    const int drawn = wA + 2 + 3 + wB + 2;
    auto one = textMeasure(fonts, bytesOf("A B"), drawn);
    CHECK(one.height == 10);
    CHECK(one.width == drawn);
    auto two = textMeasure(fonts, bytesOf("A B"), drawn - 1);
    CHECK(two.height == 20);
    CHECK(two.width == std::max(wA, wB) + 2);
    CHECK(textWidth(fonts, bytesOf("")) == 0);
    CHECK(textMeasure(fonts, bytesOf("A\r"), 320).height == 20);  // the empty last line counts
    // FF 03 c FF sets the font colour c - 1 also when only measuring (and it is not restored).
    fonts.setColor(1);
    textMeasure(fonts, bytesOf("\xFF\x03\x06\xFF" "A"), 320);
    CHECK(fonts.glyphColor() == 5);
    CHECK(fonts.current().colour() == 5);
    // FF 05 .. FF centres: the start is x + half(w - run), the run measured as above.
    const auto centred = textMeasure(fonts, bytesOf("\xFF\x05\xFF" "AB"), 101);
    const int start = (101 - (wA + 1 + wB + 1)) / 2;
    CHECK(centred.width == start + wA + 2 + wB + 2);
    // The underline is drawn with Gfx_HLine in g_drawColor on the cell's last row.
    Screen s;
    s.setDrawColor(14);
    fonts.setColor(3);
    const auto ext = textDrawBox(s, fonts, 10, 10, 200, bytesOf("\xFF\x02\xFF" "A"));
    CHECK(ext.width == wA + 2);
    for (int x = 10; x < 10 + wA + 2; ++x) CHECK(s.pixel(x, 19) == 14);
    CHECK(s.pixel(10 + wA + 2, 19) == 0);
}

TEST_CASE("text captures: story page 1 is pixel exact") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto cap = testdata::ggCapture("w0/02_story1.png");
    if (!cap) return;
    const Archive& arc = ggFiles->archive();
    Video v;
    v.init(ggFiles->exe(), arc);
    v.cursor().hide();  // State_01_Attract
    Fonts fonts = Fonts::load(arc);
    Screen& s = v.screen();
    // 2c11:0630 Attract_StoryPage, first page
    std::vector<uint8_t> pic;
    v.loadPicture(arc, 0x1009, 0x100A, pic);
    s.lpxDraw(0, 0, pic);
    s.saveAll();
    const auto lineRes = arc.read(0x1024);
    const auto line = textBytes(lineRes);
    fonts.setColor(15);
    fonts.select(0);
    fonts.current().setSpacing(1);
    fonts.drawString(s, 160 - textWidth(fonts, line) / 2, 190, line);
    fonts.current().setSpacing(2);
    const auto pageRes = arc.read(0x1019);
    s.setDrawColor(0xA3);
    s.fillRect(57, 38, 177, 53);
    fonts.select(1);
    fonts.setColor(0);
    textDrawBox(s, fonts, 57, 38, 177, textBytes(pageRes));
    // Screen_FadeOutIn: already faded, Dirty_Flush, Pal_FadeIn.
    v.flush();
    v.dac().set(v.palette().shadow());
    const auto frame = v.frameRgba();
    const auto bytes = readWholeFile(*cap);
    REQUIRE(bytes);
    const auto img = mods::decodePng(*bytes);
    REQUIRE(img.width == 640);
    int differ = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const size_t o = (static_cast<size_t>(y * 2) * 640 + static_cast<size_t>(x * 2)) * 4;
            const uint32_t c = uint32_t{img.rgba[o]} | uint32_t{img.rgba[o + 1]} << 8 | uint32_t{img.rgba[o + 2]} << 16 | 0xFF000000u;
            differ += frame[static_cast<size_t>(y * 320 + x)] != c;
        }
    CHECK(differ == 0);
}
