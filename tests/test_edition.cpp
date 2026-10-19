// Editions (edition/edition.h): detection in temporary folders with dummy files and, with
// TM_DATA_DOS, the user's DOS folder; the DOS options and the original's switches
// (edition/dos_options.h); the start-up's edition choice and a headless run of the DOS entry
// point (app/bootstrap.h). Headless only: no window, no audio.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/bootstrap.h"
#include "app/launch_options.h"
#include "app_test_util.h"
#include "dos_test_data.h"
#include "edition/dos_options.h"
#include "edition/edition.h"
#include "save/settings.h"
#include "util/file_util.h"

using namespace tmport;
namespace fs = std::filesystem;

namespace {

void touch(const fs::path& p, size_t bytes = 16) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    for (size_t i = 0; i < bytes; ++i) f.put(static_cast<char>(i * 7));
}

// A DOS folder with the three marker files; SST.EXE is the user's (TM_DATA_DOS) when real is set,
// else dummy bytes (refused by DosExe).
fs::path makeDosFolder(const fs::path& dir, bool real) {
    fs::create_directories(dir);
    if (real) {
        std::error_code ec;
        fs::copy_file(*testdata::dosDataPath() / "SST.EXE", dir / "SST.EXE", fs::copy_options::overwrite_existing, ec);
        REQUIRE(!ec);
    } else {
        touch(dir / "SST.EXE", 100);
    }
    touch(dir / "SPRITES.PAK");
    touch(dir / "ALL_PUZS");
    return dir;
}

bool contains(const std::string& s, std::string_view part) { return s.find(part) != std::string::npos; }

bool samePath(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

// Width and height from a PNG's IHDR.
std::pair<uint32_t, uint32_t> pngSize(const fs::path& p) {
    auto data = readWholeFile(p);
    if (!data || data->size() < 24) return {0, 0};
    auto be32 = [&](size_t o) {
        return (uint32_t((*data)[o]) << 24) | (uint32_t((*data)[o + 1]) << 16) | (uint32_t((*data)[o + 2]) << 8) | (*data)[o + 3];
    };
    return {be32(16), be32(20)};
}

}  // namespace

// --------------------------------------------------------------------------------- detection

TEST_CASE("edition: keys and names") {
    CHECK(parseEditionKey("win") == Edition::WinCd);
    CHECK(parseEditionKey("WinCD") == Edition::WinCd);
    CHECK(parseEditionKey("windows") == Edition::WinCd);
    CHECK(parseEditionKey("dos") == Edition::Dos1990);
    CHECK(parseEditionKey("DOS1990") == Edition::Dos1990);
    CHECK_FALSE(parseEditionKey("auto"));
    CHECK_FALSE(parseEditionKey(""));
    CHECK_FALSE(parseEditionKey("mac"));
    CHECK(editionKey(Edition::WinCd) == "win");
    CHECK(editionKey(Edition::Dos1990) == "dos");
    CHECK(parseEditionKey(editionKey(Edition::Dos1990)) == Edition::Dos1990);
    CHECK(contains(std::string(editionName(Edition::Dos1990)), "DOS"));
}

TEST_CASE("edition: detection in folders with dummy files") {
    const fs::path base = apptest::outDir("edition_dummy");

    SUBCASE("an empty folder is handed to the Windows start-up unchanged") {
        const EditionScan s = scanEditions(base);
        CHECK_FALSE(s.win);
        CHECK_FALSE(s.dos);
        const EditionCandidate c = detectEdition(base);
        CHECK(c.edition == Edition::WinCd);
        CHECK(c.path == base);
        CHECK_THROWS_AS(detectEdition(base, Edition::Dos1990), EditionError);
        try {
            detectEdition(base, Edition::Dos1990);
        } catch (const EditionError& e) {
            CHECK(e.file() == "SST.EXE");
            CHECK(contains(e.what(), "SST.EXE"));
        }
    }
    SUBCASE("Windows: game folder, TMTWINCD subfolder, single CD image") {
        touch(base / "a" / "TMTWINCD.EXE");
        auto s = scanEditions(base / "a");
        REQUIRE(s.win);
        CHECK(s.win->path == base / "a");
        touch(base / "b" / "tmtwincd" / "tmtwincd.exe");
        s = scanEditions(base / "b");
        REQUIRE(s.win);
        CHECK(samePath(s.win->path, base / "b" / "tmtwincd"));
        touch(base / "c" / "game.cue");
        s = scanEditions(base / "c");
        REQUIRE(s.win);
        CHECK(s.win->path == base / "c" / "game.cue");
        CHECK(scanEditions(base / "c" / "game.cue").win);
        touch(base / "c" / "other.iso");
        CHECK_FALSE(scanEditions(base / "c").win);  // two images: not chosen
        CHECK_FALSE(scanEditions(base / "c").dos);
        // Auto keeps the path as given (exactly the Windows start-up of before).
        CHECK(detectEdition(base / "b").path == base / "b");
        CHECK(samePath(detectEdition(base / "b", Edition::WinCd).path, base / "b" / "tmtwincd"));
    }
    SUBCASE("DOS: missing marker files and a foreign SST.EXE are refused with a message") {
        touch(base / "d" / "sst.exe", 100);
        auto s = scanEditions(base / "d");
        REQUIRE(s.dos);
        CHECK_FALSE(s.dos->usable());
        CHECK(s.dos->missingFile == "SPRITES.PAK");
        CHECK(contains(s.dos->problem, "SPRITES.PAK"));
        touch(base / "d" / "sprites.pak");
        s = scanEditions(base / "d");
        CHECK(s.dos->missingFile == "ALL_PUZS");
        touch(base / "d" / "all_puzs");  // lower case: found too
        s = scanEditions(base / "d");
        REQUIRE(s.dos);
        CHECK_FALSE(s.dos->usable());
        CHECK(s.dos->missingFile == "SST.EXE");
        CHECK(contains(s.dos->problem, "version 1.0"));
        // Only a DOS candidate: auto picks it and reports why it cannot be played.
        CHECK_THROWS_AS(detectEdition(base / "d"), EditionError);
        try {
            detectEdition(base / "d");
        } catch (const EditionError& e) {
            CHECK(e.file() == "SST.EXE");
        }
        // The SST.EXE file itself as the path: its folder.
        s = scanEditions(base / "d" / "sst.exe");
        REQUIRE(s.dos);
        CHECK(s.dos->path == base / "d");
    }
    SUBCASE("a broken DOS folder next to the Windows data does not stop the Windows edition") {
        makeDosFolder(base / "e" / "TM-DOS", false);
        touch(base / "e" / "TMTWINCD" / "TMTWINCD.EXE");
        const auto s = scanEditions(base / "e");
        REQUIRE(s.dos);
        REQUIRE(s.win);
        CHECK(samePath(s.dos->path, base / "e" / "TM-DOS"));
        const EditionCandidate c = detectEdition(base / "e");
        CHECK(c.edition == Edition::WinCd);
        CHECK(c.path == base / "e");
        CHECK_THROWS_AS(detectEdition(base / "e", Edition::Dos1990), EditionError);
    }
}

TEST_CASE("edition: the user's DOS folder (TM_DATA_DOS)") {
    TM_REQUIRE_DOS_DATA();
    const fs::path dosDir = *testdata::dosDataPath();
    const EditionCandidate own = detectEdition(dosDir);
    CHECK(own.edition == Edition::Dos1990);
    CHECK(own.usable());
    CHECK(samePath(own.path, dosDir));
    CHECK(checkDosFolder(dosDir).usable());

    const fs::path base = apptest::outDir("edition_real");
    makeDosFolder(base / "TM-DOS", true);
    // A parent folder with only the DOS edition: found in the subfolder.
    EditionCandidate c = detectEdition(base);
    CHECK(c.edition == Edition::Dos1990);
    CHECK(samePath(c.path, base / "TM-DOS"));
    // Both editions: an error naming both, unless an edition is chosen.
    touch(base / "TMTWINCD" / "TMTWINCD.EXE");
    CHECK_THROWS_AS(detectEdition(base), EditionError);
    try {
        detectEdition(base);
    } catch (const EditionError& e) {
        const std::string m = e.what();
        CHECK(contains(m, "Windows CD edition"));
        CHECK(contains(m, "1990 DOS edition"));
        CHECK(contains(m, pathToUtf8(base / "TM-DOS")));
        CHECK(contains(m, "--edition"));
        CHECK(e.file().empty());
    }
    c = detectEdition(base, Edition::Dos1990);
    CHECK(c.edition == Edition::Dos1990);
    CHECK(samePath(c.path, base / "TM-DOS"));
    c = detectEdition(base, Edition::WinCd);
    CHECK(c.edition == Edition::WinCd);
    CHECK(samePath(c.path, base / "TMTWINCD"));
    // A CD image counts as the Windows edition too.
    fs::remove_all(base / "TMTWINCD");
    touch(base / "cd.cue");
    CHECK_THROWS_AS(detectEdition(base), EditionError);
    // A truncated SST.EXE is refused with DosExe's message.
    {
        auto data = readWholeFile(dosDir / "SST.EXE");
        REQUIRE(data);
        data->resize(data->size() / 2);
        const fs::path cut = base / "cut";
        makeDosFolder(cut, false);
        REQUIRE(writeWholeFile(cut / "SST.EXE", *data));
        const EditionCandidate bad = checkDosFolder(cut);
        CHECK_FALSE(bad.usable());
        CHECK(contains(bad.problem, "SST.EXE"));
        MESSAGE("truncated SST.EXE: " << bad.problem);
    }
    // Auto-detection finds a folder with SST.EXE (the repository's TM-DOS from the test directory,
    // or nothing on other set-ups).
    if (auto p = autoDetectDosPath({})) CHECK(scanEditions(*p).dos);
}

// ------------------------------------------------------------------------------- DOS options

TEST_CASE("dos options: values and the original switches (1460:0463)") {
    using namespace tmport::dos;
    CHECK(parseLookOption("EGA") == Look::Ega);
    CHECK(parseLookOption("tandy") == Look::Tandy);
    CHECK(parseLookOption("mcga") == Look::Mcga);
    CHECK(parseLookOption("cga") == Look::Cga);
    CHECK(parseLookOption("Hercules") == Look::Cga);  // drawn like CGA
    CHECK_FALSE(parseLookOption("svga"));
    CHECK(parseSoundDevice("speaker") == SoundDevice::Speaker);
    CHECK(parseSoundDevice("Tandy") == SoundDevice::Tandy);
    CHECK(parseSoundDevice("adlib") == SoundDevice::AdLib);
    CHECK(parseSoundDevice("off") == SoundDevice::Off);
    CHECK_FALSE(parseSoundDevice("gus"));
    CHECK(parseFrameSpeed("reference") == FrameSpeed::Reference);
    CHECK(parseFrameSpeed("period") == FrameSpeed::Period);
    CHECK_FALSE(parseFrameSpeed("fast"));
    for (Look l : {Look::Ega, Look::Cga, Look::Tandy, Look::Mcga}) CHECK(parseLookOption(lookName(l)) == l);
    for (SoundDevice d : {SoundDevice::Speaker, SoundDevice::Tandy, SoundDevice::AdLib, SoundDevice::Off})
        CHECK(parseSoundDevice(iniValue(d)) == d);
    CHECK(videoTypeOf(Look::Ega) == 1);
    CHECK(videoTypeOf(Look::Cga) == 2);
    CHECK(videoTypeOf(Look::Tandy) == 3);
    CHECK(videoTypeOf(Look::Mcga) == 5);
    CHECK(soundDeviceOf(SoundDevice::Speaker) == 0);
    CHECK(soundDeviceOf(SoundDevice::Tandy) == 1);
    CHECK(soundDeviceOf(SoundDevice::AdLib) == 2);

    DosOptions o;
    CHECK(applyDosSwitches(o, "-T -St").empty());
    CHECK(o.look == Look::Tandy);
    CHECK(o.sound == SoundDevice::Tandy);
    CHECK(applyDosSwitches(o, "/e  -SI").empty());  // '/' works, either case
    CHECK(o.look == Look::Ega);
    CHECK(o.sound == SoundDevice::Speaker);
    applyDosSwitches(o, "-Sa");
    CHECK(o.sound == SoundDevice::AdLib);
    o.sound = SoundDevice::Tandy;
    applyDosSwitches(o, "-Ss");
    CHECK(o.sound == SoundDevice::AdLib);
    // Only the second character counts; later switches win.
    applyDosSwitches(o, "-EGA -CGA");
    CHECK(o.look == Look::Cga);
    applyDosSwitches(o, "-M");
    CHECK(o.look == Look::Mcga);
    applyDosSwitches(o, "-hercules");
    CHECK(o.look == Look::Cga);
    // -X toggles.
    CHECK_FALSE(o.slowCpu);
    applyDosSwitches(o, "-X");
    CHECK(o.slowCpu);
    applyDosSwitches(o, "-x -X -x");
    CHECK_FALSE(o.slowCpu);
    // No effect: -S without a known device, unknown letters, non-switches.
    const DosOptions before = o;
    const auto ignored = applyDosSwitches(o, "-S -Sq -Z foo -");
    CHECK(o == before);
    REQUIRE(ignored.size() == 5);
    CHECK(ignored[0] == "-S");
    CHECK(ignored[3] == "foo");
    CHECK(applyDosSwitches(o, std::vector<std::string>{"-T", "-St"}).empty());
    CHECK(o.look == Look::Tandy);

    DosSettings s;
    s.look = Look::Mcga;
    s.slowCpu = true;
    const DosOptions fromS = dosOptionsFrom(s);
    CHECK(fromS.look == Look::Mcga);
    CHECK(fromS.slowCpu);
    CHECK(fromS.sound == SoundDevice::Tandy);
}

// ------------------------------------------------------------------------------- start-up

TEST_CASE("bootstrap: edition choice from --data, --edition and [engine] edition") {
    const fs::path base = apptest::outDir("edition_boot");
    save::EngineSettings settings;
    app::LaunchOptions o;

    o.dataPath = pathToUtf8(base);  // empty folder: the Windows start-up reports it as before
    app::EditionChoice c = app::selectEdition(o, settings, {});
    CHECK(c.edition == Edition::WinCd);
    CHECK(c.dataPath == o.dataPath);

    makeDosFolder(base / "broken", false);
    o.dataPath = pathToUtf8(base / "broken");
    CHECK_THROWS_AS(app::selectEdition(o, settings, {}), app::StartupError);
    try {
        app::selectEdition(o, settings, {});
    } catch (const app::StartupError& e) {
        CHECK(e.file() == "SST.EXE");
        CHECK(contains(e.what(), "TM_DATA_DOS"));  // how to point at the DOS folder
    }
    o.edition = "win";  // --edition win: the Windows start-up (which will name TMTWINCD.EXE)
    c = app::selectEdition(o, settings, {});
    CHECK(c.edition == Edition::WinCd);

    if (!testdata::dosDataPath()) {
        MESSAGE("TM_DATA_DOS is not set: the rest of the test is skipped");
        return;
    }
    makeDosFolder(base / "both" / "TM-DOS", true);
    touch(base / "both" / "TMTWINCD" / "TMTWINCD.EXE");
    o = {};
    o.dataPath = pathToUtf8(base / "both");
    CHECK_THROWS_AS(app::selectEdition(o, settings, {}), app::StartupError);
    settings.edition = Edition::Dos1990;  // [engine] edition = dos
    c = app::selectEdition(o, settings, {});
    CHECK(c.edition == Edition::Dos1990);
    CHECK(samePath(pathFromUtf8(c.dataPath), base / "both" / "TM-DOS"));
    o.edition = "win";  // the command line wins over tmport.ini
    c = app::selectEdition(o, settings, {});
    CHECK(c.edition == Edition::WinCd);
    CHECK(samePath(pathFromUtf8(c.dataPath), base / "both" / "TMTWINCD"));
    o.edition = "auto";
    CHECK_THROWS_AS(app::selectEdition(o, settings, {}), app::StartupError);
    // --edition dos without --data: TM_DATA_DOS.
    o = {};
    o.edition = "dos";
    settings = {};
    c = app::selectEdition(o, settings, {});
    CHECK(c.edition == Edition::Dos1990);
    CHECK(samePath(pathFromUtf8(c.dataPath), *testdata::dosDataPath()));
}

TEST_CASE("bootstrap: DOS options and host from tmport.ini and the command line") {
    save::EngineSettings s = save::EngineSettings::fromIni(save::IniFile::parse(
        "[engine]\ninteger_scale = true\nfullscreen = true\npause_on_focus_loss = true\n"
        "[dos]\nlook = mcga\nsound = speaker\nslow_cpu = true\nframe_speed = period\nwindow_scale = 4\n"));
    app::LaunchOptions o;
    dos::DosOptions d = app::resolveDosOptions(o, s, "P:/pref");
    CHECK(d.look == dos::Look::Mcga);
    CHECK(d.sound == dos::SoundDevice::Speaker);
    CHECK(d.slowCpu);
    CHECK(d.frameSpeed == dos::FrameSpeed::Period);
    CHECK(d.prefDir == fs::path("P:/pref"));
    CHECK_FALSE(d.headless);
    // --dos-args over tmport.ini (-X toggles the ini's slow_cpu), the named options over both.
    o.dosArgs = "-T -X -Sa";
    d = app::resolveDosOptions(o, s, {});
    CHECK(d.look == dos::Look::Tandy);
    CHECK_FALSE(d.slowCpu);
    CHECK(d.sound == dos::SoundDevice::AdLib);
    o.dosLook = "cga";
    o.dosSlowCpu = true;
    o.frameSpeed = "reference";
    o.headless = true;
    o.keys = "enter";
    d = app::resolveDosOptions(o, s, {});
    CHECK(d.look == dos::Look::Cga);
    CHECK(d.slowCpu);
    CHECK(d.frameSpeed == dos::FrameSpeed::Reference);
    CHECK(d.headless);
    CHECK(d.keys == "enter");
    o.noAudio = true;
    CHECK(app::resolveDosOptions(o, s, {}).sound == dos::SoundDevice::Off);

    platform::HostConfig h = app::dosHostConfig(app::LaunchOptions{}, s, "S:/shots");
    CHECK(h.layout == gfx::PresenterLayout::dos1990());
    CHECK(h.windowScale == 4);
    CHECK(h.integerScale);
    CHECK(h.fullscreen);
    CHECK(h.pauseOnFocusLoss);
    CHECK(h.screenshotDir == fs::path("S:/shots"));
    app::LaunchOptions o2;
    o2.windowed = true;
    o2.noPause = true;
    o2.squarePixels = true;
    o2.screenshotDir = "T:/shots";
    h = app::dosHostConfig(o2, save::EngineSettings{}, "S:/shots");
    CHECK(h.layout == gfx::PresenterLayout::dos1990(true));
    CHECK(h.windowScale == 3);
    CHECK_FALSE(h.integerScale);
    CHECK_FALSE(h.fullscreen);
    CHECK_FALSE(h.pauseOnFocusLoss);
    CHECK(h.screenshotDir == fs::path("T:/shots"));
}

TEST_CASE("bootstrap: headless run of the DOS edition writes the 320x200 frame dump") {
    TM_REQUIRE_DOS_DATA();
    const bool videoBefore = SDL_WasInit(SDL_INIT_VIDEO) != 0;
    const bool audioBefore = SDL_WasInit(SDL_INIT_AUDIO) != 0;
    const fs::path dir = apptest::outDir("edition_dos_run");
    std::ofstream(dir / "tmport.ini") << "[saves]\nimported_original = true\n";
    const fs::path dump = dir / "dos_end.png";
    app::LaunchOptions o;
    o.headless = true;
    o.edition = "dos";
    o.dataPath = pathToUtf8(*testdata::dosDataPath());
    o.prefDir = pathToUtf8(dir);
    o.configPath = pathToUtf8(dir / "tmport.ini");
    o.frameDump = pathToUtf8(dump);
    std::vector<std::string> errors;
    // With no --keys script the headless run ends at once: the DOS runtime shows the first screen
    // and its empty script is used up.
    const int rc = app::runGame(o, {}, [&](const app::StartupError& e) { errors.push_back(e.what()); });
    CHECK(rc == 0);
    CHECK(errors.empty());
    for (const auto& e : errors) MESSAGE(e);
    REQUIRE(fs::exists(dump));
    CHECK(pngSize(dump) == std::pair<uint32_t, uint32_t>{320, 200});
    if (!videoBefore) CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    if (!audioBefore) CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);

    // A broken DOS folder: the start-up names SST.EXE and does not open a window.
    const fs::path broken = makeDosFolder(dir / "broken", false);
    o.dataPath = pathToUtf8(broken);
    errors.clear();
    CHECK(app::runGame(o, {}, [&](const app::StartupError& e) { errors.push_back(e.what()); }) == 3);
    REQUIRE(errors.size() == 1);
    CHECK(contains(errors[0], "SST.EXE"));
    if (!videoBefore) CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
}
