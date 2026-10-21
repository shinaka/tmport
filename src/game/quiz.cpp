#include "game/quiz.h"

#include <algorithm>

#include "mischief/util/log.h"

namespace tmport::game {

namespace {

constexpr uint8_t kCR = 0x0D;
constexpr uint8_t kLF = 0x0A;
constexpr uint8_t kEscape = 0xFF;

bool isBreak(uint8_t c) { return c == kCR || c == kLF; }

int nextPool(int status) { return status == 3 ? 0 : status + 1; }  // 0->1->2->3->0

}  // namespace

int questionTier(int q, const QuizTables& t) {
    for (int tier = 0; tier < kQuizTierCount - 1; ++tier)
        if (q >= t.tierStart[static_cast<size_t>(tier)] && q < t.tierStart[static_cast<size_t>(tier) + 1]) return tier;
    return -1;
}

bool isSilentAnswerQuestion(int q, const QuizTables& t) {
    return std::find(t.silentAnswers.begin(), t.silentAnswers.end(), q) != t.silentAnswers.end();
}

// 1150:0000 Quiz_SetTierRange
void quizSetTierRange(QuizState& s, const QuizTables& t) {
    if (s.tier < 4) {
        s.rangeLo = t.tierStart[s.tier];
        s.rangeHi = static_cast<int16_t>(t.tierStart[s.tier + 1u] - 1);
    } else {
        s.rangeLo = 0;
        s.rangeHi = kQuestionCount - 1;
    }
}

// 1150:0057 Quiz_CountStatusInRange
void quizCountStatusInRange(QuizState& s) {
    s.count.fill(0);
    for (int q = std::max<int>(s.rangeLo, 0); q <= s.rangeHi && q < kQuestionCount; ++q) {
        const uint8_t v = s.status[static_cast<size_t>(q)];
        if (v < 4) ++s.count[v];
    }
}

// 1150:00a7 Quiz_PickByStatus (the recursion on an exhausted pool becomes the outer loop)
void quizPickByStatus(QuizState& s, int status, int n, std::span<int16_t> out, mischief::BorlandRand& rng) {
    n = std::min<int>(n, static_cast<int>(out.size()));
    int emptyPools = 0;
    while (n > 0) {
        int16_t& poolCount = s.count[static_cast<size_t>(status)];
        int avail = poolCount;
        poolCount = static_cast<int16_t>(poolCount < n ? 0 : poolCount - n);
        int picked = 0;
        while (n > 0 && avail > 0) {
            const int r = rng.random(avail);
            // The (r+1)-th question from rangeLo whose history byte equals `status` exactly
            // (dealt questions carry bit 7 and are skipped).
            int q = std::max<int>(s.rangeLo, 0);
            int seen = 0;
            while (seen <= r && q < kQuestionCount) {
                if (s.status[static_cast<size_t>(q)] == status) ++seen;
                ++q;
            }
            --q;
            out[static_cast<size_t>(--n)] = static_cast<int16_t>(q);
            s.status[static_cast<size_t>(q)] |= kQuizDealtBit;
            --avail;
            ++picked;
        }
        if (n <= 0) break;
        // All four pools empty: the original would recurse for ever (cannot happen with the
        // shipped tier ranges of 57+ questions).
        emptyPools = picked ? 0 : emptyPools + 1;
        if (emptyPools >= 4) break;
        status = nextPool(status);
    }
}

// 1150:01d5 Quiz_FillSlots
void quizFillSlots(QuizState& s, mischief::BorlandRand& rng) {
    std::array<int16_t, kQuizSlotCount> picks;
    picks.fill(kQuizNoQuestion);
    quizPickByStatus(s, kQuizNeverAnswered, 3, std::span(picks).subspan(0, 3), rng);
    quizPickByStatus(s, kQuizMissed, 3, std::span(picks).subspan(3, 3), rng);
    quizPickByStatus(s, kQuizRightAfterMiss, 3, std::span(picks).subspan(6, 3), rng);

    for (QuizSlot& slot : s.slots) slot = QuizSlot{};

    // Random slot for each pick: the (r+1)-th still empty slot.
    for (int i = kQuizSlotCount - 1; i >= 0; --i) {
        const int r = rng.random(i + 1);
        int idx = 0, seen = 0;
        while (seen <= r && idx < kQuizSlotCount) {
            if (s.slots[static_cast<size_t>(idx)].q == kQuizNoQuestion) ++seen;
            ++idx;
        }
        s.slots[static_cast<size_t>(idx - 1)].q = picks[static_cast<size_t>(i)];
    }
    // Random answer order per slot: answer k goes to the (r+1)-th free display position.
    for (QuizSlot& slot : s.slots) {
        for (int k = 2; k >= 0; --k) {
            const int r = rng.random(k + 1);
            int pos = 0, seen = 0;
            while (seen <= r && pos < 3) {
                if (slot.perm[static_cast<size_t>(pos)] == 0xFF) ++seen;
                ++pos;
            }
            slot.perm[static_cast<size_t>(pos - 1)] = static_cast<uint8_t>(k);
        }
    }
}

// 1150:0327 Quiz_NewRound
bool quizNewRound(QuizState& s, const QuizTables& t, mischief::BorlandRand& rng) {
    quizSetTierRange(s, t);
    for (uint8_t& v : s.status) v &= static_cast<uint8_t>(~kQuizDealtBit);
    quizCountStatusInRange(s);
    bool promoted = false;
    if (s.tier < 4) {
        // 16-bit arithmetic as compiled: total*80 <= count1*100.
        const int16_t total = static_cast<int16_t>(s.count[0] + s.count[1] + s.count[2] + s.count[3]);
        const int16_t a = static_cast<int16_t>(total * 80);
        const int16_t b = static_cast<int16_t>(s.count[1] * 100);
        if (static_cast<int16_t>(a - b) == 0 || a < b) {
            ++s.tier;
            quizSetTierRange(s, t);
            quizCountStatusInRange(s);
            promoted = true;
        }
    }
    quizFillSlots(s, rng);
    return promoted;
}

// 1150:0375 Quiz_CarryMissedToNextLevel (observed behaviour: copy, not swap)
void quizCarryMissedToNextLevel(QuizState& s, int oldLevel) {
    if (oldLevel >= 2 || oldLevel < 0) return;
    for (int i = oldLevel * 3; i < oldLevel * 3 + 3; ++i) {
        const QuizSlot& slot = s.slots[static_cast<size_t>(i)];
        if (slot.q == kQuizNoQuestion || slot.q < 0 || slot.q >= kQuestionCount) continue;
        if (s.status[static_cast<size_t>(slot.q)] == kQuizMissed) s.slots[static_cast<size_t>(i) + 3] = slot;
    }
}

// 1150:0314 Quiz_ResetHistory
void quizResetHistory(QuizState& s) { s.status.fill(0); }

// 1150:0403 Quiz_JudgeAnswer (state part)
QuizJudgement quizJudgeAnswer(QuizState& s, int slotIndex, int pos) {
    QuizSlot& slot = s.slots[static_cast<size_t>(slotIndex)];
    QuizJudgement j;
    j.question = slot.q;
    for (uint8_t t : slot.tried)
        if (t != 0) ++j.wrongTries;
    const bool validQ = slot.q >= 0 && slot.q < kQuestionCount;
    if (slot.perm[static_cast<size_t>(pos)] == 0) {
        j.correct = true;
        if (validQ) s.status[static_cast<size_t>(slot.q)] = j.wrongTries == 0 ? kQuizRightFirstTry : kQuizRightAfterMiss;
        slot.q = kQuizNoQuestion;
    } else {
        if (validQ) s.status[static_cast<size_t>(slot.q)] = kQuizMissed;
        slot.tried[static_cast<size_t>(pos)] = 1;
    }
    return j;
}

// 1150:100a (after Quiz_JudgeAnswer returned 1)
int quizCoinsForCorrect(const QuizSlot& slot) {
    int wrong = 0;
    for (uint8_t t : slot.tried)
        if (t != 0) ++wrong;
    return wrong == 0 ? 2 : wrong == 1 ? 1 : 0;
}

int quizOpenPositions(const QuizSlot& slot) {
    int n = 0;
    for (uint8_t t : slot.tried)
        if (t == 0) ++n;
    return n;
}

// 1150:0f85 Quiz_NextItem (the original loops for ever if all three positions were tried, which
// cannot happen: the correct answer is never marked; bounded here)
int quizNextItem(int item, const QuizSlot& slot, bool voice, int numLines) {
    if (item > 9) {
        if (item + 1 < numLines + 10) return item + 1;
        item = -1;
    }
    for (int guard = 0; guard < 8; ++guard) {
        ++item;
        if (item > 2) {
            if (voice) return 10;
            item = 0;
        }
        if (slot.tried[static_cast<size_t>(item)] == 0) return item;
    }
    return item;
}

// 1150:0fcb Quiz_PrevItem
int quizPrevItem(int item, const QuizSlot& slot, bool voice, int numLines) {
    if (item > 9) {
        if (item - 1 > 9) return item - 1;
        item = 3;
    }
    for (int guard = 0; guard < 8; ++guard) {
        int prev = item - 1;
        if (item < 1) {
            if (voice) return numLines + 9;
            prev = 2;
        }
        item = prev;
        if (slot.tried[static_cast<size_t>(item)] == 0) return item;
    }
    return item;
}

int quizLineVoice(const QuizTables& t, int q, int k) { return t.voiceBase[static_cast<size_t>(q)] + k; }

int quizAnswerVoice(const QuizTables& t, int q, int numLines, int answer) {
    if (isSilentAnswerQuestion(q, t)) return -1;
    return t.voiceBase[static_cast<size_t>(q)] + numLines + answer;
}

int quizBankBase(int bank) {
    switch (bank) {
        case 1: return kClueVoiceBank1;        // 0x1c84
        case 2: return kClueVoiceBank1 + 100;  // 0x1ce8
        case 3: return kClueVoiceBank1 + 200;  // 0x1d4c
        default: return -1;
    }
}

// 1150:0901 Quiz_ParseQuestion
std::optional<ParsedQuestion> parseQuestion(std::span<const uint8_t> resource) {
    ParsedQuestion pq;
    const auto nul = std::find(resource.begin(), resource.end(), uint8_t{0});
    pq.text.assign(resource.begin(), nul);
    pq.text.push_back(0);
    std::vector<uint8_t>& t = pq.text;
    const size_t n = t.size();

    bool seen = false;
    int nl = 0;
    int end = -1;
    size_t p = 0;
    size_t term = 0;
    while (p < n && t[p] != 0 && end == -1) {
        const uint8_t c = t[p];
        if (isBreak(c)) {
            if (!seen) {
                t[p] = ' ';  // leading line breaks become spaces
            } else {
                if (nl == 1) {
                    if (pq.lineSlots < 10) pq.blank[pq.lineSlots] = 1;
                    ++pq.blankCount;
                } else if (nl == 2) {
                    t[p] = 0;  // third consecutive break ends the question
                    end = 0;
                    term = p;
                }
                if (nl < 2) ++nl;
                ++pq.lineSlots;
            }
        } else if (c == kEscape) {
            do {
                ++p;
            } while (p < n && t[p] != kEscape);
        } else {
            seen = true;
            nl = 0;
        }
        ++p;
    }
    if (end == -1) return std::nullopt;
    pq.terminated = true;
    pq.questionLen = term;
    pq.numLines = static_cast<int16_t>(pq.lineSlots - 1 - pq.blankCount);
    pq.lineSlotOf.fill(-1);
    for (int i = 0, k = 0; i < kMaxQuestionLines; ++i)
        if (!pq.blank[static_cast<size_t>(i)]) pq.lineSlotOf[static_cast<size_t>(k++)] = static_cast<int8_t>(i);
    // The non-blank lines that have a g_quizLineY entry: the question's slots are 0..lineSlots-2.
    int placed = 0;
    for (int i = 0; i < std::min(kMaxQuestionLines, pq.lineSlots - 1); ++i)
        if (!pq.blank[static_cast<size_t>(i)]) ++placed;
    if (pq.numLines > placed || pq.numLines < 0) {
        mischief::log::warn("quiz: a question with {} lines; only the first {} can be read out, highlighted and clicked",
                  int(pq.numLines), placed);
        pq.numLines = static_cast<int16_t>(std::clamp<int>(pq.numLines, 0, placed));
    }

    while (p < n && isBreak(t[p])) ++p;
    pq.answerOff[0] = p;
    int count = 1;
    while (p < n && t[p] != 0 && count < 3) {
        if (isBreak(t[p])) {
            t[p] = 0;
            ++p;
            pq.answerOff[static_cast<size_t>(count++)] = p;
        } else {
            ++p;
        }
    }
    pq.answerCount = count;
    for (int a = 0; a < 3; ++a) {
        if (a >= count) {
            pq.answerOff[static_cast<size_t>(a)] = n - 1;  // empty (the original leaves the pointer stale)
            pq.answerLen[static_cast<size_t>(a)] = 0;
            continue;
        }
        size_t e = pq.answerOff[static_cast<size_t>(a)];
        while (e < n && t[e] != 0) ++e;
        pq.answerLen[static_cast<size_t>(a)] = e - pq.answerOff[static_cast<size_t>(a)];
    }
    return pq;
}

}  // namespace tmport::game
