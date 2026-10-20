// The ggport shell: the original's switches (Init_CommandLine 303a:005c), the engine options,
// ggport.ini, the window icon from an .ico file, the tool dispatcher and headless runs of the app
// with frame dumps (re/gg/docs/PORTING_NOTES.md §7, code_runtime.md §2). Runs headless; every run
// passes a scratch --pref-dir. The runs and the checks against ssg.exe need GG_DATA.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "gg/app/cli_dispatch.h"
#include "gg/app/gg_app.h"
#include "gg/app/icon.h"
#include "gg/app/options.h"
#include "gg/app/settings.h"
#include "gg/app/test_pattern.h"
#include "gg/data/dgroup_offsets.h"
#include "gg_test_data.h"
#include "mods/png_codec.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

OriginalSwitches sw(std::vector<std::string> a) { return parseOriginalSwitches(a); }

std::vector<std::filesystem::path> pngsIn(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ".png") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

// ------------------------------------------------------------------------------- the switches

TEST_CASE("gg switches: -S, -GV and -? as Init_CommandLine reads them") {
    CHECK(sw({}).soundDevice == -1);
    CHECK(sw({"-S"}).soundDevice == 0);
    CHECK(sw({"-S-"}).soundDevice == 0);
    CHECK(sw({"/sn"}).soundDevice == 0);
    CHECK(sw({"-SI"}).soundDevice == 1);
    CHECK(sw({"-sa"}).soundDevice == 4);
    CHECK(sw({"-SB"}).soundDevice == 4);
    CHECK(sw({"/SM"}).soundDevice == 4);
    CHECK(sw({"-SBLASTER"}).soundDevice == 4);  // only three characters are read
    CHECK(sw({"-SI", "-SN"}).soundDevice == 0);  // the last one wins
    CHECK(sw({"-gv"}).displayCode == 8);
    CHECK(sw({"SI", "x"}).soundDevice == -1);    // not switches: ignored
    CHECK(sw({"-?"}).usage);
    CHECK(sw({"-?"}).bad.empty());
    CHECK(sw({"-SX"}).usage);
    CHECK(sw({"-SX"}).bad == "-SX");
    CHECK(sw({"-G"}).usage);
    CHECK(sw({"-GA"}).usage);
    CHECK(sw({"-"}).usage);
    CHECK(sw({"-Q"}).usage);
    CHECK(sw({"-X", "-SI"}).soundDevice == -1);  // the usage ends the parse
    CHECK(soundForDevice(-1) == GgSound::Fm);
    CHECK(soundForDevice(0) == GgSound::Off);
    CHECK(soundForDevice(1) == GgSound::Speaker);
    CHECK(soundForDevice(4) == GgSound::Fm);
    CHECK(splitArgs("  -SI\t-GV  ") == std::vector<std::string>{"-SI", "-GV"});
}

TEST_CASE("gg options: parsing and errors") {
    std::string error;
    const std::vector<std::string> a = {"--data",   "D",      "--pref-dir", "P",       "--headless", "--ticks", "300",
                                        "--keys",   "1:tap:a", "--keys",    "2:tap:b", "--mouse",    "3:click:1:2",
                                        "--gg-args", "-SI",    "--sound",   "speaker", "--cpu",      "286",
                                        "--seed",   "1234",   "--scale",    "3",       "--square-pixels", "--frame-dump", "F"};
    const auto o = parseGgOptions(a, error);
    REQUIRE(o.has_value());
    CHECK(o->data == "D");
    CHECK(o->prefDir == "P");
    CHECK(o->headless);
    CHECK(o->ticks == 300u);
    CHECK(o->keys == "1:tap:a 2:tap:b");
    CHECK(o->mouse == "3:click:1:2");
    CHECK(o->ggArgs == "-SI");
    CHECK(o->sound == GgSound::Speaker);
    CHECK(o->cpu == GgCpu::Cpu286);
    CHECK(o->seed == 1234);
    CHECK(o->scale == 3);
    CHECK(o->squarePixels);
    CHECK(o->frameDump == "F");
    CHECK_FALSE(parseGgOptions(std::vector<std::string>{"--bogus"}, error).has_value());
    CHECK(error.find("--bogus") != std::string::npos);
    CHECK_FALSE(parseGgOptions(std::vector<std::string>{"--ticks"}, error).has_value());
    CHECK_FALSE(parseGgOptions(std::vector<std::string>{"--ticks", "x"}, error).has_value());
    CHECK_FALSE(parseGgOptions(std::vector<std::string>{"--sound", "midi"}, error).has_value());
    CHECK_FALSE(parseGgOptions(std::vector<std::string>{"--seed", "70000"}, error).has_value());
    CHECK(parseGgOptions(std::vector<std::string>{"--help"}, error)->help);
}

TEST_CASE("gg settings: ggport.ini round trip, bad values keep the defaults") {
    const auto dir = testdata::scratchDir("gg_settings");
    const auto path = dir / GgSettings::kFileName;
    bool existed = true;
    GgSettings s = GgSettings::load(path, &existed);
    CHECK_FALSE(existed);
    CHECK(s.sound == GgSound::Fm);
    CHECK(s.cpu == GgCpu::Cpu386);
    s.data = "C:/Games/GG";
    s.sound = GgSound::Speaker;
    s.cpu = GgCpu::Cpu286;
    s.squarePixels = true;
    s.windowScale = 4;
    s.typematicDelayMs = 250;
    REQUIRE(s.save(path));
    const GgSettings r = GgSettings::load(path, &existed);
    CHECK(existed);
    CHECK(r.data == "C:/Games/GG");
    CHECK(r.sound == GgSound::Speaker);
    CHECK(r.cpu == GgCpu::Cpu286);
    CHECK(r.squarePixels);
    CHECK(r.windowScale == 4);
    CHECK(r.typematicDelayMs == 250);
    CHECK(r.typematicRateMs == 33);
    REQUIRE(writeWholeFile(path, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("[ggport]\nsound = loud\ncpu = 8086\n"), 32)));
    const GgSettings bad = GgSettings::load(path);
    CHECK(bad.sound == GgSound::Fm);
    CHECK(bad.cpu == GgCpu::Cpu386);
}

// ------------------------------------------------------------------------------------- the icon

TEST_CASE("gg icon: an .ico file's directory and its 4-bit image") {
    // A 1-entry .ico with an 8x8 16-colour DIB: colour 1 = red, the left half transparent.
    std::vector<uint8_t> f = {0, 0, 1, 0, 1, 0};
    const uint32_t dibSize = 40 + 16 * 4 + 8 * 4 + 8 * 4;
    const uint8_t entry[16] = {8, 8, 16, 0, 1, 0, 4, 0, static_cast<uint8_t>(dibSize), 0, 0, 0, 22, 0, 0, 0};
    f.insert(f.end(), entry, entry + 16);
    auto put32 = [&f](uint32_t v) {
        for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    auto put16 = [&f](uint16_t v) {
        f.push_back(static_cast<uint8_t>(v));
        f.push_back(static_cast<uint8_t>(v >> 8));
    };
    put32(40);
    put32(8);
    put32(16);  // height doubled
    put16(1);
    put16(4);
    for (int i = 0; i < 6; ++i) put32(0);
    for (int i = 0; i < 16; ++i) put32(i == 1 ? 0x00FF0000u : 0u);  // RGBQUAD B, G, R, 0: colour 1 red
    for (int row = 0; row < 8; ++row) put32(0x11111111u);           // every pixel colour 1
    for (int row = 0; row < 8; ++row) put32(0x000000F0u);           // AND: left 4 pixels transparent
    REQUIRE(f.size() == 22 + dibSize);
    const auto dir = parseIcoDirectory(f);
    REQUIRE(dir.size() == 1);
    CHECK(dir[0].dir.width == 8);
    CHECK(dir[0].offset == 22u);
    const IconImage img = decodeIcoFile(f);
    CHECK(img.width == 8);
    CHECK(img.height == 8);
    CHECK_FALSE(img.opaqueAt(0, 0));
    CHECK(img.opaqueAt(7, 7));
    CHECK(img.rgba[(7 * 8 + 7) * 4 + 0] == 0xFF);  // red
    CHECK_THROWS(parseIcoDirectory(std::vector<uint8_t>{0, 0, 2, 0, 1, 0}));
}

TEST_CASE("gg icon: the user's ssg.ico") {
    GG_REQUIRE_DATA();
    const auto ico = ggFiles->pathOf("ssg.ico");
    if (!ico) {
        MESSAGE("no ssg.ico in the game folder: test skipped");
        return;
    }
    const auto img = loadIcoFile(*ico);
    REQUIRE(img.has_value());
    CHECK(img->width == 32);
    CHECK(img->height == 32);
    CHECK(img->bitCount == 4);
}

// ------------------------------------------------------------------------------ against ssg.exe

TEST_CASE("gg start-up facts match ssg.exe") {
    GG_REQUIRE_DATA();
    const GgExe& exe = ggFiles->exe();
    // Input_InstallHandlers 1454:004a: MOV AX, 2000h (the PIT divisor).
    const auto pit = exe.at(0x1454, 0x004a);
    CHECK(pit[0] == 0xB8);
    CHECK((pit[1] | (pit[2] << 8)) == GgClock::kDivisor);
    // Timer_ISR 1454:00c9: TEST g_ticks, 1 (the sound tick on even values).
    const auto isr = exe.at(0x1454, 0x00c9);
    CHECK(isr[0] == 0xF7);
    CHECK((isr[2] | (isr[3] << 8)) == dg::kTicks);
    CHECK((isr[4] | (isr[5] << 8)) == 1);
    // The -S table CS:0234 of ovl11: 7 characters and their handlers (012f: 1, 0137: 4, 013f: 0).
    const auto table = exe.at(0x303a, 0x0234);
    REQUIRE(table.size() >= 28);
    for (int i = 0; i < 7; ++i) {
        const int ch = table[2 * i] | (table[2 * i + 1] << 8);
        const int handler = table[14 + 2 * i] | (table[15 + 2 * i] << 8);
        const int16_t expected = handler == 0x012f ? 1 : handler == 0x0137 ? 4 : handler == 0x013f ? 0 : -9;
        REQUIRE(expected != -9);
        std::string arg = "-S";
        if (ch != 0) arg += static_cast<char>(ch);
        INFO(arg);
        CHECK(sw({arg}).soundDevice == expected);
    }
}

// ----------------------------------------------------------------------------------- the app

TEST_CASE("gg app: options and folders that end the start-up") {
    const auto pref = testdata::scratchDir("gg_app_errors");
    const std::string p = pathToUtf8(pref);
    CHECK(runGgApp(std::vector<std::string>{"--help"}) == 0);
    CHECK(runGgApp(std::vector<std::string>{"--bogus"}) == 1);
    CHECK(runGgApp(std::vector<std::string>{"--pref-dir", p, "--headless", "--gg-args", "-SX"}) == 1);
    CHECK(runGgApp(std::vector<std::string>{"--pref-dir", p, "--headless", "--gg-args", "-?"}) == 0);
    // An explicit folder that is not the game: exit code 1, nothing written there.
    const auto empty = pref / "not_the_game";
    std::filesystem::create_directories(empty);
    CHECK(runGgApp(std::vector<std::string>{"--data", pathToUtf8(empty), "--pref-dir", p, "--headless"}) == 1);
    CHECK(std::filesystem::is_empty(empty));
    // The tool dispatcher leaves game options alone.
    int code = -1;
    CHECK_FALSE(runToolCli({"--headless", "--ticks", "10"}, code));
    CHECK(code == -1);
}

TEST_CASE("gg app: a headless run of the test pattern with scripted input and frame dumps") {
    GG_REQUIRE_DATA();
    const auto pref = testdata::scratchDir("gg_app_run");
    const auto frames = pref / "frames";
    const std::vector<std::string> args = {"--data",    pathToUtf8(ggFiles->folder()),
                                           "--pref-dir", pathToUtf8(pref),
                                           "--headless", "--ticks", "300",
                                           "--frame-dump", pathToUtf8(frames),
                                           "--mouse",   "40:move:100:50, 100:click:160:100",
                                           "--keys",    "120:down:left, 130:up:left"};
    CHECK(runGgApp(args) == 0);
    CHECK(std::filesystem::exists(pref / GgSettings::kFileName));
    const auto pngs = pngsIn(frames);
    REQUIRE(pngs.size() >= 6);  // the first frame, the move, the marker steps, the held key, the click
    CHECK(pngs.back().filename() == "end.png");
    CHECK(pngs.front().filename().string().starts_with("00000_t0000000"));
    const auto bytes = readWholeFile(pngs.front());
    REQUIRE(bytes.has_value());
    const auto png = mods::decodePng(*bytes);
    CHECK(png.width == 320);
    CHECK(png.height == 200);
    // The first picture shows the base palette in its grid: cell 0x0F at (300, 0).
    const auto palette = rgbaPalette(ggFiles->exe().dsBytes(dg::kPalShadow, dg::kPalShadowCount));
    const size_t px = (0 * 320 + 300) * 4;
    CHECK(png.rgba[px + 0] == (palette[15] & 0xFF));
    CHECK(png.rgba[px + 1] == ((palette[15] >> 8) & 0xFF));
    CHECK(png.rgba[px + 2] == ((palette[15] >> 16) & 0xFF));
}

TEST_CASE("gg app: Esc ends the test pattern, a custom entry gets its context") {
    GG_REQUIRE_DATA();
    const auto pref = testdata::scratchDir("gg_app_entry");
    const std::string data = pathToUtf8(ggFiles->folder()), p = pathToUtf8(pref);
    const auto last = pref / "last.png";
    CHECK(runGgApp(std::vector<std::string>{"--data", data, "--pref-dir", p, "--headless", "--ticks", "100000", "--keys",
                                            "50:tap:esc", "--frame-dump", pathToUtf8(last)}) == 0);
    CHECK(std::filesystem::exists(last));

    int shutdowns = 0;
    uint32_t ticksSeen = 0;
    uint16_t seed = 0;
    const GgEntry entry = [&](GgContext& ctx) -> int {
        ctx.atExit.atExit([&] { ++shutdowns; });
        seed = ctx.launch.seed;
        CHECK(ctx.launch.sound == GgSound::Speaker);  // -SI
        CHECK(ctx.launch.cpu == GgCpu::Cpu286);
        CHECK(ctx.runtime.deterministic());
        CHECK(ctx.runtime.handlersInstalled());
        ctx.runtime.delay(146);
        ticksSeen = ctx.runtime.ticks();
        gameExit(7);
    };
    CHECK(runGgApp(std::vector<std::string>{"--data", data, "--pref-dir", p, "--headless", "--ticks", "1000", "--gg-args",
                                            "-SI", "--cpu", "286", "--seed", "4321"},
                   entry) == 7);
    CHECK(shutdowns == 1);
    CHECK(ticksSeen == 146u);
    CHECK(seed == 4321);
}
