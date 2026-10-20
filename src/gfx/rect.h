#pragma once
// A rectangle in pixels (x, y of the top-left corner, width, height): presenter layouts, dirty
// rectangles.

namespace tmport::gfx {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    friend bool operator==(const Rect&, const Rect&) = default;
};

}  // namespace tmport::gfx
