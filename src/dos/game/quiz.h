#pragma once
// quiz.c (segment 46ce): the elf question screen, judging and rewards, question loading and
// dictionary expansion, the deal, promotion, the level-change reorder, and Clue_Reveal. Clue_BuildWords
// (46ce:079c) is in layout.h (it runs inside Layout_PlaceKey). The question texts live in the
// UiState buffers (expandedText, questionText, answerText). Spec: re/dos/docs/code_ui.md §10.
// Owner: ui agent.

#include <cstddef>
#include <cstdint>
#include <span>

namespace tmport::dos {

class DosGame;

// 46ce:0007 Quiz_Update (per frame while g_uiMode == 2): opens the caught slot's question (pick,
// split, zones, painter 1), Esc closes, Enter answers (Quiz_Correct / Quiz_Wrong, Hud_Redraw),
// Up/Down skip disabled rows, F1-F3 open the menu over it; after answering Enter or a click closes
// (coins = g_quizCoins).
void quizUpdate(DosGame& g);
// 46ce:0271 Quiz_DrawScreen (screen painter 1, when DS:922E != 0): Hud_Redraw, frame, question, answers.
void quizDrawScreen(DosGame& g);
// 46ce:028d Quiz_DrawFrame: SPRITES frame 57 at (0, 0), elf frames 94 and 103 mirrored at (0x8E, 0x8E).
void quizDrawFrame(DosGame& g);
// 46ce:02d9 Quiz_PickQuestion: slot = g_caughtSlot - 1, question, mask, text; order = rand() % 6.
int quizPickQuestion(DosGame& g);
// 46ce:0323 Quiz_SplitText: question before '~' into g_questionText, 3 answers into g_answerText;
// g_answerWidth.
void quizSplitText(DosGame& g);
// 46ce:03ff Quiz_SetupAnswers: zones 9..11 of context 3 for the enabled answers; first focus.
void quizSetupAnswers(DosGame& g);
// 46ce:0482 Quiz_DrawQuestion: Text_Draw(10, 0x1C, question) in black.
void quizDrawQuestion(DosGame& g);
// 46ce:049e Quiz_DrawAnswers: answers (disabled in light grey), focus on the first enabled row.
void quizDrawAnswers(DosGame& g);
// 46ce:052d Quiz_SetFocus(i): moves the red frame to row i.
void quizSetFocus(DosGame& g, int i);
// 46ce:05bb Quiz_Correct: Clue_Reveal, slot answered (bit 15), praise text 15 + rand() % 4, the word
// in red, reward DS:5F93[mask] into g_quizCoins, sfx 10, history 1 or 2.
void quizCorrect(DosGame& g);
// 46ce:069e Quiz_Wrong: g_caughtSlot = 0, text 19 + rand() % 4, sfx 11, history |= 3.
void quizWrong(DosGame& g);
// 46ce:06f0 Clue_Reveal: reveals the next hidden clue word of the level in the order DS:5FBC[rand() %
// 6]; g_praiseWord = its string.
void clueReveal(DosGame& g);
// 46ce:08f1 Dict_Expand(src, dst): bytes >= 0x80 -> PRG_DICT words; writes into g.ui.expandedText at
// `dstOffset` (NUL-terminated); returns the length without the NUL.
int dictExpand(DosGame& g, std::span<const uint8_t> src, size_t dstOffset);
// 46ce:095f Quiz_LoadDictionary: PRG_DICT is loaded with the game (DosGame::dictionary); sets DS:A21A
// to its length; returns 1.
int quizLoadDictionary(DosGame& g);
// 46ce:09ab Quiz_LoadQuestions(resume): tier range; new game: promotion check (Quiz_TierHasWork, tier
// 5 with Quiz_ResetHistory) and Quiz_Deal; expands the 9 dealt questions back to back. Returns 1.
int quizLoadQuestions(DosGame& g, bool resume);
// 46ce:0b6a Quiz_Deal: 9 distinct questions of the tier by wanted status (Quiz_PlanStatuses),
// rand() per attempt, slot 8 down to 0.
void quizDeal(DosGame& g);
// 46ce:0c39 Quiz_PlanStatuses: replaces wanted statuses with their fallbacks while the counts run out
// (guard the 0 <-> 1 endless loop: a documented deviation).
void quizPlanStatuses(DosGame& g);
// 46ce:0cd0 Quiz_TierHasWork: counts the statuses of the tier; 1 while right-first-time < 70 %.
int quizTierHasWork(DosGame& g);
// 46ce:0d60 Quiz_ResetHistory: history bits from the tier start cleared (one byte past the history:
// setHistoryByte(rec, 90, ...)).
void quizResetHistory(DosGame& g);
// 46ce:0d9a Quiz_FindTierEnd: DS:5FDC = the first question past the 0x7B00 window, DS:5FDE = 360
// (g.quizTiers holds them).
void quizFindTierEnd(DosGame& g);
// 46ce:0df2 Quiz_ReorderSlots (Scene_Update, scenes < 3, g_uiMode == 0): missed slots first, then
// unasked, answered last; DS:9268 = key state + 1; returns the hidden clue words of the level.
int quizReorderSlots(DosGame& g);

}  // namespace tmport::dos
