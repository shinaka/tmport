#pragma once
// Tables of the room tick (segments 10e0 player, 10e8 HUD, 1178 elves, 1190 treasure hall, 1198
// scene), read at run time from the user's TMTWINCD.EXE (DGROUP; offsets in data/dgroup_offsets.h,
// layouts in re/docs/dgroup_map.md §4.8-4.10, §4.18-4.19). Nothing here is game data compiled into
// the engine; the constants below are code constants of the original (immediate operands).
// Specs: re/docs/code_10e0-10f0.md §3.4-3.5, code_1168-1198.md §4.1, §6.2, §7.1, §8.1,
// code_10f8-1148.md §4.1, §7.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace tmport {
class ExeTables;
}

namespace tmport::game::room {

// --- Code constants (immediate operands; facts about the code, not data) -------------------------
inline constexpr int kPlayerFrameSlots = 85;        // g_frameW/H[85] (DS:5cd6 / 5d80)
inline constexpr int kPlayerBndl = 300;             // 1198:0000
inline constexpr int kHudBndl = 301;                // 10e8:0000
inline constexpr int kShopSignBndl = 302;           // 1198:0085
inline constexpr int kGateLockBndl = 415;           // 1188:01ed
inline constexpr int kFountainBndl = 328;           // 1198:0097
inline constexpr int kCoinPilePmap = 1699;          // 1110:0000
inline constexpr int kElfProjectileBndl = 310;      // 1178:02f5
// Elf animation sets 0..6 -> BNDL (1178:0251).
inline constexpr std::array<int, 7> kElfSetBndl = {316, 317, 312, 315, 320, 314, 311};
inline constexpr int kElfSetFlee = 4, kElfSetThrow = 5, kElfSetNet = 6;
inline constexpr int kMaxElves = 32;                // Elf[32] at DS:6386

// Hot spots of Room_Run (10e0:007d) and the clue box (10e8:0658).
inline constexpr uint16_t kHotPlayField = 0x600;    // (0,20,512,267)
inline constexpr uint16_t kHotLeftEdge = 0x601;     // (0,20,0,267): zero width, never hit
inline constexpr uint16_t kHotRightEdge = 0x602;    // (512,20,0,267): zero width, never hit
inline constexpr uint16_t kHotClueBox = 99;         // (10,296,190,76)
inline constexpr uint16_t kHotFullScreen = 0x409;   // (0,0,512,384)
inline constexpr uint16_t kHotEnterClick = 0x0D;    // removed by Room_Run on exit

// Layers (FrameBuffer rows): back page and pristine play-field offsets.
inline constexpr int kLayer1 = 0x180;
inline constexpr int kLayer2 = 0x300;
inline constexpr int kPlayTop = 20, kPlayW = 512, kPlayH = 267;
inline constexpr int kHudTop = 287;

// Spot pictures (1198:0234 / 0305): 118 x 133 at screen y 90; revealed object at y 152.
inline constexpr int kSlotY = 90, kSlotW = 118, kSlotH = 133, kRevealY = 152;

// Room numbers with fixed roles.
inline constexpr int kFountainRoom = 36;            // 0x24
// Gate lock sprite (1188:0220 / 0267): 20 x 25, per gate room.
struct GateLockPos {
    int room, x, y;
};
inline constexpr std::array<GateLockPos, 3> kGateLocks = {{{11, 342, 143}, {25, 410, 143}, {38, 155, 156}}};
inline constexpr int kGateLockW = 20, kGateLockH = 25;
// Net-shop niche (10f8:0000): rect 87 x 127, "net inside" PMAP, "empty niche" PMAP = +1.
struct NicheDef {
    int room, x, y, netPmap;
};
inline constexpr std::array<NicheDef, 3> kNiches = {{{13, 219, 101, 1320}, {24, 106, 103, 1322}, {36, 373, 91, 1324}}};
inline constexpr int kNicheW = 87, kNicheH = 127;
inline constexpr int kSignY = 24, kSignW = 87, kSignH = 42;  // 10f8:0285
// Fountain with frogs (1198:06b4 / 06d5): 128 x 108 at (184,179) in room 36.
inline constexpr int kFountainX = 184, kFountainY = 179, kFountainW = 128, kFountainH = 108;
inline constexpr int kCoinPileY = 230;              // 1110:0059 / 007d
// Treasure hall room 0 pedestal (1190:02a0).
inline constexpr int kHallRoom0X = 358, kHallRoom0Y = 73, kHallPedCount = 6, kHallTouchW = 30;

// Player controller constants (10e0:0447).
inline constexpr int kWallLeftX = 75, kWallRightX = 437;
inline constexpr int kExitLeftX = -40, kExitRightX = 482;
inline constexpr int kSlotWidth = 170;              // thirds of the screen
inline constexpr int kCoinLandFrame = 54;           // dx of frame 54 gives the coin landing x
inline constexpr int kStandFrame = 0, kFrontFrame = 60, kJumpFirst = 12, kJumpLast = 21;
inline constexpr int kObstacleLeft = 176, kObstacleRight = 326, kObstacleFootW = 70;
inline constexpr int kObstacleBounceRight = 106, kObstacleBounceLeft = 326;
inline constexpr int kWalkY = 142;                  // y of every auto-walk target
inline constexpr int kWalkStepX = 5, kWalkStepY = 3;
inline constexpr int kNetReachRight = 99, kNetReachLeft = -98;
// Gate auto-walk targets and destinations per gate room (10e0:0d18, 10e0:09ad).
struct GateWalk {
    int room, walkX, toRoom;
};
inline constexpr std::array<GateWalk, 3> kGateWalks = {{{11, 330, 23}, {25, 281, 36}, {38, 170, 49}}};

// One elf behaviour script (frames i8 terminated by -1 = loop or -2 = leave; dx / absolute y per
// step; optional sfx per step, -1 = none).
struct ElfScript {
    std::vector<int8_t> frames;  // including the terminator
    std::vector<int16_t> dx;     // one entry per step before the terminator
    std::vector<int16_t> y;
    std::vector<int16_t> sfx;    // empty: no sounds
};

struct RoomTables {
    // --- player (DS:0ba0..0e89) ------------------------------------------------------------------
    std::array<uint8_t, 12> netSwing{};      // DS:0ba0, 1-based
    std::array<uint8_t, 12> emptySwing{};    // DS:0bac, 1-based
    std::array<uint8_t, 14> coinToss{};      // DS:0bb8, 1-based
    std::array<uint8_t, 14> coinFx{};        // DS:0bc6
    std::array<uint8_t, 3> door{};           // DS:0bd4, 1-based
    std::array<uint8_t, 13> bump{};          // DS:0bd7, 1-based
    std::array<int16_t, 13> bumpDxRight{};   // DS:0be4
    std::array<int16_t, 13> bumpDxLeft{};    // DS:0bfe
    std::array<int16_t, 7> obstacleMinXRight{};  // DS:0c18 (jump frames 15..21)
    std::array<int16_t, 7> obstacleMaxXLeft{};   // DS:0c26
    std::array<uint8_t, kPlayerFrameSlots> nextFrame{};  // DS:0c34
    std::array<int16_t, kPlayerFrameSlots> dxRight{};    // DS:0c89 (added facing right)
    std::array<int16_t, kPlayerFrameSlots> dxLeft{};     // DS:0d33 (subtracted facing left)
    std::array<int16_t, kPlayerFrameSlots> frameY{};     // DS:0ddd (absolute y)
    std::array<int16_t, 3> shopWalkX{};      // DS:0e89

    // --- HUD (DS:0ec3..0f66) -----------------------------------------------------------------------
    std::array<uint8_t, 4> gemsNeeded{};                 // DS:0ec3
    std::array<std::array<int16_t, 5>, 4> gemX{};        // DS:0ec7
    std::array<std::array<int16_t, 5>, 4> gemY{};        // DS:0eef
    std::vector<uint8_t> answerPrompt;                   // DS:0f17 (clue box mode 1 text)
    std::vector<uint8_t> tmplCoins, tmplNets, tmplPoints;  // DS:0f5a / 0f60 / 0f66 (with the NUL)

    // --- net shop -------------------------------------------------------------------------------------
    std::array<int16_t, 3> netSignX{};       // DS:1066

    // --- elves (DS:2ac8..2c7d) --------------------------------------------------------------------
    std::array<uint8_t, 3> elfStartRoom{};   // DS:2ac8
    std::array<ElfScript, 5> elfScripts;     // behaviours 0..4 (DS:2acb, 2b1c, 2b6d, 2ba0, 2bf1)
    std::array<int8_t, 10> elfNetFrames{};   // DS:2c31
    std::array<int16_t, 9> elfNetDx{};       // DS:2c3b
    std::array<int16_t, 9> elfNetY{};        // DS:2c4d
    std::array<int16_t, 5> elfThrowDxRight{};  // DS:2c5f
    std::array<int16_t, 5> elfThrowDxLeft{};   // DS:2c69
    std::array<int16_t, 5> elfThrowY{};        // DS:2c73
    std::array<int16_t, 9> elfProjY{};         // DS:2c7d

    // --- treasure hall, scene (DS:2d90..2dda) -------------------------------------------------------
    std::array<int16_t, 6> hallPedX{};       // DS:2d90
    std::array<int16_t, 6> hallPedY{};       // DS:2d9c
    std::array<int16_t, 6> hallPedFrameBase{};  // DS:2da8
    std::array<int16_t, 3> slotX{};          // DS:2dd4
    std::array<int16_t, 3> revealX{};        // DS:2dda

    // --- Screen_DissolveCopy (1070:002d; transition 2, mods only) -----------------------------------
    std::array<uint8_t, 16> dissolveColOrder{};  // DS:09ae
    std::array<uint8_t, 16> dissolveRowShift{};  // DS:09be

    // Everything from the user's EXE. Throws tmport::Error on a malformed table.
    static RoomTables fromExe(const ExeTables& exe);
};

// Player sprite sizes (g_frameW / g_frameH, filled from the PMAP headers of BNDL 300 by
// Scene_LoadCommon 1198:0000; slots past the bundle stay 0).
struct FrameSizes {
    std::array<int16_t, kPlayerFrameSlots> w{};
    std::array<int16_t, kPlayerFrameSlots> h{};
    int16_t width(int f) const { return f >= 0 && f < kPlayerFrameSlots ? w[static_cast<size_t>(f)] : int16_t{0}; }
    int16_t height(int f) const { return f >= 0 && f < kPlayerFrameSlots ? h[static_cast<size_t>(f)] : int16_t{0}; }
};

// Bytes before the first NUL.
std::span<const uint8_t> untilNul(std::span<const uint8_t> b);

}  // namespace tmport::game::room
