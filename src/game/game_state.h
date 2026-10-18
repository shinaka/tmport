#pragma once
// GameState: the game's global variables, one plain struct per original module, keeping the
// original's meanings and units (DGROUP address in each comment). Persisted fields map 1:1 onto
// one 0x62F-byte users.dat record (re/docs/ARCHITECTURE.md §4.3, code_1150-1160.md §5.1):
// toRecord/fromRecord copy exactly the fields Users_SaveCurrent (1160:0f28) and
// Users_LoadSelected (1160:137d) copy; every other byte of the record is preserved.
// The users.dat file itself (roster, sort, merge) belongs to save/users_file.h.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "game/game_tables.h"
#include "game/quiz.h"
#include "game/room_table.h"

namespace tmport::save {
struct PlayerRecord;
}

namespace tmport::game {

inline constexpr size_t kRecordSize = 0x62F;  // one users.dat slot (1583 bytes)
inline constexpr size_t kNameSize = 9;        // char[9] at DS:224e
inline constexpr char kNoPlayerChar = '_';    // DS:224e = "________": nobody signed in

// Record offsets (ARCHITECTURE.md §4.3; the same values as save::off in save/user_record.h).
namespace rec {
inline constexpr size_t kName = 0x000, kTier = 0x009, kTotal = 0x00A, kSoundOn = 0x00E, kVoiceOn = 0x00F,
                        kHintsOn = 0x010, kHintUnseen = 0x011, kHaveKey = 0x015, kCoins = 0x016, kNets = 0x018,
                        kGemsFound = 0x01A, kClimbPoints = 0x01B, kShopCoinsLeft = 0x01D, kNumClues = 0x01F,
                        kClueFlags = 0x020, kClueAttr = 0x023, kCluePlural = 0x02C, kQuizTier = 0x02F,
                        kQuizStatus = 0x030, kQuizSlots = 0x197, kRoom = 0x1DF, kUnused1 = 0x1E0,
                        kSpotKind = 0x500, kSpotContent = 0x596, kUnused2 = 0x62C, kClimbsDone = 0x62D;
}  // namespace rec

// 1160 sign-in + 10e0 player controller: who is playing and where.
struct PlayerState {
    std::array<char, kNameSize> name{'_', '_', '_', '_', '_', '_', '_', '_', 0};  // DS:224e g_playerName
    uint8_t room = kRoomNewClimb;  // DS:5cca g_room (0xFE = start a new climb)
    int16_t x = 0;                 // DS:5ccc g_playerX (left edge)
    int16_t y = 0;                 // DS:5cce g_playerY
    uint8_t frame = 0;             // DS:5cd0 g_playerFrame
    uint8_t facingLeft = 0;        // DS:5cd1 g_playerFacingLeft
    int16_t w = 0;                 // DS:5cd2 g_playerW (width of the current frame)
    int16_t h = 0;                 // DS:5cd4 g_playerH
    uint8_t newClimb = 0;          // DS:5ef3 g_newClimb

    bool signedIn() const { return name[0] != kNoPlayerChar; }
    // Name bytes up to the NUL (raw FFNT/ANSI bytes, at most 9).
    std::string nameString() const;
    // strcpy semantics with the 9-byte buffer: copies up to 8 bytes and a NUL.
    void setName(std::string_view n);
    bool operator==(const PlayerState&) const = default;
};

// Options (per player): DS:0b96 / 0b97 / 5f00.
struct OptionsState {
    uint8_t soundOn = 1;  // DS:0b96 g_soundOn (music + sound effects)
    uint8_t voiceOn = 1;  // DS:0b97 g_voiceOn (speech)
    uint8_t hintsOn = 1;  // DS:5f00 g_hintsOn (New Player Messages)
    bool operator==(const OptionsState&) const = default;
};

// 10e8 status.cpp: inventory and per-climb counters.
struct InventoryState {
    uint8_t haveKey = 0;         // DS:5e59 g_haveKey
    uint16_t coins = 10;         // DS:5e5a g_coins
    uint16_t nets = 10;          // DS:5e5c g_nets
    uint8_t gemsFound = 0;       // DS:5e5e g_gemsFound (treasures found on this level; picks the picture)
    uint16_t climbPoints = 0;    // DS:5e5f g_climbPoints (treasures this climb, added to the total)
    uint8_t treasureCount2 = 0;  // DS:5e61 g_treasureCount2 (treasures found this climb, not saved)
    uint8_t numClues = 0;        // DS:1078 g_numClues (clue words earned on this level, 0..3)
    bool operator==(const InventoryState&) const = default;
};

// 10e8/10f0 rank: score and difficulty.
struct RankState {
    uint8_t rankTier = 0;         // DS:5e58 g_rankTier = rank / 2
    uint32_t totalTreasures = 0;  // DS:5e62 g_totalTreasures (the score, sort key)
    uint16_t climbsDone = 0;      // DS:65ce g_climbsDone (= treasures delivered)
    int16_t rank = 0;             // DS:5e78 g_rank (set at the climb end)
    uint8_t rankPromoted = 0;     // DS:0b9c g_rankPromoted
    uint8_t isChampion = 0;       // DS:5ef2 g_isChampion
    bool operator==(const RankState&) const = default;
};

// 1100 climb layout and clues: per level (mountain levels 0..2).
struct ClueState {
    std::array<uint8_t, kLevelCount> flags{};    // DS:5ea8 g_clueFlags = record mask (record +0x20)
    std::array<uint8_t, kLevelCount> keyKind{};  // DS:5ec9 g_keyKind (not saved)
    std::array<std::array<uint8_t, 3>, kLevelCount> attr{};  // DS:5eab g_clueAttr, packed (record +0x23)
    std::array<uint8_t, kLevelCount> plural{};   // DS:5eb4 g_cluePlural (record +0x2c)
    // Rebuilt from the above (1100:0793 / 08ae); never saved. Views into the EXE's string pool.
    std::array<std::array<std::string_view, 3>, kLevelCount> text{};  // DS:5e84 g_clueText
    std::array<std::array<uint16_t, 3>, kLevelCount> voice{};         // DS:5eb7 g_clueVoice (+7300/7400/7500)
    bool operator==(const ClueState&) const = default;
};

// 1130 New Player Messages.
struct HintState {
    uint32_t unseen = 0xFFFFFFFFu;  // DS:5f01 g_hintUnseen (bit n = once-only hint n not yet queued)
    int16_t pending = -1;           // DS:13ce g_hintPending (-1 = none)
    bool operator==(const HintState&) const = default;
};

// 10f8 net shop.
struct NetShopState {
    int16_t coinsLeft = 4;  // DS:5e7e g_shopCoinsLeft (shared by the three shops, saved)
    int16_t state = 0;      // DS:1064 g_netShopState: door state machine 0..7 (10f8:0000)
    bool operator==(const NetShopState&) const = default;
};

// 1110 rescue coin pile.
struct CoinPileState {
    uint8_t room = 0xFF;         // DS:1288 g_coinPileRoom (0xFF none, 0xFE armed)
    uint8_t noSpawnRoom = 0;     // DS:5ed2 g_coinPileNoSpawnRoom
    int16_t x = 0;               // DS:5ed3 g_coinPileX (y is fixed at 230)
    int16_t w = 0, h = 0;        // DS:5ed5 / 5ed7 PMAP 1699 size (set by the scene at load)
    bool operator==(const CoinPileState&) const = default;
};

// 1198 room scene: the treasure pictures of the current level (not saved).
struct TreasureState {
    int count = 0;                                            // DS:0ec3[tier] entries in use
    std::array<int16_t, kMaxTreasuresPerLevel> index{};       // local_e: treasure index (BNDL 330 + index)
    bool operator==(const TreasureState&) const = default;
};

struct GameState {
    PlayerState player;
    OptionsState options;
    InventoryState inv;
    RankState rank;
    ClueState clues;
    QuizState quiz;
    HintState hints;
    NetShopState shop;
    CoinPileState coinPile;
    TreasureState treasures;
    RoomTable rooms;  // mutable copy of DS:23d8 (spot slots, hall exits)
    // The record image the player was loaded from: bytes the game never copies (+0x1E0..0x4FF,
    // +0x62C, name bytes after the NUL) come from here when a fresh record image is built.
    std::array<uint8_t, kRecordSize> recordImage{};

    // A state with the pristine room table and no player signed in.
    static GameState initial(const GameTables& tables);

    uint8_t currentLevel() const { return rooms.level(player.room); }
    bool operator==(const GameState&) const = default;
};

// --- users.dat record mapping (one slot) ----------------------------------------------------------
// 1160:0f28 Users_SaveCurrent, field part: writes every saved field into `record` in place
// (strcpy for the name: bytes after its NUL keep their values; untouched ranges stay).
void writeRecord(const GameState& s, std::span<uint8_t, kRecordSize> record);
// Builds a record image: recordImage with the saved fields written over it.
std::array<uint8_t, kRecordSize> toRecord(const GameState& s);
// 1160:137d Users_LoadSelected, field part: plain copy of every saved field (including the sound
// flag; the sound-on-load quirk is loadPlayerRecord's). Keeps the bytes in recordImage.
void fromRecord(GameState& s, std::span<const uint8_t, kRecordSize> record);

// Bridges to the save area's typed record (save/user_record.h).
save::PlayerRecord toPlayerRecord(const GameState& s);
void fromPlayerRecord(GameState& s, const save::PlayerRecord& r);

}  // namespace tmport::game
