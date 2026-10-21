// End-to-end playthroughs of the DOS edition through the real start-up (app::runGame, headless):
// the key scripts of the playthrough driver (pt_full.py, pt_resume.py; wave 3 integration, regenerated
// in wave 4 with the reference frame timing and in wave 5 with the frame order, the frame after a tick
// running after it: extracted/scratch/dos_w5_frameorder/play), built against the
// layout of srand value 0x1EE2 (--dos-layout-seed; the layout of the Round-1 EGA capture climb). The
// checks are the milestones of the per-tick trace (--dos-trace) and fields of the saved
// dos_users.dat; they only hold for these scripts and that layout. The trace has no row for the game
// ticks that fall into the original's disk I/O (g_quit: Title_Run's load, the game start, every scene
// load), so a run has fewer rows than game ticks.
#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "app/bootstrap.h"
#include "app/launch_options.h"
#include "save/settings.h"
#include "mischief/test/app_test_util.h"
#include "dos/game/trace.h"
#include "dos_test_data.h"
#include "mischief/util/file_util.h"

using namespace tmport;
using namespace tmport::dos;
namespace fs = std::filesystem;

namespace {

// Ada's game: the four title pages, the name, the start message, Up at the clubhouse door.
constexpr const char* kBootAndNewPlayer =
    "60:tap:enter, 120:tap:enter, 180:tap:enter, 240:tap:enter, 400:tap:a, 420:tap:d, 440:tap:a, 460:tap:enter, "
    "600:tap:enter, 700:down:up, 760:up:up";

// pt_full.py: catch the clue carrier at the door, answer its question, pay the net shop, the
// treasure and the key of level 0, the trunk, the key of level 1, the geyser, the key of level 2,
// the arch, the castle ladders, the end sequence, the treasure room, the rank screen, the end
// message, Exit and Save.
constexpr const char* kFullGame =
    ", 2200:tap:right, 2400:tap:space, 2520:tap:down, 2540:tap:down, 2600:tap:enter, 2700:tap:enter, "
    "2721:down:left, 3469:up:left, 3483:tap:down, 3574:tap:down, 3665:tap:down, 3756:tap:down, 3843:down:up, "
    "3857:up:up, 3867:down:down, 3906:up:down, 3916:down:left, 3992:up:left, 4006:tap:down, 4141:tap:enter, "
    "4161:down:left, 4247:up:left, 4261:tap:down, 4398:tap:enter, 4418:down:right, 4976:up:right, 4986:down:up, "
    "5342:up:up, 5352:down:left, 6171:up:left, 6185:tap:down, 6300:down:left, 6878:up:left, 6888:down:up, "
    "7250:up:up, 7260:down:right, 7586:up:right, 7600:tap:down, 7715:down:right, 7965:up:right, 7979:tap:down, "
    "8025:tap:enter, 8045:down:right, 8295:up:right, 8305:down:up, 8339:up:up, 8357:down:right, 8443:up:right, "
    "8453:down:up, 8723:up:up, 8743:tap:pgup:4, 8845:down:up, 9063:up:up, 10527:tap:enter, 10643:tap:enter, "
    "10706:tap:enter, 10846:tap:esc, 10906:tap:y";

// pt_resume.py, run 1: the treasure and the key of level 0, the trunk, Exit and Save on level 1.
constexpr const char* kSaveOnLevel1 =
    ", 830:down:left, 1726:up:left, 1740:tap:down, 1855:down:right, 1886:tap:enter, 1953:up:right, 1967:tap:down, "
    "2102:tap:enter, 2122:down:right, 2618:up:right, 2628:down:up, 2984:up:up, 3014:tap:esc, 3074:tap:y";

// pt_resume.py, run 2: Ada (Up on the sign-in list), Yes in the saved-game box, then level 1 to the end.
constexpr const char* kResume =
    "60:tap:enter, 120:tap:enter, 180:tap:enter, 240:tap:enter, 300:tap:up, 330:tap:enter, 400:tap:enter, "
    "484:tap:enter, 495:down:left, 1068:up:left, 1082:tap:down, 1217:tap:enter, 1237:down:left, 1487:up:left, "
    "1501:tap:down, 1616:down:left, 1647:tap:enter, 2226:up:left, 2236:down:up, 2598:up:up, 2608:down:right, "
    "2639:tap:enter, 2974:up:right, 2988:tap:down, 3103:down:right, 3353:up:right, 3367:tap:down, 3413:tap:enter, "
    "3433:down:right, 3683:up:right, 3693:down:up, 3727:up:up, 3745:down:right, 3831:up:right, 3841:down:up, "
    "4111:up:up, 4131:tap:pgup:4, 4233:down:up, 4451:up:up, 6036:tap:enter, 6153:tap:enter, 6217:tap:enter, "
    "6357:tap:esc, 6417:tap:y";

struct Row {
    uint64_t tick = 0;
    int x = 0, y = 0, state = 0, coins = 0, nets = 0, level = 0, scene = 0, uiMode = 0, area = 0;
    std::string events;
    int cell() const { return ((y >> 7) << 4) | ((x >> 7) & 0xF); }
};

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

// The trace rows (dos/game/trace.h: tick,ticks,gated,x,y,camx,camy,state,keys,coins,nets,level,scene,
// uimode,area,events).
std::vector<Row> readTrace(const fs::path& file) {
    std::vector<Row> rows;
    std::ifstream in(file);
    std::string line;
    bool header = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!header) {
            header = true;
            continue;
        }
        const auto c = split(line);
        if (c.size() < 16) continue;
        Row r;
        r.tick = std::stoull(c[0]);
        r.x = std::stoi(c[3]);
        r.y = std::stoi(c[4]);
        r.state = std::stoi(c[7]);
        r.coins = std::stoi(c[9]);
        r.nets = std::stoi(c[10]);
        r.level = std::stoi(c[11]);
        r.scene = std::stoi(c[12]);
        r.uiMode = std::stoi(c[13], nullptr, 16);
        r.area = std::stoi(c[14]);
        r.events = c[15];
        rows.push_back(r);
    }
    return rows;
}

// The first tick whose row satisfies `pred`, or 0.
uint64_t firstTick(const std::vector<Row>& rows, const std::function<bool(const Row&)>& pred) {
    for (const Row& r : rows)
        if (pred(r)) return r.tick;
    return 0;
}

const Row* rowAt(const std::vector<Row>& rows, uint64_t tick) {
    for (const Row& r : rows)
        if (r.tick == tick) return &r;
    return nullptr;
}

// One headless run of the game with a key script, a trace and the given preference folder.
std::vector<Row> runScript(const fs::path& dir, const std::string& keys, const std::string& tag) {
    const std::string data = mischief::pathToUtf8(*testdata::dosDataPath());
    const std::string pref = mischief::pathToUtf8(dir), ini = mischief::pathToUtf8(dir / "tmport.ini");
    const fs::path tracePath = dir / (tag + ".csv");
    const std::string trace = mischief::pathToUtf8(tracePath);
    std::vector<const char*> argv = {"tmport", "--edition", "dos", "--data", data.c_str(), "--headless", "--pref-dir",
                                     pref.c_str(), "--config", ini.c_str(), "--sound", "tandy", "--dos-trace", trace.c_str(),
                                     "--dos-layout-seed", "0x1EE2", "--keys", keys.c_str()};
    // (The last key, Y, starts a Button_Flash: the headless run goes on until it ends and the game
    // has saved and quit, 15 game ticks after the script's end.)
    std::vector<std::string> errors;
    auto sink = [&](const app::StartupError& e) { errors.push_back(e.what()); };
    CHECK(app::runGame(static_cast<int>(argv.size()), const_cast<char**>(argv.data()), {}, sink) == 0);
    CHECK(errors.empty());
    return readTrace(tracePath);
}

struct Record {
    uint8_t saved = 0;
    uint16_t total = 0, level = 0, coins = 0, nets = 0, x = 0, y = 0, gamesPlayed = 0;
};
// Record `i` of a DOS USERS.DAT (fmt_data.md §4).
std::optional<Record> readRecord(const fs::path& file, int i) {
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() != 3243) return std::nullopt;
    const size_t r = 3 + 0x168 * static_cast<size_t>(i);
    auto w = [&](size_t o) { return static_cast<uint16_t>(b[r + o] | (b[r + o + 1] << 8)); };
    Record rec;
    rec.saved = b[r + 9];
    rec.total = w(0x0A);
    rec.level = w(0x142);
    rec.coins = w(0x144);
    rec.nets = w(0x146);
    rec.x = w(0x14E);
    rec.y = w(0x150);
    rec.gamesPlayed = w(0x162);
    return rec;
}

bool hasUsersDat() { return testdata::dosFiles() && testdata::dosFiles()->hasUsersDat(); }

}  // namespace

TEST_CASE("dos integration: a new player plays from the title to the treasure room and saves") {
    TM_REQUIRE_DOS_DATA();
    if (!hasUsersDat()) {
        MESSAGE("TM_DATA_DOS has no USERS.DAT (the scripts expect its roster): test skipped");
        return;
    }
    const fs::path dir = apptest::outDir("dos_playthrough_full");
    std::ofstream(dir / "tmport.ini") << "[saves]\nimported_original = true\n";
    const auto rows = runScript(dir, std::string(kBootAndNewPlayer) + kFullGame, "full");
    REQUIRE(rows.size() == 10897);  // 10922 game ticks (the game quits on the tick of Exit and Save), 25 in disk I/O
    // The quiz of the caught clue carrier, the right answer (+1 catch, +2 answer: 8 coins).
    CHECK(firstTick(rows, [](const Row& r) { return r.uiMode == 0x02; }) == 2444);
    CHECK(rowAt(rows, 2800)->coins == 8);
    // The net shop: 4 coins, +5 nets in the cave.
    CHECK(firstTick(rows, [](const Row& r) { return r.nets == 14; }) == 3857);
    // The trunk to level 1 (BF), the geyser to level 2 (7E), the arch into the castle hall (73).
    CHECK(firstTick(rows, [](const Row& r) { return r.level == 1 && r.cell() == 0xBF && (r.y & 0x7F) == 0x70; }) == 5342);
    CHECK(firstTick(rows, [](const Row& r) { return r.level == 2 && r.cell() == 0x7E && (r.y & 0x7F) == 0x70; }) == 7250);
    CHECK(firstTick(rows, [](const Row& r) { return r.area == 0x0D && r.cell() == 0x73; }) == 8339);
    // The ladder jump (state 33) in the castle, then out of the exit onto the castle top (level 3).
    CHECK(firstTick(rows, [](const Row& r) { return r.state == 33; }) == 8744);
    CHECK(firstTick(rows, [](const Row& r) { return r.level == 3; }) == 9063);
    // The end sequence, the chute, the treasure room (scene 4, cell 99), the rank screen (1D).
    CHECK(firstTick(rows, [](const Row& r) { return r.state == 61; }) > 9063);
    CHECK(firstTick(rows, [](const Row& r) { return r.scene == 4 && r.cell() == 0x99; }) == 10133);
    CHECK(firstTick(rows, [](const Row& r) { return r.scene == 4 && r.cell() == 0x1D; }) == 10227);
    // Back in the clubhouse: Game_End saves, the end message; Exit and Save quits. (The frame of the
    // write runs right after the tick that read the Enter: its events are in that tick's row.)
    CHECK(firstTick(rows, [](const Row& r) { return r.events.find("save=game end") != std::string::npos; }) == 10527);
    CHECK(firstTick(rows, [](const Row& r) { return r.events.find("save=exit and save") != std::string::npos; }) == 10922);
    const auto rec = readRecord(dir / "dos_users.dat", 1);
    REQUIRE(rec);
    CHECK(rec->total == 1);        // the treasure of level 0 (the others had no coin left)
    CHECK(rec->gamesPlayed == 1);
    CHECK(rec->saved == 0);
    CHECK(rec->nets == 14);
}

TEST_CASE("dos integration: a game saved on level 1 is resumed from the sign-in and played to the end") {
    TM_REQUIRE_DOS_DATA();
    if (!hasUsersDat()) {
        MESSAGE("TM_DATA_DOS has no USERS.DAT (the scripts expect its roster): test skipped");
        return;
    }
    const fs::path dir = apptest::outDir("dos_playthrough_resume");
    std::ofstream(dir / "tmport.ini") << "[saves]\nimported_original = true\n";
    // Run 1: Exit and Save on top of the trunk (level 1).
    const auto first = runScript(dir, std::string(kBootAndNewPlayer) + kSaveOnLevel1, "save");
    CHECK(first.size() == 3078);  // quits on game tick 3090; 12 ticks in disk I/O
    auto rec = readRecord(dir / "dos_users.dat", 1);
    REQUIRE(rec);
    CHECK(rec->saved == 1);
    CHECK(rec->level == 1);
    CHECK(rec->x == 0x7C8);
    CHECK(rec->y == 0x5F0);
    CHECK(rec->coins == 3);
    // Run 2 (the same preference folder): the saved-game box, Game_Restore, the rest of the game.
    const auto rows = runScript(dir, kResume, "resume");
    REQUIRE(rows.size() == 6414);  // quits on game tick 6433; 19 ticks in disk I/O
    const uint64_t restored = firstTick(rows, [](const Row& r) { return r.uiMode == 0x04; });
    CHECK(restored == 423);
    if (const Row* r = rowAt(rows, restored)) {
        CHECK(r->level == 1);
        CHECK(r->x == 0x7C8);
        CHECK(r->y == 0x5F0);
        CHECK(r->coins == 3);
    }
    CHECK(firstTick(rows, [](const Row& r) { return r.level == 2 && r.cell() == 0x7E && (r.y & 0x7F) == 0x70; }) == 2598);
    CHECK(firstTick(rows, [](const Row& r) { return r.level == 3; }) == 4451);
    CHECK(firstTick(rows, [](const Row& r) { return r.scene == 4 && r.cell() == 0x99; }) == 5628);
    CHECK(firstTick(rows, [](const Row& r) { return r.events.find("save=exit and save") != std::string::npos; }) == 6433);
    rec = readRecord(dir / "dos_users.dat", 1);
    REQUIRE(rec);
    CHECK(rec->total == 2);
    CHECK(rec->gamesPlayed == 1);
    CHECK(rec->saved == 0);
}

TEST_CASE("dos integration: --dos-layout-seed takes 0..65535, decimal or hex, and reaches the DOS options") {
    std::string err;
    auto parse = [&](std::vector<const char*> a) {
        a.insert(a.begin(), "tmport");
        return app::parseLaunchArgs(static_cast<int>(a.size()), const_cast<char**>(a.data()), err);
    };
    const auto hex = parse({"--dos-layout-seed", "0x1EE2"});
    REQUIRE(hex);
    const save::EngineSettings settings;
    CHECK(app::resolveDosOptions(*hex, settings, {}).layoutSeed == uint16_t{0x1EE2});
    const auto dec = parse({"--dos-layout-seed", "65535"});
    REQUIRE(dec);
    CHECK(app::resolveDosOptions(*dec, settings, {}).layoutSeed == uint16_t{0xFFFF});
    const auto none = parse({});
    REQUIRE(none);
    CHECK_FALSE(app::resolveDosOptions(*none, settings, {}).layoutSeed.has_value());
    CHECK_FALSE(parse({"--dos-layout-seed", "65536"}));
    CHECK_FALSE(parse({"--dos-layout-seed", "0x"}));
    CHECK_FALSE(parse({"--dos-layout-seed", "12ab"}));
}
