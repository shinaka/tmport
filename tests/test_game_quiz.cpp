// Elf questions (game/quiz.h): the parser against tools/assets/othr_puzzles.py (structure and an
// FNV-1a hash over all 359 parsed questions, produced by extracted/scratch/game/ref_vectors.py),
// the narration tables of the EXE, and the deal / promotion / judging / carry-over rules on
// synthetic histories.

#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "data/dgroup_offsets.h"
#include "game/game_tables.h"
#include "game/quiz.h"
#include "game/rules.h"
#include "test_data.h"
#include "util/hash.h"

using namespace tmport;
using namespace tmport::game;

namespace {

QuizTables shippedTierStarts() {
    QuizTables t;
    t.tierStart = {0, 63, 179, 302, 359};
    return t;
}

bool isPermutation(const std::array<uint8_t, 3>& p) {
    std::array<uint8_t, 3> s = p;
    std::sort(s.begin(), s.end());
    return s == std::array<uint8_t, 3>{0, 1, 2};
}

void put16(std::vector<uint8_t>& b, size_t v) {
    b.push_back(uint8_t(v & 0xFF));
    b.push_back(uint8_t((v >> 8) & 0xFF));
}

}  // namespace

TEST_CASE("quiz: all 359 questions parse like othr_puzzles.py") {
    TM_REQUIRE_GAME_DATA();
    uint64_t h = kFnv64Offset;
    std::map<int, int> hist;
    int totalLines = 0, answers3 = 0, withBlankLine = 0;
    for (int q = 0; q < kQuestionCount; ++q) {
        auto raw = g->res->loadRaw(kQuestionFirstId + q);
        REQUIRE(raw.has_value());
        auto pq = parseQuestion(*raw);
        REQUIRE(pq.has_value());
        std::vector<uint8_t> blob;
        put16(blob, size_t(q));
        blob.push_back(pq->lineSlots);
        blob.push_back(uint8_t(pq->numLines));
        blob.push_back(uint8_t(pq->blankCount));
        for (int i = 0; i < 10; ++i)
            if (pq->blank[size_t(i)]) blob.push_back(uint8_t(i));
        put16(blob, pq->questionLen);
        const auto qs = pq->question();
        blob.insert(blob.end(), qs.begin(), qs.end());
        for (int a = 0; a < 3; ++a) {
            const auto s = pq->answer(a);
            put16(blob, s.size());
            blob.insert(blob.end(), s.begin(), s.end());
        }
        h = fnv1a64(blob, h);
        ++hist[pq->numLines];
        totalLines += pq->numLines;
        if (pq->answerCount == 3) ++answers3;
        if (pq->blankCount > 1) ++withBlankLine;  // an empty line inside the body (+ the final one)
        // Line slots: text lines + empty lines + 1 (code_1150-1160.md §3.4); 4..8 in the data.
        CHECK(pq->lineSlots == pq->numLines + pq->blankCount + 1);
        CHECK(pq->lineSlots >= 4);
        CHECK(pq->lineSlots <= 8);
    }
    CHECK(h == 0xfe4a0fbc5b4fad1dull);
    CHECK(hist == std::map<int, int>{{2, 18}, {3, 24}, {4, 173}, {5, 99}, {6, 45}});
    CHECK(totalLines == 1565);
    CHECK(answers3 == 359);
    CHECK(withBlankLine == 30);  // fmt_othr_puzzles.md §4.1
}

TEST_CASE("quiz: parser details on a synthetic question") {
    // header code, leading CR, two lines, an empty line, a line, CR CR CR, answers, trailing CR.
    const std::vector<uint8_t> res = {0xFF, 0x05, 0xFF, 0x0D, 'a', 'b', 0x0D, 'c', 0x0D, 0x0D, 'd', 0x0D, 0x0D, 0x0D,
                                      'X', 0x0D, 'Y', 'Y', 0x0D, 'Z', 0x0D, 0, 0, 0};
    auto pq = parseQuestion(res);
    REQUIRE(pq.has_value());
    CHECK(pq->question() == std::string_view("\xFF\x05\xFF ab\rc\r\rd\r\r", 13));
    CHECK(pq->lineSlots == 6);
    CHECK(pq->blankCount == 2);
    CHECK(pq->numLines == 3);
    CHECK(pq->blank[2] == 1);
    CHECK(pq->blank[4] == 1);
    CHECK(pq->lineY(0, 17) == 0);
    CHECK(pq->lineY(1, 17) == 17);
    CHECK(pq->lineY(2, 17) == 3 * 17);  // skips the empty slot 2
    CHECK(pq->answer(0) == "X");
    CHECK(pq->answer(1) == "YY");
    CHECK(pq->answer(2) == "Z\r");  // the third answer keeps its trailing CR
    // No terminator: nullopt (the original would write answer[-1]).
    const std::vector<uint8_t> bad = {0x0D, 'a', 0x0D, 0x0D, 0};
    CHECK_FALSE(parseQuestion(bad).has_value());
}

TEST_CASE("quiz: narration tables agree with the parsed line counts") {
    TM_REQUIRE_GAME_DATA();
    const GameTables t = GameTables::fromExe(*g->exe);
    CHECK(t.quiz.tierStart == std::array<int16_t, 5>{0, 63, 179, 302, 359});
    CHECK(t.quiz.voiceBase[0] == 4000);
    int plus3 = 0, plus0 = 0, plus4 = 0;
    std::set<int> noRecordings;
    std::array<int, 4> banks{};
    for (int q = 0; q < kQuestionCount; ++q) {
        const auto pq = parseQuestion(*g->res->loadRaw(kQuestionFirstId + q));
        const int next = q + 1 < kQuestionCount ? t.quiz.voiceBase[size_t(q) + 1] : 6616;
        const int d = next - t.quiz.voiceBase[size_t(q)];
        if (d == pq->numLines + 3) {
            ++plus3;
        } else if (d == pq->numLines) {
            ++plus0;
            noRecordings.insert(q);
        } else if (d == pq->numLines + 4) {
            ++plus4;
            CHECK(q == 225);  // one unused duplicate id (5621)
        } else {
            FAIL("question " << q << ": " << d << " ids for " << pq->numLines << " lines");
        }
        const int bank = t.quiz.voiceBank[size_t(q)];
        REQUIRE(bank >= 1);
        REQUIRE(bank <= 3);
        ++banks[size_t(bank)];
        CHECK(quizLineVoice(t.quiz, q, 0) == t.quiz.voiceBase[size_t(q)]);
        if (!isSilentAnswerQuestion(q, t.quiz))
            CHECK(quizAnswerVoice(t.quiz, q, pq->numLines, 2) == t.quiz.voiceBase[size_t(q)] + pq->numLines + 2);
        else
            CHECK(quizAnswerVoice(t.quiz, q, pq->numLines, 0) == -1);
    }
    CHECK(plus3 == 349);
    CHECK(plus0 == 9);
    CHECK(plus4 == 1);
    CHECK(banks == std::array<int, 4>{0, 124, 129, 106});
    // Every question without answer recordings is in the silent list (16 entries, 9 + 7).
    for (int q : noRecordings) CHECK(isSilentAnswerQuestion(q, t.quiz));
    CHECK(quizBankBase(1) == 7300);
    CHECK(quizBankBase(2) == 7400);
    CHECK(quizBankBase(3) == 7500);
    CHECK(quizBankBase(4) == -1);
    // Tiers by index.
    CHECK(questionTier(0, t.quiz) == 0);
    CHECK(questionTier(62, t.quiz) == 0);
    CHECK(questionTier(63, t.quiz) == 1);
    CHECK(questionTier(301, t.quiz) == 2);
    CHECK(questionTier(358, t.quiz) == 3);
}

TEST_CASE("quiz: tier ranges and counts") {
    const QuizTables t = shippedTierStarts();
    QuizState s;
    const int lo[5] = {0, 63, 179, 302, 0}, hi[5] = {62, 178, 301, 358, 358};
    for (uint8_t tier = 0; tier < 6; ++tier) {
        s.tier = tier;
        quizSetTierRange(s, t);
        CHECK(s.rangeLo == lo[std::min<int>(tier, 4)]);
        CHECK(s.rangeHi == hi[std::min<int>(tier, 4)]);
    }
    s.tier = 0;
    quizSetTierRange(s, t);
    s.status[0] = 1;
    s.status[1] = 2;
    s.status[2] = 3;
    s.status[3] = 0x82;  // dealt: not counted in any pool
    s.status[63] = 1;    // outside the range
    quizCountStatusInRange(s);
    CHECK(s.count == std::array<int16_t, 4>{59, 1, 1, 1});
}

TEST_CASE("quiz: new-player deal (fresh history)") {
    const QuizTables t = shippedTierStarts();
    for (uint16_t seed : std::initializer_list<uint16_t>{1, 2, 777, 65535}) {
        CAPTURE(seed);
        QuizState s;
        BorlandRand rng;
        rng.srand(seed);
        CHECK_FALSE(quizNewRound(s, t, rng));
        std::set<int> qs;
        for (const QuizSlot& slot : s.slots) {
            CHECK(slot.q >= 0);
            CHECK(slot.q <= 62);
            qs.insert(slot.q);
            CHECK(isPermutation(slot.perm));
            CHECK(slot.tried == std::array<uint8_t, 3>{0, 0, 0});
            CHECK(s.status[size_t(slot.q)] == kQuizDealtBit);
        }
        CHECK(qs.size() == 9);
        int dealt = 0;
        for (uint8_t v : s.status) dealt += (v & kQuizDealtBit) ? 1 : 0;
        CHECK(dealt == 9);
        // Exactly 9 picks + 9 slot draws + 27 order draws = 45 rand() calls (1150:01d5).
        BorlandRand ref;
        ref.srand(seed);
        for (int i = 0; i < 45; ++i) ref.rand();
        CHECK(rng.seed() == ref.seed());
    }
}

TEST_CASE("quiz: pools, fallback chain and pick order") {
    const QuizTables t = shippedTierStarts();
    QuizState s;
    s.tier = 1;  // 63..178
    for (int q = 63; q <= 178; ++q) s.status[size_t(q)] = kQuizRightFirstTry;
    s.status[100] = kQuizMissed;            // one missed question
    s.status[70] = kQuizNeverAnswered;      // two unseen
    s.status[71] = kQuizNeverAnswered;
    s.status[150] = kQuizRightAfterMiss;    // one retried
    s.status[10] = kQuizMissed | kQuizDealtBit;  // stale bit outside the range: cleared by the round
    BorlandRand rng;
    rng.srand(4242);
    // 116 questions, 112 right first time: total*80 = 9280 <= count1*100 = 11200 -> promoted,
    // and the deal comes from tier 2.
    CHECK(quizNewRound(s, t, rng));
    CHECK(s.tier == 2);
    CHECK(s.status[10] == kQuizMissed);
    // Tier 2 (179..301) is all unseen: 9 unseen questions dealt.
    for (const QuizSlot& slot : s.slots) {
        CHECK(slot.q >= 179);
        CHECK(slot.q <= 301);
    }

    // Without promotion: tier 1 with few right answers.
    QuizState u;
    u.tier = 1;
    u.status[100] = kQuizMissed;
    u.status[150] = kQuizRightAfterMiss;
    for (int q = 63; q <= 178; ++q)
        if (q != 100 && q != 150) u.status[size_t(q)] = q < 120 ? kQuizRightFirstTry : kQuizNeverAnswered;
    BorlandRand r2;
    r2.srand(99);
    CHECK_FALSE(quizNewRound(u, t, r2));
    CHECK(u.tier == 1);
    std::map<int, int> byStatus;  // statuses after clearing the dealt bit
    std::set<int> qs;
    for (const QuizSlot& slot : u.slots) {
        qs.insert(slot.q);
        ++byStatus[u.status[size_t(slot.q)] & 0x7F];
    }
    CHECK(qs.size() == 9);
    CHECK(qs.count(100) == 1);  // the only missed question is always dealt
    CHECK(qs.count(150) == 1);  // the only retried question too
    // 3 unseen + (1 missed, then 2 from the fallback 3: 1 retried, then 0: 1 unseen) +
    // (retried pool now empty -> 0: 3 unseen) = 7 unseen, 1 missed, 1 retried.
    CHECK(byStatus == std::map<int, int>{{0, 7}, {2, 1}, {3, 1}});
}

TEST_CASE("quiz: tier promotion boundary (80 %)") {
    const QuizTables t = shippedTierStarts();
    auto promotes = [&](uint8_t tier, int right) {
        QuizState s;
        s.tier = tier;
        quizSetTierRange(s, t);
        for (int q = s.rangeLo, k = 0; q <= s.rangeHi; ++q, ++k)
            s.status[size_t(q)] = k < right ? kQuizRightFirstTry : kQuizMissed;
        BorlandRand rng;
        return quizNewRound(s, t, rng);
    };
    CHECK(promotes(0, 51));        // 63*80 = 5040 <= 5100
    CHECK_FALSE(promotes(0, 50));  // 5040 > 5000
    CHECK(promotes(1, 93));        // 116*80 = 9280 <= 9300
    CHECK_FALSE(promotes(1, 92));
    CHECK(promotes(3, 46));        // 57*80 = 4560 <= 4600
    CHECK_FALSE(promotes(3, 45));
    CHECK_FALSE(promotes(4, 359)); // tier 4 never promotes
}

TEST_CASE("quiz: judging, coins and statuses") {
    GameTables t;
    t.hintBit.fill(-1);
    GameState s;
    s.quiz.slots[4] = QuizSlot{17, {2, 0, 1}, {0, 0, 0}};
    s.quiz.status[17] = kQuizDealtBit;
    // Wrong (display 0 shows answer 2), then wrong (display 2 shows answer 1), then right.
    QuizJudgement j = quizJudgeAnswer(s.quiz, 4, 0);
    CHECK_FALSE(j.correct);
    CHECK(s.quiz.status[17] == kQuizMissed);
    CHECK(s.quiz.slots[4].tried == std::array<uint8_t, 3>{1, 0, 0});
    CHECK(s.quiz.slots[4].q == 17);
    CHECK(quizOpenPositions(s.quiz.slots[4]) == 2);
    j = quizJudgeAnswer(s.quiz, 4, 2);
    CHECK_FALSE(j.correct);
    j = quizJudgeAnswer(s.quiz, 4, 1);
    CHECK(j.correct);
    CHECK(j.wrongTries == 2);
    CHECK(s.quiz.status[17] == kQuizRightAfterMiss);
    CHECK(s.quiz.slots[4].q == kQuizNoQuestion);
    CHECK(quizCoinsForCorrect(s.quiz.slots[4]) == 0);

    // answerQuestion: judge, R(4) for the result screen, then clue word and coins.
    s.inv.coins = 5;
    s.inv.numClues = 1;
    s.quiz.slots[0] = QuizSlot{3, {1, 0, 2}, {0, 0, 0}};
    BorlandRand rng, ref;
    rng.srand(5);
    ref.srand(5);
    const AnswerOutcome a = answerQuestion(s, 0, 1, t, rng);
    CHECK(a.correct);
    CHECK(a.messageIndex == ref.random(4));
    CHECK(a.clueIndex == 1);
    CHECK(a.coins == 2);
    CHECK(s.inv.coins == 7);
    CHECK(s.inv.numClues == 2);
    CHECK(s.quiz.status[3] == kQuizRightFirstTry);
    CHECK(rng.seed() == ref.seed());
    // One earlier miss: +1 coin.
    s.quiz.slots[1] = QuizSlot{8, {0, 1, 2}, {0, 1, 0}};
    const AnswerOutcome b = answerQuestion(s, 1, 0, t, rng);
    CHECK(b.coins == 1);
    CHECK(s.inv.coins == 8);
    CHECK(s.quiz.status[8] == kQuizRightAfterMiss);
    // Wrong answer: no coins, no clue.
    s.quiz.slots[2] = QuizSlot{9, {0, 1, 2}, {0, 0, 0}};
    const AnswerOutcome c = answerQuestion(s, 2, 2, t, rng);
    CHECK_FALSE(c.correct);
    CHECK(c.coins == 0);
    CHECK(s.inv.numClues == 3);
    CHECK(s.inv.coins == 8);
}

TEST_CASE("quiz: level-change carry-over is the broken swap (copy)") {
    QuizState s;
    for (int i = 0; i < 9; ++i) s.slots[size_t(i)] = QuizSlot{int16_t(10 + i), {0, 1, 2}, {0, 0, 0}};
    s.status[10] = kQuizMissed;                  // answered wrong this climb: carried
    s.status[11] = kQuizMissed | kQuizDealtBit;  // dealt from the missed pool, not asked: stays
    s.slots[0].tried = {0, 1, 0};
    s.slots[2].q = kQuizNoQuestion;              // answered: nothing to carry
    quizCarryMissedToNextLevel(s, 0);
    CHECK(s.slots[3] == s.slots[0]);             // slot 3 = copy incl. order and tried flags
    CHECK(s.slots[0].q == 10);                   // the old slot keeps it
    CHECK(s.slots[4].q == 14);
    CHECK(s.slots[5].q == 15);
    s.status[13] = kQuizMissed;
    s.slots[3].q = 13;
    quizCarryMissedToNextLevel(s, 1);
    CHECK(s.slots[6].q == 13);
    const QuizState before = s;
    quizCarryMissedToNextLevel(s, 2);            // level 2 -> castle: nothing
    CHECK(s == before);
}

TEST_CASE("quiz: question-screen focus order (1150:0f85 / 0fcb)") {
    const QuizSlot slot{5, {0, 1, 2}, {0, 1, 0}};  // display position 1 already tried
    // Voice off: answers only, the tried one skipped, cyclic.
    CHECK(quizNextItem(-1, slot, false, 4) == 0);
    CHECK(quizNextItem(0, slot, false, 4) == 2);
    CHECK(quizNextItem(2, slot, false, 4) == 0);
    CHECK(quizPrevItem(0, slot, false, 4) == 2);
    CHECK(quizPrevItem(2, slot, false, 4) == 0);
    // Voice on: after the last answer come the question lines 10..13, then back to the answers.
    CHECK(quizNextItem(2, slot, true, 4) == 10);
    CHECK(quizNextItem(10, slot, true, 4) == 11);
    CHECK(quizNextItem(13, slot, true, 4) == 0);
    CHECK(quizPrevItem(0, slot, true, 4) == 13);
    CHECK(quizPrevItem(11, slot, true, 4) == 10);
    CHECK(quizPrevItem(10, slot, true, 4) == 2);
}
