#pragma once
// What the DOS autoplayer knows about the mountain (area autoplay, wave 5): the three levels as loops
// of path cells, the cells that matter on them (gates, doors, the net shops), the places a walking
// player falls (the geyser hole, the rope bridges from 4 stars), and the castle wall as a grid of
// ladder pictures with a route to its exit. Everything is a fact of the specs (re/dos/docs/
// code_world.md §3.3-§3.5, §6.2-§6.9) or read from the game state; nothing here moves the game.
//
// Loop positions: each level's path is a closed loop of 128-pixel cells (code_world.md §3.4). The bot
// measures positions along it in pixels: index of the cell in the loop * 128 + (x & 0x7F), walking
// right increases it. The twin cells at the seams map to their canonical cell (level 0: E3, E4, FF
// -> FD, FE, E5; level 1: AB, AC, C2 -> C0, C1, AD; level 2: 77, 78, 88 -> 86, 87, 79), which have the
// same picture, content and x & 0x7F.

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace tmport::dos {
struct GameState;
}

namespace tmport::dos::autoplay {

inline constexpr int kCellPx = 128;

// Cells of the levels (code_world.md §3.3, §6.4-§6.8).
namespace cells {
inline constexpr unsigned kStartRoom = 0x02;      // clubhouse start cell (door Up on the left, prize room on the right)
inline constexpr unsigned kRankDoor = 0x01;       // clubhouse chest picture: Up leads to the rank screen
inline constexpr unsigned kShelfDoor = 0x04;      // walking in leads to the prize shelf rooms
inline constexpr unsigned kClubDoor = 0xF3;       // level 0: the clubhouse door (Up)
inline constexpr unsigned kTrunkGate = 0xEF;      // level 0 -> 1 (key state 2)
inline constexpr unsigned kTrunkTop = 0xBF;       // level 1: where the trunk arrives (Down climbs back)
inline constexpr unsigned kGeyserGate = 0xAE;     // level 1 -> 2 (key state 4)
inline constexpr unsigned kGeyserTop = 0x7E;      // level 2: the hole the geyser comes out of
inline constexpr unsigned kArchGate = 0x79;       // level 2 -> castle (key state 6)
inline constexpr unsigned kCastleDoor = 0x73;     // castle hall door
inline constexpr unsigned kNetShopL0 = 0xEA;
inline constexpr unsigned kNetShopL1 = 0xBC;
inline constexpr unsigned kCaveDoorW = 0xEB;      // level 0: Up in the left half -> the cave (08)
inline constexpr unsigned kBonusSpot = 0x26;      // cave's upper room: +5 coins at x & 0x7F == 0x48
inline constexpr unsigned kBridgeL1 = 0xB7;
inline constexpr unsigned kBridgeL2 = 0x84;
}  // namespace cells

// The y of the path line of a world row.
constexpr uint16_t pathY(unsigned row) { return static_cast<uint16_t>(row * kCellPx + 0x70); }
constexpr unsigned cellOf(uint16_t x, uint16_t y) { return ((y >> 7) << 4) | ((x >> 7) & 0xF); }
constexpr bool onPathLine(uint16_t y) { return (y & 0x7F) == 0x70; }

struct LevelLoop {
    int level = 0;
    std::vector<uint8_t> cells;  // canonical cells, walking right
    int lengthPx() const { return static_cast<int>(cells.size()) * kCellPx; }
};
const LevelLoop& levelLoop(int level);  // level 0..2

// The canonical cell of a path cell of `level` (twins mapped), nullopt when not on the loop.
std::optional<unsigned> canonicalCell(int level, unsigned cell);
// The cell's index in the level's loop, -1 when not on it.
int loopIndex(int level, unsigned cell);
// The loop position of a world point on the level's path rows (any y inside the cell's row), nullopt
// off the loop.
std::optional<int> loopPos(int level, uint16_t x, uint16_t y);
// The loop position of x & 0x7F == `offset` in `cell`; -1 when the cell is not on the loop.
int loopPosOf(int level, unsigned cell, int offset);
// The signed shortest way from a to b along the loop (positive: walk right).
int loopDelta(int level, int a, int b);
// The level whose loop holds `cell` (0..2), -1 for none.
int levelOfPathCell(unsigned cell);

// A stretch of the loop a walking player falls through (the hole of 7E unless jumping or sliding;
// a rope bridge from tier 2 on a fast CPU): loop positions [from, to] (inclusive, x & 0x7F ranges of
// the handlers: hole 0x18..0x57, bridge 0x30..0x4F). The bot jumps over them.
struct Hazard {
    unsigned cell = 0;
    int from = 0, to = 0;
};
std::vector<Hazard> hazards(const GameState& s, int level);

// --------------------------------------------------------------------------- the castle wall

// Rows 3..7 (3 = the top row with the exit, 7 = the hall floor), columns 2..5 (code_world.md §3.5,
// §6.8-§6.9): pictures 0x31 full ladder, 0x2D short ladder (lower 32 rows), 0x2A wall, 0x32 exit,
// 0x33/0x34 top wall, 0x27 hall ladder, 0x2C hall wall, 0x3D hall door.
struct CastleGrid {
    static constexpr int kTopRow = 3, kHallRow = 7, kFirstCol = 2, kCols = 4, kRows = 5;
    std::array<std::array<uint8_t, kCols>, kRows> pic{};
    uint8_t at(int row, int col) const { return pic[static_cast<size_t>(row - kTopRow)][static_cast<size_t>(col - kFirstCol)]; }
    static bool valid(int row, int col) { return row >= kTopRow && row <= kHallRow && col >= kFirstCol && col < kFirstCol + kCols; }
};
CastleGrid castleGrid(const GameState& s);

// One step of the route up the wall.
enum class CastleMove : uint8_t { Up, JumpLeft, JumpRight, Exit };
struct CastleStep {
    int row = 0, col = 0;  // where the move starts
    CastleMove move = CastleMove::Up;
};
// The shortest route (fewest moves, jumps cost 2) from the ladder at (row, col) to the exit; empty when
// none exists. Climbable: 0x31 and 0x27 lead up into 0x31, 0x2D or 0x32; any ladder cell (0x31, 0x2D,
// 0x27) can jump sideways onto a 0x31 or 0x2D of the same row.
std::vector<CastleStep> castleRoute(const CastleGrid& g, int row, int col);
// The hall ladder columns (0x27 in row 7).
std::vector<int> hallLadders(const CastleGrid& g);
// The castle part of the world: rows 3..7, columns 0..7 (the hall 70..76 and the wall).
bool inCastle(unsigned cell);

}  // namespace tmport::dos::autoplay
