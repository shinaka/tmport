// DOS edition, elf questions (src/dos/game/quiz.cpp): question loading and expansion, statuses and
// promotion at 70 %, the planned deal with its fallback chain (and the documented guard of the
// original's endless loop), the reset one byte past the history, the question screen flow with the
// reward on closing, the level-change reorder, Clue_Reveal, and the dealt slots of the reference
// records. Only counts, indices and numbers are stored here.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <set>

#include "dos/game/game.h"
#include "dos/game/quiz.h"
#include "dos/game/signin.h"
#include "dos/game/sound.h"
#include "dos/game/ui.h"
#include "dos/save/users_dat.h"
#include "dos_game_fixture.h"

using namespace tmport;
using namespace tmport::dos;
namespace fs = std::filesystem;

namespace {

std::optional<fs::path> capturesFile(const char* rel) {
    const auto caps = testdata::dosCapturesPath();
    if (!caps) return std::nullopt;
    const fs::path p = *caps / rel;
    std::error_code ec;
    if (!fs::exists(p, ec)) return std::nullopt;
    return p;
}

// A fixture with a fresh record 0 selected (history all zero, tier 0).
void freshRecord(dosgame::GameFixture& f) {
    f.users().users() = UsersDat{};
    f.users().record(0).clear();
    f.users().record(0).name[0] = 1;
    f.selectRecord(0);
}

void setStatus(UserRecord& r, int q, int status) { r.setHistory(q, static_cast<uint8_t>(status)); }

}  // namespace

TEST_CASE("DOS quiz: tier ranges, statuses and the promotion at 70 %") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    freshRecord(f);
    DosGame& g = f.g();
    UserRecord& r = g.rec();
    // Tier 0 = questions [0, 63).
    g.ui.tierFirst = g.quizTiers.start(0);
    g.ui.tierEnd = g.quizTiers.start(1);
    REQUIRE(g.ui.tierEnd - g.ui.tierFirst == 63);
    CHECK(quizTierHasWork(g) == 1);
    CHECK(g.ui.statusCount[0] == 63);
    // 44 of 63 right first time = 69 %: still work; 45 = 71 %: promotion.
    for (int q = 0; q < 44; ++q) setStatus(r, q, 1);
    CHECK(quizTierHasWork(g) == 1);
    CHECK(g.ui.statusCount[1] == 44);
    setStatus(r, 44, 1);
    CHECK(quizTierHasWork(g) == 0);
    // Statuses 2 and 3 do not count as right first time.
    setStatus(r, 44, 2);
    setStatus(r, 45, 3);
    CHECK(quizTierHasWork(g) == 1);
    CHECK(g.ui.statusCount[2] == 1);
    CHECK(g.ui.statusCount[3] == 1);
}

TEST_CASE("DOS quiz: planned statuses fall back 0->1, 1->0, 2->0, 3->2; the endless loop is guarded") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    freshRecord(f);
    DosGame& g = f.g();
    // A fresh history: only status 0 exists: every planned 3 and 2 becomes 0.
    g.ui.statusCount = {63, 0, 0, 0};
    quizPlanStatuses(g);
    for (uint8_t c : g.ui.dealPlan) CHECK(c == 0);
    CHECK(g.ui.dealAvail[0] == 63 - 9);
    // Two missed (3) questions: the first two planned 3s stay, the third becomes 2, then 0.
    g.ui.statusCount = {50, 0, 0, 2};
    quizPlanStatuses(g);
    const auto& plan = g.ui.dealPlan;
    CHECK(std::count(plan.begin(), plan.end(), 3) == 2);
    CHECK(std::count(plan.begin(), plan.end(), 0) == 7);
    // Only statuses 2 and 3 (0 and 1 exhausted): the original loops forever; the guard takes the
    // first status with questions left.
    g.ui.statusCount = {0, 0, 5, 10};
    quizPlanStatuses(g);
    int perStatus[4] = {};
    for (uint8_t c : plan) ++perStatus[c & 3];
    CHECK(perStatus[0] + perStatus[1] == 0);
    CHECK(perStatus[2] <= 5);
    CHECK(perStatus[2] + perStatus[3] == 9);
}

TEST_CASE("DOS quiz: the deal gives 9 distinct questions of the tier by planned status") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    freshRecord(f);
    DosGame& g = f.g();
    UserRecord& r = g.rec();
    for (int q = 63; q < 70; ++q) setStatus(r, q, 3);  // 7 missed questions in tier 1 [63, 180)
    for (int q = 70; q < 75; ++q) setStatus(r, q, 2);
    r.quizTier = 1;
    r.quizSlots.fill(0xFFFF);
    g.rand.setRawSeed(12345);
    g.ui.tierFirst = g.quizTiers.start(1);
    g.ui.tierEnd = g.quizTiers.start(2);
    quizTierHasWork(g);
    const uint32_t seed0 = g.rand.seed();
    quizDeal(g);
    CHECK(g.rand.seed() != seed0);
    std::set<uint16_t> dealt(r.quizSlots.begin(), r.quizSlots.end());
    CHECK(dealt.size() == 9);
    for (size_t s = 0; s < 9; ++s) {
        CAPTURE(s);
        const uint16_t q = r.quizSlots[s];
        CHECK(q >= 63);
        CHECK(q < 180);
        CHECK(r.history(q) == g.ui.dealPlan[s]);  // the slot's question has its planned status
    }
    // The template {0, 3, 2} x 3 with these pools needs no fallback.
    CHECK(g.ui.dealPlan[1] == 3);
    CHECK(g.ui.dealPlan[2] == 2);
    // Same seed, same deal (one rand() per attempt, slots 8 down to 0).
    const auto first = r.quizSlots;
    r.quizSlots.fill(0xFFFF);
    g.rand.setRawSeed(12345);
    quizTierHasWork(g);
    quizDeal(g);
    CHECK(r.quizSlots == first);
}

TEST_CASE("DOS quiz: Quiz_ResetHistory keeps the bits below the tier start and writes one byte past") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    freshRecord(f);
    DosGame& g = f.g();
    UserRecord& r = g.rec();
    r.quizHistory.fill(0xFF);
    r.quizSlots[0] = 0x1234;
    g.ui.tierFirst = 303;  // the old start when the tier goes from 3 to 5
    quizResetHistory(g);
    // Byte 75 holds questions 300..303: the mask (DS:5FF0[3]) keeps question 303's bits only.
    CHECK(r.quizHistory[75] == 0xC0);
    CHECK(r.quizHistory[74] == 0xFF);
    for (int b = 76; b < 90; ++b) CHECK(r.quizHistory[static_cast<size_t>(b)] == 0);
    CHECK(r.quizSlots[0] == 0x1200);  // the low byte of slot 0 (record +0x66) is cleared too
}

TEST_CASE("DOS quiz: loading expands the dealt questions back to back; promotion and tier 5") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    freshRecord(f);
    DosGame& g = f.g();
    UserRecord& r = g.rec();
    SUBCASE("resume: no deal, bit 15 dropped for the lookup") {
        for (size_t s = 0; s < 9; ++s) r.quizSlots[s] = static_cast<uint16_t>(100 + 3 * s);
        r.quizSlots[4] |= 0x8000;
        REQUIRE(quizLoadQuestions(g, true) == 1);
        CHECK(r.quizSlots[0] == 100);
        CHECK(g.ui.puzzleText[0].off == 0xA6B8);
        size_t total = 0;
        for (size_t s = 0; s < 9; ++s) {
            CAPTURE(s);
            const uint16_t off = g.ui.puzzleText[s].off;
            CHECK(off == 0xA6B8 + total);
            const std::string expanded = g.dictionary.expand(g.puzzles.raw((r.quizSlots[s] & 0x7FFF)));
            total += expanded.size();
            // Each next question overwrites the previous NUL: the bytes match the expansion.
            CHECK(std::equal(expanded.begin(), expanded.end(), g.ui.expandedText.begin() + (off - 0xA6B8)));
        }
        CHECK(g.ui.expandedText[total] == '\0');
        CHECK(g.world.loadedScene == 0xFF);
        CHECK(g.ui.quizFileLength == static_cast<uint16_t>(g.puzzles.fileSize()));
    }
    SUBCASE("a new game in tier 3 with 70 % right first time goes to tier 5") {
        r.quizTier = 3;
        for (int q = 303; q < 360; ++q) setStatus(r, q, 1);
        r.quizSlots.fill(0xFFFF);
        REQUIRE(quizLoadQuestions(g, false) == 1);
        CHECK(r.quizTier == 5);
        CHECK(g.ui.tierFirst == g.quizTiers.start(5));
        CHECK(g.ui.tierEnd == 360);
        for (uint16_t q : r.quizSlots) {
            CHECK(q >= g.quizTiers.start(5));
            CHECK(q < 360);
        }
        CHECK(r.history(303) == 1);  // the reset kept question 303 (the inverse mask)
        CHECK(r.history(304) == 0);
    }
}

TEST_CASE("DOS quiz: the question screen opens, answers, rewards on closing") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    freshRecord(f);
    DosGame& g = f.g();
    UserRecord& r = g.rec();
    g.bank = &gameFiles->pak("SPRITES");
    for (size_t s = 0; s < 9; ++s) r.quizSlots[s] = static_cast<uint16_t>(10 + s);
    r.answerMasks.fill(7);
    r.clueWords = {3 | 0x80, 20 | 0x80, 40 | 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    REQUIRE(quizLoadQuestions(g, true) == 1);
    g.core.caughtSlot = 2;
    g.core.uiMode = 2;
    g.core.coins = 9;
    g.core.level = 0;
    g.ui.quizClosed = 1;
    g.input.cmd = -1;
    quizUpdate(g);  // opens slot 1's question
    CHECK(g.ui.quizClosed == 0);
    CHECK(g.ui.screen == 1);
    CHECK(g.ui.hudRedraw == 1);
    CHECK(g.ui.quizSlot == 1);
    CHECK(g.ui.quizQuestion == 11);
    CHECK(g.ui.quizOrder < 6);
    CHECK(g.ui.promptIcon == 2);
    CHECK(g.ui.quizCoins == 9);
    CHECK(g.input.context == 3);
    for (const auto& a : g.ui.answerText) CHECK(a[0] != '\0');
    // Three answer zones (9..11 of context 3), focus on the first.
    for (int i = 0; i < 3; ++i) CHECK(g.inputTables.zones(3).records[static_cast<size_t>(9 + i)].x1 == 10);
    CHECK(g.ui.quizFocus == 0);
    // Down moves the focus.
    g.input.cmd = 7;
    quizUpdate(g);
    CHECK(g.ui.quizFocus == 1);
    // Focus the right answer (index 0) and answer.
    while (g.tables.ui.answerPerms[g.ui.quizOrder][static_cast<size_t>(g.ui.quizFocus)] != 0) {
        g.input.cmd = 7;
        quizUpdate(g);
    }
    f.sound().clear();
    g.input.cmd = 8;
    quizUpdate(g);
    CHECK(g.ui.quizAnswered == 1);
    CHECK(g.ui.promptIcon == 6);
    CHECK(r.quizSlots[1] == (11 | 0x8000));
    CHECK(r.answerMasks[1] == 6);
    CHECK(g.ui.quizCoins == 9 + 2);  // mask 6 after the pick: 2 coins
    CHECK(g.core.coins == 9);        // not before the screen closes
    CHECK(r.history(11) == 1);
    int revealed = 0;
    for (int k = 0; k < 3; ++k) revealed += r.clueWords[static_cast<size_t>(k)] < 0x80 ? 1 : 0;
    CHECK(revealed == 1);
    CHECK(g.ui.praiseWord.off != 0);
    bool sfx10 = false;
    for (const auto& c : f.sound().calls()) sfx10 |= c.op == RecordingSound::Op::SfxPlay && c.arg == 10;
    CHECK(sfx10);
    // Any key closes: the reward reaches g_coins, the clue box redraws.
    const uint16_t keyCache = g.ui.hudKeyState;
    g.input.cmd = 8;
    quizUpdate(g);
    CHECK(g.ui.quizClosed == 1);
    CHECK((g.core.uiMode & 2) == 0);
    CHECK(g.ui.screen == 0);
    CHECK(g.core.coins == 11);
    CHECK(g.ui.promptIcon == 4);
    CHECK(g.ui.hudKeyState == static_cast<uint16_t>(keyCache + 1));
    CHECK(g.core.caughtSlot == 2);  // a right answer: the elf loses the question
    CHECK(g.input.context == 0);
}

TEST_CASE("DOS quiz: a wrong answer and Esc keep the question with the elf; F1 opens the menu over it") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    freshRecord(f);
    DosGame& g = f.g();
    UserRecord& r = g.rec();
    g.bank = &gameFiles->pak("SPRITES");
    for (size_t s = 0; s < 9; ++s) r.quizSlots[s] = static_cast<uint16_t>(20 + s);
    r.answerMasks.fill(7);
    REQUIRE(quizLoadQuestions(g, true) == 1);
    g.core.caughtSlot = 1;
    g.core.uiMode = 2;
    g.ui.quizClosed = 1;
    g.input.cmd = -1;
    quizUpdate(g);
    SUBCASE("F1") {
        g.input.cmd = 1;
        quizUpdate(g);
        CHECK(g.core.uiMode == 0x42);
        CHECK(g.ui.menuOpen == -1);
        CHECK(g.input.cmd == 1);  // left for Menu_Update
    }
    SUBCASE("wrong answer") {
        while (g.tables.ui.answerPerms[g.ui.quizOrder][static_cast<size_t>(g.ui.quizFocus)] == 0) {
            g.input.cmd = 7;
            quizUpdate(g);
        }
        g.input.cmd = 8;
        quizUpdate(g);
        CHECK(g.core.caughtSlot == 0);
        CHECK(r.history(20) == 3);
        CHECK((r.quizSlots[0] & 0x8000) == 0);
        CHECK(r.answerMasks[0] != 7);
        // Up / Down skip the greyed answer from now on (on a re-opened question).
        g.input.cmd = 0;
        quizUpdate(g);
        CHECK(g.ui.quizClosed == 1);
    }
    SUBCASE("Esc before answering") {
        g.input.cmd = 0;
        quizUpdate(g);
        CHECK(g.core.caughtSlot == 0);
        CHECK(g.ui.quizClosed == 1);
        CHECK(r.history(20) == 0);
    }
}

TEST_CASE("DOS quiz: the level-change reorder puts missed slots first and answered ones last") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    freshRecord(f);
    DosGame& g = f.g();
    UserRecord& r = g.rec();
    for (size_t s = 0; s < 9; ++s) {
        r.quizSlots[s] = static_cast<uint16_t>(200 + s);
        g.ui.puzzleText[s] = DsPtr{static_cast<uint16_t>(0xA6B8 + 10 * s)};
    }
    // slot 0 answered right (bit 0 clear), 1 missed (bit 1 cleared), 2 unasked, 3 missed, 4 right,
    // 5..8 unasked.
    r.answerMasks = {6, 5, 7, 3, 6, 7, 7, 7, 7};
    r.quizSlots[0] |= 0x8000;
    r.quizSlots[4] |= 0x8000;
    r.clueWords = {1, 2 | 0x80, 3 | 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    g.core.level = 0;
    g.core.uiMode = 0;
    g.core.keyState = 8;
    CHECK(quizReorderSlots(g) == 2);  // two hidden clue words on level 0
    const uint16_t expected[9] = {201, 203, 202, 205, 206, 207, 208, 204 | 0x8000, 200 | 0x8000};
    for (size_t i = 0; i < 9; ++i) {
        CAPTURE(i);
        CHECK(r.quizSlots[i] == expected[i]);
    }
    CHECK(r.answerMasks[0] == 5);
    CHECK(r.answerMasks[8] == 6);
    CHECK(g.ui.puzzleText[0].off == 0xA6B8 + 10);
    CHECK(g.ui.hudKeyState == 9);
    // Not while a UI mode is open.
    g.core.uiMode = 2;
    CHECK(quizReorderSlots(g) == 0);
}

TEST_CASE("DOS quiz: the dealt slots of the reference records are a valid deal of their tier") {
    TM_REQUIRE_DOS_GAME_FILES();
    for (const char* file : {"USERS_after_exit_level2_Ada.DAT", "USERS_after_exit_post_castle_Ada.DAT",
                             "USERS_crafted_tierb_layout.DAT", "USERS_crafted_tierc_layout.DAT",
                             "USERS_after_tandy_newplayer_Ben.DAT"}) {
        const auto path = capturesFile(file);
        if (!path) continue;
        dosgame::GameFixture f(*gameFiles);
        REQUIRE(f.loadUsers(*path));
        for (int i = 0; i < kUserRecordCount; ++i) {
            const UserRecord& r = f.users().record(i);
            if (r.empty() || r.quizSlots[0] == 0xFFFF || r.quizSlots[0] == 0) continue;
            CAPTURE(file);
            CAPTURE(i);
            const int tier = r.quizTier;
            REQUIRE((tier <= 3 || tier == 5));
            std::set<int> questions;
            int unanswered = 0;
            for (size_t s = 0; s < 9; ++s) {
                const int q = r.quizSlots[s] & 0x7FFF;
                questions.insert(q);
                CHECK(q >= f.g().quizTiers.start(tier));
                CHECK(q < f.g().quizTiers.start(tier + 1));
                const bool answered = (r.quizSlots[s] & 0x8000) != 0;
                unanswered += answered ? 0 : 1;
                // Answered right: the mask lost the right answer's bit; the history says right.
                if (answered) {
                    CHECK((r.answerMasks[s] & 1) == 0);
                    CHECK((r.history(q) == 1 || r.history(q) == 2));
                }
            }
            CHECK(questions.size() == 9);
            CHECK(unanswered >= 0);
        }
    }
}

TEST_CASE("DOS quiz: a new game deals the reference records' questions (the layouts' srand values)") {
    // Users_LoadPlayer + Game_Start (Game_NewSetup -> Layout_Generate -> Quiz_LoadQuestions ->
    // Quiz_TierHasWork, Quiz_PlanStatuses, Quiz_Deal) on the record as it was before the game, with
    // the srand value that reproduces that game's layout (tests/test_dos_layout.cpp): the 9 dealt
    // questions of the record written after the game. Rascal's are in dealt order; Ada's were
    // reordered at a level change (compared as sets). Ada's second game deals with the history of
    // her first (statuses 1, 2, 3 in the plan).
    TM_REQUIRE_DOS_GAME_FILES();
    struct Ref {
        const char* before;
        const char* after;
        int record;
        uint16_t seed;
        bool ordered;
    };
    const Ref refs[] = {
        {"original/USERS.DAT", "original/USERS.DAT", 0, 0xA954, true},
        {"USERS_after_signin_Ada.DAT", "USERS_after_exit_level2_Ada.DAT", 1, 0x1EE2, false},
        {"USERS_after_castle_climb_Ada.DAT", "USERS_after_exit_post_castle_Ada.DAT", 1, 0x2B40, false},
    };
    for (const Ref& r : refs) {
        const auto before = capturesFile(r.before);
        const auto after = capturesFile(r.after);
        if (!before || !after) continue;
        const std::string name = r.after;
        CAPTURE(name);
        dosgame::GameFixture expectedFix(*gameFiles);
        REQUIRE(expectedFix.loadUsers(*after));
        const UserRecord expected = expectedFix.users().record(r.record);
        dosgame::GameFixture f(*gameFiles);
        REQUIRE(f.loadUsers(*before));
        f.selectRecord(r.record);
        DosGame& g = f.g();
        g.rec().savedGame = 0;  // a new game
        usersLoadPlayer(g);
        g.layoutSeedOverride = r.seed;
        gameStart(g);
        std::array<int, 9> dealt{}, saved{};
        for (size_t s = 0; s < 9; ++s) {
            dealt[s] = g.rec().quizSlots[s] & 0x7FFF;
            saved[s] = expected.quizSlots[s] & 0x7FFF;
        }
        if (!r.ordered) {
            std::sort(dealt.begin(), dealt.end());
            std::sort(saved.begin(), saved.end());
        }
        CHECK(dealt == saved);
        CHECK(g.rec().quizTier == expected.quizTier);
    }
}
