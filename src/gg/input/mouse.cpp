#include "gg/input/mouse.h"

#include <algorithm>

namespace tmport::gg {

dos::Int33MouseConfig GgMouse::driverConfig() {
    dos::Int33MouseConfig c;
    c.minX = 0;
    c.maxX = 639;
    c.minY = 0;
    c.maxY = 199;
    c.xMul = 2;
    c.xDiv = 1;
    // The driver's reset (Mouse_DriverPresent 2c08:0011, AX=0) centres the pointer; DOSBox-X's state
    // after it holds (320, 100) (REFERENCE_CAPTURES.md §2.2).
    c.startX = 320;
    c.startY = 100;
    return c;
}

GgMouse::GgMouse(GgMouseOptions options) : options_(options), driver_(driverConfig()) {}

void GgMouse::installHandler() {
    // 1454:0000: INT 33h AX=0Ch, CX=1Fh, ES:DX = 17a0:0004.
    driver_.setEventHandler(kEventMask, [this](const dos::Int33Mouse::Event& e) { onEvent(e); });
}

void GgMouse::removeHandler() {
    // 1454:0058: INT 33h AX=0Ch with CX = 0.
    driver_.setEventHandler(0, {});
}

void GgMouse::setPosition(int16_t px, int16_t py) {
    if (screen_) {
        screen_->setCursorPosition(px, py);
    } else {
        x_ = px;
        y_ = py;
    }
}

// 17a0:009b Mouse_MoveCursor(CX, DX)
void GgMouse::moveCursor(int16_t px, int16_t py) {
    if (screen_) screen_->moveCursor(px, py);
    else setPosition(px, py);
}

// 17a0:0004 Mouse_EventHandler. CX and DX are working registers: every latch adds the hot spot to
// them in place, so an event that is a processed move with the left button alone held AND a press
// or release latches the second position with the hot spot added twice (one driver call reporting
// both, e.g. "moved and pressed"; the arrow's hot spot is (0, 0)).
void GgMouse::onEvent(const dos::Int33Mouse::Event& e) {
    ++events_;
    buttons_ = e.buttons;                         // 17a0:0011 g_mouseButtons = BX
    uint16_t cx = static_cast<uint16_t>(e.x >> 1);  // 17a0:0018 SHR CX,1
    uint16_t dx = e.y;
    const int16_t hx = screen_ ? screen_->hotX() : 0;
    const int16_t hy = screen_ ? screen_->hotY() : 0;
    if (e.events & dos::Int33Mouse::kEventMove) {
        // 17a0:001e: only while g_mouseHideState is 0.
        if (options_.dropMovesWhileDrawing && screen_ && screen_->drawing()) {
            ++dropped_;
        } else {
            moveCursor(static_cast<int16_t>(cx), static_cast<int16_t>(dx));
            cx = static_cast<uint16_t>(x());  // 17a0:00b5: CX, DX reloaded from g_mouseX/Y
            dx = static_cast<uint16_t>(y());
            if (buttons_ == 1) {
                // 17a0:0029: a drag with the left button alone updates the press latch.
                press_.kind = 1;
                cx = static_cast<uint16_t>(cx + hx);
                press_.x = static_cast<int16_t>(cx);
                dx = static_cast<uint16_t>(dx + hy);
                press_.y = static_cast<int16_t>(dx);
            }
        }
    }
    if (e.events & dos::Int33Mouse::kEventLeftDown) {
        // 17a0:0043: the press; the release bit of the same event is not examined.
        press_.kind = 1;
        cx = static_cast<uint16_t>(cx + hx);
        press_.x = static_cast<int16_t>(cx);
        dx = static_cast<uint16_t>(dx + hy);
        press_.y = static_cast<int16_t>(dx);
        press_.first = 1;  // g_mouseNewPress DS:0a1e
        release_.kind = 0;
    } else if (e.events & dos::Int33Mouse::kEventLeftUp) {
        // 17a0:0068: the release, stamped with g_ticks; the held flag drops.
        release_.kind = 2;
        cx = static_cast<uint16_t>(cx + hx);
        release_.x = static_cast<int16_t>(cx);
        dx = static_cast<uint16_t>(dx + hy);
        release_.y = static_cast<int16_t>(dx);
        release_.tick = ticks_ ? ticks_() : 0;
        press_.kind = 0;
    }
    // The right-button bits are not examined (only g_mouseButtons shows the right button).
}

void GgMouse::readDriverPosition() {
    // 17ab:002a: INT 33h AX=03h; g_mouseX = CX >> 1, g_mouseY = DX.
    setPosition(static_cast<int16_t>(driver_.x() >> 1), static_cast<int16_t>(driver_.y()));
}

void GgMouse::setBounds(int16_t x, int16_t y, int16_t w, int16_t h, int16_t cursorW, int16_t cursorH) {
    // 17ab:017f Mouse_SetBounds
    maxX_ = static_cast<int16_t>(x + w - cursorW);
    maxY_ = static_cast<int16_t>(y + h - cursorH);
    // Signed compares, no lower clamp.
    setPosition(std::min(this->x(), maxX_), std::min(this->y(), maxY_));
    // INT 33h AX=07h: CX = x (not doubled: every caller passes 0), DX = 2 * maxX; AX=08h: y, maxY.
    driver_.setRangeX(x, static_cast<int16_t>(maxX_ * 2));
    driver_.setRangeY(y, maxY_);
}

void GgMouse::warp(int16_t x, int16_t y) {
    // 35bb:010e: g_mouseX/Y, then INT 33h AX=04h with CX = 2x.
    setPosition(x, y);
    driver_.setPosition(static_cast<int16_t>(x * 2), y);
    if (onWarp) onWarp(x, y);
}

}  // namespace tmport::gg
