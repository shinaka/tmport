#pragma once
// The display of Gizmos & Gadgets as one object: the screen buffer with its primitives (screen.h),
// VRAM, the sprite list (sprites.h), the software cursor (cursor.h) and the palette with the DAC
// (palette.h), wired as Gfx_Init (1473:0074) sets them up, plus Dirty_Flush (1581:052a), the only
// path from the RAM screen to VRAM apart from the wipe and the movie (re/gg/docs/code_runtime.md
// §10.1, §11.2, PORTING_NOTES.md §3.2).
//
// What the player sees is VRAM through the DAC (frameRgba). The engine presents complete frames;
// the original's flush copies tiles without waiting for the retrace, so a capture may show a frame
// half copied (fidelity checks compare settled frames).
//
// How the runtime drives it (its yield points, gg/app/gg_runtime.h):
//   * a palette load (Pal_SetShadowRange, a fade step): for each pending group, waitRetrace() then
//     PaletteLoad::onRetrace(dac()); a fade: while (fade.next(cur)) run PaletteLoad(cur, 0, 2) so;
//   * the wipe: Timer_Set(2, period()) before each Wipe::step, Timer_Wait(2) after;
//   * the movie: Timer_Set(0, period()) before each MoviePlayer::step, run the returned cue, then
//     Timer_Wait(0);
//   * present frameRgba() whenever VRAM or the DAC changed (GgRuntime::showFrame).
// gg/app/display.h does all of this on the runtime, and is the mouse handler's MouseScreen
// (gg/input/mouse.h) over this object: drawing() = screen().mouseHideState() != 0; hotX()/hotY() =
// cursor().hotX()/hotY(); cursorX()/cursorY() = cursor().x()/y(); setCursorPosition =
// cursor().setPosition; moveCursor = moveCursor(); Cursor::setDriverHooks forwards Mouse_SetBounds'
// INT 33h 07h/08h to GgMouse::driver().

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "gg/gfx/cursor.h"
#include "gg/gfx/palette.h"
#include "gg/gfx/screen.h"
#include "gg/gfx/sprites.h"

namespace tmport::gg {

class Archive;
class GgExe;

// 1581:052a Dirty_Flush: nothing at all while g_dirtyPending is 0 (no cursor draw, no hide reset);
// else Mouse_DrawCursor, every dirty 8x8 tile from the screen rows to VRAM (clearing it),
// Gfx_ResetMouseHide, pending = 0, the statistics (tiles copied, flushes).
void dirtyFlush(Screen& screen, Cursor& cursor, Vram& vram);

// VRAM through the DAC as a 320x200 RGBA frame (packed R | G << 8 | B << 16 | A << 24, the
// convention of gfx::Presenter::presentFrame).
std::vector<uint32_t> vramToRgba(const Vram& vram, const Dac& dac);
void vramToRgba(const Vram& vram, const Dac& dac, std::span<uint32_t> out);  // out: 64 000 pixels

class Video {
public:
    static constexpr uint16_t kArrowCursor = 0x0000;  // Mouse_LoadCursors(0): a 9x9 RLE arrow, key 1

    Video();
    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;

    Screen& screen() { return screen_; }
    const Screen& screen() const { return screen_; }
    Cursor& cursor() { return cursor_; }
    SpriteList& sprites() { return sprites_; }
    Vram& vram() { return vram_; }
    const Vram& vram() const { return vram_; }
    Palette& palette() { return palette_; }
    const Palette& palette() const { return palette_; }
    Dac& dac() { return dac_; }
    const Dac& dac() const { return dac_; }

    // 1473:0074 Gfx_Init after Video_SetMode13h: Mouse_Init (the arrow = picture resource 0, the
    // default and forced cursor regions DS:0a54 / DS:0a70 of ssg.exe, the pointer at the driver
    // position), the base palette DS:024e into the shadow, g_drawColor = 0, Gfx_FillRect(0, 0, 320,
    // 200), Gfx_SaveToBack(0, 0, 320, 200), then Pal_Blackout: its load is returned for the runtime
    // to run (2 retraces) and the palette is marked faded. The driver position defaults to the centre
    // of mode 13h's 640x200 range, where INT 33h AX=00h leaves the pointer.
    PaletteLoad init(const GgExe& exe, const Archive& archive, int driverX = 320, int driverY = 100);

    // 1581:052a Dirty_Flush.
    void flush() { dirtyFlush(screen_, cursor_, vram_); }
    // 17a0 Mouse_MoveCursor (from the mouse event handler, only while mouseHideState() == 0):
    // Mouse_RestoreUnder, g_mouseX/Y = (x, y), Dirty_Flush.
    void moveCursor(int x, int y);

    // 1473:0399 Screen_LoadPicture(pic, pal): if pal > 0, the palette resource into the shadow
    // (Pal_SetShadowRange: a DAC load unless faded, returned); `picture` receives resource pic (the
    // original leaves it in the shared buffer for the caller to draw).
    PaletteLoad loadPicture(const Archive& archive, uint16_t pic, int pal, std::vector<uint8_t>& picture);

    // What the player sees now.
    std::vector<uint32_t> frameRgba() const { return vramToRgba(vram_, dac_); }

private:
    Screen screen_;
    Cursor cursor_;
    SpriteList sprites_;
    Vram vram_{};
    Palette palette_;
    Dac dac_;
};

}  // namespace tmport::gg
