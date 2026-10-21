// DOS edition graphics (src/dos/gfx): PAN/PAK decoding, palettes, back-buffer primitives, the
// blitter and the actor renderer, scenery, and the composed reference screens. Data-dependent
// cases read TM_DATA_DOS and store only counts, geometry and FNV-1a hashes; the hashes of decoded
// data equal those of the Python decoders in tools/dos, and the screen hashes are of renders that
// equal the DOSBox-X captures pixel for pixel (extracted/scratch/dos_w2_dos_gfx/compare_screens.py).

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <vector>

#include "dos/gfx/back_buffer.h"
#include "dos/gfx/blitter.h"
#include "dos/gfx/dos_screens.h"
#include "dos/gfx/pak.h"
#include "dos/gfx/palettes.h"
#include "dos/gfx/pan.h"
#include "dos/gfx/scenery.h"
#include "dos/text/dos_font.h"
#include "dos_test_data.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/util/hash.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

void put16(std::vector<uint8_t>& v, uint16_t w) {
    v.push_back(static_cast<uint8_t>(w));
    v.push_back(static_cast<uint8_t>(w >> 8));
}

// A PAN of one 16x2-tile picture: row 0 a run of 16 x entry 2, row 1 a 0xFFF copy of row 0; tiles:
// A5 3F 33 (63 x 33h) then A5 A5 (a literal A5h) = two tiles.
std::vector<uint8_t> tinyPan() {
    std::vector<uint8_t> map, tiles = {0xA5, 0x3F, 0x33, 0xA5, 0xA5}, f;
    put16(map, 0xF002);
    put16(map, 0x0FFF);
    put16(map, 0);
    put16(f, static_cast<uint16_t>(map.size()));
    put16(f, 16);
    put16(f, 2);
    put16(f, static_cast<uint16_t>(tiles.size()));
    f.insert(f.end(), map.begin(), map.end());
    f.insert(f.end(), tiles.begin(), tiles.end());
    return f;
}

// A PAK with the given frames {width bytes, height, x off, y off, rows (raw)}.
struct TinyFrame {
    int w, h, xo, yo;
    std::vector<uint8_t> pixels;  // packed rows
};
std::vector<uint8_t> tinyPak(const std::vector<TinyFrame>& frames) {
    std::vector<uint8_t> f;
    put16(f, static_cast<uint16_t>(frames.size()));
    put16(f, 0), put16(f, 62), put16(f, 58);
    const size_t table = f.size();
    for (size_t i = 0; i < frames.size(); ++i) put16(f, 0);
    put16(f, 0xFFFF), put16(f, 0xFFFF);
    for (size_t i = 0; i < frames.size(); ++i) {
        while (f.size() % 16) f.push_back(0);
        f[table + 2 * i] = static_cast<uint8_t>(f.size() / 16);
        f[table + 2 * i + 1] = static_cast<uint8_t>((f.size() / 16) >> 8);
        const TinyFrame& t = frames[i];
        put16(f, 0), put16(f, static_cast<uint16_t>(t.w)), put16(f, static_cast<uint16_t>(t.h));
        put16(f, static_cast<uint16_t>(t.xo)), put16(f, static_cast<uint16_t>(t.yo));
        put16(f, 0), put16(f, 0), put16(f, 0);
        for (int y = 0; y < t.h; ++y) {
            if (y % 2 == 0) {  // raw row
                f.push_back(static_cast<uint8_t>(0x80 | t.w));
                f.insert(f.end(), t.pixels.begin() + y * t.w, t.pixels.begin() + (y + 1) * t.w);
            } else {  // one (count, byte) run per byte
                f.push_back(static_cast<uint8_t>(2 * t.w));
                for (int x = 0; x < t.w; ++x) f.push_back(1), f.push_back(t.pixels[static_cast<size_t>(y * t.w + x)]);
            }
        }
    }
    while (f.size() % 16) f.push_back(0);
    return f;
}

// Frame {1 0 0 2 / 3 0 3 0}, hotspot (-1, -2).
PakFile testPak() { return PakFile::load(tinyPak({{2, 2, -1, -2, {0x10, 0x02, 0x30, 0x30}}}), "TEST.PAK"); }

uint64_t hashLogical(const BackBuffer& b) { return mischief::fnv1a64(b.logicalPixels()); }

}  // namespace

// ------------------------------------------------------------------------------------- PAN / PAK

TEST_CASE("DOS PAN: word-RLE map with runs and row copies, A5h tile escapes, in-place buffers") {
    const PanFile p = PanFile::load(tinyPan(), "TINY.PAN");
    CHECK(p.widthTiles() == 16);
    CHECK(p.heightTiles() == 2);
    CHECK(p.pictureCount() == 1);
    CHECK(p.mapWordCount() == 32);
    CHECK(p.rowBackRefs() == 1);
    CHECK(p.tileCount() == 2);
    for (size_t i = 0; i < 32; ++i) CHECK(p.mapWord(i) == 2);  // column-major: rows 0 and 1 of every column
    CHECK(p.tile(2)[0] == 0x33);
    CHECK(p.tile(2)[31] == 0xA5);  // A5 A5 = a literal A5h
    CHECK(p.mapBuffer().size() == PanFile::kMapBufferSize);
    // The packed map words are still at the end of the buffer (6 bytes, not overwritten).
    CHECK(p.mapBuffer()[PanFile::kMapBufferSize - 6] == 0x02);
    CHECK(p.mapWord(PanFile::kMapBufferSize) == 0);        // past the buffer
    CHECK(p.tile(0x7000)[5] == 0);                          // past the tile buffer: zero tile
    const auto px = p.picturePixels(0);
    CHECK(px.size() == 128 * 16);
    CHECK(px[0] == 3);
    CHECK(px[127 + 15 * 128] == 5);  // last pixel of the last tile: low nibble of A5h

    auto bad = tinyPan();
    bad.push_back(0);
    CHECK_THROWS_AS(PanFile::load(bad, "BAD.PAN"), mischief::FormatError);
}

TEST_CASE("DOS PAK: offset table, raw and run rows, header fields") {
    const PakFile k = testPak();
    REQUIRE(k.count() == 1);
    const PakFrame& f = k.frame(0);
    CHECK(f.widthBytes == 2);
    CHECK(f.widthPixels() == 4);
    CHECK(f.height == 2);
    CHECK(f.xOffset == -1);
    CHECK(f.yOffset == -2);
    CHECK(f.rawRows == 1);
    CHECK(f.runRows == 1);
    CHECK(f.pixel(0, 0) == 1);
    CHECK(f.pixel(3, 0) == 2);
    CHECK(f.pixel(2, 1) == 3);
    CHECK(k.headerWords()[1] == 62);
    auto bad = tinyPak({{2, 2, 0, 0, {1, 2, 3, 4}}});
    bad[10] = 0;  // break the FFFF FFFF trailer
    CHECK_THROWS_AS(PakFile::load(bad, "BAD.PAK"), mischief::FormatError);
}

TEST_CASE("DOS PAN/PAK: the user's files decode like tools/dos (counts and hashes)") {
    TM_REQUIRE_DOS_DATA();
    const auto files = DosGfxFiles::open(*testdata::dosDataPath());
    struct PanRef {
        const char* name;
        int pictures, tiles, backRefs;
        size_t words;
        uint64_t mapHash, tileHash;
    };
    const PanRef pans[] = {
        {"OPENING", 1, 673, 0, 1000, 0x203d8382c69d83c6ull, 0xaec7f511389f1780ull},
        {"MOUNTAIN", 81, 1849, 404, 20736, 0x15e59d3ce31700b1ull, 0xf8a112d980fa5bf9ull},
        {"MOUNT2N3", 76, 1368, 301, 19456, 0x1034127677618b68ull, 0xc674a68779f736d6ull},
        {"MOUNT3", 68, 1401, 284, 17408, 0x2de9a31fbcffa541ull, 0x05b4926531baa80cull},
        {"CLOSE", 5, 292, 15, 1280, 0x8c4bb553396d1160ull, 0x8a037f0e0d5649d4ull},
        {"SHACK", 7, 330, 9, 1792, 0x3d56b64749f63bb4ull, 0x228b426bb6c80863ull},
    };
    for (const PanRef& r : pans) {
        CAPTURE(r.name);
        const PanFile& p = files.pan(r.name);
        CHECK(p.pictureCount() == r.pictures);
        CHECK(p.tileCount() == r.tiles);
        CHECK(p.rowBackRefs() == r.backRefs);
        CHECK(p.mapWordCount() == r.words);
        CHECK(mischief::fnv1a64(std::span(p.mapBuffer()).first(r.words * 2)) == r.mapHash);
        CHECK(mischief::fnv1a64(std::span(p.tileBuffer()).first(p.tileByteCount())) == r.tileHash);
    }
    struct PakRef {
        const char* name;
        int frames, raw, run;
        uint64_t hash;
    };
    const PakRef paks[] = {
        {"SPRITES", 223, 4436, 2464, 0xfd95ac8a5620c746ull},
        {"BEGIN", 42, 1058, 360, 0xe908cc08d04a2437ull},
        {"CLOSE", 133, 2485, 1653, 0xec4f7bea955adc4aull},
        {"SHACK", 126, 2246, 846, 0xbf0639416189a30dull},
    };
    for (const PakRef& r : paks) {
        CAPTURE(r.name);
        const PakFile& k = files.pak(r.name);
        CHECK(k.count() == r.frames);
        int raw = 0, run = 0;
        uint64_t h = mischief::kFnv64Offset;
        for (const PakFrame& f : k.frames()) {
            raw += f.rawRows;
            run += f.runRows;
            h = mischief::fnv1a64U16(static_cast<uint16_t>(f.widthBytes), h);
            h = mischief::fnv1a64U16(static_cast<uint16_t>(f.height), h);
            h = mischief::fnv1a64U16(static_cast<uint16_t>(f.xOffset), h);
            h = mischief::fnv1a64U16(static_cast<uint16_t>(f.yOffset), h);
            h = mischief::fnv1a64(f.rows, h);
            CHECK(f.word0 == 0);
            CHECK((f.usedBytes + 15) / 16 * 16 == f.slotBytes);  // every frame fills its 16-byte slot
        }
        CHECK(raw == r.raw);
        CHECK(run == r.run);
        CHECK(h == r.hash);
    }
    // The message scroll (fmt_graphics.md §5.4): 306x125 at (8, 19).
    const PakFrame& scroll = files.pak("SPRITES").frame(57);
    CHECK(scroll.widthBytes == 153);
    CHECK(scroll.height == 125);
    CHECK(scroll.xOffset == 4);
    CHECK(scroll.yOffset == 19);
    // BEGIN frames 0..2 are the pointer frames of SPRITES.
    for (int i = 0; i < 3; ++i) CHECK(files.pak("BEGIN").frame(i).rows == files.pak("SPRITES").frame(i).rows);
}

// ----------------------------------------------------------------------------------- palettes

TEST_CASE("DOS palettes: EGA = Tandy RGBI, MCGA rounding, CGA dither positions") {
    std::array<uint8_t, 16> dither{};
    dither[2] = 0x99;  // 10 01 10 01
    dither[14] = 0x82;
    CHECK(cgaPixel(dither, 2, 0, 0) == 2);
    CHECK(cgaPixel(dither, 2, 1, 0) == 1);
    CHECK(cgaPixel(dither, 2, 0, 1) == 1);  // odd rows: b a d c
    CHECK(cgaPixel(dither, 14, 0, 0) == 2);
    CHECK(cgaPixel(dither, 14, 3, 0) == 2);
    CHECK(cgaPixel(dither, 14, 1, 1) == 2);
    CHECK(cgaPixel(dither, 14, 0, 1) == 0);
    CHECK(parseLook("MCGA") == Look::Mcga);
    CHECK(!parseLook("vga"));
    CHECK(lookName(Look::Tandy) == "tandy");

    TM_REQUIRE_DOS_DATA();
    auto hashOf = [](const LookPalette& p) {
        std::vector<uint8_t> b;
        for (const mischief::Rgb8& c : p.colours) b.insert(b.end(), {c.r, c.g, c.b});
        return mischief::fnv1a64(b);
    };
    const LookPalette ega = lookPalette(*dosExe, Look::Ega), tandy = lookPalette(*dosExe, Look::Tandy);
    const LookPalette mcga = lookPalette(*dosExe, Look::Mcga);
    CHECK(ega.colours == tandy.colours);
    CHECK(hashOf(ega) == 0x4389720808a60c74ull);
    CHECK(hashOf(mcga) == 0xa00c1c34abc68da3ull);
    CHECK(ega.colours[15] == mischief::Rgb8{0, 0, 0});        // logical 15 = black
    CHECK(ega.colours[0] == mischief::Rgb8{0, 0, 0xAA});      // logical 0 = blue
    // The DOSBox-X expansion of a 6-bit DAC value: (v*255+31)/63 (0x27 -> 0x9E, not 0x9D).
    CHECK(mcga.colours[5] == mischief::Rgb8{0x9E, 0x00, 0x9E});

    BackBuffer b;
    b.fillRect(0, 0, 0x9F, 0xC7, 0x0F);
    const auto rgba = toRgba(b, ega);
    REQUIRE(rgba.size() == 320u * 200u * 4u);
    CHECK(rgba[0] == 0x00);   // R of blue
    CHECK(rgba[2] == 0xAA);   // B
    CHECK(rgba[3] == 0xFF);   // A
    CHECK(rgba[4] == 0x00);   // the right pixel: black
    CHECK(rgba[6] == 0x00);
    const auto packed = toRgba32(b, ega);
    CHECK(packed[0] == 0xFFAA0000u);  // bytes R, G, B, A in memory
    CHECK(packed[1] == 0xFF000000u);
    LookPalette cga = ega;
    cga.look = Look::Cga;
    cga.cgaDither[0] = 0x99;
    CHECK(toRgba32(b, cga)[0] == 0xFFFF55FFu);  // light magenta (pixel a of 10 01 10 01)
}

// -------------------------------------------------------------------------------- back buffer

TEST_CASE("DOS back buffer: fill and colour replace work on packed bytes") {
    BackBuffer b;
    b.fillRect(1, 2, 3, 4, 0xAB);
    CHECK(b.byteAt(1, 2) == 0xAB);
    CHECK(b.byteAt(3, 4) == 0xAB);
    CHECK(b.byteAt(4, 4) == 0x00);
    CHECK(b.pixel(2, 2) == 0xA);
    CHECK(b.pixel(3, 2) == 0xB);
    b.replaceColor(0, 0, 0x9F, 0xC7, 0xAAAA, 0x5555);  // only the left pixels equal A
    CHECK(b.byteAt(2, 3) == 0x5B);
    // The left pixel is compared with the high byte's high nibble, the right one with the low byte's
    // low nibble (1000:179d).
    b.replaceColor(0, 0, 0x9F, 0xC7, 0x500B, 0x7008);
    CHECK(b.byteAt(2, 3) == 0x78);
    CHECK(b.byteAt(0, 0) == 0x00);
}

// ------------------------------------------------------------------------------- CGA plane

namespace {

// A synthetic dither table (not the game's): 2 = 10 01 10 01, 3 = 00 01 10 11, 12 = white, 14 =
// 10 00 00 10, 15 = black (a solid 00); the CGA plane test cases below use it.
std::array<uint8_t, 16> testDither() {
    std::array<uint8_t, 16> d{};
    d[2] = 0x99;
    d[3] = 0x1B;
    d[12] = 0xFF;
    d[14] = 0x82;
    d[15] = 0x00;
    return d;
}

// The 2-bit pixels of row y from pixel x0 on.
std::vector<int> cgaRow(const BackBuffer& b, int y, int x0, int n) {
    std::vector<int> v;
    for (int x = x0; x < x0 + n; ++x) v.push_back(b.cgaPixel(x, y));
    return v;
}

}  // namespace

TEST_CASE("DOS CGA plane: the tile rule by default, fills on the even tables, raw writers") {
    BackBuffer b;
    CHECK(!b.cgaEnabled());
    b.enableCga(testDither());
    REQUIRE(b.cgaEnabled());
    // Nothing drawn by a CGA-aware routine: Tile_DrawColumn's rule (a b c d on even rows, b a d c odd).
    b.setPixel(0, 0, 14);
    b.setPixel(0, 1, 14);
    CHECK(b.cgaPixel(0, 0) == 2);
    CHECK(b.cgaPixel(0, 1) == 0);
    // Gfx_FillRect 1000:1707: 7E00/7F00 on every row, so odd rows show a b c d too.
    b.fillRect(0, 0, 3, 1, 0xEE);
    CHECK(cgaRow(b, 0, 0, 8) == std::vector<int>{2, 0, 0, 2, 2, 0, 0, 2});
    CHECK(cgaRow(b, 1, 0, 8) == std::vector<int>{2, 0, 0, 2, 2, 0, 0, 2});
    // An odd first byte keeps the high half of its CGA byte; pixel for pixel the same rule.
    b.fillRect(5, 3, 5, 3, 0x22);
    CHECK(cgaRow(b, 3, 10, 2) == std::vector<int>{2, 1});  // pixels 10, 11 = positions 2, 3 of 10 01 10 01
    // A raw byte writer that changes a colour without telling: the tile rule again.
    b.row(1)[0] = 0x22;
    CHECK(b.cgaPixel(0, 1) == 1);  // b of 10 01 10 01 on an odd row
    // ... and noteDrawn() resets a rectangle written with the same colour.
    b.row(1)[1] = 0xEE;
    b.noteDrawn(1, 1, 1, 1);
    CHECK(cgaRow(b, 1, 2, 2) == std::vector<int>{2, 0});  // b a d c at positions 2, 3: d, c = 2, 0
    // clear() forgets the plane.
    b.fillRect(0, 0, 0x9F, 0xC7, 0xEE);
    b.clear(0xEE);
    CHECK(b.cgaPixel(0, 1) == 0);
}

TEST_CASE("DOS CGA plane: sprites alternate from their first drawn row, save-unders keep whole CGA bytes") {
    BackBuffer b;
    b.enableCga(testDither());
    b.clear(0xFF);
    // A 2x3 frame of colour 14 drawn at an odd row: its first row takes the even tables.
    const PakFile pak = PakFile::load(tinyPak({{2, 3, 0, 0, std::vector<uint8_t>(6, 0xEE)}}), "T.PAK");
    drawSprite(b, pak.frame(0), 2, 5, sprite_flags::kNoHotspot);
    CHECK(cgaRow(b, 5, 4, 4) == std::vector<int>{2, 0, 0, 2});  // row 5 (odd): a b c d
    CHECK(cgaRow(b, 6, 4, 4) == std::vector<int>{0, 2, 2, 0});  // row 6 (even): b a d c
    // Clipped at the top: the first drawn row, not the frame's first row, takes the even tables.
    b.setClip({0, 0x9F, 9, 0xC7});
    drawSprite(b, pak.frame(0), 6, 8, sprite_flags::kNoHotspot);
    CHECK(cgaRow(b, 9, 12, 4) == std::vector<int>{2, 0, 0, 2});
    b.setClip(kFullScreenClip);
    // Masked: colour 0 keeps the pixel below.
    const PakFile holes = PakFile::load(tinyPak({{1, 1, 0, 0, {0xE0}}}), "H.PAK");
    b.fillRect(20, 20, 20, 20, 0x33);
    drawSprite(b, holes.frame(0), 20, 20, sprite_flags::kNoHotspot | sprite_flags::kMasked);
    CHECK(cgaRow(b, 20, 40, 2) == std::vector<int>{2, 1});  // 14 at position 0, colour 3's b kept
    // Sprite_SaveUnder at an odd byte saves whole CGA bytes (x >> 1, ceil((w + 1) / 2)): restoring
    // brings back pixels 4 * (x >> 1) .. as they were.
    b.fillRect(30, 30, 35, 31, 0x33);
    const auto save = saveUnder(b, 31, 30, 3, 2);
    REQUIRE(save);
    CHECK(save->cgaX == 60);
    CHECK(save->cgaWidth == 8);
    b.fillRect(30, 30, 35, 31, 0xEE);
    drawSprite(b, save->image(), 0, 0, sprite_flags::kCgaNormal);
    CHECK(b.byteAt(30, 30) == 0xEE);  // the 4-bpp picture is restored for its own bytes only
    CHECK(b.byteAt(31, 30) == 0x33);
    CHECK(cgaRow(b, 30, 60, 8) == std::vector<int>{0, 1, 2, 3, 0, 1, 2, 3});
    CHECK(cgaRow(b, 30, 68, 4) == std::vector<int>{2, 0, 0, 2});
}

TEST_CASE("DOS CGA plane: Gfx_ReplaceColorRect compares with pixel a and writes pixel b") {
    BackBuffer b;
    b.enableCga(testDither());
    b.fillRect(0, 0, 0x9F, 1, 0xCC);  // white
    b.fillRect(4, 0, 5, 0, 0x22);     // pixels 8..11: 10 01 10 01
    // White -> colour 3: every 11 becomes b of 00 01 10 11 = 01, anywhere.
    b.replaceColor(0, 0, 7, 0, 0xCCCC, 0x3333);
    CHECK(cgaRow(b, 0, 0, 4) == std::vector<int>{1, 1, 1, 1});
    CHECK(cgaRow(b, 0, 8, 4) == std::vector<int>{2, 1, 2, 1});  // other colours unchanged
    CHECK(b.pixel(0, 0) == 3);
    // Colour 2 -> white compares with a of 10 01 10 01 = 10: the 01 pixels of colour 2 stay (the
    // 4-bpp replace changes both, so the plane holds the original's value for the new colour).
    b.replaceColor(4, 0, 5, 0, 0x2222, 0xCCCC);
    CHECK(b.pixel(9, 0) == 12);
    CHECK(cgaRow(b, 0, 8, 4) == std::vector<int>{3, 1, 3, 1});
    // An odd first byte (1000:181b): the compare runs one pixel ahead. From black (a = 00) to white on
    // black pixels, bytes 1..4 (pixels 2..9): the zero pixels match and set the NEXT position, so the
    // CGA bytes become 11 00 00 11, 11 11 11 11, 00 11 11 00 (worked through the listing by hand).
    b.fillRect(0, 1, 0x9F, 1, 0xFF);
    b.replaceColor(1, 1, 4, 1, 0xFFFF, 0xCCCC);
    CHECK(cgaRow(b, 1, 0, 12) == std::vector<int>{3, 0, 0, 3, 3, 3, 3, 3, 0, 3, 3, 0});
}

TEST_CASE("DOS CGA plane: the font turns its colour by one pixel per glyph row") {
    // One glyph, 8 pixels wide, 3 rows of set bits.
    const std::vector<uint8_t> block = {7, 0, 3, 8, 0, 0, 0xFF, 0xFF, 0xFF};
    const std::vector<uint16_t> bpr(256, 1);
    const DosFont font = DosFont::fromBlock(block, bpr);
    BackBuffer b;
    b.enableCga(testDither());
    font.drawChar(b, 0xEEEE, 1, 10, 0);  // an odd x: pixels 2..9
    CHECK(cgaRow(b, 10, 2, 8) == std::vector<int>{0, 2, 2, 0, 0, 2, 2, 0});  // row 0: a b c d by position
    CHECK(cgaRow(b, 11, 2, 8) == std::vector<int>{0, 0, 2, 2, 0, 0, 2, 2});  // row 1: d a b c
    CHECK(cgaRow(b, 12, 2, 8) == std::vector<int>{2, 0, 0, 2, 2, 0, 0, 2});  // row 2: c d a b
    // A glyph past the right edge: the 4-bpp buffer continues on the next row, the CGA one in the
    // unseen half of its row, so the next row keeps its CGA pixels.
    b.fillRect(0, 21, 3, 21, 0x33);
    font.drawChar(b, 0xEEEE, 0x9E, 20, 0);
    CHECK(b.pixel(0, 21) == 14);
    CHECK(cgaRow(b, 21, 0, 4) == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("DOS CGA plane: scrolls, presents and the RGBA conversion carry it") {
    BackBuffer back, screen;
    back.enableCga(testDither());
    screen.enableCga(testDither());
    back.fillRect(6, 17, 9, 17, 0xEE);  // pixels 12..19 of row 17 (odd): a b c d
    back.scrollPlayfieldLeft(16, 0x8F, false);
    CHECK(cgaRow(back, 17, 4, 4) == std::vector<int>{2, 0, 0, 2});  // moved 8 pixels, still the fill's phase
    back.setColor(1);
    back.fillRect(0, 0, 3, 0, 0x22);
    back.setColor(0);
    presentDirty(back, screen);
    CHECK(cgaRow(screen, 0, 0, 4) == std::vector<int>{2, 1, 2, 1});
    presentFull(back, screen);
    CHECK(cgaRow(screen, 17, 4, 4) == std::vector<int>{2, 0, 0, 2});
    LookPalette pal;
    pal.look = Look::Cga;
    pal.cgaDither = testDither();
    const auto rgba = toRgba32(screen, pal);
    CHECK(rgba[17 * 320 + 4] == 0xFFFF55FFu);  // light magenta
    CHECK(rgba[17 * 320 + 5] == 0xFF000000u);  // black
    // CgaBinding turns the plane on for the CGA look only.
    BackBuffer a, c;
    pal.look = Look::Ega;
    const CgaBinding ega(a, c, pal);
    CHECK(!a.cgaEnabled());
    pal.look = Look::Cga;
    const CgaBinding cga(a, c, pal);
    CHECK(a.cgaEnabled());
    CHECK(c.cgaEnabled());
}

TEST_CASE("DOS back buffer: dirty rectangles use row pairs and even byte bounds") {
    DirtyRegions d;
    CHECK(d.empty());
    d.addRect(3, 1, 2, 4);  // x 3 (odd: width + 1, then even) -> bytes 2..7; rows 1..2 -> pairs 0 and 1
    REQUIRE(d.spans(0).size() == 1);
    CHECK(d.spans(0)[0] == std::pair{2, 8});
    CHECK(d.spans(1)[0] == std::pair{2, 8});
    CHECK(d.spans(2).empty());
    CHECK(d.isDirty(7, 3));
    CHECK(!d.isDirty(8, 3));
    d.addRect(8, 0, 1, 2);  // touching spans merge
    CHECK(d.spans(0) == std::vector<std::pair<int, int>>{{2, 10}});
    d.clear();
    CHECK(d.empty());

    BackBuffer back, screen;
    back.setColor(1);
    back.fillRect(10, 10, 11, 10, 0x44);  // recorded: row pair 5, bytes 10..11
    back.setColor(0);
    back.fillRect(20, 20, 21, 20, 0x55);  // not recorded
    presentDirty(back, screen);
    CHECK(screen.byteAt(10, 10) == 0x44);
    CHECK(screen.byteAt(20, 20) == 0x00);
    CHECK(back.dirty().empty());
    presentFull(back, screen);
    CHECK(screen.byteAt(20, 20) == 0x55);
}

TEST_CASE("DOS back buffer: play-field scrolls of segment 1000") {
    auto patternX = [] {
        BackBuffer b;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 160; ++x) b.row(y)[x] = static_cast<uint8_t>(x);
        return b;
    };
    BackBuffer b = patternX();
    b.scrollPlayfieldLeft(16, 17, false);
    CHECK(b.byteAt(2, 16) == 6);
    CHECK(b.byteAt(0x99, 17) == 0x9D);
    CHECK(b.byteAt(0x9A, 16) == 0x9A);  // the uncovered column is left as it was
    CHECK(b.byteAt(2, 18) == 2);
    b = patternX();
    b.scrollPlayfieldLeft(16, 16, true);
    CHECK(b.byteAt(2, 16) == 0x0A);
    CHECK(b.byteAt(0x95, 16) == 0x9D);
    b = patternX();
    b.scrollPlayfieldRight(16, 16, false);
    CHECK(b.byteAt(6, 16) == 2);
    CHECK(b.byteAt(0x9D, 16) == 0x99);
    CHECK(b.byteAt(5, 16) == 5);
    b = patternX();
    b.scrollPlayfieldRight(16, 16, true);
    CHECK(b.byteAt(6, 16) == 0x9E);  // from the end of the row above
    CHECK(b.byteAt(7, 16) == 0x9F);
    CHECK(b.byteAt(8, 16) == 0);
    CHECK(b.byteAt(0x9D, 16) == 0x95);

    BackBuffer v;
    for (int y = 0; y < 200; ++y) std::fill_n(v.row(y), 160, static_cast<uint8_t>(y));
    BackBuffer up = v;
    up.scrollPlayfieldUp(16, 31, false);
    CHECK(up.byteAt(2, 16) == 24);
    CHECK(up.byteAt(0x9D, 23) == 31);
    CHECK(up.byteAt(2, 24) == 24);   // the uncovered rows stay
    CHECK(up.byteAt(1, 16) == 16);   // bytes 0, 1 and 0x9E, 0x9F are not moved
    CHECK(up.byteAt(0x9E, 16) == 16);
    BackBuffer down = v;
    down.setColor(1);
    down.scrollPlayfieldDown(16, 31, false);
    CHECK(down.byteAt(2, 31) == 23);
    CHECK(down.byteAt(2, 24) == 16);
    CHECK(down.byteAt(2, 23) == 23);
    CHECK(down.dirty().spans(8) == std::vector<std::pair<int, int>>{{0, 160}});  // full width (1000:1af6)
    BackBuffer up2 = v;
    up2.scrollPlayfieldUp(16, 47, true);
    CHECK(up2.byteAt(2, 16) == 32);
}

TEST_CASE("DOS back buffer: Tile_DrawColumn walks down the map and stops after the rows asked for") {
    // Map entries 0 (tile 0 = all 0x11) and 2 (tile 1 = all 0x22); one picture 1 wide, 2 high.
    std::vector<uint8_t> f;
    put16(f, 4), put16(f, 1), put16(f, 2), put16(f, 6);
    put16(f, 0x0000), put16(f, 0x0002);
    f.insert(f.end(), {0xA5, 32, 0x11, 0xA5, 32, 0x22});
    const PanFile p = PanFile::load(f, "COL.PAN");
    BackBuffer b;
    b.setColor(1);
    b.drawTileColumn(4, 10, 10 + 12, p, 0);
    CHECK(b.byteAt(4, 10) == 0x11);
    CHECK(b.byteAt(7, 17) == 0x11);
    CHECK(b.byteAt(4, 18) == 0x22);
    CHECK(b.byteAt(7, 22) == 0x22);
    CHECK(b.byteAt(4, 23) == 0x00);  // 13 rows only
    CHECK(b.byteAt(8, 10) == 0x00);
    CHECK(b.dirty().isDirty(4, 10));
}

// ----------------------------------------------------------------------------------- blitter

TEST_CASE("DOS blitter: hotspot, masking, mirror placement, no-hotspot, clipping") {
    const PakFile k = testPak();
    const PakFrame& f = k.frame(0);
    using namespace sprite_flags;
    BackBuffer b;
    b.clear(0x77);
    auto area = drawSprite(b, f, 10, 20, kOpaque);
    REQUIRE(area);
    CHECK(area->x == 9);
    CHECK(area->y == 18);
    CHECK(b.byteAt(9, 18) == 0x10);  // opaque: colour 0 drawn
    CHECK(b.byteAt(10, 19) == 0x30);
    b.clear(0x77);
    drawSprite(b, f, 10, 20, kMasked);
    CHECK(b.byteAt(9, 18) == 0x17);
    CHECK(b.byteAt(10, 18) == 0x72);
    CHECK(b.byteAt(9, 19) == 0x37);
    b.clear(0x77);
    area = drawSprite(b, f, 10, 20, kMasked | kMirror);  // x - (width + x offset) = 9; rows reversed
    CHECK(area->x == 9);
    CHECK(b.byteAt(9, 18) == 0x27);
    CHECK(b.byteAt(10, 18) == 0x71);
    b.clear(0x77);
    drawSprite(b, f, 10, 20, kOpaque | kNoHotspot);
    CHECK(b.byteAt(10, 20) == 0x10);
    b.clear(0x77);
    drawSprite(b, f, 10, 20, kReverse);  // reversed rows, placed like a normal draw
    CHECK(b.byteAt(9, 18) == 0x20);
    CHECK(b.byteAt(10, 18) == 0x01);

    b.clear(0x77);
    b.setClip({10, 0x9F, 19, 0xC7});
    area = drawSprite(b, f, 10, 20, kOpaque);  // left byte and top row clipped
    REQUIRE(area);
    CHECK(area->x == 10);
    CHECK(area->y == 19);
    CHECK(area->widthBytes == 1);
    CHECK(area->height == 1);
    CHECK(b.byteAt(10, 19) == 0x30);
    CHECK(b.byteAt(9, 19) == 0x77);
    b.clear(0x77);
    drawSprite(b, f, 10, 20, kOpaque | kMirror);  // mirrored: the clipped left byte is the source's last
    CHECK(b.byteAt(10, 18) == 0x77);
    CHECK(b.byteAt(10, 19) == 0x03);
    b.setClip({0, 9, 0, 0xC7});
    b.clear(0x77);
    area = drawSprite(b, f, 10, 20, kOpaque);  // right clip
    CHECK(area->widthBytes == 1);
    CHECK(b.byteAt(9, 18) == 0x10);
    CHECK(b.byteAt(10, 18) == 0x77);
    CHECK(!drawSprite(b, f, 40, 20, kOpaque));  // clipped away
    b.setClip(kFullScreenClip);

    b.clear(0x77);
    b.setColor(1);
    drawSprite(b, f, 10, 20, kMasked);
    CHECK(b.dirty().isDirty(9, 18));
    CHECK(b.dirty().isDirty(10, 19));
}

TEST_CASE("DOS blitter: save-under is the clipped rectangle and restores it") {
    BackBuffer b;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 160; ++x) b.row(y)[x] = static_cast<uint8_t>(x ^ y);
    const BackBuffer before = b;
    b.setClip(kPlayfieldClip);
    const auto save = saveUnder(b, 0, 14, 4, 4);  // clipped to x 2.., y 16..
    REQUIRE(save);
    CHECK(save->x == 2);
    CHECK(save->y == 16);
    CHECK(save->widthBytes == 2);
    CHECK(save->height == 2);
    CHECK(save->heapBytes() == 16 + 2 * 3);
    b.fillRect(0, 0, 0x9F, 0xC7, 0);
    b.setClip(kFullScreenClip);
    drawSprite(b, save->image(), 0, 0, sprite_flags::kCgaNormal);
    CHECK(b.byteAt(2, 16) == before.byteAt(2, 16));
    CHECK(b.byteAt(3, 17) == before.byteAt(3, 17));
    CHECK(b.byteAt(4, 16) == 0);
    CHECK(!saveUnder(b, 200, 0, 4, 4));
}

TEST_CASE("DOS actors: descending z, ties to the higher record, children, pointer clip, erase") {
    // Frames 0..3: solid 2x2-byte squares of colours 1..4 (frames 0..2 are "pointer" frames, codes < 6).
    std::vector<TinyFrame> frames;
    for (uint8_t c = 1; c <= 4; ++c) frames.push_back({2, 2, 0, 0, std::vector<uint8_t>(4, static_cast<uint8_t>(c * 0x11))});
    const PakFile pak = PakFile::load(tinyPak(frames), "ACTORS.PAK");

    BackBuffer b;
    b.clear(0x55);
    const BackBuffer clean = b;
    SpriteRenderer r;
    auto set = [&](int i, int x, int y, int z, uint16_t frame, int owner) {
        r.records[static_cast<size_t>(i)].x = static_cast<int16_t>(x);
        r.records[static_cast<size_t>(i)].y = static_cast<int16_t>(y);
        r.records[static_cast<size_t>(i)].z = static_cast<int16_t>(z);
        r.records[static_cast<size_t>(i)].frame = frame;
        r.records[static_cast<size_t>(i)].owner = static_cast<uint16_t>(owner);
    };
    set(3, 20, 40, 5, 6, 3);   // frame 3 (colour 4), z 5
    set(4, 20, 40, 5, 6, 4);   // same z, higher index: drawn first
    set(5, 20, 40, 9, 6, 5);   // deeper: drawn before both
    set(6, 4, 2, 0, 6, 5);     // child of 5: at (24, 42)
    r.records[3].frame = 6;
    r.records[4].frame = 7;    // frame 3 mirrored (same square)
    CHECK(r.drawSorted(b, pak, 0, 13, false, false) == 0);  // nothing changed, nothing dirty
    const int drawn = r.drawSorted(b, pak, 0, 13, false, true);
    CHECK(drawn == 4);
    CHECK(r.drawOrder[0] == 5);
    CHECK(r.drawOrder[1] == 4);
    CHECK(r.drawOrder[2] == 3);
    CHECK(b.byteAt(24, 42) == 0x44);  // the child
    CHECK(b.clip() == kFullScreenClip);
    CHECK(r.saveHeapHigh == 4 * 22);  // four 2x2 saves of 16 + 2*3 bytes after one reset
    r.eraseAll(b, 0, 13);
    CHECK(b.bytes()[0] == clean.bytes()[0]);
    CHECK(std::equal(b.bytes().begin(), b.bytes().end(), clean.bytes().begin()));
    CHECK(r.saveHeapReset);
    for (const auto& rec : r.records) CHECK((rec.owner & 0x8000) == 0);

    // A pointer frame (code < 6) opens the clip to the full screen for the rest of the pass.
    SpriteRenderer p;
    p.records[0] = {100, 2, 9, 2, 0, {}};   // frame 1 at y 2 (outside the play field), drawn first (deepest)
    p.records[1] = {100, 4, 1, 6, 1, {}};   // frame 3 at y 4: drawn too, the clip stays open
    p.records[2] = {100, 6, -1, 6, 2, {}};  // negative z: never picked; the previous pick is drawn again
    b.clear(0x55);
    CHECK(p.drawSorted(b, pak, 0, 3, false, true) == 3);
    CHECK(p.drawOrder[2] == 1);
    CHECK(b.byteAt(100, 2) == 0x22);
    CHECK(b.byteAt(100, 5) == 0x44);
    CHECK(b.byteAt(100, 7) == 0x55);
    SpriteRenderer q;
    q.records[1] = {100, 4, 1, 6, 1, {}};   // without the pointer: clipped by the play field
    b.clear(0x55);
    CHECK(q.drawSorted(b, pak, 0, 13, false, true) == 0);
    for (int i = 0; i < SpriteRenderer::kRecords; ++i)  // Sprites_Snapshot rewrites the owners (and the drawn marks)
        q.records[static_cast<size_t>(i)].owner = static_cast<uint16_t>(i);
    CHECK(q.drawSorted(b, pak, 0, 13, true, true) == 1);  // title mode: full screen
}

// ----------------------------------------------------------------------------------- scenery

TEST_CASE("DOS scenery: camera latch of Frame_Begin") {
    const auto c = SceneryCamera::at(0xC0, 0);
    CHECK(c.blockColumn == 1);
    CHECK(c.columnOffset == 0x100);
    CHECK(c.rowOffset == 0);
    CHECK(c.blockRowBase == 0);
    const auto d = SceneryCamera::at(0x70F, 0x7A5);
    CHECK(d.blockColumn == 0x0E);
    CHECK(d.columnOffset == 0x020);  // low 3 bits ignored
    CHECK(d.blockRowBase == 0xF0);
    CHECK(d.rowOffset == 0x08);
    const auto n = SceneryCamera::at(0xFFF8, 0x100);  // negative x: column 15 of the row above
    CHECK(n.blockColumn == 0x0F);
    CHECK(n.blockRowBase == 0x10);
    CHECK(n.columnOffset == 0x1E0);
    std::vector<uint16_t> grid(256, 0);
    grid[0x21] = 0x83;  // bits 0..6 only
    CHECK(blockMapWord(grid, 0x21, 0x1FE) == 3u * 256u + 0xFFu);
    CHECK(blockMapWord(grid, 300, 4) == 2u);
}

TEST_CASE("DOS scenery: incremental scrolling equals a full redraw (fast CPU)") {
    TM_REQUIRE_DOS_DATA();
    const auto files = DosGfxFiles::open(*testdata::dosDataPath());
    const PanFile& pan = files.pan("MOUNTAIN");
    const auto grid = initialCellPictures(files.exe());
    auto full = [&](uint16_t x, uint16_t y) {
        BackBuffer b;
        drawPlayfield(b, pan, grid, SceneryCamera::at(x, y));
        return b;
    };
    for (const auto& [x0, y0, dx, dy] : {std::array{0x1F8, 0x0F0, 8, 0}, std::array{0x200, 0x0F8, -8, 0},
                                         std::array{0x3C0, 0x0F8, 0, 8}, std::array{0x3C0, 0x100, 0, -8}}) {
        CAPTURE(x0);
        CAPTURE(y0);
        BackBuffer b = full(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0));
        const auto cam = SceneryCamera::at(static_cast<uint16_t>(x0 + dx), static_cast<uint16_t>(y0 + dy));
        if (dx) scrollColumns(b, pan, grid, cam, dx, false, false);
        else scrollRows(b, pan, grid, cam, dy, false);
        const BackBuffer want = full(static_cast<uint16_t>(x0 + dx), static_cast<uint16_t>(y0 + dy));
        bool same = true;
        for (int y = 0x10; y <= 0x8F; ++y)
            for (int x = 2; x <= 0x9D; ++x) same = same && b.byteAt(x, y) == want.byteAt(x, y);
        CHECK(same);
    }
}

// ----------------------------------------------------------------------------------- screens

TEST_CASE("DOS screens: the composed reference screens equal the verified renders") {
    TM_REQUIRE_DOS_DATA();
    const auto files = DosGfxFiles::open(*testdata::dosDataPath());
    // Title (with the sparkle and pointer of each capture), sign-in (the shipped USERS.DAT, ega/004)
    // and the new-game clubhouse (ega/008, tandy/21).
    CHECK(hashLogical(composeScreen(files, defaultScreenSpec(DosScreen::Title, Look::Ega))) == 0xbb32d331665e00d2ull);
    CHECK(hashLogical(composeScreen(files, defaultScreenSpec(DosScreen::Title, Look::Tandy))) == 0x739e943c49593068ull);
    CHECK(hashLogical(composeScreen(files, defaultScreenSpec(DosScreen::Title, Look::Mcga))) == 0xc03fe1bd13472f59ull);
    CHECK(hashLogical(composeScreen(files, defaultScreenSpec(DosScreen::Title, Look::Cga))) == 0x2592d89bc038a1ffull);
    CHECK(hashLogical(composeScreen(files, defaultScreenSpec(DosScreen::Clubhouse, Look::Ega))) == 0xd3a186da87e0bb79ull);
    // The shipped USERS.DAT may have been changed by playing; the sign-in hash is only valid for it.
    if (const auto users = mischief::readWholeFile(*testdata::dosDataPath() / "USERS.DAT"); users && mischief::fnv1a64(*users) == 0x751da8416dfb5ed3ull)
        CHECK(hashLogical(composeScreen(files, defaultScreenSpec(DosScreen::SignIn, Look::Ega))) == 0x3575c75c7ae54d23ull);

    // The play screens need the saved game of the captures: a USERS.DAT whose record 1 holds the
    // block numbers of its hiding places and variable cells (layout values, not art).
    static constexpr uint8_t kSavedPictures[57] = {
        11, 25, 8,  17, 26, 21, 23, 15, 5,  29, 14, 11, 25, 26, 12, 38, 22, 7,  2,  19, 10, 5,  18, 31, 32, 1,  13, 26, 12,
        23, 1,  6,  40, 49, 8,  50, 12, 10, 5,  15, 14, 26, 3,  18, 20, 16, 25, 44, 75, 38, 47, 38, 56, 47, 71, 71, 76};
    std::vector<uint8_t> users(3 + 9 * 0x168, 0);
    users[3 + 0x168] = 'X';
    std::copy(std::begin(kSavedPictures), std::end(kSavedPictures), users.begin() + 3 + 0x168 + 0xBB);
    const auto dir = std::filesystem::temp_directory_path() / "tmport_test_dos_gfx";
    const auto path = dir / "USERS.DAT";
    REQUIRE(mischief::writeWholeFile(path, users));
    struct Ref {
        DosScreen screen;
        uint64_t hash;
    };
    for (const Ref& r : {Ref{DosScreen::Quiz, 0x3b54a8efb7bb58acull}, Ref{DosScreen::Path, 0x741fc564083932b4ull},
                         Ref{DosScreen::Castle, 0xe1514f210009554cull}}) {
        CAPTURE(dosScreenName(r.screen));
        DosScreenSpec spec = defaultScreenSpec(r.screen, Look::Ega);
        spec.usersFile = path;
        CHECK(hashLogical(composeScreen(files, spec)) == r.hash);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
