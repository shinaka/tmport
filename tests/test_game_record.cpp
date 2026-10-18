// GameState <-> users.dat record mapping (game/game_state.h), byte for byte, on synthetic records
// and on a real users.dat written by the original (TM_USERS_DAT, read only). On the reference file
// of REFERENCE_CAPTURES.md (identified by its hash) the test also re-derives every stored climb:
// the layout records behind the saved spots and the srand() seed whose quiz deal and layout draw
// reproduce the saved slots exactly (end-to-end check of the RNG call order, 1118:00e5..).

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <vector>

#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/layout.h"
#include "game/quiz.h"
#include "game/rules.h"
#include "save/user_record.h"
#include "test_data.h"
#include "util/file_util.h"
#include "util/hash.h"

using namespace tmport;
using namespace tmport::game;

namespace {

using Record = std::array<uint8_t, kRecordSize>;

Record patternRecord(uint32_t seed) {
    Record r{};
    uint32_t x = seed;
    for (auto& b : r) {
        x = x * 1103515245u + 12345u;
        b = static_cast<uint8_t>(x >> 16);
    }
    return r;
}

constexpr uint64_t kReferenceUsersDatHash = 0x4bf2ccb10d895db1ull;  // REFERENCE_CAPTURES.md §3 file

// Layout record indices (tier 0) behind each reference player's spots, from
// extracted/scratch/game/ref_vectors.py (othr_tables.place_record matched against the file).
constexpr std::array<std::array<int, 3>, 7> kReferenceLayouts = {{
    {240, 388, 72}, {460, 210, 15}, {355, 169, 18}, {5, 413, 325}, {90, 52, 219}, {311, 123, 358}, {398, 245, 276},
}};

}  // namespace

TEST_CASE("record: fromRecord -> toRecord is byte-identical and keeps unknown bytes") {
    for (uint32_t seed : {1u, 2u, 77u}) {
        const Record r = patternRecord(seed);
        GameState s;
        fromRecord(s, r);
        CHECK(toRecord(s) == r);
        // Writing into another buffer changes only the mapped fields.
        Record other = patternRecord(seed + 100);
        writeRecord(s, other);
        for (size_t o = rec::kUnused1; o < rec::kSpotKind; ++o) CHECK(other[o] == patternRecord(seed + 100)[o]);
        CHECK(other[rec::kUnused2] == patternRecord(seed + 100)[rec::kUnused2]);
        for (size_t o = rec::kTier; o < rec::kUnused1; ++o) CHECK(other[o] == r[o]);
        for (size_t o = rec::kSpotKind; o < rec::kUnused2; ++o) CHECK(other[o] == r[o]);
        CHECK(other[rec::kClimbsDone] == r[rec::kClimbsDone]);
        CHECK(other[rec::kClimbsDone + 1] == r[rec::kClimbsDone + 1]);
    }
}

TEST_CASE("record: field mapping and name strcpy semantics") {
    GameState s;
    s.player.setName("Abcdefghij");  // truncated to 8 + NUL
    CHECK(s.player.nameString() == "Abcdefgh");
    s.rank.rankTier = 2;
    s.rank.totalTreasures = 0x01020304;
    s.options = {0, 1, 0};
    s.hints.unseen = 0xA5A5A5A5u;
    s.inv.haveKey = 1;
    s.inv.coins = 0x1234;
    s.inv.nets = 7;
    s.inv.gemsFound = 3;
    s.inv.climbPoints = 0x0102;
    s.shop.coinsLeft = -2;
    s.inv.numClues = 2;
    s.clues.flags = {0x0B, 0x0D, 0x07};
    s.clues.attr = {{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}};
    s.clues.plural = {1, 0, 1};
    s.quiz.tier = 4;
    s.quiz.status[0] = 0x81;
    s.quiz.status[358] = 3;
    s.quiz.slots[8] = QuizSlot{358, {2, 1, 0}, {0, 1, 0}};
    s.player.room = 17;
    s.rooms.spotKind(49, 2) = 0xAB;
    s.rooms.spotContent(49, 2) = 0xCD;
    s.rank.climbsDone = 0x0201;
    Record r{};
    r.fill(0xEE);
    writeRecord(s, r);
    CHECK(r[0] == 'A');
    CHECK(r[8] == 0);
    CHECK(r[rec::kTier] == 2);
    CHECK(r[rec::kTotal] == 4);
    CHECK(r[rec::kTotal + 3] == 1);
    CHECK(r[rec::kSoundOn] == 0);
    CHECK(r[rec::kVoiceOn] == 1);
    CHECK(r[rec::kHintUnseen] == 0xA5);
    CHECK(r[rec::kCoins] == 0x34);
    CHECK(r[rec::kCoins + 1] == 0x12);
    CHECK(r[rec::kShopCoinsLeft] == 0xFE);
    CHECK(r[rec::kShopCoinsLeft + 1] == 0xFF);
    CHECK(r[rec::kClueAttr + 4] == 5);
    CHECK(r[rec::kQuizStatus] == 0x81);
    CHECK(r[rec::kQuizSlots + 8 * 8] == 0x66);  // 358 = 0x166
    CHECK(r[rec::kQuizSlots + 8 * 8 + 1] == 0x01);
    CHECK(r[rec::kQuizSlots + 8 * 8 + 2] == 2);
    CHECK(r[rec::kQuizSlots + 8 * 8 + 6] == 1);
    CHECK(r[rec::kRoom] == 17);
    CHECK(r[rec::kSpotKind + 149] == 0xAB);
    CHECK(r[rec::kSpotContent + 149] == 0xCD);
    CHECK(r[rec::kClimbsDone] == 1);
    CHECK(r[rec::kClimbsDone + 1] == 2);
    CHECK(r[rec::kUnused1] == 0xEE);
    CHECK(r[rec::kUnused2] == 0xEE);
    // A shorter name: strcpy writes the NUL, later bytes keep the record's values.
    s.player.setName("Bo");
    writeRecord(s, r);
    CHECK(r[2] == 0);
    CHECK(r[3] == 'd');
    // Loading copies up to the NUL; the state's later name bytes are kept (strcpy).
    GameState t;
    t.player.setName("Zyxwvuts");
    fromRecord(t, r);
    CHECK(t.player.nameString() == "Bo");
    CHECK(t.player.name[3] == 'w');
    // Round trip through the save area's typed record.
    GameState u;
    fromRecord(u, r);
    const save::PlayerRecord pr = toPlayerRecord(u);
    CHECK(pr.coins == 0x1234);
    CHECK(pr.shopCoinsLeft == -2);
    CHECK(pr.quizSlots[8].id == 358);
    GameState v;
    v.recordImage = r;
    fromPlayerRecord(v, pr);
    CHECK(toRecord(v) == r);
}

TEST_CASE("record: real users.dat records round-trip; reference climbs re-derived") {
    const auto path = getEnv("TM_USERS_DAT");
    if (!path) {
        MESSAGE("TM_USERS_DAT is not set: test skipped");
        return;
    }
    const auto file = readWholeFile(pathFromUtf8(*path));
    REQUIRE(file);
    REQUIRE(file->size() >= 4 + 7 * kRecordSize);
    std::vector<Record> records;
    for (size_t i = 0; i < 7; ++i) {
        Record r;
        std::copy_n(file->begin() + 4 + std::ptrdiff_t(i * kRecordSize), kRecordSize, r.begin());
        if (r[0] == 0) break;
        records.push_back(r);
    }
    MESSAGE(std::format("{} records", records.size()));
    for (const Record& r : records) {
        GameState s;
        fromRecord(s, r);
        CHECK(toRecord(s) == r);
        save::UserSlot slot;
        slot.bytes = r;
        CHECK(toPlayerRecord(s) == save::PlayerRecord::fromSlot(slot));
        // Structural facts of any saved climb in progress (ARCHITECTURE.md §5.3).
        if (s.player.room != kRoomNewClimb) {
            for (int L = 0; L < kLevelCount; ++L) {
                for (const RoomSlot g : layoutGateSlots()[size_t(L)]) CHECK(s.rooms.spotContent(g.room, g.slot) == kSpotGate);
                const RoomSlot sh = layoutShopSlots()[size_t(L)];
                CHECK(s.rooms.spotKind(sh.room, sh.slot) == kSpotShop);
                int keys = 0;
                for (const RoomSlot sp : layoutSpotSlots()[size_t(L)]) {
                    CHECK(s.rooms.spotKind(sp.room, sp.slot) < 112);
                    keys += s.rooms.spotContent(sp.room, sp.slot) == kContentKey ? 1 : 0;
                }
                CHECK(keys <= 1);
            }
        }
    }

    if (fnv1a64(*file) != kReferenceUsersDatHash) {
        MESSAGE("not the REFERENCE_CAPTURES.md users.dat: reference checks skipped");
        return;
    }
    REQUIRE(records.size() == 7);
    // REFERENCE_CAPTURES.md §3 / §4.8: every player is at room 9 of a fresh tier-0 climb.
    for (const Record& r : records) {
        GameState s;
        fromRecord(s, r);
        CHECK(s.player.room == 9);
        CHECK(s.rank.rankTier == 0);
        CHECK(s.inv.coins == 10);
        CHECK(s.inv.nets == 10);
        CHECK(s.shop.coinsLeft == 4);
        CHECK(s.hints.unseen == 0xFFFFFFFAu);  // hints 0 and 2 were queued
        for (int L = 0; L < kLevelCount; ++L) {
            int counts[4] = {0, 0, 0, 0};
            for (const RoomSlot sp : layoutSpotSlots()[size_t(L)]) ++counts[s.rooms.spotContent(sp.room, sp.slot) & 3];
            CHECK(counts[0] == 10);
            CHECK(counts[1] == 5);
            CHECK(counts[2] == 2);
            CHECK(counts[3] == 1);
        }
    }
    // The first player's level 0 as listed in REFERENCE_CAPTURES.md §3.
    {
        GameState s;
        fromRecord(s, records[0]);
        auto spot = [&](int room, int slot) { return std::pair<int, int>{s.rooms.spotKind(room, slot), s.rooms.spotContent(room, slot)}; };
        CHECK(spot(10, 0) == std::pair<int, int>{18, kContentTreasure});
        CHECK(spot(10, 1).first == kSpotNone);
        CHECK(spot(10, 2) == std::pair<int, int>{99, kContentEmpty});
        CHECK(spot(11, 1).second == kSpotGate);
        CHECK(spot(11, 2).second == kSpotGate);
        CHECK(spot(12, 0).second == kContentDecoy);
        CHECK(spot(12, 1).second == kContentEmpty);
        CHECK(spot(13, 0) == std::pair<int, int>{10, kContentKey});
        CHECK(spot(13, 1).second == kSpotShop);
        CHECK(spot(14, 0).second == kContentDecoy);
        CHECK(spot(15, 0).second == kContentEmpty);
        CHECK(spot(16, 0).second == kContentDecoy);
        CHECK(spot(18, 1).second == kContentDecoy);
        CHECK(spot(22, 0).second == kContentDecoy);
        CHECK(spot(22, 1).second == kContentTreasure);
    }

    TM_REQUIRE_GAME_DATA();
    const GameTables t = GameTables::load(*g->exe, *g->res);
    // applyLayoutRecord + Clue_ResolveAttributes reproduce the saved spots and clue state.
    for (size_t i = 0; i < records.size(); ++i) {
        CAPTURE(i);
        GameState saved;
        fromRecord(saved, records[i]);
        RoomTable rooms = RoomTable::fromDefs(t.rooms);
        rooms.clearSpots();
        ClueState clues;
        for (size_t L = 0; L < 3; ++L) {
            const LayoutRecord& lr = t.layouts[0][size_t(kReferenceLayouts[i][L])];
            applyLayoutRecord(rooms, int(L), lr);
            clues.flags[L] = lr.mask;
            clues.keyKind[L] = lr.keyKind;
        }
        resolveClueAttributes(clues, t);
        bool same = true;
        for (int room = 0; room < kRoomCount; ++room)
            for (int sl = 0; sl < 3; ++sl)
                same = same && rooms.spotKind(room, sl) == saved.rooms.spotKind(room, sl) &&
                       rooms.spotContent(room, sl) == saved.rooms.spotContent(room, sl);
        CHECK(same);
        CHECK(clues.flags == saved.clues.flags);
        CHECK(clues.attr == saved.clues.attr);
        CHECK(clues.plural == saved.clues.plural);
    }
    // Brute force over all 65536 srand() seeds: Quiz_NewRound on a new player's history, then the
    // layout draw. Exactly one seed must reproduce each player's nine slots, dealt flags and
    // layout records.
    QuizState fresh;
    std::vector<int> matches(records.size(), 0);
    std::vector<uint32_t> seeds(records.size(), 0);
    std::vector<GameState> saved(records.size());
    for (size_t i = 0; i < records.size(); ++i) fromRecord(saved[i], records[i]);
    for (uint32_t seed = 0; seed <= 0xFFFF; ++seed) {
        QuizState q = fresh;
        BorlandRand rng;
        rng.srand(uint16_t(seed));
        quizNewRound(q, t.quiz, rng);
        const LayoutChoice c = pickLayoutRecords(int(t.layouts[0].size()), rng);
        for (size_t i = 0; i < records.size(); ++i) {
            if (q.slots != saved[i].quiz.slots) continue;
            if (c.record != kReferenceLayouts[i]) continue;
            if (q.status != saved[i].quiz.status || q.tier != saved[i].quiz.tier) continue;
            ++matches[i];
            seeds[i] = seed;
        }
    }
    for (size_t i = 0; i < records.size(); ++i) {
        CAPTURE(i);
        MESSAGE(std::format("player {}: seed {:#06x}", i, seeds[i]));
        CHECK(matches[i] == 1);
    }
}
