// The mod layer of the 1990 DOS edition (src/dos/mods/, docs/MODDING.md "The 1990 DOS edition"):
// byte-exact round trips of every file and of the DGROUP through the editable formats, the packers
// against every original file, served overrides (a sprite frame and its hotspot, a tune, a question,
// a message), the checks that refuse malformed mod files, folder discovery and the CLI. Mod files
// written here are generated test content (patterns, short texts) in a temporary folder, or the
// user's own data converted in memory; tests keep only sizes, counts and positions.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "dos/data/dgroup_offsets.h"
#include "dos/data/puzzles.h"
#include "dos/gfx/pak.h"
#include "dos/gfx/pan.h"
#include "dos/mods/dos_exe_strings.h"
#include "dos/mods/dos_mod_folder.h"
#include "dos/mods/dos_mod_kit.h"
#include "dos/mods/dos_mods.h"
#include "dos/mods/dos_mods_cli.h"
#include "dos/mods/dos_pictures.h"
#include "dos/mods/dos_sound_text.h"
#include "dos/mods/dos_text_codec.h"
#include "dos/mods/dos_text_files.h"
#include "dos/text/dos_font.h"
#include "dos_test_data.h"
#include "mischief/mods/json.h"
#include "mischief/mods/png_codec.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mods/mod_common.h"
#include "mods/mods_cli.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

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

void put(const std::filesystem::path& p, std::span<const uint8_t> b) {
    std::filesystem::create_directories(p.parent_path());
    REQUIRE(mischief::writeWholeFile(p, b));
}
void putText(const std::filesystem::path& p, std::string_view s) {
    put(p, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
}
std::vector<uint8_t> bytesOf(std::string_view s) { return {s.begin(), s.end()}; }
std::string textOf(std::span<const uint8_t> b) { return std::string(b.begin(), b.end()); }

// The message of what f throws ("" when it does not throw).
std::string errorOf(const std::function<void()>& f) {
    try {
        f();
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

bool contains(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }

bool anyContains(const std::vector<std::string>& v, std::string_view part) {
    return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return contains(s, part); });
}

uint64_t hashOf(std::span<const uint8_t> b) {
    uint64_t h = 1469598103934665603ull;
    for (const uint8_t c : b) h = (h ^ c) * 1099511628211ull;
    return h;
}

// Replaces the first occurrence of `from` after `after` (or from the start) in s.
std::string replaceFirst(std::string s, std::string_view from, std::string_view to, std::string_view after = {}) {
    const size_t start = after.empty() ? 0 : s.find(after);
    REQUIRE(start != std::string::npos);
    const size_t at = s.find(from, start);
    REQUIRE(at != std::string::npos);
    s.replace(at, from.size(), to);
    return s;
}

}  // namespace

// ---- text codes (no game data) ---------------------------------------------------------------------

TEST_CASE("dos mods: text codes round-trip every byte and name the key pictograms") {
    std::vector<uint8_t> all;
    for (int b = 1; b < 256; ++b) all.push_back(static_cast<uint8_t>(b));
    for (const DosTextForm form : {DosTextForm::Lines, DosTextForm::Json})
        CHECK(unescapeDosText(escapeDosText(all, form), form) == all);
    CHECK(escapeDosText(std::vector<uint8_t>{0x1B, 0x07, 0x06, '{', 0x0D, 'a'}, DosTextForm::Lines) == "{up}{spacebar}{{\na");
    CHECK(unescapeDosText("a\r\nb\nc", DosTextForm::Lines) == std::vector<uint8_t>{'a', 0x0D, 'b', 0x0D, 'c'});
    CHECK(unescapeDosText("a\r\nb", DosTextForm::Json) == std::vector<uint8_t>{'a', 0x0D, 0x0A, 'b'});
    CHECK(contains(errorOf([] { (void)unescapeDosText("ab{nope}", DosTextForm::Lines); }), "unknown code {nope}"));
    CHECK(contains(errorOf([] { (void)unescapeDosText("x\n{x00}", DosTextForm::Lines); }), "line 2"));
    CHECK(contains(errorOf([] { (void)unescapeDosText("caf\xC3\xA9", DosTextForm::Lines); }), "outside ASCII"));
    CHECK(contains(errorOf([] { (void)unescapeDosText("a{up", DosTextForm::Lines); }), "without '}'"));
    CHECK(checkDosMarkup(bytesOf("~035040Hi |12x\t05y")).empty());
    CHECK(contains(checkDosMarkup(bytesOf("~03504 Hi")), "six digits"));
    CHECK(contains(checkDosMarkup(bytesOf("a|1")), "two digits"));
}

TEST_CASE("dos mods: DOS mod folders come from tmport.ini [mods] dos_paths or the default folders") {
    // An absolute path of the platform (a drive path is relative off Windows).
#ifdef _WIN32
    const std::string absDir = "C:/abs/b";
#else
    const std::string absDir = "/abs/b";
#endif
    const auto ini = mischief::save::IniFile::parse("[mods]\ndos_paths = a ; " + absDir + " ;\n");
    const auto dirs = dosModDirsFromIni(&ini, "C:/ini", "C:/pref", "C:/exe");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0] == std::filesystem::path("C:/ini") / "a");
    CHECK(dirs[1] == std::filesystem::path(absDir));
    const auto off = mischief::save::IniFile::parse("[mods]\nenabled = false\ndos_paths = a\n");
    CHECK(dosModDirsFromIni(&off, "C:/ini", "C:/pref", "C:/exe").empty());
    const auto defaults = dosModDirsFromIni(nullptr, {}, "C:/pref", "C:/exe");
    REQUIRE(defaults.size() == 2);
    CHECK(defaults[0] == std::filesystem::path("C:/pref") / "dos_mods");
    CHECK(defaults[1] == std::filesystem::path("C:/exe") / "dos_mods");

    const DosModManifest m = parseDosManifest(R"({"name": "x", "edition": "dos", "resizable": ["sprites.pak:72", "BEGIN.PAK:31-35", "SHACK.PAK:*"]})");
    CHECK(m.frameResizable("SPRITES.PAK", 72));
    CHECK(!m.frameResizable("SPRITES.PAK", 73));
    CHECK(m.frameResizable("BEGIN.PAK", 33));
    CHECK(m.frameResizable("SHACK.PAK", 125));
    CHECK(contains(errorOf([] { (void)parseDosManifest(R"({"resizable": ["SPRITES.PAK:9-3"]})"); }), "FILE:N"));
    CHECK(contains(errorOf([] { (void)parseDosManifest(R"({"edition": "win"})"); }), "\"dos\""));
}

// ---- formats against the user's files ------------------------------------------------------------

TEST_CASE("dos mods: the sound files round-trip through note text; malformed notes are refused") {
    TM_REQUIRE_DOS_FILES();
    for (const FileIndex i : {FileIndex::TunesIbm, FileIndex::SfxIbm, FileIndex::TunesTan, FileIndex::SfxTan}) {
        const auto orig = dosFiles->bytes(i);
        const std::string text = soundFileToText(orig, i, "X");
        const auto back = soundFileFromText(text, i, "X");
        CHECK_MESSAGE(std::equal(back.begin(), back.end(), orig.begin(), orig.end()), static_cast<int>(i));
    }
    // A generated effects file: 12 effects of one note each.
    std::string sfx;
    for (int e = 0; e < 12; ++e) sfx += "effect " + std::to_string(e) + "\n  tempo 30  # a comment\n  C#4 1/8. tie\n  rest 1/16\n  Db5 full\n  end\n";
    const auto built = soundFileFromText(sfx, FileIndex::SfxIbm, "SFX.IBM");
    CHECK(built.size() == 0x96 + 12 * 10);
    CHECK(built[0x96 + 2] == 0x32);  // C#4: octave code 3, note 2
    CHECK(built[0x96 + 3] == (3 | 8 | 16));
    CHECK(built[0x96 + 6] == 0x42);  // Db5 = C#5
    CHECK(built[0x96 + 7] == 0x80);
    CHECK(soundFileFromText(soundFileToText(built, FileIndex::SfxIbm, "SFX.IBM"), FileIndex::SfxIbm, "SFX.IBM") == built);

    CHECK(contains(errorOf([&] { (void)soundFileFromText(replaceFirst(sfx, "C#4", "H4"), FileIndex::SfxIbm, "SFX.IBM"); }), "line 3"));
    CHECK(contains(errorOf([&] { (void)soundFileFromText(replaceFirst(sfx, "  end\n", ""), FileIndex::SfxIbm, "SFX.IBM"); }), "no 'end'"));
    CHECK(contains(errorOf([&] { (void)soundFileFromText(sfx.substr(0, sfx.rfind("effect 11")), FileIndex::SfxIbm, "SFX.IBM"); }),
                   "exactly 12"));
    CHECK(contains(errorOf([&] { (void)soundFileFromText(replaceFirst(sfx, "tempo 30", "envelope 36"), FileIndex::SfxIbm, "SFX.IBM"); }),
                   "envelope 36"));
    CHECK(contains(errorOf([&] { (void)soundFileFromText(sfx, FileIndex::TunesIbm, "TUNES.IBM"); }), "holds tunes"));
    std::string longFx = sfx;
    for (int k = 0; k < 900; ++k) longFx = replaceFirst(longFx, "  rest 1/16\n", "  rest 1/16\n  rest 1/16\n");
    CHECK(contains(errorOf([&] { (void)soundFileFromText(longFx, FileIndex::SfxIbm, "SFX.IBM"); }), "buffer holds 1712"));
}

TEST_CASE("dos mods: questions, word table and help round-trip; the quiz limits are checked") {
    TM_REQUIRE_DOS_FILES();
    const auto dictBytes = dosFiles->bytes(FileIndex::PrgDict);
    const auto back = dictionaryFromText(dictionaryToText(dictBytes));
    CHECK(std::equal(back.begin(), back.end(), dictBytes.begin(), dictBytes.end()));

    const Dictionary dict = Dictionary::parse(dictBytes);
    const PuzzleFile puz = PuzzleFile::parse(dosFiles->bytes(FileIndex::AllPuzs));
    const std::string text = puzzlesToText(puz, dict, dosFiles->exe());
    const auto expanded = puzzlesFromText(text);
    REQUIRE(expanded.size() == 360);
    const auto built = buildAllPuzs(expanded, dict);
    const auto orig = dosFiles->bytes(FileIndex::AllPuzs);
    CHECK(std::equal(built.begin(), built.end(), orig.begin(), orig.end()));  // the compressor reproduces all 360
    int same = 0;
    for (int i = 0; i < 360; ++i) {
        const auto c = compressPuzzle(expanded[static_cast<size_t>(i)], dict);
        same += std::equal(c.begin(), c.end(), puz.raw(i).begin(), puz.raw(i).end()) ? 1 : 0;
    }
    CHECK(same == 360);

    const DosFont font = DosFont::fromExe(dosFiles->exe());
    const QuizTiers tiers = QuizTiers::read(dosFiles->exe(), puz);
    std::vector<std::pair<int, int>> ranges;
    for (const int t : {0, 1, 2, 3, 5}) ranges.push_back(tiers.range(t));
    DosTextProblems pr;
    checkPuzzles(expanded, ranges, font, pr);
    CHECK(pr.errors.empty());
    CHECK(pr.warnings.empty());

    const auto help = dosFiles->bytes(FileIndex::HelpDat);
    const auto helpBack = helpFromText(helpToText(help), help.first(4));
    CHECK(std::equal(helpBack.begin(), helpBack.end(), help.begin(), help.end()));
    DosTextProblems hp;
    checkHelp(help, font, hp);
    CHECK(hp.errors.empty());
    CHECK(hp.warnings.empty());

    // Malformed questions and pages.
    const std::string q0 = "=== question 0";
    CHECK(contains(errorOf([&] { (void)puzzlesFromText(replaceFirst(text, "--- answers\n", "--- answers\nextra\n", q0)); }),
                   "more than three answers"));
    CHECK(contains(errorOf([&] { (void)puzzlesFromText(text.substr(0, text.find("=== question 359"))); }), "exactly 360"));
    CHECK(contains(errorOf([&] { (void)puzzlesFromText(replaceFirst(text, "\n--- answers", "~\n--- answers", q0)); }), "'~'"));
    auto longAnswer = expanded;
    longAnswer[5].insert(longAnswer[5].begin() + static_cast<std::ptrdiff_t>(std::find(longAnswer[5].begin(), longAnswer[5].end(), '~') -
                                                                               longAnswer[5].begin() + 1),
                         40, 'x');
    DosTextProblems lp;
    checkPuzzles(longAnswer, ranges, font, lp);
    CHECK(anyContains(lp.errors, "question 5 answer 1"));
    CHECK(contains(errorOf([&] { (void)helpFromText(helpToText(help).substr(0, helpToText(help).find("=== topic 3")), help.first(4)); }),
                   "pages"));
    CHECK(contains(errorOf([&] { (void)dictionaryFromText("0 abcdefghijklmnop\n"); }), "15"));
}

TEST_CASE("dos mods: the packers reproduce every PAK frame and PAN file of the game") {
    TM_REQUIRE_DOS_FILES();
    int frames = 0, reproduced = 0;
    for (const FileIndex i : {FileIndex::SpritesPak, FileIndex::BeginPak, FileIndex::ClosePak, FileIndex::ShackPak}) {
        const auto b = dosFiles->bytes(i);
        const PakFile p = PakFile::load(b, "X");
        frames += p.count();
        reproduced += pakFramesReproduced(b, "X");
        CHECK(rebuildPak(b, {}, "X").file == std::vector<uint8_t>(b.begin(), b.end()));
        std::map<int, PakFrameEdit> same;
        same[3].image = pakFramePixels(p.frame(3));
        same[3].xOffset = p.frame(3).xOffset;
        CHECK(rebuildPak(b, same, "X").framesChanged == 0);
    }
    CHECK(frames == 524);
    CHECK(reproduced == 524);
    for (const FileIndex i : {FileIndex::OpeningPan, FileIndex::MountainPan, FileIndex::Mount2n3Pan, FileIndex::Mount3Pan,
                              FileIndex::ClosePan, FileIndex::ShackPan}) {
        const auto b = dosFiles->bytes(i);
        const auto again = encodePan(panContent(PanFile::load(b, "X")));
        CHECK_MESSAGE(std::equal(again.begin(), again.end(), b.begin(), b.end()), static_cast<int>(i));
    }
}

TEST_CASE("dos mods: changed frames and pictures are packed and decode back; bad pictures are refused") {
    TM_REQUIRE_DOS_FILES();
    const DosColours colours = dosPngColours(dosFiles->exe());
    // A frame: a pattern of the same size, through PNG.
    const auto sprites = dosFiles->bytes(FileIndex::SpritesPak);
    const PakFile pak = PakFile::load(sprites, "SPRITES.PAK");
    LogicalImage img = pakFramePixels(pak.frame(72));
    for (size_t k = 0; k < img.pixels.size(); ++k) img.pixels[k] = static_cast<uint8_t>((k / 3) % 16);
    const LogicalImage viaPng = pngToLogical(logicalToPng(img, colours, true), colours, true);
    CHECK(viaPng == img);
    std::map<int, PakFrameEdit> edits;
    edits[72].image = img;
    edits[72].yOffset = pak.frame(72).yOffset + 3;
    const PakRebuild rb = rebuildPak(sprites, edits, "SPRITES.PAK");
    CHECK(rb.framesChanged == 1);
    const PakFile back = PakFile::load(rb.file, "SPRITES.PAK");
    REQUIRE(back.count() == pak.count());
    CHECK(pakFramePixels(back.frame(72)) == img);
    CHECK(back.frame(72).yOffset == pak.frame(72).yOffset + 3);
    CHECK(pakFramePixels(back.frame(71)) == pakFramePixels(pak.frame(71)));
    CHECK(pakFramePixels(back.frame(222)) == pakFramePixels(pak.frame(222)));
    // Odd widths and missing frames are refused.
    LogicalImage odd = img;
    odd.width += 1;
    odd.pixels.resize(static_cast<size_t>(odd.width) * odd.height);
    std::map<int, PakFrameEdit> bad;
    bad[72].image = odd;
    CHECK(contains(errorOf([&] { (void)rebuildPak(sprites, bad, "SPRITES.PAK"); }), "odd"));
    std::map<int, PakFrameEdit> missing;
    missing[223].xOffset = 1;
    CHECK(contains(errorOf([&] { (void)rebuildPak(sprites, missing, "SPRITES.PAK"); }), "frame 223 does not exist"));

    // A picture with new tiles: a block of MOUNT2N3.PAN and the title picture.
    for (const FileIndex i : {FileIndex::Mount2n3Pan, FileIndex::OpeningPan}) {
        const auto pan = dosFiles->bytes(i);
        const PanContent c = panContent(PanFile::load(pan, "P"));
        const int k = c.pictures > 3 ? 3 : 0;
        LogicalImage pic = panPicture(c, k);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 40; ++x) pic.pixels[static_cast<size_t>(y + 20) * pic.width + x + 20] = static_cast<uint8_t>((x ^ y) & 15);
        const PanRebuild pr = rebuildPan(pan, {{k, pic}}, "P");
        CHECK(pr.picturesChanged == 1);
        const PanContent after = panContent(PanFile::load(pr.file, "P"));
        CHECK(panPicture(after, k) == pic);
        for (int o = 0; o < c.pictures; ++o)
            if (o != k) CHECK_MESSAGE(panPicture(after, o) == panPicture(c, o), o);
    }
    // MOUNTAIN.PAN fills the game's tile buffer (1849 of 1860 tiles): a block made of existing tiles
    // fits, a new pattern does not.
    const auto mountain = dosFiles->bytes(FileIndex::MountainPan);
    const PanContent mc = panContent(PanFile::load(mountain, "MOUNTAIN.PAN"));
    const PanRebuild copy = rebuildPan(mountain, {{3, panPicture(mc, 5)}}, "MOUNTAIN.PAN");
    CHECK(copy.picturesChanged == 1);
    CHECK(copy.tiles <= static_cast<int>(mc.tiles.size() / 32));
    CHECK(panPicture(panContent(PanFile::load(copy.file, "MOUNTAIN.PAN")), 3) == panPicture(mc, 5));
    LogicalImage busy = panPicture(mc, 3);
    for (size_t k = 0; k < busy.pixels.size(); ++k) busy.pixels[k] = static_cast<uint8_t>((k * 2654435761u >> 13) & 15);
    CHECK(contains(errorOf([&] { (void)rebuildPan(mountain, {{3, busy}}, "MOUNTAIN.PAN"); }), "tile buffer holds 1860"));
    LogicalImage small;
    small.width = small.height = 64;
    small.pixels.assign(64 * 64, 1);
    CHECK(contains(errorOf([&] { (void)rebuildPan(mountain, {{0, small}}, "MOUNTAIN.PAN"); }), "must be 128x128"));
    CHECK(contains(errorOf([&] { (void)rebuildPan(mountain, {{81, small}}, "MOUNTAIN.PAN"); }), "picture 81 does not exist"));
    std::vector<uint8_t> rgba(16 * 16 * 4, 0);  // fully transparent
    CHECK(contains(errorOf([&] { (void)pngToLogical(mischief::mods::encodePngRgba(16, 16, rgba), colours, false); }), "transparent"));
    const LogicalImage sprite = pngToLogical(mischief::mods::encodePngRgba(16, 16, rgba), colours, true);
    CHECK(sprite.pixels == std::vector<uint8_t>(256, 0));
    // An RGB picture: exact colours map to their index, others to the nearest.
    std::vector<uint8_t> rgb(2 * 4);
    rgb = {colours[9].r, colours[9].g, colours[9].b, 255, static_cast<uint8_t>(colours[12].r ^ 1), colours[12].g, colours[12].b, 255};
    const LogicalImage two = pngToLogical(mischief::mods::encodePngRgba(2, 1, rgb), colours, false);
    CHECK(two.pixels == std::vector<uint8_t>{9, 12});
    CHECK(two.remapped == 1);
}

TEST_CASE("dos mods: SST.EXE strings and palettes round-trip; the layout rules are enforced") {
    TM_REQUIRE_DOS_FILES();
    const DosExe& orig = dosFiles->exe();
    const DosStringCatalogue cat = DosStringCatalogue::read(orig);
    CHECK(cat.pools().size() == 6);
    for (const DosStringPool& p : cat.pools()) CHECK_MESSAGE(p.used <= static_cast<size_t>(p.end - p.start), p.name);
    CHECK(cat.entries().size() > 150);
    const DosFont font = DosFont::fromExe(orig);
    DosStringLayout layout;
    layout.font = &font;

    // Export -> import with no edits: nothing changes.
    {
        DosExe exe = orig;
        DosStringEdits edits;
        parseStringsJson(exportStringsJson(orig), cat, edits);
        CHECK(edits.size() == cat.entries().size());
        const DosStringsResult r = applyStringEdits(exe, cat, edits, layout);
        CHECK(r.errors.empty());
        CHECK(r.changed == 0);
        CHECK(hashOf(exe.image()) == hashOf(orig.image()));
        DosExe pal = orig;
        CHECK(applyPalettesJson(pal, exportPalettesJson(orig)) == 0);
        CHECK(hashOf(pal.image()) == hashOf(orig.image()));
    }
    auto msgPtr = [](const DosExe& e, int id) { return e.readFarPtr(static_cast<uint16_t>(dg::kFlutterText + 4 * id)).off; };
    // The hints pool is full (its bytes are the game's writable copy of the messages): a message
    // grows when another one shrinks; the pool is packed again and the pointers follow.
    {
        DosExe exe = orig;
        const uint16_t m4 = msgPtr(orig, 4), m5 = msgPtr(orig, 5);
        const std::string shorter = textOf(cat.find(m4)->text).substr(0, cat.find(m4)->text.size() - 21);
        const std::string longer = textOf(cat.find(m5)->text) + " and some more words.";
        DosStringEdits edits;
        edits[m4].text = bytesOf(shorter);
        edits[m5].text = bytesOf(longer);
        const DosStringsResult r = applyStringEdits(exe, cat, edits, layout);
        for (const auto& e : r.errors) MESSAGE(e);
        CHECK(r.errors.empty());
        CHECK(r.changed == 2);
        CHECK(exe.dsString(msgPtr(exe, 4)) == shorter);
        CHECK(exe.dsString(msgPtr(exe, 5)) == longer);
        for (int id = 0; id < 37; ++id)
            if (id != 4 && id != 5) CHECK_MESSAGE(exe.dsString(msgPtr(exe, id)) == orig.dsString(msgPtr(orig, id)), id);
        CHECK(msgPtr(exe, 5) == m5 - 21);
        CHECK(msgPtr(exe, 6) == msgPtr(orig, 6));
        DosExe full = orig;
        DosStringEdits more;
        more[m5].text = bytesOf(longer);
        CHECK(anyContains(applyStringEdits(full, cat, more, layout).errors, "21 more than their place"));
    }
    // Rules: a fixed place, the menu items, the champion's name, a box off the screen, a pool overflow.
    auto refused = [&](uint16_t ds, const std::string& text, std::string_view why) {
        DosExe exe = orig;
        DosStringEdits edits;
        edits[ds].text = bytesOf(text);
        const DosStringsResult r = applyStringEdits(exe, cat, edits, layout);
        CHECK_MESSAGE(anyContains(r.errors, why), why);
        CHECK(hashOf(exe.image()) == hashOf(orig.image()));
    };
    refused(dg::kStrChampion, textOf(cat.find(dg::kStrChampion)->text) + "S", "holds 8");
    refused(dg::kStrMenuFileItems, "One\rTwo", "items");
    refused(msgPtr(orig, 35), "Too short", "champion's name");
    refused(orig.readFarPtr(static_cast<uint16_t>(dg::kMsgBoxes + 16 * 5 + dg::boxrec::kText)).off,
            std::string(120, 'W'), "off the 320x200 screen");
    refused(msgPtr(orig, 0), std::string(5000, 'a'), "more than their place");
    refused(dg::kStrTitlePrompt, "~03504 x", "six digits");
    {  // countAt outside the text
        DosExe exe = orig;
        DosStringEdits edits;
        edits[msgPtr(orig, 26)].countAt = 500;
        CHECK(anyContains(applyStringEdits(exe, cat, edits, layout).errors, "countAt 500"));
    }
    CHECK(contains(errorOf([&] {
                       DosStringEdits e;
                       parseStringsJson(R"({"strings": {"DS:1234": {"text": "x"}}})", cat, e);
                   }),
                   "DS:1234"));
    CHECK(contains(errorOf([&] {
                       DosStringEdits e;
                       parseStringsJson(R"({"strings": {"DS:013A": {"text": "a{bad}"}}})", cat, e);
                   }),
                   "{bad}"));
    DosExe pal = orig;
    CHECK(contains(errorOf([&] { (void)applyPalettesJson(pal, R"({"ega": [64]})"); }), "17 entries"));
    std::string ega = R"({"tandy": [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,16]})";
    CHECK(contains(errorOf([&] { (void)applyPalettesJson(pal, ega); }), "tandy[15]"));
    CHECK(applyPalettesJson(pal, R"({"cga": [null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,255]})") <= 1);
    CHECK(pal.ds8(static_cast<uint16_t>(dg::kPalCga + 15)) == 255);
}

TEST_CASE("dos mods: the exported kit imports to byte-identical files and DGROUP") {
    TM_REQUIRE_DOS_FILES();
    TempDir kit("tm_dos_kit");
    const DosModKitReport r = exportDosModKit(*dosFiles, kit.p / "kit");
    for (const auto& f : r.failures) MESSAGE(f);
    CHECK(r.failures.empty());
    CHECK(r.verified == 18);  // 17 data files (all but USERS.DAT) and the DGROUP
    CHECK(r.frames == 524);
    CHECK(r.framesReproduced == 524);
    CHECK(r.picturesReproduced == 6);
    CHECK(r.counts.at("PAK") == 524 + 4);
    CHECK(r.counts.at("SOUND") == 4);
    CHECK(r.counts.at("TEXT") == 3);
    CHECK(r.counts.at("EXE") == 2);
    CHECK(contains(errorOf([&] { (void)exportDosModKit(*dosFiles, kit.p / "kit"); }), "not empty"));

    // The kit installed as a mod: DosFiles::open serves exactly the originals.
    const DosModSet set({scanDosModFolder(kit.p / "kit")});
    const DosFiles modded = DosFiles::open(dosFiles->folder(), &set);
    CHECK(set.lastReport().errors.empty());
    CHECK(set.lastReport().changedFiles.empty());
    CHECK(!set.lastReport().exeChanged);
    for (int i = 0; i < FileTable::kCount; ++i)
        CHECK_MESSAGE(hashOf(modded.fileSet()[static_cast<size_t>(i)]) == hashOf(dosFiles->fileSet()[static_cast<size_t>(i)]), i);
    CHECK(hashOf(modded.exe().image()) == hashOf(dosFiles->exe().image()));
}

TEST_CASE("dos mods: a sprite frame, a tune, a question and a message of a mod folder are served") {
    TM_REQUIRE_DOS_FILES();
    TempDir dir("tm_dos_mod");
    const DosExe& exe = dosFiles->exe();
    const DosColours colours = dosPngColours(exe);
    const PakFile sprites = PakFile::load(dosFiles->bytes(FileIndex::SpritesPak), "SPRITES.PAK");
    LogicalImage frame = pakFramePixels(sprites.frame(72));
    for (size_t k = 0; k < frame.pixels.size(); ++k) frame.pixels[k] = static_cast<uint8_t>(k % 7 + 1);
    put(dir.p / "PAK" / "SPRITES" / "072.png", logicalToPng(frame, colours, true));
    putText(dir.p / "PAK" / "SPRITES.json", R"({"frames": {"72": {"x": 5}}})");
    std::string tunes = soundFileToText(dosFiles->bytes(FileIndex::TunesTan), FileIndex::TunesTan, "TUNES.TAN");
    tunes = replaceFirst(tunes, "tempo ", "tempo 99\n      # was: tempo ", "tune 0");
    putText(dir.p / "SOUND" / "TUNES.TAN.txt", tunes);
    const Dictionary dict = Dictionary::parse(dosFiles->bytes(FileIndex::PrgDict));
    std::string puz = puzzlesToText(PuzzleFile::parse(dosFiles->bytes(FileIndex::AllPuzs)), dict, exe);
    const size_t answers = puz.find("--- answers\n", puz.find("=== question 0")) + 12;
    puz.replace(answers, puz.find('\n', answers) - answers, "A test answer");
    putText(dir.p / "TEXT" / "ALL_PUZS.txt", puz);
    const uint16_t m3 = exe.readFarPtr(static_cast<uint16_t>(dg::kFlutterText + 12)).off;
    putText(dir.p / "EXE" / "strings.json", std::format(R"({{"strings": {{"DS:{:04X}": {{"text": "Hello\rfrom a test mod"}}}}}})", m3));
    putText(dir.p / "README.txt", "not read");

    auto checkServed = [&](const DosFiles& f) {
        const PakFile p = PakFile::load(f.bytes(FileIndex::SpritesPak), "SPRITES.PAK");
        CHECK(pakFramePixels(p.frame(72)) == frame);
        CHECK(p.frame(72).xOffset == 5);
        CHECK(p.frame(72).yOffset == sprites.frame(72).yOffset);
        CHECK(f.bytes(FileIndex::TunesTan).size() == dosFiles->bytes(FileIndex::TunesTan).size());
        CHECK(hashOf(f.bytes(FileIndex::TunesTan)) != hashOf(dosFiles->bytes(FileIndex::TunesTan)));
        const PuzzleFile q = PuzzleFile::parse(f.bytes(FileIndex::AllPuzs));
        CHECK(q.puzzle(0, Dictionary::parse(f.bytes(FileIndex::PrgDict))).answers[0] == "A test answer");
        CHECK(q.puzzle(1, dict).answers[0] == PuzzleFile::parse(dosFiles->bytes(FileIndex::AllPuzs)).puzzle(1, dict).answers[0]);
        const uint16_t now = f.exe().readFarPtr(static_cast<uint16_t>(dg::kFlutterText + 12)).off;
        CHECK(f.exe().dsString(now) == "Hello\rfrom a test mod");
        CHECK(hashOf(f.bytes(FileIndex::MountainPan)) == hashOf(dosFiles->bytes(FileIndex::MountainPan)));
    };
    const DosModSet set({scanDosModFolder(dir.p)});
    checkServed(DosFiles::open(dosFiles->folder(), &set));
    const DosModsReport rep = set.lastReport();
    for (const auto& e : rep.errors) MESSAGE(e);
    CHECK(rep.errors.empty());
    CHECK(rep.applied == 5);
    CHECK(rep.changedFiles.size() == 3);
    CHECK(rep.exeChanged);

    // The same through the start-up's hook (installDosMods + DosFiles::open(folder)); the guard
    // removes it again whatever happens, so no other test sees the mod.
    {
        struct Uninstall {
            ~Uninstall() { uninstallDosMods(); }
        } guard;
        const DosModsReport inst = installDosMods({dir.p / "missing", dir.p});
        CHECK(inst.folders.size() == 1);
        CHECK(dosOverrides() != nullptr);
        checkServed(DosFiles::open(dosFiles->folder()));
    }
    CHECK(dosOverrides() == nullptr);
    CHECK(hashOf(DosFiles::open(dosFiles->folder()).bytes(FileIndex::SpritesPak)) == hashOf(dosFiles->bytes(FileIndex::SpritesPak)));
    CHECK(installDosMods({dir.p / "missing"}).folders.empty());
    CHECK(dosOverrides() == nullptr);
}

TEST_CASE("dos mods: malformed mod files are reported and the game's own data is used") {
    TM_REQUIRE_DOS_FILES();
    TempDir dir("tm_dos_bad");
    const DosColours colours = dosPngColours(dosFiles->exe());
    LogicalImage wrong;
    wrong.width = 10;
    wrong.height = 4;
    wrong.pixels.assign(40, 3);
    put(dir.p / "PAK" / "SPRITES" / "072.png", logicalToPng(wrong, colours, true));
    put(dir.p / "PAK" / "SPRITES" / "999.png", logicalToPng(wrong, colours, true));
    putText(dir.p / "PAK" / "BEGIN.json", R"({"frames": {"3": {"q": 1}}})");
    put(dir.p / "PAK" / "OPENING" / "000.png", logicalToPng(wrong, colours, true));
    put(dir.p / "PAN" / "MOUNTAIN" / "000.png", logicalToPng(wrong, colours, false));
    putText(dir.p / "SOUND" / "SFX.IBM.txt", "effect 0\n  C4 1/3\n  end\n");
    putText(dir.p / "TEXT" / "HELP.DAT.txt", "=== topic 0 page 0\nonly one page\n");
    putText(dir.p / "TEXT" / "PRG_DICT.txt", "0 the\n");
    putText(dir.p / "EXE" / "strings.json", R"({"strings": {"DS:0001": {"text": "x"}}})");
    putText(dir.p / "EXE" / "palettes.json", R"({"ega": "red"})");
    putText(dir.p / "FILES" / "USERS.DAT", "players");
    putText(dir.p / "FILES" / "SPRITES.PAK", "not a pack");
    putText(dir.p / "FILES" / "SST.EXE", "not an exe");
    putText(dir.p / "PMAP" / "1000.png", "a Windows mod file");
    putText(dir.p / "mod.json", "{broken");

    const DosModSet set({scanDosModFolder(dir.p)});
    const DosFiles f = DosFiles::open(dosFiles->folder(), &set);
    const DosModsReport rep = set.lastReport();
    for (int i = 0; i < FileTable::kCount; ++i)
        CHECK_MESSAGE(hashOf(f.fileSet()[static_cast<size_t>(i)]) == hashOf(dosFiles->fileSet()[static_cast<size_t>(i)]), i);
    CHECK(hashOf(f.exe().image()) == hashOf(dosFiles->exe().image()));
    CHECK(rep.applied == 0);
    const auto& e = rep.errors;
    CHECK(anyContains(e, "10x4 pixels; frame 72 of SPRITES.PAK"));
    CHECK(anyContains(e, "SPRITES.PAK has frames 0-222"));
    CHECK(anyContains(e, "unknown field (x, y, a, c, e)"));
    CHECK(anyContains(e, "OPENING.PAK is not a sprite pack"));
    CHECK(anyContains(e, "must be 128x128"));
    CHECK(anyContains(e, "'1/3' is not a duration"));
    CHECK(anyContains(e, "topics of 6, 4, 4 and 1 pages"));
    CHECK(anyContains(e, "1 entries; the table needs 128 to 208"));
    CHECK(anyContains(e, "\"DS:0001\" is not a string"));
    CHECK(anyContains(e, "\"ega\" must be an array"));
    CHECK(anyContains(e, "USERS.DAT holds the players"));
    CHECK(anyContains(e, "SPRITES.PAK: too short"));
    CHECK(anyContains(e, "SST.EXE cannot be replaced"));
    CHECK(anyContains(e, "mod.json"));
    CHECK(anyContains(rep.warnings, "PMAP: unknown folder"));

    // A resizable frame takes the new size.
    putText(dir.p / "mod.json", R"({"resizable": ["SPRITES.PAK:72"]})");
    std::filesystem::remove_all(dir.p / "FILES");
    const DosModSet set2({scanDosModFolder(dir.p)});
    const DosFiles f2 = DosFiles::open(dosFiles->folder(), &set2);
    const PakFile p = PakFile::load(f2.bytes(FileIndex::SpritesPak), "SPRITES.PAK");
    CHECK(p.frame(72).widthPixels() == 10);
    CHECK(p.frame(72).height == 4);
}

TEST_CASE("dos mods: --check-mods and --export-mod-kit work on a DOS folder") {
    TM_REQUIRE_DOS_FILES();
    TempDir dir("tm_dos_cli");
    putText(dir.p / "mod" / "EXE" / "strings.json",
            std::format(R"({{"strings": {{"DS:{:04X}": {{"text": "x"}}}}}})", static_cast<int>(dg::kStrBtnYes)));
    putText(dir.p / "bad" / "SOUND" / "TUNES.IBM.txt", "tune 0\n");
    DosModsCommand c;
    c.dataArg = mischief::pathToUtf8(dosFiles->folder());
    c.paths = save::Paths::under(dir.p / "pref", dir.p / "exe");
    c.compare = true;
    c.modDirs = {dir.p / "mod"};
    CHECK(runDosModsCommand(c) == 0);
    c.modDirs = {dir.p / "bad"};
    CHECK(runDosModsCommand(c) == 1);
    c.dataArg = mischief::pathToUtf8(dir.p);  // no DOS edition there
    CHECK(runDosModsCommand(c) == 3);

    // The routing of the mods tools: --edition dos (and a --data that names a DOS folder).
    const std::string data = mischief::pathToUtf8(dosFiles->folder()), mod = mischief::pathToUtf8(dir.p / "mod");
    std::vector<std::string> args = {"tmport", "--check-mods", mod, "--data", data};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    CHECK(mods::runModsCommand(static_cast<int>(argv.size()), argv.data()) == std::optional<int>(0));
    std::vector<std::string> bad = {"tmport", "--check-mods", "--edition", "amiga"};
    std::vector<char*> badv;
    for (auto& a : bad) badv.push_back(a.data());
    CHECK(mods::runModsCommand(static_cast<int>(badv.size()), badv.data()) == std::optional<int>(2));
}

TEST_CASE("dos mods: a CLOSE.PAN that unpacks over MOUNT3's chute blocks is warned about") {
    TM_REQUIRE_DOS_FILES();
    TempDir dir("tm_dos_chute");
    const DosColours colours = dosPngColours(dosFiles->exe());
    const PanContent close = panContent(PanFile::load(dosFiles->bytes(FileIndex::ClosePan), "CLOSE.PAN"));
    // Two blocks of noise: about 500 new tiles that do not compress.
    for (int k = 0; k < 2; ++k) {
        LogicalImage pic = panPicture(close, k);
        uint32_t x = 12345u + static_cast<uint32_t>(k);
        for (auto& v : pic.pixels) {
            x = x * 1103515245u + 12345u;
            v = static_cast<uint8_t>((x >> 16) & 15);
        }
        put(dir.p / "PAN" / "CLOSE" / std::format("{:03}.png", k), logicalToPng(pic, colours, false));
    }
    const DosModSet set({scanDosModFolder(dir.p)});
    const DosFiles f = DosFiles::open(dosFiles->folder(), &set);
    const DosModsReport rep = set.lastReport();
    CHECK(rep.errors.empty());
    CHECK(rep.changedFiles == std::vector<std::string>{"CLOSE.PAN"});
    CHECK(anyContains(rep.warnings, "castle chute"));
    // The unmodded files leave the chute alone: no warning for the kit's own CLOSE pictures.
    TempDir same("tm_dos_chute_same");
    put(same.p / "PAN" / "CLOSE" / "000.png", logicalToPng(panPicture(close, 0), colours, false));
    const DosModSet set2({scanDosModFolder(same.p)});
    (void)DosFiles::open(dosFiles->folder(), &set2);
    CHECK(!anyContains(set2.lastReport().warnings, "castle chute"));
}
