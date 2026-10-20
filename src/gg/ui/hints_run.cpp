// New Player Hints on the game (hints.h, "wave 3: the run"; code_ui.md §13). Owner: ui agent.
//
// The queue is GgState::ui.hints (g_hintSlots DS:a0de); the flags are the player record's (g_hintsOff
// DS:45a2, g_hintSeen DS:45a4, the cancel words DS:458a / DS:458c of the live block). Other modules
// trigger hints only through hintQueue and hintShow (the triggers' table: code_ui.md §13.2).
#include "gg/ui/hints.h"

#include "gg/data/dgroup_offsets.h"
#include "gg/game/game.h"
#include "gg/game/hud.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

bool isHint(uint16_t id) { return id >= kFirstHintId && id < kFirstHintId + kHintCount; }

}  // namespace

// 3060:0013 Hint_Init
void hintInit(GgGame& g) { g.ui.hints.clear(); }

// 3060:0037 Hint_Queue
void hintQueue(GgGame& g, uint16_t id, uint16_t delay, uint16_t cancelDs) {
    const bool seen = isHint(id) && hintSeen(g.player, id) != 0;
    g.ui.hints.queue(id, delay, g.ticks(), g.player.hintsOff != 0, seen, {}, cancelDs);
}

// 3060:00bc Hint_Update: the first pending hint when due (due < g_ticks, strict: kept) and not cancelled
void hintUpdate(GgGame& g) {
    const auto id = g.ui.hints.update(g.ticks(), [&g](uint16_t ds) {
        // The cancel flags are words of the player record's live block.
        if (ds == dg::kHintCancel1046) return g.player.live.hintCancel1046 != 0;
        if (ds == dg::kHintCancel1048) return g.player.live.hintCancel1048 != 0;
        log::warn("Hint_Update: a cancel flag at DS:{:04x} is not modelled", ds);
        return false;
    });
    if (id) hintShow(g, *id);
}

// 3060:013b Hint_Show
void hintShow(GgGame& g, uint16_t id) {
    if (!isHint(id)) {
        log::warn("Hint_Show({:04x}): not a hint (0x1043..0x1055)", id);
        return;
    }
    if (g.player.hintsOff == 0 && hintSeen(g.player, id) == 0) {
        hudEscHook(g);  // a part held on the cursor goes back
        const int prevFont = g.fonts.select(0);
        const int prevForce = g.video.cursor().forceDefault(1);
        const HintBox box = HintBox::load(g.strings, g.archive(), id);  // Res_LoadCopy: 'FF 06 note FF' and the text
        // The chime (0x1056, the bank layer adds g_fmPresent) at priority 0x32: the box's Snd_Pause cuts it
        // a few instructions later (kept: code_ui.md §13.1).
        const int chime = g.sound.loadSong(kHintChime);
        g.sound.playEffect(chime, 0, 0x32);
        dlgShow(g, box.desc);  // Dlg_MessageBox(g_hintBox DS:2456)
        g.sound.freeSong(chime);
        g.video.cursor().forceDefault(prevForce);
        g.fonts.select(prevFont);
    }
    hintSeen(g.player, id) = 1;  // also when hints are off (kept)
}

// 3060:01fb Hint_ClearQueue
void hintClearQueue(GgGame& g) { g.ui.hints.clear(); }

}  // namespace tmport::gg
