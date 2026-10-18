#pragma once
// The player-character controller of the room tick: Player_Update (10e0:0447) and its helpers
// Player_WalkTo (10e0:1033). Walking, jumping, mouse-directed walking, the net swing (Space), the
// coin toss (Enter), doors, the locked gates, the net-shop walk, the room-36 fountain bump, walls
// and room exits, with the original's state precedence (re/docs/code_10e0-10f0.md §4.3,
// ARCHITECTURE.md §5.2-5.3, REFERENCE_CAPTURES.md §4.4/§4.6, verified against the export).
//
// One call per 100 ms tick. Held arrows are polled once per tick (the caller's PlayerInput reads
// the flags dispatched by the last input fetch).

#include <cstdint>
#include <functional>

#include "game/game_hooks.h"
#include "game/game_state.h"
#include "game/game_tables.h"
#include "game/room/room_state.h"
#include "game/room/room_tables.h"

namespace tmport::game::room {

// The input the controller polls (DS:06ee..06f8 held arrows and mouse, 1020:0106, 1098:017e).
class PlayerInput {
public:
    virtual ~PlayerInput() = default;
    virtual bool upHeld() = 0;
    virtual bool downHeld() = 0;
    virtual bool leftHeld() = 0;
    virtual bool rightHeld() = 0;
    virtual int mouseX() = 0;
    virtual int mouseY() = 0;
    virtual bool mouseButtonDown() = 0;   // 1020:0106 Mouse_IsButtonDown
    virtual uint16_t pollMouseDown() = 0; // 1098:017e Hotspot_PollMouseDown
};

// Synthetic input for tests and tools.
struct ScriptedPlayerInput final : PlayerInput {
    bool up = false, down = false, left = false, right = false;
    int mx = 0, my = 0;
    bool button = false;
    uint16_t mouseDownCode = 0;
    bool upHeld() override { return up; }
    bool downHeld() override { return down; }
    bool leftHeld() override { return left; }
    bool rightHeld() override { return right; }
    int mouseX() override { return mx; }
    int mouseY() override { return my; }
    bool mouseButtonDown() override { return button; }
    uint16_t pollMouseDown() override { return button ? mouseDownCode : uint16_t{0}; }
};

// 10e0:0447 coin landing slot: clamp((facingLeft ? x - dxLeft[54] : x + dxRight[54]) / 170, 0, 2)
// with C (truncating) division.
int coinTargetSlot(int x, bool facingLeft, const RoomTables& t);

class PlayerController {
public:
    PlayerController(GameState& s, const GameTables& t, const RoomTables& rt, const FrameSizes& sizes, PlayerRuntime& p,
                     SceneRuntime& scene, GameHooks& hooks)
        : s_(s), t_(t), rt_(rt), sizes_(sizes), p_(p), scene_(scene), hooks_(hooks) {}

    // GateLock_Draw(0x180), called when the door step-up at the gate ends (10e0:0538).
    void setStampGateLock(std::function<void()> f) { stampGateLock_ = std::move(f); }

    // 10e0:0447 Player_Update. Reads p.inputEvent (this tick's input code; may be replaced by a
    // mouse code as in the original).
    void update(PlayerInput& in);

    // 10e0:1033 Player_WalkTo.
    void walkTo(int x, int y, uint8_t arg3, uint8_t arriveAction);

private:
    void moveFrame(bool left);   // x -= dxLeft[f] / += dxRight[f], y = frameY[f]
    void boundsAndExits();       // 10e0:0da7: walls, room-36 obstacle, exits
    void tail();                 // 10e0:0f55: entry pose for type 1/2, frame size
    void netSwing();
    void emptySwing();
    void coinToss(int slot);
    void walkStep();
    void freeMovement(bool left, bool right, bool up, int slot, PlayerInput& in);

    int16_t dxR(int f) const { return f >= 0 && f < kPlayerFrameSlots ? rt_.dxRight[static_cast<size_t>(f)] : int16_t{0}; }
    int16_t dxL(int f) const { return f >= 0 && f < kPlayerFrameSlots ? rt_.dxLeft[static_cast<size_t>(f)] : int16_t{0}; }
    int16_t fy(int f) const { return f >= 0 && f < kPlayerFrameSlots ? rt_.frameY[static_cast<size_t>(f)] : int16_t{0}; }
    uint8_t next(int f) const { return f >= 0 && f < kPlayerFrameSlots ? rt_.nextFrame[static_cast<size_t>(f)] : uint8_t{0}; }

    GameState& s_;
    const GameTables& t_;
    const RoomTables& rt_;
    const FrameSizes& sizes_;
    PlayerRuntime& p_;
    SceneRuntime& scene_;
    GameHooks& hooks_;
    std::function<void()> stampGateLock_;
};

}  // namespace tmport::game::room
