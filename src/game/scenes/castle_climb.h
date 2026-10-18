#pragma once
// Castle-wall ladder climb, room 49 (segment 1168 climb.cpp: ClimbScene_Run 1168:0000 and its
// helpers; re/docs/code_1168-1198.md §2, ARCHITECTURE.md §2.9, §5.13, dgroup_map.md §4.17).
//
// CastleClimb is the deterministic part (the 1168 globals and the per-tick updates); it changes
// GameState (player pose, coins/nets, hints) and reports effects through GameHooks, so tests can
// drive it with synthetic input. runCastleClimb() is the scene: resources, drawing, the 10 Hz
// walk / climb loops, the Exit dialog on Esc; it ends with g_room := 0xFE (the climb is over).
//
//   screen 0   walking like a normal room (BNDL 300 frames): Space / Enter still spend a net / a
//              coin and play the animation; walking right adds dxLeft (DS:0d33) -- the original's
//              copy-paste quirk at 1168:09e3, kept: a round-2 capture of the original walking right
//              on this screen moves +30 +8 +15 +6 +23 +12 +17 -1 per tick (dxLeft), pixel-exact;
//              Up under a ladder column starts climbing;
//   screens 1-3 ladder grids (one of R(tier+1) layouts): climb phase cycle of 4 frames, Left /
//              Right jump to the next column (4 ticks), no ladder -> 7-step fall cycle until a
//              ladder is grabbed or (screen 0) the landing (6 ticks, back to walking);
//              y < -35 -> next screen up, y > 231 -> screen down; off the top of screen 3 = done;
//   creature   the Master of Mischief (BNDL 326): spawn chance per tick DS:2aaa[tier] permille on
//              screens 1-3 in a column without a full ladder; 12-tick emerge / hold / retreat;
//              at full reach he pushes a climber on the adjacent column: jump away, -1 coin.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "app/app_context.h"
#include "game/game_hooks.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/scenes/scene_support.h"
#include "rtl/borland_rand.h"

namespace tmport {
class ExeTables;
class ResourceManager;
}  // namespace tmport

namespace tmport::game::scenes {

namespace castle {
inline constexpr int kScreens = 4;
inline constexpr int kColumns = 5;
inline constexpr int kClimbBndl = 321;    // 0x141: 24 climber frames
inline constexpr int kCreatBndl = 326;    // 0x146: 4 creature frames
inline constexpr int kPlayerBndl = 300;   // walking frames
inline constexpr int kLadderFull = 1090, kLadderLow = 1091, kLadderStub = 1092;
inline constexpr int kBackground0 = 1040, kBackground = 1041;
inline constexpr int kLadderDx = -29;                  // ladder sprite x = colX - 29
inline constexpr int kLadderFullY = 20, kLadderLowY = 141, kLadderStubY = 254;
inline constexpr int kClimbXOffset = -30;              // climber x = colX + climbDx - 30
inline constexpr int kClimbStartY = 120;               // 0x78
inline constexpr int kCreatXOffset = -41;              // creature x = colX - 41 (+ flip anchor)
inline constexpr int kCreatReach = 52;                 // hit test uses creatY + 52
inline constexpr int kCreatHitH = 110;
inline constexpr int kHalfLadderMinY = 0x55;           // a half ladder holds below y 85
inline constexpr int kLandY = 0x6d;                    // falling on screen 0 lands below y 109
inline constexpr int kScreenUpY = -35, kScreenDownY = 231, kScreenH = 267;
inline constexpr int kWalkMinX = 16, kWalkMaxRight = 496;  // x < 16 -> 15; x + w > 496 -> 497 - w
// Hot spots (1168:0442 / 0a6d).
inline constexpr uint16_t kSteer = 0x600, kLeftEdge = 0x601, kRightEdge = 0x602, kFloorClick = 0x0D;
// Sound effects.
inline constexpr int kSfxNetWhoosh = 0x1e, kSfxNet = 6, kSfxCoinToss = 0x22, kSfxCoin = 0;
inline constexpr int kSfxClimb1 = 0x1b, kSfxClimb3 = 0x19, kSfxFall = 0x21, kSfxLand = 0x28;
inline constexpr int kSfxCreatEmerge = 0x1d, kSfxCreatReach = 5;
inline constexpr int kMusicZone = 7;
}  // namespace castle

// DGROUP tables of 1168 (dgroup_map.md §4.17; 1-based tables keep a dummy element 0) and the
// player walk tables of 10e0 that WalkUpdate reuses (DS:0ba0..0ddd).
struct CastleTables {
    std::array<std::array<std::array<int16_t, castle::kColumns>, castle::kScreens>, 4> layouts{};  // DS:2950
    std::array<int16_t, castle::kColumns> colX{};  // DS:29f0
    std::array<uint8_t, 4> climbFrames{};          // DS:29fa (low bytes)
    std::array<int16_t, 4> climbDx{};              // DS:2a02
    std::array<int16_t, 4> climbUpDy{};            // DS:2a0a
    std::array<int16_t, 4> climbDownDy{};          // DS:2a12
    std::array<uint8_t, 5> jumpFrames{};           // DS:2a1a idx 0..4
    std::array<int16_t, 4> jumpDxLeft{};           // DS:2a24 idx 0..3 (magnitudes)
    std::array<int16_t, 5> jumpDxRight{};          // DS:2a2c idx 1..4
    std::array<int16_t, 5> jumpDy{};               // DS:2a36 idx 0..4
    std::array<uint8_t, 8> fallFrames{};           // step 1..7 (code base DS:2a3e)
    std::array<int16_t, 8> fallDx{};               // base DS:2a4c
    std::array<int16_t, 8> fallDy{};               // base DS:2a5a (before +32)
    std::array<uint8_t, 7> landFrames{};           // step 1..6 (base DS:2a68)
    std::array<int16_t, 7> landDx{};               // base DS:2a74
    std::array<int16_t, 7> landY{};                // base DS:2a80 (absolute)
    std::array<int16_t, 13> creatSeq{};            // tick 1..12 (base DS:2a8c), -1 = hidden
    std::array<int16_t, 4> spawnPermille{};        // DS:2aaa by rank tier
    // 10e0 player tables.
    std::array<uint8_t, 85> nextFrame{};           // DS:0c34
    std::array<int16_t, 85> dxRight{};             // DS:0c89
    std::array<int16_t, 85> dxLeft{};              // DS:0d33
    std::array<int16_t, 85> frameY{};              // DS:0ddd
    std::array<uint8_t, 12> netFrames{};           // DS:0ba0 step 1..11
    std::array<uint8_t, 12> emptySwingFrames{};    // DS:0bac step 1..11
    std::array<uint8_t, 14> coinFrames{};          // DS:0bb8 step 1..13
    std::array<uint8_t, 14> coinFxFrames{};        // DS:0bc6 step 1..13

    static CastleTables fromExe(const ExeTables& exe);
};

// Frame sizes the logic uses (from the PMAP headers of BNDL 300 / 321 / 326).
struct CastleSizes {
    std::array<int16_t, 85> playerW{}, playerH{};  // g_frameW / g_frameH (BNDL 300)
    std::array<int16_t, 24> climbW{}, climbH{};    // cl_climbW / cl_climbH (BNDL 321)
    std::array<int16_t, 4> creatW{}, creatH{};     // cl_creatW / cl_creatH (BNDL 326)

    static CastleSizes fromResources(const ResourceManager& res);
};

// What a tick reads besides the input code: the held arrows (DS:06ee..06f4), the mouse
// (DS:06f6/06f8, button DS:06e3) and Hotspot_PollMouseDown (1098:017e).
struct TickSense {
    bool left = false, right = false, up = false, down = false;
    bool mouseDown = false;
    int mouseX = 0, mouseY = 0;
    std::function<uint16_t()> pollMouseDown;   // empty: 0
    std::function<void(int&, int&)> readMouse; // the mouse position after the poll; empty: mouseX/Y
};

struct CastleOptions {
    // 1168:09e3 adds dxLeft (DS:0d33) when walking right on screen 0 (ARCHITECTURE.md §8 item 1).
    // Confirmed by the round-2 capture of the original (castle walk right/left matches pixel for
    // pixel with this on); false is a comfort option that walks like Player_Update.
    bool rightWalkUsesDxLeft = true;
};

class CastleClimb {
public:
    CastleClimb(GameState& s, const GameTables& gt, const CastleTables& t, const CastleSizes& z, GameHooks& hooks,
                BorlandRand& rng, CastleOptions options = {});

    // --- the 1168 globals (DS:6178..6224, 2aa6/2aa8) ------------------------------------------
    int screen = 0;          // cl_screen 0..3
    int nextScreen = 0;      // cl_nextScreen
    int nextY = 0;           // cl_nextY
    int column = 0;          // cl_column
    int climbPhase = 0;      // cl_climbPhase 0..3, -1 = re-attach
    int jumpStep = 0;        // cl_jumpStep 1..4
    int fallStep = 0;        // cl_fallStep 1..7
    int landStep = 0;        // cl_landStep 1..6
    bool onLadder = false;   // cl_onLadder
    bool done = false;       // cl_done
    int layout = 0;          // index of cl_layout in DS:2950
    int creatTick = 0;       // cl_creatTick 0 = inactive, 1..13
    int creatX = 0, creatY = 0;
    int creatFrame = 0;      // -1 = hidden
    int creatFlip = 0;       // 1 = faces left
    int creatCol = 0;
    int creatScreen = 0;
    bool walkMouseFollow = false;   // DS:2aa6
    bool climbMouseFollow = false;  // DS:2aa8
    // The 10e0 player globals WalkUpdate reuses.
    uint8_t jumping = 0;     // g_jumping
    int coinStep = 0;        // g_coinStep 1..13
    int netStep = 0;         // g_netStep 1..11
    int emptySwingStep = 0;  // g_emptySwingStep 1..11
    int fxFrame = 0, fxX = 0, fxY = 0, fxW = 0, fxH = 0;  // g_fxFrame / X / Y / W / H

    int cell(int scr, int col) const;
    // 1168:0047 ClimbScene_PickLayout: layout = R(tier + 1).
    void pickLayout();
    // 1168:03d6 ClimbScene_FindLadderAtPlayer (sets column).
    bool findLadderAtPlayer();
    // 1168:0442 WalkLoop entry: frame 0, y = frameY[0], facing right, size of frame 0, cleared
    // flags and animations (x is kept).
    void startWalk();
    // 1168:05b5 ClimbScene_WalkUpdate.
    void walkUpdate(uint16_t input, const TickSense& sense);
    // 1168:0a6d ClimbLoop entry: frame 0 at colX[column] - 30, y 120, facing right; creature,
    // jump, fall, land, phase cleared; climbing always starts on screen 0.
    void startClimb();
    // 1168:1143 ClimbScene_UpdateCreature.
    void updateCreature();
    // 1168:0c04 ClimbScene_ClimbUpdate.
    void climbUpdate(uint16_t input, const TickSense& sense);
    // One ClimbLoop tick of logic (UpdateCreature + ClimbUpdate). True while the inner loop goes on
    // (on a ladder and no screen change requested).
    bool climbTick(uint16_t input, const TickSense& sense);
    // After the inner loop: a requested screen change (true: the background must be redrawn) or,
    // past screen 3, done.
    bool finishScreenChange();

private:
    uint16_t resolveInput(uint16_t input, const TickSense& sense, bool& follow, int& mx, int& my) const;

    GameState& s_;
    const GameTables& gt_;
    const CastleTables& t_;
    const CastleSizes& z_;
    GameHooks& hooks_;
    BorlandRand& rng_;
    CastleOptions options_;
};

// 1168:0000 ClimbScene_Run: the whole castle-wall scene. Ensures music zone 7 (GameMain normally
// started it on entering room 49), plays until the climber leaves screen 3 at the top and sets
// g_room := 0xFE. Esc runs the Exit dialog (QuitGame on Yes). Uses SceneHooks::rng (default
// ctx.engine.rng()).
void runCastleClimb(AppContext& ctx, GameState& s, const GameTables& t, const SceneHooks& hooks = {},
                    CastleOptions options = {});

}  // namespace tmport::game::scenes
