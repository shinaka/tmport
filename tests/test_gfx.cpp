#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "gfx/frame_buffer.h"
#include "gfx/pixmap.h"
#include "mischief/gfx/palette.h"
#include "mischief/gfx/presenter.h"
#include "mischief/rtl/borland_rand.h"

using namespace tmport;
using namespace tmport::gfx;

namespace {

// A synthetic 256-colour palette: index i = (i, i, i) except the key colour at 15 and 241 and
// white at 0 and 77 (as in the shipped palette's layout, but no game data).
mischief::gfx::Palette testPalette() {
    std::vector<uint8_t> rgb(768);
    for (int i = 0; i < 256; ++i) rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = static_cast<uint8_t>(i == 0 ? 1 : i);
    auto set = [&](int i, uint8_t r, uint8_t g, uint8_t b) {
        rgb[i * 3] = r;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = b;
    };
    set(15, 0, 0, 0xAA);
    set(241, 0, 0, 0xAA);
    set(0, 255, 255, 255);
    set(77, 255, 255, 255);
    set(200, 255, 255, 85);  // yellow
    set(201, 0, 0, 0);       // black
    set(202, 0, 0, 170);     // blue (same as the key colour!)
    set(203, 170, 0, 0);     // red
    set(204, 170, 85, 0);    // brown
    mischief::gfx::Palette p = mischief::gfx::Palette::fromRrgb(rgb);
    std::array<mischief::Rgb8, 16> ega{};
    ega[0] = {0, 0, 0};
    ega[1] = {0, 0, 170};
    ega[4] = {170, 0, 0};
    ega[6] = {170, 85, 0};
    ega[14] = {255, 255, 85};
    ega[15] = {255, 255, 255};
    std::array<uint8_t, 16> v16{};
    p.setPens(ega, v16);
    return p;
}

PixMap makePix(int w, int h, std::vector<uint8_t> px) {
    PixMap pm;
    pm.width = w;
    pm.height = h;
    pm.pixels = std::move(px);
    return pm;
}

}  // namespace

TEST_CASE("RUND encoder matches the documented algorithm on a small vector") {
    const std::vector<uint8_t> px = {5, 5, 5, 5, 5, 1, 2, 3};
    const auto enc = rundCompress(px, 8, 1);
    const std::vector<uint8_t> expect = {8, 0, 1, 0, 'R', 'U', 'N', 'D', 0x85, 5, 0x03, 1, 2, 3};
    CHECK(enc == expect);
    size_t end = 0;
    CHECK(rundDecompress(enc, 8, 1, 8, &end) == px);
    CHECK(end == enc.size());
    // No run may start in the last 5 pixels (i < N2 - 1 in header-inclusive coordinates).
    const std::vector<uint8_t> tail = {1, 2, 3, 4, 9, 9, 9, 9};
    const auto enc2 = rundCompress(tail, 8, 1);
    CHECK(enc2 == std::vector<uint8_t>{8, 0, 1, 0, 'R', 'U', 'N', 'D', 0x08, 1, 2, 3, 4, 9, 9, 9, 9});
}

TEST_CASE("RUND round trip on pseudo-random images, packets crossing rows") {
    mischief::BorlandRand r;
    for (int t = 0; t < 50; ++t) {
        const int w = 1 + r.random(97), h = 1 + r.random(41);
        std::vector<uint8_t> px(static_cast<size_t>(w) * h);
        uint8_t v = 0;
        for (auto& p : px) {
            if (r.random(4) == 0) v = static_cast<uint8_t>(r.random(6));
            p = v;
        }
        const auto enc = rundCompress(px, w, h);
        CHECK(isRund(enc));
        PixMap pm = decodePmap(enc, 8);
        CHECK(pm.encoding == PixEncoding::Rund);
        CHECK(pm.width == w);
        CHECK(pm.height == h);
        CHECK(pm.pixels == px);
        CHECK(pm.consumed == enc.size());
    }
}

TEST_CASE("PMAP decode: 4 bpp packed rows and raw 8 bpp") {
    // 3x2, 4 bpp: stride 2, high nibble = left pixel, unused low nibble 0.
    const std::vector<uint8_t> d4 = {3, 0, 2, 0, 0x12, 0x30, 0xAB, 0xC0};
    PixMap a = decodePmap(d4, 4);
    CHECK(a.pixels == std::vector<uint8_t>{1, 2, 3, 0xA, 0xB, 0xC});
    CHECK(a.consumed == 8);
    const std::vector<uint8_t> d8 = {2, 0, 2, 0, 9, 8, 7, 6, 0, 0};
    PixMap b = decodePmap(d8, 8);
    CHECK(b.encoding == PixEncoding::Raw8);
    CHECK(b.pixels == std::vector<uint8_t>{9, 8, 7, 6});
    CHECK_THROWS(decodePmap(std::vector<uint8_t>{4, 0, 4, 0, 1}, 8));
}

TEST_CASE("FrameBuffer: keyed sprite, inclusive clip, mirror, dirty rects") {
    const mischief::gfx::Palette p = testPalette();
    FrameBuffer fb;
    fb.setPalette(p);
    fb.clear(7);
    // 4x2 sprite with key pixels (15 and 241).
    const PixMap s = makePix(4, 2, {1, 2, 15, 4, 241, 6, 7, 8});
    fb.setClip({10, 10, 10, 12});  // top, bottom, left, right (inclusive): x 10..12, y 10 only
    fb.setDirtyEnabled(true);
    fb.drawSprite(9, 9, s, false);
    // Row y=9 is outside the clip; row y=10 = sprite row 1: x 9 (clipped), 10:6, 11:7, 12:8? x 12 = col 3 = 8
    CHECK(fb.at(9, 10) == 7);
    CHECK(fb.at(10, 10) == 6);
    CHECK(fb.at(11, 10) == 7);
    CHECK(fb.at(12, 10) == 8);
    CHECK(fb.at(13, 10) == 7);  // right of the clip
    CHECK(fb.at(10, 9) == 7);
    REQUIRE(fb.dirtyRects().size() == 1);
    CHECK(fb.dirtyRects()[0] == mischief::gfx::Rect{9, 9, 4, 2});  // the unclipped sprite box

    fb.resetClip();
    fb.clear(7);
    fb.drawSprite(100, 50, s, true);  // mirrored: destination X takes source column w-1-(X-x)
    CHECK(fb.at(100, 50) == 4);
    CHECK(fb.at(101, 50) == 7);  // key pixel (15) skipped
    CHECK(fb.at(102, 50) == 2);
    CHECK(fb.at(103, 50) == 1);
    CHECK(fb.at(103, 51) == 7);  // key 241 skipped
    CHECK(fb.at(100, 51) == 8);

    // Dirty rects are clipped to 512x384 and capped at 256.
    fb.clearDirtyRects();
    fb.addDirtyRect(-5, 380, 20, 10);
    CHECK(fb.dirtyRects().back() == mischief::gfx::Rect{0, 380, 15, 4});
    fb.addDirtyRect(0, 400, 10, 10);  // entirely below the screen: dropped
    CHECK(fb.dirtyRects().size() == 1);
    for (int i = 0; i < 300; ++i) fb.addDirtyRect(0, 0, 1, 1);
    CHECK(fb.dirtyRects().size() == FrameBuffer::kMaxDirtyRects);
    fb.setDirtyEnabled(false);
    fb.clearDirtyRects();
    fb.addDirtyRect(0, 0, 5, 5);
    CHECK(fb.dirtyRects().empty());
}

TEST_CASE("FrameBuffer: opaque draws are unclipped (by the clip rectangle) and copyRect") {
    const mischief::gfx::Palette p = testPalette();
    FrameBuffer fb;
    fb.setPalette(p);
    fb.clear(0);
    fb.setClip({0, 0, 0, 0});
    const PixMap s = makePix(2, 2, {15, 1, 2, 3});
    fb.drawOpaque(510, 1053, s);  // clipped only by the buffer
    CHECK(fb.at(510, 1053) == 15);  // key colour drawn: opaque
    CHECK(fb.at(511, 1054) == 3);
    fb.copyRect(510, 1053, 2, 2, 0, 384);
    CHECK(fb.at(0, 384) == 15);
    CHECK(fb.at(1, 385) == 3);
}

TEST_CASE("FrameBuffer: fills and frames (WinG 256-colour and 16-colour paths)") {
    const mischief::gfx::Palette p = testPalette();
    FrameBuffer fb;
    fb.setPalette(p);
    fb.clear(7);
    fb.fillRectColor(10, 10, 0, 5, 14);  // empty width: nothing in 256 colours
    CHECK(fb.at(10, 10) == 7);
    fb.frameRectColor(20, 20, 5, 6, 15);  // WinG: bottom edge at top + max(1, h-2)
    const uint8_t w = p.pen(15);
    CHECK(fb.at(20, 20) == w);
    CHECK(fb.at(24, 20) == w);
    CHECK(fb.at(20, 23) == w);
    CHECK(fb.at(24, 23) == w);
    CHECK(fb.at(22, 24) == w);  // bottom row at 20 + 4
    CHECK(fb.at(22, 25) == 7);  // GDI would draw it at 25
    CHECK(fb.at(20, 24) == w);

    mischief::gfx::Palette v = mischief::gfx::Palette::vga16();
    v.setPens({}, {0, 4, 2, 6, 1, 5, 1, 8, 7, 12, 10, 14, 9, 13, 11, 15});
    FrameBuffer f16(mischief::gfx::ColorMode::Colour16);
    f16.setPalette(v);
    f16.clear(0);
    f16.fillRectColor(10, 10, 0, 0, 14);  // widened to 1x1
    CHECK(f16.at(10, 10) == 11);
    f16.frameRectColor(20, 20, 5, 6, 15);  // top/left one pixel longer than GDI
    CHECK(f16.at(25, 20) == 15);
    CHECK(f16.at(20, 26) == 15);
    CHECK(f16.at(24, 25) == 15);
    CHECK(f16.at(21, 21) == 0);
}

TEST_CASE("FrameBuffer: save-under rounds to even x and width") {
    FrameBuffer fb;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) fb.row(y)[x] = static_cast<uint8_t>(x + y * 20);
    const PixMap s = fb.saveRect(3, 2, 4, 3);  // x 3 -> 2, w 4 -> 5 -> 6
    CHECK(s.width == 6);
    CHECK(s.height == 3);
    CHECK(s.at(0, 0) == 2 + 2 * 20);
    FrameBuffer copy = fb;
    copy.fillRectColor(0, 0, 20, 20, 0);
    copy.restoreRect(3, 2, s);  // drawn at x & ~1 = 2
    for (int y = 2; y < 5; ++y)
        for (int x = 2; x < 8; ++x) CHECK(copy.at(x, y) == fb.at(x, y));
}

TEST_CASE("Presenter: window picture and 640x480 canvas") {
    const mischief::gfx::Palette p = testPalette();
    FrameBuffer fb;
    fb.clear(200);
    mischief::gfx::Presenter pr;
    pr.setPalette(p);
    const auto c0 = pr.canvas();
    // Nothing presented yet: the game area shows the initial (index 0) picture, borders black.
    CHECK(c0[0] == 0);
    CHECK(c0[3] == 255);
    pr.blitToWindow(fb, 500, 380, 50, 50);  // clipped to 512x384
    const auto c = pr.canvas();
    auto px = [&](int x, int y) { return c.subspan((static_cast<size_t>(y) * 640 + x) * 4, 4); };
    CHECK(px(64 + 511, 48 + 383)[0] == 255);  // yellow (255,255,85)
    CHECK(px(64 + 511, 48 + 383)[2] == 85);
    CHECK(px(64 + 499, 48 + 383)[2] == 255);  // index 0 (white): not presented
    CHECK(px(64 + 512, 48 + 383)[0] == 0);    // border
    CHECK(px(0, 0)[3] == 255);
    const auto area = pr.gameAreaRgba();
    CHECK(area.size() == 512u * 384u * 4u);
}

TEST_CASE("Presenter: debug marks are outlines over the game area, under the overlay") {
    const mischief::gfx::Palette p = testPalette();
    FrameBuffer fb;
    fb.clear(200);
    mischief::gfx::Presenter pr;
    pr.setPalette(p);
    pr.blitAll(fb);
    const std::vector<uint8_t> before(pr.canvas().begin(), pr.canvas().end());
    auto px = [&](int x, int y) {
        uint32_t v;
        std::memcpy(&v, pr.canvas().data() + (static_cast<size_t>(y) * 640 + x) * 4, 4);
        return v;
    };
    constexpr uint32_t kGold = 0xFF00D7FFu;
    pr.setDebugMarks({{100, 50, 20, 10, kGold}});
    CHECK(px(64 + 100, 48 + 50) == kGold);  // top-left corner
    CHECK(px(64 + 119, 48 + 59) == kGold);  // bottom-right corner (inside w x h)
    CHECK(px(64 + 101, 48 + 55) == kGold);  // left edge, 2 px thick
    CHECK(px(64 + 102, 48 + 55) != kGold);  // the inside shows the game
    CHECK(px(64 + 120, 48 + 55) != kGold);  // outside
    // Presenting the game area again keeps the mark (it is not part of the window picture).
    pr.blitAll(fb);
    CHECK(px(64 + 100, 48 + 50) == kGold);
    // The overlay (menu bar) wins over a mark.
    pr.overlayPixels()[static_cast<size_t>(48 + 50) * 640 + 64 + 100] = 0xFF0000FFu;
    pr.overlayChanged(64 + 100, 48 + 50, 1, 1);
    CHECK(px(64 + 100, 48 + 50) == 0xFF0000FFu);
    pr.clearOverlay();
    CHECK(px(64 + 100, 48 + 50) == kGold);
    // Removing the marks restores the canvas exactly.
    pr.setDebugMarks({});
    CHECK(std::equal(before.begin(), before.end(), pr.canvas().begin()));
}

// ------------------------------------------------------------------------- presenter layouts

TEST_CASE("Presenter: the DOS layout takes RGBA frames and has no overlay") {
    mischief::gfx::Presenter pr(mischief::gfx::PresenterLayout::dos1990());
    CHECK(pr.layout() == mischief::gfx::PresenterLayout::dos1990());
    CHECK(pr.canvas().size() == 320u * 200u * 4u);
    std::vector<uint32_t> frame(320 * 200);
    for (size_t i = 0; i < frame.size(); ++i)
        frame[i] = 0xFF000000u | static_cast<uint32_t>((i * 2654435761u) & 0xFFFFFFu);
    pr.presentFrame(frame);
    CHECK(std::memcmp(pr.canvas().data(), frame.data(), frame.size() * 4) == 0);
    const auto area = pr.gameAreaRgba();
    REQUIRE(area.size() == 320u * 200u * 4u);
    CHECK(std::memcmp(area.data(), frame.data(), area.size()) == 0);
    // Bytes in memory are R, G, B, A.
    frame[0] = 0xFF332211u;
    pr.presentFrame(frame);
    CHECK(pr.canvas()[0] == 0x11);
    CHECK(pr.canvas()[1] == 0x22);
    CHECK(pr.canvas()[2] == 0x33);
    CHECK(pr.canvas()[3] == 0xFF);
    // A palette change leaves an RGBA frame alone; a frame of the wrong size is ignored.
    pr.setPalette(testPalette());
    CHECK(pr.canvas()[0] == 0x11);
    pr.presentFrame(std::vector<uint32_t>(10, 0xFFFFFFFFu));
    CHECK(pr.canvas()[0] == 0x11);
    // No overlay layer in this layout.
    CHECK(pr.overlayPixels().empty());
    pr.overlayChanged(0, 0, 320, 200);
    CHECK_FALSE(pr.hasOverlay());
    CHECK(pr.canvas()[0] == 0x11);
    // Debug marks work in game-area coordinates (the game area is the whole canvas).
    pr.setDebugMarks({{10, 10, 8, 8, 0xFF00FF00u}});
    uint32_t v = 0;
    std::memcpy(&v, pr.canvas().data() + (10 * 320 + 10) * 4, 4);
    CHECK(v == 0xFF00FF00u);
    pr.setDebugMarks({});
    CHECK(std::memcmp(pr.canvas().data(), frame.data(), frame.size() * 4) == 0);
    // Back to the window picture (indices through the palette).
    FrameBuffer fb;
    fb.clear(200);
    pr.blitToWindow(fb, 0, 0, 1, 1);
    CHECK(pr.canvas()[0] == 255);  // yellow (255,255,85)
    CHECK(pr.canvas()[2] == 85);
    CHECK(pr.canvas()[4] == 255);  // index 0 (white): presented as indices too
}
