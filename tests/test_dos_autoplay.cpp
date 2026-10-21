// The DOS edition's autoplayer (area autoplay, wave 5; src/dos/autoplay/dos_autoplay.h): a bot that
// plays the real game headless and checks the game's invariants while it plays. The data-free part
// checks the bot's map of the mountain (level loops, twins, hazards) and the castle route; the game
// part needs TM_DATA_DOS (skips without it): whole climbs, the champion path, Exit and Save with a
// resumed program run, Start New Player and back, the checks themselves on crafted ticks, and what the
// bot sees against the elf table. No window, no audio device.

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <string>

#include "dos/autoplay/dos_autoplay.h"
#include "dos/autoplay/dos_autoplay_checks.h"
#include "dos/autoplay/dos_autoplay_world.h"
#include "dos/game/game_state.h"
#include "dos/save/users_store.h"
#include "dos_game_fixture.h"
#include "mischief/util/file_util.h"

using namespace tmport;
using namespace tmport::dos;
using namespace tmport::dos::autoplay;

namespace {

std::filesystem::path testDir(const std::string& name) {
    return std::filesystem::temp_directory_path() / "tmport_dos_autoplay_test" / name;
}

// A run's result as test messages.
void report(const RunResult& r) {
    MESSAGE(summarize(r));
    if (!r.abortReason.empty()) MESSAGE(r.abortReason);
    for (const auto& v : r.violations) MESSAGE(v);
    if (!r.ok)
        for (const auto& l : r.recent) MESSAGE(l);
}

}  // namespace

TEST_CASE("dos autoplay world: the level loops, their twins and walking directions") {
    // code_world.md §3.4: level 0 E3..FF, level 1 AB..C2, level 2 77..88, each with 3 twin cells.
    CHECK(levelLoop(0).cells.size() == 26);
    CHECK(levelLoop(1).cells.size() == 21);
    CHECK(levelLoop(2).cells.size() == 15);
    CHECK(canonicalCell(0, 0xE3) == 0xFDu);
    CHECK(canonicalCell(0, 0xFF) == 0xE5u);
    CHECK(canonicalCell(1, 0xAB) == 0xC0u);
    CHECK(canonicalCell(1, 0xC2) == 0xADu);
    CHECK(canonicalCell(2, 0x77) == 0x86u);
    CHECK(canonicalCell(2, 0x88) == 0x79u);
    CHECK_FALSE(canonicalCell(0, 0xDF));  // the trunk is not on the path
    CHECK(levelOfPathCell(0xEF) == 0);
    CHECK(levelOfPathCell(0xBF) == 1);
    CHECK(levelOfPathCell(0x7E) == 2);
    CHECK(levelOfPathCell(0x02) == -1);
    // Walking right off EF continues in F0 (the strip wrap): neighbouring loop positions.
    CHECK(loopPosOf(0, 0xF0, 0) - loopPosOf(0, 0xEF, 0) == 128);
    CHECK(*loopPos(0, 0x7F0, 0x770) == loopPosOf(0, 0xEF, 0x70));
    // The shortest way around the loop.
    const int len = levelLoop(2).lengthPx();
    CHECK(loopDelta(2, 0, 100) == 100);
    CHECK(loopDelta(2, 0, len - 100) == -100);
    CHECK(loopDelta(2, len - 10, 10) == 20);
    // The castle region: the hall 70..76 and the wall, not the level-2 seam 77 or the shelf rooms.
    CHECK(inCastle(0x73));
    CHECK(inCastle(0x35));
    CHECK_FALSE(inCastle(0x77));
    CHECK_FALSE(inCastle(0x38));
}

TEST_CASE("dos autoplay world: the hazards follow the tier and the CPU") {
    GameState s;
    s.world.cellType[0x84] = 45;
    s.world.cellType[0xB7] = 45;
    s.core.tier = 1;
    CHECK(hazards(s, 0).empty());
    CHECK(hazards(s, 1).empty());
    REQUIRE(hazards(s, 2).size() == 1);  // the geyser hole only
    CHECK(hazards(s, 2)[0].cell == 0x7E);
    CHECK(hazards(s, 2)[0].to - hazards(s, 2)[0].from == 0x3F);
    s.core.tier = 2;
    CHECK(hazards(s, 1).size() == 1);
    CHECK(hazards(s, 2).size() == 2);
    s.core.slowCpu = 1;  // Cells_AdjustForCpu: plain path on a slow CPU
    CHECK(hazards(s, 1).empty());
}

TEST_CASE("dos autoplay world: castle routes up the ladder layouts") {
    // Layout 0 of g_castleLayouts (rows 3..7, columns 2..5), as castleGrid reads it from the cells.
    GameState s;
    const uint8_t layout0[5][4] = {{0x33, 0x34, 0x33, 0x32}, {0x2A, 0x31, 0x2D, 0x31}, {0x2A, 0x31, 0x2D, 0x31},
                                   {0x2A, 0x2A, 0x31, 0x2A}, {0x2C, 0x3D, 0x27, 0x2C}};
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 4; ++c) s.world.cellPicture[static_cast<size_t>((3 + r) * 16 + 2 + c)] = layout0[r][c];
    CastleGrid g = castleGrid(s);
    REQUIRE(hallLadders(g) == std::vector<int>{4});
    auto route = castleRoute(g, 7, 4);
    // Up the hall ladder (74) into 64 and 54 (a short ladder), a jump right onto 55, up 45 to the exit 35.
    REQUIRE(route.size() == 6);
    CHECK(route[0].move == CastleMove::Up);
    CHECK(route[1].move == CastleMove::Up);
    CHECK(route[2].move == CastleMove::JumpRight);
    CHECK(route[2].row == 5);
    CHECK(route[2].col == 4);
    CHECK(route[3].move == CastleMove::Up);
    CHECK(route[4].move == CastleMove::Up);
    CHECK(route[5].move == CastleMove::Exit);
    CHECK(route[5].row == 3);
    CHECK(route[5].col == 5);
    // No ladder: no route.
    CHECK(castleRoute(g, 7, 2).empty());
    // Layout 3: three hall ladders, but only the one in column 2 leads up (columns 4 and 5 end in short
    // ladders between a wall and each other); the exit is in column 4.
    const uint8_t layout3[5][4] = {{0x34, 0x33, 0x32, 0x34}, {0x31, 0x2D, 0x31, 0x31}, {0x2D, 0x31, 0x2D, 0x31},
                                   {0x31, 0x2A, 0x2D, 0x2D}, {0x27, 0x3D, 0x27, 0x27}};
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 4; ++c) s.world.cellPicture[static_cast<size_t>((3 + r) * 16 + 2 + c)] = layout3[r][c];
    g = castleGrid(s);
    CHECK(hallLadders(g) == std::vector<int>{2, 4, 5});
    route = castleRoute(g, 7, 2);
    REQUIRE_FALSE(route.empty());
    CHECK(route.back().move == CastleMove::Exit);
    CHECK(route.back().col == 4);
    CHECK(castleRoute(g, 7, 4).empty());
    CHECK(castleRoute(g, 7, 5).empty());
}

TEST_CASE("dos autoplay: a new player's climbs through the real game, no invariant violated, reproducible") {
    TM_REQUIRE_DOS_GAME_FILES();
    RunOptions o;
    o.seed = 3;
    o.climbs = 2;
    o.hazardWalkRate = 0;  // jump over the geyser hole every time (falls: the next test case)
    o.prefDir = testDir("seed_3");
    const RunResult r = runDosAutoplay(*gameFiles, o);
    report(r);
    CHECK(r.ok);
    CHECK(r.violations.empty());
    CHECK(r.stats.climbsDone == 2);
    // Every part of a climb: three gates, a key per level, all the treasures of tier 0 (2 per level),
    // elves caught and questions answered, the castle's ladder jump, the rank screen, two game ends and
    // Exit and Save.
    CHECK(r.stats.gates == 6);
    CHECK(r.stats.keys == 6);
    CHECK(r.stats.treasures == 12);
    CHECK(r.stats.finalTotal == 12);
    CHECK(r.stats.catches > 0);
    CHECK(r.stats.correct > 0);
    CHECK(r.stats.ladderJumps >= 2);
    CHECK(r.stats.rankVisits == 2);
    CHECK(r.saveChecks >= 4);  // new player, 2 game ends, Exit and Save
    // The same seed plays the same game.
    const RunResult again = runDosAutoplay(*gameFiles, o);
    CHECK(again.ok);
    CHECK(again.gameTicks == r.gameTicks);
    CHECK(again.stats.drops == r.stats.drops);
    std::error_code ec;
    std::filesystem::remove_all(o.prefDir, ec);
}

TEST_CASE("dos autoplay: a crafted player near 300 takes the champion path at tier 3") {
    TM_REQUIRE_DOS_GAME_FILES();
    RunOptions o;
    o.seed = 5;
    o.climbs = 1;
    o.craftedTotal = 290;
    o.craftedClimbs = 40;
    o.look = Look::Tandy;
    o.prefDir = testDir("champion");
    const RunResult r = runDosAutoplay(*gameFiles, o);
    report(r);
    CHECK(r.ok);
    CHECK(r.stats.champion == 1);
    CHECK(r.stats.finalTotal >= 300);
    CHECK(r.stats.maxTier == 3);
    CHECK(r.stats.hazardJumps > 0);  // the geyser hole and, from tier 2, the bridges
    std::error_code ec;
    std::filesystem::remove_all(o.prefDir, ec);
}

TEST_CASE("dos autoplay: Exit and Save mid-climb, a new program run resumes the same record") {
    TM_REQUIRE_DOS_GAME_FILES();
    RunOptions o;
    o.seed = 7;
    o.climbs = 1;
    o.exitTest = true;
    o.frameSpeed = FrameSpeed::Period;
    o.prefDir = testDir("exit");
    const RunResult r = runDosAutoplay(*gameFiles, o);
    report(r);
    CHECK(r.ok);
    CHECK(r.phases == 2);
    CHECK(r.stats.exits == 2);    // mid-climb, and after the climb
    CHECK(r.stats.resumes == 1);  // the record compared byte for byte
    std::error_code ec;
    std::filesystem::remove_all(o.prefDir, ec);
}

TEST_CASE("dos autoplay: Start New Player and back, slow CPU, CGA, clubhouse trips and menus") {
    TM_REQUIRE_DOS_GAME_FILES();
    RunOptions o;
    o.seed = 11;
    o.climbs = 1;
    o.newPlayerTest = true;
    o.slowCpu = true;
    o.look = Look::Cga;
    o.sound = SoundDevice::Speaker;
    o.clubhouseTrips = true;
    o.menuTrips = true;
    o.prefDir = testDir("new_player");
    const RunResult r = runDosAutoplay(*gameFiles, o);
    report(r);
    CHECK(r.ok);
    CHECK(r.stats.rankVisits >= 2);   // the clubhouse's chest and the end of the climb
    CHECK(r.stats.roomVisits >= 1);   // the treasure room from the clubhouse
    CHECK(r.stats.shelfVisits >= 1);  // the prize shelf rooms
    std::error_code ec;
    std::filesystem::remove_all(o.prefDir, ec);
}

TEST_CASE("dos autoplay: falls on purpose through the geyser hole and a 4-star bridge, and back up") {
    TM_REQUIRE_DOS_GAME_FILES();
    // Tier 0: the first hazard is the geyser hole on level 2. The slide leaves the player in AE above
    // the path line (Cell_P39 gives no lift at y & 0x7F 0x60), from where Down walks him down; the
    // geyser takes him up again.
    RunOptions o;
    o.seed = 3;
    o.climbs = 1;
    o.hazardWalkRate = 1.0;
    o.prefDir = testDir("hole");
    RunResult r = runDosAutoplay(*gameFiles, o);
    report(r);
    CHECK(r.ok);
    CHECK(r.stats.holeSlides == 1);
    CHECK(r.stats.gates == 4);  // the geyser twice
    // Tier 2: the level-1 bridge breaks under the player, who falls to level 0 and climbs the trunk
    // again.
    o.craftedTotal = 120;
    o.craftedClimbs = 14;
    o.prefDir = testDir("bridge");
    r = runDosAutoplay(*gameFiles, o);
    report(r);
    CHECK(r.ok);
    CHECK(r.stats.bridgeBreaks == 1);
    CHECK(r.stats.gates == 4);  // the trunk twice
    std::error_code ec;
    std::filesystem::remove_all(testDir("hole"), ec);
    std::filesystem::remove_all(testDir("bridge"), ec);
}

TEST_CASE("dos autoplay checks: unexplained coins and nets, and a broken save, are violations") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    TickSnapshot b = snapshotOf(g, f.users());
    b.coins = 5;
    b.nets = 3;
    TickSnapshot a = b;
    Checker c;
    // A coin dropped (player state 27 entered): -1 is explained.
    a.coins = 4;
    a.playerState = 27;
    c.onTick(g, b, a);
    CHECK(c.violations().empty());
    // A coin gone without a reason.
    a.playerState = b.playerState;
    c.onTick(g, b, a);
    REQUIRE(c.violations().size() == 1);
    CHECK(c.violations()[0].find("unexplained") != std::string::npos);
    // A net catch (an elf entering state 69): +1 coin, -1 net.
    Checker c2;
    a = b;
    a.coins = 6;
    a.nets = 2;
    a.elfState[3] = 69;
    c2.onTick(g, b, a);
    CHECK(c2.violations().empty());
    // ... but two nets for one tick are not.
    a.nets = 1;
    c2.onTick(g, b, a);
    CHECK(c2.violations().size() == 1);
    // Elf dust from tier 0 is against the rules (no throwers below 2 stars).
    Checker c3;
    a = b;
    a.coins = 4;
    a.elfState[5] = 54;
    a.tier = 0;
    c3.onTick(g, b, a);
    CHECK(c3.violations().size() == 1);
}

TEST_CASE("dos autoplay: what the bot sees is the elf table") {
    TM_REQUIRE_DOS_GAME_FILES();
    const VisionCheck v = checkVision(*gameFiles, 2, 2500);
    MESSAGE(std::format("{} samples, {} seen, {} of the right kind, {} strays", v.samples, v.seen, v.rightKind, v.strays));
    CHECK(v.samples > 100);
    CHECK(v.ok());
}
