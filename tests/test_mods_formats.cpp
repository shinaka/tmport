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

#include "gfx/pixmap.h"
#include "mischief/gfx/palette.h"
#include "mischief/mods/png_codec.h"
#include "mischief/util/file_util.h"
#include "mods/dam_asm.h"
#include "mods/mod_common.h"
#include "mods/mod_folder.h"
#include "mods/mods.h"
#include "mods/pmap_codec.h"
#include "mods/text_codec.h"
#include "mods/wav_codec.h"
#include "res/ssnd.h"
#include "save/settings.h"

using namespace tmport;
using namespace tmport::mods;

namespace {

std::vector<uint8_t> bytesOf(std::string_view s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// A synthetic 256-colour palette: a colour ramp with the colour key (0,0,170) at 15 and 241 and
// white duplicated at 0 and 77 (the shape, not the content, of the game's palette).
mischief::gfx::Palette testPalette() {
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
    return mischief::gfx::Palette::fromRrgb(rrgb);
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

void writeText(const std::filesystem::path& p, std::string_view s) { REQUIRE(mischief::writeWholeFile(p, bytesOf(s))); }

}  // namespace

// ============================================================================================ JSON

// ==================================================================================== zlib / PNG

// ================================================================================= PMAP from PNG

TEST_CASE("mods/pmap: indexed PNG with the game palette keeps every index (duplicates too)") {
    const mischief::gfx::Palette pal = testPalette();
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
    const mischief::gfx::Palette pal = testPalette();
    const std::vector<uint8_t> rgba = {
        0,   0,   0xAA, 255,  // key colour -> 15
        10,  20,  30,   0,    // alpha 0 -> 15
        255, 0,   0,    255,  // exact -> 2
        5,   250, 3,    200,  // nearest -> 3 (green)
        0,   0,   0xA8, 255,  // nearest is the key colour, which is excluded -> 5 (0,0,A0)
        250, 252, 255,  127,  // alpha < 128 -> key
    };
    const auto img = pngToIndices(mischief::mods::encodePngRgba(6, 1, rgba), pal);
    CHECK_FALSE(img.direct);
    CHECK(img.indices == std::vector<uint8_t>{15, 15, 2, 3, 5, 15});
    CHECK(img.keyPixels == 3);
    CHECK(img.remapped == 2);
    // White maps to the first white entry (0), not the duplicate 77.
    CHECK(pngToIndices(mischief::mods::encodePngRgba(1, 1, std::vector<uint8_t>{255, 255, 255, 255}), pal).indices[0] == 0);
    // An indexed PNG with a foreign palette is mapped by colour; alpha entries become the key.
    const std::vector<mischief::Rgb8> foreign = {{255, 0, 0}, {1, 254, 1}, {9, 9, 9}};
    const auto fi = pngToIndices(mischief::mods::encodePngIndexed(3, 1, std::vector<uint8_t>{0, 1, 2}, foreign, std::vector<uint8_t>{255, 255, 0}), pal);
    CHECK_FALSE(fi.direct);
    CHECK(fi.indices == std::vector<uint8_t>{2, 3, 15});
    // 16-colour art: VGA palette, key nibble 3, packed 4 bpp with a zero pad nibble.
    const mischief::gfx::Palette vga = mischief::gfx::Palette::vga16();
    const auto v = pngToIndices(mischief::mods::encodePngRgba(3, 1, std::vector<uint8_t>{0, 0, 0, 255, 0, 0, 0xAA, 255, 255, 255, 255, 255}), vga);
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
    const auto f = mischief::mods::parseWavFormat(wav);
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
    const auto body = wavToSsnd(mischief::mods::makeWav(44100, 2, 16, s), 22050, &note);
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
    CHECK(parseSsnd(wavToSsnd(mischief::mods::makeWav(11025, 1, 8, std::vector<uint8_t>(1000, 128)), 22050))->sampleCount == 2000);
    // 32-bit float and 24-bit PCM.
    std::vector<uint8_t> fl;
    for (float v : {0.0f, 0.5f, -1.0f, 2.0f}) {
        uint8_t b[4];
        std::memcpy(b, &v, 4);
        fl.insert(fl.end(), b, b + 4);
    }
    auto fw = mischief::mods::makeWav(22050, 1, 32, fl);
    fw[20] = 3;  // format tag: IEEE float
    const auto fb = wavToSsnd(fw, 22050);
    CHECK(fb[12] == 128);
    CHECK(fb[13] == 192);
    CHECK(fb[14] == 0);
    CHECK(fb[15] == 255);  // clamped
    const auto b24 = wavToSsnd(mischief::mods::makeWav(22050, 1, 24, std::vector<uint8_t>{0, 0, 0x40, 0, 0, 0xC0}), 22050);  // +0.5, -0.5
    CHECK(b24[12] == 192);
    CHECK(b24[13] == 64);
}

TEST_CASE("mods/wav: unsupported files give clear errors") {
    auto adpcm = mischief::mods::makeWav(22050, 1, 8, std::vector<uint8_t>(10, 128));
    adpcm[20] = 2;  // MS ADPCM
    try {
        wavToSsnd(adpcm, 22050);
        FAIL("no error");
    } catch (const mischief::FormatError& e) {
        CHECK(std::string(e.what()).find("PCM") != std::string::npos);
    }
    CHECK_THROWS_AS(wavToSsnd(bytesOf(std::string_view("RIFF\x04\0\0\0WAVE", 12)), 22050), mischief::FormatError);
    CHECK_THROWS_AS(wavToSsnd(mischief::mods::makeWav(22050, 1, 8, {}), 22050), mischief::FormatError);  // empty
    auto b12 = mischief::mods::makeWav(22050, 1, 16, std::vector<uint8_t>(10, 0));
    b12[34] = 12;
    CHECK_THROWS_AS(wavToSsnd(b12, 22050), mischief::FormatError);
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
    CHECK_THROWS_AS(othrBodyFromTextFile("{end}"), mischief::FormatError);
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
    CHECK_THROWS_AS(parseManifest("[1]"), mischief::FormatError);
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
    const auto ini = mischief::save::IniFile::parse("[mods]\npaths = mine ; " + absDir + " ;; mine\n");
    dirs = modDirsFromIni(&ini, iniDir, pref, exe);
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == iniDir / "mine");
    CHECK(dirs[1] == std::filesystem::path(absDir));
    const auto off = mischief::save::IniFile::parse("[mods]\nenabled = false\npaths = mine\n");
    CHECK(modDirsFromIni(&off, iniDir, pref, exe).empty());
}
