#pragma once
// Climb layout and clue words (segment 1100): three distinct layout records per climb written
// into fixed room slots, the gate and net-shop markers, the key object's clue attributes, and the
// clue words / voice ids. Specs: re/docs/code_10f8-1148.md §3.3-3.6, §5;
// fmt_othr_tables.md §2-3; ARCHITECTURE.md §5.3-5.4.

#include <array>
#include <cstdint>
#include <string_view>

#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/room_table.h"
#include "mischief/rtl/borland_rand.h"

namespace tmport::game {

// Clue attribute bits of a layout record's mask, in the order the words are stored.
inline constexpr uint8_t kClueCount = 8;
inline constexpr uint8_t kClueSize = 4;
inline constexpr uint8_t kClueShape = 2;
inline constexpr uint8_t kClueNoun = 1;

// Voice-id biases of 1100:08ae (SSND 7300/7400/7500 + id).
inline constexpr int kClueVoiceCountBias = 0x25;   // count v -> 37 + v
inline constexpr int kClueVoiceSizeBias = 0x2C;    // size v -> 44 + v
inline constexpr int kClueVoiceShapeBias = 0x30;   // shape v -> 48 + v
inline constexpr int kClueVoiceNounBias = 7;       // noun v -> 7 + v
inline constexpr int kClueVoicePluralBias = 0x16;  // plural noun v -> 22 + v

struct RoomSlot {
    uint8_t room;
    uint8_t slot;
};
// Layout spot j of level L -> (room, slot) (1100:0000; code_10f8-1148.md §3.3).
const std::array<std::array<RoomSlot, kLayoutSpots>, kLevelCount>& layoutSpotSlots();
// Gate slots (kind = content = 0xFE) and the net-shop slot (0xFD) per level.
const std::array<std::array<RoomSlot, 2>, kLevelCount>& layoutGateSlots();
const std::array<RoomSlot, kLevelCount>& layoutShopSlots();

// 1100:0000 Layout_ApplyRecord: writes the record's 18 spots plus the gate and shop markers of
// one level. Levels other than 0..2 do nothing.
void applyLayoutRecord(RoomTable& rooms, int level, const LayoutRecord& r);

// The record indices chosen by Layout_GenerateClimb.
struct LayoutChoice {
    std::array<int, kLevelCount> record{};
};
// 1100:04b5 Layout_GenerateClimb: clears every spot slot, draws three distinct records from
// layouts[rankTier] with R(count) (re-rolled on collision), applies them, stores mask and key
// kind per level, then resolves the clue attributes (1100:06cc). Exactly 3+ rand() calls.
LayoutChoice generateClimbLayout(RoomTable& rooms, ClueState& clues, uint8_t rankTier, const GameTables& t,
                                 mischief::BorlandRand& rng);
// The index draw alone (same rand() calls), for a table of `count` records. Throws FormatError when
// count < kMinLayoutRecords (the original would re-roll forever).
LayoutChoice pickLayoutRecords(int count, mischief::BorlandRand& rng);

// 1100:06cc Clue_ResolveAttributes: the key object's attributes named by the mask, packed in the
// order count, size, shape, noun; plural = key object's count > 1 (even when count is not named).
void resolveClueAttributes(ClueState& clues, const GameTables& t);
// 1100:0793 Clue_BuildTexts: word views (plural noun table when plural).
void buildClueTexts(ClueState& clues, const ClueWordTables& words);
// 1100:08ae Clue_BuildVoiceIds.
void buildClueVoiceIds(ClueState& clues);
// Number of clue words a mask names (3 for every shipped record; at most 3 are stored).
int clueWordCount(uint8_t mask);
// SSND id of clue word k of a level in narrator bank 1..3 (HUD replay always uses bank 1).
int clueWordSoundId(const ClueState& clues, int level, int k, int bank = 1);

}  // namespace tmport::game
