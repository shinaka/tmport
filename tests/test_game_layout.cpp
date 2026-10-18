// Climb layout generation and clue words (game/layout.h) against the reference tool
// tools/assets/othr_tables.py (BorlandRand, pick_records, place_record, clue_values,
// clue_speech_ids). Expected values were produced by extracted/scratch/game/ref_vectors.py from
// the user's files; only record indices, numbers and FNV-1a hashes are stored here.

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "game/game_tables.h"
#include "game/layout.h"
#include "game/room_table.h"
#include "mods/mods.h"
#include "test_data.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/hash.h"

using namespace tmport;
using namespace tmport::game;

namespace {

const GameTables& tables() {
    static GameTables t = GameTables::load(*testdata::game()->exe, *testdata::game()->res);
    return t;
}

uint64_t spotHash(const RoomTable& rooms) {
    std::vector<uint8_t> blob;
    for (int r = 0; r < kRoomCount; ++r)
        for (int s = 0; s < 3; ++s) blob.push_back(rooms.spotKind(r, s));
    for (int r = 0; r < kRoomCount; ++r)
        for (int s = 0; s < 3; ++s) blob.push_back(rooms.spotContent(r, s));
    return fnv1a64(blob);
}

struct LayoutVector {
    int tier;
    uint16_t seed;
    std::array<int, 3> rec;
    uint64_t hash;
    int next;  // the rand() value after the draw
    std::array<uint8_t, 3> masks;
    std::array<std::array<uint8_t, 3>, 3> attrs;
    std::array<uint8_t, 3> plural;
    std::array<std::array<uint16_t, 3>, 3> voices;
};

// ref_vectors.py output ("L ..." lines).
const LayoutVector kVectors[] = {
    {0, 1, {5, 1, 167}, 0xf7d791eaf636b623ull, 1090, {7, 13, 11}, {{{3, 2, 4}, {5, 2, 8}, {1, 1, 6}}}, {1, 1, 0}, {{{47, 50, 26}, {42, 46, 30}, {38, 49, 13}}}},
    {0, 2, {10, 498, 333}, 0x24ce13b52a3baa67ull, 23967, {11, 11, 11}, {{{2, 4, 12}, {2, 2, 5}, {7, 1, 7}}}, {1, 1, 1}, {{{39, 52, 34}, {39, 50, 27}, {44, 49, 29}}}},
    {0, 4660, {124, 170, 134}, 0x9c4f57e90ab0f0c2ull, 19970, {7, 7, 11}, {{{1, 2, 1}, {2, 4, 2}, {1, 1, 6}}}, {0, 1, 0}, {{{45, 50, 8}, {46, 52, 24}, {38, 49, 13}}}},
    {0, 48879, {286, 158, 201}, 0x44c0c2b91738d224ull, 3628, {11, 11, 11}, {{{3, 2, 1}, {1, 2, 1}, {4, 2, 10}}}, {1, 0, 1}, {{{40, 50, 23}, {38, 50, 8}, {41, 50, 32}}}},
    {0, 65535, {300, 348, 209}, 0x554cd371c54b7427ull, 4027, {7, 11, 13}, {{{3, 4, 6}, {1, 3, 2}, {5, 2, 5}}}, {1, 0, 1}, {{{47, 52, 28}, {38, 51, 9}, {42, 46, 27}}}},
    {0, 31337, {91, 384, 484}, 0x84c642d1a8587da7ull, 18271, {11, 11, 13}, {{{3, 4, 12}, {2, 1, 11}, {5, 2, 10}}}, {1, 1, 1}, {{{40, 52, 34}, {39, 49, 33}, {42, 46, 32}}}},
    {0, 0, {0, 5, 1}, 0x08e5c1c405cf9deeull, 10982, {11, 7, 13}, {{{2, 4, 6}, {3, 2, 4}, {5, 2, 8}}}, {1, 1, 1}, {{{39, 52, 28}, {47, 50, 26}, {42, 46, 30}}}},
    {1, 1, {5, 1, 167}, 0x1cbb51c9cd0fc1f1ull, 1090, {11, 13, 11}, {{{6, 2, 10}, {5, 2, 8}, {2, 3, 2}}}, {1, 1, 1}, {{{43, 50, 32}, {42, 46, 30}, {39, 51, 24}}}},
    {1, 2, {10, 498, 333}, 0xfc4b854e96aeea89ull, 23967, {11, 7, 11}, {{{2, 1, 12}, {1, 1, 10}, {2, 2, 1}}}, {1, 1, 1}, {{{39, 49, 34}, {45, 49, 32}, {39, 50, 23}}}},
    {1, 48879, {286, 158, 201}, 0xa808f3a51ac48fc3ull, 3628, {7, 13, 13}, {{{3, 2, 4}, {1, 4, 4}, {2, 3, 6}}}, {1, 0, 1}, {{{47, 50, 26}, {38, 48, 11}, {39, 47, 28}}}},
    {1, 0, {0, 5, 1}, 0x6927580dbe53d551ull, 10982, {11, 11, 13}, {{{1, 3, 1}, {6, 2, 10}, {5, 2, 8}}}, {0, 1, 1}, {{{38, 51, 8}, {43, 50, 32}, {42, 46, 30}}}},
    {2, 1, {5, 1, 167}, 0x5e7d088787452d8cull, 1090, {7, 7, 11}, {{{2, 1, 13}, {4, 2, 14}, {3, 4, 9}}}, {1, 1, 1}, {{{46, 49, 35}, {48, 50, 36}, {40, 52, 31}}}},
    {2, 4660, {124, 170, 134}, 0xe826b7ebb2e0c856ull, 19970, {11, 7, 11}, {{{4, 1, 13}, {2, 1, 3}, {2, 1, 15}}}, {1, 1, 1}, {{{41, 49, 35}, {46, 49, 25}, {39, 49, 37}}}},
    {2, 31337, {91, 384, 484}, 0x7083890e4bdde96dull, 18271, {13, 11, 11}, {{{2, 4, 4}, {1, 3, 1}, {3, 1, 15}}}, {1, 0, 1}, {{{39, 48, 26}, {38, 51, 8}, {40, 49, 37}}}},
    {3, 2, {10, 498, 333}, 0xda45bc57bf671595ull, 23967, {11, 7, 11}, {{{1, 2, 3}, {2, 1, 5}, {2, 3, 2}}}, {0, 1, 1}, {{{38, 50, 10}, {46, 49, 27}, {39, 51, 24}}}},
    {3, 65535, {300, 348, 209}, 0x17228ebb7aa4d62cull, 4027, {11, 13, 7}, {{{2, 2, 11}, {4, 1, 8}, {2, 2, 3}}}, {1, 1, 1}, {{{39, 50, 33}, {41, 45, 30}, {46, 50, 25}}}},
    {3, 0, {0, 5, 1}, 0x261d17bfe17dbdc6ull, 10982, {13, 11, 7}, {{{4, 1, 13}, {2, 4, 12}, {1, 1, 15}}}, {1, 1, 1}, {{{41, 45, 35}, {39, 52, 34}, {45, 49, 37}}}},
};

}  // namespace

TEST_CASE("layout: spot slot table is the 1100:0000 map") {
    const auto& spots = layoutSpotSlots();
    const auto& gates = layoutGateSlots();
    const auto& shops = layoutShopSlots();
    // Rooms of each level (dgroup_map.md §4.16): 9..22, 23..35, 36..48.
    const int lo[3] = {9, 23, 36}, hi[3] = {22, 35, 48};
    for (int L = 0; L < 3; ++L) {
        std::set<int> used;
        auto add = [&](RoomSlot s) {
            CHECK(s.slot < 3);
            CHECK(int(s.room) >= lo[L]);
            CHECK(int(s.room) <= hi[L]);
            CHECK(used.insert(s.room * 3 + s.slot).second);  // no slot written twice
        };
        for (const RoomSlot s : spots[size_t(L)]) add(s);
        for (const RoomSlot s : gates[size_t(L)]) {
            add(s);
            CHECK(int(s.room) == kGateRooms[size_t(L)]);
        }
        add(shops[size_t(L)]);
        CHECK(int(shops[size_t(L)].room) == kShopRooms[size_t(L)]);
        CHECK(used.size() == 21);
        // Rooms that never get spots: 9 (level 0) and the unreachable 19-21 / 32-34 / 45-47.
        for (int r : {9, 19, 20, 21, 32, 33, 34, 45, 46, 47})
            for (int s = 0; s < 3; ++s) CHECK(used.count(r * 3 + s) == 0);
    }
}

TEST_CASE("layout: applyLayoutRecord writes record spots and markers, nothing else") {
    RoomTable rooms;
    rooms.clearSpots();
    LayoutRecord r;
    for (int j = 0; j < kLayoutSpots; ++j) {
        r.kind[size_t(j)] = uint8_t(j + 1);
        r.content[size_t(j)] = uint8_t(j % 4);
    }
    applyLayoutRecord(rooms, 1, r);
    applyLayoutRecord(rooms, 3, r);   // ignored
    applyLayoutRecord(rooms, -1, r);  // ignored
    int written = 0;
    for (int room = 0; room < kRoomCount; ++room)
        for (int s = 0; s < 3; ++s)
            if (rooms.spotKind(room, s) != kSpotNone) ++written;
    CHECK(written == 21);
    CHECK(rooms.spotKind(23, 0) == 1);   // spot 0 -> 23.0
    CHECK(rooms.spotKind(35, 1) == 18);  // spot 17 -> 35.1
    CHECK(rooms.spotContent(29, 1) == 9 % 4);
    CHECK(rooms.spotKind(25, 1) == kSpotGate);
    CHECK(rooms.spotContent(25, 2) == kSpotGate);
    CHECK(rooms.spotKind(24, 0) == kSpotShop);
    CHECK(rooms.spotContent(24, 0) == kSpotShop);
}

TEST_CASE("layout: pickLayoutRecords draws distinct records like pick_records") {
    BorlandRand rng;
    rng.srand(1);
    const LayoutChoice c = pickLayoutRecords(500, rng);
    CHECK(c.record == std::array<int, 3>{5, 1, 167});
    // Tiny tables force re-rolls; the three indices stay distinct.
    for (uint16_t seed = 0; seed < 200; ++seed) {
        rng.srand(seed);
        const LayoutChoice d = pickLayoutRecords(3, rng);
        CHECK(std::set<int>(d.record.begin(), d.record.end()).size() == 3);
    }
}

TEST_CASE("layout: generation from fixed seeds matches othr_tables.py") {
    TM_REQUIRE_GAME_DATA();
    const GameTables& t = tables();
    for (const LayoutVector& v : kVectors) {
        CAPTURE(v.tier);
        CAPTURE(v.seed);
        RoomTable rooms = RoomTable::fromDefs(t.rooms);
        ClueState clues;
        BorlandRand rng;
        rng.srand(v.seed);
        const LayoutChoice c = generateClimbLayout(rooms, clues, uint8_t(v.tier), t, rng);
        CHECK(c.record == v.rec);
        CHECK(spotHash(rooms) == v.hash);
        CHECK(rng.rand() == v.next);  // exactly the rand() calls of 1100:04b5
        for (size_t L = 0; L < 3; ++L) {
            CHECK(clues.flags[L] == v.masks[L]);
            CHECK(clues.attr[L] == v.attrs[L]);
            CHECK(clues.plural[L] == v.plural[L]);
            CHECK(clues.keyKind[L] == t.layouts[size_t(v.tier)][size_t(v.rec[L])].keyKind);
        }
        buildClueVoiceIds(clues);
        buildClueTexts(clues, t.words);
        for (size_t L = 0; L < 3; ++L) {
            CHECK(clues.voice[L] == v.voices[L]);
            for (size_t k = 0; k < 3; ++k) CHECK_FALSE(clues.text[L][k].empty());
            // The noun is the last word; plural selects the plural table (1100:0793).
            const auto& nouns = clues.plural[L] ? t.words.nounPlural : t.words.noun;
            CHECK(clues.text[L][2] == nouns[clues.attr[L][2]]);
            CHECK(clueWordSoundId(clues, int(L), 2, 1) == 7300 + clues.voice[L][2]);
            CHECK(clueWordSoundId(clues, int(L), 0, 3) == 7500 + clues.voice[L][0]);
        }
    }
}

TEST_CASE("layout: shipped records satisfy the documented invariants") {
    TM_REQUIRE_GAME_DATA();
    const GameTables& t = tables();
    REQUIRE(t.objects.size() == 128);
    for (size_t v = 0; v < 112; ++v) CHECK(t.objects[v].noun != 0);
    for (size_t v = 112; v < 128; ++v) CHECK(t.objects[v] == ObjectAttributes{});
    const int expectedCounts[4][4] = {{10, 5, 2, 1}, {8, 6, 3, 1}, {6, 7, 4, 1}, {4, 8, 5, 1}};
    for (int tier = 0; tier < 4; ++tier) {
        const auto& table = t.layouts[size_t(tier)];
        REQUIRE(table.size() == 500);
        int failures = 0;
        for (const LayoutRecord& r : table) {
            int counts[4] = {0, 0, 0, 0};
            int keyAt = -1;
            for (int j = 0; j < kLayoutSpots; ++j) {
                const uint8_t c = r.content[size_t(j)];
                if (c < 4) ++counts[c];
                if (c == kContentKey) keyAt = j;
            }
            bool ok = (r.mask == 0x0B || r.mask == 0x0D || r.mask == 0x07) && clueWordCount(r.mask) == 3;
            for (int c = 0; c < 4; ++c) ok = ok && counts[c] == expectedCounts[tier][c];
            ok = ok && counts[2] == t.gemsNeeded[size_t(tier)];
            ok = ok && keyAt >= 0 && r.kind[size_t(keyAt)] == r.keyKind;
            // Only the key picture matches all three named attributes.
            const ObjectAttributes key = t.object(r.keyKind);
            int matches = 0;
            for (int j = 0; j < kLayoutSpots; ++j) {
                const ObjectAttributes o = t.object(r.kind[size_t(j)]);
                const bool m = (!(r.mask & 8) || o.count == key.count) && (!(r.mask & 4) || o.size == key.size) &&
                               (!(r.mask & 2) || o.shape == key.shape) && (!(r.mask & 1) || o.noun == key.noun);
                if (m) ++matches;
            }
            ok = ok && matches == 1;
            if (!ok) ++failures;
        }
        CHECK(failures == 0);
    }
}

TEST_CASE("layout: clue resolution, plural rule and voice biases on synthetic data") {
    GameTables t;
    t.objects.assign(128, ObjectAttributes{});
    t.objects[7] = ObjectAttributes{3, 2, 1, 3};  // three, small, round, (plural) noun 3
    t.objects[9] = ObjectAttributes{1, 4, 0, 8};  // one, short, no shape, noun 8
    ClueState c;
    c.keyKind = {7, 9, 7};
    c.flags = {0x0B, 0x0D, 0x07};
    resolveClueAttributes(c, t);
    CHECK(c.attr[0] == std::array<uint8_t, 3>{3, 1, 3});  // count, shape, noun
    CHECK(c.attr[1] == std::array<uint8_t, 3>{1, 4, 8});  // count, size, noun
    CHECK(c.attr[2] == std::array<uint8_t, 3>{2, 1, 3});  // size, shape, noun
    CHECK(c.plural == std::array<uint8_t, 3>{1, 0, 1});   // count > 1 even if count is not named
    buildClueVoiceIds(c);
    CHECK(c.voice[0] == std::array<uint16_t, 3>{3 + 0x25, 1 + 0x30, 3 + 0x16});
    CHECK(c.voice[1] == std::array<uint16_t, 3>{1 + 0x25, 4 + 0x2C, 8 + 7});
    CHECK(c.voice[2] == std::array<uint16_t, 3>{2 + 0x2C, 1 + 0x30, 3 + 0x16});
    // A mask with all four bits stores only three values (the original would overflow).
    c.flags = {0x0F, 0x0B, 0x0B};
    resolveClueAttributes(c, t);
    CHECK(c.attr[0] == std::array<uint8_t, 3>{3, 2, 1});
    CHECK(clueWordCount(0x0F) == 3);
}

TEST_CASE("layout: dump for the reference comparison (TM_GAME_LAYOUT_DUMP)") {
    // Writes "tier seed a b c hash next voices..." for seeds 0..1999 of every tier when the variable
    // names an output file; extracted/scratch/game/compare_layout_dump.py checks it against
    // tools/assets/othr_tables.py.
    const auto out = getEnv("TM_GAME_LAYOUT_DUMP");
    if (!out) return;
    TM_REQUIRE_GAME_DATA();
    const GameTables& t = tables();
    std::ofstream f(pathFromUtf8(*out));
    REQUIRE(f.good());
    for (int tier = 0; tier < 4; ++tier) {
        for (uint32_t seed = 0; seed < 2000; ++seed) {
            RoomTable rooms = RoomTable::fromDefs(t.rooms);
            ClueState clues;
            BorlandRand rng;
            rng.srand(uint16_t(seed));
            const LayoutChoice c = generateClimbLayout(rooms, clues, uint8_t(tier), t, rng);
            buildClueVoiceIds(clues);
            f << std::format("{} {} {} {} {} {:016x} {}", tier, seed, c.record[0], c.record[1], c.record[2],
                             spotHash(rooms), rng.rand());
            for (size_t L = 0; L < 3; ++L)
                for (size_t k = 0; k < 3; ++k) f << ' ' << clues.voice[L][k];
            f << "\n";
        }
    }
}

// ------------------------------------------------------------------ REVIEW_1 M-3 regressions

namespace {

// Runs f on a thread of its own; true if it finished within `ms`. A regression to the endless
// re-roll then fails the check instead of hanging the test run (the thread only touches its own
// copies).
template <class F>
bool finishesWithin(int ms, F f) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread([done, f]() mutable {
        f();
        *done = true;
    }).detach();
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!*done && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return *done;
}

std::vector<uint8_t> layoutBytes(int records) {
    std::vector<uint8_t> b = {uint8_t(records), uint8_t(records >> 8)};
    for (int r = 0; r < records; ++r)
        for (int k = 0; k < kLayoutRecordSize; ++k) b.push_back(uint8_t(r + k));
    b.resize((b.size() + 511) / 512 * 512, 0);  // NE padding
    return b;
}

}  // namespace

TEST_CASE("layout (REVIEW_1 M-3): fewer than 3 layout records is an error, not an endless loop") {
    for (int count : {0, 1, 2}) {
        auto threw = std::make_shared<std::atomic<bool>>(false);
        const bool finished = finishesWithin(2000, [count, threw] {
            BorlandRand rng;
            rng.srand(1);
            try {
                (void)pickLayoutRecords(count, rng);
            } catch (const FormatError&) {
                *threw = true;
            }
        });
        CHECK_MESSAGE(finished, "pickLayoutRecords(", count, ") did not return within 2 s");
        CHECK(*threw);
    }
    // The load-time check names the resource.
    for (int count : {1, 2}) {
        try {
            (void)loadLayoutTable(10551, layoutBytes(count));
            FAIL("no error for a table of ", count, " records");
        } catch (const FormatError& e) {
            const std::string m = e.what();
            CHECK(m.find("OTHR 10551") != std::string::npos);
            CHECK(m.find(std::to_string(count) + " records") != std::string::npos);
        }
    }
    CHECK(loadLayoutTable(10551, layoutBytes(3)).size() == 3);
    CHECK_THROWS_AS(loadLayoutTable(10551, std::vector<uint8_t>{}), FormatError);
}

TEST_CASE("layout (REVIEW_1 M-3): a mod layout table of 2 records is refused; the game's table stays") {
    TM_REQUIRE_GAME_DATA();
    std::random_device rd;
    const auto dir = std::filesystem::temp_directory_path() / ("tmport_layout_mod_" + std::to_string(rd()));
    REQUIRE(writeWholeFile(dir / "M" / "OTHR" / "10551.bin", layoutBytes(2)));
    ResourceManager::Options o;
    auto files = std::make_unique<GameFiles>(GameFiles::open(pathFromUtf8(*testdata::dataPath()), o));
    mods::applyMods(*files, {dir / "M"});
    CHECK(files->res->loadRaw(10551) == files->res->loadOriginal(ResType::OTHR, 10551));
    const GameTables t = GameTables::load(*files->exe, *files->res);  // no hang, no error
    CHECK(t.layouts[1].size() == tables().layouts[1].size());
    std::string all;  // overrides are imported (and refused) on first use
    for (const auto& f : files->res->mods().failures()) all += f + "\n";
    INFO(all);
    CHECK(all.find("OTHR 10551: the layout table has 2 records; a climb needs at least 3") != std::string::npos);
    files.reset();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
