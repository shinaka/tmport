// quiz.c (segment 46ce) of the 1990 DOS edition: the elf question screen, the judging and its
// reward, the question loading with the dictionary expansion, the deal with its planned statuses,
// the promotion at 70 %, the slot reorder at a level change, and Clue_Reveal. The question texts
// are UiState's buffers (expandedText, questionText, answerText); the history, slots, masks and
// clue words are fields of the selected player record (g.rec()). Spec: re/dos/docs/code_ui.md §10.
#include "dos/game/quiz.h"

#include <algorithm>

#include "dos/data/dgroup_offsets.h"
#include "dos/game/game.h"
#include "dos/game/hints.h"
#include "dos/game/hud.h"
#include "dos/game/sound.h"
#include "dos/game/ui.h"
#include "dos/gfx/blitter.h"

namespace tmport::dos {

namespace {

constexpr uint8_t kModeQuiz = 0x02, kModeMenu = 0x40;
constexpr int kCtxPlay = 0, kCtxQuiz = 3;
constexpr int kCmdEsc = 0, kCmdUp = 6, kCmdDown = 7, kCmdEnter = 8;
constexpr int kFirstAnswerZone = 9;     // zones 9..11 of context 3 (DS:80B2): the answer rows
constexpr uint8_t kZoneDisabled = 200;
constexpr uint16_t kAnswered = 0x8000;  // bit 15 of a slot: answered right
constexpr int kSlots = 9;
constexpr uint16_t kExpandedText = 0xA6B8;  // DS offset of UiState::expandedText
constexpr int kMsgPraise = 15, kMsgWrong = 19;  // Flutter_Show texts 15..18 / 19..22
constexpr int kSfxRight = 10, kSfxWrong = 11;

// Question q's 2-bit history status (masks DS:5EAE): 0 never asked, 1 right first time, 2 right
// after a miss, 3 last answer wrong.
int status(const UserRecord& r, unsigned q) {
    const unsigned b = q >> 2;
    if (b > r.quizHistory.size()) return 0;
    return (historyByte(r, static_cast<int>(b)) >> (2 * (q & 3))) & 3;
}

int8_t dsI8(const DosGame& g, int off) {
    if (off < 0 || static_cast<size_t>(off) >= g.tables.dgroup.size()) return 0;
    return static_cast<int8_t>(g.tables.dgroup[static_cast<size_t>(off)]);
}

// The answer index (0 = right) shown on display row `row` in order g_quizOrder (DS:5E9C, read as
// the original indexes it: a focus of -1 reads the byte before the row).
int answerAt(const DosGame& g, int row) { return dsI8(g, dg::kAnswerPerms + 3 * g.ui.quizOrder + row); }

// The answer's bit in a choice mask (DS:5F8A and its copies 5F8D, 5F90: identical tables).
uint8_t answerBit(const DosGame& g, uint16_t table, int answer) { return static_cast<uint8_t>(dsI8(g, table + answer)); }

// DS:5FD2[t] after Quiz_FindTierEnd (word 5 computed, word 6 = 360); past the table: 360.
uint16_t tierStart(const DosGame& g, int t) {
    return t >= 0 && t < QuizTiers::kEntries ? g.quizTiers.start(t) : static_cast<uint16_t>(PuzzleFile::kCount);
}

// The selected record's quiz tier byte (record +0x13F, g_quizTier DS:9101).
int8_t& quizTierByte(DosGame& g) { return reinterpret_cast<int8_t&>(g.rec().quizTier); }

// close() of Quiz_Update (46ce:0080.. / 46ce:0219..): the play screen comes back, the HUD clue box
// redraws, and the reward reaches g_coins (the HUD counts it up).
void closeQuestion(DosGame& g) {
    UiState& ui = g.ui;
    ui.quizClosed = 1;
    ++g.world.fullRedraw;
    ++ui.hudKeyState;
    g.core.uiMode &= static_cast<uint8_t>(~kModeQuiz);
    ui.screen = 0;
    g.input.context = kCtxPlay;
    ui.promptIcon = 4;
}

}  // namespace

// 46ce:0007 Quiz_Update
void quizUpdate(DosGame& g) {
    if (uiFlashPending(g)) return;
    UiState& ui = g.ui;
    CoreState& core = g.core;
    if ((core.uiMode & 0xFD) != 0 || !(core.uiMode & kModeQuiz)) return;
    g.input.context = kCtxQuiz;
    bool reopenMenu = false;
    ui.uiCmd = g.input.cmd;
    g.input.deliverNext = false;
    const int cmd = g.input.cmd;
    if (ui.quizClosed == 0) {
        if (cmd == kCmdEsc) {  // closes, answered or not; an unanswered question stays with the elf
            closeQuestion(g);
            if (ui.quizAnswered == 0) core.caughtSlot = 0;
            if (core.coins != ui.quizCoins) core.coins = ui.quizCoins;
        } else if (ui.quizAnswered == 0) {
            if (cmd == kCmdEnter) {
                const int a = answerAt(g, ui.quizFocus);
                if (g.hasRec() && ui.quizSlot < kSlots)
                    g.rec().answerMasks[ui.quizSlot] &= static_cast<uint8_t>(~answerBit(g, dg::kAnswerBit, a));
                core.dirty = 1;
                ui.quizAnswered = 1;
                ui.promptIcon = 6;
                if (a == 0) quizCorrect(g);
                else quizWrong(g);
                hudRedraw(g);  // shows the new prompt icon
            } else if (cmd > 8 && cmd < 12) {  // the mouse over an answer row
                quizSetFocus(g, cmd - kFirstAnswerZone);
            } else if (cmd > 5 && cmd < 8) {  // Up / Down skip the greyed answers
                int p = ui.quizFocus;
                for (int k = 0; k < 3; ++k) {
                    p += g.tables.ui.quizFocusStep[static_cast<size_t>(cmd - kCmdUp)];
                    if (p == -1) p = 2;
                    else if (p == 3) p = 0;
                    if (answerBit(g, dg::kAnswerBit, answerAt(g, p)) & ui.quizMask) break;
                }
                quizSetFocus(g, p);
            } else if (cmd > 0 && cmd < 4) {  // F1..F3: the menu bar opens over the question
                ui.menuOpen = -1;
                ui.menuItem = 0;
                core.uiMode |= kModeMenu;
                reopenMenu = true;
            }
        } else if (cmd == kCmdEnter || g.mouse.prevButtons != g.mouse.buttons) {  // after the answer: any key or click
            closeQuestion(g);
            if (core.coins != ui.quizCoins) core.coins = ui.quizCoins;
            g.mouse.buttons = 0;
            g.mouse.prevButtons = 0;
        }
    } else {
        // Open the caught question if any slot is still unanswered.
        int s = kSlots - 1;
        while (s >= 0 && g.hasRec() && (g.rec().quizSlots[static_cast<size_t>(s)] & kAnswered)) --s;
        if (s < 0 || !g.hasRec()) {
            core.uiMode &= static_cast<uint8_t>(~kModeQuiz);
            g.input.context = kCtxPlay;
        } else {
            quizPickQuestion(g);
            quizSplitText(g);
            quizSetupAnswers(g);
            ui.hudRedraw = 1;
            ui.screen = 1;  // painter 1: Quiz_DrawScreen
            core.dirty = 1;
            ui.quizClosed = 0;
            ui.quizAnswered = 0;
            ui.promptIcon = 2;
            ui.quizCoins = core.coins;
        }
    }
    if (!reopenMenu) {
        g.input.cmd = -1;
        g.input.deliverNext = true;
    }
}

// 46ce:0271 Quiz_DrawScreen
void quizDrawScreen(DosGame& g) {
    if (g.ui.hudRedraw == 0) return;
    hudRedraw(g);  // full, without the actors (DS:922E = 1)
    quizDrawFrame(g);
    quizDrawQuestion(g);
    quizDrawAnswers(g);
}

// 46ce:028d Quiz_DrawFrame
void quizDrawFrame(DosGame& g) {
    drawBankFrame(g, 0x72, 0, 0, sprite_flags::kMasked);                                   // 57: the panel
    drawBankFrame(g, 0xBC, 0x8E, 0x8E, sprite_flags::kMasked | sprite_flags::kMirror);     // 94: the elf
    drawBankFrame(g, 0xCE, 0x8E, 0x8E, sprite_flags::kMasked | sprite_flags::kMirror);     // 103
}

// 46ce:02d9 Quiz_PickQuestion
int quizPickQuestion(DosGame& g) {
    UiState& ui = g.ui;
    ui.quizSlot = static_cast<uint16_t>(g.core.caughtSlot - 1);
    if (g.hasRec() && ui.quizSlot < kSlots) {
        ui.quizQuestion = g.rec().quizSlots[ui.quizSlot];
        ui.quizMask = g.rec().answerMasks[ui.quizSlot];
        ui.openText = ui.puzzleText[ui.quizSlot];
    }
    const int r = g.rand.rand();
    ui.quizOrder = static_cast<uint16_t>(r % 6);  // one of the 6 answer orders
    return r / 6;
}

// 46ce:0323 Quiz_SplitText
void quizSplitText(DosGame& g) {
    UiState& ui = g.ui;
    ui.answerWidth = 0;
    // The open question in the expanded buffer (read on past a NUL, as the original's scan would:
    // only the last expanded question keeps its NUL).
    const auto& buf = ui.expandedText;
    const size_t start = ui.openText.off >= kExpandedText ? ui.openText.off - kExpandedText : buf.size();
    auto at = [&buf](size_t i) -> char { return i < buf.size() ? buf[i] : '\0'; };
    size_t tilde = start;
    while (tilde < buf.size() && at(tilde) != '~' && at(tilde) != '\0') ++tilde;
    // The question: everything before '~' (with its two pad bytes).
    const size_t qlen = std::min(tilde - std::min(tilde, start), ui.questionText.size() - 1);
    std::copy_n(buf.begin() + static_cast<ptrdiff_t>(std::min(start, buf.size())), qlen, ui.questionText.begin());
    ui.questionText[qlen] = '\0';
    if (at(tilde) != '~') {  // no '~' (none in V 1.0): no answers
        for (auto& a : ui.answerText) a[0] = '\0';
        return;
    }
    // Three answers, each ended by CR or NUL (35-byte buffers; the original has no length check).
    size_t src = tilde + 1, col = 0;
    for (int n = 0; n < 3; ++src) {
        auto& answer = ui.answerText[static_cast<size_t>(n)];
        const char c = at(src);
        if (c == '\r' || c == '\0') {
            answer[col] = '\0';
            col = 0;
            const int w = textWidth(g, std::string_view(answer.data()));
            if (static_cast<int>(ui.answerWidth) < w) ui.answerWidth = static_cast<uint16_t>(w);
            ++n;
        } else if (col + 1 < answer.size()) {
            answer[col++] = c;
        }
    }
}

// 46ce:03ff Quiz_SetupAnswers
void quizSetupAnswers(DosGame& g) {
    UiState& ui = g.ui;
    ui.quizFocus = -1;
    auto& zones = g.inputTables.zones(kCtxQuiz).records;
    for (int i = 0; i < 3; ++i) {
        const auto zi = static_cast<size_t>(kFirstAnswerZone + i);
        if (zi >= zones.size()) break;
        InputZone& z = zones[zi];
        if ((answerBit(g, dg::kAnswerBit2, answerAt(g, i)) & ui.quizMask) == 0) {
            z.x1 = kZoneDisabled;  // an answer already tried: greyed, no zone
        } else {
            z.x1 = 10;
            z.x2 = static_cast<uint8_t>(ui.answerWidth + 12);
            z.y1 = static_cast<uint8_t>(i * 12 + 0x6A);
            z.y2 = static_cast<uint8_t>(z.y1 + 12);
            if (ui.quizFocus == -1) ui.quizFocus = static_cast<int16_t>(i);
        }
    }
}

// 46ce:0482 Quiz_DrawQuestion
void quizDrawQuestion(DosGame& g) {
    g.ui.textInk = 0xFFFF;
    textDraw(g, 10, 0x1C, std::string_view(g.ui.questionText.data()));
}

// 46ce:049e Quiz_DrawAnswers
void quizDrawAnswers(DosGame& g) {
    UiState& ui = g.ui;
    bool focused = false;
    for (int i = 0; i < 3; ++i) {
        const int a = answerAt(g, i);
        ui.textInk = (answerBit(g, dg::kAnswerBit3, a) & ui.quizMask) == 0 ? 0xDDDD : 0xFFFF;  // tried: light grey
        if (a >= 0 && a < 3) textDraw(g, 10, 0x6C + 12 * i, std::string_view(ui.answerText[static_cast<size_t>(a)].data()));
        if (!focused && ui.textInk == 0xFFFF) {
            ui.quizFocus = static_cast<int16_t>(i);
            quizSetFocus(g, i);
            focused = true;
        }
    }
}

// 46ce:052d Quiz_SetFocus
void quizSetFocus(DosGame& g, int i) {
    UiState& ui = g.ui;
    const auto& zones = g.inputTables.zones(kCtxQuiz).records;
    auto zone = [&zones](int row) -> const InputZone* {
        const int k = kFirstAnswerZone + row;  // a focus of -1 reads zone 8
        return k >= 0 && static_cast<size_t>(k) < zones.size() ? &zones[static_cast<size_t>(k)] : nullptr;
    };
    const InputZone* to = zone(i);
    if (!to || to->x1 == kZoneDisabled) return;
    if (const InputZone* from = zone(ui.quizFocus))  // cyan erases the old frame on the panel colour
        gfxFrameRect(g, from->x1 - 1, from->y1, from->x2 - 1, from->y2, 0x7777);
    ui.quizFocus = static_cast<int16_t>(i);
    gfxFrameRect(g, to->x1 - 1, to->y1, to->x2 - 1, to->y2, 0x4444);
    g.core.dirty = 1;
}

// 46ce:05bb Quiz_Correct
void quizCorrect(DosGame& g) {
    UiState& ui = g.ui;
    clueReveal(g);
    if (!g.hasRec() || ui.quizSlot >= kSlots) return;
    UserRecord& r = g.rec();
    r.quizSlots[ui.quizSlot] |= kAnswered;
    ui.textInk = 0xFFFF;
    flutterShow(g, (g.rand.rand() % 4 & 0xFF) + kMsgPraise);
    ui.textInk = 0x4444;
    textDraw(g, 0x3C, 0x5E, g.dsString(ui.praiseWord.off));  // the revealed clue word, in red
    ui.textInk = 0xFFFF;
    const uint8_t mask = r.answerMasks[ui.quizSlot];  // after the pick: 6 = right first time
    ui.quizCoins = static_cast<uint16_t>(ui.quizCoins + dsI8(g, dg::kQuizReward + static_cast<int8_t>(mask)));
    sfxPlay(g, kSfxRight);
    const unsigned q = ui.quizQuestion, b = q >> 2;
    if (b <= r.quizHistory.size()) {
        const unsigned shift = 2 * (q % 4);
        uint8_t h = historyByte(r, static_cast<int>(b));
        h = static_cast<uint8_t>(h & ~g.tables.ui.historyMasks[q % 4]);
        h = static_cast<uint8_t>(h | ((mask == 6 ? 1 : 2) << shift));
        setHistoryByte(r, static_cast<int>(b), h);
    }
}

// 46ce:069e Quiz_Wrong
void quizWrong(DosGame& g) {
    UiState& ui = g.ui;
    g.core.caughtSlot = 0;  // the elf keeps the question
    ui.textInk = 0xFFFF;
    flutterShow(g, g.rand.rand() % 4 + kMsgWrong);
    sfxPlay(g, kSfxWrong);
    if (!g.hasRec()) return;
    UserRecord& r = g.rec();
    const unsigned q = ui.quizQuestion, b = q >> 2;
    if (b <= r.quizHistory.size()) {
        const auto h = static_cast<uint8_t>(historyByte(r, static_cast<int>(b)) | g.tables.ui.historyMasks[q % 4]);
        setHistoryByte(r, static_cast<int>(b), h);
    }
}

// 46ce:06f0 Clue_Reveal
void clueReveal(DosGame& g) {
    const unsigned base = g.core.level * 3u;
    const int r = g.rand.rand();
    if (!g.hasRec()) return;
    auto& clue = g.rec().clueWords;
    const auto& order = g.tables.elves.clueRevealOrders[static_cast<size_t>(r % 6)];
    // The first still hidden word in the order DS:5FBC[r % 6] is revealed; if all three are known
    // the praise word is the order's last.
    unsigned k = order[2];
    for (int j = 0; j < 3; ++j) {
        const unsigned c = base + order[static_cast<size_t>(j)];
        if (c < clue.size() && clue[c] > 0x7F) {
            clue[c] &= 0x7F;
            k = order[static_cast<size_t>(j)];
            break;
        }
    }
    const unsigned c = base + k;
    if (c < clue.size() && clue[c] < g.tables.ui.clueWordText.size())
        g.ui.praiseWord = DsPtr{g.tables.ui.clueWordText[clue[c]]};
}

// 46ce:08f1 Dict_Expand
int dictExpand(DosGame& g, std::span<const uint8_t> src, size_t dstOffset) {
    auto& dst = g.ui.expandedText;
    size_t o = dstOffset;
    auto put = [&dst, &o](char c) {
        if (o + 1 < dst.size()) dst[o] = c;  // (the 9 expanded questions fit the buffer in V 1.0)
        ++o;
    };
    for (const uint8_t b : src) {
        if (b == 0) break;
        if (b < 0x80) {
            put(static_cast<char>(b));
        } else {
            for (const char c : g.dictionary.word(b & 0x7F)) put(c);
        }
    }
    if (o < dst.size()) dst[o] = '\0';
    else if (!dst.empty()) dst.back() = '\0';
    return static_cast<int>(o - dstOffset);
}

// 46ce:095f Quiz_LoadDictionary: PRG_DICT is DosGame::dictionary (read at start-up).
int quizLoadDictionary(DosGame& g) {
    g.ui.quizFileLength = static_cast<uint16_t>(g.files.files().bytes(FileIndex::PrgDict).size());
    g.back.dirty().clear();
    return 1;
}

// 46ce:09ab Quiz_LoadQuestions
int quizLoadQuestions(DosGame& g, bool resume) {
    UiState& ui = g.ui;
    if (g.core.startDrive < 2) {  // floppies only
        msgBoxDraw(g, 22);
        presentDirty(g.back, g.screen);
        ++g.screenVersion;
    }
    g.world.loadedScene = 0xFF;
    if (!g.hasRec()) return 0;
    ui.quizFileLength = static_cast<uint16_t>(g.puzzles.fileSize());
    quizFindTierEnd(g);
    int8_t& tier = quizTierByte(g);
    ui.tierFirst = tierStart(g, tier);
    ui.tierEnd = tierStart(g, tier + 1);
    if (!resume) {
        if (quizTierHasWork(g) == 0) {  // >= 70 % right first time: the next tier
            ++tier;
            if (tier > 3) {
                tier = 5;
                quizResetHistory(g);  // still with the old start (303)
            }
            ui.tierFirst = tierStart(g, tier);
            ui.tierEnd = tierStart(g, tier + 1);
            quizTierHasWork(g);  // recount for the new range
        }
        quizDeal(g);
    }
    // The 9 dealt questions expanded back to back (each overwrites the previous one's NUL). The
    // offset lookup doubles the slot value, which drops bit 15 of an answered slot.
    size_t p = 0;
    for (size_t s = 0; s < kSlots; ++s) {
        const int q = g.rec().quizSlots[s] & 0x7FFF;
        ui.puzzleText[s] = DsPtr{static_cast<uint16_t>(kExpandedText + p)};
        const std::span<const uint8_t> text = q < PuzzleFile::kCount ? g.puzzles.raw(q) : std::span<const uint8_t>{};
        p += static_cast<size_t>(dictExpand(g, text, p));
    }
    return 1;
}

// 46ce:0b6a Quiz_Deal
void quizDeal(DosGame& g) {
    UiState& ui = g.ui;
    UserRecord& r = g.rec();
    quizPlanStatuses(g);
    for (int s = kSlots - 1; s >= 0; --s) {
        const int c = static_cast<int8_t>(ui.dealPlan[static_cast<size_t>(s)]) & 3;
        const int pool = ui.statusCount[static_cast<size_t>(c)];  // the full pool, not the plan's copy
        if (pool == 0) continue;  // (the plan never asks for an empty status)
        unsigned q = ui.tierFirst;
        for (bool dealt = true; dealt;) {  // one rand() per attempt, again while the question is dealt
            const int want = g.rand.rand() % pool;
            int k = 0;
            for (q = ui.tierFirst; q < ui.tierEnd; ++q)
                if (status(r, q) == c && k++ == want) break;
            dealt = std::find(r.quizSlots.begin(), r.quizSlots.end(), static_cast<uint16_t>(q)) != r.quizSlots.end();
        }
        r.quizSlots[static_cast<size_t>(s)] = static_cast<uint16_t>(q);
    }
}

// 46ce:0c39 Quiz_PlanStatuses
void quizPlanStatuses(DosGame& g) {
    UiState& ui = g.ui;
    const UiTables& t = g.tables.ui;
    ui.dealPlan = t.dealTemplate;  // {0, 3, 2} three times
    ui.dealAvail = ui.statusCount;
    int i = 0, fallbacks = 0;
    while (i < kSlots) {
        const int c = ui.dealPlan[static_cast<size_t>(i)] & 3;
        if (ui.dealAvail[static_cast<size_t>(c)] != 0) {
            --ui.dealAvail[static_cast<size_t>(c)];
            ++i;
            fallbacks = 0;
            continue;
        }
        // Exhausted: the status and the later ones like it fall back (0 -> 1, 1 -> 0, 2 -> 0,
        // 3 -> 2) and position i is examined again. Deviation (documented guard): with statuses 0
        // and 1 both exhausted the original loops forever (0 -> 1 -> 0); after more fallbacks than
        // the longest chain (3 -> 2 -> 0 -> 1) the first status with questions left is taken.
        uint8_t repl = t.dealFallback[t.dealFallbackIndex[static_cast<size_t>(c)] % t.dealFallback.size()];
        if (++fallbacks > 3) {
            const auto it = std::find_if(ui.dealAvail.begin(), ui.dealAvail.end(), [](uint16_t n) { return n != 0; });
            if (it == ui.dealAvail.end()) break;  // fewer than 9 questions in the tier (none in V 1.0)
            repl = static_cast<uint8_t>(it - ui.dealAvail.begin());
        }
        for (int j = i; j < kSlots; ++j)
            if (ui.dealPlan[static_cast<size_t>(j)] == c) ui.dealPlan[static_cast<size_t>(j)] = repl;
    }
}

// 46ce:0cd0 Quiz_TierHasWork
int quizTierHasWork(DosGame& g) {
    UiState& ui = g.ui;
    const UserRecord& r = g.rec();
    ui.statusCount.fill(0);
    for (unsigned q = ui.tierFirst; q < ui.tierEnd; ++q) ++ui.statusCount[static_cast<size_t>(status(r, q))];
    const auto span = static_cast<uint16_t>(ui.tierEnd - ui.tierFirst);
    if (span == 0) return 0;  // the original would divide by zero; no tier is empty in V 1.0
    const unsigned percent = static_cast<uint16_t>(ui.statusCount[1] * 100u) / span;  // 16-bit product
    return percent < 70 ? 1 : 0;
}

// 46ce:0d60 Quiz_ResetHistory
void quizResetHistory(DosGame& g) {
    UserRecord& r = g.rec();
    const unsigned first = g.ui.tierFirst, b = first >> 2;
    if (b >= r.quizHistory.size()) return;
    // The mask keeps the bits of the questions from the start on (the inverse of "keep the ones
    // below"), and the zeroing runs one byte past the history into slot 0's low byte (record
    // +0x66), which Quiz_Deal overwrites right after.
    r.quizHistory[b] &= g.tables.ui.historyResetMasks[first & 3];
    for (unsigned i = b + 1; i <= r.quizHistory.size(); ++i) setHistoryByte(r, static_cast<int>(i), 0);
}

// 46ce:0d9a Quiz_FindTierEnd: word 5 of the tier table (the first question past one 0x7B00-byte
// read from the file start) and word 6 = 360 are DosGame::quizTiers, computed when the files load.
void quizFindTierEnd(DosGame&) {}

// 46ce:0df2 Quiz_ReorderSlots
int quizReorderSlots(DosGame& g) {
    int carriers = 0;
    if (g.core.uiMode != 0 || !g.hasRec()) return carriers;
    UiState& ui = g.ui;
    UserRecord& r = g.rec();
    const unsigned base = g.core.level * 3u;
    for (unsigned k = 0; k < 3; ++k)  // the level's hidden clue words: one carrier elf each
        if (base + k < r.clueWords.size() && r.clueWords[base + k] > 0x7F) ++carriers;
    constexpr uint8_t kUnset = 0xFF;
    std::array<uint8_t, kSlots> order;
    order.fill(kUnset);
    auto mask = [&r](int s) { return r.answerMasks[static_cast<size_t>(s)]; };
    auto place = [&order](int at, int s) { order[static_cast<size_t>(at)] = static_cast<uint8_t>(s); };
    int front = 0;
    for (int s = 0; s < kSlots && front < 3; ++s)  // missed, not yet answered right: first
        if ((mask(s) & 1) && (mask(s) & 6) != 6) place(front++, s);
    int back = kSlots - 1;
    for (int s = 0; s < kSlots && front < kSlots; ++s) {
        if ((mask(s) & 7) == 7) place(front++, s);           // unasked
        else if ((mask(s) & 1) == 0) place(back--, s);       // answered right: last
    }
    // Deviation (documented): a 4th missed slot is placed nowhere and leaves an entry of the
    // original's list unset (stack garbage); the engine fills such entries with the slots left out.
    for (int s = 0; s < kSlots; ++s) {
        if (std::find(order.begin(), order.end(), static_cast<uint8_t>(s)) != order.end()) continue;
        const auto hole = std::find(order.begin(), order.end(), kUnset);
        if (hole != order.end()) *hole = static_cast<uint8_t>(s);
    }
    const auto slots = r.quizSlots;
    const auto masks = r.answerMasks;
    const auto texts = ui.puzzleText;
    for (size_t i = 0; i < kSlots; ++i) {
        const size_t from = order[i] % kSlots;
        r.quizSlots[i] = slots[from];
        r.answerMasks[i] = masks[from];
        ui.puzzleText[i] = texts[from];
    }
    ui.hudKeyState = static_cast<uint16_t>(g.core.keyState + 1);  // forces the clue box redraw
    return carriers;
}

}  // namespace tmport::dos
