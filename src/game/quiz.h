#pragma once
// Elf questions, deterministic part (segment 1150 quiz.cpp): question-history pools, tiers, the
// per-climb deal, tier promotion, answer judging, the level-change carry-over and the question
// resource parser. The question screen itself (drawing, input loop, narration playback) is UI.
// Specs: re/docs/code_1150-1160.md §3, fmt_othr_puzzles.md §4, §6-8, ARCHITECTURE.md §5.6.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "game/game_tables.h"
#include "mischief/rtl/borland_rand.h"

namespace tmport::game {

// History byte values (DS:5f83[q], record +0x30).
inline constexpr uint8_t kQuizNeverAnswered = 0;
inline constexpr uint8_t kQuizRightFirstTry = 1;
inline constexpr uint8_t kQuizMissed = 2;          // last attempt wrong
inline constexpr uint8_t kQuizRightAfterMiss = 3;
inline constexpr uint8_t kQuizDealtBit = 0x80;     // dealt this climb (saved as is)
inline constexpr int16_t kQuizNoQuestion = -1;

// QuizSlot (8 bytes at DS:5f3a + 8*slot; record +0x197).
struct QuizSlot {
    int16_t q = kQuizNoQuestion;                   // +0 question 0..358, -1 = none / answered
    std::array<uint8_t, 3> perm{0xFF, 0xFF, 0xFF}; // +2 perm[displayPos] = answer index (0 = correct)
    std::array<uint8_t, 3> tried{};                // +5 1 = that display position was answered wrongly
    bool operator==(const QuizSlot&) const = default;
};

// Module state of 1150 (persisted part: tier, status, slots).
struct QuizState {
    uint8_t tier = 0;                               // DS:5f82 g_quizTier 0..4 (record +0x2f)
    std::array<uint8_t, kQuestionCount> status{};   // DS:5f83 g_quizStatus (record +0x30)
    std::array<QuizSlot, kQuizSlotCount> slots{};   // DS:5f3a g_quizSlots (record +0x197)
    int16_t rangeLo = 0;                            // DS:60ee g_quizRangeLo (inclusive)
    int16_t rangeHi = 0;                            // DS:60f0 g_quizRangeHi (inclusive)
    std::array<int16_t, 4> count{};                 // DS:60f2..60f8 g_quizCount[status]
    bool operator==(const QuizState&) const = default;
};

inline int quizSlotIndex(int level, int elf) { return 3 * level + elf; }  // 10e0:01b8
// Tier of a question by index (0..3), from the tier start table.
int questionTier(int q, const QuizTables& t);
bool isSilentAnswerQuestion(int q, const QuizTables& t);

// 1150:0000 Quiz_SetTierRange
void quizSetTierRange(QuizState& s, const QuizTables& t);
// 1150:0057 Quiz_CountStatusInRange (exact byte compare: dealt entries are not counted)
void quizCountStatusInRange(QuizState& s);
// 1150:00a7 Quiz_PickByStatus(status, n, out): writes n question indices to out[n-1] down to
// out[0], marks them dealt, falls back 0->1->2->3->0 when a pool runs out. One R(cnt) per pick.
// (The original recurses forever when every pool is empty; this stops after a full cycle.)
void quizPickByStatus(QuizState& s, int status, int n, std::span<int16_t> out, mischief::BorlandRand& rng);
// 1150:01d5 Quiz_FillSlots: 3 unseen, 3 missed, 3 retried picks, random slots, random answer order.
void quizFillSlots(QuizState& s, mischief::BorlandRand& rng);
// 1150:0327 Quiz_NewRound (start of every new climb). Returns true if the quiz tier was promoted.
bool quizNewRound(QuizState& s, const QuizTables& t, mischief::BorlandRand& rng);
// 1150:0375 Quiz_CarryMissedToNextLevel: the "broken swap" -- each slot of oldLevel whose question
// is currently missed (status 2) is copied to the same elf's slot of the next level; the old slot
// keeps it too. oldLevel >= 2 does nothing.
void quizCarryMissedToNextLevel(QuizState& s, int oldLevel);
// 1150:0314 Quiz_ResetHistory
void quizResetHistory(QuizState& s);

// 1150:0403 Quiz_JudgeAnswer, state part: display position `pos` of slot `slot` was chosen.
// Correct (perm[pos] == 0): status := 1 if no wrong try, else 3; slot.q := -1 (tried flags kept).
// Wrong: status := 2; tried[pos] := 1. The caller shows the result screen (one R(4) for its text)
// and, when correct, calls finishCorrectAnswer (rules.h) for the clue word and the coins.
struct QuizJudgement {
    bool correct = false;
    int question = -1;
    int wrongTries = 0;  // wrong display positions before this answer (0..2)
};
QuizJudgement quizJudgeAnswer(QuizState& s, int slot, int pos);
// 1150:100a tail: coins for a correct answer: +2 / +1 / 0 for 0 / 1 / 2 wrong tries.
int quizCoinsForCorrect(const QuizSlot& slot);
// Display positions that can still be chosen (tried == 0).
int quizOpenPositions(const QuizSlot& slot);

// --- Question-screen focus order (1150:0f85 Quiz_NextItem, 1150:0fcb Quiz_PrevItem) ------------
// Items: 0..2 = answer display positions (tried ones are skipped), 10..10+numLines-1 = question
// lines (only reachable with voice on). Down/Up move through them cyclically; the first item of a
// question is quizNextItem(-1, ...).
int quizNextItem(int item, const QuizSlot& slot, bool voice, int numLines);
int quizPrevItem(int item, const QuizSlot& slot, bool voice, int numLines);

// --- Narration ids (code_1150-1160.md §3.8, §3.7) ----------------------------------------------
// SSND id of question line k (0-based among the non-blank lines).
int quizLineVoice(const QuizTables& t, int q, int k);
// SSND id of answer index a (file order, 0 = correct) or -1 for the silent-answer questions.
int quizAnswerVoice(const QuizTables& t, int q, int numLines, int answer);
// Narrator bank base: 7300 / 7400 / 7500 for bank 1 / 2 / 3 (other values: -1, uninitialised in
// the original). Praise m -> base + m, sorry m -> base + 4 + m, clue word -> base + clueVoice.
int quizBankBase(int bank);

// --- Question resource (OTHR 11000 + q), parser 1150:0901 ----------------------------------------
// The parser edits its copy in place, like the original: leading line breaks become spaces, the
// third consecutive break of the question becomes NUL, the breaks between answers become NUL.
// Text is kept as raw bytes (FFNT encoding with FF escapes); views are offsets into `text`.
// g_quizLineY has 10 entries: only lines in the first 10 line slots can be read out, highlighted
// and clicked. A longer question (only possible in a mod; the shipped ones have at most 6 lines)
// gets numLines clamped to those lines (REVIEW_1 L-1); its text is still drawn whole.
inline constexpr int kMaxQuestionLines = 10;
struct ParsedQuestion {
    std::vector<uint8_t> text;      // the edited copy (up to and including the first NUL of the resource)
    size_t questionLen = 0;         // question = text[0, questionLen) (ends before the terminator)
    std::array<size_t, 3> answerOff{};
    std::array<size_t, 3> answerLen{};  // answer 2 runs to the first NUL (keeps a trailing CR)
    int answerCount = 0;            // answers found (3 for every shipped question)
    uint8_t lineSlots = 0;          // DS:610e g_quizLineSlots: line breaks seen (incl. terminator)
    int16_t numLines = 0;           // DS:610f g_quizNumLines: non-blank question lines (<= 10, see above)
    std::array<uint8_t, kMaxQuestionLines> blank{};      // blank flag per line slot (first 10)
    std::array<int8_t, kMaxQuestionLines> lineSlotOf{};  // k-th non-blank line -> line slot (g_quizLineY[k] = slot*lh)
    int blankCount = 0;
    bool terminated = false;        // three consecutive breaks found

    std::string_view question() const { return view(0, questionLen); }
    std::string_view answer(int a) const {
        return view(answerOff[static_cast<size_t>(a)], answerLen[static_cast<size_t>(a)]);
    }
    // DS:6115 g_quizLineY: y offset of the k-th non-blank line for a font of height lh.
    int lineY(int k, int lineHeight) const { return lineSlotOf[static_cast<size_t>(k)] * lineHeight; }

private:
    std::string_view view(size_t off, size_t len) const {
        return std::string_view(reinterpret_cast<const char*>(text.data()) + off, len);
    }
};
// nullopt when the question has no terminator (the original would write answer[-1]).
std::optional<ParsedQuestion> parseQuestion(std::span<const uint8_t> resource);

}  // namespace tmport::game
