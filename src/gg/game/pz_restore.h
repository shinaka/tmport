#pragma once
// Starting a part-B puzzle from a DOSBox-X state (puzzles.h, "starting from a DOSBox-X state"): the
// cursor as the state has it. The default cursor region's record (DS:0a6c) is the puzzle's choice; the
// active copy DS:7a6a follows it only at the next Mouse_DrawCursor (a flush or a pointer move), so a
// state saved right after a box closed shows the record it had before (the arrow) although the region
// already holds the hand. When the state's copy came from that record (g_cursorActiveRecord DS:7a4a ==
// DS:0a6c) the record is selected as usual (Mouse_SetCursorPicture: the cursor is drawn again in the
// same place); otherwise only the region's record is set, and the copy (with the state's save-under,
// GgStateFile::apply) changes at the next draw as the original's does.

#include "gg/data/dgroup_offsets.h"
#include "gg/game/game.h"
#include "gg/gfx/cursor.h"
#include "gg/save/gg_state_file.h"

namespace tmport::gg {

inline void restoreStateCursorRecord(GgGame& g, const GgStateFile& s, CursorRecord* record) {
    Cursor& c = g.video.cursor();
    if (s.dsDword(dg::kCursorActiveRecord) != s.dsDword(0x0A6C)) {
        // The copy is stale: the next Mouse_DrawCursor copies the region's record, here as there.
        c.defaultRegion().record = record;
        return;
    }
    c.setCursorPicture(record);
}

}  // namespace tmport::gg
