#pragma once
// Drawing helpers of the room modules that wrap FrameBuffer primitives the way the original's
// callers use them.

#include "gfx/frame_buffer.h"

namespace tmport::game::room {

// 10e0:03a6 Gfx_RestoreFromLayer: clip to the clip rectangle (each axis clips one side only:
// the left/top side, else the right/bottom side), then copy (x, y+dy) -> (x, y) with a dirty rect.
// The sprite eraser of every room module.
inline void restoreFromLayer(gfx::FrameBuffer& fb, int x, int y, int w, int h, int dy) {
    const gfx::ClipRect& c = fb.clip();
    if (x < c.left) {
        w -= c.left - x;
        x = c.left;
    } else if (x + w - 1 > c.right) {
        w = c.right - x + 1;
    }
    if (y < c.top) {
        h -= c.top - y;
        y = c.top;
    } else if (y + h - 1 > c.bottom) {
        h = c.bottom - y + 1;
    }
    if (w > 0 && h > 0) fb.copyRect(x, y + dy, w, h, x, y);
}

}  // namespace tmport::game::room
