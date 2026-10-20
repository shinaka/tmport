#pragma once
// The puzzles' pointer warp on a windowed host (re/gg/docs/PORTING_NOTES.md §8, code_puzzles_b.md §9).
// Gears (3b2f:098f) and magnets (35bb:0103) move the pointer themselves: g_mouseX/Y and INT 33h AX=04h.
// GgMouse::warp does both on the engine's driver model. On a window the host pointer must follow, or
// the next host motion event would put the driver back where the real pointer is (the original's
// driver moved its own pointer, so later mickeys continue from the warped place). PointerWarpHook sets
// GgMouse::onWarp for as long as it lives: in real time with a window it moves the host pointer to the
// warped canvas pixel's centre (SDL_WarpMouseInWindow; the motion event this produces maps back to the
// same driver position). Headless and deterministic runs (tests, tools) install nothing: the driver
// model is the pointer there.

#include <cstdint>
#include <functional>

namespace tmport::gg {

class GgGame;

class PointerWarpHook {
public:
    explicit PointerWarpHook(GgGame& g);
    ~PointerWarpHook();
    PointerWarpHook(const PointerWarpHook&) = delete;
    PointerWarpHook& operator=(const PointerWarpHook&) = delete;

    bool installed() const { return installed_; }

private:
    GgGame& g_;
    std::function<void(int16_t, int16_t)> previous_;
    bool installed_ = false;
};

}  // namespace tmport::gg
