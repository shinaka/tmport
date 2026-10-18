// Mod-layer tests against the user's game data (TM_DATA): byte-exact round trips of every
// resource through its editable form, the override hook (precedence, validation, fallback),
// table overrides, MIDI overrides. Mod files written here are generated test content (patterns,
// a beep, a tiny font, a two-note tune) in a temporary folder; no game data is written to disk.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "anim/anim_bundle.h"
#include "anim/dam_script.h"
#include "audio/audio_tables.h"
#include "data/dgroup_offsets.h"
#include "game/game_tables.h"
#include "game/quiz.h"
#include "mods/dam_asm.h"
#include "mods/json.h"
#include "mods/mod_kit.h"
#include "mods/mods.h"
#include "mods/png_codec.h"
#include "mods/resource_import.h"
#include "mods/table_overrides.h"
#include "mods/text_codec.h"
#include "mods/wav_codec.h"
#include "test_data.h"
#include "text/font.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::mods;

namespace {

std::vector<uint8_t> bytesOf(std::string_view s) { return std::vector<uint8_t>(s.begin(), s.end()); }

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

void put(const std::filesystem::path& p, std::span<const uint8_t> b) { REQUIRE(writeWholeFile(p, b)); }
void putText(const std::filesystem::path& p, std::string_view s) { put(p, bytesOf(s)); }

std::unique_ptr<GameFiles> freshFiles(gfx::ColorMode mode = gfx::ColorMode::Colour256) {
    ResourceManager::Options o;
    o.colorMode = mode;
    return std::make_unique<GameFiles>(GameFiles::open(pathFromUtf8(*testdata::dataPath()), o));
}

// Indexed PNG with the game palette (the direct path): a pattern of palette indices.
std::vector<uint8_t> patternPng(const gfx::Palette& pal, int w, int h, uint8_t a, uint8_t b) {
    std::vector<uint8_t> idx(static_cast<size_t>(w * h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) idx[static_cast<size_t>(y * w + x)] = ((x / 4 + y / 4) % 2) ? a : b;
    std::vector<Rgb8> colours(pal.colours().begin(), pal.colours().end());
    return encodePngIndexed(w, h, idx, colours);
}

std::vector<uint8_t> beepWav() {
    std::vector<uint8_t> s;  // 0.2 s of 880 Hz, 16-bit stereo at 44100 Hz
    for (int i = 0; i < 8820; ++i) {
        const auto v = static_cast<int16_t>(std::lround(12000.0 * std::sin(2 * 3.14159265358979 * 880.0 * i / 44100.0)));
        for (int c = 0; c < 2; ++c) {
            s.push_back(static_cast<uint8_t>(v & 0xFF));
            s.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        }
    }
    return makeWav(44100, 2, 16, s);
}

std::vector<uint8_t> twoNoteMidi() {
    const std::vector<uint8_t> trk = {0x00, 0x90, 0x3C, 0x64, 0x60, 0x80, 0x3C, 0x40, 0x00, 0x90, 0x43, 0x64,
                                      0x60, 0x80, 0x43, 0x40, 0x00, 0xFF, 0x2F, 0x00};
    std::vector<uint8_t> m = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0, static_cast<uint8_t>(trk.size())};
    m.insert(m.end(), trk.begin(), trk.end());
    return m;
}

// A tiny FFNT: 256 glyphs, 8 px high, all 6 px wide, all sharing one 8-byte box bitmap.
std::vector<uint8_t> tinyFont() {
    std::vector<uint8_t> f = {0, 255, 8};
    for (int i = 0; i < 256; ++i) f.push_back(6);
    for (int i = 0; i < 256; ++i) f.push_back(0);  // offLo: glyph bitmap at pool offset 0
    for (int i = 0; i < 256; ++i) f.push_back(0);  // offHi
    for (int b : {0xFC, 0x84, 0x84, 0x84, 0x84, 0x84, 0xFC, 0x00}) f.push_back(static_cast<uint8_t>(b));
    return f;
}

}  // namespace

// ======================================================================= round trip of everything

TEST_CASE("mods/game: every resource round-trips byte-exactly through its editable form") {
    TM_REQUIRE_GAME_DATA();
    const ResourceManager& res = *g->res;
    const ModManifest none;
    const ImportContext ctx = importContextFor(res, &none);
    std::map<std::string, int> counts;
    int failures = 0;
    auto check = [&](ResType t, int id, gfx::ColorMode mode) {
        const auto e = exportResource(res, t, id, mode);
        REQUIRE(e);
        ++counts[e->rel.substr(0, e->rel.find('/')) + (e->kind == ModFileKind::OthrText ? ".txt" : e->kind == ModFileKind::OthrBin ? ".bin" : "")];
        const ImportResult r = importResourceBytes(e->kind, id, e->bytes, ctx);
        if (r.body != e->original) {
            ++failures;
            MESSAGE("round trip differs: " << e->rel);
        }
    };
    for (int id : res.ids(ResType::PMAP, DllId::TMT256)) check(ResType::PMAP, id, gfx::ColorMode::Colour256);
    if (res.hasDll(DllId::TMT16))
        for (int id : res.ids(ResType::PMAP, DllId::TMT16)) check(ResType::PMAP, id, gfx::ColorMode::Colour16);
    for (int n = 0; n < 9; ++n)
        for (int id : res.ids(ResType::SSND, soundDll(n))) check(ResType::SSND, id, gfx::ColorMode::Colour256);
    for (ResType t : {ResType::OTHR, ResType::BNDL, ResType::FFNT, ResType::RRGB})
        for (int id : res.ids(t, DllId::TMT)) check(t, id, gfx::ColorMode::Colour256);
    CHECK(failures == 0);
    CHECK(counts["PMAP"] == 764);
    if (res.hasDll(DllId::TMT16)) CHECK(counts["PMAP16"] == 764);
    CHECK(counts["SSND"] == 2861);
    CHECK(counts["DAM"] == 63);
    CHECK(counts["OTHR.txt"] == 408);  // puzzlers, hints, help and story pages
    CHECK(counts["OTHR.bin"] == 5);    // layout tables 10550-10553, object table 10555
    CHECK(counts["BNDL"] == 79);
    CHECK(counts["FFNT"] == 1);
    CHECK(counts["RRGB"] == 1);
}

TEST_CASE("mods/game: DAM disassembly -> assembly is exact for all 63 scripts") {
    TM_REQUIRE_GAME_DATA();
    int n = 0, tails = 0, loops = 0;
    for (int id : g->res->ids(ResType::OTHR, DllId::TMT)) {
        const auto body = g->res->loadOriginal(ResType::OTHR, id);
        REQUIRE(body);
        if (!isDamResource(*body)) continue;
        ++n;
        const std::string text = disassembleDam(*body, id);
        const DamAssembly a = assembleDam(text);
        auto bytes = a.bytes;
        bytes.resize(a.paddedSize, 0);
        CHECK_MESSAGE(bytes == *body, "DAM " << id);
        CHECK(a.endCount >= 1);
        tails += text.find("after END") != std::string::npos;
        loops += text.find("loop start forever") != std::string::npos;
        // The assembler's statistics agree with the engine's own analysis of the script.
        const auto stats = anim::analyze(*anim::makeScript(id, *body));
        if (text.find("after END") == std::string::npos) CHECK(a.maxFrame == stats.maxFrame);
    }
    CHECK(n == 63);
    CHECK(tails == 0);   // every script ends with its only FF, then NE zero padding
    CHECK(loops == 52);  // the idle loops FA 04 00 00
}

// ================================================================================= table overrides

TEST_CASE("mods/game: table export -> apply changes nothing; edits are visible to the readers") {
    TM_REQUIRE_GAME_DATA();
    auto exe = ExeTables::load(*g->source);
    const std::vector<uint8_t> before(exe->raw().begin(), exe->raw().end());
    const auto doc = exportTables(*exe);
    for (const auto& n : supportedTables()) CHECK(doc.find(n));
    const auto r0 = applyTables(*exe, doc);
    CHECK(r0.changedBytes == 0);
    CHECK(r0.repointedStrings == 0);
    CHECK(std::vector<uint8_t>(exe->raw().begin(), exe->raw().end()) == before);
    // The per-table files parse back to the same values.
    for (const auto& [name, text] : exportTableFiles(*exe)) CHECK(applyTablesText(*exe, text).changedBytes == 0);

    const auto r = applyTablesText(*exe, R"({
        "_comment": "ignored",
        "rooms": {"9": {"bgPmap": 1004, "music": 3}},
        "rankThresholds": [0, 6, 25, 70, 115, 170, 230, 301],
        "gemsNeeded": [null, 4, null, null],
        "elfCountByTier": {"3": 2},
        "clueWords": {"noun": {"1": "gizmo"}, "nounPlural": {"1": "gizmos"}},
        "rankNames": {"0": "{center}Rookie"}
    })");
    CHECK(r.repointedStrings == 3);
    CHECK(r.warnings.size() == 1);  // 2 elves: question elves missing
    CHECK(exe->rooms()[9].bgPmap == 1004);
    CHECK(exe->rooms()[9].music == 3);
    CHECK(exe->rooms()[10].bgPmap == g->exe->rooms()[10].bgPmap);
    CHECK(exe->u32(dg::kRankThresholds + 4) == 6u);
    CHECK(exe->u32(dg::kRankThresholds + 28) == 301u);
    const auto t = game::GameTables::fromExe(*exe);
    CHECK(t.gemsNeeded[1] == 4);
    CHECK(t.gemsNeeded[0] == g->exe->u8(dg::kGemsNeeded));
    CHECK(t.elfCountByTier[3] == 2);
    CHECK(t.words.noun[1] == "gizmo");
    CHECK(t.words.nounPlural[1] == "gizmos");
    CHECK(t.words.noun[2] == game::GameTables::fromExe(*g->exe).words.noun[2]);
    const auto rank0 = exe->farCstr(dg::kRankNames);
    CHECK(std::string(rank0.begin(), rank0.end()) == "\xFF\x05\xFFRookie");
    CHECK(exe->validate().ok);  // the original image is still recognised
    CHECK(exe->size() > exe->originalSize());
}

TEST_CASE("mods/game: invalid table overrides are refused and change nothing") {
    TM_REQUIRE_GAME_DATA();
    auto exe = ExeTables::load(*g->source);
    const std::vector<uint8_t> before(exe->raw().begin(), exe->raw().end());
    auto err = [&](std::string_view j) -> std::string {
        try {
            applyTablesText(*exe, j);
        } catch (const FormatError& e) {
            return e.what();
        }
        return "";
    };
    // A valid table first in the same document: nothing may be applied when a later one fails.
    CHECK(err(R"({"gemsNeeded": [5,5,5,5], "rankThresholds": [5, 6, 25, 70, 115, 170, 230, 300]})").find("rankThresholds: element 0 must be 0") != std::string::npos);
    CHECK(err(R"({"rankThresholds": [0, 6, 5, 70, 115, 170, 230, 300]})").find("smaller") != std::string::npos);
    CHECK(err(R"({"rooms": {"3": {"exitLeft": 60}}})").find("rooms[3].exitLeft: 60 is not a room") != std::string::npos);
    CHECK(err(R"({"rooms": {"3": {"bgPmap": 500}}})").find("a PMAP id") != std::string::npos);
    CHECK(err(R"({"rooms": {"3": {"colour": 1}}})").find("unknown room field") != std::string::npos);
    CHECK(err(R"({"rooms": {"50": {}}})").find("not an index") != std::string::npos);
    CHECK(err(R"({"gemsNeeded": [1, 2, 3]})").find("needs 4 elements") != std::string::npos);
    CHECK(err(R"({"gemsNeeded": [1, 2, 3, 9]})").find("1-5 treasures") != std::string::npos);
    CHECK(err(R"({"hintBit": {"0": 32}})").find("bit number 0-31") != std::string::npos);
    CHECK(err(R"({"clueWords": {"noun": {"1": "{bogus}"}}})").find("clueWords.noun[1]") != std::string::npos);
    CHECK(err(R"({"clueWords": {"verbs": []}})").find("unknown list") != std::string::npos);
    CHECK(err(R"({"menus": []})").find("unknown table") != std::string::npos);
    CHECK(err(R"({"gemX": [[1,2,3,4,5],[1,2,3,4,5],[1,2,3,4,5],[1,2,3,4,"x"]]})").find("gemX[3][4]") != std::string::npos);
    CHECK(err("[1]").find("JSON object") != std::string::npos);
    CHECK(std::vector<uint8_t>(exe->raw().begin(), exe->raw().end()) == before);
}

// ============================================================================ resource overrides

TEST_CASE("mods/game: PMAP overrides: precedence, size check, new ids, 16-colour fallback") {
    TM_REQUIRE_GAME_DATA();
    TempDir t("tmport_mods_pmap");
    const gfx::Palette pal = g->res->palette(900);
    const auto o1400 = g->res->pixmap(1400);
    const auto o1437 = g->res->pixmap(1437);
    const auto o1474 = g->res->pixmap(1474);
    REQUIRE(o1400);
    REQUIRE(o1437);
    REQUIRE(o1474);
    const auto a = t.p / "A", b = t.p / "B";
    put(a / "PMAP" / "1400.png", patternPng(pal, o1400->width, o1400->height, 2, 200));
    put(b / "PMAP" / "1400.png", patternPng(pal, o1400->width, o1400->height, 9, 9));
    put(a / "PMAP" / "1437.png", patternPng(pal, o1437->width + 1, o1437->height, 2, 3));   // wrong size
    put(a / "PMAP" / "2500.png", patternPng(pal, 16, 16, 2, 3));                            // new id, undeclared
    putText(b / "mod.json", R"({"name": "B", "newIds": [2501], "resizable": [1474]})");
    put(b / "PMAP" / "2501.png", patternPng(pal, 20, 10, 4, 5));                            // new id, declared
    put(b / "PMAP" / "1474.png", patternPng(pal, o1474->width + 8, o1474->height + 2, 6, 7));  // resizable
    put(a / "PMAP" / "5000.png", patternPng(pal, 4, 4, 1, 1));                              // SSND id range

    {
        auto files = freshFiles();
        const auto rep = applyMods(*files, {a, b});
        CHECK(rep.folders.size() == 2);
        const auto p = files->res->pixmap(1400);
        REQUIRE(p);
        CHECK(std::all_of(p->pixels.begin(), p->pixels.end(), [](uint8_t v) { return v == 9; }));  // B wins
        CHECK(files->res->pixmap(1437)->pixels == o1437->pixels);  // refused: original
        CHECK(files->res->pixmap(2500) == nullptr);
        const bool served2500 = files->res->pixmap(2500) != nullptr;
        CHECK_FALSE(served2500);
        const auto n = files->res->pixmap(2501);
        REQUIRE(n);
        CHECK(n->width == 20);
        CHECK(files->res->exists(2501));
        CHECK(files->res->pixmap(1474)->width == o1474->width + 8);
        CHECK(files->res->roundedSize(ResType::PMAP, 1400) % 512 == 0);
        CHECK(files->res->loadOriginal(ResType::PMAP, 1400)->size() == g->res->loadRaw(ResType::PMAP, 1400)->size());
        const auto fails = files->res->mods().failures();
        std::string all;
        for (const auto& f : fails) all += f + "\n";
        CHECK(all.find("but PMAP 1437 is") != std::string::npos);
        CHECK(all.find("PMAP 2500 does not exist in the game") != std::string::npos);
        CHECK(files->res->mods().shadowed().size() == 1);  // A's 1400
        // validateInstalledMods also reports the id-range error of 5000.png.
        ModsReport r2;
        validateInstalledMods(*files, r2);
        std::string errs;
        for (const auto& e : r2.errors) errs += e + "\n";
        CHECK(errs.find("id 5000 is a SSND id") != std::string::npos);
        CHECK(r2.errors.size() == 3);
    }
    {
        auto files = freshFiles();
        applyMods(*files, {b, a});  // A last: A wins
        const auto p = files->res->pixmap(1400);
        CHECK(p->at(0, 0) == 200);
        CHECK(p->at(4, 0) == 2);
    }
    if (g->res->hasDll(DllId::TMT16)) {
        auto files = freshFiles(gfx::ColorMode::Colour16);
        applyMods(*files, {a});
        const auto p = files->res->pixmap(1400, gfx::ColorMode::Colour16);
        REQUIRE(p);
        CHECK(p->depth == 4);
        CHECK(std::all_of(p->pixels.begin(), p->pixels.end(), [](uint8_t v) { return v < 16; }));
        CHECK(p->pixels != files->res->pixmap(1437, gfx::ColorMode::Colour16)->pixels);
    }
}

TEST_CASE("mods/game: RGBA art with transparency is quantised to the game palette") {
    TM_REQUIRE_GAME_DATA();
    TempDir t("tmport_mods_rgba");
    const auto o = g->res->pixmap(1700);
    REQUIRE(o);
    std::vector<uint8_t> rgba(static_cast<size_t>(o->width * o->height) * 4);
    for (int y = 0; y < o->height; ++y)
        for (int x = 0; x < o->width; ++x) {
            uint8_t* p = &rgba[static_cast<size_t>(y * o->width + x) * 4];
            p[0] = static_cast<uint8_t>(x * 255 / o->width);
            p[1] = static_cast<uint8_t>(y * 255 / o->height);
            p[2] = 128;
            p[3] = (x < 3) ? 0 : 255;  // a transparent stripe
        }
    put(t.p / "M" / "PMAP" / "1700.png", encodePngRgba(o->width, o->height, rgba));
    auto files = freshFiles();
    applyMods(*files, {t.p / "M"});
    const auto p = files->res->pixmap(1700);
    REQUIRE(p);
    const gfx::Palette pal = files->res->palette(900);
    CHECK(pal.keyTable()[p->at(0, 0)]);
    CHECK(pal.keyTable()[p->at(2, o->height - 1)]);
    for (int x = 3; x < o->width; x += 7) CHECK_FALSE(pal.keyTable()[p->at(x, 5)]);
}

TEST_CASE("mods/game: SSND, OTHR text, BNDL, DAM, RRGB and FFNT overrides with validation") {
    TM_REQUIRE_GAME_DATA();
    TempDir t("tmport_mods_res");
    const auto m = t.p / "M";
    put(m / "SSND" / "7000.wav", beepWav());
    put(m / "SSND" / "8000.wav", beepWav());  // no DLL serves 8000-9999 and it is not declared
    putText(m / "OTHR" / "11000.txt",
            "{center}{color 0}{note L=1}\nWhich of these is a fruit?\n\n\nAn apple\nA chair\nA cloud\n{end}\n");
    putText(m / "OTHR" / "11001.txt", "{center}Two answers only?\n\n\nYes\nNo{end}");
    putText(m / "OTHR" / "11500.txt", "\n{center}A test message.{end}");
    std::vector<uint32_t> rev;
    const auto b301 = g->res->bundle(301);
    REQUIRE(b301);
    REQUIRE(b301->ids.size() == 10);
    for (int id : b301->ids) rev.insert(rev.begin(), static_cast<uint32_t>(id));
    putText(m / "BNDL" / "301.json", bndlJson(rev));
    const auto b300 = g->res->bundle(300);
    REQUIRE(b300);
    std::vector<uint32_t> short300(b300->ids.begin(), b300->ids.end() - 1);
    putText(m / "BNDL" / "300.json", bndlJson(short300));
    putText(m / "BNDL" / "330.json", "[1700,1700,1700,1700,1700,1700,1700,1700,1700,1700,1700]");
    putText(m / "DAM" / "20100.dam.txt", "tag DAM\nstart:\n sprite 0 0 1\n tick\n sprite 0 0 2\n tick\n loop start forever\n end\n");
    putText(m / "OTHR" / "20100.bin", "lower priority than the DAM file");
    putText(m / "DAM" / "20101.dam.txt", "tag DAM\nl: sprite 0 0 0\n loop l forever\n");  // no tick: refused
    std::vector<uint8_t> pal(768);
    for (size_t i = 0; i < 768; ++i) pal[i] = static_cast<uint8_t>(i * 7);
    pal[15 * 3] = 0;
    pal[15 * 3 + 1] = 0;
    pal[15 * 3 + 2] = 0xAA;
    pal[200 * 3] = 1;
    pal[200 * 3 + 1] = 2;
    pal[200 * 3 + 2] = 3;
    put(m / "RRGB" / "900.pal", pal);
    put(m / "FFNT" / "999.ffnt", tinyFont());

    auto files = freshFiles();
    applyMods(*files, {m});
    ResourceManager& res = *files->res;
    // SSND: converted to the original's 22050 Hz 8-bit mono.
    const auto si = res.soundInfo(7000);
    REQUIRE(si);
    CHECK(si->sampleRate == 22050);
    CHECK(si->sampleCount == 4410);
    CHECK_FALSE(res.soundInfo(8000));
    // Question: valid one replaces the original; the broken one falls back.
    const auto q = game::parseQuestion(*res.loadRaw(11000));
    REQUIRE(q);
    CHECK(q->answerCount == 3);
    CHECK(q->answer(0) == "An apple");
    CHECK(res.loadRaw(11001) == res.loadOriginal(ResType::OTHR, 11001));
    CHECK(std::string(reinterpret_cast<const char*>(res.loadRaw(11500)->data())).find("A test message.") != std::string::npos);
    // BNDL: reordered 301 accepted, 300 with 68 members and 330 with 11 refused.
    std::vector<int> want301(rev.begin(), rev.end());
    CHECK(res.bundle(301)->ids == want301);
    CHECK(res.bundle(300)->ids.size() == 69);
    CHECK(res.bundle(330)->ids == g->res->bundle(330)->ids);
    // DAM: the .dam.txt beats the .bin of the same folder; a script without tick is refused.
    const auto s = anim::loadScript(res, 20100);
    CHECK(anim::analyze(*s).maxFrame == 2);
    CHECK(anim::loadScript(res, 20101)->bytes == *res.loadOriginal(ResType::OTHR, 20101));
    // RRGB and FFNT.
    CHECK(res.palette(900).colour(200) == Rgb8{1, 2, 3});
    const text::Font f = text::Font::load(res);
    CHECK(f.height() == 8);
    CHECK(f.charWidth('A') == 6);
    // The failures are the expected ones.
    const auto fails = res.mods().failures();
    std::string all;
    for (const auto& x : fails) all += x + "\n";
    INFO(all);
    CHECK(all.find("SSND 8000 does not exist") != std::string::npos);
    CHECK(all.find("question 11001 needs exactly three answers") != std::string::npos);
    CHECK(all.find("BNDL 300 (player frames) must have exactly 69 members, not 68") != std::string::npos);
    CHECK(all.find("BNDL 330 (treasure frames) must have 1 to 10 members, not 11") != std::string::npos);
    CHECK(all.find("no 'tick'") != std::string::npos);
    CHECK(fails.size() == 5);
    bool shadow = false;
    for (const auto& x : res.mods().shadowed()) shadow = shadow || x.find("20100.bin: ignored") != std::string::npos;
    CHECK(shadow);
}

TEST_CASE("mods/game: binary OTHR overrides and PMAP16 art") {
    TM_REQUIRE_GAME_DATA();
    TempDir t("tmport_mods_bin");
    const auto m = t.p / "M";
    // A generated picture-attribute table (OTHR 10555 is binary): 100 entries, padded to 512.
    std::vector<uint8_t> table;
    for (int k = 0; k < 100; ++k) table.insert(table.end(), {static_cast<uint8_t>(1 + k % 7), static_cast<uint8_t>(k % 5), static_cast<uint8_t>(k % 5), static_cast<uint8_t>(1 + k % 15)});
    put(m / "OTHR" / "10555.bin", table);
    // A layout table that declares more records than it holds is refused.
    put(m / "OTHR" / "10550.bin", std::vector<uint8_t>{0x10, 0x00, 1, 2, 3});
    // 16-colour art: PMAP16 wins over PMAP in --16 mode, PMAP wins in 256-colour mode.
    const auto o = g->res->pixmap(1400);
    REQUIRE(o);
    std::vector<uint8_t> idx16(o->pixels.size());
    for (size_t i = 0; i < idx16.size(); ++i) idx16[i] = (i / 8) % 2 ? 1 : 12;
    const gfx::Palette vga = gfx::Palette::vga16();
    put(m / "PMAP16" / "1400.png", encodePngIndexed(o->width, o->height, idx16, std::vector<Rgb8>(vga.colours().begin(), vga.colours().begin() + 16)));
    put(m / "PMAP" / "1400.png", patternPng(g->res->palette(900), o->width, o->height, 2, 2));

    auto files = freshFiles();
    applyMods(*files, {m});
    auto raw = *files->res->loadRaw(10555);
    CHECK(raw.size() == 512);
    CHECK(std::equal(table.begin(), table.end(), raw.begin()));
    const auto objs = game::parseObjectTable(raw);
    CHECK(objs[3] == game::ObjectAttributes{4, 3, 3, 4});
    CHECK(files->res->loadRaw(10550) == files->res->loadOriginal(ResType::OTHR, 10550));
    CHECK(files->res->mods().failures().size() == 1);
    CHECK(files->res->pixmap(1400)->at(0, 0) == 2);
    if (g->res->hasDll(DllId::TMT16)) {
        auto f16 = freshFiles(gfx::ColorMode::Colour16);
        applyMods(*f16, {m});
        CHECK(f16->res->pixmap(1400, gfx::ColorMode::Colour16)->pixels == idx16);
    }
}

TEST_CASE("mods/game: broken palette and font overrides fall back to the originals") {
    TM_REQUIRE_GAME_DATA();
    TempDir t("tmport_mods_bad");
    const auto m = t.p / "M";
    put(m / "RRGB" / "900.pal", std::vector<uint8_t>(700, 1));
    put(m / "FFNT" / "999.ffnt", std::vector<uint8_t>{0, 255, 8, 1, 2, 3});
    auto files = freshFiles();
    applyMods(*files, {m});
    CHECK(files->res->palette(900).colours() == g->res->palette(900).colours());
    CHECK(text::Font::load(*files->res).height() == text::Font::load(*g->res).height());
    CHECK(files->res->mods().failures().size() == 2);
}

// ========================================================================= MIDI and table folders

TEST_CASE("mods/game: MIDI overrides are served through the data source; table files are applied") {
    TM_REQUIRE_GAME_DATA();
    TempDir t("tmport_mods_midi");
    const auto m = t.p / "M";
    const auto tune = twoNoteMidi();
    put(m / "MIDI" / "9000.mid", tune);
    put(m / "MIDI" / "9001.b.mid", tune);
    putText(m / "MIDI" / "9002.mid", "not a midi file");
    put(m / "MIDI" / "9100.mid", tune);
    putText(m / "TABLES" / "a.json", R"({"gemsNeeded": [3, 3, 4, 5]})");
    putText(m / "TABLES" / "b.json", R"({"gemsNeeded": [9, 3, 4, 5]})");  // invalid: ignored entirely
    auto files = freshFiles();
    const auto rep = applyMods(*files, {m});
    CHECK(rep.tunes == 3);  // 9000 E and B, 9001 B
    CHECK(rep.tableFiles == 1);
    CHECK(rep.errors.size() == 3);  // 9002 not a MIDI file, 9100 not a tune id, b.json
    const audio::AudioTables at = audio::AudioTables::fromExe(*files->exe);
    auto path = [&](int i, char set) {
        std::string n = at.tuneFiles[static_cast<size_t>(i)];
        n[0] = set;
        return at.soundsDir + n;
    };
    auto read = [&](const vfs::DataSource& src, const std::string& p) {
        auto f = src.open(p);
        REQUIRE(f);
        return f->readAll();
    };
    CHECK(read(*files->source, path(0, 'E')) == tune);
    CHECK(read(*files->source, path(0, 'B')) == tune);
    CHECK(read(*files->source, path(1, 'B')) == tune);
    CHECK(read(*files->source, path(1, 'E')) == read(*g->source, path(1, 'E')));
    CHECK(read(*files->source, path(2, 'E')) == read(*g->source, path(2, 'E')));
    CHECK(read(*files->source, toLowerAscii(path(0, 'E'))) == tune);  // case-insensitive
    CHECK(files->exe->array<uint8_t, 4>(dg::kGemsNeeded) == std::array<uint8_t, 4>{3, 3, 4, 5});
    // The DLLs still come from the game data.
    CHECK(files->res->pixmap(1000) != nullptr);
}
