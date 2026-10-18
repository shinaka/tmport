#include "game/layout.h"

#include <bit>

#include "game/quiz.h"

namespace tmport::game {

namespace {

constexpr RoomSlot rs(int room, int slot) { return RoomSlot{static_cast<uint8_t>(room), static_cast<uint8_t>(slot)}; }

// 1100:0000 store order (code_10f8-1148.md §3.3, fmt_othr_tables.md §3.4).
constexpr std::array<std::array<RoomSlot, kLayoutSpots>, kLevelCount> kSpotSlots = {{
    {rs(10, 0), rs(10, 2), rs(12, 0), rs(12, 1), rs(13, 0), rs(14, 0), rs(14, 1), rs(14, 2), rs(15, 0),
     rs(16, 0), rs(16, 2), rs(17, 1), rs(17, 2), rs(18, 0), rs(18, 1), rs(18, 2), rs(22, 0), rs(22, 1)},
    {rs(23, 0), rs(23, 2), rs(24, 2), rs(26, 0), rs(26, 2), rs(27, 0), rs(27, 1), rs(28, 0), rs(28, 2),
     rs(29, 1), rs(29, 2), rs(30, 1), rs(30, 2), rs(31, 0), rs(31, 1), rs(31, 2), rs(35, 0), rs(35, 1)},
    {rs(36, 0), rs(37, 0), rs(37, 2), rs(39, 0), rs(39, 1), rs(40, 0), rs(40, 2), rs(41, 0), rs(41, 1),
     rs(41, 2), rs(42, 2), rs(43, 1), rs(43, 2), rs(44, 0), rs(44, 1), rs(44, 2), rs(48, 0), rs(48, 1)},
}};
constexpr std::array<std::array<RoomSlot, 2>, kLevelCount> kGateSlots = {{
    {rs(11, 1), rs(11, 2)},
    {rs(25, 1), rs(25, 2)},
    {rs(38, 0), rs(38, 1)},
}};
constexpr std::array<RoomSlot, kLevelCount> kShopSlots = {rs(13, 1), rs(24, 0), rs(36, 2)};

}  // namespace

const std::array<std::array<RoomSlot, kLayoutSpots>, kLevelCount>& layoutSpotSlots() { return kSpotSlots; }
const std::array<std::array<RoomSlot, 2>, kLevelCount>& layoutGateSlots() { return kGateSlots; }
const std::array<RoomSlot, kLevelCount>& layoutShopSlots() { return kShopSlots; }

// 1100:0000 Layout_ApplyRecord
void applyLayoutRecord(RoomTable& rooms, int level, const LayoutRecord& r) {
    if (level < 0 || level >= kLevelCount) return;
    const size_t L = static_cast<size_t>(level);
    for (size_t j = 0; j < kLayoutSpots; ++j) {
        const RoomSlot s = kSpotSlots[L][j];
        rooms.spotKind(s.room, s.slot) = r.kind[j];
        rooms.spotContent(s.room, s.slot) = r.content[j];
    }
    for (const RoomSlot s : kGateSlots[L]) {
        rooms.spotKind(s.room, s.slot) = kSpotGate;
        rooms.spotContent(s.room, s.slot) = kSpotGate;
    }
    const RoomSlot shop = kShopSlots[L];
    rooms.spotKind(shop.room, shop.slot) = kSpotShop;
    rooms.spotContent(shop.room, shop.slot) = kSpotShop;
}

LayoutChoice pickLayoutRecords(int count, BorlandRand& rng) {
    LayoutChoice c;
    c.record[0] = rng.random(count);
    do {
        c.record[1] = rng.random(count);
    } while (c.record[1] == c.record[0]);
    do {
        c.record[2] = rng.random(count);
    } while (c.record[2] == c.record[0] || c.record[2] == c.record[1]);
    return c;
}

// 1100:04b5 Layout_GenerateClimb
LayoutChoice generateClimbLayout(RoomTable& rooms, ClueState& clues, uint8_t rankTier, const GameTables& t,
                                 BorlandRand& rng) {
    rooms.clearSpots();
    const auto& table = t.layouts[rankTier < kTierCount ? rankTier : kTierCount - 1];
    const LayoutChoice c = pickLayoutRecords(static_cast<int>(table.size()), rng);
    for (size_t L = 0; L < kLevelCount; ++L) {
        const LayoutRecord& r = table[static_cast<size_t>(c.record[L])];
        applyLayoutRecord(rooms, static_cast<int>(L), r);
        clues.flags[L] = r.mask;
        clues.keyKind[L] = r.keyKind;
    }
    resolveClueAttributes(clues, t);
    return c;
}

// 1100:06cc Clue_ResolveAttributes
void resolveClueAttributes(ClueState& clues, const GameTables& t) {
    for (size_t L = 0; L < kLevelCount; ++L) {
        const ObjectAttributes e = t.object(clues.keyKind[L]);
        const uint8_t f = clues.flags[L];
        size_t k = 0;
        // The original has no bound: a mask with all four bits would spill into the next level.
        auto put = [&](uint8_t v) {
            if (k < 3) clues.attr[L][k] = v;
        };
        if (f & kClueCount) {
            put(e.count);
            ++k;
        }
        if (f & kClueSize) {
            put(e.size);
            ++k;
        }
        if (f & kClueShape) {
            put(e.shape);
            ++k;
        }
        if (f & kClueNoun) put(e.noun);  // no increment: the last word
        clues.plural[L] = e.count > 1 ? 1 : 0;
    }
}

// 1100:0793 Clue_BuildTexts
void buildClueTexts(ClueState& clues, const ClueWordTables& w) {
    auto word = [](const auto& table, uint8_t v) -> std::string_view { return v < table.size() ? table[v] : std::string_view{}; };
    for (size_t L = 0; L < kLevelCount; ++L) {
        const uint8_t f = clues.flags[L];
        size_t k = 0;
        auto put = [&](std::string_view s) {
            if (k < 3) clues.text[L][k] = s;
        };
        if (f & kClueCount) {
            put(word(w.count, clues.attr[L][k < 3 ? k : 2]));
            ++k;
        }
        if (f & kClueSize) {
            put(word(w.size, clues.attr[L][k < 3 ? k : 2]));
            ++k;
        }
        if (f & kClueShape) {
            put(word(w.shape, clues.attr[L][k < 3 ? k : 2]));
            ++k;
        }
        if (f & kClueNoun) {
            const uint8_t v = clues.attr[L][k < 3 ? k : 2];
            put(clues.plural[L] ? word(w.nounPlural, v) : word(w.noun, v));
        }
    }
}

// 1100:08ae Clue_BuildVoiceIds
void buildClueVoiceIds(ClueState& clues) {
    for (size_t L = 0; L < kLevelCount; ++L) {
        const uint8_t f = clues.flags[L];
        size_t k = 0;
        auto put = [&](int bias) {
            if (k < 3) clues.voice[L][k] = static_cast<uint16_t>(clues.attr[L][k] + bias);
        };
        if (f & kClueCount) {
            put(kClueVoiceCountBias);
            ++k;
        }
        if (f & kClueSize) {
            put(kClueVoiceSizeBias);
            ++k;
        }
        if (f & kClueShape) {
            put(kClueVoiceShapeBias);
            ++k;
        }
        if (f & kClueNoun) put(clues.plural[L] ? kClueVoicePluralBias : kClueVoiceNounBias);
    }
}

int clueWordCount(uint8_t mask) {
    const int n = std::popcount(static_cast<unsigned>(mask & 0x0F));
    return n > 3 ? 3 : n;
}

int clueWordSoundId(const ClueState& clues, int level, int k, int bank) {
    const int base = quizBankBase(bank);
    if (base < 0) return -1;
    return base + clues.voice[static_cast<size_t>(level)][static_cast<size_t>(k)];
}

}  // namespace tmport::game
