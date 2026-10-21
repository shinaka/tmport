// Layout_Generate and the other layout.c routines of the DOS edition (src/dos/game/layout.cpp):
// seed-for-seed reproduction of the layouts saved in the reference USERS.DAT files (the shipped
// record, Ada's two games and the crafted tier-2 / tier-3 players; seeds found with the wave-1
// simulator extracted/scratch/dos_w1_elves/layout_sim.py), the rand() position handed to the quiz
// deal, a CRC sweep against the simulator over all schemes and tiers, Turbo C's time() seed, and
// the small helpers. Only coordinates, counts, CRCs and per-call numbers are compared.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

#include "dos/game/game.h"
#include "dos/game/layout.h"
#include "dos/game/signin.h"
#include "dos/save/users_dat.h"
#include "dos_game_fixture.h"
#include "mischief/util/png_writer.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

// Number of rand() calls that take a generator from `from` to `to` (-1 if more than `limit`).
int randCallsBetween(uint32_t from, uint32_t to, int limit = 5000) {
    mischief::BorlandRand r;
    r.setRawSeed(from);
    for (int n = 0; n <= limit; ++n) {
        if (r.seed() == to) return n;
        r.rand();
    }
    return -1;
}

// Game_NewSetup's part before and around Layout_Generate (47c7:0bd6): slots cleared, variable cells
// reset, the tier as loaded from the record, the seed.
void generateLayout(DosGame& g, uint16_t seed, uint16_t tier) {
    layoutClearSlots(g);
    cellsResetVariable(g);
    g.core.tier = tier;
    g.core.slowCpu = 0;
    g.layoutSeedOverride = seed;
    layoutGenerate(g);
}

// The layout fields the CRC sweep covers, in the simulator's order (sweep.py): the 48 hiding-place
// pictures and contents (low bytes, slot order), 9 clue words, 3 key pictures, castle layout, tier,
// and the number of rand() calls (u16).
uint32_t layoutCrc(const DosGame& g, int nrand) {
    std::vector<uint8_t> b;
    for (const uint16_t c : g.tables.world.hidingCells) b.push_back(static_cast<uint8_t>(g.world.cellPicture[c] & 0xFF));
    for (const uint16_t c : g.tables.world.hidingCells) b.push_back(static_cast<uint8_t>(g.world.cellContent[c] & 0xFF));
    for (const uint8_t w : g.rec().clueWords) b.push_back(w);
    for (int l = 0; l < 3; ++l) b.push_back(static_cast<uint8_t>(g.world.levelPicData[static_cast<size_t>(l)][1] & 0xFF));
    b.push_back(g.world.castleLayout);
    b.push_back(static_cast<uint8_t>(g.core.tier));
    b.push_back(static_cast<uint8_t>(nrand & 0xFF));
    b.push_back(static_cast<uint8_t>(nrand >> 8));
    return mischief::png::crc32(b);
}

struct ReferenceLayout {
    const char* file;      // under extracted/captures/dos
    int record;
    uint16_t seed;         // the only seed of 65536 that reproduces the record (brute.py)
    int nrand;             // rand() calls of the layout (simulator)
    uint16_t tierAfter;
};

constexpr ReferenceLayout kReferences[] = {
    {"original/USERS.DAT", 0, 0xA954, 734, 0},
    {"USERS_after_exit_level2_Ada.DAT", 1, 0x1EE2, 732, 0},
    {"USERS_after_exit_post_castle_Ada.DAT", 1, 0x2B40, 739, 0},
    {"USERS_crafted_tierb_layout.DAT", 2, 0x426E, 671, 2},
    {"USERS_crafted_tierc_layout.DAT", 3, 0x42D3, 637, 3},
};

}  // namespace

TEST_CASE("dos layout: Turbo C time() gives the srand seeds of Ada's games") {
    // The DOS clock of the two USERS.DAT snapshots written in the same second as Game_Start (§5.5).
    CHECK((turboCTime(2026, 10, 18, 15, 32, 50) & 0xFFFF) == 0x1EE2u);
    CHECK((turboCTime(2026, 10, 18, 16, 25, 36) & 0xFFFF) == 0x2B40u);
    // EST5EDT: local time + 5 h, - 1 h in daylight saving time.
    CHECK(turboCTime(1990, 1, 1, 0, 0, 0) == 631170000u);
    CHECK(turboCTime(1990, 7, 4, 12, 0, 0) == 647107200u);
    CHECK(turboCTime(1990, 4, 1, 1, 59, 0) == 638953140u);
    CHECK(turboCTime(1990, 4, 1, 3, 0, 0) == 638953200u);
}

TEST_CASE("dos layout: Turbo C's daylight saving rule") {
    // 1987 rule: first Sunday of April 2:00 to the last Sunday of October 2:00 (month 1..12, day).
    CHECK_FALSE(turboCIsDst(20, 4, 1, 1));  // 1990-04-01 is a Sunday
    CHECK(turboCIsDst(20, 4, 1, 2));
    CHECK_FALSE(turboCIsDst(20, 3, 31, 12));
    CHECK(turboCIsDst(20, 10, 28, 1));      // 1990-10-28, the last Sunday
    CHECK_FALSE(turboCIsDst(20, 10, 28, 2));
    CHECK(turboCIsDst(20, 7, 4, 0));
    CHECK_FALSE(turboCIsDst(20, 11, 1, 12));
    // Before 1987: from the last Sunday of April (1986-04-27).
    CHECK_FALSE(turboCIsDst(16, 4, 6, 12));
    CHECK_FALSE(turboCIsDst(16, 4, 27, 1));
    CHECK(turboCIsDst(16, 4, 27, 2));
    // 2026 under the 1987 rule: 5 April to 25 October (not the modern March-November rule).
    CHECK_FALSE(turboCIsDst(56, 3, 20, 12));
    CHECK(turboCIsDst(56, 4, 5, 3));
    CHECK(turboCIsDst(56, 10, 25, 1));
    CHECK_FALSE(turboCIsDst(56, 10, 25, 2));
    // Month 0: the month comes from the 0-based day of the year (2026-10-18 = day 290).
    CHECK(turboCIsDst(56, 0, 290, 15));
    CHECK_FALSE(turboCIsDst(56, 0, 297, 15));  // 25 October, 15:00
}

TEST_CASE("dos layout: Layout_Generate reproduces the reference USERS.DAT layouts seed for seed") {
    TM_REQUIRE_DOS_GAME_FILES();
    const auto caps = testdata::dosCapturesPath();
    if (!caps) {
        MESSAGE("extracted/captures/dos not found: test skipped");
        return;
    }
    for (const ReferenceLayout& ref : kReferences) {
        CAPTURE(ref.file);
        dosgame::GameFixture f(*gameFiles);
        if (!f.loadUsers(*caps / ref.file)) {
            MESSAGE("missing reference file, skipped: " << ref.file);
            continue;
        }
        DosGame& g = f.g();
        f.selectRecord(ref.record);
        const UserRecord expected = g.rec();
        // The player as signed in before the game: shown treasure from the record, tier 0 (the
        // crafted players' tiers 2 and 3 are outputs of this layout).
        g.player.shownTreasure = expected.shownTreasure;
        generateLayout(g, ref.seed, 0);

        const auto& hc = g.tables.world.hidingCells;
        int pictures = 0, contents = 0;
        for (size_t i = 0; i < hc.size(); ++i) {
            pictures += (g.world.cellPicture[hc[i]] & 0xFF) == expected.hidingPictures[i];
            contents += (g.world.cellContent[hc[i]] & 0x7F) == (expected.hidingContents[i] & 0x7F);  // bit 7 = searched
        }
        CHECK(pictures == 48);
        CHECK(contents == 48);
        for (size_t i = 0; i < 9; ++i) {
            CAPTURE(i);
            CHECK((g.rec().clueWords[i] & 0x7F) == (expected.clueWords[i] & 0x7F));  // bit 7 = not revealed
            CHECK((g.rec().clueWords[i] & 0x80) != 0);  // a new layout reveals nothing
        }
        for (size_t l = 0; l < 3; ++l) CHECK(g.world.levelPicData[l][1] == expected.levelPicWords[2 * l]);
        CHECK(g.world.castleLayout == static_cast<uint8_t>(expected.castleLayout));
        CHECK(g.core.tier == ref.tierAfter);
        CHECK(g.core.tier == expected.tier);
        // The rand() position handed to Quiz_LoadQuestions / the deal.
        CHECK(randCallsBetween(ref.seed, g.rand.seed()) == ref.nrand);

        // Mirror cells and the castle ladders.
        for (int i = 0; i < 3; ++i) {
            CHECK(g.world.cellPicture[0xFD + i] == g.world.cellPicture[0xE3 + i]);
            CHECK(g.world.cellContent[0xC0 + i] == g.world.cellContent[0xAB + i]);
            CHECK(g.world.cellPicture[0x86 + i] == g.world.cellPicture[0x77 + i]);
        }
        const auto& ladders = g.tables.world.castleLayouts[g.world.castleLayout];
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 4; ++c) CHECK(g.world.cellPicture[0x32 + 16 * r + c] == ladders[static_cast<size_t>(4 * r + c)]);
    }
}

TEST_CASE("dos layout: the quiz deal continues the layout's rand() sequence") {
    // deal_check.py: a player without history gets slots 8..0 = tier-0 questions lo + rand() % n,
    // re-drawn on a duplicate. Rascal's record holds them in dealt order, Ada's (game 1) reordered
    // by Quiz_ReorderSlots. So nothing draws a rand() between the layout and the deal.
    TM_REQUIRE_DOS_GAME_FILES();
    const auto caps = testdata::dosCapturesPath();
    if (!caps) {
        MESSAGE("extracted/captures/dos not found: test skipped");
        return;
    }
    for (int k = 0; k < 2; ++k) {
        const ReferenceLayout& ref = kReferences[k];
        CAPTURE(ref.file);
        dosgame::GameFixture f(*gameFiles);
        if (!f.loadUsers(*caps / ref.file)) continue;
        DosGame& g = f.g();
        f.selectRecord(ref.record);
        const UserRecord expected = g.rec();
        g.player.shownTreasure = expected.shownTreasure;
        generateLayout(g, ref.seed, 0);

        const auto [lo, hi] = g.quizTiers.range(0);
        std::array<int, 9> slots;
        slots.fill(-1);
        for (int s = 8; s >= 0; --s) {
            int q = 0;
            do {
                q = lo + g.rand.rand() % (hi - lo);
            } while (std::find(slots.begin(), slots.end(), q) != slots.end());
            slots[static_cast<size_t>(s)] = q;
        }
        std::array<int, 9> saved{};
        for (size_t s = 0; s < 9; ++s) saved[s] = expected.quizSlots[s] & 0x7FFF;
        if (k == 0) {
            CHECK(saved == slots);
        } else {
            std::sort(saved.begin(), saved.end());
            std::sort(slots.begin(), slots.end());
            CHECK(saved == slots);
        }
    }
}

TEST_CASE("dos layout: CRC sweep against the wave-1 simulator (schemes, tiers, collected, decoys)") {
    TM_REQUIRE_DOS_GAME_FILES();
    // extracted/scratch/dos_w3_elves/sweep.py: seed, total, tier from the record, collected ids
    // k = first..first+n-1, shown treasure, then the simulator's scheme, rand() count, tier, castle
    // and the CRC of layoutCrc's bytes.
    struct Case {
        uint16_t seed, total, tierIn;
        int firstCollected, collectedCount;
        uint16_t shown;
        int scheme, nrand, tier, castle;
        uint32_t crc;
    };
    static constexpr Case kCases[] = {
        {0x0001, 0, 0, 0, 0, 0xFFFF, 1, 569, 0, 0, 0xE2E0B417u},
        {0x1234, 0, 0, 0, 0, 0xFFFF, 0, 448, 0, 0, 0x67EC3932u},
        {0x7FFF, 0, 0, 0, 0, 0xFFFF, 1, 636, 0, 0, 0x62D9B05Eu},
        {0x8000, 0, 0, 0, 0, 0xFFFF, 2, 536, 0, 0, 0xF29A36A2u},
        {0xBEEF, 0, 0, 0, 0, 0xFFFF, 1, 595, 0, 0, 0x8873716Fu},
        {0xFFFF, 0, 0, 0, 0, 0xFFFF, 0, 427, 0, 0, 0x88CCD944u},
        {0x2468, 150, 0, 10, 20, 0x0010, 1, 711, 2, 0, 0xEAE2503Cu},
        {0x1357, 260, 0, 0, 12, 0xFFFF, 2, 768, 3, 0, 0x9913238Fu},
        {0x4242, 400, 0, 20, 24, 0x0064, 2, 607, 3, 0, 0x86BB650Au},
        {0x9999, 120, 0, 0, 45, 0x0062, 1, 702, 2, 2, 0xC5E6BCCAu},
        {0xC0DE, 24, 3, 0, 0, 0xFFFF, 1, 669, 3, 2, 0xA76D5497u},
        {0x0F0F, 381, 1, 0, 48, 0xFFFF, 2, 655, 3, 2, 0x686CD8D8u},
    };
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    f.selectRecord(0);
    for (const Case& c : kCases) {
        CAPTURE(c.seed);
        UserRecord& r = g.rec();
        r.clear();
        r.totalTreasures = static_cast<int16_t>(c.total);
        r.collected.fill(0x80);
        for (int i = 0; i < c.collectedCount; ++i)
            r.collected[static_cast<size_t>(i)] = static_cast<uint8_t>(6 + 2 * (c.firstCollected + i));
        g.player.shownTreasure = static_cast<int16_t>(c.shown);
        generateLayout(g, c.seed, c.tierIn);
        const int nrand = randCallsBetween(c.seed, g.rand.seed());
        CHECK(g.elves.clueScheme == c.scheme);
        CHECK(nrand == c.nrand);
        CHECK(g.core.tier == c.tier);
        CHECK(g.world.castleLayout == c.castle);
        CHECK(layoutCrc(g, nrand) == c.crc);
    }
    // The two cases with a hand-picked collected list (ids 3, 9, 17 and 1, 2).
    struct ListCase {
        uint16_t seed, total, tierIn;
        std::array<int, 3> ids;
        int idCount;
        uint16_t shown;
        int nrand;
        uint32_t crc;
    };
    static constexpr ListCase kLists[] = {
        {0x0BAD, 30, 0, {3, 9, 17}, 3, 0x002E, 379, 0xABCF5B72u},
        {0x5A5A, 10, 2, {1, 2, 0}, 2, 0xFFFF, 831, 0x4E60A5C6u},
    };
    for (const ListCase& c : kLists) {
        CAPTURE(c.seed);
        UserRecord& r = g.rec();
        r.clear();
        r.totalTreasures = static_cast<int16_t>(c.total);
        r.collected.fill(0x80);
        for (int i = 0; i < c.idCount; ++i) r.collected[static_cast<size_t>(i)] = static_cast<uint8_t>(6 + 2 * c.ids[static_cast<size_t>(i)]);
        g.player.shownTreasure = static_cast<int16_t>(c.shown);
        generateLayout(g, c.seed, c.tierIn);
        const int nrand = randCallsBetween(c.seed, g.rand.seed());
        CHECK(nrand == c.nrand);
        CHECK(layoutCrc(g, nrand) == c.crc);
    }
}

TEST_CASE("dos layout: every hiding place is filled; keys, treasures and decoys per level") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    f.selectRecord(0);
    g.rec().clear();
    g.rec().collected.fill(0x80);
    g.player.shownTreasure = -1;
    for (const uint16_t seed : {uint16_t{3}, uint16_t{0x4000}, uint16_t{0xA954}}) {
        CAPTURE(seed);
        generateLayout(g, seed, 0);
        const auto& hc = g.tables.world.hidingCells;
        const int first[3] = {29, 13, 0}, last[3] = {47, 26, 10};
        for (int level = 0; level < 3; ++level) {
            int keys = 0, treasures = 0, empty = 0;
            for (int s = first[level]; s <= last[level]; ++s) {
                const uint16_t cell = hc[static_cast<size_t>(s)];
                CHECK(g.world.cellPicture[cell] >= 1);
                CHECK(g.world.cellPicture[cell] <= g.world.levelPicData[static_cast<size_t>(level)][0]);
                const uint16_t c = g.world.cellContent[cell];
                if (c == 0x66 + 2 * level) ++keys;
                else if (c == 0x80) ++empty;
                else if (c >= 6 && c < 6 + 2 * 48 && (c & 1) == 0) ++treasures;
            }
            CHECK(keys == 1);
            CHECK(treasures == 2);  // total 0: two treasures per level
            CHECK(keys + treasures + empty == last[level] - first[level] + 1);
            CHECK(g.world.cellPicture[g.elves.keyCellL1] != 0);
        }
        CHECK(g.world.cellContent[g.elves.keyCellL1] == 0x68);
        CHECK(g.world.cellContent[g.elves.keyCellL2] == 0x6A);
        // Treasure ids are all different (nothing collected, nothing on show).
        std::vector<uint16_t> ids;
        for (const uint16_t cell : hc)
            if (g.world.cellContent[cell] < 0x66 && g.world.cellContent[cell] >= 6) ids.push_back(g.world.cellContent[cell]);
        std::sort(ids.begin(), ids.end());
        // The mirror slots repeat their source cells' treasures.
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        CHECK(ids.size() == 6);
    }
}

TEST_CASE("dos layout: helpers (slots, mirrors, quirks, CPU patches, castle)") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    const auto& hc = g.tables.world.hidingCells;

    // Layout_ClearSlots: the 48 cells picture 0, content 0x80.
    for (const uint16_t c : hc) g.world.cellPicture[c] = 7;
    layoutClearSlots(g);
    for (const uint16_t c : hc) {
        CHECK(g.world.cellPicture[c] == 0);
        CHECK(g.world.cellContent[c] == 0x80);
    }

    // Layout_PutInSlot: only free slots of the level, one rand() per try.
    g.rand.setRawSeed(77);
    for (int n = 0; n < 11; ++n) layoutPutInSlot(g, 5 + n, 0x80, 2);
    for (int s = 0; s <= 10; ++s) CHECK(g.world.cellPicture[hc[static_cast<size_t>(s)]] != 0);
    for (int s = 11; s < 48; ++s) CHECK(g.world.cellPicture[hc[static_cast<size_t>(s)]] == 0);
    layoutClearSlots(g);
    layoutPutInSlot(g, 9, 0x68, 1);
    int filled = 0;
    for (int s = 13; s <= 26; ++s) filled += g.world.cellPicture[hc[static_cast<size_t>(s)]] == 9;
    CHECK(filled == 1);
    CHECK(g.world.cellContent[g.elves.keyCellL1] == 0x68);

    // Layout_UnmarkSharedCells: & 0x7F, so an empty mirror place (0x80) becomes 0 (quirk kept).
    g.world.cellContent[0xC0] = 0x80;
    g.world.cellContent[0x86] = 0x8A;
    g.world.cellContent[0xFF] = 0x66;
    layoutUnmarkSharedCells(g);
    CHECK(g.world.cellContent[0xC0] == 0);
    CHECK(g.world.cellContent[0x86] == 0x0A);
    CHECK(g.world.cellContent[0xFF] == 0x66);

    // Cells_ResetVariable: defaults from the EXE, content 0x80.
    for (const uint16_t c : g.tables.world.variableCells) g.world.cellPicture[c] = 0;
    cellsResetVariable(g);
    for (size_t j = 0; j < 9; ++j) {
        CHECK(g.world.cellPicture[g.tables.world.variableCells[j]] == g.tables.world.variableCellDefaults[j]);
        CHECK(g.world.cellContent[g.tables.world.variableCells[j]] == 0x80);
    }

    // Cells_AdjustForCpu.
    g.core.slowCpu = 1;
    cellsAdjustForCpu(g);
    CHECK(g.world.cellPicture[0xBF] == 0x40);
    CHECK(g.world.cellPicture[0x7E] == 0x2E);
    CHECK(g.world.cellPicture[0x84] == 0x2E);
    CHECK(g.world.cellType[0x84] == 0x1C);
    CHECK(g.world.cellType[0xB7] == 0x1C);
    g.core.slowCpu = 0;
    g.world.cellPicture[0xB7] = 0x30;  // changed in play: left alone
    cellsAdjustForCpu(g);
    CHECK(g.world.cellPicture[0xBF] == 0x41);
    CHECK(g.world.cellPicture[0x7E] == 0x37);
    CHECK(g.world.cellPicture[0x84] == 0x2F);
    CHECK(g.world.cellType[0x84] == 0x2D);
    CHECK(g.world.cellPicture[0xB7] == 0x30);
    CHECK(g.world.cellType[0xB7] == 0x1C);

    // Castle_PickLayout: one rand() even at tier 0; rand() % (tier + 1).
    for (uint16_t tier = 0; tier <= 3; ++tier) {
        g.core.tier = tier;
        g.rand.setRawSeed(1234 + tier);
        mischief::BorlandRand ref;
        ref.setRawSeed(1234 + tier);
        const int expect = ref.rand() % (tier + 1);
        CHECK(castlePickLayout(g) == expect);
        CHECK(g.world.castleLayout == expect);
        CHECK(g.rand.seed() == ref.seed());
        castleApplyLayout(g);
        CHECK(g.world.cellPicture[0x72 + 3] == g.tables.world.castleLayouts[static_cast<size_t>(expect)][19]);
    }
    // Tier above 3: the old layout is the divisor.
    g.core.tier = 7;
    g.world.castleLayout = 3;
    g.rand.setRawSeed(99);
    mischief::BorlandRand ref;
    ref.setRawSeed(99);
    CHECK(castlePickLayout(g) == ref.rand() % 3);

    // Clue_BuildWords: scheme 1 (number, shape, object) draws no rand(); words get bit 7.
    f.selectRecord(0);
    g.elves.clueScheme = 1;
    g.elves.keyPicture = 3;
    g.rand.setRawSeed(5);
    clueBuildWords(g, 3);
    CHECK(g.rand.seed() == 5u);
    for (int a = 0; a < 3; ++a) CHECK((g.rec().clueWords[static_cast<size_t>(3 + a)] & 0x80) != 0);
    // Scheme 0 draws at least one rand() % 4 for the size word (attribute 1).
    g.elves.clueScheme = 0;
    g.rand.setRawSeed(5);
    clueBuildWords(g, 0);
    CHECK(g.rand.seed() != 5u);
}

TEST_CASE("dos layout: Game_Start of the shipped record reproduces its layout and dealt questions (integration)") {
    // The whole new-game path of the sign-in (Users_LoadPlayer, Game_Start -> Game_NewSetup ->
    // Layout_Generate, Quiz_LoadQuestions -> Quiz_Deal) with the srand value of the shipped record:
    // the 48 hiding places and the 9 questions in the saved order.
    TM_REQUIRE_DOS_GAME_FILES();
    const auto caps = testdata::dosCapturesPath();
    if (!caps) {
        MESSAGE("extracted/captures/dos not found: test skipped");
        return;
    }
    dosgame::GameFixture f(*gameFiles);
    if (!f.loadUsers(*caps / "original/USERS.DAT")) return;
    DosGame& g = f.g();
    f.selectRecord(0);
    const UserRecord expected = g.rec();
    g.rec().savedGame = 0;  // start a new game instead of the saved one
    usersLoadPlayer(g);
    g.layoutSeedOverride = uint16_t{0xA954};
    gameStart(g);
    const auto& hc = g.tables.world.hidingCells;
    int pictures = 0, contents = 0;
    for (size_t i = 0; i < hc.size(); ++i) {
        pictures += (g.world.cellPicture[hc[i]] & 0xFF) == expected.hidingPictures[i];
        contents += (g.world.cellContent[hc[i]] & 0x7F) == (expected.hidingContents[i] & 0x7F);
    }
    CHECK(pictures == 48);
    CHECK(contents == 48);
    for (size_t s = 0; s < 9; ++s) {
        CAPTURE(s);
        CHECK((g.rec().quizSlots[s] & 0x7FFF) == (expected.quizSlots[s] & 0x7FFF));
    }
}
