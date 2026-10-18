#pragma once
// The subset of Win16 window messages the original's window procedure turns into game state
// (MainWndProc 1010:0196; re/docs/code_1008-1030.md §5). SDL events are first translated into
// these (input/event_translator.h); InputQueue then "dispatches" them exactly like the original
// WndProc did, inside the event fetch (Evt_GetNextEvent 1028:004f).

#include <cstdint>

namespace tmport {

struct WinMsg {
    enum class Type : uint8_t {
        Char,         // WM_CHAR: param = character byte (Windows ANSI)
        KeyDown,      // WM_KEYDOWN: param = virtual-key code
        KeyUp,        // WM_KEYUP: param = virtual-key code
        MouseMove,    // WM_MOUSEMOVE: x, y in game coordinates
        LButtonDown,  // WM_LBUTTONDOWN
        LButtonUp,    // WM_LBUTTONUP
        RButtonDown,  // WM_RBUTTONDOWN
        RButtonUp,    // WM_RBUTTONUP
        Close,        // WM_CLOSE / WM_QUERYENDSESSION / SC_CLOSE: quit request
        PostKey,      // Evt_PostKey(param) from engine code (scripted input, tools)
    };
    Type type = Type::Char;
    uint16_t param = 0;
    int16_t x = 0, y = 0;  // game coordinates (window pixel - viewport origin), may be outside 0..511/0..383

    static WinMsg chr(uint16_t c) { return WinMsg{Type::Char, c, 0, 0}; }
    static WinMsg keyDown(uint16_t vk) { return WinMsg{Type::KeyDown, vk, 0, 0}; }
    static WinMsg keyUp(uint16_t vk) { return WinMsg{Type::KeyUp, vk, 0, 0}; }
    static WinMsg mouse(Type t, int x, int y) {
        return WinMsg{t, 0, static_cast<int16_t>(x), static_cast<int16_t>(y)};
    }
    static WinMsg close() { return WinMsg{Type::Close, 0, 0, 0}; }
    static WinMsg postKey(uint16_t code) { return WinMsg{Type::PostKey, code, 0, 0}; }

    bool operator==(const WinMsg&) const = default;
};

}  // namespace tmport
