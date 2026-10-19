// Data-free tests of the mod layer's formats (docs/MODDING.md): JSON, zlib/PNG, PMAP
// quantisation, WAV conversion, the text escape syntax, the DAM assembler, mod folders.
// Everything here is generated test content; no game data is used.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "gfx/palette.h"
#include "gfx/pixmap.h"
#include "mods/dam_asm.h"
#include "mods/json.h"
#include "mods/mod_common.h"
#include "mods/mod_folder.h"
#include "mods/mods.h"
#include "mods/pmap_codec.h"
#include "mods/png_codec.h"
#include "mods/text_codec.h"
#include "mods/wav_codec.h"
#include "res/ssnd.h"
#include "save/settings.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::mods;

namespace {

std::vector<uint8_t> bytesOf(std::string_view s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::vector<uint8_t> fromHex(std::string_view h) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back(static_cast<uint8_t>(std::stoi(std::string(h.substr(i, 2)), nullptr, 16)));
    return v;
}

// A synthetic 256-colour palette: a colour ramp with the colour key (0,0,170) at 15 and 241 and
// white duplicated at 0 and 77 (the shape, not the content, of the game's palette).
gfx::Palette testPalette() {
    std::vector<uint8_t> rrgb(1024, 0);
    for (int i = 0; i < 256; ++i) {
        rrgb[static_cast<size_t>(i) * 3] = static_cast<uint8_t>((i * 37) & 0xFF);
        rrgb[static_cast<size_t>(i) * 3 + 1] = static_cast<uint8_t>((i * 91) & 0xFF);
        rrgb[static_cast<size_t>(i) * 3 + 2] = static_cast<uint8_t>((i * 53) & 0xFF);
    }
    auto set = [&](size_t i, uint8_t r, uint8_t g, uint8_t b) {
        rrgb[i * 3] = r;
        rrgb[i * 3 + 1] = g;
        rrgb[i * 3 + 2] = b;
    };
    set(15, 0, 0, 0xAA);
    set(241, 0, 0, 0xAA);
    set(0, 255, 255, 255);
    set(77, 255, 255, 255);
    set(1, 0, 0, 0);
    set(2, 255, 0, 0);
    set(3, 0, 255, 0);
    set(4, 0, 0, 255);
    set(5, 0, 0, 0xA0);  // close to the key colour but not it
    return gfx::Palette::fromRrgb(rrgb);
}

// --- Test PNG encoder, independent of the engine's: stored deflate blocks, filter type cycling
// per row (0..4), optional Adam7 interlacing, any colour type / bit depth.
void putBe(std::vector<uint8_t>& v, uint32_t x) {
    for (int k = 3; k >= 0; --k) v.push_back(static_cast<uint8_t>(x >> (8 * k)));
}
void chunk(std::vector<uint8_t>& out, const char* t, const std::vector<uint8_t>& d) {
    putBe(out, static_cast<uint32_t>(d.size()));
    const size_t s = out.size();
    out.insert(out.end(), t, t + 4);
    out.insert(out.end(), d.begin(), d.end());
    putBe(out, png::crc32(std::span<const uint8_t>(out).subspan(s)));
}
std::vector<uint8_t> storedZlib(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t p = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - p);
        z.push_back(p + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(p), raw.begin() + static_cast<std::ptrdiff_t>(p + n));
        p += n;
    } while (p < raw.size());
    putBe(z, png::adler32(raw));
    return z;
}
int paethP(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
std::vector<uint8_t> buildPng(int w, int h, int colorType, int depth, const std::vector<uint16_t>& samples, bool interlace,
                              const std::vector<Rgb8>& plte = {}, const std::vector<uint8_t>& trns = {}) {
    const int ch = colorType == 0 ? 1 : colorType == 2 ? 3 : colorType == 3 ? 1 : colorType == 4 ? 2 : 4;
    const size_t bpp = static_cast<size_t>(std::max(1, ch * depth / 8));
    struct P {
        int x0, y0, dx, dy;
    };
    const std::vector<P> passes = interlace ? std::vector<P>{{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                                             {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}}
                                            : std::vector<P>{{0, 0, 1, 1}};
    std::vector<uint8_t> raw;
    int rowNo = 0;
    for (const P& p : passes) {
        const int pw = (w - p.x0 + p.dx - 1) / p.dx, ph = (h - p.y0 + p.dy - 1) / p.dy;
        if (pw <= 0 || ph <= 0) continue;
        const size_t rb = (static_cast<size_t>(pw) * static_cast<size_t>(ch * depth) + 7) / 8;
        std::vector<uint8_t> prior(rb, 0);
        for (int y = 0; y < ph; ++y) {
            std::vector<uint8_t> row(rb, 0);
            for (int x = 0; x < pw; ++x)
                for (int c = 0; c < ch; ++c) {
                    const size_t si = (static_cast<size_t>(p.y0 + y * p.dy) * static_cast<size_t>(w) + static_cast<size_t>(p.x0 + x * p.dx)) *
                                          static_cast<size_t>(ch) + static_cast<size_t>(c);
                    const uint16_t v = samples[si];
                    const size_t xi = static_cast<size_t>(x) * static_cast<size_t>(ch) + static_cast<size_t>(c);
                    if (depth == 16) {
                        row[xi * 2] = static_cast<uint8_t>(v >> 8);
                        row[xi * 2 + 1] = static_cast<uint8_t>(v);
                    } else if (depth == 8) {
                        row[xi] = static_cast<uint8_t>(v);
                    } else {
                        const size_t bit = static_cast<size_t>(x) * static_cast<size_t>(depth);
                        row[bit / 8] |= static_cast<uint8_t>(v << (8 - depth - static_cast<int>(bit % 8)));
                    }
                }
            const int f = rowNo++ % 5;
            raw.push_back(static_cast<uint8_t>(f));
            for (size_t i = 0; i < rb; ++i) {
                const int a = i >= bpp ? row[i - bpp] : 0, b = prior[i], c = i >= bpp ? prior[i - bpp] : 0;
                int v = row[i];
                if (f == 1) v -= a;
                if (f == 2) v -= b;
                if (f == 3) v -= (a + b) / 2;
                if (f == 4) v -= paethP(a, b, c);
                raw.push_back(static_cast<uint8_t>(v));
            }
            prior = row;
        }
    }
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    putBe(ihdr, static_cast<uint32_t>(w));
    putBe(ihdr, static_cast<uint32_t>(h));
    ihdr.insert(ihdr.end(), {static_cast<uint8_t>(depth), static_cast<uint8_t>(colorType), 0, 0, static_cast<uint8_t>(interlace ? 1 : 0)});
    chunk(out, "IHDR", ihdr);
    if (!plte.empty()) {
        std::vector<uint8_t> pl;
        for (auto c : plte) pl.insert(pl.end(), {c.r, c.g, c.b});
        chunk(out, "PLTE", pl);
    }
    if (!trns.empty()) chunk(out, "tRNS", trns);
    chunk(out, "tEXt", bytesOf(std::string("Comment") + '\0' + "ancillary chunk, ignored"));
    chunk(out, "IDAT", storedZlib(raw));
    chunk(out, "IEND", {});
    return out;
}

struct TempDir {
    std::filesystem::path p;
    explicit TempDir(const char* name) {
        std::random_device rd;
        p = std::filesystem::temp_directory_path() / (std::string(name) + "_" + std::to_string(rd()));
        std::filesystem::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
    }
};

void writeText(const std::filesystem::path& p, std::string_view s) { REQUIRE(writeWholeFile(p, bytesOf(s))); }

// A dynamic-Huffman zlib stream made by Python's zlib (level 9) from the LCG text of the test below.
const char* const kDynamicZlib =
    "78da4d968b75dd300c4357e16ae267ff154a5cd02f3d691adb92f80540d57b39dd99af2a665faadfe4bc7ec56feecf4c"
    "e46e88dd936f7ff7cb7b2f66df7a7764965ef763b60e45624767bdae87a73dcf1f742433f414fbb31b3a2b875d6bac7c"
    "a23612fbea7dcbc0448676e1b278d45b29ba75ae505f6162a323a50da63625b6d60c09bc92c15d5f9749164a4e31b4e2"
    "20da21a7273fb336f4f3be5486cf04e790e55b01af110e0727d75987be2419b3dc8f4ae4acfd6023fef4fc9ce32389e1"
    "98c2507bf8138af64ad79beb665bf6f28855dfb762af691bc572a9c9dc81f57db88cf550ee654da7136655bb655b5dd8"
    "0215a57aef33b387c7b5af77cd6d57067b13ad40dc329b1578e88e921d57bfc7d6d954f242017102fcf292547d85d0dd"
    "54e155d90f355678d3a612b4b473cfb57bf8fddff2d0d5eb6222c8a39ee14979c1212533dc373251c0802445ca0a9c71"
    "f0f288b1e742f8258780077a060c2514727e915e3d09980136013c80b275a7dbca742e6f18017cb1fcd5183f43990100"
    "6ceaffa8a544a20c204a0a56de21b008f1b9f34147c2841b0abd0b011ec3855023f3832486b67afb3e5f479368f37d4c"
    "51bfe5aea5008b0185d2b2a9640cc70b1463e5b80a1fac477de873e316f1a0ac4f0f746ca6262edd311be2b1d1b5838d"
    "c255b0b85d683e96bd169c321af5af04d4b888a438791446565efd6281e056060524a80dceea418f0ae59c529df5be91"
    "0dd0c5a1a986c44a48afbbed52cb7d4a6d9c3c6748c9e87dd2660b8eb65aeaf2a3bb293c8bec6dfebecf87c563f298fd"
    "40b180ba612ea993f2d5587f24718538a9206d303627b72b615696d29ecba8e1150a5be74bec3c6954eedb9366a14e9a"
    "414e8af174aa3e14e589223c531dba3f4d3bc17ca7ba86b6db5fe3ec8c011ad8aa2ec22d80cf25ff9d8f773d3f06a37a"
    "919f07c8580ed867fac867514142e3d3760d95751288dcc7ea8db06e3c69e311de90c913a4ba6903910507d5e62138d6"
    "1035526f6eb261dbcc97ce633260cf93a60a6b625f137ff05d43f161d803163acd4163e593f8aafe0429c86fa0c7fb29"
    "80f17a5208ecbc9a162a71ed6c508862367d22012ee16a30213a4f3c35f3b82878685ac888c1729c96db1b0ece5c8518"
    "986dc52a0f18cc4531214f973514caad8112f92e6cc80bd5dff7facd70c64b1e214f87bcbac4caaf4645bf98e7712320"
    "af4eb2d98c93a082775ff2ae31d2b60c624850ec1e8da40fa165513f346b68894efdc3607e971d8d31cadd8e57269699"
    "be302c1cae6298f258172d9a4f13265220b70fc4c6dd174085cd70f97a7f60522dad1490577a1a7d922c6ea8f29480a9"
    "a97a59b1e76e051cfa507bf741cf268befc243c93210f93388233128722e8be909db12bbf24019dd00b817c1792122a7"
    "f21b321f00de15f086e3dda06eb01e0ee4a3dcdebe813f9ea646c866c055d1343cc9b9b15f4788bb88243123c82074ee"
    "d2e9098650015f5593800533b4e006c6271830f2f0e32b16c345c5f807c1704daa";

}  // namespace

// ============================================================================================ JSON

TEST_CASE("mods/json: parse, write, round trip") {
    const auto v = json::parse(R"({"a": [1, -2, 3.5, true, false, null], "s": "x\"y\\\u00e9\ud83d\ude00", "o": {"k": 4294967295}})");
    REQUIRE(v.isObject());
    const auto* a = v.find("a");
    REQUIRE(a);
    CHECK(a->size() == 6);
    CHECK(a->asArray()[1].asInt() == -2);
    CHECK(a->asArray()[2].asDouble() == 3.5);
    CHECK_FALSE(a->asArray()[2].isInteger());
    CHECK(v.find("s")->asString() == "x\"y\\\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(v.find("o")->find("k")->asInt() == 4294967295ll);
    CHECK(json::parse(json::write(v)) == v);
    CHECK(json::parse(json::write(v, {0, 100})) == v);
    CHECK(json::parse("\xEF\xBB\xBF[1]").size() == 1);  // BOM
}

TEST_CASE("mods/json: errors name line and column") {
    try {
        json::parse("{\n  \"a\": 1,\n  \"b\" 2\n}");
        FAIL("no error");
    } catch (const json::ParseError& e) {
        CHECK(e.line() == 3);
        CHECK(std::string(e.what()).find("':'") != std::string::npos);
    }
    CHECK_THROWS_AS(json::parse("[1,]"), json::ParseError);
    CHECK_THROWS_AS(json::parse("{\"a\":1,\"a\":2}"), json::ParseError);  // duplicate key
    CHECK_THROWS_AS(json::parse("[1] 2"), json::ParseError);
    CHECK_THROWS_AS(json::parse("\"abc"), json::ParseError);
    CHECK_THROWS_AS(json::parse("[01x]"), json::ParseError);
}

// ==================================================================================== zlib / PNG

TEST_CASE("mods/png: inflate of deflate, stored and dynamic-Huffman streams") {
    std::mt19937 rng(7);
    for (size_t n : {size_t(0), size_t(1), size_t(100), size_t(70000), size_t(300000)}) {
        std::vector<uint8_t> d(n);
        for (size_t i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(i % 1000 < 500 ? rng() % 4 : rng());
        const auto z = deflateZlib(d);
        CHECK(inflateZlib(z) == d);
        CHECK(inflateZlib(storedZlib(d)) == d);
        if (n > 1000) CHECK(z.size() < n);
    }
    const std::vector<uint8_t> z = fromHex(kDynamicZlib);
    std::vector<uint8_t> want;
    uint32_t x = 12345;
    for (int i = 0; i < 3000; ++i) {
        x = (x * 1103515245u + 12345u) & 0x7FFFFFFFu;
        want.push_back(static_cast<uint8_t>("aaaaaaabbbbccd e"[(x >> 16) & 15]));
    }
    REQUIRE(((z[2] >> 1) & 3) == 2);  // BTYPE = dynamic
    CHECK(inflateZlib(z) == want);
    // Corruption is detected.
    auto bad = z;
    bad[bad.size() - 1] ^= 1;
    CHECK_THROWS_AS(inflateZlib(bad), FormatError);
    CHECK_THROWS_AS(inflateZlib(std::vector<uint8_t>(z.begin(), z.begin() + 100)), FormatError);
    CHECK_THROWS_AS(inflateZlib(z, 100), FormatError);  // larger than allowed
}

TEST_CASE("mods/png: decoder handles every colour type, bit depth, filter and interlacing") {
    const int w = 13, h = 11;
    const size_t np = static_cast<size_t>(w * h);
    for (bool il : {false, true}) {
        CAPTURE(il);
        // Grey 1/2/4/8/16 bit.
        for (int depth : {1, 2, 4, 8, 16}) {
            std::vector<uint16_t> s(np);
            const int maxv = (1 << depth) - 1;
            for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<uint16_t>((i * 7) % static_cast<size_t>(maxv + 1));
            const auto img = decodePng(buildPng(w, h, 0, depth, s, il));
            CHECK(img.width == w);
            size_t bad = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                const int want = depth == 16 ? s[i] >> 8 : s[i] * 255 / maxv;
                bad += img.rgba[i * 4] != want || img.rgba[i * 4 + 3] != 255;
            }
            CHECK_MESSAGE(bad == 0, "grey depth " << depth);
        }
        for (int depth : {8, 16}) {
            // RGB with a tRNS colour key.
            std::vector<uint16_t> s(np * 3);
            for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<uint16_t>(((i * 2654435761u) & 0xFFFFFFFFu) >> (depth == 8 ? 24 : 16));
            s[0] = s[1] = s[2] = static_cast<uint16_t>(depth == 8 ? 0x12 : 0x1234);
            const std::vector<uint8_t> trns = depth == 8 ? std::vector<uint8_t>{0, 0x12, 0, 0x12, 0, 0x12}
                                                         : std::vector<uint8_t>{0x12, 0x34, 0x12, 0x34, 0x12, 0x34};
            const auto img = decodePng(buildPng(w, h, 2, depth, s, il, {}, trns));
            CHECK(img.rgba[3] == 0);
            size_t bad = 0;
            for (size_t i = 1; i < np; ++i)
                bad += img.rgba[i * 4] != (depth == 16 ? s[i * 3] >> 8 : s[i * 3]) ||
                       img.rgba[i * 4 + 2] != (depth == 16 ? s[i * 3 + 2] >> 8 : s[i * 3 + 2]);
            CHECK_MESSAGE(bad == 0, "rgb depth " << depth);
            // RGBA and grey+alpha.
            std::vector<uint16_t> a(np * 4);
            for (size_t i = 0; i < a.size(); ++i) a[i] = static_cast<uint16_t>((i * 40503u) & (depth == 8 ? 0xFFu : 0xFFFFu));
            const auto rgba = decodePng(buildPng(w, h, 6, depth, a, il));
            bad = 0;
            for (size_t i = 0; i < a.size(); ++i) bad += rgba.rgba[i] != (depth == 16 ? a[i] >> 8 : a[i]);
            CHECK_MESSAGE(bad == 0, "rgba depth " << depth);
            const auto ga = decodePng(buildPng(w, h, 4, depth, std::vector<uint16_t>(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(np * 2)), il));
            CHECK(ga.rgba[3] == (depth == 16 ? a[1] >> 8 : a[1]));
            CHECK(ga.rgba[4] == (depth == 16 ? a[2] >> 8 : a[2]));
        }
        // Indexed 1/2/4/8 bit keeps the indices.
        for (int depth : {1, 2, 4, 8}) {
            const int n = 1 << depth;
            std::vector<Rgb8> pal;
            for (int i = 0; i < n; ++i) pal.push_back(Rgb8{static_cast<uint8_t>(i), 0, 0});
            std::vector<uint16_t> s(np);
            for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<uint16_t>((i * 5) % static_cast<size_t>(n));
            const auto img = decodePng(buildPng(w, h, 3, depth, s, il, pal, {0}));
            REQUIRE(img.indexed());
            CHECK(img.paletteAlpha[0] == 0);
            size_t bad = 0;
            for (size_t i = 0; i < s.size(); ++i) bad += img.indices[i] != s[i];
            CHECK_MESSAGE(bad == 0, "indexed depth " << depth);
        }
    }
}

TEST_CASE("mods/png: encoders round-trip; broken files are rejected") {
    std::vector<uint8_t> idx(40 * 30);
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<uint8_t>(i % 7 == 0 ? 241 : i % 251);
    std::vector<Rgb8> pal(256);
    for (size_t i = 0; i < 256; ++i) pal[i] = Rgb8{static_cast<uint8_t>(i), static_cast<uint8_t>(255 - i), 9};
    std::vector<uint8_t> alpha(256, 255);
    alpha[241] = 0;
    const auto img = decodePng(encodePngIndexed(40, 30, idx, pal, alpha));
    CHECK(img.indices == idx);
    CHECK(img.paletteAlpha[241] == 0);
    // The engine's uncompressed PNG writer is readable too.
    CHECK(decodePng(png::encodeIndexed(40, 30, idx, pal, alpha)).indices == idx);
    std::vector<uint8_t> rgba(16 * 8 * 4);
    for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<uint8_t>(i * 13);
    CHECK(decodePng(encodePngRgba(16, 8, rgba)).rgba == rgba);

    const auto f = encodePngRgba(16, 8, rgba);
    CHECK_THROWS_AS(decodePng(std::vector<uint8_t>(f.begin(), f.begin() + 7)), FormatError);
    auto crc = f;
    crc[20] ^= 0x40;  // inside IHDR
    CHECK_THROWS_AS(decodePng(crc), FormatError);
    CHECK_THROWS_AS(decodePng(bytesOf("GIF89a not a png")), FormatError);
}

// ================================================================================= PMAP from PNG

TEST_CASE("mods/pmap: indexed PNG with the game palette keeps every index (duplicates too)") {
    const gfx::Palette pal = testPalette();
    std::vector<uint8_t> idx(17 * 9);
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<uint8_t>(i * 3);
    idx[0] = 241;  // second key index
    idx[1] = 77;   // duplicate white
    gfx::PixMap pm;
    pm.width = 17;
    pm.height = 9;
    pm.pixels = idx;
    const auto img = pngToIndices(pixmapToEditablePng(pm, pal), pal);
    CHECK(img.direct);
    CHECK(img.indices == idx);
    CHECK(img.remapped == 0);
    // Encodings: RUND and raw 8 bpp decode back; the body is padded to 512 bytes.
    for (auto enc : {gfx::PixEncoding::Rund, gfx::PixEncoding::Raw8}) {
        const auto body = encodePmapBody(img, 8, enc);
        CHECK(body.size() % 512 == 0);
        const auto back = gfx::decodePmap(body, 8);
        CHECK(back.encoding == enc);
        CHECK(back.pixels == idx);
    }
}

TEST_CASE("mods/pmap: RGBA quantisation (key colour, alpha, nearest colour, never a key index)") {
    const gfx::Palette pal = testPalette();
    const std::vector<uint8_t> rgba = {
        0,   0,   0xAA, 255,  // key colour -> 15
        10,  20,  30,   0,    // alpha 0 -> 15
        255, 0,   0,    255,  // exact -> 2
        5,   250, 3,    200,  // nearest -> 3 (green)
        0,   0,   0xA8, 255,  // nearest is the key colour, which is excluded -> 5 (0,0,A0)
        250, 252, 255,  127,  // alpha < 128 -> key
    };
    const auto img = pngToIndices(encodePngRgba(6, 1, rgba), pal);
    CHECK_FALSE(img.direct);
    CHECK(img.indices == std::vector<uint8_t>{15, 15, 2, 3, 5, 15});
    CHECK(img.keyPixels == 3);
    CHECK(img.remapped == 2);
    // White maps to the first white entry (0), not the duplicate 77.
    CHECK(pngToIndices(encodePngRgba(1, 1, std::vector<uint8_t>{255, 255, 255, 255}), pal).indices[0] == 0);
    // An indexed PNG with a foreign palette is mapped by colour; alpha entries become the key.
    const std::vector<Rgb8> foreign = {{255, 0, 0}, {1, 254, 1}, {9, 9, 9}};
    const auto fi = pngToIndices(encodePngIndexed(3, 1, std::vector<uint8_t>{0, 1, 2}, foreign, std::vector<uint8_t>{255, 255, 0}), pal);
    CHECK_FALSE(fi.direct);
    CHECK(fi.indices == std::vector<uint8_t>{2, 3, 15});
    // 16-colour art: VGA palette, key nibble 3, packed 4 bpp with a zero pad nibble.
    const gfx::Palette vga = gfx::Palette::vga16();
    const auto v = pngToIndices(encodePngRgba(3, 1, std::vector<uint8_t>{0, 0, 0, 255, 0, 0, 0xAA, 255, 255, 255, 255, 255}), vga);
    CHECK(v.indices[1] == 3);
    const auto body = encodePmapBody(v, 4, gfx::PixEncoding::Raw4);
    CHECK(body[4] == ((v.indices[0] << 4) | 3));
    CHECK((body[5] & 0x0F) == 0);
    CHECK(gfx::decodePmap(body, 4).pixels == v.indices);
}

// ============================================================================================ WAV

TEST_CASE("mods/wav: 8-bit mono at the target rate is copied; SSND <-> WAV round trip") {
    std::vector<uint8_t> ssnd = {0, 0, 0, 0, 0x22, 0x56, 0, 0, 5, 0, 0, 0, 1, 2, 3, 250, 128};
    padToNe(ssnd);
    const auto wav = ssndToWav(ssnd);
    const auto f = parseWavFormat(wav);
    CHECK(f.sampleRate == 22050);
    CHECK(f.channels == 1);
    CHECK(f.bitsPerSample == 8);
    CHECK(f.frames == 5);
    CHECK(wav.size() % 2 == 0);  // odd data chunk + RIFF pad byte
    CHECK(wavToSsnd(wav, 22050) == ssnd);
}

TEST_CASE("mods/wav: 16-bit stereo 44.1 kHz, float, 24-bit -> 22050 Hz 8-bit mono") {
    // 1 s of a 441 Hz sine in both channels.
    std::vector<uint8_t> s;
    for (int i = 0; i < 44100; ++i) {
        const auto v = static_cast<int16_t>(std::lround(16000.0 * std::sin(2 * 3.14159265358979 * 441.0 * i / 44100.0)));
        for (int c = 0; c < 2; ++c) {
            s.push_back(static_cast<uint8_t>(v & 0xFF));
            s.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        }
    }
    std::string note;
    const auto body = wavToSsnd(makeWav(44100, 2, 16, s), 22050, &note);
    CHECK(note.find("44100 Hz 16-bit stereo") != std::string::npos);
    const auto info = parseSsnd(body);
    REQUIRE(info);
    CHECK(info->sampleRate == 22050);
    CHECK(info->sampleCount == 22050);
    CHECK(body.size() % 512 == 0);
    const auto pcm = info->pcm(body);
    int lo = 255, hi = 0;
    for (uint8_t b : pcm) {
        lo = std::min<int>(lo, b);
        hi = std::max<int>(hi, b);
    }
    CHECK(hi >= 186);  // 128 + 16000/256 = 190.5, slightly attenuated by the averaging
    CHECK(hi <= 192);
    CHECK(lo >= 64);
    CHECK(lo <= 70);
    CHECK(std::abs(static_cast<int>(pcm[0]) - 128) <= 3);  // average of the first two source samples
    // Upsampling 11025 -> 22050 doubles the length.
    CHECK(parseSsnd(wavToSsnd(makeWav(11025, 1, 8, std::vector<uint8_t>(1000, 128)), 22050))->sampleCount == 2000);
    // 32-bit float and 24-bit PCM.
    std::vector<uint8_t> fl;
    for (float v : {0.0f, 0.5f, -1.0f, 2.0f}) {
        uint8_t b[4];
        std::memcpy(b, &v, 4);
        fl.insert(fl.end(), b, b + 4);
    }
    auto fw = makeWav(22050, 1, 32, fl);
    fw[20] = 3;  // format tag: IEEE float
    const auto fb = wavToSsnd(fw, 22050);
    CHECK(fb[12] == 128);
    CHECK(fb[13] == 192);
    CHECK(fb[14] == 0);
    CHECK(fb[15] == 255);  // clamped
    const auto b24 = wavToSsnd(makeWav(22050, 1, 24, std::vector<uint8_t>{0, 0, 0x40, 0, 0, 0xC0}), 22050);  // +0.5, -0.5
    CHECK(b24[12] == 192);
    CHECK(b24[13] == 64);
}

TEST_CASE("mods/wav: unsupported files give clear errors") {
    auto adpcm = makeWav(22050, 1, 8, std::vector<uint8_t>(10, 128));
    adpcm[20] = 2;  // MS ADPCM
    try {
        wavToSsnd(adpcm, 22050);
        FAIL("no error");
    } catch (const FormatError& e) {
        CHECK(std::string(e.what()).find("PCM") != std::string::npos);
    }
    CHECK_THROWS_AS(wavToSsnd(bytesOf(std::string_view("RIFF\x04\0\0\0WAVE", 12)), 22050), FormatError);
    CHECK_THROWS_AS(wavToSsnd(makeWav(22050, 1, 8, {}), 22050), FormatError);  // empty
    auto b12 = makeWav(22050, 1, 16, std::vector<uint8_t>(10, 0));
    b12[34] = 12;
    CHECK_THROWS_AS(wavToSsnd(b12, 22050), FormatError);
}

// ================================================================================== text escapes

TEST_CASE("mods/text: named escapes") {
    const std::vector<uint8_t> b = {0xFF, 0x05, 0xFF, 0xFF, 0x03, 0x01, 0xFF, 0xFF, 0x06, 'L',  '=',  '2',  0xFF, 'A',  '{',  'b',
                                    0x0D, 0x82, 0x84, 0x85, 0xFE, 0x09, 0x0A, 0xFF, 0x02, 0xFF, '_',  0xFF, 0x02, 0xFF, 0x86, 0x84,
                                    0xFF, 0x01, 0x02, 0xFF, 0xFF, 0x04, 40,   80,   0xFF, 0xFF, 0x07, 0x41, 0xFF, 0xFF, 0xFF, 0xC0, 0x8B};
    const std::string t = escapeGameText(b);
    CHECK(t ==
          "{center}{color 0}{note L=2}A{{b\n{up}{spacebar}{margin}{tab}{lf}{ul}_{ul}{esc}{spacebar-1}{arg 2}{tabs 40 80}"
          "{ff 07 41}{ff}{xC0}{enter}");
    CHECK(unescapeGameText(t) == b);
    CHECK_FALSE(isPlainGameText(b));  // the {xC0}
    CHECK(isPlainGameText(std::vector<uint8_t>(b.begin(), b.end() - 2)));
}

TEST_CASE("mods/text: every byte string round-trips exactly") {
    std::mt19937 rng(11);
    for (int n = 0; n < 300; ++n) {
        std::vector<uint8_t> b(rng() % 200);
        for (auto& c : b) {
            const unsigned r = rng() % 10;
            c = static_cast<uint8_t>(r < 4 ? 0x20 + rng() % 95 : r < 6 ? 0xFF : r < 7 ? 0x0D : 1 + rng() % 255);
        }
        CHECK(unescapeGameText(escapeGameText(b)) == b);
    }
    std::vector<uint8_t> all;
    for (int c = 1; c < 256; ++c) all.push_back(static_cast<uint8_t>(c));
    CHECK(unescapeGameText(escapeGameText(all)) == all);
}

TEST_CASE("mods/text: line ends, {end}, Mac Roman characters, errors") {
    CHECK(unescapeGameText("a\r\nb\nc\rd") == bytesOf("a\rb\rc\rd"));
    CHECK(unescapeGameText("ab{end}\n\n") == bytesOf("ab"));
    CHECK(unescapeGameText("\xEF\xBB\xBFx") == bytesOf("x"));
    CHECK(unescapeGameText("ma\xC3\xB1" "ana \xE2\x80\x9Cq\xE2\x80\x9D") ==
          std::vector<uint8_t>{'m', 'a', 0x96, 'a', 'n', 'a', ' ', 0xD2, 'q', 0xD3});
    CHECK(unescapeGameText("a\tb") == bytesOf("a\tb"));
    auto err = [](std::string_view s) -> std::string {
        try {
            unescapeGameText(s);
        } catch (const ModSyntaxError& e) {
            return e.what();
        }
        return "";
    };
    CHECK(err("x\n{bogus}").find("line 2, column 1: unknown escape {bogus}") != std::string::npos);
    CHECK(err("{color}").find("{color N}") != std::string::npos);
    CHECK(err("a{b").find("closing") != std::string::npos);
    CHECK(err("{x00}").find("NUL") != std::string::npos);
    CHECK(err("ab{end}cd").find("after {end}") != std::string::npos);
    CHECK(err("\xE4\xB8\xAD").find("U+4E2D") != std::string::npos);
    CHECK(err("a\x01").find("control character") != std::string::npos);
    // OTHR files.
    const auto body = othrBodyFromTextFile("Hello\n{end}\n");
    CHECK(body.size() == 512);
    CHECK(std::memcmp(body.data(), "Hello\r\0", 7) == 0);
    CHECK(othrTextPayload(body) == bytesOf("Hello\r"));
    CHECK(othrTextFile(bytesOf("Hello\r")) == "Hello\n{end}\n");
    CHECK_THROWS_AS(othrBodyFromTextFile("{end}"), FormatError);
}

// ============================================================================== DAM assembler

TEST_CASE("mods/dam: assembler syntax and output") {
    const char* src =
        "; a test animation\n"
        "tag DAM\n"
        "start:\n"
        "  sprite 10 -2 0        ; FD\n"
        "  tick\n"
        "  sprite 0x10, 300, 1, 2\n"
        "  sound 16\n"
        "  stopsound\n"
        "  update -1 255 20 7\n"
        "  tick\n"
        "  loop start 3\n"
        "  loop start forever 1\n"
        "  byte 0x00 7\n"
        "  end\n";
    const auto a = assembleDam(src);
    const std::vector<uint8_t> want = {'D',  'A', 'M', 0x1A, 0xFD, 10, 0,  0xFE, 0xFF, 0, 0,  0xFE, 0xFD, 0x10, 0,    0x2C, 1, 1, 2,
                                       0xFC, 0,   16,  0xFB, 0,    0xF9, 0xFF, 0xFF, 255, 20, 0, 7, 0xFE, 0xFA, 4, 0,    3,    0xFA,
                                       4,    0,   0x80, 0,   7,    0xFF};
    CHECK(a.bytes == want);
    CHECK(a.paddedSize == 512);
    CHECK(a.endCount == 1);
    CHECK(a.maxFrame == 1);
    CHECK(a.sounds == std::vector<int>{16});
    const auto body = damBodyFromText(src);
    CHECK(body.size() == 512);
    CHECK(damBodyFromText(disassembleDam(body, 1)) == body);
}

TEST_CASE("mods/dam: disassembler keeps tags, no-op bytes, bytes after END and odd sizes") {
    std::vector<uint8_t> s = {'D', 'O', 'P', 0x1A, 0x12, 0xFD, 1, 0, 2, 0, 3, 0, 0xFE, 0xFA, 6, 0, 0, 0xFF,
                              0,   0,   0xFE, 0xFD, 9, 9, 9, 9, 9, 9, 0xFE, 0xFF, 0x33};
    padToNe(s);
    const std::string t = disassembleDam(s, 7);
    CHECK(t.find("tag DOP") != std::string::npos);
    CHECK(t.find("after END") != std::string::npos);
    CHECK(t.find("byte 0x12") != std::string::npos);
    CHECK(t.find("loop 0x0006 forever") != std::string::npos);  // target inside an instruction: numeric
    CHECK(damBodyFromText(t) == s);
    // A body larger than the NE rounding gets a size statement; an unknown tag is kept.
    std::vector<uint8_t> big = {'X', 'Y', 'Z', 'W', 0xFE, 0xFF};
    big.resize(1024, 0);
    const std::string tb = disassembleDam(big, 8);
    CHECK(tb.find("size 1024") != std::string::npos);
    CHECK(tb.find("tag 0x58 0x59 0x5A 0x57") != std::string::npos);
    CHECK(damBodyFromText(tb) == big);
    // An operand that would run past the resource is written as bytes.
    std::vector<uint8_t> cut = {'D', 'A', 'M', 0x1A, 0xFE, 0xFF};
    cut.resize(512, 0);
    cut[510] = 0xFD;
    CHECK(damBodyFromText(disassembleDam(cut, 9)) == cut);
}

TEST_CASE("mods/dam: scripts the interpreter cannot run are refused") {
    auto err = [](std::string_view s) -> std::string {
        try {
            assembleDam(s);
        } catch (const ModSyntaxError& e) {
            return e.what();
        }
        return "";
    };
    CHECK(err("sprite 0 0 0\nend").find("tag DAM") != std::string::npos);
    CHECK(err("tag DAM\nstart:\nsprite 0 0 0\nloop start forever").find("no 'tick'") != std::string::npos);
    CHECK(err("tag DAM\nsprite 0 0 0\ntick").find("no 'end'") != std::string::npos);
    CHECK(err("tag DAM\nloop 2 1\ntick\nend").find("outside the script") != std::string::npos);
    CHECK(err("tag DAM\nloop nowhere 1\nend").find("unknown label") != std::string::npos);
    CHECK(err("tag DAM\nfoo 1\nend").find("line 2: unknown statement 'foo'") != std::string::npos);
    CHECK(err("tag DAM\nsprite 0 0 256\nend").find("outside 0..255") != std::string::npos);
    CHECK(err("tag DAM\nsprite 0 0\nend").find("3 to 4 operands") != std::string::npos);
    CHECK(err("tag DAM\na:\na:\nend").find("defined twice") != std::string::npos);
    CHECK(err("tag DAM\nend\nsize 4").find("smaller") != std::string::npos);
    // A forever loop alone is a valid script.
    CHECK(assembleDam("tag DAM\nl: sprite 0 0 0\ntick\nloop l forever").endCount == 0);
}

// ==================================================================================== mod folders

TEST_CASE("mods/folder: scanning, manifest, unknown names") {
    TempDir t("tmport_modscan");
    const auto d = t.p / "MyMod";
    writeText(d / "mod.json", R"({"name": "Test", "newIds": [2500, "2600-2610"], "resizable": [1400]})");
    writeText(d / "pmap" / "1400.PNG", "x");
    writeText(d / "PMAP16" / "1400.png", "x");
    writeText(d / "OTHR" / "11000.txt", "x");
    writeText(d / "OTHR" / "10555.bin", "x");
    writeText(d / "OTHR" / "readme.txt", "x");
    writeText(d / "DAM" / "20000.dam.txt", "x");
    writeText(d / "MIDI" / "9000.mid", "x");
    writeText(d / "MIDI" / "9001.b.mid", "x");
    writeText(d / "TABLES" / "b.json", "{}");
    writeText(d / "TABLES" / "a.json", "{}");
    writeText(d / "Stuff" / "x.txt", "x");
    writeText(d / "README.txt", "ignored silently");
    const ModFolder f = scanModFolder(d);
    CHECK(f.manifest.name == "Test");
    CHECK(f.manifest.newIds.contains(2500));
    CHECK(f.manifest.newIds.contains(2605));
    CHECK_FALSE(f.manifest.newIds.contains(2611));
    CHECK(f.manifest.resizable.contains(1400));
    CHECK(f.errors.empty());
    CHECK(f.files.size() == 7);
    REQUIRE(f.tableFiles.size() == 2);
    CHECK(f.tableFiles[0].filename() == "a.json");
    int midiB = 0, dam = 0, p16 = 0;
    for (const auto& m : f.files) {
        midiB += m.kind == ModFileKind::MidiB && m.id == 9001;
        dam += m.kind == ModFileKind::Dam && m.id == 20000;
        p16 += m.kind == ModFileKind::Pmap16;
    }
    CHECK(midiB == 1);
    CHECK(dam == 1);
    CHECK(p16 == 1);
    CHECK(f.warnings.size() == 2);  // OTHR/readme.txt, Stuff/
    // A broken manifest is an error; a missing folder is just empty.
    writeText(d / "mod.json", R"({"newIds": ["12-3"]})");
    CHECK(scanModFolder(d).errors.size() == 1);
    CHECK(scanModFolder(t.p / "nope").files.empty());
    CHECK_THROWS_AS(parseManifest("[1]"), FormatError);
}

TEST_CASE("mods/folder: configured folders from tmport.ini") {
    const std::filesystem::path pref = "C:/pref", exe = "C:/exe", iniDir = "C:/inidir";
    auto dirs = modDirsFromIni(nullptr, iniDir, pref, exe);
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == pref / "mods");
    CHECK(dirs[1] == exe / "mods");
    // An absolute path of the platform (a drive path is relative off Windows).
#ifdef _WIN32
    const std::string absDir = "D:/abs/x";
#else
    const std::string absDir = "/abs/x";
#endif
    const auto ini = save::IniFile::parse("[mods]\npaths = mine ; " + absDir + " ;; mine\n");
    dirs = modDirsFromIni(&ini, iniDir, pref, exe);
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == iniDir / "mine");
    CHECK(dirs[1] == std::filesystem::path(absDir));
    const auto off = save::IniFile::parse("[mods]\nenabled = false\npaths = mine\n");
    CHECK(modDirsFromIni(&off, iniDir, pref, exe).empty());
}
