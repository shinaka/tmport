// scene.c (segment 1437) and Scene_LoadPan (1460:05e8): loading the scene of g_level, the menu-bar
// blank and restore around scenes 3/4, the end-of-game hook (re/dos/docs/code_world.md §7).
#include "dos/game/scene.h"

#include <string>
#include <string_view>

#include "dos/game/elves.h"
#include "dos/game/game.h"
#include "dos/game/hud.h"
#include "dos/game/layout.h"
#include "dos/game/main_loop.h"
#include "dos/game/quiz.h"
#include "dos/game/signin.h"
#include "dos/game/sound.h"
#include "dos/game/ui.h"

namespace tmport::dos {

namespace {

// "mountain.pan" -> "mountain": DosGfxFiles looks the decoded files up by stem (any case).
std::string stemOf(const std::string& name) { return name.substr(0, name.find('.')); }

}  // namespace

// 1437:0034 Scene_Update
uint16_t sceneUpdate(DosGame& g) {
    WorldState& w = g.world;
    CoreState& c = g.core;
    // Entering scene 3/4 with a redraw: blank the menu bar (rows 0..10).
    if (w.fullRedraw != 0 && c.menuLock == 1 && w.sceneWanted >= 3 && w.loadedScene < 3) {
        g.back.fillRect(0, 0, 0x9F, 0x0A, 0xFF);
        g.ui.promptIcon = 6;
        hudDrawPromptIcon(g);
        w.menuBarHidden = 1;
        ++c.dirty;
    }
    if (w.sceneWanted != w.loadedScene && c.uiMode == 0) {
        if (static_cast<int16_t>(c.startDrive) <= 1) {  // running from a floppy: the "one moment" box
            msgBoxDraw(g, 0x16);
            presentDirty(g.back, g.screen);
        }
        if (c.endState == 2) {  // leaving the treasure room after a game: the next game starts
            musicStop(g);
            sfxStop(g);
            gameEnd(g);
            cellsResetVariable(g);
            layoutClearSlots(g);
            gameStart(g);
            c.endState = 0;
            g.elves.messagePending = 2;
            g.ui.hudRedraw = 1;
            g.back.dirty().clear();
        }
        // g_quit = 1 freezes Game_Tick while the files load; the engine's loads are instant, the two
        // writes are kept so a quit set earlier in the frame is cleared exactly as in the original.
        c.quit = 1;
        if (w.sceneWanted < 3) {  // the elves are respawned here, with their rand() calls
            const int carriers = quizReorderSlots(g);
            elvesSpawn(g, carriers);
        }
        sceneLoadPan(g, w.sceneWanted);
        FileIndex bankFile = FileIndex::SpritesPak;
        if (w.sceneWanted >= 3) bankFile = static_cast<FileIndex>(w.sceneWanted + 13);  // CLOSE / SHACK
        if (w.sceneWanted >= 3 || w.loadedScene >= 3) {  // SPRITES.PAK also after the initial 0xFF
            loadSpriteBank(g, bankFile);
            reportBankLoad(g, bankFile);
        }
        w.loadedScene = w.sceneWanted;
        setMusicLoop(g, true);
        c.quit = 0;
        if (static_cast<int16_t>(c.startDrive) <= 1) {
            ++w.fullRedraw;
            g.ui.boxSaved = 0;
            g.sprites.heapBase = 0;
            g.sprites.heapSegment = 0;
            g.sprites.heapOffset = 0;
            ++c.dirty;
            g.input.context = 0;
        }
        --g.ui.hudFound;
    }
    // Back below scene 3 with a redraw: restore the menu bar.
    if (w.fullRedraw != 0 && w.menuBarHidden == 1 && w.sceneWanted < 3) {
        w.menuBarHidden = 0;
        menuBarDrawTitle(g, 0, false);
        menuBarDrawTitle(g, 1, false);
        menuBarDrawTitle(g, 2, false);
        ++g.ui.hudPrompt;
        ++c.dirty;
    }
    return 0;
}

// 1460:05e8 Scene_LoadPan. Quirk kept (code_world.md §6.10): the buffers are not cleared, so the
// previous PAN stays readable where a smaller one did not write (camera.h reads both). The engine's
// load is instant; the reference machine's unpacking time is reported to the frame timing.
int sceneLoadPan(DosGame& g, int fileIndex) {
    const PanFile& pan = g.files.pan(stemOf(g.files.files().fileTable().entry(fileIndex).name));
    if (&pan != g.pan) {  // reloading the same PAN rewrites the same bytes: the stale part stays
        g.prevPan = g.pan;
        g.pan = &pan;
    }
    reportPanLoad(g, static_cast<FileIndex>(fileIndex));
    return 1;
}

// The sprite-bank part of Scene_Update 1437:0118 / 1437:0195 and Sprites_LoadBegin 1460:02c6.
void loadSpriteBank(DosGame& g, FileIndex file) {
    g.bank = &g.files.pak(stemOf(g.files.files().fileTable().name(file)));
}

}  // namespace tmport::dos
