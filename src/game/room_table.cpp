#include "game/room_table.h"

namespace tmport::game {

void RoomTable::clearSpots() {
    for (RoomDef& r : rooms) {
        r.slotObject.fill(kSpotNone);
        r.slotContent.fill(kSpotNone);
    }
}

bool RoomTable::operator==(const RoomTable& o) const {
    for (size_t i = 0; i < rooms.size(); ++i) {
        const RoomDef& a = rooms[i];
        const RoomDef& b = o.rooms[i];
        if (a.startX != b.startX || a.startY != b.startY || a.startFrame != b.startFrame ||
            a.startFacingLeft != b.startFacingLeft || a.exitLeft != b.exitLeft || a.exitRight != b.exitRight ||
            a.exitDoor != b.exitDoor || a.unused09 != b.unused09 || a.typeLeft != b.typeLeft ||
            a.typeRight != b.typeRight || a.typeDoor != b.typeDoor || a.unused0D != b.unused0D ||
            a.doorX != b.doorX || a.unused10 != b.unused10 || a.bgPmap != b.bgPmap ||
            a.slotObject != b.slotObject || a.slotContent != b.slotContent || a.level != b.level ||
            a.music != b.music)
            return false;
    }
    return true;
}

int treasureHallLength(uint16_t climbsDone) {
    unsigned n = climbsDone < 2 ? 2u : (static_cast<unsigned>(climbsDone) - 2u) / 6u + 2u;
    if (n > static_cast<unsigned>(kHallMaxRooms)) n = kHallMaxRooms;
    return static_cast<int>(n);
}

// 1190:0000 TreasureHall_BuildRooms
void RoomTable::buildTreasureHall(uint16_t climbsDone) {
    const int n = treasureHallLength(climbsDone);
    if (n == 1) {  // unreachable (n >= 2), kept from the original
        (*this)[0].exitRight = 0xFF;
        return;
    }
    (*this)[0].exitRight = 1;
    int r = 1;
    for (int i = 0; i < n - 2; ++i, ++r) {
        (*this)[r].exitLeft = static_cast<uint8_t>(r - 1);
        (*this)[r].exitRight = static_cast<uint8_t>(r + 1);
        (*this)[r].bgPmap = kHallMiddleBackground;
    }
    (*this)[r].exitLeft = static_cast<uint8_t>(r - 1);
    (*this)[r].exitRight = 0xFF;
    (*this)[r].bgPmap = kHallLastBackground;
}

// 1190:009b TreasureHall_LoadRoom (pedestal indices only; loading is the hall scene's job)
std::array<int8_t, 6> hallPedestalTreasures(uint16_t climbsDone, int room) {
    std::array<int8_t, 6> ped{};
    ped.fill(-1);
    auto reduce = [](int8_t v) -> int8_t { return v > 42 ? static_cast<int8_t>(v % kTreasureCycle) : v; };
    if (room == 0) {
        ped[0] = reduce(static_cast<int8_t>(static_cast<uint8_t>(climbsDone - 1)));
    } else {
        uint16_t v = static_cast<uint16_t>(climbsDone - 2 - (room - 1) * 6);
        for (int i = 0; i < 6; ++i, --v) ped[static_cast<size_t>(i)] = reduce(static_cast<int8_t>(static_cast<uint8_t>(v)));
    }
    for (int8_t& p : ped)
        if (p < 0) p = -1;  // any negative index = empty pedestal (only >= 0 are loaded)
    return ped;
}

}  // namespace tmport::game
