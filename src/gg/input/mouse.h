#pragma once
// The mouse of Gizmos & Gadgets (ssg.exe 17a0 Mouse_EventHandler and Mouse_MoveCursor, 17ab
// Mouse_SetBounds, the registration in 1454:0000; re/gg/docs/code_runtime.md §7, PORTING_NOTES.md §8).
//
// The game reads the mouse through an INT 33h fn 0Ch user handler (mask 1Fh) that runs at interrupt
// time: the driver here is the shared dos::Int33Mouse in mode 13h units (x 0..639 = logical x * 2,
// y 0..199, reset at (320, 100) like DOSBox-X's driver). Host pointer moves and button changes
// become driver events that call onEvent() at once (the runtime delivers them at PIT-tick times),
// which keeps the original's latches:
//
//   press latch   DS:0a10  +00h held flag, +0Ah/+0Ch pointer + hot spot, +0Eh first-poll flag
//   release latch DS:0a20  +00h 2 when pending, +06h g_ticks, +0Ah/+0Ch pointer + hot spot
//   g_mouseButtons DS:0a0c (bit 0 left, bit 1 right: some puzzles read it directly)
//   g_mouseX DS:7a88 / g_mouseY DS:7a86 (the cursor's top-left; x = driver x >> 1)
//
// A move is processed only while g_mouseHideState DS:01e2 is 0, i.e. between a Dirty_Flush and the
// next drawing call; otherwise it is dropped (the option dropMovesWhileDrawing turns that off).
// The software cursor itself (its picture, save-under, hot spot and the dirty-tile flush) belongs to
// the graphics module, which plugs in as a MouseScreen.

#include <cstdint>
#include <functional>

#include "dos/input/int33_mouse.h"
#include "gg/input/keyboard.h"

namespace tmport::gg {

// The software cursor and the screen as the mouse handler sees them: the graphics module (17ab
// cursor, 1399 Gfx_HideMouseOnce, 1581 Dirty_Flush; gg/gfx Video, Cursor, Screen) owns them, and
// with them g_mouseX / g_mouseY. Without a MouseScreen the GgMouse keeps g_mouseX/Y itself (tools,
// tests, the stand-in picture): never drawing, hot spot (0, 0), no cursor to draw.
class MouseScreen {
public:
    virtual ~MouseScreen() = default;
    // g_mouseHideState DS:01e2 != 0: a primitive has run since the last flush (a frame is drawing).
    virtual bool drawing() const = 0;
    // The active cursor's hot spot DS:7a74 / DS:7a76 (added to the latched click positions).
    virtual int16_t hotX() const = 0;
    virtual int16_t hotY() const = 0;
    // g_mouseX DS:7a88 / g_mouseY DS:7a86 (the cursor picture's top-left).
    virtual int16_t cursorX() const = 0;
    virtual int16_t cursorY() const = 0;
    // Writes g_mouseX/Y without drawing (Mouse_SetBounds' clamp, the puzzles' warps).
    virtual void setCursorPosition(int16_t x, int16_t y) = 0;
    // 17a0:009b Mouse_MoveCursor: Mouse_RestoreUnder 17ab:072a, g_mouseX/Y = (x, y), Dirty_Flush
    // 1581:052a (which draws the cursor at the new place and copies the dirty tiles).
    virtual void moveCursor(int16_t x, int16_t y) = 0;
};

struct GgMouseOptions {
    // 17a0:001e: a move while g_mouseHideState != 0 is dropped (the original). Off: it is processed
    // like any other move (restore under, new position, flush).
    bool dropMovesWhileDrawing = true;
};

class GgMouse {
public:
    // The event mask of Input_InstallHandlers 1454:0008: move, left press/release, right press/release.
    static constexpr uint16_t kEventMask = 0x1F;
    // The driver in mode 13h: x 0..639 (logical x * 2), y 0..199, after a reset at (320, 100).
    static dos::Int33MouseConfig driverConfig();

    explicit GgMouse(GgMouseOptions options = {});
    GgMouse(const GgMouse&) = delete;
    GgMouse& operator=(const GgMouse&) = delete;

    const GgMouseOptions& options() const { return options_; }
    void setOptions(const GgMouseOptions& o) { options_ = o; }
    // The cursor and screen (not owned); nullptr = none.
    void setScreen(MouseScreen* screen) { screen_ = screen; }
    MouseScreen* screen() const { return screen_; }
    // g_ticks for the release stamp (the runtime's clock); nullptr reads 0.
    void setTickSource(std::function<uint32_t()> ticks) { ticks_ = std::move(ticks); }

    // The INT 33h driver: host input goes in through setLogicalPosition / setButtons; the game's
    // own calls (fn 3, 4, 7, 8) are its methods.
    dos::Int33Mouse& driver() { return driver_; }
    const dos::Int33Mouse& driver() const { return driver_; }

    // Input_InstallHandlers 1454:0000 (INT 33h AX=0Ch, CX=1Fh, Mouse_EventHandler) and
    // Input_RemoveHandlers 1454:0058 (AX=0Ch, CX=0).
    void installHandler();
    void removeHandler();
    bool handlerInstalled() const { return driver_.eventMask() != 0; }

    // 17a0:0004 Mouse_EventHandler with the driver's AX (events), BX (buttons), CX (x), DX (y).
    void onEvent(const dos::Int33Mouse::Event& e);

    // Mouse_Init 17ab:000e, its INT 33h AX=03h: g_mouseX = CX / 2, g_mouseY = DX.
    void readDriverPosition();
    // 17ab:017f Mouse_SetBounds(x, y, w, h) with the active cursor's size (DS:7a70 / 7a72) (the
    // graphics module's Cursor has its own, whose INT 33h calls go to driver().setRangeX/Y):
    // maxX = x + w - cursorW, maxY = y + h - cursorH (16-bit), g_mouseX/Y clamped down to them (no
    // lower clamp), then INT 33h AX=07h (CX = x, not doubled; DX = 2 * maxX) and AX=08h (y, maxY).
    void setBounds(int16_t x, int16_t y, int16_t w, int16_t h, int16_t cursorW, int16_t cursorH);
    // The puzzles' pointer warp (PzMagnets_WarpMouse 35bb:0103, 3b2f:098f, 4140:0000): g_mouseX/Y =
    // (x, y), then INT 33h AX=04h with CX = 2x, DX = y. The host pointer is not moved here: onWarp
    // follows it (game/pointer_warp.h PointerWarpHook sets it while gears and magnets run in a window).
    void warp(int16_t x, int16_t y);
    std::function<void(int16_t x, int16_t y)> onWarp;  // the runtime's host-pointer hook (optional)

    // ---- the game's variables
    // g_mouseX DS:7a88 / g_mouseY DS:7a86: the MouseScreen's when there is one, else kept here.
    int16_t x() const { return screen_ ? screen_->cursorX() : x_; }
    int16_t y() const { return screen_ ? screen_->cursorY() : y_; }
    void setPosition(int16_t px, int16_t py);
    int16_t maxX() const { return maxX_; }  // g_mouseMaxX DS:7a64 (Mouse_SetBounds)
    int16_t maxY() const { return maxY_; }  // g_mouseMaxY DS:7a62
    uint16_t buttons() const { return buttons_; }  // g_mouseButtons DS:0a0c
    InputRecord& press() { return press_; }        // DS:0a10 (g_mouseLatch, DS:0a1e first poll)
    const InputRecord& press() const { return press_; }
    InputRecord& release() { return release_; }    // DS:0a20 (g_mouseRelease)
    const InputRecord& release() const { return release_; }

    // Diagnostics.
    uint64_t eventsHandled() const { return events_; }
    uint64_t movesDropped() const { return dropped_; }

private:
    void moveCursor(int16_t px, int16_t py);

    GgMouseOptions options_;
    dos::Int33Mouse driver_;
    MouseScreen* screen_ = nullptr;
    std::function<uint32_t()> ticks_;
    int16_t x_ = 0, y_ = 0;
    int16_t maxX_ = 0, maxY_ = 0;
    uint16_t buttons_ = 0;
    InputRecord press_, release_;
    uint64_t events_ = 0, dropped_ = 0;
};

}  // namespace tmport::gg
