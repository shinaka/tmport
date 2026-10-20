#pragma once
// What the TLC UI library of Gizmos & Gadgets draws with (re/gg/docs/code_ui.md §2): the screen
// buffer and its primitives (gg/gfx/screen.h), the two fonts with their selection state
// (gg/text/font.h) and Dirty_Flush (1581:052a), which copies the dirty tiles to VRAM and draws the
// cursor. In the original these are globals; the UI functions of src/gg/ui take them as one
// parameter, so they run against the game's Video as well as against a bare Screen in tests.

#include <functional>

#include "gg/gfx/screen.h"
#include "gg/gfx/video.h"
#include "gg/text/font.h"

namespace tmport::gg {

struct UiCanvas {
    Screen& screen;
    Fonts& fonts;
    // 1581:052a Dirty_Flush (Video::flush); empty: nothing reaches VRAM (tests, layout only).
    std::function<void()> flush;

    void dirtyFlush() const {
        if (flush) flush();
    }
    // The game's display: Video's screen, its Dirty_Flush (with the software cursor).
    static UiCanvas of(Video& video, Fonts& fonts) {
        return {video.screen(), fonts, [&video] { video.flush(); }};
    }
};

}  // namespace tmport::gg
