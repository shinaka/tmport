#pragma once
// One player slot of users.dat (0x62F = 1583 bytes; re/docs/ARCHITECTURE.md §4.3,
// sys_resources_saves.md §6.3). UserSlot keeps the raw bytes, so fields the game never touches
// (+0x1E0..0x4FF, +0x62C, name bytes after the NUL) survive a load/save byte for byte.
// PlayerRecord is the set of game variables a save copies into a slot (1160:0f28) and a restore
// copies back (1160:137d).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tmport::save {

inline constexpr size_t kSlotSize = 0x62F;                    // 1583
inline constexpr int kSlotCount = 7;
inline constexpr size_t kTableSize = kSlotSize * kSlotCount;  // 0x2B49 = 11081
inline constexpr size_t kHeaderSize = 4;
inline constexpr size_t kFileSize = kHeaderSize + kTableSize;  // 11085
inline constexpr std::array<uint8_t, 4> kMagic = {0x55, 0x53, 0x52, 0x1A};  // written on save, ignored on load (DS:2101)
inline constexpr size_t kNameSize = 9;   // char[9], at most 8 characters + NUL
inline constexpr size_t kMaxNameLength = 8;
inline constexpr char kNoPlayerChar = '_';  // DS:224E = "________": nobody signed in

// Field offsets inside a slot.
namespace off {
inline constexpr size_t kName = 0x000;            // char[9]
inline constexpr size_t kTier = 0x009;            // u8 rank tier (rank / 2)
inline constexpr size_t kTotalTreasures = 0x00A;  // u32, sort key
inline constexpr size_t kSoundOn = 0x00E;         // u8 Music & Sound
inline constexpr size_t kVoiceOn = 0x00F;         // u8
inline constexpr size_t kHintsOn = 0x010;         // u8 New Player Messages
inline constexpr size_t kHintMask = 0x011;        // u32 unseen once-only hints
inline constexpr size_t kHaveKey = 0x015;         // u8
inline constexpr size_t kCoins = 0x016;           // u16
inline constexpr size_t kNets = 0x018;            // u16
inline constexpr size_t kGemsFound = 0x01A;       // u8 treasures found on this level
inline constexpr size_t kClimbTreasures = 0x01B;  // u16 treasures this climb
inline constexpr size_t kShopCoinsLeft = 0x01D;   // i16
inline constexpr size_t kClueWords = 0x01F;       // u8
inline constexpr size_t kClueFlags = 0x020;       // u8[3]
inline constexpr size_t kClueAttr = 0x023;        // u8[3][3]
inline constexpr size_t kCluePlural = 0x02C;      // u8[3]
inline constexpr size_t kQuizTier = 0x02F;        // u8
inline constexpr size_t kQuizHistory = 0x030;     // u8[359]
inline constexpr size_t kQuizSlots = 0x197;       // 9 x {u16 id; u8 order[3]; u8 done[3]}
inline constexpr size_t kLocation = 0x1DF;        // u8 room, 0xFE = start a new climb
inline constexpr size_t kUnused1 = 0x1E0;         // 800 bytes, never touched
inline constexpr size_t kSpotKind = 0x500;        // u8[50][3]
inline constexpr size_t kSpotContent = 0x596;     // u8[50][3]
inline constexpr size_t kUnused2 = 0x62C;         // 1 byte, never touched
inline constexpr size_t kClimbsDone = 0x62D;      // u16
}  // namespace off

inline constexpr size_t kQuizHistorySize = 359;
inline constexpr int kQuizSlotCount = 9;
inline constexpr int kRooms = 50;
inline constexpr uint8_t kLocationNewClimb = 0xFE;

struct UserSlot {
    std::array<uint8_t, kSlotSize> bytes{};

    bool empty() const { return bytes[0] == 0; }  // the roster ends at the first empty name
    // Name bytes up to the first NUL (at most 9).
    std::string name() const;
    // strcpy semantics: writes the name (truncated to 8 bytes) and its NUL; the bytes after the
    // NUL keep their previous values.
    void setName(std::string_view name);

    uint8_t u8(size_t o) const { return bytes[o]; }
    uint16_t u16(size_t o) const { return static_cast<uint16_t>(bytes[o] | (bytes[o + 1] << 8)); }
    uint32_t u32(size_t o) const {
        return static_cast<uint32_t>(bytes[o]) | (static_cast<uint32_t>(bytes[o + 1]) << 8) |
               (static_cast<uint32_t>(bytes[o + 2]) << 16) | (static_cast<uint32_t>(bytes[o + 3]) << 24);
    }
    void setU8(size_t o, uint8_t v) { bytes[o] = v; }
    void setU16(size_t o, uint16_t v) {
        bytes[o] = static_cast<uint8_t>(v);
        bytes[o + 1] = static_cast<uint8_t>(v >> 8);
    }
    void setU32(size_t o, uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes[o + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
    }

    uint32_t totalTreasures() const { return u32(off::kTotalTreasures); }
    uint8_t location() const { return u8(off::kLocation); }
    uint16_t climbsDone() const { return u16(off::kClimbsDone); }

    bool operator==(const UserSlot&) const = default;
};

struct QuizSlot {
    uint16_t id = 0xFFFF;  // 0xFFFF = none
    std::array<uint8_t, 3> order{};
    std::array<uint8_t, 3> done{};
    bool operator==(const QuizSlot&) const = default;
};

// The game variables saved per player (DGROUP variable in the comment). Defaults = a new player
// (Users_InitNewPlayer 1160:1632) with the EXE image's initial sound/voice flags (DS:0b96/0b97 = 1);
// the per-climb fields are regenerated at climb start because location = 0xFE.
struct PlayerRecord {
    std::string name;                 // 224E (raw ANSI bytes, at most 8)
    uint8_t tier = 0;                 // 5E58
    uint32_t totalTreasures = 0;      // 5E62
    uint8_t soundOn = 1;              // 0B96
    uint8_t voiceOn = 1;              // 0B97
    uint8_t hintsOn = 1;              // 5F00
    uint32_t hintUnseenMask = 0xFFFFFFFFu;  // 5F01
    uint8_t haveKey = 0;              // 5E59
    uint16_t coins = 10;              // 5E5A
    uint16_t nets = 10;               // 5E5C
    uint8_t gemsFound = 0;            // 5E5E
    uint16_t climbTreasures = 0;      // 5E5F
    int16_t shopCoinsLeft = 4;        // 5E7E
    uint8_t clueWords = 0;            // 1078
    std::array<uint8_t, 3> clueFlags{};    // 5EA8
    std::array<uint8_t, 9> clueAttr{};     // 5EAB
    std::array<uint8_t, 3> cluePlural{};   // 5EB4
    uint8_t quizTier = 0;             // 5F82
    std::array<uint8_t, kQuizHistorySize> quizHistory{};  // 5F83
    std::array<QuizSlot, kQuizSlotCount> quizSlots{};     // 5F3A
    uint8_t location = kLocationNewClimb;                 // 5CCA
    std::array<uint8_t, kRooms * 3> spotKind{};           // 23EC + 0x1C*room + spot
    std::array<uint8_t, kRooms * 3> spotContent{};        // 23EF + 0x1C*room + spot
    uint16_t climbsDone = 0;          // 65CE

    // 1160:137d Users_LoadSelected, field copy (the Music & Sound restore quirk is game logic).
    static PlayerRecord fromSlot(const UserSlot& slot);
    // 1160:0f28 Users_SaveCurrent, field copy: writes every field; untouched bytes stay.
    void writeTo(UserSlot& slot) const;

    bool operator==(const PlayerRecord&) const = default;
};

}  // namespace tmport::save
