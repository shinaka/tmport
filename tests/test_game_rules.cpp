// Economy, ranks, hints, treasures, coin pile, net shop, treasure hall and climb flow
// (game/rules.h, game/room_table.h). Table values are facts checked against the user's EXE.

#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "game/game_hooks.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/layout.h"
#include "game/room_table.h"
#include "game/rules.h"
#include "test_data.h"

using namespace tmport;
using namespace tmport::game;

namespace {

const GameTables& tables() {
    static GameTables t = GameTables::load(*testdata::game()->exe, *testdata::game()->res);
    return t;
}

// The hint/rank tables as documented (ARCHITECTURE.md §5.12, §5.15), for tests without TM_DATA.
GameTables syntheticTables() {
    GameTables t;
    t.rankThresholds = {0, 5, 25, 70, 115, 170, 230, 300};
    t.gemsNeeded = {2, 3, 4, 5};
    t.elfCountByTier = {6, 8, 10, 12};
    t.creatureSpawnPermille = {0, 50, 100, 200};
    t.hintBit = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, 10, 11, 12, -1, 13, 14, -1, 15, 16, -1, 17, 18, -1, 19, 20, -1, 21, 22, -1, 23, 24, -1, 25};
    t.room9RankHints = {11, 13, 16, 19, 22, 25, 28, 31};
    t.rankPromoHints = {-1, 12, 15, 18, 21, 24, 27, 30};
    t.rankStayHints = {10, 14, 17, 20, 23, 26, 29, 32};
    t.shopWalkX = {220, 112, 378};
    t.netSignX = {207, 99, 365};
    for (int r = 0; r < kRoomCount; ++r) {
        RoomDef& d = t.rooms[size_t(r)];
        d.level = r < 23 ? 0 : r < 36 ? 1 : r < 49 ? 2 : 3;
        d.music = r < 9 ? 0 : r < 23 ? 1 : r < 36 ? 3 : r < 49 ? 5 : 7;
        d.startX = int16_t(100 + r);
        d.startY = 142;
        d.startFrame = 60;
        d.startFacingLeft = uint8_t(r & 1);
    }
    return t;
}

struct RecordingHooks : GameHooks {
    std::vector<int> sfx;
    int hud = 0;
    std::vector<bool> sound;
    std::vector<int> spawned;
    int unloaded = 0;
    void playSfx(int n) override { sfx.push_back(n); }
    void hudChanged() override { ++hud; }
    void soundToggled(bool on) override { sound.push_back(on); }
    void spawnElves(GameState&, int count) override { spawned.push_back(count); }
    void unloadElves(GameState&) override { ++unloaded; }
};

}  // namespace

TEST_CASE("rules: tables read from the EXE") {
    TM_REQUIRE_GAME_DATA();
    const GameTables t = GameTables::fromExe(*g->exe);
    const GameTables s = syntheticTables();
    CHECK(t.rankThresholds == s.rankThresholds);
    CHECK(t.gemsNeeded == s.gemsNeeded);
    CHECK(t.elfCountByTier == s.elfCountByTier);
    CHECK(t.creatureSpawnPermille == s.creatureSpawnPermille);
    CHECK(t.hintBit == s.hintBit);
    CHECK(t.room9RankHints == s.room9RankHints);
    CHECK(t.rankStayHints == s.rankStayHints);
    for (size_t r = 1; r < 8; ++r) CHECK(t.rankPromoHints[r] == s.rankPromoHints[r]);
    CHECK(t.shopWalkX == s.shopWalkX);
    CHECK(t.netSignX == s.netSignX);
    // Clue words: index 0 is the empty string in every table, the others are not.
    CHECK(t.words.noun[0].empty());
    for (size_t i = 1; i < 16; ++i) CHECK_FALSE(t.words.noun[i].empty());
    for (size_t i = 1; i < 16; ++i) CHECK_FALSE(t.words.nounPlural[i].empty());
    for (size_t i = 1; i < 8; ++i) CHECK_FALSE(t.words.count[i].empty());
    for (size_t i = 1; i < 5; ++i) CHECK_FALSE(t.words.size[i].empty());
    for (size_t i = 1; i < 5; ++i) CHECK_FALSE(t.words.shape[i].empty());
    // Room table: levels and zones by room (ARCHITECTURE.md §5.2).
    for (int r = 0; r < kRoomCount; ++r) {
        CHECK(t.rooms[size_t(r)].level == s.rooms[size_t(r)].level);
        CHECK(t.rooms[size_t(r)].music == s.rooms[size_t(r)].music);
    }
}

TEST_CASE("rules: ranks and tiers") {
    const GameTables t = syntheticTables();
    const std::pair<uint32_t, int> cases[] = {{0, 0}, {4, 0}, {5, 1}, {24, 1}, {25, 2}, {69, 2}, {70, 3},
                                              {114, 3}, {115, 4}, {169, 4}, {170, 5}, {229, 5}, {230, 6},
                                              {299, 6}, {300, 7}, {0xFFFFFFF0u, 7}};
    for (auto [total, rank] : cases) {
        CAPTURE(total);
        CHECK(rankFromTotal(total, t) == rank);
        GameState s;
        s.rank.totalTreasures = total;
        updateRankTier(s, t);
        CHECK(s.rank.rankTier == rank / 2);
    }
}

TEST_CASE("rules: coins and nets refuse to go below 0; HUD digits") {
    GameState s;
    RecordingHooks h;
    s.inv.coins = 1;
    CHECK(addCoins(s, -1, h));
    CHECK(s.inv.coins == 0);
    CHECK_FALSE(addCoins(s, -1, h));
    CHECK(s.inv.coins == 0);
    CHECK(addCoins(s, 10, h));
    CHECK_FALSE(addCoins(s, -11, h));
    CHECK(addCoins(s, -10, h));
    CHECK(h.hud == 3);
    s.inv.nets = 0;
    CHECK_FALSE(addNets(s, -1, h));
    CHECK(addNets(s, 10, h));
    CHECK(s.inv.nets == 10);
    CHECK(hudTwoDigits(0) == "0");
    CHECK(hudTwoDigits(9) == "9");
    CHECK(hudTwoDigits(10) == "10");
    CHECK(hudTwoDigits(123) == "23");  // modulo 100
    CHECK(hudTwoDigits(205) == "05");
    CHECK(hudPointsDigits(7) == "7");
    CHECK(hudPointsDigits(65535) == "65535");
}

TEST_CASE("rules: hint queue (once-only bits consumed when queued, one pending)") {
    const GameTables t = syntheticTables();
    GameState s;
    queueHint(s, 4, t);
    CHECK(s.hints.pending == 4);
    CHECK((s.hints.unseen & (1u << 4)) == 0);
    queueHint(s, 9, t);  // already one pending: ignored, bit 9 kept
    CHECK(s.hints.pending == 4);
    CHECK((s.hints.unseen & (1u << 9)) != 0);
    CHECK(takePendingHint(s) == 4);
    CHECK(s.hints.pending == -1);
    queueHint(s, 4, t);  // once only
    CHECK(s.hints.pending == -1);
    queueHint(s, 10, t);  // always shown (bit -1)
    CHECK(s.hints.pending == 10);
    takePendingHint(s);
    queueHint(s, 10, t);
    CHECK(s.hints.pending == 10);
    takePendingHint(s);
    queueHint(s, 33, t);  // bit 25
    CHECK(s.hints.pending == 33);
    takePendingHint(s);
    queueHint(s, 34, t);  // outside the table
    queueHint(s, -1, t);
    CHECK(s.hints.pending == -1);

    // Option off: mask cleared and pending dropped; always-shown hints still appear.
    s.hints.pending = 5;
    setHintsOption(s, false);
    CHECK(s.hints.unseen == 0);
    CHECK(s.hints.pending == -1);
    CHECK(s.options.hintsOn == 0);
    queueHint(s, 3, t);
    CHECK(s.hints.pending == -1);
    queueHint(s, 14, t);
    CHECK(s.hints.pending == 14);
    setHintsOption(s, true);
    CHECK(s.hints.unseen == 0xFFFFFFFFu);
}

TEST_CASE("rules: room-tick hints 0, 2 and the room-9 rank hint") {
    const GameTables t = syntheticTables();
    GameState s;
    s.player.room = 9;
    roomTickHints(s, t);
    CHECK(s.hints.pending == 0);
    CHECK(takePendingHint(s) == 0);
    roomTickHints(s, t);
    CHECK(s.hints.pending == 2);
    takePendingHint(s);
    roomTickHints(s, t);
    CHECK(s.hints.pending == -1);  // first climb: no rank hint
    CHECK(s.hints.unseen == 0xFFFFFFFAu);  // what the shipped users.dat records hold
    s.rank.climbsDone = 1;
    s.rank.totalTreasures = 30;  // rank 2 -> hint 16 (bit 14)
    roomTickHints(s, t);
    CHECK(s.hints.pending == 16);
    takePendingHint(s);
    roomTickHints(s, t);
    CHECK(s.hints.pending == -1);
    s.player.room = 10;
    s.rank.totalTreasures = 300;
    roomTickHints(s, t);
    CHECK(s.hints.pending == -1);
}

TEST_CASE("rules: Space / Enter economy and hints 5, 6, 7") {
    const GameTables t = syntheticTables();
    GameState s;
    s.inv.coins = 0;
    s.inv.nets = 1;
    CHECK(pressNetKey(s, t));
    CHECK(s.inv.nets == 0);
    CHECK_FALSE(pressCoinKey(s, t));  // no coins, nets 0 now -> hint 7
    CHECK(s.hints.pending == 7);
    takePendingHint(s);
    GameState u;
    u.inv.coins = 3;
    u.inv.nets = 0;
    CHECK_FALSE(pressNetKey(u, t));
    CHECK(u.hints.pending == 5);
    CHECK(u.inv.nets == 0);
    GameState w;
    w.inv.coins = 0;
    w.inv.nets = 4;
    CHECK_FALSE(pressCoinKey(w, t));
    CHECK(w.hints.pending == 6);
    CHECK(pressCoinKey(u, t));
    CHECK(u.inv.coins == 2);
}

TEST_CASE("rules: coin toss results, key, treasure and the net shop") {
    const GameTables t = syntheticTables();
    GameState s = GameState::initial(t);
    RecordingHooks h;
    s.rooms.clearSpots();
    s.player.room = 10;
    s.rooms.spotContent(10, 0) = kContentTreasure;
    s.rooms.spotContent(10, 1) = kContentKey;
    s.rooms.spotContent(10, 2) = kContentDecoy;
    TossOutcome o = resolveCoinToss(s, 0, t, h);
    CHECK(o.result == TossResult::Treasure);
    CHECK(s.rooms.spotContent(10, 0) == kContentEmpty);
    CHECK(s.inv.gemsFound == 1);
    CHECK(s.inv.treasureCount2 == 1);
    CHECK(s.inv.climbPoints == 1);
    CHECK(s.hints.pending == 4);
    takePendingHint(s);
    o = resolveCoinToss(s, 1, t, h);
    CHECK(o.result == TossResult::Key);
    CHECK(s.inv.haveKey == 1);
    CHECK(s.hints.pending == 9);
    CHECK(s.rooms.spotContent(10, 1) == kContentEmpty);
    CHECK(resolveCoinToss(s, 2, t, h).result == TossResult::Nothing);
    CHECK(s.rooms.spotContent(10, 2) == kContentDecoy);  // decoys are left alone
    CHECK(resolveCoinToss(s, 0xFF, t, h).result == TossResult::Nothing);
    CHECK(h.sfx == std::vector<int>{41, 12});
    // A treasure in the castle (level 3) gives no climb point.
    s.player.room = 49;
    s.rooms.spotContent(49, 0) = kContentTreasure;
    resolveCoinToss(s, 0, t, h);
    CHECK(s.inv.climbPoints == 1);
    CHECK(s.inv.gemsFound == 2);

    // Gates: hint 8 without the key, the walk target with it.
    {
        GameState gs = GameState::initial(t);
        gs.player.room = 25;
        GateOutcome go = tryGate(gs, t);
        CHECK_FALSE(go.opens);
        CHECK(gs.hints.pending == 8);
        gs.inv.haveKey = 1;
        go = tryGate(gs, t);
        CHECK(go.opens);
        CHECK(go.walkToX == 281);
        gs.player.room = 11;
        CHECK(tryGate(gs, t).walkToX == 330);
        gs.player.room = 38;
        CHECK(tryGate(gs, t).walkToX == 170);
    }

    // Net shop of level 1 (room 24 slot 0).
    s.player.room = 24;
    s.rooms.spotKind(24, 0) = s.rooms.spotContent(24, 0) = kSpotShop;
    s.shop.coinsLeft = 4;
    for (int i = 0; i < 3; ++i) CHECK(resolveCoinToss(s, 0, t, h).result == TossResult::ShopCoin);
    CHECK(s.shop.coinsLeft == 1);
    o = resolveCoinToss(s, 0, t, h);
    CHECK(o.result == TossResult::ShopOpens);
    CHECK(o.walkToX == 112);
    CHECK(s.shop.state == 1);
    CHECK(s.shop.coinsLeft == 0);
    CHECK(s.rooms.spotContent(24, 0) == kSpotShop);
    const uint16_t nets = s.inv.nets;
    netShopTakeNet(s, h);
    CHECK(s.inv.nets == nets + 10);
    CHECK(s.shop.coinsLeft == 4);
    CHECK(s.shop.state == 3);
}

TEST_CASE("rules: treasure picks (1198:0103) in the original call order") {
    const GameTables t = syntheticTables();
    GameState s = GameState::initial(t);
    s.player.room = 10;
    for (uint8_t tier = 0; tier < 4; ++tier) {
        for (uint16_t climbs : std::initializer_list<uint16_t>{0, 1, 42, 43, 50, 300}) {
            for (uint8_t found : std::initializer_list<uint8_t>{0, 1}) {
                CAPTURE(int(tier));
                CAPTURE(climbs);
                s.rank.rankTier = tier;
                s.rank.climbsDone = climbs;
                s.inv.treasureCount2 = found;
                BorlandRand rng, ref;
                rng.srand(uint16_t(climbs * 7 + tier));
                ref = rng;
                pickTreasuresForLevel(s, t, rng);
                const int base = climbs % 43;
                CHECK(s.treasures.count == t.gemsNeeded[tier]);
                // Reference draw: element 0 = base on the first pick of a climb, the others
                // base + R(51 - base) re-rolled until distinct.
                std::vector<int> expect;
                for (int i = 0; i < t.gemsNeeded[tier]; ++i) {
                    int v = base;
                    if (i != 0 || found) {
                        do {
                            v = base + ref.random(51 - base);
                        } while (std::find(expect.begin(), expect.end(), v) != expect.end());
                    }
                    expect.push_back(v);
                }
                for (int i = 0; i < s.treasures.count; ++i) CHECK(s.treasures.index[size_t(i)] == expect[size_t(i)]);
                CHECK(rng.seed() == ref.seed());
                std::set<int> distinct(s.treasures.index.begin(), s.treasures.index.begin() + s.treasures.count);
                CHECK(int(distinct.size()) == s.treasures.count);
            }
        }
    }
    // The n-th find shows list[n].
    s.inv.gemsFound = 1;
    CHECK(treasureForNextFind(s) == s.treasures.index[1]);
    // No pick in the castle (level 3).
    s.player.room = 49;
    s.treasures.count = 0;
    BorlandRand rng;
    pickTreasuresForLevel(s, t, rng);
    CHECK(s.treasures.count == 0);
    // Delivered treasure after climbsDone++.
    CHECK(deliveredTreasureIndex(1) == 0);
    CHECK(deliveredTreasureIndex(43) == 42);
    CHECK(deliveredTreasureIndex(44) == 0);
    CHECK(deliveredTreasureIndex(100) == 99 % 43);
}

TEST_CASE("rules: rescue coin pile (1110:00aa)") {
    const GameTables t = syntheticTables();
    GameState s = GameState::initial(t);
    RecordingHooks h;
    BorlandRand rng;
    rng.srand(321);
    s.inv.coins = 0;
    s.inv.nets = 0;
    s.player.room = 10;
    s.player.x = 0;
    s.player.w = 40;
    updateCoinPile(s, rng, h);
    CHECK(s.coinPile.room == 0xFE);  // armed
    CHECK(s.coinPile.noSpawnRoom == 10);
    updateCoinPile(s, rng, h);  // still in the room where the player ran out
    CHECK(s.coinPile.room == 0xFE);
    s.player.room = 0;  // treasure hall: never there
    updateCoinPile(s, rng, h);
    CHECK(s.coinPile.room == 0xFE);
    s.player.room = 11;
    BorlandRand ref = rng;
    updateCoinPile(s, rng, h);
    CHECK(s.coinPile.room == 11);
    CHECK(s.coinPile.noSpawnRoom == 0xFF);
    CHECK(s.coinPile.x == 50 + ref.random(412));
    s.coinPile.w = 52;
    s.player.room = 12;  // left behind: it moves to the next room entered
    updateCoinPile(s, rng, h);
    CHECK(s.coinPile.room == 12);
    s.player.room = 10;  // the no-spawn room is forgotten once the pile appeared
    updateCoinPile(s, rng, h);
    CHECK(s.coinPile.room == 10);
    // Pick-up: horizontal overlap only.
    s.player.x = int16_t(s.coinPile.x + s.coinPile.w + 1);
    CHECK_FALSE(updateCoinPile(s, rng, h));
    s.player.x = int16_t(s.coinPile.x + s.coinPile.w);
    CHECK(updateCoinPile(s, rng, h));
    CHECK(s.inv.coins == 10);
    CHECK(s.coinPile.room == 0xFF);
    CHECK(h.sfx == std::vector<int>{7});
    // Coins again: nothing armed.
    updateCoinPile(s, rng, h);
    CHECK(s.coinPile.room == 0xFF);
    // Cancelled as soon as the player has a net again.
    s.inv.coins = 0;
    updateCoinPile(s, rng, h);
    CHECK(s.coinPile.room == 0xFE);
    s.inv.nets = 1;
    updateCoinPile(s, rng, h);
    CHECK(s.coinPile.room == 0xFF);
}

TEST_CASE("rules: treasure hall rooms and pedestals (1190:0000, 1190:009b)") {
    const std::pair<int, int> lengths[] = {{0, 2}, {1, 2}, {2, 2}, {7, 2}, {8, 3}, {13, 3}, {14, 4},
                                           {44, 9}, {50, 9}, {1000, 9}, {65535, 9}};
    for (auto [climbs, n] : lengths) CHECK(treasureHallLength(uint16_t(climbs)) == n);
    const GameTables t = syntheticTables();
    RoomTable rooms = RoomTable::fromDefs(t.rooms);
    rooms.buildTreasureHall(20);  // 5 rooms
    CHECK(rooms[0].exitRight == 1);
    for (int r = 1; r <= 3; ++r) {
        CHECK(rooms[r].exitLeft == r - 1);
        CHECK(rooms[r].exitRight == r + 1);
        CHECK(rooms[r].bgPmap == kHallMiddleBackground);
    }
    CHECK(rooms[4].exitLeft == 3);
    CHECK(rooms[4].exitRight == 0xFF);
    CHECK(rooms[4].bgPmap == kHallLastBackground);
    rooms.buildTreasureHall(0);  // 2 rooms; longer chains of earlier players stay but are cut off
    CHECK(rooms[1].exitRight == 0xFF);
    CHECK(rooms[1].bgPmap == kHallLastBackground);

    CHECK(hallPedestalTreasures(0, 0) == std::array<int8_t, 6>{-1, -1, -1, -1, -1, -1});
    CHECK(hallPedestalTreasures(1, 0) == std::array<int8_t, 6>{0, -1, -1, -1, -1, -1});
    CHECK(hallPedestalTreasures(44, 0)[0] == 0);  // 43 % 43
    CHECK(hallPedestalTreasures(9, 1) == std::array<int8_t, 6>{7, 6, 5, 4, 3, 2});
    CHECK(hallPedestalTreasures(9, 2) == std::array<int8_t, 6>{1, 0, -1, -1, -1, -1});
    CHECK(hallPedestalTreasures(50, 1) == std::array<int8_t, 6>{5, 4, 3, 2, 1, 0});  // 48 % 43 = 5
    // After 128 climbs the signed-char index wraps negative: the pedestal stays empty (kept quirk).
    CHECK(hallPedestalTreasures(130, 0)[0] == -1);  // (char)129 = -127
    CHECK(hallPedestalTreasures(128, 0)[0] == 41);  // (char)127 % 43
}

TEST_CASE("rules: new player, climb set-up and climb end") {
    TM_REQUIRE_GAME_DATA();
    const GameTables& t = tables();
    GameState s = GameState::initial(t);
    RecordingHooks h;
    s.options.soundOn = 0;
    s.inv.haveKey = 1;  // left over from a previous player: reset by the climb set-up
    s.inv.climbPoints = 7;
    initNewPlayer(s, h);
    CHECK(h.sound == std::vector<bool>{true});
    CHECK(s.options.soundOn == 1);
    CHECK(s.player.room == kRoomNewClimb);
    CHECK(s.inv.coins == 10);
    CHECK(s.inv.nets == 10);
    CHECK(s.shop.coinsLeft == 4);

    BorlandRand rng;
    rng.srand(1);
    const ClimbSetup cs = setupClimb(s, t, rng, h);
    CHECK(cs.newClimb);
    CHECK(s.player.room == kMountainStartRoom);  // never finished a climb
    CHECK(s.player.newClimb == 1);
    CHECK(s.inv.haveKey == 0);
    CHECK(s.inv.climbPoints == 0);
    CHECK(h.spawned == std::vector<int>{6});
    CHECK(s.treasures.count == 2);
    CHECK(s.treasures.index[0] == 0);  // climbsDone 0, nothing found yet
    CHECK(s.player.x == t.rooms[9].startX);
    CHECK(s.player.y == t.rooms[9].startY);
    CHECK(s.player.frame == t.rooms[9].startFrame);
    CHECK(s.player.facingLeft == t.rooms[9].startFacingLeft);
    for (int L = 0; L < 3; ++L)
        for (int k = 0; k < 3; ++k) CHECK_FALSE(s.clues.text[size_t(L)][size_t(k)].empty());
    // The deal came first (45 rand calls), then the layout draw.
    {
        BorlandRand ref;
        ref.srand(1);
        for (int i = 0; i < 45; ++i) ref.rand();
        const LayoutChoice c = pickLayoutRecords(500, ref);
        CHECK(c.record == cs.layoutRecord);
    }
    // Layout written: tier-0 counts per level.
    for (int L = 0; L < 3; ++L) {
        int counts[4] = {0, 0, 0, 0};
        for (const RoomSlot rs : layoutSpotSlots()[size_t(L)]) ++counts[s.rooms.spotContent(rs.room, rs.slot)];
        CHECK(counts[0] == 10);
        CHECK(counts[1] == 5);
        CHECK(counts[2] == 2);
        CHECK(counts[3] == 1);
    }

    // Resume (room != 0xFE): no deal, no layout, newClimb 0.
    GameState r = s;
    r.player.room = 13;
    const auto quizBefore = r.quiz;
    BorlandRand rng2;
    const ClimbSetup rs = setupClimb(r, t, rng2, h);
    CHECK_FALSE(rs.newClimb);
    CHECK(r.player.newClimb == 0);
    CHECK(r.quiz == quizBefore);
    CHECK(r.player.room == 13);

    // Zone change without level change (room 9 -> hall room 0): treasures re-picked, clues kept.
    s.inv.numClues = 2;
    s.player.room = 0;
    CHECK_FALSE(onRoomChanged(s, 1, 0, t, rng, h));
    CHECK(s.inv.numClues == 2);
    // Level change through the gate 11 -> 23.
    s.player.room = 23;
    s.inv.haveKey = 1;
    s.inv.gemsFound = 2;
    CHECK(onRoomChanged(s, 1, 0, t, rng, h));
    CHECK(s.inv.numClues == 0);
    CHECK(s.inv.haveKey == 0);
    CHECK(s.inv.gemsFound == 0);
    CHECK(h.unloaded == 1);
    CHECK(h.spawned.back() == 6);
    // Same zone: nothing.
    CHECK_FALSE(onRoomChanged(s, 3, 1, t, rng, h));

    // Climb end: 3 treasures found, first climb.
    s.player.room = kRoomNewClimb;
    s.inv.climbPoints = 5;
    s.hints.pending = 4;  // the result hint replaces anything pending
    endClimbBeforeSave(s, h);
    const CelebrationInfo ci = celebrateClimb(s, t);
    CHECK(s.rank.climbsDone == 1);
    CHECK(s.rank.totalTreasures == 5);
    CHECK(ci.promoted);
    CHECK(ci.rank == 1);
    CHECK_FALSE(ci.champion);
    CHECK(ci.musicZone == -96);
    CHECK(ci.deliveredTreasure == 0);
    CHECK(s.hints.pending == 12);  // promoted to rank 1
    updateRankTier(s, t);
    CHECK(s.rank.rankTier == 0);
    // Next climb starts in the hall.
    BorlandRand rng3;
    rng3.srand(2);
    setupClimb(s, t, rng3, h);
    CHECK(s.player.room == kHallFirstRoom);
    // Not promoted: the always-shown stay hint.
    s.inv.climbPoints = 1;
    s.hints.pending = -1;
    const CelebrationInfo c2 = celebrateClimb(s, t);
    CHECK_FALSE(c2.promoted);
    CHECK(s.hints.pending == 14);
    // Champion and the saturating total.
    s.rank.totalTreasures = 0xFFFFFFE0u;
    s.inv.climbPoints = 0x18;
    const CelebrationInfo c3 = celebrateClimb(s, t);
    CHECK(s.rank.totalTreasures == 0xFFFFFFF0u);
    CHECK(c3.champion);
    CHECK_FALSE(c3.promoted);
    CHECK(c3.musicZone == -98);
    CHECK(c3.deliveredTreasure == 2);
    // As compiled the add wraps before the saturation test (unreachable in practice): a zero total
    // keeps the old rank and clears the champion flag.
    s.inv.climbPoints = 0x10;
    const CelebrationInfo c4 = celebrateClimb(s, t);
    CHECK(s.rank.totalTreasures == 0u);
    CHECK(c4.rank == 7);
    CHECK_FALSE(c4.champion);
}

TEST_CASE("rules: sound-on-load quirk (1160:137d)") {
    std::array<uint8_t, kRecordSize> rec{};
    rec[rec::kSoundOn] = 1;
    RecordingHooks h;
    GameState s;
    s.options.soundOn = 0;  // sound off now, record says on: stays off
    loadPlayerRecord(s, rec, h);
    CHECK(s.options.soundOn == 0);
    CHECK(h.sound.empty());
    rec[rec::kSoundOn] = 0;
    s.options.soundOn = 1;  // on now, record says off: switched off
    loadPlayerRecord(s, rec, h);
    CHECK(s.options.soundOn == 0);
    CHECK(h.sound == std::vector<bool>{false});
    rec[rec::kSoundOn] = 1;
    s.options.soundOn = 1;
    loadPlayerRecord(s, rec, h);
    CHECK(s.options.soundOn == 1);
    CHECK(h.sound.size() == 1);
}

TEST_CASE("rules: castle wall per-tier randomness") {
    const GameTables t = syntheticTables();
    BorlandRand rng, ref;
    rng.srand(77);
    ref.srand(77);
    for (uint8_t tier = 0; tier < 4; ++tier) {
        const int layout = pickCastleLayout(tier, rng);
        CHECK(layout == ref.random(tier + 1));
        CHECK(layout <= tier);
    }
    int spawns[4] = {0, 0, 0, 0};
    for (uint8_t tier = 0; tier < 4; ++tier)
        for (int i = 0; i < 4000; ++i) spawns[tier] += creatureSpawnRoll(tier, t, rng) ? 1 : 0;
    CHECK(spawns[0] == 0);
    CHECK(spawns[1] > 120);  // ~5 %
    CHECK(spawns[1] < 280);
    CHECK(spawns[3] > 650);  // ~20 %
    CHECK(spawns[3] < 950);
}
