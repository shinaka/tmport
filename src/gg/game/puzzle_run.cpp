// State 9 and the puzzle framework on the game (puzzles.h, "wave 3: the run"; ovl18 38e2,
// re/gg/docs/code_puzzles_a.md §1, §2). Owner: puzzles_a agent.
#include "gg/game/puzzles.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/game.h"
#include "gg/game/hud.h"
#include "gg/game/pz_balance.h"
#include "gg/game/pz_electric.h"
#include "gg/game/pz_energy.h"
#include "gg/game/pz_gears.h"
#include "gg/game/pz_launch.h"
#include "gg/game/pz_magnets.h"
#include "gg/game/pz_pieces.h"
#include "gg/game/pz_simple.h"
#include "gg/game/world.h"
#include "gg/save/gg_state_file.h"
#include "gg/text/text.h"
#include "gg/ui/menu_bar.h"
#include "gg/ui/text_viewer.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// Pz*_Run of the kind on the game's state (the Puzzle_Dispatch jump table CS:018e). Each puzzle
// object is bound to its section of GgState::puzzles before it runs.
int runKind(GgGame& g, Puzzle& pz, int kind, int variant) {
    auto& p = g.puzzles;
    switch (kind) {
    case 0: {
        auto& k = static_cast<PzElectric&>(pz);
        k.bind(p.electric);
        return k.run(g, variant);
    }
    case 1: {
        auto& k = static_cast<PzSimple&>(pz);
        k.bind(p.simple);
        return k.run(g, variant);
    }
    case 2: {
        auto& k = static_cast<PzPieces&>(pz);
        k.bind(p.pieces);
        return k.run(g, variant);
    }
    case 3: {
        auto& k = static_cast<PzGears&>(pz);
        k.bind(p.gears);
        return k.run(g, variant);
    }
    case 4: {
        auto& k = static_cast<PzBalance&>(pz);
        k.bind(p.balance);
        return k.run(g, variant);
    }
    case 5: {
        auto& k = static_cast<PzLaunch&>(pz);
        k.bind(p.launch);
        return k.run(g, variant);
    }
    case 6: {
        auto& k = static_cast<PzEnergy&>(pz);
        k.bind(p.energy);
        return k.run(g, variant);
    }
    case 7: {
        auto& k = static_cast<PzMagnets&>(pz);
        k.bind(p.magnets);
        return k.run(g, variant);
    }
    default: return 0;
    }
}

// 38e2:0162..018b: the solved flag is written when it was 0 (a solved variant stays solved).
void storeResult(GgGame& g) {
    const int kind = g.puzzles.kind, variant = g.puzzles.variant;
    if (kind < 0 || kind >= kPuzzleKinds) return;  // JA past the table: nothing ran
    uint16_t& flag = puzzleSolved(g.player, kind, variant);
    if (flag == 0) flag = static_cast<uint16_t>(g.puzzles.result);
}

}  // namespace

// 38e2:0000 State_09_Puzzle
int16_t state09Puzzle(GgGame& g, StateEntry entry) {
    auto& p = g.puzzles;
    PuzzlesObjects& objs = g.puzzlesObjects();
    const bool resume = entry == StateEntry::Resume && objs.resumePending && objs.running;
    if (resume) {
        // The state's moment is inside the running puzzle's loop: state 9's entry and the choice were
        // made before it (puzzlesRestoreFromState pushed the zone list and linked the puzzle's zones; the
        // menu bar the entry installed is the UI's restore, which load_state runs first: installing it
        // again would link its title zones twice).
        objs.resumePending = false;
        p.result = 0;
        p.result = static_cast<int16_t>(objs.running->resume(g));
        objs.running.reset();
        storeResult(g);
    } else {
        objs.resumePending = false;
        g.video.palette().save();  // Pal_Save 1473:074e
        g.zones().push();          // Zones_Push 17ab:04bd
        menuSetMenuBar(g, true);   // Menu_SetMenuBar(&DS:4053)
        p.leave = 0;
        int idx = 0;
        for (bool chosen = false; !chosen;) {
            p.kind = static_cast<int16_t>(puzzleRandomType(g));  // rand #1 per tried kind
            puzzlePickUnsolved(g, p.kind);                       // rand #2 when few are left
            int k = 3;
            if (p.candidates[2] == -1) k = 2;
            if (p.candidates[1] == -1) k -= 1;
            if (p.candidates[0] != -1) {
                idx = g.rand.random(k);  // 38e2:0052: also made when k = 1
                chosen = true;
            }
        }
        p.variant = p.candidates[static_cast<size_t>(idx)];
        puzzleDispatch(g);
    }
    roomOpenPuzzleDoor(g, p.result);  // 12e0:0a24 Room_OpenPuzzleDoor(g_puzzleResult)
    g.display.fadeOut();              // Pal_FadeOut: nothing when already faded
    g.zones().pop();
    menuSetMenuBar(g, false);
    g.video.palette().restore();      // Pal_Restore 1473:0766 (no DAC load)
    int16_t next = 5;
    if (p.leave & 2) {
        next = 16;
        g.flow.leaveRequest = 3;
    }
    if (p.leave & 1) {  // tested last: wins
        next = 16;
        g.flow.leaveRequest = 2;
    }
    return next;
}

// 38e2:00d4 Puzzle_Dispatch
void puzzleDispatch(GgGame& g) {
    auto& p = g.puzzles;
    puzzleDispatchLine(g.video.screen());  // g_drawColor = 0, Gfx_HLine(0, 9, 320)
    p.result = 0;
    if (p.kind >= 0 && p.kind < kPuzzleKinds) {
        PuzzlesObjects& objs = g.puzzlesObjects();
        objs.running = makePuzzle(p.kind, g.exe(), g.archive());
        p.result = static_cast<int16_t>(runKind(g, *objs.running, p.kind, p.variant));
        objs.running.reset();
    }
    storeResult(g);
}

// 38e2:019e Puzzle_FewUnsolvedLeft
bool puzzleFewUnsolvedLeft(GgGame& g) {
    int unsolved = 0;
    for (int kind = 0; kind < kPuzzleKinds; ++kind)
        for (int v = 0; v < g.tables.puzzleVariants[static_cast<size_t>(kind)]; ++v)
            if (puzzleSolved(g.player, kind, v) == 0) ++unsolved;
    return unsolved < 5;
}

// 38e2:01e8 Puzzle_RandomType
int puzzleRandomType(GgGame& g) { return g.rand.random(8); }  // (rand() << 3) / 0x8000

// 38e2:0202 Puzzle_PickUnsolved
void puzzlePickUnsolved(GgGame& g, int kind) {
    auto& cand = g.puzzles.candidates;
    cand = {-1, -1, -1};
    const int n = g.tables.puzzleVariants[static_cast<size_t>(kind)];
    if (!puzzleFewUnsolvedLeft(g)) {
        // The first up to three unsolved variants, in index order.
        int found = 0;
        for (int v = 0; v < n && found < 3; ++v)
            if (puzzleSolved(g.player, kind, v) == 0) cand[static_cast<size_t>(found++)] = static_cast<int16_t>(v);
    } else {
        cand[0] = static_cast<int16_t>(g.rand.random(n));  // 38e2:022d: any variant, solved or not
    }
}

// 38e2:0286 Puzzle_ShowHintText
void puzzleShowHintText(GgGame& g, std::span<const uint8_t> text) { textViewRun(g, text); }

// 38e2:0299 Puzzle_ShowSolved
void puzzleShowSolved(GgGame& g, int jingle) {
    g.sound.pzPlay(jingle + g.rand.random(4), 4);  // 38e2:02a3: the only rand() of a part-A puzzle
    PuzzlesObjects& objs = g.puzzlesObjects();
    objs.panelZone = zoneFromDs(g.exe(), dg::kPzPanelZone);
    g.zones().addLocal(objs.panelZone);
    const auto message = puzzleText(g.archive(), kSolvedMessage);  // Res_Read(0x2000)
    g.video.screen().lpxDrawKeyed(0, 165, objs.panel.panel(), 0);
    g.fonts.select(1);
    g.fonts.setColor(0);
    textDrawBox(g.video.screen(), g.fonts, 4, 170, 208, message);
    g.display.flush();
}

// 38e2:031a Puzzle_PressGoBack
void puzzlePressGoBack(GgGame& g) {
    g.video.screen().lpxDrawKeyed(249, 168, g.puzzlesObjects().panel.goBackPressed(), 0);
    g.display.flush();
    puzzleWaitRelease(g);
    g.video.screen().restoreRect(249, 168, 65, 19);
    g.display.flush();
}

// 38e2:0357 Puzzle_InitPanel
void puzzleInitPanel(GgGame& g, std::span<const uint8_t> text) {
    PuzzlesObjects& objs = g.puzzlesObjects();
    objs.panel.load(g.archive());
    objs.goBackZone = zoneFromDs(g.exe(), dg::kPzGoBackZone);
    g.zones().addLocal(objs.goBackZone);
    g.fonts.select(1);
    g.fonts.setColor(0);
    g.video.screen().lpxDrawKeyed(0, 165, objs.panel.panel(), 0);
    textDrawBox(g.video.screen(), g.fonts, 4, 170, 208, text);
}

// 38e2:03bf Puzzle_AwardPoints
void puzzleAwardPoints(GgGame& g, int sound) {
    for (int i = 0; i < 4; ++i) {
        g.sound.pzPlay(sound, 5);
        hudAddScoreNow(g, 100);  // 1a04:0d36: Score_Add(100, 1), the score drawn, flush
        pzSndWaitEnd(g, sound);
    }
}

// 38e2:03ed Puzzle_FreePanel
void puzzleFreePanel(GgGame& g) {
    g.fonts.select(0);
    g.puzzlesObjects().panel.free();
}

bool puzzlesRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    auto& p = g.puzzles;
    PuzzlesObjects& objs = g.puzzlesObjects();
    objs.resumePending = false;
    objs.running.reset();
    if (p.kind < 0 || p.kind >= kPuzzleKinds) {
        why = std::format("g_puzzleKind is {}", p.kind);
        return false;
    }
    if (p.variant < 0 || p.variant >= g.tables.puzzleVariants[static_cast<size_t>(p.kind)]) {
        why = std::format("g_puzzleVariant {} is outside kind {}'s variants", p.variant, p.kind);
        return false;
    }
    objs.running = makePuzzle(p.kind, g.exe(), g.archive());
    // State 9's Zones_Push: the room's list it saved is not part of the moment (an empty list is
    // pushed); the puzzle's zones are linked over it.
    g.zones().push();
    // (the state's cursor save-under: GgStateFile::apply)
    if (!objs.running->restoreFromState(g, s, why)) {
        objs.running.reset();
        g.zones().clearLocal();
        g.zones().pop();
        g.video.cursor().clearRegions();
        g.video.cursor().setCursorPicture(g.video.cursor().arrow());
        why = std::format("puzzle kind {} ({}): {}", p.kind, puzzleKindInfo(p.kind).name, why);
        return false;
    }
    objs.resumePending = true;
    return true;
}

}  // namespace tmport::gg
