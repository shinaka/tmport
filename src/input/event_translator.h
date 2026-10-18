#pragma once
// Translates SDL3 events into the Win16 messages the original window procedure handled
// (input/win_msg.h; MainWndProc 1010:0196, re/docs/code_1008-1030.md §5, PORTING_NOTES.md §4.7).
//
// Keyboard (what Win16 TranslateMessage + the WndProc saw):
//  * printable characters arrive as SDL_EVENT_TEXT_INPUT and become WM_CHAR bytes in Windows
//    ANSI (cp1252); characters without an ANSI byte are dropped;
//  * Enter/keypad Enter 0x0D (Ctrl: 0x0A), Esc 0x1B, Backspace 0x08 (Ctrl: 0x7F), Tab 0x09 and
//    Ctrl+A..Z 0x01..0x1A, Ctrl+[ 0x1B, Ctrl+\ 0x1C, Ctrl+] 0x1D are synthesised from key-down
//    events (SDL sends no text for control characters); not while Alt is held (AltGr = Ctrl+Alt);
//  * PgUp/PgDn/End/Home/arrows/Insert/Delete (and the keypad equivalents with NumLock off) become
//    WM_KEYDOWN / WM_KEYUP with the Win16 virtual-key code (no key-down while Alt is held: that
//    is WM_SYSKEYDOWN, which the game's WndProc never sees);
//  * key repeat produces repeated messages, as Windows auto-repeat did.
// Mouse: motion and left/right buttons become WM_MOUSEMOVE / WM_xBUTTONxxx with GAME coordinates:
// window -> 640x480 logical canvas (SDL logical presentation of the renderer, when one is set)
// -> game area 512x384 at canvas (64,48). Window close / quit requests become a Close message.

#include <cstdint>
#include <string_view>
#include <vector>

#include "input/win_msg.h"

union SDL_Event;
struct SDL_Renderer;

namespace tmport {

class EventTranslator {
public:
    static constexpr int kGameOriginX = 64;  // game area origin in the 640x480 canvas
    static constexpr int kGameOriginY = 48;

    // With a renderer, mouse coordinates are converted from window to render (canvas)
    // coordinates by SDL_ConvertEventToRenderCoordinates; without one they are taken as canvas
    // coordinates already (headless tests, scripted input).
    void setRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }
    SDL_Renderer* renderer() const { return renderer_; }

    // Appends the messages for one SDL event to out. Returns the number of messages appended.
    size_t translate(const SDL_Event& event, std::vector<WinMsg>& out) const;

    // Canvas (logical 640x480) coordinate -> game coordinate (floor, then minus the origin).
    static int canvasToGameX(float cx);
    static int canvasToGameY(float cy);

    // Win16 virtual-key code for a navigation key (0 if the key is not one of them).
    static uint16_t navigationVk(uint32_t sdlKeycode, uint32_t sdlScancode, uint16_t sdlMod);
    // WM_CHAR byte for a control key press (0 if the key produces no control character).
    static uint16_t controlChar(uint32_t sdlKeycode, uint16_t sdlMod);
    // UTF-8 text -> Windows ANSI (cp1252) bytes; code points without an ANSI byte and control
    // characters are dropped.
    static std::vector<uint8_t> utf8ToAnsi(std::string_view utf8);

private:
    SDL_Renderer* renderer_ = nullptr;
};

}  // namespace tmport
