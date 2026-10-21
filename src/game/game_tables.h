#pragma once
// Static game-logic tables, read at run time from the user's TMTWINCD.EXE (DGROUP) and TMT.DLL
// (OTHR 10550..10553, 10555). Nothing here is game content compiled into the engine: every value
// comes from the user's files (offsets in data/dgroup_offsets.h).
// Specs: re/docs/ARCHITECTURE.md §5, fmt_othr_tables.md §2-3, code_1150-1160.md §3.1,
// dgroup_map.md.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "data/exe_tables.h"

namespace tmport {
class ResourceManager;
}

namespace tmport::game {

// --- Fixed sizes of the original's arrays (facts about the code, not data) ----------------------
inline constexpr int kRoomCount = 50;         // RoomDef[50] at DS:23d8
inline constexpr int kLevelCount = 3;         // mountain levels 0..2 (room +0x1a); 3 = castle wall
inline constexpr int kTierCount = 4;          // rank tiers 0..3 (DS:5e58)
inline constexpr int kRankCount = 8;          // ranks 0..7 (thresholds DS:0ea0)
inline constexpr int kQuestionCount = 359;    // OTHR 11000..11358, history bytes at DS:5f83
inline constexpr int kQuizTierCount = 5;      // quiz tiers 0..3 by index, 4 = all questions
inline constexpr int kQuizSlotCount = 9;      // QuizSlot[9] at DS:5f3a (slot = 3*level + elf)
inline constexpr int kHintEventCount = 34;    // g_hintBit[34] at DS:13d0
inline constexpr int kLayoutSpots = 18;       // spots per layout record (one mountain level)
inline constexpr int kLayoutRecordSize = 0x26;
inline constexpr int kObjectTableEntries = 128;  // OTHR 10555: 512 bytes of 4-byte entries
inline constexpr int kMaxTreasuresPerLevel = 5;  // local_e[5] in 1198:0103

// --- Resource ids ---------------------------------------------------------------------------------
inline constexpr int kLayoutFirstId = 10550;   // + rank tier (1100:04fa)
inline constexpr int kObjectTableId = 10555;   // 1100:06d4
inline constexpr int kQuestionFirstId = 11000; // + question index (1150:100a)
inline constexpr int kHintTextFirstId = 11500; // + hint event (BNDL 327)
inline constexpr int kHintVoiceFirstId = 7100; // + hint event (1130:0166)
inline constexpr int kSpotPictureFirstId = 1400;  // + spot kind (1100:0638)
inline constexpr int kTreasureBndlFirstId = 330;  // + treasure index (1198:0103, 10f0:0000)
inline constexpr int kTreasureAnimFirstId = 20000; // + treasure index (1190:009b)
inline constexpr int kClueVoiceBank1 = 7300;   // SSND base of narrator bank 1 (0x1c84, 10e8:01ef)

// --- Treasure arithmetic (1198:0103, 10f0:0000, 1190:009b) --------------------------------------
inline constexpr int kTreasureCycle = 43;      // climbsDone mod 43 = the climb's own treasure
inline constexpr int kTreasureIndexLimit = 51; // random extras are drawn from base..50

// One layout record of OTHR 10550+tier (fmt_othr_tables.md §3.1).
struct LayoutRecord {
    uint8_t mask = 0;     // +0x00 which attributes the clue names: 8 count, 4 size, 2 shape, 1 noun
    uint8_t keyKind = 0;  // +0x01 picture (spot kind) that hides the key
    std::array<uint8_t, kLayoutSpots> kind{};     // +0x02 spot kind per layout spot
    std::array<uint8_t, kLayoutSpots> content{};  // +0x14 spot content per layout spot (0..3)
    bool operator==(const LayoutRecord&) const = default;
};

// One entry of OTHR 10555: attributes of spot picture PMAP 1400+k (fmt_othr_tables.md §2).
struct ObjectAttributes {
    uint8_t count = 0;  // 1..7
    uint8_t size = 0;   // 0..4
    uint8_t shape = 0;  // 0..4
    uint8_t noun = 0;   // 1..15
    bool operator==(const ObjectAttributes&) const = default;
};

// The clue-word tables (far char* arrays in DGROUP, index 0 = empty string). The views point
// into the ExeTables image and stay valid as long as that ExeTables lives. Raw bytes in the
// game's FFNT encoding.
struct ClueWordTables {
    std::array<std::string_view, 8> count{};        // DS:10f9 g_strCount
    std::array<std::string_view, 5> size{};         // DS:1119 g_strSize
    std::array<std::string_view, 5> shape{};        // DS:112d g_strShape
    std::array<std::string_view, 16> noun{};        // DS:1079 g_strNoun
    std::array<std::string_view, 16> nounPlural{};  // DS:10b9 g_strNounPlural
};

// Per-question tables (code_1150-1160.md §3.1).
struct QuizTables {
    std::array<uint16_t, kQuestionCount + 1> voiceBase{};  // DS:1990 first narration SSND id (+1 entry read as the end)
    std::array<uint8_t, kQuestionCount> voiceBank{};       // DS:1c60 narrator bank 1..3
    std::array<int16_t, 16> silentAnswers{};               // DS:1dc7 questions whose answers are never read
    std::array<int16_t, kQuizTierCount> tierStart{};       // DS:1de7 {0, 63, 179, 302, 359}
};

struct GameTables {
    std::array<RoomDef, kRoomCount> rooms{};             // DS:23d8 (pristine; RoomTable makes the mutable copy)
    std::array<uint32_t, kRankCount> rankThresholds{};   // DS:0ea0
    std::array<uint8_t, kTierCount> gemsNeeded{};        // DS:0ec3 treasures per level by tier
    std::array<uint8_t, kTierCount> elfCountByTier{};    // DS:128e
    std::array<int16_t, kTierCount> creatureSpawnPermille{};  // DS:2aaa castle wall creature chance
    std::array<int16_t, kHintEventCount> hintBit{};      // DS:13d0 event -> once-only bit, -1 = always
    std::array<int16_t, kRankCount> room9RankHints{};    // DS:0e8f hint queued in room 9 by rank
    std::array<int16_t, kRankCount> rankPromoHints{};    // DS:0fc8 climb-end hint when promoted
    std::array<int16_t, kRankCount> rankStayHints{};     // DS:0fd8 climb-end hint otherwise
    std::array<int16_t, kLevelCount> netSignX{};         // DS:1066
    std::array<int16_t, kLevelCount> shopWalkX{};        // DS:0e89 auto-walk target when the shop opens
    ClueWordTables words;
    QuizTables quiz;
    std::array<std::vector<LayoutRecord>, kTierCount> layouts;  // OTHR 10550..10553
    std::vector<ObjectAttributes> objects;                      // OTHR 10555

    // Everything (EXE tables + OTHR layouts). Throws mischief::Error on missing/short resources.
    static GameTables load(const ExeTables& exe, const ResourceManager& res);
    // Only the DGROUP tables (layouts/objects stay empty).
    static GameTables fromExe(const ExeTables& exe);

    // Attributes of a spot picture; zeros for kinds outside the table (the original reads past
    // the 10555 data into stale scratch memory there; never happens with shipped data).
    ObjectAttributes object(uint8_t kind) const;
};

// OTHR 10550+tier: u16 count, then count x 38-byte records. Records beyond the data are dropped.
std::vector<LayoutRecord> parseLayoutTable(std::span<const uint8_t> data);
// A climb draws three different records (one per mountain level, 1100:04b5); with fewer it would
// re-roll forever (REVIEW_1 M-3).
inline constexpr int kMinLayoutRecords = 3;
// parseLayoutTable of OTHR `id`; throws FormatError naming the resource when it holds fewer than
// kMinLayoutRecords records.
std::vector<LayoutRecord> loadLayoutTable(int id, std::span<const uint8_t> data);
// OTHR 10555: up to 128 x {count, size, shape, noun} (the NE padding gives zero entries).
std::vector<ObjectAttributes> parseObjectTable(std::span<const uint8_t> data);

}  // namespace tmport::game
