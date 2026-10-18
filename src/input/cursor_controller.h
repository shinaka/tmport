#pragma once
// Cursor controller of the player module (10e0:0000 Player_UpdateCursor, 10e0:0063
// SetCursorForced; re/docs/code_10e0-10f0.md §4.2) and the mouse-directed walking latch used by
// the player controllers (10e0:0447 §4.3.1, castle climb 1168 WalkUpdate).
//
//  * update(): if the cursor is not forced and the last dispatched mouse position is over the
//    play-field (0 <= x <= 512, 20 <= y < 287): when the position changed since the last call,
//    show walk cursor 1 (moving left, EXE cursor 129) or 2 (otherwise, cursor 130). Anywhere else,
//    or while forced, show the system arrow. Then remember the position.
//  * setForced(f): called with true before every modal screen and false after; resets the
//    remembered position to (-1,-1) and runs update().
//  * installAsMoveHook(): Room_Run (10e0:0116) installs update() as the mouse-move hook; nothing
//    removes it again in the original.

#include <cstdint>

#include "input/cursors.h"
#include "input/input_codes.h"
#include "input/input_queue.h"

namespace tmport {

class CursorController {
public:
    static constexpr int kWalkLeftCursor = 1;   // index into the set {128, 129, 130}
    static constexpr int kWalkRightCursor = 2;

    CursorController(InputQueue& input, CursorSet& cursors) : input_(input), cursors_(cursors) {}

    // 10e0:0000 Player_UpdateCursor.
    void update() {
        const int mx = input_.mouseX(), my = input_.mouseY();
        if (!forced_ && mx >= 0 && mx <= 512 && my >= 20 && my < 287) {
            if (mx != lastX_ || my != lastY_) cursors_.select(mx < lastX_ ? kWalkLeftCursor : kWalkRightCursor);
        } else {
            cursors_.setArrow();
        }
        lastX_ = mx;
        lastY_ = my;
    }

    // 10e0:0063 SetCursorForced (DS:06e2).
    void setForced(bool forced) {
        forced_ = forced;
        lastX_ = -1;
        lastY_ = -1;
        update();
    }
    bool forced() const { return forced_; }

    // 1020:00be Mouse_SetMoveHook(10e0:0000) as done by Room_Run.
    void installAsMoveHook() {
        input_.setMouseMoveHook([this] { update(); });
    }

    int lastX() const { return lastX_; }
    int lastY() const { return lastY_; }

private:
    InputQueue& input_;
    CursorSet& cursors_;
    bool forced_ = false;
    int lastX_ = 0, lastY_ = 0;  // DS lastMouseX/Y (zero in the image until Room_Run sets -1)
};

// Mouse-directed walking (hot spot 0x600 "latched while the button is held"). The player
// controllers decode their intent once per tick:
//    code = latch.resolve(inputEvent, input);   // steps 1-2 of 10e0:0447 §4.3.1
//    if ((code & 0xFFF) is one of the play-field spots) { steer toward the mouse; latch.set(); }
// The room controller latches on 0x600/0x601/0x602, the castle climb only on 0x600.
class MouseWalkLatch {
public:
    static constexpr uint16_t kPlayField = 0x600;
    static constexpr uint16_t kLeftEdge = 0x601;
    static constexpr uint16_t kRightEdge = 0x602;

    // If no input event arrived this tick: 0x600 while latched and the button is still down,
    // otherwise Hotspot_PollMouseDown (1098:017e). Any input event clears the latch.
    uint16_t resolve(uint16_t inputEvent, InputQueue& input) {
        if (inputEvent != 0) {
            latched_ = false;
            return inputEvent;
        }
        if (latched_ && input.mouseButtonDown()) return kPlayField;
        return input.pollMouseDown();
    }
    void set() { latched_ = true; }
    void clear() { latched_ = false; }
    bool latched() const { return latched_; }

private:
    bool latched_ = false;
};

}  // namespace tmport
