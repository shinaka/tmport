#pragma once
// RoomTable: the mutable copy of the 50 RoomDef records (DS:23d8). The original edits the table
// in place: Layout_GenerateClimb (1100:04b5) writes the spot slots, TreasureHall_BuildRooms
// (1190:0000) rewrites the hall exits, and a player record restores the spot slots
// (1160:137d). Specs: re/docs/dgroup_map.md §4.16, ARCHITECTURE.md §5.2-5.3, code_1168-1198.md §7.

#include <array>
#include <cstdint>

#include "data/exe_tables.h"
#include "game/game_tables.h"

namespace tmport::game {

// Spot kind (+0x14+i) and content (+0x17+i) markers (code_10f8-1148.md §3.2).
inline constexpr uint8_t kSpotNone = 0xFF;  // no spot
inline constexpr uint8_t kSpotGate = 0xFE;  // the level's locked gate (kind = content)
inline constexpr uint8_t kSpotShop = 0xFD;  // the net-shop coin slot (kind = content)
inline constexpr uint8_t kContentEmpty = 0;
inline constexpr uint8_t kContentDecoy = 1;  // behaves exactly like 0 in the Windows EXE
inline constexpr uint8_t kContentTreasure = 2;
inline constexpr uint8_t kContentKey = 3;

// Rooms with fixed roles (constants in the code, ARCHITECTURE.md §5.2).
inline constexpr uint8_t kRoomNewClimb = 0xFE;   // g_room sentinel: start a new climb
inline constexpr int kHallFirstRoom = 0;         // treasure hall rooms 0..8 (zone 0)
inline constexpr int kHallMaxRooms = 9;
inline constexpr int kMountainStartRoom = 9;     // first climb starts here (1118:0000)
inline constexpr int kCastleRoom = 49;           // castle-wall climb (1168:0000)
inline constexpr std::array<int, kLevelCount> kGateRooms = {11, 25, 38};
inline constexpr std::array<int, kLevelCount> kGateTargets = {23, 36, 49};
inline constexpr std::array<int, kLevelCount> kShopRooms = {13, 24, 36};
inline constexpr uint16_t kHallMiddleBackground = 1037;  // 0x40d (1190:0000)
inline constexpr uint16_t kHallLastBackground = 1038;    // 0x40e

struct RoomTable {
    std::array<RoomDef, kRoomCount> rooms{};

    static RoomTable fromDefs(const std::array<RoomDef, kRoomCount>& defs) {
        RoomTable t;
        t.rooms = defs;
        return t;
    }

    static bool valid(int room) { return room >= 0 && room < kRoomCount; }
    RoomDef& operator[](int room) { return rooms[static_cast<size_t>(room)]; }
    const RoomDef& operator[](int room) const { return rooms[static_cast<size_t>(room)]; }

    // Room +0x1a (0..2 mountain levels, 3 castle) and +0x1b (music zone; 0 = treasure hall).
    // Out-of-range rooms (e.g. the 0xFE sentinel) give 0xFF.
    uint8_t level(int room) const { return valid(room) ? (*this)[room].level : uint8_t{0xFF}; }
    uint8_t musicZone(int room) const { return valid(room) ? (*this)[room].music : uint8_t{0xFF}; }
    bool inHall(int room) const { return musicZone(room) == 0; }

    uint8_t& spotKind(int room, int slot) { return (*this)[room].slotObject[static_cast<size_t>(slot)]; }
    uint8_t& spotContent(int room, int slot) { return (*this)[room].slotContent[static_cast<size_t>(slot)]; }
    uint8_t spotKind(int room, int slot) const { return (*this)[room].slotObject[static_cast<size_t>(slot)]; }
    uint8_t spotContent(int room, int slot) const { return (*this)[room].slotContent[static_cast<size_t>(slot)]; }

    // First loop of 1100:04b5: every spot slot of every room := 0xFF (kind and content).
    void clearSpots();
    bool operator==(const RoomTable& o) const;  // field-wise (RoomDef has no comparison)
    // 1190:0000 TreasureHall_BuildRooms: chain rooms 0..N-1 (N = treasureHallLength).
    void buildTreasureHall(uint16_t climbsDone);
};

// Number of treasure-hall rooms for a player: climbsDone < 2 ? 2 : min(9, (climbsDone-2)/6 + 2)
// (unsigned arithmetic, 1190:0000).
int treasureHallLength(uint16_t climbsDone);

// 1190:009b TreasureHall_LoadRoom, the pedestal contents of hall room `room`: treasure index per
// pedestal or -1 (empty). Room 0 shows the newest treasure on pedestal 0 only; room r >= 1 shows
// climbsDone-2-6(r-1)-i. The original keeps the value in a signed char and reduces only values
// above 42 (signed % 43), so after 128 climbs indices wrap negative and pedestals show empty
// (kept, PORTING_NOTES.md §6.1 "Treasure-hall signed index").
std::array<int8_t, 6> hallPedestalTreasures(uint16_t climbsDone, int room);

}  // namespace tmport::game
