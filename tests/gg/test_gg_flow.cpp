// The game's flow (wave 3, gg/game/flow.h): Seq_Run's tables from the exe, the boot and the attract on
// the game (frame for frame and sample for sample the wave 2 preview's, which equals the DOSBox-X
// captures), Esc in the attract, the hub against the w0 captures, its dispatch, states 16 and 17, the
// LZH refill's key drain, the trace logger and the DGROUP dump, starting from a DOSBox-X state in the
// hub, Save & Exit with the PLAYERS.DAT copies of round 2, and the dump at the hub against the w0 state.
// Every run takes a scratch preference folder; only ticks, counts, CRCs and coordinates are kept here.
#include <doctest/doctest.h>

#include <algorithm>
#include <climits>
#include <filesystem>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "gg/app/gg_app.h"
#include "gg/app/options.h"
#include "gg/app/preview.h"
#include "gg/app/seq_run.h"
#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/game/hub.h"
#include "gg/game/load_state.h"
#include "gg/game/trace_log.h"
#include "gg/game/world.h"
#include "gg/game/actors.h"
#include "gg/game/workshop.h"
#include "gg/save/gg_state_file.h"
#include "gg/save/players_store.h"
#include "gg/ui/players.h"
#include "gg_game_fixture.h"
#include "gg_test_data.h"
#include "mods/png_codec.h"
#include "util/file_util.h"
#include "util/hash.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

struct Shown {
    uint64_t tick = 0;
    uint64_t hash = 0;
    bool operator==(const Shown&) const = default;
};

uint64_t frameHash(std::span<const uint32_t> rgba) {
    return fnv1a64(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(rgba.data()), rgba.size() * 4));
}

std::vector<std::string> appArgs(const GgFiles& files, const std::filesystem::path& pref) {
    return {"--data", pathToUtf8(files.folder()), "--pref-dir", pathToUtf8(pref), "--headless", "--seed", "10703"};
}

// A capture as 320 x 200 RGB (640 x 400 shots are 2 x 2 blocks).
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
                rgb[(static_cast<size_t>(y) * 320 + static_cast<size_t>(x)) * 3 + static_cast<size_t>(k)] =
                    png.rgba[(static_cast<size_t>(y * step) * static_cast<size_t>(png.width) + static_cast<size_t>(x * step)) * 4 +
                             static_cast<size_t>(k)];
    return rgb;
}

int differing(std::span<const uint32_t> frame, const std::vector<uint8_t>& rgb) {
    int n = 0;
    for (size_t i = 0; i < 320 * 200; ++i) {
        const uint32_t p = frame[i];
        const uint8_t* c = &rgb[i * 3];
        n += (p & 0xFF) != c[0] || ((p >> 8) & 0xFF) != c[1] || ((p >> 16) & 0xFF) != c[2];
    }
    return n;
}

// The hub zone with a release code, its centre.
std::pair<int, int> zoneCentre(const GgExe& exe, int releaseCode) {
    for (const HubZone& z : HubScreen::zones(exe))
        if (z.releaseCode == releaseCode) return {z.x + z.w / 2, z.y + z.h / 2};
    return {-1, -1};
}

// Runs the fixture's function until it returns (at most `max` frames).
void runToEnd(gggame::GameFixture& f, int max) {
    f.runUntil([](const GgTraceRecord&) { return false; }, max);
}

bool setUpPlayer(gggame::GameFixture& f, const char* players, const char* name) {
    const auto p = testdata::ggCapture(players);
    return p && f.loadPlayers(*p) && f.restorePlayerByName(name);
}

}  // namespace

TEST_CASE("flow: Seq_Run's tables are read from the exe") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    const auto main = mainStateTable(f.g());
    CHECK_FALSE(main[0]);
    CHECK_FALSE(main[18]);
    for (size_t i = 1; i <= 17; ++i) CHECK(static_cast<bool>(main[i]));
    const auto attract = attractStateTable(f.g());
    CHECK_FALSE(attract[0]);
    CHECK_FALSE(attract[12]);
    for (size_t i = 1; i <= 11; ++i) CHECK(static_cast<bool>(attract[i]));
    // State 17 returns 0: entry 18 is NULL and the main sequence ends with 0.
    f.g().flow.state = 17;
    CHECK(runMainSequence(f.g()) == 0);
    CHECK(f.g().flow.state == 18);
    // Seq_Run's history word: -2 goes back to it (a run resumed from a state passes the state it left).
    std::vector<int> ran;
    const std::array<StateFn, 5> t = {StateFn{}, [&] { ran.push_back(1); return int16_t{-2}; },
                                      [&] { ran.push_back(2); return int16_t{0}; }, [&] { ran.push_back(3); return int16_t{-1}; },
                                      StateFn{}};
    int16_t s = 1;
    CHECK(seqRun(t, s, 2) == -1);
    CHECK(ran == std::vector<int>{1, 2, 3});
}

TEST_CASE("flow: the game's options") {
    std::string error;
    const std::vector<std::string> args = {"--gg-trace", "t.csv", "--gg-trace-polls", "--gg-dump-state", "4000", "d.bin",
                                           "--gg-load-state", "s.sav", "--gg-preview"};
    const auto o = parseGgOptions(args, error);
    REQUIRE(o);
    CHECK(o->traceFile == "t.csv");
    CHECK(o->tracePolls);
    CHECK(o->dumpStateTick == 4000u);
    CHECK(o->dumpStateFile == "d.bin");
    CHECK(o->loadState == "s.sav");
    CHECK(o->preview);
    CHECK_FALSE(parseGgOptions(std::vector<std::string>{"--gg-dump-state", "40"}, error));
    CHECK_FALSE(parseGgOptions(std::vector<std::string>{"--gg-dump-state", "x", "d.bin"}, error));
}

TEST_CASE("flow: the boot and the attract equal the wave 2 preview frame for frame") {
    GG_REQUIRE_DATA();
    // The w0 tour's start: the seed 0x29CF, Space on story page 1 at PIT tick 3200.
    auto run = [&](bool preview, std::vector<Shown>& shown, int& title, int& story) {
        const auto pref = testdata::scratchDir(preview ? "gg_flow_boot_preview" : "gg_flow_boot_game");
        auto args = appArgs(*ggFiles, pref);
        for (const char* a : {"--ticks", "3300", "--keys", "3200:tap:space"}) args.push_back(a);
        const auto titleRgb = testdata::ggCapture("w0/01_title.png");
        const auto storyRgb = testdata::ggCapture("w0/02_story1.png");
        std::optional<std::vector<uint8_t>> t, s;
        if (titleRgb) t = captureRgb(*titleRgb);
        if (storyRgb) s = captureRgb(*storyRgb);
        title = story = INT_MAX;
        const GgEntry entry = [&](GgContext& ctx) {
            ctx.runtime.setFrameSink([&](uint64_t tick, std::span<const uint32_t> rgba) {
                shown.push_back({tick, frameHash(rgba)});
                if (t && tick < 1400) title = std::min(title, differing(rgba, *t));
                if (s && tick >= 1400 && tick < 3200) story = std::min(story, differing(rgba, *s));
            });
            return preview ? previewMain(ctx) : gameMain(ctx);
        };
        CHECK(runGgApp(args, entry) == 0);
    };
    std::vector<Shown> previewFrames, gameFrames;
    int pTitle = 0, pStory = 0, gTitle = 0, gStory = 0;
    run(true, previewFrames, pTitle, pStory);
    run(false, gameFrames, gTitle, gStory);
    // Up to the Space every picture at the same PIT tick.
    const auto before = [](const std::vector<Shown>& v) {
        std::vector<Shown> out;
        for (const Shown& s : v)
            if (s.tick < 3200) out.push_back(s);
        return out;
    };
    const auto p = before(previewFrames), g = before(gameFrames);
    CHECK(p.size() > 400);
    CHECK(p.size() == g.size());
    size_t firstDiff = 0;
    while (firstDiff < std::min(p.size(), g.size()) && p[firstDiff] == g[firstDiff]) ++firstDiff;
    INFO("first differing picture: " << firstDiff << " of " << p.size());
    CHECK(firstDiff == p.size());
    if (testdata::ggCapturesPath()) {
        CHECK(gTitle == 0);
        CHECK(gStory == 0);
    }
}

TEST_CASE("flow: the game's attract sounds as the preview's (offline render)") {
    GG_REQUIRE_DATA();
    auto render = [&](bool preview) {
        const auto pref = testdata::scratchDir(preview ? "gg_flow_wav_preview" : "gg_flow_wav_game");
        const auto wav = pref / "attract.wav";
        auto args = appArgs(*ggFiles, pref);
        for (const std::string& a : {std::string("--ticks"), std::string("1900"), std::string("--wav-dump"), pathToUtf8(wav)})
            args.push_back(a);
        CHECK(runGgApp(args, [&](GgContext& c) { return preview ? previewMain(c) : gameMain(c); }) == 0);
        return readWholeFile(wav);
    };
    const auto a = render(true), b = render(false);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->size() > 100000);
    CHECK(fnv1a64(*a) == fnv1a64(*b));
}

TEST_CASE("flow: Esc in the attract ends the program at once") {
    GG_REQUIRE_DATA();
    const auto pref = testdata::scratchDir("gg_flow_esc");
    auto args = appArgs(*ggFiles, pref);
    for (const char* a : {"--ticks", "4000", "--keys", "300:tap:esc"}) args.push_back(a);
    bool returned = false;
    uint64_t at = 0;
    CHECK(runGgApp(args, [&](GgContext& c) {
              const int code = gameMain(c);
              returned = true;
              at = c.runtime.clock().interrupts();
              return code;
          }) == 0);
    CHECK(returned);           // exit(0), not the run limit
    CHECK(at < 600);           // the title's fade-out after the Esc, nothing more
}

TEST_CASE("flow: the hub against the w0 captures") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    gggame::GameFixture f(*ggFiles);
    if (!setUpPlayer(f, "savestates/w0/06_hub.players", "Ada")) {
        MESSAGE("w0/06_hub.players missing: test skipped");
        return;
    }
    const auto hub = testdata::ggCapture("w0/06_hub.png");
    const auto hover = testdata::ggCapture("w0/07_hub_hover_aircraft.png");
    REQUIRE(hub);
    REQUIRE(hover);
    // The pointer as in the shots: on the sign-in booth (93, 137), then over the left building (100, 50).
    REQUIRE(f.mouse("1:move:93:137"));
    const uint64_t randBefore = f.rand().calls();
    f.start([](GgGame& g) { state03Hub(g); });
    int best06 = INT_MAX, best07 = INT_MAX;
    int frames = 0;
    for (; frames < 120 && !f.done(); ++frames) {
        f.step(1);
        best06 = std::min(best06, f.differingPixels(*hub));
    }
    REQUIRE(f.mouse("1:move:100:50"));
    for (int k = 0; k < 120 && !f.done(); ++k) {
        f.step(1);
        best07 = std::min(best07, f.differingPixels(*hover));
    }
    CHECK_FALSE(f.done());
    f.finish();
    CHECK(best06 == 0);
    CHECK(best07 == 0);
    CHECK(f.rand().calls() == randBefore);  // the hub calls rand() never (code_world.md §3.3)
    // The music: the title bank loaded and played, arrangement 2; the windmill's 4-tick frames.
    const auto& trace = f.trace();
    REQUIRE(trace.size() > 10);
    CHECK(trace[0].sound.find("loadMusicBank(4128") != std::string::npos);
    CHECK(trace[0].sound.find("selectArrangement") != std::string::npos);
    for (size_t i = 3; i < 20; ++i) CHECK(trace[i].ticks - trace[i - 1].ticks == 4);
}

TEST_CASE("flow: the hub's dispatch") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const GgExe& exe = ggFiles->exe();
    const auto [ax, ay] = zoneCentre(exe, kHubCodeBuilding0 + 1);
    const auto [bx, by] = zoneCentre(exe, kHubCodeSignIn);
    REQUIRE(ax >= 0);
    REQUIRE(bx >= 0);
    SUBCASE("a building: g_building, then state 4") {
        gggame::GameFixture f(*ggFiles);
        REQUIRE(setUpPlayer(f, "savestates/w0/06_hub.players", "Ada"));
        REQUIRE(f.mouse(std::format("60:click:{}:{}", ax, ay)));
        int16_t result = -99;
        f.start([&](GgGame& g) { result = state03Hub(g); });
        runToEnd(f, 400);
        REQUIRE(f.done());
        f.finish();
        CHECK(result == 0);
        CHECK(f.g().player.building == 1);
        CHECK(f.g().world.inBuilding == 0);
        CHECK(f.g().keyboard().playMode() == 0);
        CHECK_FALSE(f.g().flowObjects().hub);  // the screen freed
        CHECK(f.g().screenAnims.size() == 0);  // ScreenAnim_Free of both animations
    }
    SUBCASE("the booth: the sign-in (2); a finished building only shows its box") {
        gggame::GameFixture f(*ggFiles);
        REQUIRE(setUpPlayer(f, "savestates/w0/06_hub.players", "Ada"));
        f.g().player.racesWon[1] = 4;
        // The finished building (its box closed with Enter), Esc's Save & Exit answered with Esc twice
        // (or, without the box, Esc again), then the booth.
        REQUIRE(f.mouse(std::format("60:click:{}:{}, 100:tap:enter, 140:tap:esc, 180:tap:esc, 260:click:{}:{}", ax, ay, bx, by)));
        int16_t result = -99;
        f.start([&](GgGame& g) { result = state03Hub(g); });
        runToEnd(f, 600);
        REQUIRE(f.done());
        f.finish();
        CHECK(result == 2);
        CHECK(f.g().player.building == -1);  // Ada's (no building chosen)
        CHECK_FALSE(f.exitCode().has_value());
    }
}

TEST_CASE("flow: states 16 and 17") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    f.g().flow.leaveRequest = 3;
    f.g().actors.playerParts = {1, 2};
    f.g().actors.actors[kPlayerActor].carried = 2;
    int16_t r = -99;
    f.start([&](GgGame& g) { r = state16SaveAndLeave(g); });
    runToEnd(f, 50);
    f.finish();
    CHECK(r == 3);
    CHECK(f.g().actors.playerParts.empty());               // List_Clear
    CHECK(f.g().actors.actors[kPlayerActor].carried == 2);  // the count stays (code_world.md §20 bug 7)
    CHECK(state17End(f.g()) == 0);
}

TEST_CASE("flow: the LZH refill drains the keys typed during a decode") {
    GG_REQUIRE_DATA();
    CHECK(lzhReadCount(0) == 0);
    CHECK(lzhReadCount(4) == 0);
    CHECK(lzhReadCount(5) == 1);
    CHECK(lzhReadCount(4004) == 1);
    CHECK(lzhReadCount(4005) == 2);
    // A compressed entry: the title picture.
    const Archive& arc = ggFiles->archive();
    REQUIRE(arc.info(kTitlePicture).method != kMethodStored);
    LzhInfo info;
    (void)arc.load(kTitlePicture, &info);
    const int reads = lzhReadCount(info.usedBytes);
    CHECK(reads >= 1);
    for (const bool drain : {false, true}) {
        gggame::GameFixture f(*ggFiles);
        GgGame& g = f.g();
        REQUIRE(f.keys("2:tap:a"));
        g.rt.waitTicks(4);  // the key is in the ring and the BIOS buffer
        int calls = 0;
        if (drain) {
            installLzhKeyDrain(g, true);
            const LzhRefillHook keyDrain = setLzhRefillHook({});
            setLzhRefillHook([&calls, keyDrain](int n) {
                calls += n;
                keyDrain(n);
            });
        }
        (void)g.archive().load(kTitlePicture);
        CHECK(calls == (drain ? reads : 0));
        CHECK(g.rt.getInputCode() == (drain ? 0 : 'a'));
        installLzhKeyDrain(g, false);
    }
    // Other threads and stored entries never call it.
    setLzhRefillHook({});
}

TEST_CASE("flow: the trace logger and the DGROUP dump") {
    GG_REQUIRE_DATA();
    gggame::GameFixture f(*ggFiles);
    std::ostringstream csv;
    const auto dir = testdata::scratchDir("gg_flow_dump");
    {
        GgTraceLogger logger(f.g(), csv, false);
        GgStateDumper dumper(f.g(), 40, dir / "hub.dgroup");
        f.start([](GgGame& g) { state03Hub(g); });
        f.step(30);
        f.finish();
        CHECK(logger.rows() >= 29);  // the fixture's boundaries are the logger's rows
        CHECK(dumper.written());
    }
    std::istringstream in(csv.str());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) lines.push_back(line);
    REQUIRE(lines.size() > 3);
    CHECK(lines[0].starts_with("#"));
    CHECK(lines[1] == traceCsvHeader());
    // Every row has the header's columns (the sound column is quoted: its calls hold commas).
    const auto columns = [](const std::string& l) {
        int n = 1;
        bool quoted = false;
        for (const char c : l) {
            if (c == '"') quoted = !quoted;
            else if (c == ',' && !quoted) ++n;
        }
        return n;
    };
    for (size_t i = 2; i < lines.size(); ++i) CHECK(columns(lines[i]) == columns(lines[1]));
    CHECK(lines[2].find("loadMusicBank(4128,") != std::string::npos);  // the hub's set-up, in the first row
    CHECK(lines[3].find(",timerWait.0,") != std::string::npos);
    const auto image = readWholeFile(dir / "hub.dgroup");
    REQUIRE(image);
    CHECK(image->size() == GgExe::kDgroupSize);
    // g_state is the hub's, the Borland copyright at DS:0004 comes from the exe's image.
    CHECK(((*image)[dg::kState] | ((*image)[dg::kState + 1] << 8)) == 1);  // main's g_state = 1 (the fixture runs no Seq_Run)
    CHECK(std::string(reinterpret_cast<const char*>(image->data() + 4), 9) == "Borland C");
    const auto tsv = readWholeFile(dir / "hub.dgroup.fields.tsv");
    REQUIRE(tsv);
    const auto rows = std::count(tsv->begin(), tsv->end(), '\n');
    CHECK(static_cast<size_t>(rows) == gameStateFields().size() + serviceFields().size() + 1);
}

TEST_CASE("flow: --gg-load-state in the hub goes on from the w0 state") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/w0/06_hub.sav");
    const auto shot = testdata::ggCapture("w0/06_hub.png");
    if (!sav || !shot) {
        MESSAGE("w0/06_hub missing: test skipped");
        return;
    }
    gggame::GameFixture f(*ggFiles);
    const GgStateFile s = GgStateFile::load(*sav);
    const GgLoadedState loaded = loadGgState(f.g(), s);
    INFO("why: " << loaded.why << "; cpu " << loaded.cpu);
    REQUIRE(loaded.supported);
    CHECK(loaded.state == 3);
    CHECK(f.g().player.slot == 98);  // Ada, from the state's DGROUP
    f.start([&loaded](GgGame& g) { runFromLoadedState(g, loaded); });
    int best = INT_MAX;
    for (int k = 0; k < 40 && !f.done(); ++k) {
        f.step(1);
        best = std::min(best, f.differingPixels(*shot));
    }
    CHECK_FALSE(f.done());
    f.finish();
    CHECK(best == 0);
    // Every frame is 4 ticks; rand() is not called.
    const auto& t = f.trace();
    for (size_t i = 2; i < t.size(); ++i) CHECK(t[i].ticks - t[i - 1].ticks == 4);
    CHECK(t.back().randState == s.dsDword(dg::kRandState));
}

TEST_CASE("flow: Save & Exit from the hub writes the player and exits (r2/resume)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto players = testdata::ggCapture("savestates/r2/resume/07_hub_bob_resumed.players");
    if (!players) {
        MESSAGE("r2/resume/07_hub_bob_resumed.players missing: test skipped");
        return;
    }
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadPlayers(*players));
    REQUIRE(f.restorePlayerByName("Bob"));
    const int writesBefore = f.players().writes();
    // Esc opens Save & Exit (default No); 'y' answers Yes.
    REQUIRE(f.keys("60:tap:esc, 100:tap:y"));
    f.start([](GgGame& g) { state03Hub(g); });
    f.polls(true);
    f.runUntil([](const GgTraceRecord&) { return false; }, 20000);
    const bool exited = f.done() && f.exitCode().has_value();
    f.finish();
    REQUIRE(exited);
    CHECK(*f.exitCode() == 0);
    CHECK(f.players().writes() == writesBefore + 1);
    CHECK(f.players().lastMoment() == GgSaveMoment::SaveAndExit);
    // Nothing changed in the hub: the file is the one Bob was resumed from, byte for byte.
    const auto before = readWholeFile(*players);
    const auto after = readWholeFile(f.players().path());
    REQUIRE(before);
    REQUIRE(after);
    CHECK(fnv1a64(*after) == fnv1a64(*before));
}

TEST_CASE("flow: Save & Exit in a building writes what the original wrote (r2/resume 03 -> 04)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/r2/resume/03_save_exit_dialog.sav");
    const auto expected = testdata::ggCapture("savestates/r2/resume/04_after_save_exit.players");
    if (!sav || !expected) {
        MESSAGE("r2/resume 03 / 04 missing: test skipped");
        return;
    }
    gggame::GameFixture f(*ggFiles);
    REQUIRE(f.loadState(*sav));
    const GgStateFile s = GgStateFile::load(*sav);
    std::string why;
    for (auto* restore : {&worldRestoreFromState, &actorsRestoreFromState}) REQUIRE_MESSAGE(restore(f.g(), s, why), why);
    (void)vehiclesRestoreFromState(f.g(), s, why);
    // Cmd_SaveAndExit's Yes: Players_SaveCurrent (the building's block, g_inBuilding = 1 in the state).
    REQUIRE(f.g().world.inBuilding == 1);
    const int writes = f.players().writes();
    playersSaveCurrent(f.g(), GgSaveMoment::SaveAndExit);
    REQUIRE(f.players().writes() == writes + 1);
    const auto want = readWholeFile(*expected);
    const auto got = readWholeFile(f.players().path());
    const auto before = readWholeFile(GgStateFile::playersFileOf(*sav));
    REQUIRE(want);
    REQUIRE(got);
    REQUIRE(before);
    REQUIRE(want->size() == got->size());
    REQUIRE(before->size() == got->size());
    // The building's block of the player's record (record +0x27E + b * 0x6BA) and its in-progress word
    // (record +0x3A + 2 b) are what Players_SaveCurrent writes for the building (code_ui.md §12.5).
    const size_t record = 0x8A0 + static_cast<size_t>(f.g().player.slot) * 0x16BD;
    const size_t b = static_cast<size_t>(f.g().player.building);
    const size_t block = record + 0x27E + b * 0x6BA, blockEnd = block + 0x6BA;
    const size_t progress = record + 0x3A + 2 * b;
    const auto inBuildingPart = [&](size_t i) { return (i >= block && i < blockEnd) || i == progress || i == progress + 1; };
    size_t diffOutside = 0, diffBuilding = 0;
    bool blockWritten = false;
    for (size_t i = 0; i < want->size(); ++i) {
        if ((*want)[i] == (*got)[i]) continue;
        (inBuildingPart(i) ? diffBuilding : diffOutside) += 1;
    }
    for (size_t i = block; i < blockEnd; ++i) blockWritten = blockWritten || (*before)[i] != (*got)[i];
    // The record's head (score, live state) and the index entry's points: exactly the original's.
    CHECK(diffOutside == 0);
    CHECK(blockWritten);
    CHECK(diffBuilding == 0);
}

TEST_CASE("flow: --gg-dump-state at the hub against the w0 state") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/w0/06_hub.sav");
    if (!sav) {
        MESSAGE("w0/06_hub.sav missing: test skipped");
        return;
    }
    // The w0 tour: Space on story page 3 (the state's g_storyTextId is 0x101c), "ada", Enter (Start
    // Game), With Chimps (93, 137), the hub.
    const auto pref = testdata::scratchDir("gg_flow_dump_hub");
    const auto dump = pref / "hub.dgroup";
    auto args = appArgs(*ggFiles, pref);
    for (const std::string& a :
         {std::string("--ticks"), std::string("12900"), std::string("--keys"),
          std::string("12000:tap:space, 12300:tap:a, 12310:tap:d, 12320:tap:a, 12400:tap:enter"), std::string("--mouse"),
          std::string("12500:click:93:137"), std::string("--gg-dump-state"), std::string("12800"), pathToUtf8(dump)})
        args.push_back(a);
    CHECK(runGgApp(args, &gameMain) == 0);
    const auto image = readWholeFile(dump);
    REQUIRE(image);
    const GgStateFile s = GgStateFile::load(*sav);
    std::vector<DgroupField> fields = gameStateFields();
    for (const DgroupField& d : serviceFields()) fields.push_back(d);
    const auto differ = diffDgroup(*image, s.dgroup(), &fields);
    std::string names;
    for (const auto n : differ) names += std::string(n) + " ";
    MESSAGE("fields that differ from w0/06_hub: " << names);
    // Only what depends on the moment and the host's key timing: the clock and its deadlines, the dialog
    // freeze's tick, the roster scroll bar's tick stamp (+0x0e), the key ring's indexes.
    for (const auto n : differ) {
        INFO(std::string(n));
        CHECK((n == "g_ticks" || n == "g_timers" || n == "g_savedTicks" || n == "g_rosterScrollBar" || n == "g_kbdRingWrite" ||
               n == "g_kbdRingRead"));
    }
    const auto w = [&](uint16_t off) { return static_cast<int>((*image)[off] | ((*image)[off + 1] << 8)); };
    const auto same = [&](std::string_view name) { return std::find(differ.begin(), differ.end(), name) == differ.end(); };
    // The flow's own and the start-up's fields.
    CHECK(w(dg::kState) == 3);
    for (const char* n : {"g_state", "g_leaveRequest", "g_shutdownFlag", "g_storyTextId", "g_displayCode", "g_cpuType",
                          "g_soundDevice", "g_randSeed", "g_randState", "g_inBuilding", "g_kbdPlayMode"}) {
        INFO(n);
        CHECK(same(n));
    }
}

TEST_CASE("flow: resumed in the hub, into a building in progress (r2/resume 07 -> 08)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto sav = testdata::ggCapture("savestates/r2/resume/07_hub_bob_resumed.sav");
    const auto after = testdata::ggCapture("savestates/r2/resume/08_aircraft_resumed.sav");
    const auto shot = testdata::ggCapture("r2/resume/08_aircraft_resumed.png");
    if (!sav || !after || !shot) {
        MESSAGE("r2/resume 07 / 08 missing: test skipped");
        return;
    }
    gggame::GameFixture f(*ggFiles);
    const GgStateFile s = GgStateFile::load(*sav);
    const GgLoadedState loaded = loadGgState(f.g(), s);
    INFO("why: " << loaded.why);
    REQUIRE(loaded.supported);
    // Bob (Without Chimps) clicks the Aircraft building at (50, 50); the building in progress is resumed
    // without a question in room 4 (REFERENCE_CAPTURES §9.5).
    REQUIRE(f.mouse("8:move:50:50, 12:click:50:50"));
    f.start([&loaded](GgGame& g) { runFromLoadedState(g, loaded); });
    const auto rows = f.runUntil([](const GgTraceRecord& r) { return r.state == 7; }, 2000);
    REQUIRE_FALSE(rows.empty());
    REQUIRE(rows.back().state == 7);
    int best = INT_MAX;
    for (int k = 0; k < 60 && !f.done(); ++k) {
        f.step(1);
        best = std::min(best, f.differingPixels(*shot));
    }
    f.finish();
    const GgStateFile want = GgStateFile::load(*after);
    std::vector<uint8_t> image = dumpToDgroup(f.g());
    const auto differ = diffDgroup(image, want.dgroup());
    std::string names;
    for (const auto n : differ) names += std::string(n) + " ";
    MESSAGE("the warehouse after the resume: " << best << " pixels differ from r2/resume/08; DGROUP fields that differ: " << names
                                                << "(the sound handles: RecordingSound never reuses a freed handle)");
    {
        GgState w = initialState(ggFiles->exe());
        loadFromDgroup(w, want.dgroup());
        for (int a = 0; a < 2; ++a) {
            const Actor& x = f.g().actors.actors[static_cast<size_t>(a)];
            const Actor& y = w.actors.actors[static_cast<size_t>(a)];
            MESSAGE(std::format("actor {}: engine room {} xy ({}, {}) seq {} disabled {}; state room {} xy ({}, {}) seq {} disabled {}", a,
                                x.room, x.sprite.x, x.sprite.y, x.seq, x.disabled, y.room, y.sprite.x, y.sprite.y, y.seq, y.disabled));
        }
    }
    CHECK(best == 0);
    CHECK(f.g().playerRoom() == 4);
    CHECK(f.g().world.inBuilding == 1);
    CHECK(f.g().rand.seed() == want.dsDword(dg::kRandState));  // the resume's discarded shuffles, call for call
}

TEST_CASE("flow: ggport --gg-load-state runs from a state or refuses one with no resume point") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const auto hub = testdata::ggCapture("savestates/w0/06_hub.sav");
    const auto title = testdata::ggCapture("savestates/w0/01_title.sav");
    if (!hub || !title) {
        MESSAGE("w0 states missing: test skipped");
        return;
    }
    const auto pref = testdata::scratchDir("gg_flow_load_state");
    const auto trace = pref / "hub.csv";
    auto args = appArgs(*ggFiles, pref);
    for (const std::string& a : {std::string("--ticks"), std::string("200"), std::string("--gg-load-state"), pathToUtf8(*hub),
                                 std::string("--gg-trace"), pathToUtf8(trace)})
        args.push_back(a);
    CHECK(runGgApp(args, &gameMain) == 0);  // the run limit ends it in the hub
    const auto csv = readWholeFile(trace);
    REQUIRE(csv);
    CHECK(std::count(csv->begin(), csv->end(), '\n') > 40);  // 4-tick frames from the state's moment on
    // The title (state 1) has no resume point: an error, nothing run.
    auto bad = appArgs(*ggFiles, pref);
    for (const std::string& a : {std::string("--ticks"), std::string("200"), std::string("--gg-load-state"), pathToUtf8(*title)})
        bad.push_back(a);
    CHECK(runGgApp(bad, &gameMain) == exit_code::kAbort);
}
