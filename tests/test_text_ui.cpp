// Tests of the text/UI area: FFNT font, Text_Render layout and drawing, DlgDesc dialogs, the help
// pager and the modal input loops. Expected values come from the independent Python oracles
// (tools/assets/ffnt.py and extracted/scratch/refcap/dlg.py, which reproduce the reference
// captures of the original with 0 differing pixels); see extracted/scratch/text_ui/gen_expect.py.
// Only lengths, coordinates and FNV-1a hashes are stored here, never game text.

#include <doctest/doctest.h>

#include <array>
#include <deque>
#include <set>
#include <string_view>

#include "data/dgroup_offsets.h"
#include "gfx/frame_buffer.h"
#include "platform/engine.h"
#include "test_data.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/dialog_desc.h"
#include "ui/game_dialog.h"
#include "ui/help_topics.h"
#include "util/hash.h"

using namespace tmport;

namespace {

// --- expected values (gen_expect.py) ------------------------------------------------------------
constexpr uint64_t kFontWidthHash = 0x44452e9bb9b17340ull;
constexpr uint64_t kFontPixelHash = 0x58cbb25d2b5f6a0dull;
constexpr uint64_t kKeyCapPixelHash = 0x736243d0b203e387ull;  // glyphs 0x80..0x95

struct TextCase {
    int id, x, y, w, colour;
    size_t length;
    int extentW, extentH;
    uint64_t pixels;  // FNV-1a of the 512x384 screen (EGA numbers, 255 = untouched)
};
const TextCase kTextCases[] = {
    {11500, 83, 92, 321, 0, 157, 290, 136, 0x9d06958e73ff0263ull},
    {11502, 83, 92, 321, 0, 99, 272, 119, 0x2ffef43ce4d79607ull},
    {11503, 83, 92, 321, 0, 128, 298, 102, 0x187accda6a9c7f9dull},
    {11509, 83, 92, 321, 0, 65, 180, 85, 0x7acf6ad9a3eff7b5ull},
    {11530, 83, 92, 321, 0, 123, 318, 119, 0x4474c54c4299b4c0ull},
    {11600, 23, 58, 470, 0, 431, 469, 221, 0x5ea6ef5375975488ull},
    {11601, 23, 58, 470, 0, 337, 469, 170, 0x88b19ebadc326cfbull},
    {11603, 23, 58, 470, 0, 401, 464, 153, 0x4d6d6ffd78df32d0ull},
    {11605, 23, 58, 470, 0, 522, 373, 221, 0x419791b2b3bc6d3aull},
    {11606, 23, 58, 470, 0, 182, 337, 85, 0xb72943b024550b4aull},
    {11607, 23, 58, 470, 0, 397, 416, 153, 0xb1e899e75aeef3fcull},
    {11608, 23, 58, 470, 0, 677, 462, 238, 0x740ab4f73cb08ef7ull},
    {11611, 23, 58, 470, 0, 405, 465, 136, 0xfce97cedba4f1d13ull},
    {11600, 0, 0, 200, 4, 431, 198, 408, 0xc786b90e04e26ef6ull},
    {11605, 5, 5, 300, 0, 522, 293, 306, 0xc2925b511175e96cull},
    {20105, 43, 67, 264, 0, 285, 263, 170, 0x6b3324c4bdc0991eull},
    {20108, 43, 67, 264, 0, 226, 253, 153, 0x19a3718de481b1c2ull},
    {11000, 10, 10, 400, 0, 162, 205, 204, 0x64c957d6e1c05fa7ull},
    {11225, 10, 10, 400, 0, 128, 155, 221, 0x1d22472b45e10fc3ull},
    {11358, 40, 30, 330, 0, 215, 291, 221, 0xf9e1507a708b208full},
};

// Synthetic strings (test inputs written for the engine, not game text), drawn at (7, 9).
struct SynthCase {
    std::string_view text;
    int w;
    int extentW, extentH;
    uint64_t pixels;
};
using namespace std::string_view_literals;
const SynthCase kSynthCases[] = {
    {"Tab\tstop\tand\tmore"sv, 512, 159, 17, 0x19320d9506e6ea4eull},
    {"Line one\rLine two\nLine three\r\rafter blank"sv, 512, 84, 85, 0xf83c57b5366790adull},
    {"\xff\x05\xff" "Centred line\rsecond centred line that is long\xff\x05\xff\rleft"sv, 300, 267, 51,
     0x191640967938d9e6ull},
    {"Under \xff\x02\xff___ blank\xff\x02\xff and \xff\x03\x05\xffred\xff\x03\x01\xff text"sv, 512, 226, 17,
     0x026ff0082c0ca388ull},
    {"overprint      xx\xfe" "ABC"sv, 512, 122, 17, 0xf1d8c944049742b6ull},
    {"Hello \xff\x01\x01\xff, you have \xff\x01\x02\xff coins and more words to wrap here"sv, 160, 143, 68,
     0x58ad307465b2adeeull},
    {"Hello \xff\x01\x01\xff world"sv, 512, 133, 17, 0x0d5f2aa36bbaf292ull},
    {"\xff\x04\x28\x00\x50\x00\xff\tA\tB\tC"sv, 512, 20460, 17, 0x20775e881374e331ull},
    {"\xff\x00X\xff\x07Y\xff\x06" "comment\xffZ"sv, 512, 33, 17, 0x345531cad53d6b55ull},
    {"A long sentence that will need several wraps in a narrow box of text"sv, 100, 95, 119, 0xc9bf84c7d0a132ceull},
    {"Word   with   many   spaces   and\ttabs\t\tto skip at wrap points"sv, 120, 137, 85, 0x1c794ffaf589a575ull},
    {"\x86 to Go Back \x8b \x84\x85 \x80\x81\x82\x83"sv, 512, 307, 17, 0x6074b69672ca4911ull},
    {""sv, 512, -511, 17, 0x757c0fb2f1392325ull},
};
// Substitution tables of cases 5 and 6.
const std::string_view kSubst5[] = {"World Wide"sv, "ten"sv};
const std::string_view kSubst6[] = {"Name"sv};

struct Rect4 {
    int x, y, w, h;
};
struct DialogCase {
    uint16_t off;
    int x, y, w, h, footerY;
    Rect4 b0, b1;
    int defaultButton;
    uint64_t pixels;
};
// Bodies with %s are filled with "Tester" (the player name of the reference captures).
const DialogCase kDialogCases[] = {
    {0x14d6, 86, 70, 340, 243, 284, {337, 254, 64, 21}, {257, 254, 64, 21}, 0, 0x3124e48f22c2bb3bull},
    {0x1533, 103, 108, 305, 167, 246, {319, 216, 64, 21}, {239, 216, 64, 21}, 0, 0x5697306dc9c396c1ull},
    {0x15cc, 137, 70, 238, 243, 284, {286, 254, 64, 21}, {206, 254, 64, 21}, 1, 0x794c5765cf3f7a7dull},
    {0x1625, 148, 118, 215, 148, 237, {274, 207, 64, 21}, {194, 207, 64, 21}, 1, 0xcb5076c7cc79a3f7ull},
    {0x163f, 160, 118, 191, 148, 237, {262, 207, 64, 21}, {182, 207, 64, 21}, 1, 0xa1f6f32c12d73bedull},
    {0x1919, 6, 10, 499, 351, 332, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0xac9365f8c37b23deull},
    {0x2084, 99, 130, 314, 123, 224, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0xf5b7089f708062e6ull},
    {0x2129, 177, 142, 157, 99, 212, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0x7b664598b4ae5607ull},
    {0x21e6, 99, 70, 313, 243, 284, {323, 254, 64, 21}, {243, 254, 64, 21}, 0, 0x1c71c749d2f36e54ull},
    {0x2200, 119, 95, 273, 194, 260, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0x93bd25e36bf8c0f8ull},
    {0x221a, 105, 102, 301, 180, 253, {0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0x332492087c76ed4dull},
    {0x2234, 160, 80, 191, 224, 275, {262, 245, 64, 21}, {182, 245, 64, 21}, 0, 0x922fd42057964782ull},
};

struct PagerCase {
    int firstId, count, page, type, x, w, h;
    uint64_t pixels;
};
const PagerCase kPagerCases[] = {
    {11600, 4, 0, 5, 8, 495, 303, 0xeac235a660399524ull}, {11600, 4, 1, 6, 8, 495, 303, 0xada87ad77aca8a8aull},
    {11600, 4, 2, 6, 8, 495, 303, 0xf8ce518fcea7e9daull}, {11600, 4, 3, 7, 8, 495, 303, 0xb14427b888d5a7a3ull},
    {11605, 2, 0, 5, 8, 495, 303, 0x7781e01cafe44d0bull}, {11605, 2, 1, 7, 8, 495, 303, 0x145c37094f863ab4ull},
    {11607, 1, 0, 4, 8, 495, 303, 0xc814e25d039777a9ull}, {11608, 4, 0, 5, 8, 495, 303, 0xd83453508c465c9dull},
    {11608, 4, 2, 6, 8, 495, 303, 0xa5350161200717c3ull},
};

// --- helpers ------------------------------------------------------------------------------------

const text::Font& gameFont() {
    static const text::Font font = text::Font::load(*testdata::game()->res);
    return font;
}

uint64_t screenHash(const gfx::FrameBuffer& fb) {
    return fnv1a64(std::span<const uint8_t>(fb.pixels(), static_cast<size_t>(512) * 384));
}

// Pens = identity, so the buffer holds EGA numbers like the oracle's canvas; 255 = untouched.
void resetIdentity(gfx::FrameBuffer& fb) {
    std::array<uint8_t, 16> id{};
    for (size_t i = 0; i < 16; ++i) id[i] = static_cast<uint8_t>(i);
    fb.setPens(id);
    fb.clear(255);
    fb.setForeColor(0);
}

std::vector<uint8_t> othrText(int id) {
    auto body = testdata::game()->res->loadRaw(ResType::OTHR, id);
    REQUIRE(body.has_value());
    const auto s = text::untilNul(*body);
    return {s.begin(), s.end()};
}

ui::DlgDesc loadDialog(uint16_t off) {
    ui::DlgDesc d = ui::DlgDesc::fromExe(*testdata::game()->exe, off);
    d.body = ui::formatPercentS(d.body, text::asBytes("Tester"));
    return d;
}

// Scripted input for the modal loops: codes are handed out one per poll; an exhausted script
// answers Esc so a wrong loop cannot hang the test run.
struct Script {
    std::deque<uint16_t> codes;
    int polls = 0;
    std::function<void()> onFirstPoll;
    uint16_t next() {
        if (polls++ == 0 && onFirstPoll) onFirstPoll();
        if (codes.empty()) return 0x1B;
        const uint16_t c = codes.front();
        codes.pop_front();
        return c;
    }
};

void installScript(ui::DialogSystem& ds, Script& s, int* flushes = nullptr, int* delays = nullptr) {
    ds.hooks().getInputCode = [&s] { return s.next(); };
    ds.hooks().flushInput = [flushes] {
        if (flushes) ++*flushes;
    };
    ds.hooks().delayMs = [delays](uint32_t) {
        if (delays) ++*delays;
    };
}

}  // namespace

// ================================================================================== font

TEST_CASE("text: FFNT 999 tables and glyph bitmaps") {
    TM_REQUIRE_GAME_DATA();
    const text::Font& f = gameFont();
    CHECK(f.height() == 17);
    REQUIRE(f.glyphCount() == 256);
    CHECK(f.charWidth(' ') == 5);
    for (uint8_t c = '0'; c <= '9'; ++c) CHECK(f.charWidth(c) == (c == '1' ? 6 : 10));  // '1' is narrow
    CHECK(f.charWidth('_') == 10);
    CHECK(f.charWidth(0x8B) == 46);
    CHECK(f.charWidth(0x91) == 46);
    int ink = 0, maxW = 0;
    std::vector<uint8_t> widths, pixels, keycaps;
    for (int c = 0; c < 256; ++c) {
        const auto b = static_cast<uint8_t>(c);
        if (f.hasInk(b)) ++ink;
        maxW = std::max(maxW, f.charWidth(b));
        widths.push_back(static_cast<uint8_t>(f.charWidth(b)));
        for (int y = 0; y < f.height(); ++y)
            for (int x = 0; x < f.charWidth(b); ++x) {
                pixels.push_back(f.pixel(b, x, y) ? 1 : 0);
                if (c >= 0x80 && c <= 0x95) keycaps.push_back(f.pixel(b, x, y) ? 1 : 0);
            }
    }
    CHECK(ink == 213);
    CHECK(maxW == 46);
    CHECK(fnv1a64(widths) == kFontWidthHash);
    CHECK(fnv1a64(pixels) == kFontPixelHash);
    CHECK(fnv1a64(keycaps) == kKeyCapPixelHash);
}

TEST_CASE("text: FFNT parser rejects truncated data") {
    const uint8_t tooShort[] = {0, 0xFF};
    CHECK_THROWS(text::Font::parse(tooShort));
    std::vector<uint8_t> tables(3 + 3 * 256, 0);
    tables[1] = 0xFF;
    tables[2] = 17;
    tables[3 + 'A'] = 8;  // glyph 'A' needs 17 bytes of bitmap that are not there
    CHECK_THROWS(text::Font::parse(tables));
    tables.resize(tables.size() + 17, 0xFF);
    const text::Font f = text::Font::parse(tables);
    CHECK(f.charWidth('A') == 8);
    CHECK(f.pixel('A', 7, 16));
}

TEST_CASE("text: glyphs are drawn in the current pen, key caps included") {
    TM_REQUIRE_GAME_DATA();
    const text::Font& f = gameFont();
    gfx::FrameBuffer fb;
    std::array<uint8_t, 16> pens{};
    for (size_t i = 0; i < 16; ++i) pens[i] = static_cast<uint8_t>(100 + i);
    fb.setPens(pens);
    for (uint8_t c : {uint8_t{'A'}, uint8_t{0x86}, uint8_t{0x8B}, uint8_t{0x95}}) {
        fb.clear(7);
        fb.setForeColor(5);
        const int adv = f.drawChar(fb, 30, 40, c);
        CHECK(adv == f.charWidth(c));
        int painted = 0, wrong = 0;
        for (int y = 0; y < 60; ++y)
            for (int x = 0; x < 100; ++x) {
                const bool set = f.pixel(c, x - 30, y - 40);
                const uint8_t v = fb.at(x, y);
                if (set) {
                    ++painted;
                    if (v != 105) ++wrong;
                } else if (v != 7) {
                    ++wrong;
                }
            }
        CHECK(painted > 0);
        CHECK(wrong == 0);
    }
    // No clipping in the original; here pixels outside the buffer are skipped safely.
    fb.setForeColor(0);
    CHECK(f.drawChar(fb, 505, 1050, 0x8B) == 46);
    CHECK(f.drawChar(fb, -20, -5, 'W') == f.charWidth('W'));
}

// ================================================================================ layout

TEST_CASE("text: OTHR texts lay out and draw like the reference renderer") {
    TM_REQUIRE_GAME_DATA();
    const text::Font& f = gameFont();
    gfx::FrameBuffer fb;
    for (const auto& tc : kTextCases) {
        CAPTURE(tc.id);
        CAPTURE(tc.w);
        const auto s = othrText(tc.id);
        CHECK(s.size() == tc.length);
        text::TextRenderer measure(f);
        CHECK(measure.measure(s, {}, tc.w) == text::TextExtent{tc.extentW, tc.extentH});
        resetIdentity(fb);
        fb.setForeColor(tc.colour);
        text::TextRenderer tr(f, &fb);
        tr.drawBox(tc.x, tc.y, tc.w, s);
        CHECK(screenHash(fb) == tc.pixels);
    }
}

TEST_CASE("text: line breaks of the help pages") {
    TM_REQUIRE_GAME_DATA();
    // Lines = measured height / 17, including empty lines; the glyph observer sees the same rows.
    const text::Font& f = gameFont();
    text::TextRenderer tr(f);
    for (int id = 11600; id <= 11611; ++id) {
        if (id == 11604) continue;  // exists but no topic shows it
        CAPTURE(id);
        const auto s = othrText(id);
        std::set<int> rows;
        int glyphs = 0;
        tr.setGlyphObserver([&](uint8_t, int, int y, int) {
            rows.insert(y);
            ++glyphs;
        });
        const auto e = tr.measure(s, {}, 470);
        tr.setGlyphObserver({});
        CHECK(e.height % f.height() == 0);
        CHECK(static_cast<int>(rows.size()) <= e.height / f.height());
        CHECK(glyphs > 0);
        CHECK(e.width <= 470);  // help pages are written to fit the 470-px box
        for (int y : rows) CHECK(y % f.height() == 0);
    }
}

TEST_CASE("text: control codes (synthetic strings)") {
    TM_REQUIRE_GAME_DATA();
    const text::Font& f = gameFont();
    gfx::FrameBuffer fb;
    std::vector<text::Bytes> subst5, subst6;
    for (auto s : kSubst5) subst5.push_back(text::asBytes(s));
    for (auto s : kSubst6) subst6.push_back(text::asBytes(s));
    int index = 0;
    for (const auto& sc : kSynthCases) {
        CAPTURE(index);
        const text::SubstTable subst = index == 5 ? text::SubstTable(subst5)
                                       : index == 6 ? text::SubstTable(subst6)
                                                    : text::SubstTable{};
        const text::Bytes s = text::asBytes(sc.text);
        text::TextRenderer m(f);
        CHECK(m.measure(s, subst, sc.w) == text::TextExtent{sc.extentW, sc.extentH});
        resetIdentity(fb);
        text::TextRenderer tr(f, &fb);
        tr.drawBox(7, 9, sc.w, s, subst);
        CHECK(screenHash(fb) == sc.pixels);
        ++index;
    }
}

TEST_CASE("text: Text_Width and side effects of measuring") {
    TM_REQUIRE_GAME_DATA();
    const text::Font& f = gameFont();
    gfx::FrameBuffer fb;
    text::TextRenderer tr(f, &fb);
    CHECK(tr.width(text::asBytes("")) == 0);
    CHECK(tr.width(text::asBytes("Hello")) == 37);
    CHECK(tr.width(text::asBytes("a b")) == 23);
    CHECK(tr.width(text::asBytes("\t")) == -511);  // no glyph placed: 1 - w
    fb.setForeColor(0);
    CHECK(tr.width(text::asBytes("\xff\x03\x05\xffHi")) == 15);
    CHECK(fb.foreColor() == 4);  // FF 03 sets the pen even while only measuring (10b0:02ae)
    // A span stops at its end or at an embedded NUL.
    const uint8_t withNul[] = {'H', 'i', 0, 'X', 'X'};
    CHECK(tr.width(withNul) == tr.width(text::asBytes("Hi")));
    // Wrap test: a run ending exactly at x0 + w - 1 still fits.
    const int wAB = tr.width(text::asBytes("ab cd"));
    CHECK(tr.measure(text::asBytes("ab cd"), {}, wAB + 1).height == 17);
    CHECK(tr.measure(text::asBytes("ab cd"), {}, wAB).height == 34);
}

// ================================================================================ dialogs

TEST_CASE("ui: static DlgDesc decoding") {
    TM_REQUIRE_GAME_DATA();
    REQUIRE(ui::staticDialogs().size() == 12);
    CHECK(ui::findDialog("exit") == dg::kDlgExit);
    CHECK(ui::findDialog("1533") == uint16_t{0x1533});
    CHECK(ui::findDialog("0x221a") == uint16_t{0x221A});
    CHECK_FALSE(ui::findDialog("nonsense").has_value());
    const auto about = ui::DlgDesc::fromExe(*g->exe, dg::kDlgAbout);
    CHECK(about.type == 1);
    CHECK(about.footer == 2);
    CHECK(about.x == 6);
    CHECK(about.y == 10);
    CHECK(about.textWidth == 484);
    CHECK(about.lineCount == 14);
    CHECK(about.title.has_value());
    CHECK(about.body.size() == 703);
    CHECK_FALSE(about.picture);
    const auto full = ui::DlgDesc::fromExe(*g->exe, dg::kDlgListFull);
    CHECK_FALSE(full.title.has_value());
    const auto exitDlg = ui::DlgDesc::fromExe(*g->exe, dg::kDlgExit);
    CHECK(exitDlg.type == 8);
    CHECK(exitDlg.footer == 1);
    const auto help = ui::PagerDesc::fromExe(*g->exe, dg::kHelpDesc);
    CHECK(help.x == 8);
    CHECK(help.y == 22);
    CHECK(help.textWidth == 480);
    CHECK(help.lineCount == 11);
    CHECK(help.footer == 0);
    CHECK(help.pageCount == 0);
    const uint8_t fmt[] = {'a', '%', 's', 'b', '%', 's'};
    const auto filled = ui::formatPercentS(fmt, text::asBytes("XY"));
    CHECK(std::string(filled.begin(), filled.end()) == "aXYb%s");
}

TEST_CASE("ui: dialog geometry of every static DlgDesc") {
    TM_REQUIRE_GAME_DATA();
    Engine engine(*g, gfx::ColorMode::Colour256);
    ui::DialogSystem ds(engine, gameFont());
    for (const auto& dc : kDialogCases) {
        CAPTURE(dc.off);
        const ui::DlgDesc d = loadDialog(dc.off);
        ds.layout(d);
        const auto& geo = ds.geometry();
        CHECK(geo.x == dc.x);
        CHECK(geo.y == dc.y);
        CHECK(geo.w == dc.w);
        CHECK(geo.h == dc.h);
        if (dc.footerY >= 0) CHECK(geo.footerY == dc.footerY);
        if (d.type == ui::dlgtype::kYesNo) {
            const auto& b0 = geo.buttons[0];
            const auto& b1 = geo.buttons[1];
            CHECK(b0.x == dc.b0.x);
            CHECK(b0.y == dc.b0.y);
            CHECK(b0.w == dc.b0.w);
            CHECK(b0.h == dc.b0.h);
            CHECK(b1.x == dc.b1.x);
            CHECK(b1.y == dc.b1.y);
            CHECK(b1.w == dc.b1.w);
            CHECK(b1.h == dc.b1.h);
            CHECK(b0.label == dg::kStrBtnNo);
            CHECK(b1.label == dg::kStrBtnYes);
        }
        CHECK(d.defaultButton == dc.defaultButton);
    }
    // REFERENCE_CAPTURES.md §4.8: the name-entry box is at X = 105, W = 301 (not X = 41).
    ds.layout(loadDialog(dg::kDlgEnterName));
    CHECK(ds.geometry().x == 105);
    CHECK(ds.geometry().w == 301);
}

TEST_CASE("ui: dialog pixels match the reference renderer (WinG frame quirk included)") {
    TM_REQUIRE_GAME_DATA();
    Engine engine(*g, gfx::ColorMode::Colour256);
    ui::DialogSystem ds(engine, gameFont());
    for (const auto& dc : kDialogCases) {
        CAPTURE(dc.off);
        resetIdentity(engine.frame());
        ds.draw(loadDialog(dc.off), false);
        CHECK(screenHash(engine.frame()) == dc.pixels);
        if (dc.b0.w) CHECK(ds.geometry().buttons[static_cast<size_t>(dc.defaultButton)].highlighted);
    }
}

TEST_CASE("ui: help pager pages") {
    TM_REQUIRE_GAME_DATA();
    Engine engine(*g, gfx::ColorMode::Colour256);
    ui::DialogSystem ds(engine, gameFont());
    for (const auto& pc : kPagerCases) {
        CAPTURE(pc.firstId);
        CAPTURE(pc.page);
        int topic = -1;
        for (size_t t = 0; t < ui::kHelpTopics.size(); ++t)
            if (ui::kHelpTopics[t].firstId == pc.firstId) topic = static_cast<int>(t);
        REQUIRE(topic >= 0);
        const ui::PagerDesc p = ui::buildHelpPager(*g->exe, *g->res, topic);
        CHECK(p.pageCount == pc.count);
        const ui::DlgDesc d = ui::DialogSystem::pagerPage(p, pc.page);
        CHECK(d.type == pc.type);
        resetIdentity(engine.frame());
        ds.draw(d, false);
        CHECK(ds.geometry().x == pc.x);
        CHECK(ds.geometry().w == pc.w);
        CHECK(ds.geometry().h == pc.h);
        CHECK(screenHash(engine.frame()) == pc.pixels);
    }
}

// ============================================================================= modal loops

TEST_CASE("ui: Yes/No modal loop") {
    TM_REQUIRE_GAME_DATA();
    Engine engine(*g, gfx::ColorMode::Colour256);
    ui::DialogSystem ds(engine, gameFont());
    auto& fb = engine.frame();
    // A background pattern that the save-under must bring back.
    for (int y = 0; y < 384; ++y)
        for (int x = 0; x < 512; ++x) fb.row(y)[x] = static_cast<uint8_t>((x * 7 + y * 3) & 0xFF);
    const uint64_t before = screenHash(fb);
    const ui::DlgDesc exitDlg = loadDialog(dg::kDlgExit);  // default button 0 = "No"

    struct Expect {
        std::vector<uint16_t> codes;
        int result;
        int flashes;  // Delay_ms calls (4 per flash)
    };
    const Expect cases[] = {
        {{0x0D}, 0, 4},                     // Enter on the default "No"
        {{0x09, 0x0D}, 1, 4},               // Tab to "Yes", Enter
        {{0x127, 0x125, 0x0D}, 0, 4},       // arrows toggle twice
        {{'y'}, 1, 4},                      // Y
        {{0x09, 'n'}, 0, 4},                // N with Yes highlighted
        {{0x1B}, -1, 0},                    // Esc: no flash
        {{0x044E}, -1, 0},                  // click on the footer line
        {{0x044F, 0x244F, 0x0D}, 0, 4},     // clicks outside the buttons are ignored
        {{0x844C, 0x044C}, 1, 4},           // press and release on "Yes"
        {{0x844C, 0x844D, 0x044D}, 0, 4},   // press on Yes, move to No, release on No
        {{0x244D}, 0, 4},                   // right release on No
    };
    for (const auto& c : cases) {
        CAPTURE(c.result);
        Script s;
        s.codes.assign(c.codes.begin(), c.codes.end());
        int flushes = 0, delays = 0;
        installScript(ds, s, &flushes, &delays);
        bool hotOk = false;
        int depth = -1;
        s.onFirstPoll = [&] {
            const auto& hs = engine.hotspots();
            const auto& geo = ds.geometry();
            hotOk = hs.hitTest(geo.buttons[0].x + 2, geo.buttons[0].y + 2) == ui::dlghot::k44D &&
                    hs.hitTest(geo.buttons[1].x + 2, geo.buttons[1].y + 2) == ui::dlghot::k44C &&
                    hs.hitTest(geo.x + 10, geo.footerY + 2) == ui::dlghot::k44E && hs.hitTest(3, 3) == ui::dlghot::k44F;
            depth = ds.musicPauseDepth();
        };
        fb.setDirtyEnabled(true);
        fb.setForeColor(9);
        const int r = ds.runModal(exitDlg);
        CHECK(r == c.result);
        CHECK(delays == c.flashes);
        CHECK(flushes == 2);  // Input_Flush before and after
        CHECK(hotOk);
        CHECK(depth == 1);
        CHECK(ds.musicPauseDepth() == 0);
        CHECK(screenHash(fb) == before);  // save-under restored
        CHECK_FALSE(ds.hasSaveUnder());
        CHECK(fb.dirtyEnabled());
        CHECK(fb.foreColor() == 9);
        CHECK(engine.hotspots().hitTest(3, 3) == 0);
    }
}

TEST_CASE("ui: message box modal loop and hooks") {
    TM_REQUIRE_GAME_DATA();
    Engine engine(*g, gfx::ColorMode::Colour256);
    ui::DialogSystem ds(engine, gameFont());
    const ui::DlgDesc about = loadDialog(dg::kDlgAbout);
    int pauses = 0, resumes = 0, menuOff = 0, menuOn = 0, cursor = 0;
    ds.hooks().soundPauseAll = [&] { ++pauses; };
    ds.hooks().soundResumeAll = [&] { ++resumes; };
    ds.hooks().menuIsEnabled = [] { return true; };
    ds.hooks().menuEnableAll = [&](bool on) { ++(on ? menuOn : menuOff); };
    ds.hooks().cursorForced = [] { return false; };
    ds.hooks().setCursorForced = [&](bool on) { cursor += on ? 1 : 0; };
    const std::pair<std::vector<uint16_t>, int> cases[] = {
        {{0x0D}, 0}, {{0x044E}, 0}, {{0x244E}, 0}, {{0x1B}, -1}, {{0x044F}, -1}, {{0x244F}, -1}, {{'y', 0x09, 0x0D}, 0},
    };
    for (const auto& [codes, result] : cases) {
        Script s;
        s.codes.assign(codes.begin(), codes.end());
        installScript(ds, s);
        CHECK(ds.runModal(about) == result);
    }
    CHECK(pauses == 7);
    CHECK(resumes == 7);
    CHECK(menuOff == 7);
    CHECK(menuOn == 7);
    CHECK(cursor == 7);
    // Nested pausing (10d0): only the outermost resume resumes the sound.
    ds.musicPauseNested();
    ds.musicPauseNested();
    ds.musicResumeNested();
    CHECK(resumes == 7);
    ds.musicResumeNested();
    CHECK(resumes == 8);
    ds.musicResumeNested();  // below zero clamps to 0 and resumes again
    CHECK(ds.musicPauseDepth() == 0);
    CHECK(resumes == 9);
}

TEST_CASE("ui: pager navigation") {
    TM_REQUIRE_GAME_DATA();
    Engine engine(*g, gfx::ColorMode::Colour256);
    ui::DialogSystem ds(engine, gameFont());
    const ui::PagerDesc p = ui::buildHelpPager(*g->exe, *g->res, 0);  // 4 pages
    REQUIRE(p.pageCount == 4);
    struct Expect {
        std::vector<uint16_t> codes;
        int result;
        int lastPage;  // page shown when the pager ended
    };
    const Expect cases[] = {
        {{0x0D, 0x0D, 0x0D, 0x0D}, 1, 3},                    // next, next, next, done
        {{0x122, 0x122, 0x122, 0x0D}, 1, 3},                 // PgDn x3, Enter on "Done"
        {{0x122, 0x121, 0x122, 0x122, 0x122, 0x122}, 0, 3},  // PgUp back; PgDn on the last page does nothing
        {{0x0D, 0x09, 0x0D, 0x0D, 0x1B}, 0, 1},              // Tab on page 1 selects "back"
        {{0x1B}, 0, 0},                                      // Esc
        {{0x044E}, 0, 0},                                    // click outside the box
        {{0x244E, 0x044F, 0x121, 0x0D, 0x1B}, 0, 1},         // ignored codes on page 0, then next, Esc
        {{0x044C, 0x844D, 0x044D, 0x044D, 0x044C, 0x1B}, 0, 2},  // mouse: next, (press), next, next... back
    };
    for (const auto& c : cases) {
        CAPTURE(c.result);
        CAPTURE(c.lastPage);
        Script s;
        s.codes.assign(c.codes.begin(), c.codes.end());
        installScript(ds, s);
        // Track the page from the button label of the drawn page.
        int lastType = -1;
        auto inner = ds.hooks().getInputCode;
        ds.hooks().getInputCode = [&] {
            const auto& geo = ds.geometry();
            lastType = geo.buttons[0].label == dg::kStrBtnPrevPage ? (geo.buttons[1].label == dg::kStrBtnDone ? 7 : 6) : 5;
            return inner();
        };
        CHECK(ds.runPager(p) == c.result);
        const int expectType = c.lastPage == 0 ? 5 : (c.lastPage == 3 ? 7 : 6);
        CHECK(lastType == expectType);
        CHECK(engine.hotspots().hitTest(3, 3) == 0);
        CHECK(ds.musicPauseDepth() == 0);
    }
    // A single-page topic: Enter = "Done".
    const ui::PagerDesc mouse = ui::buildHelpPager(*g->exe, *g->res, 2);
    Script s;
    s.codes = {0x09, 0x0D};
    installScript(ds, s);
    CHECK(ds.runPager(mouse) == 1);
}
