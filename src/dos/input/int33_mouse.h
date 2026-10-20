#pragma once
// A DOS mouse driver as a game sees it through INT 33h (both DOS games; the DOSBox-X driver of the
// reference captures). The engine feeds it the host pointer in logical pixels of the 320x200 screen
// (absolute; mickey ratios do not apply) and the host buttons; the game reads it by polling or
// through an event handler:
//
//   AX=03h  position and buttons (x(), y(), buttons())         TM DOS Mouse_Poll 130f:0D94
//   AX=04h  set the position (setPosition)
//   AX=07h  x range, AX=08h y range (setRangeX/Y; the position is clamped into them)
//   AX=0Ch  event handler (setEventHandler): called with the event bits (AX), the buttons (BX) and
//           the position (CX, DX) whenever a subscribed event happens; G&G registers
//           Mouse_EventHandler 17a0:0004 with mask 1Fh (re/gg/docs/code_runtime.md §7.1-7.2)
//
// Driver units: the range set by the game. The logical-to-driver x scale is a parameter: TM DOS
// asks for x 0..159 (one unit = 2 pixels, Mouse_Setup 1460:0A74: x / 2), while in mode 13h the
// driver counts 640 virtual pixels (x 0..639, x * 2) and G&G halves them again (CX >> 1). The
// defaults are TM DOS's (dos/input/mouse.h MouseDriver).

#include <cstdint>
#include <functional>

namespace tmport::dos {

struct Int33MouseConfig {
    int minX = 0, maxX = 0x9F;  // AX=07h range at start (TM DOS: 0..159; G&G mode 13h: 0..639)
    int minY = 0, maxY = 0xC7;  // AX=08h range at start (0..199)
    int xMul = 1, xDiv = 2;     // driver x = logical x * xMul / xDiv (TM DOS: / 2; mode 13h: * 2)
    uint16_t startX = 0x9F, startY = 101;  // the position after the driver's reset
};

class Int33Mouse {
public:
    static constexpr uint16_t kLeft = 1, kRight = 2, kMiddle = 4;
    // AX=0Ch event bits (the mask in CX, the events in the handler's AX).
    static constexpr uint16_t kEventMove = 0x01, kEventLeftDown = 0x02, kEventLeftUp = 0x04, kEventRightDown = 0x08,
                              kEventRightUp = 0x10, kEventMiddleDown = 0x20, kEventMiddleUp = 0x40;

    struct Event {
        uint16_t events = 0;   // AX: the subscribed events that happened
        uint16_t buttons = 0;  // BX
        uint16_t x = 0, y = 0;  // CX, DX (driver units)
    };
    using EventHandler = std::function<void(const Event&)>;

    explicit Int33Mouse(const Int33MouseConfig& config = {});

    // A driver answered the game's detection (TM DOS g_mouse DS:912A == -1; G&G always needs one).
    bool present = true;

    // ---- host input (each change is an event for the handler)
    // Host pointer at logical pixel (x, y) of the 320x200 screen; scaled to driver units and clamped
    // to the ranges.
    void setLogicalPosition(int x, int y);
    void setButtons(uint16_t mask);
    void setButton(uint16_t button, bool down) { setButtons(down ? (buttons_ | button) : (buttons_ & ~button)); }

    // ---- INT 33h
    uint16_t x() const { return x_; }  // AX=03h CX
    uint16_t y() const { return y_; }  // AX=03h DX
    uint16_t buttons() const { return buttons_; }  // AX=03h BX
    void setPosition(int x, int y);           // AX=04h (clamped to the ranges, no event)
    void setRangeX(int min, int max);         // AX=07h (swapped when min > max; the position is clamped)
    void setRangeY(int min, int max);         // AX=08h
    int minX() const { return minX_; }
    int maxX() const { return maxX_; }
    int minY() const { return minY_; }
    int maxY() const { return maxY_; }
    // AX=0Ch: mask 0 or an empty handler removes it.
    void setEventHandler(uint16_t mask, EventHandler handler);
    uint16_t eventMask() const { return handler_ ? mask_ : 0; }
    const Int33MouseConfig& config() const { return config_; }

private:
    void emit(uint16_t events);
    uint16_t clampX(long x) const;
    uint16_t clampY(long y) const;

    Int33MouseConfig config_;
    int minX_, maxX_, minY_, maxY_;
    uint16_t x_, y_;
    uint16_t buttons_ = 0;
    uint16_t mask_ = 0;
    EventHandler handler_;
};

}  // namespace tmport::dos
