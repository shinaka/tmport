#pragma once
// The vertical scroll bar (ovl30 406a; re/gg/docs/code_ui.md §9): the sign-in list's only one,
// g_rosterScrollBar DS:20dc = {244, 26, 11, 109, pos, max, page 8, ...}.
//
// Record (20 bytes): +0 x, +2 y, +4 w, +6 h, +8 pos (first visible row), +0xa max, +0xc page, +0xe u32
// next repeat tick, +0x12 thumb y. Pictures: bundle 0x102E (G x 5, L): members 1/2 the up/down
// arrows, 3/4 the same greyed (max <= 0); member 0 is not drawn.
// Thumb y = y + (h - 31) * pos / max + 10 (16-bit signed IMUL / IDIV), drawn as an 11 x 11 frame in
// colour 0xF1 around a 9 x 9 fill of 0xCB; the track is filled with 0xC8 and framed with 0xF1.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "gg/data/resources.h"
#include "gg/ui/ui_canvas.h"

namespace tmport::gg {

class Archive;
class GgExe;

inline constexpr uint16_t kScrollBarPictures = 0x102E;

struct ScrollBar {
    int x = 0, y = 0, w = 0, h = 0;  // +0..+6
    int pos = 0;                      // +8
    int max = 0;                      // +0xa
    int page = 0;                     // +0xc
    uint32_t nextRepeat = 0;          // +0xe
    int thumbY = 0;                   // +0x12

    // The record at DS:off (default g_rosterScrollBar DS:20dc).
    static ScrollBar fromRecord(const GgExe& exe, uint16_t dsOff);
};

// The bar's pictures (Res_Read(0x102E) at every ScrollBar_Draw; the engine keeps one copy).
struct ScrollBarPictures {
    Bundle bundle;
    std::vector<std::span<const uint8_t>> members;  // into `bundle` (move-only)

    static ScrollBarPictures load(const Archive& archive, uint16_t id = kScrollBarPictures);
    ScrollBarPictures() = default;
    ScrollBarPictures(ScrollBarPictures&&) noexcept = default;
    ScrollBarPictures& operator=(ScrollBarPictures&&) noexcept = default;
    ScrollBarPictures(const ScrollBarPictures&) = delete;
    ScrollBarPictures& operator=(const ScrollBarPictures&) = delete;
};

// 406a:0000 ScrollBar_DrawThumb(x, y): colour 0xF1 Gfx_FrameRect(x, y, 11, 11), colour 0xCB
// FillRect(x + 1, y + 1, 9, 9).
void scrollBarDrawThumb(const UiCanvas& ui, int x, int y);
// 406a:003f ScrollBar_Draw: colour 0xC8 FillRect(x, y, w, h); colour 0xF1 FrameRect; colour 15; the
// arrows keyed at (x, y) and (x, y + h - 11) (members 1/2, or 3/4 when max <= 0); when max > 0 the
// thumb at thumbY = y + (h - 31) * pos / max + 10 (stored). No flush; g_drawColor is left at 15.
void scrollBarDraw(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb);
// 406a:013e ScrollBar_SetPos(sb, p): p clamped to 0..max; if it changed: pos = p, draw, Dirty_Flush.
void scrollBarSetPos(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb, int p);
// 406a:017f ScrollBar_SetRange(sb, m): m = max(0, m); if it changed: max = m, pos = min(pos, m),
// draw, Dirty_Flush.
void scrollBarSetRange(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb, int m);

// 406a:01b6 ScrollBar_HandleClick(sb, code), up to the thumb drag. Nothing when max <= 0. The first
// poll of a press (code & 0x4000) acts at once (next = now + 27); later polls act only when next <
// now. By the pointer's y (g_mouseY): above y + 10: pos - 1; below y + h - 10: pos + 1; above the
// thumb: pos - page; at or below thumb y + 11: pos + page (ScrollBar_SetPos, then next = now + 27).
// On the thumb it returns the drag's grab offset (pointer y - thumb y): the caller then polls
// Input_GetCode while the code has bit 15 (held over the zone), calling scrollBarDragMove for each
// poll, and ends with scrollBarDragEnd.
std::optional<int> scrollBarHandleClick(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb,
                                        uint16_t code, int mouseY, uint32_t now);
// One poll of the drag (406a:02a0..0320): when the pointer's y changed since the last move and its x
// is within [x, x + w): thumb = clamp(pointer y - grab, y + 9, y + h - 19); if that differs from the
// thumb y the drag started with (+0x12 is not updated during the drag), Gfx_RestoreRect of the whole
// bar from the back page, ScrollBar_DrawThumb there, Dirty_Flush. `dragY` is the drag's last pointer y
// and `thumb` its last computed thumb y (both updated).
void scrollBarDragMove(const UiCanvas& ui, ScrollBar& sb, int grab, int mouseX, int mouseY, int& dragY, int& thumb);
// The end of the drag (406a:032f): pos = ((thumb - (y + 10)) * max + half(h - 31)) / (h - 31) (16-bit),
// thumbY = thumb, a forced redraw through ScrollBar_SetPos, next = now + 27. Quirk: when the pointer
// never moved, the original's thumb register still holds its caller's value (the sign-in passes the
// input code there, so a click on the thumb without a move scrolls to the end); the caller passes what
// it wants for that case.
void scrollBarDragEnd(const UiCanvas& ui, const ScrollBarPictures& pics, ScrollBar& sb, int thumb, uint32_t now);

}  // namespace tmport::gg
