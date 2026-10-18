// Room tick modules (game/room): player state machine, coin-toss slot, HUD counters, elves,
// Room_Present transitions and a headless Room_Run session. Expected values come from the specs'
// rules applied to the tables read from the user's EXE (TM_DATA); tests that need game data skip
// without it. Pixel comparisons with the original's captures are done by `tmport --room-run` (see
// extracted/scratch/room, REFERENCE_CAPTURES.md E3-E6, E13).

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "game/game_hooks.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/room/elves.h"
#include "game/room/hud.h"
#include "game/room/player.h"
#include "game/room/room_run.h"
#include "game/room/room_tables.h"
#include "game/room_table.h"
#include "game/rules.h"
#include "platform/engine.h"
#include "test_data.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/game_dialog.h"

using namespace tmport;
using namespace tmport::game;
using namespace tmport::game::room;

namespace {

struct Recorder final : GameHooks {
    std::vector<int> sfx;
    int hud = 0;
    void playSfx(int n) override { sfx.push_back(n); }
    void hudChanged() override { ++hud; }
};

const GameTables& gameTables() {
    static GameTables t = GameTables::load(*testdata::game()->exe, *testdata::game()->res);
    return t;
}
const RoomTables& roomTables() {
    static RoomTables t = RoomTables::fromExe(*testdata::game()->exe);
    return t;
}

// Player frame sizes from BNDL 300 (g_frameW/H, 1198:0000).
const FrameSizes& frameSizes() {
    static FrameSizes s = [] {
        FrameSizes f;
        const auto b = testdata::game()->res->bundle(kPlayerBndl);
        for (size_t i = 0; b && i < b->ids.size() && i < size_t(kPlayerFrameSlots); ++i) {
            const auto pm = testdata::game()->res->pixmap(b->ids[i]);
            f.w[i] = int16_t(pm ? pm->width : 0);
            f.h[i] = int16_t(pm ? pm->height : 0);
        }
        return f;
    }();
    return s;
}

// A player controller on a fresh state, standing in `room` at x facing right/left.
struct Rig {
    GameState s;
    PlayerRuntime p;
    SceneRuntime sc;
    Recorder hooks;
    ScriptedPlayerInput in;
    std::unique_ptr<PlayerController> pc;
    explicit Rig(int room, int x, bool left = false, int frame = 0) : s(GameState::initial(gameTables())) {
        s.player.room = uint8_t(room);
        s.player.x = int16_t(x);
        s.player.frame = uint8_t(frame);
        s.player.y = roomTables().frameY[size_t(frame)];
        s.player.facingLeft = left ? 1 : 0;
        s.player.w = frameSizes().width(frame);
        s.player.h = frameSizes().height(frame);
        s.hints.unseen = 0xFFFFFFFFu;
        p.nextRoom = uint8_t(room);
        pc = std::make_unique<PlayerController>(s, gameTables(), roomTables(), frameSizes(), p, sc, hooks);
    }
    void tick(uint16_t event = 0) {
        p.inputEvent = event;
        pc->update(in);
    }
};

}  // namespace

TEST_CASE("room: coin-toss target slot (10e0:0447)") {
    RoomTables t;
    t.dxRight[kCoinLandFrame] = 78;  // DS:0c89[54] / DS:0d33[54] as documented
    t.dxLeft[kCoinLandFrame] = 41;
    CHECK(coinTargetSlot(15, false, t) == 0);    // REFERENCE_CAPTURES.md §4.6
    CHECK(coinTargetSlot(268, false, t) == 2);
    CHECK(coinTargetSlot(230, true, t) == 1);
    CHECK(coinTargetSlot(10, true, t) == 0);     // (10-41)/170 truncates to 0
    CHECK(coinTargetSlot(-300, true, t) == 0);   // negative quotient clamped
    CHECK(coinTargetSlot(91, false, t) == 0);    // 169 / 170
    CHECK(coinTargetSlot(92, false, t) == 1);    // 170 / 170
    CHECK(coinTargetSlot(500, false, t) == 2);   // 3 clamped
}

TEST_CASE("room: HUD counter strings (10e8:02cf / 04b2)") {
    const std::vector<uint8_t> two = {0xFF, 0x05, 0xFF, '0', '0', 0};
    const std::vector<uint8_t> five = {0xFF, 0x05, 0xFF, '0', '0', '0', '0', '0', 0};
    auto str = [](const std::vector<uint8_t>& b) {
        std::string s;
        for (size_t i = 3; i < b.size() && b[i]; ++i) s.push_back(char(b[i]));
        return s;
    };
    CHECK(str(hudCounterText(two, 0)) == "0");
    CHECK(str(hudCounterText(two, 7)) == "7");
    CHECK(str(hudCounterText(two, 10)) == "10");
    CHECK(str(hudCounterText(two, 99)) == "99");
    CHECK(str(hudCounterText(two, 123)) == "23");  // modulo 100
    CHECK(str(hudCounterText(two, 65535)) == "35");
    CHECK(hudCounterText(two, 7)[0] == 0xFF);
    CHECK(str(hudPointsText(five, 0)) == "0");
    CHECK(str(hudPointsText(five, 42)) == "42");
    CHECK(str(hudPointsText(five, 999)) == "999");
    CHECK(str(hudPointsText(five, 1000)) == "1000");
    CHECK(str(hudPointsText(five, 65535)) == "65535");
}

TEST_CASE("room: walking follows nextFrame / dx / absolute y; release stands (E5)") {
    TM_REQUIRE_GAME_DATA();
    const RoomTables& rt = roomTables();
    Rig r(9, 233, false, kFrontFrame);
    r.tick();  // standing in the front view: frame 60 is kept
    CHECK(r.s.player.frame == kFrontFrame);
    r.in.right = true;
    int x = 233, f = kFrontFrame;
    for (int i = 0; i < 15; ++i) {
        r.tick();
        f = rt.nextFrame[size_t(f)];
        x += rt.dxRight[size_t(f)];
        CHECK(r.s.player.frame == f);
        CHECK(r.s.player.x == x);
        CHECK(r.s.player.y == rt.frameY[size_t(f)]);
        CHECK(r.s.player.w == frameSizes().width(f));
    }
    CHECK(f >= 1);
    CHECK(f <= 8);
    r.in.right = false;
    r.tick();
    CHECK(r.s.player.frame == 0);
    CHECK(r.s.player.x == x);
    CHECK(r.s.player.y == rt.frameY[0]);
    // Walking left subtracts dxLeft.
    r.in.left = true;
    r.tick();
    CHECK(r.s.player.facingLeft == 1);
    CHECK(r.s.player.frame == rt.nextFrame[0]);
    CHECK(r.s.player.x == x - rt.dxLeft[rt.nextFrame[0]]);
    // Holding Down keeps the current frame instead of standing.
    r.in.left = false;
    r.in.down = true;
    const int keep = r.s.player.frame;
    r.tick();
    CHECK(r.s.player.frame == keep);
    CHECK(r.hooks.sfx.empty());
}

TEST_CASE("room: jump 12..21 then frame 1; only frame 14 sounds; type-0 exit keeps the pose") {
    TM_REQUIRE_GAME_DATA();
    const RoomTables& rt = roomTables();
    Rig r(9, 400);
    r.in.right = true;
    r.in.up = true;
    r.tick();
    CHECK(r.p.jumping == 1);
    CHECK(r.s.player.frame == kJumpFirst);
    r.in.up = false;
    int frames = 1;
    while (r.p.jumping && frames < 20) {
        r.tick();
        ++frames;
    }
    CHECK(frames == 11);  // 12..21 then the end of the jump
    CHECK(r.s.player.frame == 1);
    CHECK(r.hooks.sfx == std::vector<int>{35});
    // From x 400 the jump crosses x > 482: room 9's right exit (type 0) keeps the pose, x - 512.
    CHECK(r.p.nextRoom == gameTables().rooms[9].exitRight);
    CHECK(r.p.roomTransition == gameTables().rooms[9].typeRight);
    CHECK(r.p.roomTransition == 0);
    CHECK(r.p.entryX == r.s.player.x - 512);
    CHECK(r.p.entryFrame == r.s.player.frame);
    (void)rt;
}

TEST_CASE("room: walls clamp at 75 / 437 - w") {
    TM_REQUIRE_GAME_DATA();
    // Find a room with a left wall and one with a right wall in the room table.
    int leftWall = -1, rightWall = -1;
    for (int i = 9; i < kRoomCount; ++i) {
        if (leftWall < 0 && gameTables().rooms[size_t(i)].exitLeft == 0xFF) leftWall = i;
        if (rightWall < 0 && gameTables().rooms[size_t(i)].exitRight == 0xFF) rightWall = i;
    }
    REQUIRE(leftWall >= 0);
    REQUIRE(rightWall >= 0);
    Rig a(leftWall, 80, true);
    a.in.left = true;
    for (int i = 0; i < 4; ++i) a.tick();
    CHECK(a.s.player.x == kWallLeftX);
    Rig b(rightWall, 400);
    b.in.right = true;
    for (int i = 0; i < 6; ++i) b.tick();
    CHECK(b.s.player.x + frameSizes().width(b.s.player.frame) == kWallRightX);
}

TEST_CASE("room: net swing, empty swing and their precedence") {
    TM_REQUIRE_GAME_DATA();
    const RoomTables& rt = roomTables();
    Rig r(10, 15);
    r.tick(0x20);
    CHECK(r.p.netStep == 1);
    CHECK(r.s.inv.nets == 9);
    r.in.left = true;  // ignored while swinging
    int x = 15;
    for (int n = 1; n <= 11; ++n) {
        r.tick();
        const int f = rt.netSwing[size_t(n)];
        x += rt.dxRight[size_t(f)];
        CHECK(r.s.player.frame == f);
        CHECK(r.s.player.x == x);
        if (n == 1) CHECK(r.p.netX == x + kNetReachRight);
    }
    CHECK(r.p.netStep == 0);
    CHECK(r.hooks.sfx == std::vector<int>{30, 6});
    // Without nets: empty swing (11 steps, no sound) and hint 5 while coins remain.
    Rig e(10, 15);
    e.s.inv.nets = 0;
    e.tick(0x20);
    CHECK(e.p.emptySwingStep == 1);
    CHECK(e.s.hints.pending == 5);
    CHECK(e.s.inv.nets == 0);
    for (int n = 0; n < 11; ++n) e.tick();
    CHECK(e.p.emptySwingStep == 0);
    CHECK(e.hooks.sfx.empty());
}

TEST_CASE("room: coin toss steps, sounds and the step-13 result") {
    TM_REQUIRE_GAME_DATA();
    Rig r(10, 15);
    r.s.rooms.spotKind(10, 0) = 18;
    r.s.rooms.spotContent(10, 0) = kContentTreasure;
    r.s.treasures.count = 2;
    r.tick(0x0D);
    CHECK(r.p.coinStep == 1);
    CHECK(r.s.inv.coins == 9);
    CHECK(r.hooks.hud == 1);  // the HUD is redrawn in the Enter tick
    for (int n = 1; n <= 13; ++n) {
        r.tick(0x0D);  // further Enters are ignored while tossing
        if (n == 1) CHECK(r.p.coinTargetSlot == 0);
        if (n < 13) CHECK(r.p.fxFrame == roomTables().coinFx[size_t(n)]);
    }
    CHECK(r.p.coinStep == 0);
    CHECK(r.p.coinTargetSlot == 0xFF);
    CHECK(r.s.inv.coins == 9);
    CHECK(r.s.inv.gemsFound == 1);
    CHECK(r.s.rooms.spotContent(10, 0) == kContentEmpty);
    CHECK(r.hooks.sfx == std::vector<int>{34, 0, 6, 41});
    CHECK(r.s.hints.pending == 4);
    // No coins: no toss, hint 6 (nets left).
    Rig n(10, 15);
    n.s.inv.coins = 0;
    n.tick(0x0D);
    CHECK(n.p.coinStep == 0);
    CHECK(n.s.hints.pending == 6);
}

TEST_CASE("room: doors and type-1 entry pose (E4)") {
    TM_REQUIRE_GAME_DATA();
    const RoomDef& r9 = gameTables().rooms[9];
    REQUIRE(r9.typeDoor == 1);
    Rig r(9, 233, false, kFrontFrame);
    r.in.up = true;
    r.tick();
    CHECK(r.p.doorStep == 1);
    r.tick();
    CHECK(r.s.player.frame == roomTables().door[1]);
    CHECK(r.s.player.y == roomTables().frameY[kFrontFrame] - 5);
    r.tick();
    CHECK(r.s.player.frame == roomTables().door[2]);
    CHECK(r.p.nextRoom == r9.exitDoor);
    CHECK(r.p.roomTransition == 1);
    const RoomDef& to = gameTables().rooms[r9.exitDoor];
    CHECK(r.p.entryX == to.startX);
    CHECK(r.p.entryY == to.startY);
    CHECK(r.p.entryFrame == to.startFrame);
}

TEST_CASE("room: the locked gate with and without the key (E4)") {
    TM_REQUIRE_GAME_DATA();
    // Without the key: hint 8.
    Rig n(11, 329, true);
    n.s.rooms.spotContent(11, 1) = kSpotGate;
    n.s.rooms.spotContent(11, 2) = kSpotGate;
    n.in.up = true;
    n.tick();
    CHECK(n.s.hints.pending == 8);
    CHECK(n.p.walkActive == 0);
    // With the key: auto-walk to 330, unlock (sfx 17), step up, through to room 23.
    Rig r(11, 320, true);
    r.s.rooms.spotContent(11, 1) = kSpotGate;
    r.s.rooms.spotContent(11, 2) = kSpotGate;
    r.s.inv.haveKey = 1;
    r.in.up = true;
    r.tick();
    CHECK(r.p.gateWalkStarted == 1);
    CHECK(r.p.inputLocked == 1);
    CHECK(r.p.walkActive == 1);
    CHECK(r.p.walkTargetX == 330);
    r.in.up = false;
    r.tick();
    CHECK(r.s.player.x == 325);  // 5 px per tick
    CHECK(r.s.player.facingLeft == 0);
    r.tick();
    CHECK(r.s.player.x == 330);
    r.tick();  // arrived
    CHECK(r.sc.gateUnlocking == 1);
    CHECK(r.p.doorStep == 1);
    CHECK(r.hooks.sfx == std::vector<int>{17});
    bool stamped = false;
    r.pc->setStampGateLock([&] { stamped = true; });
    r.tick();
    r.tick();
    CHECK(stamped);
    CHECK(r.p.arriveAction == 1);
    r.tick();
    CHECK(r.p.nextRoom == 23);
    CHECK(r.p.roomTransition == 1);
    CHECK(r.p.entryX == gameTables().rooms[23].startX);
}

TEST_CASE("room: net shop: the fourth coin opens the door, the walk, +10 nets (E13)") {
    TM_REQUIRE_GAME_DATA();
    Rig r(13, 158);
    r.s.rooms.spotKind(13, 1) = kSpotShop;
    r.s.rooms.spotContent(13, 1) = kSpotShop;
    for (int coin = 0; coin < 4; ++coin) {
        r.tick(0x0D);
        for (int n = 0; n < 13; ++n) r.tick();
    }
    CHECK(r.s.shop.coinsLeft == 0);
    CHECK(r.s.shop.state == 1);
    CHECK(r.p.walkActive == 1);
    CHECK(r.p.inputLocked == 1);
    CHECK(r.p.walkTargetX == roomTables().shopWalkX[0]);
    int guard = 0;
    while (r.s.inv.nets == 10 && guard++ < 40) r.tick(0x20);  // Space is flushed by the caller; ignored here anyway
    CHECK(r.s.inv.nets == 20);
    CHECK(r.s.player.x == roomTables().shopWalkX[0]);
    CHECK(r.s.player.frame == kFrontFrame);
    CHECK(r.s.shop.coinsLeft == 4);
    CHECK(r.s.shop.state == 3);
    CHECK(r.p.inputLocked == 0);
    CHECK(r.s.inv.coins == 6);
}

TEST_CASE("room: room-36 fountain bump and jump thresholds") {
    TM_REQUIRE_GAME_DATA();
    Rig r(kFountainRoom, 100);
    r.in.right = true;
    int guard = 0;
    while (r.p.bumpStep == 0 && guard++ < 10) r.tick();
    CHECK(r.p.bumpStep == 1);
    CHECK(r.s.player.x == kObstacleBounceRight);
    r.in.right = false;
    for (int n = 1; n <= 12; ++n) r.tick();
    CHECK(r.p.bumpStep == 0);
    CHECK(r.s.player.x == kObstacleBounceRight);  // the bump deltas sum to 0
    CHECK(std::count(r.hooks.sfx.begin(), r.hooks.sfx.end(), 40) == 1);
}

TEST_CASE("room: a jump from the bounce point clears the fountain, an early one bounces") {
    TM_REQUIRE_GAME_DATA();
    Rig r(kFountainRoom, kObstacleBounceRight);
    r.in.right = true;
    r.in.up = true;
    r.tick();
    r.in.up = false;
    for (int i = 0; i < 12 && r.p.jumping; ++i) r.tick();
    CHECK(r.p.bumpStep == 0);
    CHECK(r.s.player.x >= kObstacleRight);
    // Jumping far too early: the zone is reached with frames whose threshold is not met.
    Rig e(kFountainRoom, 10);
    e.in.right = true;
    e.in.up = true;
    e.tick();
    e.in.up = false;
    bool bumped = false;
    for (int i = 0; i < 12; ++i) {
        e.tick();
        bumped = bumped || e.p.bumpStep != 0;
    }
    CHECK(bumped);
}

TEST_CASE("room: mouse-directed walking and its latch (hot spot 0x600)") {
    TM_REQUIRE_GAME_DATA();
    Rig r(10, 200);
    r.in.mx = 50;
    r.in.my = 200;
    r.in.button = true;
    r.in.mouseDownCode = 0x8600;
    r.tick();
    CHECK(r.s.player.facingLeft == 1);
    CHECK(r.p.mouseWalkLatched == 1);
    const int x1 = r.s.player.x;
    r.in.mouseDownCode = 0;  // still held: the latch keeps walking
    r.tick();
    CHECK(r.s.player.x < x1);
    r.in.button = false;
    r.tick();
    CHECK(r.s.player.frame == 0);
}

// --------------------------------------------------------------------------------------- elves

namespace {
struct ElfRig {
    GameState s;
    PlayerRuntime p;
    SceneRuntime sc;
    Recorder hooks;
    BorlandRand rng;
    ElfSystem elves{roomTables()};
    ElfRig() : s(GameState::initial(gameTables())) {}
    void tick() {
        ElfTick t{s, p, sc, hooks, rng};
        elves.updateAll(t);
    }
};
}  // namespace

TEST_CASE("elves: spawn placement in the original's random-number order") {
    TM_REQUIRE_GAME_DATA();
    ElfRig r;
    r.s.player.room = 9;
    r.s.quiz.slots[0].q = 5;  // elf 0 of level 0 carries a question, elves 1 and 2 do not
    r.s.quiz.slots[1].q = kQuizNoQuestion;
    r.s.quiz.slots[2].q = 7;
    r.rng.srand(0x6f38);
    BorlandRand ref;
    ref.srand(0x6f38);
    r.elves.init();
    r.elves.spawnForLevel(r.s, 8, r.rng, nullptr);
    REQUIRE(r.elves.count() == 8);
    const int start = roomTables().elfStartRoom[0];
    for (int i = 0; i < 8; ++i) {
        const Elf& e = r.elves.elves()[size_t(i)];
        const int x = ref.random(250) + 30;
        const int facing = ref.random(2);
        int room = start;
        for (int k = 0; k < 15; ++k) room = ref.random(3) == 0 ? gameTables().rooms[size_t(room)].exitRight
                                                               : gameTables().rooms[size_t(room)].exitLeft;
        CHECK(e.x == x);
        CHECK(e.y == 175);
        CHECK(e.facing == facing);
        CHECK(e.room == room);
        CHECK(e.step == 1);
        const int set = i < 3 ? (e.hasItem ? 3 : 0) : i < 6 ? 2 : 1;
        CHECK(e.set == set);
        CHECK(e.behaviour == set);
    }
    CHECK(r.elves.elves()[0].hasItem == 1);
    CHECK(r.elves.elves()[1].hasItem == 0);
    CHECK(r.elves.elves()[2].hasItem == 1);
    CHECK(r.rng.seed() == ref.seed());
    // No elves on the castle wall.
    ElfRig c;
    c.s.player.room = kCastleRoom;
    c.elves.spawnForLevel(c.s, 6, c.rng, nullptr);
    CHECK(c.elves.count() == 0);
}

TEST_CASE("elves: a behaviour loop moves by the script and ends with R(20), R(30)") {
    TM_REQUIRE_GAME_DATA();
    // A hopper (index 3) walks its script: x += dx[s], y = y[s], no random numbers before the end.
    ElfRig r;
    r.s.player.room = 9;
    r.elves.loadSets(nullptr);
    r.elves.setCount(4);
    for (int i = 0; i < 3; ++i) r.elves.elves()[size_t(i)] = Elf{0, 0, 1, 200, 181, 0, 40, 0, 0};
    Elf& e = r.elves.elves()[3];
    e = Elf{2, 2, 1, 200, 175, 0, 12, 0, 0};
    const ElfScript& sc = roomTables().elfScripts[2];
    int x = 200;
    r.rng.srand(1234);
    const uint32_t seed0 = r.rng.seed();
    for (size_t st = 1; st + 1 < sc.frames.size(); ++st) {
        r.tick();
        x += sc.dx[st];
        CHECK(e.x == x);
        CHECK(e.y == sc.y[st]);
        CHECK(e.step == int(st) + 1);
    }
    CHECK(r.rng.seed() == seed0);
    // At the terminator: R(20) (re-pick the behaviour) then R(30) (turn), step 1, entry 0 moves.
    ElfRig solo;
    solo.s.player.room = 9;
    solo.elves.loadSets(nullptr);
    solo.elves.setCount(1);
    Elf& h = solo.elves.elves()[0];
    const ElfScript& s0 = roomTables().elfScripts[0];
    h = Elf{0, 0, int16_t(s0.frames.size() - 1), 100, 181, 0, 12, 0, 0};
    solo.rng.srand(99);
    BorlandRand ref;
    ref.srand(99);
    solo.tick();
    ref.random(20);
    const bool turn = ref.random(30) == 0;
    CHECK(solo.rng.seed() == ref.seed());
    CHECK(h.step == 1);
    CHECK(h.facing == (turn ? 1 : 0));
    CHECK(h.x == 100 + (turn ? -1 : 1) * s0.dx[0]);
    // A thrower in the player's room facing the middle starts a throw with R(10) < 8 instead.
    ElfRig t;
    t.s.player.room = 12;
    t.elves.loadSets(nullptr);
    t.elves.setCount(7);
    for (int i = 0; i < 6; ++i) t.elves.elves()[size_t(i)] = Elf{0, 0, 1, 200, 181, 0, 40, 0, 0};
    const ElfScript& s1 = roomTables().elfScripts[1];
    t.elves.elves()[6] = Elf{1, 1, int16_t(s1.frames.size() - 1), 100, 189, 0, 12, 0, 0};  // x < 193 facing right
    for (uint16_t seed = 0; seed < 50; ++seed) {
        for (int i = 0; i < 6; ++i) t.elves.elves()[size_t(i)] = Elf{0, 0, 1, 200, 181, 0, 40, 0, 0};
        t.elves.elves()[6] = Elf{1, 1, int16_t(s1.frames.size() - 1), 100, 189, 0, 12, 0, 0};
        t.elves.throwState = 0;
        t.rng.srand(seed);
        BorlandRand rr;
        rr.srand(seed);
        const bool throws = rr.random(10) < 8;
        t.tick();
        CHECK(t.elves.elves()[6].set == (throws ? kElfSetThrow : 1));
        CHECK(t.elves.throwState == (throws ? 1 : 0));
        CHECK(t.rng.seed() == rr.seed());  // no R(30) when facing the middle
    }
}

TEST_CASE("elves: net catch (+1 coin), question trigger at step 7, release at step 9") {
    TM_REQUIRE_GAME_DATA();
    ElfRig r;
    r.s.player.room = 11;
    r.s.player.facingLeft = 0;
    r.elves.loadSets(nullptr);
    r.elves.setCount(1);
    Elf& e = r.elves.elves()[0];
    e = Elf{3, 3, 1, 180, 181, 1, 11, 0, 1};  // a carrier
    r.p.netX = 167;
    r.p.netStep = 1;  // step 1: too early
    r.tick();
    CHECK(e.inNet == 0);
    r.p.netStep = 2;
    e.x = 190;  // |190 - 167| = 23 <= 30
    r.tick();
    CHECK(e.inNet == 1);
    CHECK(r.s.inv.coins == 11);
    CHECK(r.elves.netBusy == 1);
    CHECK(e.facing == 0);
    for (int k = 3; k <= 7; ++k) {
        r.p.netStep = uint8_t(k);
        r.tick();
    }
    CHECK(r.elves.caughtElf == 1);
    r.p.netStep = 8;
    r.tick();
    r.sc.elfQuizResult = 0;  // wrong answer / Esc: keeps the question
    r.p.netStep = 9;
    r.tick();
    CHECK(e.inNet == 0);
    CHECK(r.elves.netBusy == 0);
    CHECK(e.hasItem == 1);
    CHECK(e.set == 3);
    // A non-carrier flees (set 4) at the release.
    ElfRig f;
    f.s.player.room = 11;
    f.elves.loadSets(nullptr);
    f.elves.setCount(1);
    Elf& ng = f.elves.elves()[0];
    ng = Elf{2, 2, 1, 160, 205, 0, 11, 0, 0};
    f.p.netX = 167;
    for (int k = 2; k <= 9; ++k) {
        f.p.netStep = uint8_t(k);
        f.tick();
    }
    CHECK(ng.inNet == 0);
    CHECK(ng.set == kElfSetFlee);
    CHECK(f.elves.caughtElf == 0);
}

TEST_CASE("elves: the thrower: wind-up, throw, hit (-1 coin, sfx 8), coin back, reset") {
    TM_REQUIRE_GAME_DATA();
    ElfRig r;
    r.s.player.room = 12;
    r.s.player.x = 300;
    r.s.player.w = 60;
    r.elves.loadSets(nullptr);
    r.elves.setCount(7);
    for (int i = 0; i < 6; ++i) r.elves.elves()[size_t(i)] = Elf{0, 0, 1, 0, 181, 0, 40, 0, 0};
    Elf& e = r.elves.elves()[6];
    e = Elf{5, 5, 1, 100, 189, 0, 12, 0, 0};  // already switched to throwing, facing right
    r.elves.throwState = 1;
    std::vector<int> states;
    for (int t = 0; t < 40 && r.elves.throwState != 0; ++t) {
        r.tick();
        states.push_back(r.elves.throwState);
    }
    CHECK(r.s.inv.coins == 9);
    CHECK(std::count(r.hooks.sfx.begin(), r.hooks.sfx.end(), 31) == 1);
    CHECK(std::count(r.hooks.sfx.begin(), r.hooks.sfx.end(), 8) == 1);
    CHECK(r.elves.throwState == 0);
    CHECK(e.set == 1);  // back to walking
    CHECK(std::find(states.begin(), states.end(), 10) != states.end());
    // A jumping player is never hit.
    ElfRig j;
    j.s.player.room = 12;
    j.s.player.x = 300;
    j.s.player.w = 60;
    j.p.jumping = 1;
    j.elves.loadSets(nullptr);
    j.elves.setCount(1);
    j.elves.elves()[0] = Elf{5, 5, 1, 100, 189, 0, 12, 0, 0};
    j.elves.throwState = 1;
    for (int t = 0; t < 40 && j.elves.throwState != 0; ++t) j.tick();
    CHECK(j.s.inv.coins == 10);
}

TEST_CASE("elves: room edges, confinement after the gate walk, the room-36 fountain") {
    TM_REQUIRE_GAME_DATA();
    ElfRig r;
    r.s.player.room = 9;
    r.elves.loadSets(nullptr);
    r.elves.setCount(1);
    Elf& e = r.elves.elves()[0];
    const int room = 12;
    const RoomDef& rd = gameTables().rooms[room];
    REQUIRE(rd.exitRight != 0xFF);
    const int dx4 = roomTables().elfScripts[2].dx[4];
    REQUIRE(dx4 > 0);
    e = Elf{2, 2, 4, 500, 175, 0, int16_t(room), 0, 0};  // step 4 moves it past 512 - 21
    r.tick();
    if (rd.exitDoor == 0xFF) {
        CHECK(e.room == rd.exitRight);
        CHECK(e.x == 500 + dx4 - 512);
    } else {
        CHECK(e.room != room);
    }
    // Side door: leaving a room away from the player, an elf takes that room's door (same level,
    // not the hall) when R(3) == 0: room 11 -> 12 -> door of 11 (17).
    const RoomDef& r11 = gameTables().rooms[11];
    REQUIRE(r11.exitDoor != 0xFF);
    for (uint16_t seed = 0; seed < 30; ++seed) {
        ElfRig d;
        d.s.player.room = 9;
        d.elves.loadSets(nullptr);
        d.elves.setCount(4);
        for (int i = 0; i < 3; ++i) d.elves.elves()[size_t(i)] = Elf{0, 0, 1, 200, 181, 0, 40, 0, 0};
        d.elves.elves()[3] = Elf{2, 2, 4, 500, 175, 0, 11, 0, 0};
        d.rng.srand(seed);
        BorlandRand ref;
        ref.srand(seed);
        const bool door = ref.random(3) == 0;
        d.tick();
        const Elf& de = d.elves.elves()[3];
        if (door) {
            CHECK(de.room == r11.exitDoor);
            const bool fromLeft = gameTables().rooms[r11.exitDoor].exitLeft == 11;
            CHECK(de.x == (fromLeft ? -40 : 472));
            CHECK(de.facing == (fromLeft ? 0 : 1));
        } else {
            CHECK(de.room == r11.exitRight);
        }
        CHECK(d.rng.seed() == ref.seed());
    }
    // Gate walk: elves in the player's room flee, every elf becomes confined.
    ElfRig gw;
    gw.s.player.room = 11;
    gw.elves.loadSets(nullptr);
    gw.elves.setCount(2);
    gw.elves.elves()[0] = Elf{0, 0, 1, 100, 181, 0, 11, 0, 0};
    gw.elves.elves()[1] = Elf{0, 0, 1, 100, 181, 0, 14, 0, 0};
    gw.p.gateWalkStarted = 1;
    gw.tick();
    CHECK(gw.elves.elves()[0].set == kElfSetFlee);
    CHECK(gw.elves.elves()[1].set == 0);
    CHECK(gw.elves.confined == 1);
    // Confined: an elf turns at the edge instead of leaving.
    gw.p.gateWalkStarted = 0;
    gw.elves.elves()[1] = Elf{2, 2, 4, 500, 175, 0, 14, 0, 0};
    gw.tick();
    CHECK(gw.elves.elves()[1].room == 14);
    CHECK(gw.elves.elves()[1].facing == 1);
    // The fountain in room 36 turns elves back.
    ElfRig f;
    f.s.player.room = 9;
    f.elves.loadSets(nullptr);
    f.elves.setCount(1);
    f.elves.elves()[0] = Elf{2, 2, 4, 120, 175, 0, int16_t(kFountainRoom), 0, 0};
    f.tick();
    CHECK(f.elves.elves()[0].x == 124);
    CHECK(f.elves.elves()[0].facing == 1);
}

// --------------------------------------------------------------------- Room_Run / Room_Present

namespace {
struct LoopRig {
    ManualClock clock{1000};
    std::unique_ptr<Engine> engine;
    std::unique_ptr<text::Font> font;
    std::unique_ptr<text::TextRenderer> text;
    std::unique_ptr<ui::DialogSystem> dialogs;
    GameState state;
    std::unique_ptr<AppContext> ctx;
    std::unique_ptr<RoomLoop> loop;
    std::vector<int> pauses;
    std::vector<int> hints;
    LoopRig() : state(GameState::initial(gameTables())) {
        EngineConfig cfg;
        cfg.clock = &clock;
        engine = std::make_unique<Engine>(*testdata::game(), gfx::ColorMode::Colour256, cfg);
        font = std::make_unique<text::Font>(text::Font::load(*testdata::game()->res));
        text = std::make_unique<text::TextRenderer>(*font, &engine->frame(), &engine->presenter());
        dialogs = std::make_unique<ui::DialogSystem>(*engine, *font);
        ctx = std::make_unique<AppContext>(AppContext{*engine, *testdata::game(), *font, *text, *dialogs});
        RoomHooks h;
        h.beforePause = [this](int ms) { pauses.push_back(ms); };
        h.showHint = [this](int e) { hints.push_back(e); };
        loop = std::make_unique<RoomLoop>(*ctx, state, gameTables(), engine->rng(), h);
    }
};
}  // namespace

TEST_CASE("room: Room_Present(1) shows a black play-field only for room 9 and the hall (E4)") {
    TM_REQUIRE_GAME_DATA();
    LoopRig r;
    initNewPlayer(r.state);
    r.engine->rng().srand(0x6f38);
    r.loop->loadCommon();
    setupClimb(r.state, gameTables(), r.engine->rng(), r.loop->gameHooks());
    REQUIRE(r.state.player.room == 9);
    r.loop->startClimb();
    CHECK(r.pauses.size() == 1);  // room 9
    for (const auto& [room, expected] : std::vector<std::pair<int, size_t>>{{0, 1}, {17, 0}, {9, 1}, {23, 0}}) {
        r.pauses.clear();
        r.state.player.room = uint8_t(room);
        r.loop->scene().present(1 | 0x80);
        CHECK(r.pauses.size() == expected);
    }
    // Transition 0: no black.
    r.pauses.clear();
    r.loop->scene().present(0x80);
    CHECK(r.pauses.empty());
    // Transition 2 (dissolve, mods only): 16 paced passes, ending with the same picture as a cut (3).
    r.state.player.room = 10;
    r.loop->scene().present(3 | 0x80);
    const auto cut = r.engine->presenter().gameAreaRgba();
    r.engine->frame().setForeColor(0);
    r.engine->frame().fillRect(0, 20, 512, 267);
    r.engine->presentAll();
    r.pauses.clear();
    r.loop->scene().present(2 | 0x80);
    CHECK(r.pauses.size() == 16);
    CHECK(r.engine->presenter().gameAreaRgba() == cut);
}

TEST_CASE("room: a headless Room_Run session: hints, walking, exit through a door") {
    TM_REQUIRE_GAME_DATA();
    LoopRig r;
    initNewPlayer(r.state);
    r.engine->rng().srand(0x6f38);
    r.loop->loadCommon();
    setupClimb(r.state, gameTables(), r.engine->rng(), r.loop->gameHooks());
    r.loop->startClimb();
    // Up held: the room-9 door leads to the hall (transition 1). Hints 0 and 2 show first.
    r.engine->input().post(WinMsg::keyDown(0x26));
    r.loop->run();
    CHECK(r.hints == std::vector<int>{0, 2});
    CHECK(r.state.player.room == gameTables().rooms[9].exitDoor);
    CHECK(r.state.player.x == gameTables().rooms[r.state.player.room].startX);
    CHECK(r.loop->ticks() >= 3);
    CHECK(r.engine->hotspots().find(kHotPlayField) < 0);
    CHECK(r.engine->frame().dirtyEnabled() == false);
}

TEST_CASE("room: visitRoom through the gate: level change, elves respawned on level 1, key cleared") {
    TM_REQUIRE_GAME_DATA();
    LoopRig r;
    initNewPlayer(r.state);
    r.engine->rng().srand(0x6f38);
    r.loop->loadCommon();
    setupClimb(r.state, gameTables(), r.engine->rng(), r.loop->gameHooks());
    REQUIRE(r.loop->elves().count() == 6);
    r.state.player.room = 11;
    r.state.player.x = 329;
    r.state.player.facingLeft = 1;
    r.state.player.frame = 0;
    r.state.inv.haveKey = 1;
    REQUIRE(r.state.rooms.spotContent(11, 1) == kSpotGate);
    r.loop->startClimb();
    r.engine->input().post(WinMsg::keyDown(0x26));
    int cutscene = -1;
    ClimbHooks ch;
    ch.levelCutscene = [&](int lvl) { cutscene = lvl; };
    const RoomChange rc = r.loop->visitRoom(ch);
    CHECK(rc.oldRoom == 11);
    CHECK(rc.newRoom == 23);
    CHECK(rc.levelChanged);
    CHECK(rc.transition == 1);
    CHECK(cutscene == 0);
    CHECK(r.state.inv.haveKey == 0);
    CHECK(r.loop->sceneState().gateUnlocking == 0);
    CHECK(r.loop->elves().confined == 0);  // reset by the respawn
    REQUIRE(r.loop->elves().count() == 6);
    for (int i = 0; i < 6; ++i) CHECK(gameTables().rooms[size_t(r.loop->elves().elves()[size_t(i)].room)].level == 1);
    CHECK(r.state.player.x == gameTables().rooms[23].startX);
}
