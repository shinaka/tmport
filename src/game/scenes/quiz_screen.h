#pragma once
// The elf question screen (segment 1150: Quiz_AskQuestion 1150:100a and its helpers;
// re/docs/code_1150-1160.md §3.5-3.8, fmt_othr_puzzles.md §8, ARCHITECTURE.md §5.6).
//
//   screen     scroll PMAP 2000 opaque at (0,20); the question (OTHR 11000+q, parsed by
//              game::parseQuestion) in EGA 0 centred, the three answers in display order in
//              yellow (EGA 14) or dark grey (EGA 8, answered wrongly before, not selectable);
//              with Voice on the speaker button PMAP 2001 at (438,220);
//   focus      two nested EGA 4 frames around an answer box or the speaker button; Up/Down move
//              through the open answers and (Voice on) the question lines, Left/Right toggle the
//              speaker button; Enter chooses the focused answer, speaks the focused line or (on
//              the speaker) reads everything;
//   mouse      hot spots 0xA7F play area, 0xA80+pos answers, 0xA70+k lines (k*lh rows: the
//              original's quirk, the reading highlight uses the real line rows), 0xA88 speaker;
//              press = focus, release on the focused item = choose / speak;
//   reading    yellow highlight (Gfx_HighlightRect) of each line / answer while it is spoken,
//              250 ms after the lines, 500 ms before each answer; the QuizLine / QuizAnswer stop
//              lists interrupt it;
//   judging    game::quizJudgeAnswer, then the result screen draws its R(4) at the original
//              point, then (correct) game::finishCorrectAnswer: clue word and +2/+1/0 coins;
//              Esc records nothing;
//   results    praise: sfx 12, PMAP 2011 bubble, praise text 0..3 and the new clue word (EGA 4)
//              spoken in the question's narrator bank; sorry: sfx 8, sorry text 0..3;
//   music      halted for the question, restarted from the beginning afterwards.

#include <array>
#include <cstdint>

#include "app/app_context.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/quiz.h"
#include "game/scenes/scene_support.h"
#include "gfx/frame_buffer.h"

namespace tmport::text {
class TextRenderer;
}

namespace tmport::game::scenes {

// Hot-spot ids of the question screen (1150:0a97) and the result screens (1150:049a / 0748).
namespace quizhot {
inline constexpr uint16_t kPlayArea = 0xA7F;  // (0,20,512,267): keeps clicks off 0x409
inline constexpr uint16_t kAnswer0 = 0xA80;   // + display position (untried answers only)
inline constexpr uint16_t kLine0 = 0xA70;     // + non-blank line k (Voice on only)
inline constexpr uint16_t kSpeaker = 0xA88;   // speaker button (Voice on only)
inline constexpr uint16_t kAnyClick = 0x0D;   // result screens with Voice on: a click = Enter
}  // namespace quizhot

// Fixed facts of 1150 (code constants).
namespace quiz {
inline constexpr int kScrollPmap = 2000;    // 0x7d0, 512x267
inline constexpr int kSpeakerPmap = 2001;   // 0x7d1, 44x64
inline constexpr int kBubblePmap = 2011;    // 0x7db, 418x205 (shared with the hint popup)
inline constexpr int kSpeakerX = 438, kSpeakerY = 220;
inline constexpr int kSpeakerRectX = 434, kSpeakerRectY = 217;
inline constexpr int kBubbleX = 33, kBubbleY = 60;
inline constexpr int kResultTextX = 33, kResultTextY = 136, kResultTextW = 416;
inline constexpr int kClueCentreX = 241;
inline constexpr int kSfxPraise = 12, kSfxSorry = 8;
inline constexpr int kLinePauseMs = 250, kAnswerPauseMs = 500;
inline constexpr int kFocusColour = 4, kAnswerColour = 14, kTriedColour = 8;
}  // namespace quiz

// The geometry Quiz_DrawQuestionScreen (1150:0a97) computes (g_quizText*, g_quizLineY,
// g_quizAns*, g_quizSpkRect).
struct QuizLayout {
    int lineHeight = 0;
    int textX = 0, textY = 0, textW = 0;  // g_quizTextX / Y / W (box of the question text)
    std::array<int, 10> lineY{};          // g_quizLineY: y of the k-th non-blank line from textY
    int numLines = 0;                     // g_quizNumLines
    int ansX = 0, ansW = 0, ansH = 0;     // common answer box x / width / height (lh + 6)
    std::array<int, 3> ansY{};            // box top per display position
    std::array<int, 3> ansTextX{}, ansTextY{}, ansTextW{};  // where each answer is drawn
    gfx::Rect speaker{};                  // g_quizSpkRect (focus frame / hot spot 0xA88)

    gfx::Rect answerBox(int pos) const { return {ansX, ansY[static_cast<size_t>(pos)], ansW, ansH}; }
    // Reading highlight of an answer (1150:0d2d mode 0).
    gfx::Rect answerHighlight(int pos) const { return {ansX, ansY[static_cast<size_t>(pos)] + 3, ansW, lineHeight}; }
    // Hot spot 0xA70+k: k*lh rows, blank lines not skipped (kept quirk).
    gfx::Rect lineHotspot(int k) const { return {textX, textY + k * lineHeight, textW, lineHeight}; }
    // Reading highlight of line k: the real row g_quizLineY[k].
    gfx::Rect lineHighlight(int k) const { return {textX, textY + lineY[static_cast<size_t>(k)], textW, lineHeight}; }
};

// 1150:0a97 layout part. speakerW/H = size of PMAP 2001 (44x64 in the shipped art).
QuizLayout layoutQuestion(const ParsedQuestion& pq, const QuizSlot& slot, text::TextRenderer& text, int speakerW,
                          int speakerH);

struct QuizResult {
    int result = 0;          // the original's return value: 1 correct, 0 wrong or Esc
    bool answered = false;   // false: left with Esc (nothing recorded)
    bool correct = false;
    int position = -1;       // display position chosen
    int messageIndex = -1;   // R(4) of the result screen (praise / sorry text and voice)
    int clueIndex = -1;      // correct: clue word shown and spoken (numClues before the increment)
    int coins = 0;           // correct: coins awarded
};

// 1150:100a Quiz_AskQuestion(slot): the question of g_quizSlots[slot] (slot = 3*level + elf).
// Uses SceneHooks::rng (default ctx.engine.rng()) for the result screen's R(4). Does nothing
// (result 0) for an empty slot.
QuizResult askQuestion(AppContext& ctx, GameState& s, const GameTables& t, int slot, const SceneHooks& hooks = {});

}  // namespace tmport::game::scenes
