// Win16 cursor decoding (input/cursors.h). Synthetic blobs always; the EXE's cursors 128-130
// when TM_DATA is set. Set TM_CURSOR_DUMP=<dir> to write the decoded cursors as PNGs.
#include <doctest/doctest.h>

#include <array>
#include <format>
#include <vector>

#include "data/dgroup_offsets.h"
#include "input/cursors.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/util/png_writer.h"
#include "test_data.h"

using namespace tmport;

namespace {

void put16(std::vector<uint8_t>& v, size_t o, uint16_t x) {
    v[o] = static_cast<uint8_t>(x);
    v[o + 1] = static_cast<uint8_t>(x >> 8);
}
void put32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[o + static_cast<size_t>(i)] = static_cast<uint8_t>(x >> (8 * i));
}

// A 16x16 RT_CURSOR blob (BITMAPINFOHEADER variant). Rows are given top-down as strings of
// '#' black, '.' white, ' ' transparent, '~' inverted; stored bottom-up as Windows does.
std::vector<uint8_t> makeCursor(const std::array<const char*, 16>& rows, int hotX, int hotY, bool coreHeader = false) {
    const int w = 16, h = 16, stride = 4;
    const size_t hdr = coreHeader ? 12 : 40;
    const size_t rgb = coreHeader ? 3 : 4;
    const size_t bits = 4 + hdr + 2 * rgb;
    std::vector<uint8_t> v(bits + 2 * static_cast<size_t>(stride * h), 0);
    put16(v, 0, static_cast<uint16_t>(hotX));
    put16(v, 2, static_cast<uint16_t>(hotY));
    put32(v, 4, static_cast<uint32_t>(hdr));
    if (coreHeader) {
        put16(v, 8, w);
        put16(v, 10, 2 * h);
        put16(v, 12, 1);
        put16(v, 14, 1);
    } else {
        put32(v, 8, w);
        put32(v, 12, 2 * h);
        put16(v, 16, 1);
        put16(v, 18, 1);
    }
    // colour 0 black, colour 1 white
    for (size_t c = 0; c < 3; ++c) v[4 + hdr + rgb + c] = 0xFF;
    for (int y = 0; y < h; ++y) {
        const int row = h - 1 - y;
        for (int x = 0; x < w; ++x) {
            const char c = rows[static_cast<size_t>(y)][x];
            const bool andBit = c == ' ' || c == '~';
            const bool xorBit = c == '.' || c == '~';
            const auto bit = static_cast<uint8_t>(0x80 >> (x & 7));
            if (xorBit) v[bits + static_cast<size_t>(row * stride + x / 8)] |= bit;
            if (andBit) v[bits + static_cast<size_t>(stride * h + row * stride + x / 8)] |= bit;
        }
    }
    return v;
}

const std::array<const char*, 16> kArrow = {
    "#               ", "##              ", "#.#             ", "#..#            ", "#...#           ",
    "#....#          ", "#.....#         ", "#......#        ", "#.......#       ", "#....#####      ",
    "#.#..#          ", "##  #..#        ", "#   #..#    ~~  ", "     #..#   ~~  ", "     ####       ",
    "                ",
};

}  // namespace

TEST_CASE("cursor decode: synthetic Win16 cursor (both DIB header kinds)") {
    for (bool core : {false, true}) {
        const CursorImage c = decodeCursorResource(makeCursor(kArrow, 1, 2, core));
        CHECK(c.width == 16);
        CHECK(c.height == 16);
        CHECK(c.hotX == 1);
        CHECK(c.hotY == 2);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const char ch = kArrow[static_cast<size_t>(y)][x];
                const CursorImage::Pixel want = ch == '#'   ? CursorImage::kBlack
                                                : ch == '.' ? CursorImage::kWhite
                                                : ch == '~' ? CursorImage::kInvert
                                                            : CursorImage::kTransparent;
                CHECK(c.at(x, y) == want);
                // Masks are stored top-down, 1 bpp.
                const bool a = (c.andMask[static_cast<size_t>(y * 2 + x / 8)] >> (7 - (x & 7))) & 1;
                CHECK(a == (want == CursorImage::kTransparent || want == CursorImage::kInvert));
            }
        const auto rgba = c.rgba(2);
        REQUIRE(rgba.size() == 32u * 32u * 4u);
        CHECK(rgba[3] == 255);                       // (0,0) black, opaque
        CHECK(rgba[(5 * 32 + 3) * 4 + 0] == 255);    // (1,2) white at scale 2
        CHECK(rgba[(0 * 32 + 31) * 4 + 3] == 0);     // transparent
    }
    // Rejected input.
    auto bad = makeCursor(kArrow, 0, 0);
    put32(bad, 4, 64);
    CHECK_THROWS_AS(decodeCursorResource(bad), mischief::FormatError);
    CHECK_THROWS_AS(decodeCursorResource(makeCursor(kArrow, 16, 0)), mischief::FormatError);  // hot spot outside
    CHECK_THROWS_AS(decodeCursorResource(std::vector<uint8_t>(20, 0)), mischief::FormatError);
}

TEST_CASE("cursor decode: the EXE's cursors 128-130") {
    TM_REQUIRE_GAME_DATA();
    const auto& exe = g->exe->exe();
    // The game's id list (DS:1292).
    std::vector<uint16_t> ids;
    for (uint32_t off = dg::kCursorIds; ids.size() < 15; off += 2) {
        const uint16_t id = g->exe->u16(off);
        if (id == 0xFFFF) break;
        ids.push_back(id);
    }
    CHECK(ids == std::vector<uint16_t>{128, 129, 130});

    struct Expect {
        int transparent, black, white;
    };
    // Pixel census of the decoded cursors (observed; no inverted pixels in any of them).
    const Expect expect[3] = {{625, 399, 0}, {609, 242, 173}, {609, 243, 172}};
    std::vector<CursorImage> imgs;
    for (int i = 0; i < 3; ++i) {
        const CursorImage c = loadGroupCursor(exe, ids[static_cast<size_t>(i)]);
        CHECK(c.width == 32);
        CHECK(c.height == 32);
        CHECK(c.hotX == 0);  // all three resources store hot spot (0,0)
        CHECK(c.hotY == 0);
        int n[4] = {};
        for (uint8_t p : c.pixels) ++n[p];
        CHECK(n[CursorImage::kTransparent] == expect[i].transparent);
        CHECK(n[CursorImage::kBlack] == expect[i].black);
        CHECK(n[CursorImage::kWhite] == expect[i].white);
        CHECK(n[CursorImage::kInvert] == 0);
        // Mask sanity: every opaque pixel has AND = 0, every transparent one AND = 1 / XOR = 0.
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const bool a = (c.andMask[static_cast<size_t>(y * 4 + x / 8)] >> (7 - (x & 7))) & 1;
                const bool xb = (c.xorMask[static_cast<size_t>(y * 4 + x / 8)] >> (7 - (x & 7))) & 1;
                const auto p = c.at(x, y);
                CHECK(a == (p == CursorImage::kTransparent));
                CHECK(xb == (p == CursorImage::kWhite));
            }
        imgs.push_back(c);
    }
    // The walking cursors face opposite ways: 129 is (nearly) the mirror image of 130.
    int same = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) same += imgs[1].at(x, y) == imgs[2].at(31 - x, y);
    CHECK(same > 32 * 32 * 95 / 100);

    CursorSet set;
    set.load(exe, ids);
    CHECK(set.loadedCount() == 3);
    REQUIRE(set.image(1));
    CHECK(set.image(1)->pixels == imgs[1].pixels);
    CHECK(set.image(3) == nullptr);

    if (auto dir = mischief::getEnv("TM_CURSOR_DUMP")) {
        for (int i = 0; i < 3; ++i)
            for (int scale : {1, 4}) {
                const auto px = imgs[static_cast<size_t>(i)].rgba(scale);
                // Composite over grey so transparent and black are both visible.
                std::vector<uint8_t> out(px.size());
                for (size_t k = 0; k < px.size(); k += 4)
                    for (size_t c = 0; c < 4; ++c) out[k + c] = c == 3 ? 255 : (px[k + 3] ? px[k + c] : 128);
                const auto path = mischief::pathFromUtf8(*dir) / std::format("cursor_{}_x{}.png", ids[static_cast<size_t>(i)], scale);
                CHECK(mischief::png::writeRgba(path, 32 * scale, 32 * scale, out));
            }
    }
}
