#include "game/scenes/quiz_screen.h"

#include <algorithm>
#include <format>
#include <span>

#include "audio/audio_tables.h"
#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "game/rules.h"
#include "input/input_codes.h"
#include "platform/engine.h"
#include "res/resource_manager.h"
#include "text/text_renderer.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::game::scenes {

namespace {

std::span<const uint8_t> bytesOf(std::string_view s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

bool isPress(uint16_t code) { return (code & (input::kLeftDownFlag | input::kRightDownFlag)) != 0; }

}  // namespace

// 1150:0a97, layout part.
QuizLayout layoutQuestion(const ParsedQuestion& pq, const QuizSlot& slot, text::TextRenderer& text, int speakerW,
                          int speakerH) {
    QuizLayout L;
    const int lh = text.font().height();
    L.lineHeight = lh;
    L.numLines = pq.numLines;
    L.ansH = lh + 6;
    L.textY = (gfx::FrameBuffer::kPlayHeight - (pq.lineSlots * lh + (L.ansH - 2) * 3)) / 2 + gfx::FrameBuffer::kPlayTop;
    const int qw = text.width(bytesOf(pq.question()));
    L.textW = qw + 6;
    if (L.textW & 1) L.textW = qw + 7;
    L.textX = (512 - L.textW) / 2;
    if (L.textX & 1) L.textX -= 1;
    // g_quizLineY: one entry per non-blank line slot among the first 10 (1150:0901).
    for (int i = 0, k = 0; i < 10; ++i)
        if (!pq.blank[static_cast<size_t>(i)] && k < 10) L.lineY[static_cast<size_t>(k++)] = i * lh;
    int maxW = 0, xOfMax = 0;
    for (int pos = 0; pos < 3; ++pos) {
        const uint8_t a = slot.perm[static_cast<size_t>(pos)];
        const int w = a < 3 ? text.width(bytesOf(pq.answer(a))) : 0;
        const int x = (512 - w) / 2;
        if (w > maxW) {
            maxW = w;
            xOfMax = x;
        }
        const int y = pq.lineSlots * lh + L.textY + (L.ansH - 2) * pos;
        L.ansY[static_cast<size_t>(pos)] = y - 3;
        L.ansTextX[static_cast<size_t>(pos)] = x;
        L.ansTextY[static_cast<size_t>(pos)] = y;
        L.ansTextW[static_cast<size_t>(pos)] = w;
    }
    L.ansW = maxW + 8;
    L.ansX = xOfMax - 4;
    L.speaker = {quiz::kSpeakerRectX, quiz::kSpeakerRectY, speakerW + 8, speakerH + 6};
    return L;
}

namespace {

class QuizScreen {
public:
    QuizScreen(AppContext& ctx, GameState& s, const GameTables& t, int slot, const SceneHooks& hooks)
        : io_(ctx, hooks), s_(s), t_(t), slotIndex_(slot) {}

    QuizResult run();

private:
    QuizSlot& slot() { return s_.quiz.slots[static_cast<size_t>(slotIndex_)]; }
    bool tried(int pos) { return slot().tried[static_cast<size_t>(pos)] != 0; }

    void drawScreen();                        // 1150:0a97 Quiz_DrawQuestionScreen
    void highlight(int item, bool frame);     // 1150:0d2d Quiz_HighlightItem
    void unhighlight(int item);               // 1150:0e30 Quiz_UnhighlightItem
    void speakerFocus(bool focused);          // 1150:0ed6 Quiz_DrawSpeakerFocus
    int nextItem(int i) { return quizNextItem(i, slot(), voice_, layout_.numLines); }  // 1150:0f85
    int prevItem(int i) { return quizPrevItem(i, slot(), voice_, layout_.numLines); }  // 1150:0fcb
    void speakLine(int k);                    // 1150:1409 Quiz_SpeakLine
    void speakAnswer(int pos);                // 1150:14d7 Quiz_SpeakAnswer
    void speakAll();                          // 1150:1449 Quiz_SpeakAll
    void showCorrect();                       // 1150:049a Quiz_ShowCorrectScreen
    void showWrong();                         // 1150:0748 Quiz_ShowWrongScreen
    void waitContinue();                      // the CONTINUE wait of both result screens
    void drawResultBase();                    // speaker icon + bubble

    SceneIo io_;
    GameState& s_;
    const GameTables& t_;
    int slotIndex_;
    int q_ = -1;                 // g_quizQ
    bool voice_ = false;         // g_quizVoice (copy of g_voiceOn at the start)
    bool noAnswerVoice_ = false; // g_quizNoAnswerVoice
    bool interrupted_ = false;   // g_quizInterrupted
    ParsedQuestion pq_;
    QuizLayout layout_;
    std::shared_ptr<const gfx::PixMap> speakerIcon_;
    int messageIndex_ = -1;
    int clueIndex_ = -1;
};

void QuizScreen::drawScreen() {
    gfx::FrameBuffer& fb = io_.fb();
    fb.drawOpaqueOrFlippedWhiteKey(0, gfx::FrameBuffer::kPlayTop, *io_.pixmap(quiz::kScrollPmap), false);
    layout_ = layoutQuestion(pq_, slot(), io_.text(), speakerIcon_->width, speakerIcon_->height);
    fb.setForeColor(0);
    io_.text().drawBox(layout_.textX, layout_.textY, layout_.textW, bytesOf(pq_.question()));
    for (int pos = 0; pos < 3; ++pos) {
        const uint8_t a = slot().perm[static_cast<size_t>(pos)];
        fb.setForeColor(tried(pos) ? quiz::kTriedColour : quiz::kAnswerColour);
        if (a < 3)
            io_.text().draw(layout_.ansTextX[static_cast<size_t>(pos)], layout_.ansTextY[static_cast<size_t>(pos)],
                            bytesOf(pq_.answer(a)));
    }
    HotspotTable& hs = io_.engine().hotspots();
    hs.set(quizhot::kPlayArea, 0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight);
    for (int pos = 0; pos < 3; ++pos)
        if (!tried(pos)) {
            const gfx::Rect r = layout_.answerBox(pos);
            hs.set(static_cast<uint16_t>(quizhot::kAnswer0 + pos), r.x, r.y, r.w, r.h);
        }
    if (voice_)
        for (int k = 0; k < layout_.numLines; ++k) {
            const gfx::Rect r = layout_.lineHotspot(k);
            hs.set(static_cast<uint16_t>(quizhot::kLine0 + k), r.x, r.y, r.w, r.h);
        }
    // The clean screen (without the speaker button) goes to the back page.
    fb.copyRect(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight, 0,
                gfx::FrameBuffer::kPlayTop + gfx::FrameBuffer::kLayer1Y);
    if (voice_) {
        fb.drawSprite(quiz::kSpeakerX, quiz::kSpeakerY, *speakerIcon_, false);
        const gfx::Rect& r = layout_.speaker;
        hs.set(quizhot::kSpeaker, r.x, r.y, r.w, r.h);
    }
}

void QuizScreen::highlight(int item, bool frame) {
    gfx::FrameBuffer& fb = io_.fb();
    if (item < 10) {
        if (!frame) {
            const gfx::Rect r = layout_.answerHighlight(item);
            io_.highlightRect(r.x, r.y, r.w, r.h);
            io_.present(r.x, r.y, r.w, r.h);
        } else {
            gfx::Rect r = layout_.answerBox(item);
            const gfx::Rect box = r;
            fb.setForeColor(quiz::kFocusColour);
            for (int i = 0; i < 2; ++i) {
                fb.frameRect(r.x, r.y, r.w, r.h);
                r = {r.x + 1, r.y + 1, r.w - 2, r.h - 2};
            }
            io_.present(box.x, box.y, box.w, box.h);
        }
    } else {
        const gfx::Rect r = layout_.lineHighlight(item - 10);
        io_.highlightRect(r.x, r.y, r.w, r.h);
        io_.present(r.x, r.y, r.w, r.h);
    }
}

void QuizScreen::unhighlight(int item) {
    const gfx::Rect r = item < 10 ? layout_.answerBox(item) : layout_.lineHighlight(item - 10);
    io_.fb().copyRect(r.x, r.y + gfx::FrameBuffer::kLayer1Y, r.w, r.h, r.x, r.y);
    io_.present(r.x, r.y, r.w, r.h);
}

void QuizScreen::speakerFocus(bool focused) {
    gfx::FrameBuffer& fb = io_.fb();
    const gfx::Rect& s = layout_.speaker;
    if (!focused) {
        fb.copyRect(s.x, s.y + gfx::FrameBuffer::kLayer1Y, s.w, s.h, s.x, s.y);
    } else {
        fb.setForeColor(quiz::kFocusColour);
        gfx::Rect r = s;
        for (int i = 0; i < 2; ++i) {
            fb.frameRect(r.x, r.y, r.w, r.h);
            r = {r.x + 1, r.y + 1, r.w - 2, r.h - 2};
        }
    }
    fb.drawSprite(quiz::kSpeakerX, quiz::kSpeakerY, *speakerIcon_, false);
    io_.present(s.x, s.y, s.w, s.h);
}

void QuizScreen::speakLine(int k) {
    if (!io_.voiceOn()) return;
    interrupted_ = io_.playUntilEvent(quizLineVoice(t_.quiz, q_, k), audio::StopList::QuizLine);
}

void QuizScreen::speakAnswer(int pos) {
    if (noAnswerVoice_) return;  // g_quizInterrupted keeps its value
    const int id = quizAnswerVoice(t_.quiz, q_, layout_.numLines, slot().perm[static_cast<size_t>(pos)]);
    if (id < 0) return;
    interrupted_ = io_.playUntilEvent(id, audio::StopList::QuizAnswer);
}

void QuizScreen::speakAll() {
    if (!io_.voiceOn()) return;
    io_.trace("read all");
    for (int k = 0; k < layout_.numLines; ++k) {
        highlight(10 + k, false);
        speakLine(k);
        unhighlight(10 + k);
        if (interrupted_) return;
    }
    io_.delayMs(quiz::kLinePauseMs);
    for (int pos = 0; pos < 3; ++pos) {
        if (tried(pos)) continue;
        io_.delayMs(quiz::kAnswerPauseMs);
        highlight(pos, false);
        speakAnswer(pos);
        unhighlight(pos);
        if (interrupted_) return;
    }
}

void QuizScreen::drawResultBase() {
    gfx::FrameBuffer& fb = io_.fb();
    fb.drawSprite(quiz::kSpeakerX, quiz::kSpeakerY, *speakerIcon_, false);
    fb.drawSprite(quiz::kBubbleX, quiz::kBubbleY, *io_.pixmap(quiz::kBubblePmap), false);
}

void QuizScreen::waitContinue() {
    io_.setClueBoxMode(2);
    for (;;) {
        const uint16_t c = io_.waitCode();
        if (c == input::kEnter || c == input::kEsc || c == input::kSpace || c == 0x409 || c == 0x2409) break;
    }
}

void QuizScreen::showCorrect() {
    io_.setCursorForced(true);
    io_.sfxPlay(quiz::kSfxPraise);
    drawResultBase();
    gfx::FrameBuffer& fb = io_.fb();
    const int m = io_.rng().random(4);
    messageIndex_ = m;
    fb.setForeColor(0);
    io_.text().drawBox(quiz::kResultTextX, quiz::kResultTextY, quiz::kResultTextW,
                       io_.exe().farCstr(dg::kQuizPraiseText + 4 * static_cast<uint32_t>(m)));
    const int level = s_.currentLevel();
    const int k = s_.inv.numClues;
    clueIndex_ = k;
    std::string_view word;
    uint16_t clueVoice = 0;
    if (level >= 0 && level < kLevelCount && k >= 0 && k < 3) {
        word = s_.clues.text[static_cast<size_t>(level)][static_cast<size_t>(k)];
        clueVoice = s_.clues.voice[static_cast<size_t>(level)][static_cast<size_t>(k)];
    }
    fb.setForeColor(quiz::kFocusColour);
    const int w = io_.text().width(bytesOf(word));
    const int x = quiz::kClueCentreX - w / 2;
    const int y = quiz::kResultTextY + io_.lineHeight() * ((m == 2) + 2);
    io_.text().draw(x, y, bytesOf(word));
    fb.setForeColor(0);
    io_.text().draw(x + w, y, io_.exe().cstr(dg::kStrQuizPeriod));
    io_.present(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight);
    io_.waitWhilePlaying();
    bool intr = false;
    if (io_.voiceOn()) {
        HotspotTable& hs = io_.engine().hotspots();
        hs.set(quizhot::kAnyClick, 0, 0, 512, 384);
        // 1150:06ac Quiz_NarratePraise(bank, m, clue word)
        const int base = quizBankBase(t_.quiz.voiceBank[static_cast<size_t>(q_)]);
        if (base >= 0) {
            intr = io_.playUntilEvent(base + m, audio::StopList::Praise);
            if (!intr) intr = io_.playUntilEvent(base + clueVoice, audio::StopList::Praise);
        }
        io_.flushInput();
        hs.remove(quizhot::kAnyClick);
    }
    if (!intr) waitContinue();
    io_.setCursorForced(true);
}

void QuizScreen::showWrong() {
    const int m = io_.rng().random(4);
    messageIndex_ = m;
    io_.setCursorForced(true);
    io_.sfxPlay(quiz::kSfxSorry);
    drawResultBase();
    io_.fb().setForeColor(0);
    io_.text().drawBox(quiz::kResultTextX, quiz::kResultTextY, quiz::kResultTextW,
                       io_.exe().farCstr(dg::kQuizSorryText + 4 * static_cast<uint32_t>(m)));
    io_.present(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight);
    io_.waitWhilePlaying();
    bool intr = false;
    if (io_.voiceOn()) {
        HotspotTable& hs = io_.engine().hotspots();
        hs.set(quizhot::kAnyClick, 0, 0, 512, 384);
        // 1150:0891 Quiz_NarrateSorry(bank, m)
        const int base = quizBankBase(t_.quiz.voiceBank[static_cast<size_t>(q_)]);
        if (base >= 0) intr = io_.playUntilEvent(base + 4 + m, audio::StopList::WrongAnswer);
        io_.flushInput();
        hs.remove(quizhot::kAnyClick);
    }
    if (!intr) waitContinue();
    io_.setCursorForced(true);
}

QuizResult QuizScreen::run() {
    QuizResult out;
    if (slotIndex_ < 0 || slotIndex_ >= kQuizSlotCount || slot().q < 0 || slot().q >= kQuestionCount) {
        log::warn("Quiz_AskQuestion: slot {} holds no question", slotIndex_);
        return out;
    }
    // Setup (1150:100a).
    q_ = slot().q;
    voice_ = io_.voiceOn();
    io_.musicHalt();
    noAnswerVoice_ = isSilentAnswerQuestion(q_, t_.quiz);
    gfx::FrameBuffer& fb = io_.fb();
    const bool savedDirty = fb.dirtyEnabled();
    fb.setDirtyEnabled(false);
    MenuDisable menu(io_.ctx());
    io_.setCursorForced(true);
    speakerIcon_ = io_.pixmap(quiz::kSpeakerPmap);
    {
        auto raw = io_.res().loadRaw(kQuestionFirstId + q_);
        auto parsed = raw ? parseQuestion(*raw) : std::nullopt;
        if (!parsed) throw Error(std::format("OTHR {} is missing or has no question terminator", kQuestionFirstId + q_));
        pq_ = std::move(*parsed);
    }
    io_.tracef("question {} slot {} voice {}", q_, slotIndex_, voice_ ? 1 : 0);
    io_.setClueBoxMode(1);
    drawScreen();
    int sel = nextItem(-1);
    highlight(sel, true);
    io_.present(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight);
    io_.flushInput();

    bool spk = false, chosen = false, done = false;
    while (!done) {
        const uint16_t code = io_.waitCode();
        const bool oldSpk = spk;
        int newSel = sel;
        const uint16_t h = code & 0x0FFF;
        switch (code) {  // key table at 1150:139e
            case input::kEnter:
                if (spk)
                    speakAll();
                else if (sel >= 10)
                    speakLine(sel - 10);
                else
                    chosen = done = true;
                break;
            case input::kEsc: done = true; break;
            case input::kLeft:
            case input::kRight:
            case input::kLeft | input::kRightUpFlag:
            case input::kRight | input::kRightUpFlag:
                if (voice_) spk = !spk;
                break;
            case input::kUp:
            case input::kUp | input::kRightUpFlag:
                if (!spk) newSel = prevItem(sel);
                break;
            case input::kDown:
            case input::kDown | input::kRightUpFlag:
                if (!spk) newSel = nextItem(sel);
                break;
            default:
                if (h >= quizhot::kAnswer0 && h <= quizhot::kAnswer0 + 2) {
                    if (isPress(code)) {
                        spk = false;
                        newSel = h - quizhot::kAnswer0;
                    } else if (h - quizhot::kAnswer0 == sel && !spk) {
                        chosen = done = true;
                    }
                } else if (h >= quizhot::kLine0 && h < quizhot::kLine0 + layout_.numLines) {
                    if (isPress(code)) {
                        spk = false;
                        newSel = h - quizhot::kLine0 + 10;
                    } else if (h - quizhot::kLine0 + 10 == sel && !spk) {
                        speakLine(sel - 10);
                    }
                } else if (h == quizhot::kSpeaker) {
                    if (isPress(code))
                        spk = true;
                    else if (spk)
                        speakAll();
                }
                break;
        }
        // Redraw (1150:1273-12be).
        if (spk == oldSpk) {
            if (newSel != sel) {
                unhighlight(sel);
                highlight(newSel, true);
            }
        } else if (!spk) {
            highlight(newSel, true);
            speakerFocus(false);
        } else {
            unhighlight(newSel);
            speakerFocus(true);
            newSel = nextItem(-1);
        }
        sel = newSel;
    }
    io_.setCursorForced(true);
    if (chosen) {
        // 1150:0403 Quiz_JudgeAnswer: state, result screen (its R(4)), then the clue word; the
        // coins follow in Quiz_AskQuestion.
        io_.setCursorForced(true);
        const QuizJudgement j = quizJudgeAnswer(s_.quiz, slotIndex_, sel);
        out.answered = true;
        out.position = sel;
        out.correct = j.correct;
        io_.tracef("answer position {} {}", sel, j.correct ? "correct" : "wrong");
        if (j.correct) {
            showCorrect();
            out.coins = finishCorrectAnswer(s_, slotIndex_, t_, io_.gameHooks());
            out.result = 1;
        } else {
            showWrong();
        }
        out.messageIndex = messageIndex_;
        out.clueIndex = j.correct ? clueIndex_ : -1;
    } else {
        io_.trace("question left with Esc");
    }
    // Cleanup.
    HotspotTable& hs = io_.engine().hotspots();
    for (int i = -1; i < 3; ++i) hs.remove(static_cast<uint16_t>(quizhot::kAnswer0 + i));
    if (voice_) {
        for (int k = 0; k < layout_.numLines; ++k) hs.remove(static_cast<uint16_t>(quizhot::kLine0 + k));
        hs.remove(quizhot::kSpeaker);
    }
    menu.restore();
    io_.flushInput();
    fb.setDirtyEnabled(savedDirty);
    io_.musicResume();
    io_.setClueBoxMode(0);
    io_.setCursorForced(false);
    return out;
}

}  // namespace

QuizResult askQuestion(AppContext& ctx, GameState& s, const GameTables& t, int slot, const SceneHooks& hooks) {
    QuizScreen screen(ctx, s, t, slot, hooks);
    return screen.run();
}

}  // namespace tmport::game::scenes
