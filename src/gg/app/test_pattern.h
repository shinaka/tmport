#pragma once
// A stand-in entry for the runtime's tests (runGgApp without an entry; ggport itself runs the
// preview, gg/app/preview.h). It runs a frame loop exactly like the original's (Timer_Set(0, 4) ...
// Timer_Wait(0); Input_GetCode), so the runtime, the input chain and the frame dumps can be checked
// end to end:
//
//   rows 0..159    the base palette DS:024e (the user's ssg.exe) as 16 x 16 cells of 20 x 10
//   rows 160..199  a marker that moves every 73 ticks (0.5 s), the held-key bits (Left, Right, Up,
//                  Down, Ctrl, Space), the sound flag Ctrl+S toggles, the 16 bits of the last
//                  non-zero input code
//   a crosshair at (g_mouseX, g_mouseY); a yellow border for 73 ticks after a click
//
// One full-screen zone {0, 0, 320, 200, priority 5, press 0, release 0x0D} turns a click into Enter
// (as the attract's DS:0a3a). Esc ends the run (exit code 0).

#include <array>
#include <cstdint>
#include <span>

namespace tmport::gg {

struct GgContext;

struct TestPatternView {
    uint32_t ticks = 0;        // g_ticks
    int16_t mouseX = 0, mouseY = 0;
    uint16_t heldKeys = 0;
    uint16_t lastCode = 0;
    bool soundOn = true;
    bool flash = false;
};

// 6-bit DAC values expanded as (v * 255 + 31) / 63 (PORTING_NOTES.md §3.2), presenter RGBA.
std::array<uint32_t, 256> rgbaPalette(std::span<const uint8_t> dac768);

// Draws the pattern into a 320x200 RGBA picture.
void drawTestPattern(std::span<uint32_t> rgba, const std::array<uint32_t, 256>& palette, const TestPatternView& v);

// The frame loop (runs until Esc, GgExit or GgRunEnd).
int testPatternMain(GgContext& ctx);

}  // namespace tmport::gg
