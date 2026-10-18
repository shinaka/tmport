#pragma once
// 16-bit input codes as produced by the original's Input_GetCode (1028:0131;
// re/docs/code_1008-1030.md §6.3, ARCHITECTURE.md §3.3):
//  * a WM_CHAR character (0x0D Enter, 0x1B Esc, 0x20 Space, 0x08 Backspace, 0x12 Ctrl+R,
//    0x13 Ctrl+S, letters as typed, ...);
//  * 0x100 | VK for the navigation keys PgUp..Home, the arrows, Insert and Delete;
//  * 0x1B for a window-close / quit request;
//  * a hot-spot id for mouse buttons, with a flag for the transition (see below); 0 when the
//    pointer is over no hot spot.

#include <cstdint>

namespace tmport::input {

inline constexpr uint16_t kNone = 0;
inline constexpr uint16_t kBackspace = 0x08;
inline constexpr uint16_t kTab = 0x09;
inline constexpr uint16_t kEnter = 0x0D;
inline constexpr uint16_t kCtrlR = 0x12;  // remove player on the sign-in list
inline constexpr uint16_t kCtrlS = 0x13;  // sound toggle (handled inside the event fetch)
inline constexpr uint16_t kEsc = 0x1B;
inline constexpr uint16_t kSpace = 0x20;

// WM_KEYDOWN VK 0x21..0x2E -> 0x100 | VK (1010:0471).
inline constexpr uint16_t kVkFlag = 0x100;
inline constexpr uint16_t kPageUp = 0x121;
inline constexpr uint16_t kPageDown = 0x122;
inline constexpr uint16_t kEnd = 0x123;
inline constexpr uint16_t kHome = 0x124;
inline constexpr uint16_t kLeft = 0x125;
inline constexpr uint16_t kUp = 0x126;
inline constexpr uint16_t kRight = 0x127;
inline constexpr uint16_t kDown = 0x128;
inline constexpr uint16_t kInsert = 0x12D;
inline constexpr uint16_t kDelete = 0x12E;

// Mouse transitions over a hot spot `id` (1028:0131): left down id|0x8000, left up id,
// right down id|0x4000, right up id|0x2000.
inline constexpr uint16_t kLeftDownFlag = 0x8000;
inline constexpr uint16_t kRightDownFlag = 0x4000;
inline constexpr uint16_t kRightUpFlag = 0x2000;

// Win16 virtual-key codes the window procedure reacts to (WM_KEYDOWN 0x21..0x2E, WM_KEYUP 0x25..0x28).
namespace vk {
inline constexpr uint16_t kPrior = 0x21;
inline constexpr uint16_t kNext = 0x22;
inline constexpr uint16_t kEnd = 0x23;
inline constexpr uint16_t kHome = 0x24;
inline constexpr uint16_t kLeft = 0x25;
inline constexpr uint16_t kUp = 0x26;
inline constexpr uint16_t kRight = 0x27;
inline constexpr uint16_t kDown = 0x28;
inline constexpr uint16_t kInsert = 0x2D;
inline constexpr uint16_t kDelete = 0x2E;
}  // namespace vk

}  // namespace tmport::input
