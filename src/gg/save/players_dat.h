#pragma once
// PLAYERS.DAT of Gizmos & Gadgets: the sign-in roster and the saved players (re/gg/docs/fmt_data.md
// §8, code_ui.md §11-§12, code_world.md §4.2, tools/gg/players.py).
//
//   0x000   'U' 'S' 'R' 0x1A, u16 version (12), u16 player count           header (8 bytes)
//   0x008   100 index entries of 22 bytes {char name[16], u32 points, u16 slot}
//   0x8A0   99 player records of 0x16BD (5821) bytes, slot s at 0x8A0 + s * 0x16BD
//   = 578 487 bytes; the file is pre-allocated at its full size and never changes size.
//
// The header and index are the sign-in's private copy g_roster (DS:a075, 0x8A0 bytes); a record is
// the memory image of the current player g_player (DS:4562..5C1E), so record offset r = DS:4562 + r.
// Every byte of the file is a typed field below (the unused words included), so parse() followed by
// bytes() reproduces any file byte for byte.
//
// Field names follow re/names/gg/dgroup.tsv (the global each field is): g_score -> points,
// g_withChimps -> withChimps, ...

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::gg {

inline constexpr uint16_t kPlayersVersion = 12;
inline constexpr size_t kPlayersHeaderSize = 8;
inline constexpr size_t kRosterEntrySize = 22;   // a copy of the record's first 22 bytes
inline constexpr int kRosterEntryCount = 100;     // entries 0..98 are used; entry 99 is never read
inline constexpr size_t kRosterSize = kPlayersHeaderSize + kRosterEntrySize * kRosterEntryCount;  // 0x8A0
inline constexpr size_t kPlayerRecordSize = 0x16BD;  // 5821
inline constexpr int kPlayerRecordCount = 99;
inline constexpr size_t kPlayersFileSize = kRosterSize + kPlayerRecordSize * kPlayerRecordCount;  // 578 487
inline constexpr size_t kPlayerNameSize = 16;      // up to 15 characters + NUL
inline constexpr int kMaxPlayers = 99;             // New Player refuses a 100th (0x1069)

inline constexpr int kBuildingCount = 3;           // 0 Automotive, 1 Aircraft, 2 Alternative Energy
inline constexpr int kRaceCount = 15;              // race index = building * 5 + level
inline constexpr int kHintCount = 19;              // New Player Hints 0x1043..0x1055
inline constexpr uint16_t kFirstHintId = 0x1043;
inline constexpr int kPuzzleKinds = 8;
inline constexpr int kPuzzleVariantSlots = 33;     // per kind; at most 32 variants are used
inline constexpr size_t kBuildingBlockSize = 0x6BA;  // 1722
inline constexpr int kPartStates = 150;            // block +0x014: 0x258 bytes of {holder, flags}
inline constexpr size_t kDoorTypesSize = 0x96;     // block +0x26C
inline constexpr size_t kRoomItemsSize = 0x190;    // block +0x302
inline constexpr int kActorSlots = 12;             // block +0x492: 12 x 0x2E bytes
inline constexpr size_t kActorStateSize = 0x2E;

// Record offsets (DS address = 0x4562 + offset). fmt_data.md §8.3, code_ui.md §12.4.
namespace prec {
inline constexpr size_t kName = 0x000;               // char[16]                 g_player (DS:4562)
inline constexpr size_t kPoints = 0x010;             // u32                      g_score DS:4572
inline constexpr size_t kSlot = 0x014;               // i16                      g_playerSlot DS:4576
inline constexpr size_t kWithChimps = 0x016;         // i16 1 / 0                g_withChimps DS:4578
inline constexpr size_t kUnused18 = 0x018;           // u16, never referenced    DS:457a
inline constexpr size_t kLive = 0x01A;               // 20 bytes                 DS:457c..458f
inline constexpr size_t kRaceIndex = 0x02E;          // i16 building*5 + level   g_raceIndex DS:4590
inline constexpr size_t kBuilding = 0x030;           // i16, -1 none             g_building DS:4592
inline constexpr size_t kLevel = 0x032;              // i16                      g_level DS:4594
inline constexpr size_t kRacesWon = 0x034;           // i16[3], -1..4            g_racesWon DS:4596
inline constexpr size_t kBuildingInProgress = 0x03A; // i16[3]                   g_buildingInProgress DS:459c
inline constexpr size_t kHintsOff = 0x040;           // i16                      g_hintsOff DS:45a2
inline constexpr size_t kHintSeen = 0x042;           // u16[19]                  g_hintSeen DS:45a4
inline constexpr size_t kUnused68 = 0x068;           // u16, never referenced    DS:45ca
inline constexpr size_t kSoundOff = 0x06A;           // i16                      g_soundOff DS:45cc
inline constexpr size_t kUnused6C = 0x06C;           // u16, never referenced    DS:45ce
inline constexpr size_t kPuzzleSolved = 0x06E;       // u16[8][33]               g_puzzleSolved DS:45d0
inline constexpr size_t kBuildingBlocks = 0x27E;     // 3 x 0x6BA                g_buildingRecords DS:47e0
inline constexpr size_t kRacesWonTotal = 0x16AC;     // i16, 15 = the end        g_racesWonTotal DS:5c0e
inline constexpr size_t kVehicleColor = 0x16AE;      // u8[15]                   g_vehicleColor DS:5c10
}  // namespace prec

// Building block offsets (block of building b at record +0x27E + b * 0x6BA). fmt_data.md §8.4.
namespace pblock {
inline constexpr size_t kLive = 0x000;       // copy of the 20 live bytes (record +0x1A)
inline constexpr size_t kParts = 0x014;      // Parts_SaveState 153f:0385: {i16 holder, u16 flags} per part
inline constexpr size_t kDoorTypes = 0x26C;  // Rooms_SaveState 1622:0fb3: 7 or 9 per door object, room order
inline constexpr size_t kRoomItems = 0x302;  // Rooms_SaveState: per room present {u16 n, n x {x, floor, kind}}
inline constexpr size_t kActors = 0x492;     // Actors_SaveState 1834:0b5e: actor fields +0x14..+0x41
}  // namespace pblock

// The 20 live bytes of the building in progress, DS:457c..458f (record +0x1A, block +0x000).
struct LiveState {
    int16_t blueprintSeen = 0;      // g_blueprintSeen DS:457c
    int16_t projectiles = 0;        // g_projectiles DS:457e (bananas; 10 at level 0)
    int16_t tubeRoom = 0;           // g_tubeRoom DS:4580
    int16_t partsNeeded = 0;        // g_partsNeeded DS:4582
    int16_t partsInstalled = 0;     // g_partsInstalled DS:4584
    int16_t vehicleBonusGiven = 0;  // g_vehicleBonusGiven DS:4586
    int16_t paintColor = 0;         // g_paintColor DS:4588 (-1 none)
    int16_t hintCancel1046 = 0;     // g_hintCancel1046 DS:458a
    int16_t hintCancel1048 = 0;     // g_hintCancel1048 DS:458c
    int16_t setupCoin = 0;          // g_setupCoin DS:458e (written only)
    static constexpr size_t kSize = 20;
    bool operator==(const LiveState&) const = default;
};

// One part's saved state (Parts_SaveState 153f:0385): part record +0x0c holder (-1 nowhere or in a
// room, 0 the player, 2..11 a chimp) and +0x36 flags.
struct PartState {
    int16_t holder = 0;
    uint16_t flags = 0;
    bool operator==(const PartState&) const = default;
};

// One actor's saved fields +0x14..+0x41 (Actors_SaveState 1834:0b5e; code_world.md §9.1).
struct ActorState {
    int16_t anchorX = 0;       // +0x14
    int16_t anchorY = 0;       // +0x16
    int16_t seq = 0;           // +0x18
    int16_t room = 0;          // +0x1A
    int16_t floor = 0;         // +0x1C
    int16_t prevSeq = 0;       // +0x1E
    int16_t exitSeq = 0;       // +0x20
    int16_t step = 0;          // +0x22
    int16_t doorCooldown = 0;  // +0x24
    int16_t loopCounter = 0;   // +0x26
    int16_t loopStart = 0;     // +0x28
    int16_t disabled = 0;      // +0x2A
    int16_t pendingSeq = 0;    // +0x2C
    int16_t wallsOn = 0;       // +0x2E
    int16_t contactsOn = 0;    // +0x30
    int16_t blocked = 0;       // +0x32
    int16_t unused34 = 0;      // +0x34 (not read; the projectile's is -1)
    int16_t doorObject = 0;    // +0x36
    int16_t pad = 0;           // +0x38
    int16_t edgeObject = 0;    // +0x3A
    int16_t prevRoom = 0;      // +0x3C
    int16_t immune = 0;        // +0x3E
    int16_t item = 0;          // +0x40
    bool operator==(const ActorState&) const = default;
};

// An item of a room's saved list (Rooms_SaveState 1622:0fb3, code_world.md §7.5).
struct SavedItem {
    int16_t x = 0;
    int16_t floor = 0;
    int16_t kind = 0;
    bool operator==(const SavedItem&) const = default;
};

// A per-building block (0x6BA bytes): the state of a building left in progress.
struct BuildingBlock {
    LiveState live;
    std::array<PartState, kPartStates> parts{};
    std::array<uint8_t, kDoorTypesSize> doorTypes{};
    std::array<uint8_t, kRoomItemsSize> roomItems{};  // variable-length lists, see readRoomItems
    std::array<ActorState, kActorSlots> actors{};

    // The item lists of the first `roomCount` rooms present (Rooms_RestoreState reads exactly
    // that many). Throws tmport::FormatError when the lists run past the 0x190 bytes.
    std::vector<std::vector<SavedItem>> readRoomItems(int roomCount) const;
    // Writes the lists the way Rooms_SaveState does (the rest of the area is left as it is).
    // Throws tmport::FormatError when they do not fit in 0x190 bytes (the original overruns).
    void writeRoomItems(const std::vector<std::vector<SavedItem>>& rooms);

    bool operator==(const BuildingBlock&) const = default;
};

// One player record (0x16BD bytes = g_player DS:4562..5C1E).
struct PlayerRecord {
    std::array<uint8_t, kPlayerNameSize> name{};
    uint32_t points = 0;
    int16_t slot = 0;
    int16_t withChimps = 0;
    uint16_t unused18 = 0;
    LiveState live;
    int16_t raceIndex = 0;
    int16_t building = 0;
    int16_t level = 0;
    std::array<int16_t, kBuildingCount> racesWon{};
    std::array<int16_t, kBuildingCount> buildingInProgress{};
    int16_t hintsOff = 0;
    std::array<uint16_t, kHintCount> hintSeen{};
    uint16_t unused68 = 0;
    int16_t soundOff = 0;
    uint16_t unused6C = 0;
    std::array<std::array<uint16_t, kPuzzleVariantSlots>, kPuzzleKinds> puzzleSolved{};
    std::array<BuildingBlock, kBuildingCount> blocks{};
    int16_t racesWonTotal = 0;
    std::array<uint8_t, kRaceCount> vehicleColor{};

    static PlayerRecord fromBytes(std::span<const uint8_t, kPlayerRecordSize> bytes);
    void toBytes(std::span<uint8_t, kPlayerRecordSize> out) const;
    std::vector<uint8_t> bytes() const;

    // Player_InitRecord 2b5a:0000: all zero except building = -1 and racesWon = -1 x 3 (the sound
    // flag the init stores first is overwritten by the copy, code_ui.md §7).
    static PlayerRecord blank();

    // The name bytes up to the first NUL.
    std::string nameString() const;
    // Up to 15 bytes, NUL-padded to 16 (the sign-in's typed buffer is NUL-terminated).
    void setName(std::string_view s);
    bool operator==(const PlayerRecord&) const = default;
};

// An index entry (a copy of a record's first 22 bytes). Points -1 (being typed) and -2 (created by
// New Player) exist only in memory (code_ui.md §11.1).
struct RosterEntry {
    std::array<uint8_t, kPlayerNameSize> name{};
    uint32_t points = 0;
    uint16_t slot = 0;

    static RosterEntry fromBytes(std::span<const uint8_t, kRosterEntrySize> bytes);
    void toBytes(std::span<uint8_t, kRosterEntrySize> out) const;
    // The entry Players_SaveCurrent writes for a record: its name, points and slot.
    static RosterEntry ofRecord(const PlayerRecord& rec);
    std::string nameString() const;
    bool operator==(const RosterEntry&) const = default;
};

// The header and index: g_roster (DS:a075), read and written as the file's first 0x8A0 bytes.
struct Roster {
    std::array<uint8_t, 4> magic{'U', 'S', 'R', 0x1A};
    uint16_t version = kPlayersVersion;
    uint16_t count = 0;  // players: entries 0..count-1 in name order; the rest free slots in order
    std::array<RosterEntry, kRosterEntryCount> entries{};

    static Roster fromBytes(std::span<const uint8_t, kRosterSize> bytes);
    void toBytes(std::span<uint8_t, kRosterSize> out) const;
    std::vector<uint8_t> bytes() const;
    bool magicOk() const { return magic == std::array<uint8_t, 4>{'U', 'S', 'R', 0x1A}; }
    bool operator==(const Roster&) const = default;
};

// The whole file (the records live on the heap: 99 records are about 0.6 MB).
struct PlayersDat {
    Roster roster;
    std::vector<PlayerRecord> records = std::vector<PlayerRecord>(kPlayerRecordCount);  // always 99

    // A whole file (578 487 bytes, USR 0x1A; any version: check versionOk()); nullopt otherwise.
    static std::optional<PlayersDat> parse(std::span<const uint8_t> file);
    std::vector<uint8_t> bytes() const;
    bool versionOk() const { return roster.version == kPlayersVersion; }

    // Roster_LoadOrCreate's creation (2caa:004f): header with count 0, entries 0..98 zero with slot =
    // entry number, entry 99 zero (the original leaves uninitialised heap bytes there), then the 99
    // blank records saved one by one through Players_SaveCurrent with g_rosterIndex = `rosterIndex`
    // (0 at start-up), which rewrites that index entry each time: entry 0 ends as {"", 0, 98}, so
    // slot 98 is listed twice and slot 0 never (fmt_data.md §8.2).
    static PlayersDat create(int rosterIndex = 0);

    static constexpr size_t recordOffset(int slot) {
        return kRosterSize + kPlayerRecordSize * static_cast<size_t>(slot);
    }
    static constexpr size_t entryOffset(int index) {
        return kPlayersHeaderSize + kRosterEntrySize * static_cast<size_t>(index);
    }
    bool operator==(const PlayersDat&) const = default;
};

}  // namespace tmport::gg
