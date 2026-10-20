// The wave 2 integration: the ggport-cli dispatcher over every area's tools, the display on the
// runtime (gg/app/display.h) and the preview (gg/app/preview.h) run headless through all its screens
// with the DOSBox-X captures as the reference (re/gg/docs/SCOPING.md §9, the stop/go point). Every run
// passes a scratch --pref-dir. Tests store tick numbers, counts and coordinates only.

#include <doctest/doctest.h>

#include <algorithm>
#include <climits>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "gg/app/cli_dispatch.h"
#include "gg/app/display.h"
#include "gg/app/gg_app.h"
#include "gg/app/gg_runtime.h"
#include "gg/app/preview.h"
#include "gg/audio/gg_audio.h"
#include "gg/audio/soundtest.h"
#include "gg/gfx/video.h"
#include "gg_test_data.h"
#include "app_test_util.h"
#include "mods/png_codec.h"
#include "mods/wav_file.h"
#include "util/file_util.h"
#include "util/log.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

struct Box {
    int x0, y0, x1, y1;  // inclusive
};

// A capture as 320 x 200 RGB (DOSBox-X's 640 x 400 shots are 2 x 2 blocks).
std::optional<std::vector<uint8_t>> captureRgb(const std::filesystem::path& file) {
    const auto bytes = readWholeFile(file);
    if (!bytes) return std::nullopt;
    const auto png = mods::decodePng(*bytes);
    const int step = png.width == 640 ? 2 : 1;
    if (png.width != 320 * step || png.height != 200 * step) return std::nullopt;
    std::vector<uint8_t> rgb(320 * 200 * 3);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x)
            for (int k = 0; k < 3; ++k)
                rgb[(static_cast<size_t>(y) * 320 + x) * 3 + k] =
                    png.rgba[(static_cast<size_t>(y * step) * png.width + x * step) * 4 + k];
    return rgb;
}

// Differing pixels between a presented frame (packed RGBA) and a capture, outside the boxes.
int differing(std::span<const uint32_t> frame, const std::vector<uint8_t>& rgb, const std::vector<Box>& masks) {
    int n = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            if (std::any_of(masks.begin(), masks.end(), [&](const Box& b) { return x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1; }))
                continue;
            const uint32_t p = frame[static_cast<size_t>(y) * 320 + x];
            const uint8_t* c = &rgb[(static_cast<size_t>(y) * 320 + x) * 3];
            n += (p & 0xFF) != c[0] || ((p >> 8) & 0xFF) != c[1] || ((p >> 16) & 0xFF) != c[2];
        }
    return n;
}

std::vector<std::string> previewArgs(const GgFiles& files, const std::filesystem::path& pref) {
    return {"--data", pathToUtf8(files.folder()), "--pref-dir", pathToUtf8(pref), "--headless"};
}

}  // namespace

TEST_CASE("integration: ggport-cli dispatches every area's tools") {
    GG_REQUIRE_DATA();
    const auto dir = testdata::scratchDir("gg_dispatch");
    const std::string d = pathToUtf8(ggFiles->folder());
    const auto run = [&](std::vector<std::string> args) {
        args.push_back("--data");
        args.push_back(d);
        int code = -1;
        const bool took = runToolCli(args, code);
        CHECK(took);
        return took ? code : -1;
    };
    CHECK(run({"--gg-info"}) == 0);                                              // data
    CHECK(run({"--gg-font", "9", pathToUtf8(dir / "font.png")}) == 0);           // gfx
    CHECK(run({"--gg-songs"}) == 0);                                             // audio
    CHECK(run({"--gg-clock", "--ticks", "4"}) == 0);                             // runtime
    CHECK(run({"--gg-rooms", "0"}) == 0);                                        // world
    CHECK(run({"--gg-ui-info"}) == 0);                                           // ui
    // --gg-screen: the world tool takes its four screens, the UI tool every other name.
    CHECK(run({"--gg-screen", "hub", pathToUtf8(dir / "hub.png")}) == 0);
    CHECK(run({"--gg-screen", "puzzle", "3", "0", pathToUtf8(dir / "pz3.png")}) == 0);
    CHECK(run({"--gg-screen", "story1", pathToUtf8(dir / "story1.png")}) == 0);
    CHECK(run({"--gg-screen", "nothing", pathToUtf8(dir / "x.png")}) == 2);       // the UI tool's usage error
    for (const char* f : {"font.png", "hub.png", "pz3.png", "story1.png"}) CHECK(std::filesystem::exists(dir / f));
    CHECK_FALSE(std::filesystem::exists(dir / "x.png"));
    int code = -1;
    CHECK_FALSE(runToolCli({"--headless", "--ticks", "10"}, code));  // the game's options are not a tool
}

TEST_CASE("integration: the display presents flushes, palette loads and the mouse handler's moves") {
    GG_REQUIRE_DATA();
    GgRuntime rt;
    rt.installHandlers();
    Video video;
    Display display(rt, video);
    CHECK(rt.mouse().screen() == &display);
    display.init(ggFiles->exe(), ggFiles->archive());  // Pal_Blackout: 2 retraces, a picture each
    CHECK(rt.framesShown() == 2);
    CHECK(rt.clock().interrupts() >= 2);  // two retraces of 14.27 ms: past 4 PIT ticks
    // Gfx_Init's FillRect left the whole screen dirty: the first flush presents it, a second one with
    // nothing dirty presents nothing, a drawn rectangle does again.
    display.flush();
    CHECK(rt.framesShown() == 3);
    display.flush();
    CHECK(rt.framesShown() == 3);
    video.screen().setDrawColor(15);
    video.screen().fillRect(10, 20, 30, 40);
    display.flush();
    CHECK(rt.framesShown() == 4);
    // The INT 33h handler (17a0): a host move reaches g_mouseX/Y through the MouseScreen; the shown
    // cursor is redrawn by Mouse_MoveCursor's flush and presented.
    video.cursor().show();
    video.screen().dirty().mark(0, 0, 1, 1);
    display.flush();  // Dirty_Flush draws the cursor
    CHECK(rt.framesShown() == 5);
    rt.hostPointer(100.0f, 50.0f);
    CHECK(video.cursor().x() == 100);
    CHECK(video.cursor().y() == 50);
    CHECK(rt.mouse().x() == 100);
    CHECK(rt.framesShown() == 6);
    // Mouse_SetBounds reaches the driver (INT 33h 07h / 08h, x doubled).
    video.cursor().setBounds(0, 0, 200, 100);
    CHECK(rt.mouse().driver().maxX() == 2 * video.cursor().maxX());
    CHECK(rt.mouse().driver().maxY() == video.cursor().maxY());
    // A fade: 9 loads of 2 retraces.
    const uint64_t shown = rt.framesShown();
    display.fadeIn();
    CHECK(rt.framesShown() - shown == 18);
}

TEST_CASE("integration: the preview through every screen against the captures") {
    GG_REQUIRE_DATA();
    const auto pref = testdata::scratchDir("gg_preview_tour");
    // The w0 tour's path with its srand value 0x29CF: the attract, Space on story page 1, "ada" typed
    // in the sign-in, the hub with the pointer on the sign-in booth, a click on the Aircraft building,
    // its warehouse (the w0 deal), the eight puzzle screens.
    std::string keys = "3200:tap:space, 3500:tap:a, 3510:tap:d, 3520:tap:a, 3600:tap:enter, 4600:tap:enter";
    for (int k = 0; k < 8; ++k) keys += ", " + std::to_string(4900 + 300 * k) + ":tap:enter";
    auto args = previewArgs(*ggFiles, pref);
    for (const char* a : {"--seed", "10703", "--ticks", "7300", "--mouse", "3700:move:93:137, 4000:click:50:50", "--keys"})
        args.push_back(a);
    args.push_back(keys);

    struct Case {
        const char* capture;
        std::vector<Box> masks;
        uint64_t lo = 0, hi = 0;
        std::optional<std::vector<uint8_t>> rgb = std::nullopt;
        int best = INT_MAX;
        Case(const char* c, std::vector<Box> m, uint64_t from, uint64_t to) : capture(c), masks(std::move(m)), lo(from), hi(to) {}
    };
    const Box ptr{50, 50, 89, 89};             // the preview's pointer after the click (the puzzles' cursors)
    const Box capCursor{269, 150, 277, 158};   // the arrow the hint box left in the puzzle shots
    const Box capCursorTimed{269, 150, 300, 175};  // the launch and energy puzzles redraw their own cursor
    std::vector<Case> cases = {
        {"w0/01_title.png", {}, 0, 1400},
        {"w0/02_story1.png", {}, 1400, 3200},
        {"w0/03_signin.png", {}, 3200, 3499},
        {"w0/04_signin_name.png", {}, 3520, 3599},
        {"w0/06_hub.png", {}, 3600, 3990},
        // a torn frame: each half at its own pad phase; the actors and the crate a chimp took are wave 3's
        {"w0/14_warehouse.png", {ptr, capCursor, {0, 96, 319, 199}}, 4000, 4590},
        {"w0/14_warehouse.png", {ptr, capCursor, {0, 0, 319, 95}, {0, 110, 50, 164}, {230, 90, 290, 140}, {290, 120, 319, 140}},
         4000, 4590},
    };
    for (int k = 0; k < 8; ++k) {
        static const char* kNames[8] = {"r2/puzzle/k0_01_screen.png", "r2/puzzle/k1_01_screen.png", "r2/puzzle/k2_01_screen.png",
                                        "r2/puzzle/k3_01_screen.png", "r2/puzzle/k4_01_screen.png", "r2/puzzle/k5_01_screen.png",
                                        "r2/puzzle/k6_01_screen.png", "r2/puzzle/k7_01_screen.png"};
        cases.push_back({kNames[k], {ptr, (k == 5 || k == 6) ? capCursorTimed : capCursor}, 4600 + 300u * k, 4890 + 300u * k});
    }
    bool withCaptures = true;
    for (Case& c : cases) {
        if (const auto f = testdata::ggCapture(c.capture)) c.rgb = captureRgb(*f);
        withCaptures = withCaptures && c.rgb.has_value();
    }

    uint64_t frames = 0, lastTick = 0;
    const GgEntry entry = [&](GgContext& ctx) {
        ctx.runtime.setFrameSink([&](uint64_t tick, std::span<const uint32_t> rgba) {
            ++frames;
            lastTick = tick;
            for (Case& c : cases)
                if (c.rgb && tick >= c.lo && tick <= c.hi) c.best = std::min(c.best, differing(rgba, *c.rgb, c.masks));
        });
        return previewMain(ctx);
    };
    CHECK(runGgApp(args, entry) == 0);
    CHECK(frames > 1000);
    CHECK(lastTick >= 7200);
    if (!withCaptures) {
        MESSAGE("GG_CAPTURES is not set: the frames were not compared");
        return;
    }
    for (const Case& c : cases) {
        INFO(c.capture);
        CHECK(c.best == 0);
    }
}

TEST_CASE("integration: the preview's offline sound equals the sound tool's render of its calls") {
    GG_REQUIRE_DATA();
    const auto pref = testdata::scratchDir("gg_preview_sound");
    const auto wav = pref / "attract.wav";
    const auto opl = pref / "attract.opl.txt";
    auto args = previewArgs(*ggFiles, pref);
    for (const std::string& a : {std::string("--ticks"), std::string("1900"), std::string("--seed"), std::string("10703"),
                                std::string("--verbose"), std::string("--wav-dump"), pathToUtf8(wav), std::string("--opl-log"),
                                pathToUtf8(opl)})
        args.push_back(a);
    std::vector<std::string> calls;
    {
        apptest::LogCapture log;
        CHECK(runGgApp(args, &previewMain) == 0);
        for (const std::string& l : log.lines())
            if (const auto p = l.find("sound call: "); p != std::string::npos) calls.push_back(l.substr(p + 12));
    }
    log::setVerbose(false);
    // The attract: its banks, the title music, the title's effects 12 and 13, an animation's effect.
    REQUIRE(calls.size() >= 6);
    CHECK(calls[0].ends_with(":e:1022"));
    CHECK(calls[1].ends_with(":m:1020"));
    const auto dumped = readWholeFile(wav);
    REQUIRE(dumped.has_value());
    const auto format = mods::parseWavFormat(*dumped);
    CHECK(format.sampleRate == 48000u);
    const uint64_t ticks = 1900 / 2;  // one sound tick per 2 PIT ticks
    std::string script;
    for (const std::string& c : calls) script += c + ",";
    script += std::to_string(ticks) + ":end";
    GgAudio audio(ggFiles->exe(), ggFiles->archive(), GgAudioConfig{});
    std::vector<float> pcm;
    SoundTest(audio).run(SoundTestSchedule::parse(script), &pcm);
    CHECK(audio.tickIndex() == ticks);
    CHECK(format.frames == pcm.size());
    std::vector<uint8_t> bytes;
    for (const float f : pcm) {
        const auto v = static_cast<int16_t>(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
        bytes.push_back(static_cast<uint8_t>(v));
        bytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
    }
    CHECK(mods::makeWav(48000, 1, 16, bytes) == *dumped);
    CHECK(std::filesystem::file_size(opl) > 0);
}
