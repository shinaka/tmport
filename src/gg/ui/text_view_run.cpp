// The text viewer on the game (text_viewer.h, "wave 3: the run"; code_ui.md §8). Owner: ui agent.
//
// TextView_Run is a busy poll of Input_StripHeld(Input_GetCode()); its arrow repeat polls
// Timer_Expired(6) and its thumb drag spins on g_mouseButtons and the pointer (each turn a GgRuntime
// idle poll). Time is not frozen; timer slot 6 is the only one it uses.
#include "gg/ui/text_viewer.h"

#include "gg/game/game.h"
#include "gg/game/pointer_warp.h"
#include "gg/ui/ui_run.h"

namespace tmport::gg {

namespace {

// 4140:0034 TextView_ZoneHit(x, y, zone): the zone is linked (its next pointer is not NULL: an unlinked
// bar zone never answers) and x in [zx, zx + w), y in [zy, zy + h).
bool textViewZoneHit(int x, int y, const Zone& z) {
    if (z.next == nullptr) return false;
    return z.x <= x && z.x + z.w > x && z.y <= y && z.y + z.h > y;
}

}  // namespace

// 4140:0000 TextView_WarpMouse
void textViewWarpMouse(GgGame& g, int x, int y) {
    g.mouse().warp(static_cast<int16_t>(x), static_cast<int16_t>(y));  // g_mouseX/Y, INT 33h fn 4
    Cursor& c = g.video.cursor();
    c.setCursorPicture(c.arrow());  // Mouse_SetCursorPicture(g_cursorArrow): restore under, flush
}

// 4140:056b TextView_FlashArrow
void textViewFlashArrow(GgGame& g, TextViewer& view, bool down) {
    const UiCanvas ui = g.canvas();
    view.drawArrow(ui, down, true);
    ui.dirtyFlush();
    g.timers().set(6, 7);
    g.rt.timerWait(6);
    view.drawArrow(ui, down, false);
    ui.dirtyFlush();
}

// 4140:05b3 TextView_MouseOnArrow: the pointer (g_mouseX/Y, the hot spot is (0, 0) here) on the arrow
bool textViewMouseOnArrow(GgGame& g, bool down) {
    const int x = g.mouse().x(), y = g.mouse().y();
    if (x < 0x75 || x >= 0x80) return false;
    return down ? (y >= 0x95 && y < 0xA5) : (y >= 0x1D && y < 0x2D);
}

// 4140:0667 TextView_Run
void textViewRun(GgGame& g, std::span<const uint8_t> text) {
    // The set-up: the menu hook saved and cleared, Zones_Push, the arrow forced, the hot spot (0, 0).
    const InputCodes::MenuHandler prevMenu = g.input().menuHandler();
    g.input().setMenuHandler({});
    g.zones().push();
    g.video.cursor().forceDefault(1);
    g.video.cursor().setHotSpot(nullptr, 0, 0);
    TextViewer view(g.exe(), g.archive(), text);  // Res_LoadCopy(0x11d5), Res_Read(0x11d4)
    view.bind(g.ui.viewThumbStep, g.ui.viewThumbY, g.ui.viewScrollMax);
    const UiCanvas ui = g.canvas();
    const PointerWarpHook warpHook(g);  // a window's pointer follows TextView_WarpMouse
    OnUnwind unwind([&] {
        g.zones().clearLocal();
        g.zones().pop();
        g.input().setMenuHandler(prevMenu);
        g.video.cursor().forceDefault(0);
    });
    view.open(ui, &g.zones());  // the save-under, font 1 colour 0, the zones, the scroll words, Redraw

    // The arrows' auto-repeat (4140:07b9 / 0865): the pressed picture, Timer_Set(6, 146); while the left
    // button is held on the arrow: each Timer_Expired(6) steps a line and the thumb, redraws (the arrow
    // pressed) and sets 29 ticks; then the normal picture.
    const auto repeatArrow = [&](bool down) {
        g.timers().set(6, 146);
        view.drawArrow(ui, down, true);
        ui.dirtyFlush();
        while (g.mouse().buttons() != 0 && textViewMouseOnArrow(g, down)) {
            if (!g.rt.timerExpired(6)) continue;
            view.scrollBy(down ? 1 : -1);
            if (down) view.thumbDown();
            else view.thumbUp();
            view.redraw(ui, !down, down);
            g.timers().set(6, 29);
        }
        view.drawArrow(ui, down, false);
        ui.dirtyFlush();
    };

    for (bool done = false; !done;) {
        const uint16_t code = InputCodes::stripHeld(g.rt.getInputCode());  // the busy poll
        switch (code) {  // the table CS:0a8c
            case 0x001B:      // Esc
            case kViewClose:  // a click anywhere outside the viewer's other zones
                done = true;
                break;
            case kViewUpPress: repeatArrow(false); break;
            case kViewDownPress: repeatArrow(true); break;
            case kViewUpRelease:  // 4140:083d: the arrow flashed, a line up
                textViewFlashArrow(g, view, false);
                view.scrollBy(-1);
                view.thumbUp();
                view.redraw(ui);
                break;
            case kViewDownRelease:
                textViewFlashArrow(g, view, true);
                view.scrollBy(1);
                view.thumbDown();
                view.redraw(ui);
                break;
            case kViewPageUp:  // the track above the thumb: 10 lines, 10 thumb steps
                view.scrollBy(-10);
                for (int k = 0; k < 10; ++k) view.thumbUp();
                view.redraw(ui);
                break;
            case kViewPageDown:
                view.scrollBy(10);
                for (int k = 0; k < 10; ++k) view.thumbDown();
                view.redraw(ui);
                break;
            case kViewThumb: {
                // 4140:09e3: the drag. While the left button is held: on the track below the thumb one
                // step and one line down, above it one up, each redrawn with the pointer warped onto the
                // thumb (4140:0925); anywhere else it spins (4140:09c4) until the pointer is on the thumb
                // again or the button is up. The loop polls nothing: each turn is an idle poll here, so
                // that the pointer and the button can change between turns.
                bool seekThumb = false;
                while (g.mouse().buttons() != 0) {
                    const int x = g.mouse().x(), y = g.mouse().y();
                    if (seekThumb) {
                        seekThumb = !textViewZoneHit(x, y, view.thumbZone());
                    } else if (textViewZoneHit(x, y, view.pageDownZone())) {
                        view.thumbDown();
                        view.scrollBy(1);
                        view.redraw(ui);
                        textViewWarpMouse(g, 0x7A, view.thumbY() + 10);
                    } else if (textViewZoneHit(x, y, view.pageUpZone())) {
                        view.thumbUp();
                        view.scrollBy(-1);
                        view.redraw(ui);
                        textViewWarpMouse(g, 0x7A, view.thumbY());
                    } else {
                        seekThumb = !textViewZoneHit(x, y, view.thumbZone());
                    }
                    g.rt.idle();
                }
                break;
            }
            default: break;  // 0x51E (the text) and every other code
        }
    }
    // The exit: Zones_ClearLocal, the arrows freed, the area back, Dirty_Flush, Mouse_ForceDefault(0)
    // (not the previous value), Zones_Pop, the hook back. The font stays 1, the hot spot (0, 0).
    g.zones().clearLocal();
    view.close(ui);
    g.video.cursor().forceDefault(0);
    g.zones().pop();
    g.input().setMenuHandler(prevMenu);
}

}  // namespace tmport::gg
