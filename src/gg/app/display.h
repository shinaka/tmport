#pragma once
// The display of Gizmos & Gadgets on the runtime (re/gg/docs/code_runtime.md §11-§14,
// PORTING_NOTES.md §2.2, §3): the graphics module's Video (gg/gfx/video.h: RAM screen, VRAM, DAC,
// software cursor) driven by GgRuntime's yield points and clocks.
//
//   * every Dirty_Flush that copies tiles presents VRAM through the DAC (GgRuntime::showFrame);
//   * Pal_SetRange (1399:05d6) waits for the start of a vertical retrace before each of its two
//     128-colour groups (GgRuntime::waitRetrace) and the new colours are presented there; fades are
//     back-to-back loads (Pal_Fade 1473:015c: 12 out, 9 in);
//   * the random wipe (1473:0479) copies its spans straight to VRAM, one step per Timer_Set(2,
//     period) .. Timer_Wait(2);
//   * the mouse event handler (17a0, at interrupt time) reads the cursor through MouseScreen: the hide
//     state, the hot spot, g_mouseX/Y, and Mouse_MoveCursor (restore under, move, Dirty_Flush);
//     Mouse_SetBounds' INT 33h 07h / 08h go to the runtime's driver.
//
// The Display registers itself as the runtime mouse's MouseScreen and as the cursor's flush and
// driver hooks for as long as it lives.

#include <cstdint>
#include <functional>

#include "gg/gfx/palette.h"
#include "gg/gfx/video.h"
#include "gg/input/mouse.h"

namespace tmport {
class BorlandRand;
}

namespace tmport::gg {

class Archive;
class GgExe;
class GgRuntime;

class Display final : public MouseScreen {
public:
    Display(GgRuntime& runtime, Video& video);
    ~Display() override;
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    Video& video() { return video_; }
    GgRuntime& runtime() { return runtime_; }

    // ---- MouseScreen (the mouse event handler 17a0:0004)
    bool drawing() const override;
    int16_t hotX() const override;
    int16_t hotY() const override;
    int16_t cursorX() const override;
    int16_t cursorY() const override;
    void setCursorPosition(int16_t x, int16_t y) override;
    // 17a0:009b Mouse_MoveCursor, then the new picture is presented.
    void moveCursor(int16_t x, int16_t y) override;

    // ---- start-up
    // 1473:0074 Gfx_Init with Mouse_Init at the driver's position (INT 33h AX=03h), then
    // Pal_Blackout's load (2 retraces).
    void init(const GgExe& exe, const Archive& archive);

    // ---- what reaches the screen
    // VRAM through the DAC to the host (and the frame sink).
    void present();
    // 1581:052a Dirty_Flush; presents when tiles were copied.
    void flush();
    // Pal_SetRange's load on the retrace clock: per group, waitRetrace, the colours, present.
    void runLoad(PaletteLoad load);
    // Pal_Fade's loads, back to back.
    void runFade(PalFade fade);
    void fadeOut() { runFade(video_.palette().fadeOut()); }              // 1473:0265 Pal_FadeOut
    void fadeIn() { runFade(video_.palette().fadeIn()); }                // 1473:02a0 Pal_FadeIn
    void fadeFromSaved() { runFade(video_.palette().fadeFromSaved()); }  // 1473:02db
    // 1473:0325 Screen_FadeOutIn: Pal_FadeOut, Dirty_Flush, Pal_FadeIn.
    void fadeOutIn();
    // 1473:045a Screen_Show: faded: Dirty_Flush and Pal_FadeIn; else Screen_RandomWipe (its rand()
    // calls from `rng`; the sound effect started with `effect(k, true)` and stopped with `effect(k,
    // false)`, k = 0x19 or 0x1A of g_effectBank) and Dirty_Clear.
    void show(const Archive& archive, BorlandRand& rng, const std::function<void(int k, bool start)>& effect = {});

private:
    GgRuntime& runtime_;
    Video& video_;
};

}  // namespace tmport::gg
