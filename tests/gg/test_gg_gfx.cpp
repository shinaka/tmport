// Tests of the G&G graphics area (src/gg/gfx): the screen buffer and its primitives, the LPX
// blitters, dirty tiles, sprites, the software cursor, the palette and fades, the wipe and the movie.
// Synthetic pictures test the exact semantics of code_runtime.md §10-§15; with GG_DATA the decoders
// are checked against digests of the reference tools (tools/gg/lpx.py, movie.py) and the shipped
// tables; with GG_CAPTURES composed screens are compared pixel by pixel with the DOSBox-X captures.
// No game data is stored here: counts, sizes, coordinates and CRC-32 digests only.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

#include "gg/data/dgroup_offsets.h"
#include "gg/data/resources.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/gfx_cli.h"
#include "gg/gfx/lpx.h"
#include "gg/gfx/movie.h"
#include "gg/gfx/palette.h"
#include "gg/gfx/screen.h"
#include "gg/gfx/sprites.h"
#include "gg/gfx/transitions.h"
#include "gg/gfx/video.h"
#include "gg_test_data.h"
#include "mods/png_codec.h"
#include "rtl/borland_rand.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

void put16(std::vector<uint8_t>& v, int x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

// A synthetic LPX picture: header, data offset 2, then `data` (raw pixels or the RLE stream).
std::vector<uint8_t> lpx(int format, int w, int h, int key, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> p = {'L', 'P', 'X', 0x1A};
    put16(p, format);
    put16(p, w);
    put16(p, h);
    put16(p, key);
    put16(p, 2);
    p.insert(p.end(), data.begin(), data.end());
    return p;
}

void fillScreen(Screen& s, uint8_t v) { std::fill(s.bytes().begin(), s.bytes().end(), v); }

int countTiles(const DirtyMap& m) { return m.dirtyTiles(); }

// Counts restoreUnder calls (a stand-in for the cursor).
struct CountingUnder final : MouseUnder {
    int calls = 0;
    void restoreUnder() override { ++calls; }
};

// CRC-32 of the little-endian CRC-32s of a list (the digests computed from the tools' listings).
struct CrcDigest {
    std::vector<uint8_t> acc;
    void add(uint32_t c) {
        for (int i = 0; i < 4; ++i) acc.push_back(static_cast<uint8_t>(c >> (8 * i)));
    }
    uint32_t value() const { return png::crc32(acc); }
};

// Pixels of a 320x200 PNG (a capture of 640x400 is halved: every DOSBox-X pixel is a 2x2 block),
// packed as R | G << 8 | B << 16 | 0xFF << 24.
std::vector<uint32_t> loadFrame(const std::filesystem::path& png) {
    const auto bytes = readWholeFile(png);
    REQUIRE(bytes);
    const auto img = mods::decodePng(*bytes);
    const int step = img.width == 640 ? 2 : 1;
    std::vector<uint32_t> px;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            const size_t o = (static_cast<size_t>(y * step) * static_cast<size_t>(img.width) + static_cast<size_t>(x * step)) * 4;
            px.push_back(uint32_t{img.rgba[o]} | uint32_t{img.rgba[o + 1]} << 8 | uint32_t{img.rgba[o + 2]} << 16 | 0xFF000000u);
        }
    return px;
}

int differingPixels(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    int n = 0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) n += a[i] != b[i];
    return n + static_cast<int>(std::max(a.size(), b.size()) - std::min(a.size(), b.size()));
}

}  // namespace

TEST_CASE("gfx: Dirty_Mark tiles, wrap, the y test and the pending count") {
    DirtyMap m;
    m.mark(3, 5, 10, 2);  // cols ceil((3 + 10) / 8) = 2, rows ceil((5 + 2) / 8) = 1
    CHECK(countTiles(m) == 2);
    CHECK(m.tile(0, 0));
    CHECK(m.tile(1, 0));
    CHECK(m.pending() == 1);
    m.mark(0, 200, 8, 8);  // y >= 200: nothing, not counted
    m.mark(0, -1, 8, 8);   // a negative y is >= 200 unsigned
    CHECK(m.pending() == 1);
    m.mark(-1, 0, 8, 8);  // x shifted unsigned: outside the map, counted
    CHECK(countTiles(m) == 2);
    CHECK(m.pending() == 2);
    m.clear();
    m.mark(312, 0, 16, 8);  // column 39 and the wrap into row 1 column 0
    CHECK(m.tile(39, 0));
    CHECK(m.tile(0, 1));
    CHECK(countTiles(m) == 2);
    m.clear();
    m.mark(0, 0, 1000, 8);  // widths above 320 count as 320
    CHECK(countTiles(m) == 40);
    m.clear();
    m.mark(0, 196, 8, 8);  // rows 24 and 25: row 25 is past the map
    CHECK(countTiles(m) == 1);
    CHECK(m.tile(0, 24));
    CHECK(m.pending() == 1);
}

TEST_CASE("gfx: line primitives keep Gfx_ClipX / Gfx_ClipY") {
    Screen s;
    s.setDrawColor(9);
    s.hLine(310, 0, 10);  // ClipX shortens to 9 for the test only: all 10 pixels are drawn
    for (int x = 310; x < 320; ++x) CHECK(s.pixel(x, 0) == 9);
    CHECK(s.dirty().tile(38, 0));
    CHECK(s.dirty().tile(39, 0));
    s.dirty().clear();
    s.hLine(320, 1, 5);  // x > right: rejected, nothing marked
    CHECK(s.pixel(0, 2) == 0);
    CHECK(s.dirty().pending() == 0);
    s.vLine(5, 195, 10);  // bottom read as exclusive: rows 195..198
    CHECK(s.pixel(5, 198) == 9);
    CHECK(s.pixel(5, 199) == 0);
    s.vLine(7, -3, 10);  // y < top: the height grows by 3, rows 0..12
    CHECK(s.pixel(7, 12) == 9);
    CHECK(s.pixel(7, 13) == 0);
    SavedImage img;
    s.getImage(310, 0, 10, 1, img);  // the image clip is exclusive too
    CHECK(img.width == 9);
    CHECK(img.height == 1);
    SavedImage untouched{3, 4, {}};
    s.getImage(320, 0, 4, 4, untouched);  // rejected: the buffer stays as it was
    CHECK(untouched.width == 3);
}

TEST_CASE("gfx: rectangles, pixels, frames, save and restore") {
    Screen s;
    s.setDrawColor(4);
    s.fillRect(10, 10, 3, 2);
    CHECK(s.pixel(12, 11) == 4);
    CHECK(s.pixel(13, 11) == 0);
    s.xorRect(10, 10, 1, 1);
    CHECK(s.pixel(10, 10) == (4 ^ 0x0F));
    s.putPixel(100, 100, 77);
    CHECK(s.pixel(100, 100) == 77);
    s.saveToBack(0, 0, 320, 200);
    CHECK(s.pixel(100, 300) == 77);
    s.fillRect(0, 0, 320, 200);
    s.restoreRect(-3, 100, 105, 1);  // x < 0: w += x, x = 0 -> columns 0..101
    CHECK(s.pixel(100, 100) == 77);
    CHECK(s.pixel(102, 100) == 4);
    s.setDrawColor(1);
    s.frameRect(50, 50, 4, 3);
    CHECK(s.pixel(50, 50) == 1);
    CHECK(s.pixel(53, 52) == 1);
    CHECK(s.pixel(51, 51) == 4);
    SavedImage img;
    s.getImage(49, 49, 6, 5, img);
    s.fillRect(40, 40, 30, 30);
    s.putImage(49, 49, img);
    CHECK(s.pixel(50, 50) == 1);
    CHECK(s.pixel(51, 51) == 4);
    s.clearPlayArea();  // colour 2, rows 9..163
    CHECK(s.pixel(0, 9) == 2);
    CHECK(s.pixel(0, 163) == 2);
    CHECK(s.pixel(0, 164) != 2);
    CHECK(Screen::imageSize(280, 163) == static_cast<int16_t>(280 * 163 + 4));
}

TEST_CASE("gfx: Gfx_DrawGlyph is LSB-first and marks nothing") {
    Screen s;
    // 1 full byte + 3 bits per row, 2 rows: row 0 = 0b00000101 + 0b011, row 1 = 0x80 + 0b100
    const std::vector<uint8_t> g = {1, 3, 2, 0, 0x05, 0x03, 0x80, 0x04};
    s.drawGlyph(20, 30, g, 7);
    CHECK(s.pixel(20, 30) == 7);
    CHECK(s.pixel(21, 30) == 0);
    CHECK(s.pixel(22, 30) == 7);
    CHECK(s.pixel(28, 30) == 7);
    CHECK(s.pixel(29, 30) == 7);
    CHECK(s.pixel(30, 30) == 0);
    CHECK(s.pixel(27, 31) == 7);
    CHECK(s.pixel(30, 31) == 7);
    CHECK(s.dirty().pending() == 0);
}

TEST_CASE("gfx: the keyed raw blit clips to the inclusive rectangle") {
    Screen s;
    fillScreen(s, 0xEE);
    // 4x3, key 7: row 0 = 1 7 2 3, row 1 = 4 5 6 7, row 2 = 8 9 10 11
    const auto pic = lpx(0, 4, 3, 0x0107, {1, 7, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    s.lpxDrawKeyed(10, 20, pic, 0);
    CHECK(s.pixel(10, 20) == 1);
    CHECK(s.pixel(11, 20) == 0xEE);  // the key's low byte is skipped
    CHECK(s.pixel(13, 22) == 11);
    CHECK(s.lastBlit() == BlitRect{10, 20, 4, 3});
    s.lpxDrawKeyed(-2, 40, pic, 0);  // negative x clipped
    CHECK(s.pixel(0, 40) == 2);
    CHECK(s.pixel(1, 40) == 3);
    CHECK(s.lastBlit() == BlitRect{0, 40, 2, 3});
    s.dirty().clear();
    s.lpxDrawKeyed(30, -1, pic, 0);  // y compared unsigned: rejected, no mark
    CHECK(s.lastBlit().x == -1);
    CHECK(s.dirty().pending() == 0);
    CHECK(s.pixel(30, 0) == 0xEE);
    s.lpxDrawKeyed(318, 198, pic, 0);  // right and bottom inclusive
    CHECK(s.pixel(318, 198) == 1);
    CHECK(s.pixel(319, 199) == 5);
    CHECK(s.pixel(0, 199) == 0xEE);  // no wrap
    CHECK(s.lastBlit() == BlitRect{318, 198, 2, 2});
    s.setClip({93, 226, 0, 199});  // the workshop's narrowed clip
    s.lpxDrawKeyed(91, 60, pic, 0);
    CHECK(s.pixel(92, 60) == 0xEE);
    CHECK(s.pixel(93, 60) == 2);
}

TEST_CASE("gfx: the RLE blits (skips, rows, mirror, wrap, the literal carry)") {
    Screen s;
    fillScreen(s, 0xEE);
    // 5x2: row 0 = skip 2, literal 1 2 3; row 1 = literal 4 5 6 7 8
    const auto pic = lpx(1, 5, 2, 1, {0, 2, 3, 1, 2, 3, 5, 4, 5, 6, 7, 8});
    s.lpxDraw(10, 10, pic);
    CHECK(s.pixel(10, 10) == 0xEE);
    CHECK(s.pixel(11, 10) == 0xEE);
    CHECK(s.pixel(12, 10) == 1);
    CHECK(s.pixel(14, 10) == 3);
    CHECK(s.pixel(14, 11) == 8);
    s.lpxDrawKeyed(30, 10, pic, 2);  // mode 2 mirrors an RLE picture
    CHECK(s.pixel(34, 10) == 0xEE);
    CHECK(s.pixel(32, 10) == 1);
    CHECK(s.pixel(30, 10) == 3);
    CHECK(s.pixel(30, 11) == 8);
    CHECK(s.pixel(34, 11) == 4);
    s.lpxDrawKeyed(318, 20, pic, 0);  // no clipping: the row wraps into the next one
    CHECK(s.pixel(318, 20) == 0xEE);
    CHECK(s.pixel(0, 21) == 1);
    CHECK(s.pixel(2, 21) == 3);
    s.dirty().clear();
    s.lpxDrawKeyed(-3, 30, pic, 0);  // drawn before the row start, not marked
    CHECK(s.dirty().pending() == 0);
    CHECK(s.pixel(319, 29) == 1);
    CHECK(s.pixel(1, 30) == 3);
    // A raw picture with mode 2 is not mirrored.
    const auto raw = lpx(0, 2, 1, 0x0100, {5, 6});
    s.lpxDrawKeyed(50, 50, raw, 2);
    CHECK(s.pixel(50, 50) == 5);
    // The literal carry: width 3, a literal of 4 sets the borrow that RCR shifts into the count, so
    // 4 + 256 bytes are copied; the remainder (3 - 4 = 255) is then used up by a skip of 255.
    std::vector<uint8_t> data = {4};
    for (int i = 0; i < 260; ++i) data.push_back(static_cast<uint8_t>(i + 1));
    data.push_back(0);
    data.push_back(255);
    fillScreen(s, 0xEE);
    s.lpxDraw(0, 100, lpx(1, 3, 1, 1, data));
    CHECK(s.pixel(0, 100) == 1);
    CHECK(s.pixel(3, 100) == 4);
    CHECK(s.pixel(259, 100) == static_cast<uint8_t>(260));
    CHECK(s.pixel(260, 100) == 0xEE);
}

TEST_CASE("gfx: Gfx_RemapPicture and Gfx_RecolorPicture change raw pictures in place") {
    auto raw = lpx(0, 3, 1, 1, {1, 2, 3});
    auto rle = lpx(1, 3, 1, 1, {3, 1, 2, 3});
    const std::vector<uint8_t> from = {1, 3, 1}, to = {7, 8, 9, 0xFF, 5};
    recolorPicture(raw, from, to);  // a later pair wins (1 -> 9); the lists end at 0xFF
    CHECK(raw[14] == 9);
    CHECK(raw[15] == 2);
    CHECK(raw[16] == 8);
    const auto before = rle;
    recolorPicture(rle, from, to);  // RLE pictures are left alone
    CHECK(rle == before);
    const DecodedLpx d = decodeLpx(raw);
    CHECK(d.pixels == std::vector<uint8_t>{9, 2, 8});
    CHECK(keyedMask(d) == std::vector<uint8_t>{1, 1, 1});
    CHECK_THROWS(decodeLpx(lpx(2, 1, 1, 1, {0})));
    CHECK_THROWS(decodeLpx(lpx(0, 4, 4, 1, {0})));
}

TEST_CASE("gfx: the blitter table (format + mode) & 3 stays inside the payload") {
    Screen s;
    const auto raw = lpx(0, 4, 4, 1, std::vector<uint8_t>(16, 3));
    const auto rle = lpx(1, 4, 1, 1, {4, 9, 9, 9, 9});
    for (int mode = 0; mode < 4; ++mode) {
        s.lpxDrawKeyed(10, 10, raw, mode);  // modes 1 and 3: the RLE blitters read raw data
        s.lpxDrawKeyed(10, 50, rle, mode);  // modes 1 and 3: the raw blit reads the RLE stream
        if (mode == 2) CHECK(s.pixel(10, 50) == 9);  // RLE + 2: mirrored
    }
    CHECK(s.pixel(10, 50) == 4);  // RLE + 3 = raw: the stream's first byte (the literal count) as a pixel
    s.lpxDraw(0, 0, std::vector<uint8_t>{'L', 'P', 'X'});  // shorter than a header: nothing
}

TEST_CASE("gfx: Gfx_HideMouseOnce restores the cursor once per frame") {
    Screen s;
    CountingUnder under;
    s.setMouseUnder(&under);
    s.fillRect(0, 0, 8, 8);
    CHECK(under.calls == 1);
    CHECK(s.mouseHideState() == 2);
    s.fillRect(0, 0, 8, 8);
    CHECK(under.calls == 1);
    s.resetMouseHide();
    s.putPixel(1, 1, 1);
    CHECK(under.calls == 2);
    s.setMouseUnder(nullptr);
}

TEST_CASE("gfx: the sprite list (erase stops at a do-not-erase record, draw skips bit 4)") {
    Screen s;
    s.setDrawColor(5);
    s.fillRect(0, 0, 320, 200);
    s.saveAll();
    s.setDrawColor(9);
    s.fillRect(0, 0, 320, 200);
    const auto pic = lpx(0, 2, 2, 0x0100, {7, 7, 7, 7});
    SpriteList list;
    list.add({0, 10, 10, 2, 2, pic});
    list.add({SpriteList::kNoErase, 20, 10, 2, 2, pic});
    list.add({0, 30, 10, 2, 2, pic});
    list.add({SpriteList::kNoDraw, 40, 10, 2, 2, pic});
    list.drawAll(s);
    CHECK(s.pixel(10, 10) == 7);
    CHECK(s.pixel(40, 10) == 9);
    list.eraseAll(s);
    CHECK(list.count() == 0);
    CHECK(s.pixel(10, 10) == 5);
    CHECK(s.pixel(20, 10) == 7);  // the bit-7 record ...
    CHECK(s.pixel(30, 10) == 7);  // ... and every record after it stay
    for (int i = 0; i < 60; ++i) list.add({0, 0, 0, 1, 1, pic});
    CHECK(list.count() == SpriteList::kCapacity);
}

TEST_CASE("gfx: Dirty_Flush, the cursor and Mouse_SetBounds") {
    Video v;
    Screen& s = v.screen();
    // A 3x2 RLE arrow with key 1 (as resource 0 is), and a 2x2 raw "hand".
    const auto arrowPic = lpx(1, 3, 2, 1, {3, 15, 15, 15, 0, 1, 2, 15, 15});
    int rangeX[2] = {-1, -1}, rangeY[2] = {-1, -1};
    v.cursor().setDriverHooks({[&](int a, int b) { rangeX[0] = a, rangeX[1] = b; },
                               [&](int a, int b) { rangeY[0] = a, rangeY[1] = b; }});
    CursorRegion full{0, 0, 0, 320, 200, 0, nullptr};
    v.cursor().init(CursorRecord::fromResource(0, arrowPic), 200, 50, full, full);
    CHECK(v.cursor().x() == 100);
    CHECK(v.cursor().y() == 50);
    s.init();
    v.flush();
    // The first flush copies every tile (Gfx_FillRect marked them) and draws the cursor.
    CHECK(s.dirty().tilesCopied == 1000);
    CHECK(s.mouseHideState() == 0);
    CHECK(s.dirty().pending() == 0);
    CHECK(v.cursor().shown());
    CHECK(v.vram()[50 * 320 + 100] == 15);
    CHECK(v.vram()[51 * 320 + 100] == 0);  // skipped by the RLE row: the background
    CHECK(rangeX[0] == 0);
    CHECK(rangeX[1] == 2 * (319 - 3));
    CHECK(rangeY[1] == 199 - 2);
    // Nothing pending: nothing at all happens.
    const auto before = s.dirty().flushes;
    v.flush();
    CHECK(s.dirty().flushes == before);
    // A primitive removes the cursor from the screen rows; the next flush draws it again.
    s.setDrawColor(3);
    s.fillRect(0, 0, 4, 4);
    CHECK(!v.cursor().shown());
    CHECK(s.pixel(100, 50) == 0);
    v.flush();
    CHECK(s.pixel(100, 50) == 15);
    // A cursor region with another record switches the active record.
    auto hand = CursorRecord::fromPicture(arrowPic);
    v.cursor().setHotSpot(hand.get(), 2, 1);
    CursorRegion region{0, 90, 40, 20, 20, 1, hand.get()};
    v.cursor().addRegion(&region);
    v.moveCursor(95, 45);  // Mouse_MoveCursor: restore, move, flush
    CHECK(v.cursor().activeRecord() == hand.get());
    CHECK(v.cursor().hotX() == 2);
    CHECK(v.cursor().hotY() == 1);
    CHECK(s.pixel(100, 50) == 0);  // the old place restored
    CHECK(s.pixel(95, 45) == 15);
    v.cursor().clearRegions();
    CHECK(v.cursor().regionCount() == 1);
    v.cursor().forceDefault(1);
    s.putPixel(0, 0, 1);
    v.flush();
    CHECK(v.cursor().activeRecord() == v.cursor().arrow());
    v.cursor().hide();
    s.putPixel(0, 0, 1);
    v.flush();
    CHECK(!v.cursor().shown());
    CHECK(s.pixel(95, 45) == 0);
    v.cursor().setBounds(0, 9, 319, 154);  // clamps the pointer to maxY = 9 + 154 - 2
    CHECK(v.cursor().maxY() == 161);
}

TEST_CASE("gfx: the DAC, palette loads and the 6-bit expansion") {
    CHECK(dacTo8(0) == 0);
    CHECK(dacTo8(11) == 44);  // (v * 255 + 31) / 63 would give 45
    CHECK(dacTo8(16) == 65);
    CHECK(dacTo8(49) == 199);  // and 198
    CHECK(dacTo8(63) == 255);
    Rgb6 pal{};
    for (size_t i = 0; i < pal.size(); ++i) pal[i] = static_cast<uint8_t>(i % 64);
    Dac dac;
    PaletteLoad load(pal, 0, 2);
    CHECK(load.remainingRetraces() == 2);
    load.onRetrace(dac);  // colours 0..127
    CHECK(dac.registers()[383] == pal[383]);
    CHECK(dac.registers()[384] == 0);
    load.onRetrace(dac);
    CHECK(!load.pending());
    CHECK(dac.registers() == pal);
    CHECK(dac.rgba()[1] == (0xFF000000u | uint32_t{dacTo8(3)} | uint32_t{dacTo8(4)} << 8 | uint32_t{dacTo8(5)} << 16));
}

TEST_CASE("gfx: Pal_Fade loads (12 out, 9 in) and the visible frames (22, 16)") {
    Rgb6 target{};
    for (size_t i = 0; i < target.size(); ++i) target[i] = static_cast<uint8_t>((i * 7) % 64);
    Palette p(target);
    // Fade out from the shown palette.
    Dac dac;
    dac.set(target);
    PalFade out = p.fadeOut();
    CHECK(p.faded());
    CHECK(out.loads() == 12);
    auto frames = fadeRetraceFrames(dac, out);
    REQUIRE(frames.size() == 24);
    auto changing = [](const Rgb6& start, const std::vector<Rgb6>& f) {
        int n = 0;
        Rgb6 prev = start;
        for (const auto& x : f) {
            n += x != prev;
            prev = x;
        }
        return n;
    };
    CHECK(changing(target, frames) == 22);
    CHECK(frames.back() == Rgb6{});
    CHECK(p.fadeOut().loads() == 0);  // already faded: nothing
    // Fade in.
    dac.set(Rgb6{});
    PalFade in = p.fadeIn();
    CHECK(!p.faded());
    CHECK(in.loads() == 9);
    frames = fadeRetraceFrames(dac, in);
    REQUIRE(frames.size() == 18);
    CHECK(changing(Rgb6{}, frames) == 16);
    CHECK(frames.back() == target);
    // Pal_SetShadowRange loads the DAC only when not faded; Pal_LoadRes never.
    const std::vector<uint8_t> rgb = {1, 2, 3};
    CHECK(p.setShadowRange(rgb, 3, 3).pending());
    CHECK(p.shadow()[3] == 1);
    p.blackout();
    CHECK(!p.setShadowRange(rgb, 6, 3).pending());
    p.save();
    p.shadow()[0] = 9;
    p.restore();
    CHECK(p.shadow()[0] == target[0]);
}

TEST_CASE("gfx: the wipe (iterations, spans straight to VRAM, rand() calls, column 319)") {
    Screen s;
    fillScreen(s, 4);
    WipePattern w;
    w.period = 2;
    w.leftEnabled = 1;
    w.leftStep = 10;
    for (int r = 0; r < WipePattern::kRows; ++r) {
        w.left[static_cast<size_t>(r)] = 0;
        w.right[static_cast<size_t>(r)] = 320;
    }
    BorlandRand rng, ref;
    rng.setRawSeed(0x1234);
    ref.setRawSeed(0x1234);
    Vram vram{};
    Wipe wipe(s, w, rng);
    ref.rand();
    CHECK(rng.seed() == ref.seed());  // one rand() for the sound
    CHECK(wipe.soundEffect() >= kWipeSoundBase);
    CHECK(s.mouseHideState() != 0);
    while (!wipe.finished()) wipe.step(s, vram);
    CHECK(wipe.steps() == 33);  // 0x11D6: left +10, 33 iterations
    CHECK(vram[9 * 320] == 4);
    CHECK(vram[164 * 320 + 319] == 4);
    CHECK(vram[8 * 320] == 0);    // row 8 untouched
    CHECK(vram[165 * 320] == 0);  // row 165 untouched
    CHECK(s.dirty().pending() == 0);
    wipe.end(s);
    CHECK(s.mouseHideState() == 0);
    // 0x11DA: the right frontier is 319 and the right sweep is off: column 319 is never copied.
    for (auto& r : w.right) r = 319;
    Vram vram2{};
    Wipe bug(s, w, rng);
    while (!bug.finished()) bug.step(s, vram2);
    CHECK(vram2[9 * 320 + 318] == 4);
    CHECK(vram2[9 * 320 + 319] == 0);
    CHECK(randomWipePattern(rng) >= kWipePatternFirst);
}

TEST_CASE("gfx: Movie_DecodeFrame codes") {
    Vram v{};
    std::fill(v.begin(), v.end(), uint8_t{0xEE});
    std::vector<uint8_t> f = {
        2, 10, 11,       // literal 2
        0, 3, 5,         // run of 3 x 5
        0, 2, 1,         // run of 1 = skip 2
        0x83,            // skip 3
        0x80, 4, 0,      // skip 4
        0x80, 2, 0x80, 20, 21,  // long literal 2
        0x80, 2, 0xC0, 7,       // long run 2 x 7
        0x80, 3, 0xC0, 1,       // long run of 1 = skip 3
        1, 99,
        0x80, 0, 0};     // end
    const auto r = movieDecodeFrame(f, v);
    CHECK(r.complete);
    CHECK(r.used == f.size());
    const std::vector<uint8_t> expect = {10, 11, 5, 5, 5, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
                                         20, 21, 7, 7, 0xEE, 0xEE, 0xEE, 99, 0xEE};
    CHECK(std::equal(expect.begin(), expect.end(), v.begin()));
    CHECK(r.end == 22);
    const std::vector<uint8_t> cut = {5, 1, 2};
    CHECK(!movieDecodeFrame(cut, v).complete);
}

// --- with the user's files ------------------------------------------------------------------------------

namespace {

// Every LPX picture of the archives, in tools/gg/ggarc.py walk_leaves order.
template <class F>
void forEachPicture(const Archive& arc, F&& f) {
    for (const uint16_t id : arc.ids()) {
        const auto r = arc.load(id);
        if (r.resType() == ResType::Bundle) {
            for (const auto& m : parseBundle(r.bytes))
                if (classify(m.type, m.payload, id, m.index) == "picture") f(m.payload);
        } else if (classify(r.type, r.bytes, id) == "picture") {
            f(std::span<const uint8_t>(r.bytes));
        }
    }
}

}  // namespace

TEST_CASE("gfx data: every LPX picture decodes as tools/gg/lpx.py and blits as it decodes") {
    GG_REQUIRE_DATA();
    int pictures = 0, raw = 0, rle = 0, blitMismatches = 0;
    CrcDigest digest;
    Screen s;
    forEachPicture(ggFiles->archive(), [&](std::span<const uint8_t> p) {
        const DecodedLpx d = decodeLpx(p);
        ++pictures;
        (d.format == 0 ? raw : rle)++;
        CHECK(d.used == p.size());
        digest.add(png::crc32(d.pixels));
        digest.add(png::crc32(d.opaque));
        // The keyed blit at (0, 0) over a sentinel shows exactly the decoded pixels where the
        // keyed mask is set (every shipped picture fits the screen).
        fillScreen(s, 0xAB);
        s.lpxDrawKeyed(0, 0, p, 0);
        const auto mask = keyedMask(d);
        bool ok = true;
        for (int y = 0; y < d.height && ok; ++y)
            for (int x = 0; x < d.width && ok; ++x) {
                const size_t i = static_cast<size_t>(y) * static_cast<size_t>(d.width) + static_cast<size_t>(x);
                ok = s.pixel(x, y) == (mask[i] ? d.pixels[i] : 0xAB);
            }
        blitMismatches += !ok;
    });
    CHECK(pictures == 2959);
    CHECK(raw == 904);
    CHECK(rle == 2055);
    CHECK(blitMismatches == 0);
    // Digest of (CRC-32 of the pixels, CRC-32 of the opacity mask) per picture, from lpx.py.
    CHECK(digest.value() == 0x7bddd437u);
}

TEST_CASE("gfx data: the movie decodes as tools/gg/movie.py; the cue table") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    Vram vram{};
    const DecodedLpx bg = decodeLpx(arc.read(0x1076));
    std::copy(bg.pixels.begin(), bg.pixels.end(), vram.begin());
    const auto cues = ggFiles->exe().dgroup().subspan(dg::kMovieCues);
    MoviePlayer player(arc, kMovieFirst, kMovieEnd, kMoviePeriod, cues, kMovieEffectBank);
    CrcDigest digest;
    std::vector<MovieCue> ran;
    int incomplete = 0;
    while (!player.finished()) {
        if (const auto cue = player.step(vram)) ran.push_back(*cue);
        incomplete += !player.lastDecode().complete;
        digest.add(png::crc32(vram));
    }
    CHECK(player.counter() == 202);
    CHECK(incomplete == 0);
    CHECK(png::crc32(vram) == 0x420dba12u);  // the last frame, as movie.py
    CHECK(digest.value() == 0xc3775bc1u);     // every frame
    // 25 cues run (the table has no count; record 25 {6, 0, 0, 0} never matches again): the pauses
    // at counters 15, 17, 19 and 116, a stop at 135, sound starts up to counter 176.
    REQUIRE(ran.size() == 25);
    CHECK(std::count_if(ran.begin(), ran.end(), [](const MovieCue& c) { return c.type == 0 && c.a == 1460; }) == 4);
    CHECK(ran[0].frame == 15);
    CHECK(ran[15].frame == 116);
    CHECK(ran[18].type == 2);
    CHECK(ran.back().frame == 176);
    CHECK(player.pendingCue() == MovieCue{6, 0, 0, 0});
}

TEST_CASE("gfx data: the eight wipe patterns") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    const int iterations[8] = {33, 33, 24, 40, 48, 48, 18, 33};  // code_runtime.md §14.2
    for (int k = 0; k < 8; ++k) {
        CAPTURE(k);
        const auto w = WipePattern::parse(arc.read(static_cast<uint16_t>(kWipePatternFirst + k)));
        CHECK(w.period == 2);
        Screen s;
        fillScreen(s, 4);
        Vram vram{};
        BorlandRand rng;
        Wipe wipe(s, w, rng);
        while (!wipe.finished() && wipe.steps() < 1000) wipe.step(s, vram);
        CHECK(wipe.steps() == iterations[k]);
        int uncopied = 0;
        for (int y = 9; y <= 164; ++y)
            for (int x = 0; x < 320; ++x) uncopied += vram[static_cast<size_t>(y * 320 + x)] != 4;
        CHECK(uncopied == (k == 4 ? 156 : 0));  // 0x11DA: column 319 of rows 9..164
    }
}

TEST_CASE("gfx data: Screen_Show fades in when faded, else wipes and clears the dirty map") {
    GG_REQUIRE_DATA();
    Video v;
    v.init(ggFiles->exe(), ggFiles->archive());
    v.cursor().hide();
    BorlandRand rng, ref;
    rng.setRawSeed(0x9DEA5908u);
    ref.setRawSeed(0x9DEA5908u);
    {  // faded (after Gfx_Init's blackout): Dirty_Flush, then Pal_FadeIn; no rand()
        ScreenShow show(v, ggFiles->archive(), rng);
        CHECK(show.fading());
        CHECK(show.fade().loads() == 9);
        CHECK(v.screen().dirty().pending() == 0);
        show.finish(v);
        CHECK(rng.seed() == ref.seed());
    }
    v.screen().setDrawColor(7);
    v.screen().fillRect(0, 0, 320, 200);  // marks every tile
    ScreenShow show(v, ggFiles->archive(), rng);
    REQUIRE(!show.fading());
    const int k = ref.random(8);
    CHECK(show.wipePattern() == kWipePatternFirst + k);
    CHECK(show.wipe().soundEffect() == kWipeSoundBase + ref.random(2));
    CHECK(rng.seed() == ref.seed());  // two rand() calls: the pattern, then the sound
    while (!show.wipe().finished()) show.wipe().step(v.screen(), v.vram());
    show.finish(v);
    CHECK(v.screen().dirty().pending() == 0);
    CHECK(v.screen().dirty().dirtyTiles() == 0);  // cleared without a flush: rows 0..8 never reached VRAM
    CHECK(v.vram()[0] == 0);
    CHECK(v.vram()[100 * 320 + 100] == 7);
    CHECK(v.screen().mouseHideState() == 0);
}

TEST_CASE("gfx data: the base palette, the cursor regions and Gfx_Init") {
    GG_REQUIRE_DATA();
    const Palette base = Palette::fromExe(ggFiles->exe());
    CHECK(std::all_of(base.shadow().begin(), base.shadow().end(), [](uint8_t v) { return v <= 63; }));
    Video v;
    const PaletteLoad black = v.init(ggFiles->exe(), ggFiles->archive());
    CHECK(black.remainingRetraces() == 2);
    CHECK(v.palette().faded());
    CHECK(v.cursor().arrow()->width == 9);
    CHECK(v.cursor().arrow()->height == 9);
    CHECK(v.cursor().defaultRegion().w == 320);
    CHECK(v.cursor().defaultRegion().h == 200);
    CHECK(v.screen().dirty().dirtyTiles() == 1000);
}

// --- against the DOSBox-X captures -----------------------------------------------------------------------

TEST_CASE("gfx captures: the title screen and movie frames are pixel exact") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto dir = testdata::scratchDir("gg_gfx_captures");
    const std::string data = pathToUtf8(ggFiles->folder());
    int code = -1;
    // Title phase 3 with the screen animations at their frame steps 2 and 1 (w0/01_title).
    REQUIRE(gfxCli({"--gg-title", pathToUtf8(dir / "title.png"), "--anim-step", "2,1", "--data", data}, code));
    REQUIRE(code == 0);
    if (const auto cap = testdata::ggCapture("w0/01_title.png"))
        CHECK(differingPixels(loadFrame(dir / "title.png"), loadFrame(*cap)) == 0);
    // The final movie through the results palette (r2/movie): frames 14, 16, 18, 115, 146.
    REQUIRE(gfxCli({"--gg-movie", "3000", "30ca", pathToUtf8(dir / "movie"), "--data", data}, code));
    REQUIRE(code == 0);
    const std::pair<const char*, int> shots[] = {{"r2/movie/02_movie_start.png", 14}, {"r2/movie/03_movie_10s.png", 16},
                                                 {"r2/movie/04_movie_20s.png", 18}, {"r2/movie/05_movie_35s.png", 115},
                                                 {"r2/movie/06_movie_50s.png", 146}};
    for (const auto& [shot, frame] : shots) {
        CAPTURE(shot);
        if (const auto cap = testdata::ggCapture(shot))
            CHECK(differingPixels(loadFrame(dir / "movie" / std::format("movie_{:03d}.png", frame)), loadFrame(*cap)) == 0);
    }
}
