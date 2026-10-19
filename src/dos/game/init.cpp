// init.c (segment 1460), the parts the engine keeps: Game_Init's game steps, Game_InitVars and the
// BEGIN.PAK load. Spec: re/dos/docs/code_runtime.md §2.4, §4.1, §4.3-4.4; PORTING_NOTES.md §2.4, §3.
#include "dos/game/init.h"

#include "dos/game/anim.h"
#include "dos/game/game.h"
#include "dos/game/quiz.h"
#include "dos/game/scene.h"
#include "dos/game/ui.h"

namespace tmport::dos {

namespace {

// g_machineId DS:1300 as Hw_Detect 1000:04A1 leaves it: 0x21 on a Tandy 1000, else the BIOS model
// byte F000:FFFE of a 186+ machine; the DOSBox-X reference machine (an AT) has 0xFC there.
constexpr uint8_t kMachineTandy = 0x21;
constexpr uint8_t kMachineAt = 0xFC;

}  // namespace

// 1460:0003 Game_Init. Done elsewhere: the sprite bank allocation (DosGfxFiles holds every PAK), the
// critical-error handler and the keyboard flags (dropped, PORTING_NOTES §6), Hw_Detect and
// ParseCommandLine (the options), the floppy and network checks (dropped), Pal_Apply (the look's
// palette at presentation), Mouse_Setup (MouseDriver), Sound_LoadData (DosAudioDevice), the file
// reads of Help_LoadFile and Quiz_LoadDictionary (DosGame's constructor). A missing file stops the
// start-up earlier (DosFiles names it) instead of Game_Init returning 0.
void gameInit(DosGame& g) {
    CoreState& c = g.core;
    // Hw_Detect + ParseCommandLine: the options decide what the original detected or was told.
    c.videoType = static_cast<int16_t>(videoTypeOf(g.options.look));
    c.machineId = g.options.look == Look::Tandy ? kMachineTandy : kMachineAt;
    // Without sound (an engine option the original lacks) the game sees the speaker; no game code
    // reads the device, only the driver does.
    c.soundDevice = g.options.sound == SoundDevice::Off ? 0 : static_cast<uint16_t>(soundDeviceOf(g.options.sound));
    c.slowCpu = g.options.slowCpu ? 1 : 0;
    // getdisk(): a hard disk, so no floppy path applies (Path_InitDrives).
    c.startDrive = 2;
    // Mouse_Detect 1460:0A5E: -1 when a driver answered.
    c.mouse = g.rt.mouse().present ? -1 : 0;
    // Video_Init 1000:0361: the mode set, then Gfx_SetDirtyTracking(0xFF) (every primitive adds its
    // rectangle to the dirty list until a routine switches it off around its own drawing).
    g.back.setColor(0xFF);
    // Sound_LoadData: the driver's files are loaded with DosAudioDevice. Help_LoadFile and
    // Quiz_LoadDictionary only note what DosGame's constructor read (g_helpLoaded, DS:A21A).
    helpLoadFile(g);
    quizLoadDictionary(g);
    spritesLoadBegin(g);
    gameInitVars(g);
}

// 1460:033a Game_InitVars.
void gameInitVars(DosGame& g) {
    CoreState& c = g.core;
    UiState& ui = g.ui;
    g.elves.hintsOn = 0;
    ui.hudRedraw = 0;
    g.input.deliverNext = true;  // DS:9238 = 1: the next command is delivered at once
    g.input.queuedCmd = -1;
    ui.menuOpen = -1;
    g.input.cmd = -1;
    TickCounters& t = g.counters();
    t.cycle13 = 12;
    t.secondCountdown = 60;
    t.seconds = 0;
    ui.menuItem = 1;
    ui.hudCoins = 0;  // the HUD caches: everything is drawn at the first HUD redraw
    ui.hudNets = 0;
    ui.hudFound = 0;
    ui.hudKeyState = 0;
    ui.hudPrompt = 0;
    g.world.transition = 0;
    c.unknown9260 = 0;
    c.unknown9262 = 0;
    c.unknown9291 = 0;
    g.back.setClip(ClipWindow{0, 0x9F, 0, 199});  // DS:37DA..37E0: the whole screen
    g.keys() = 0;
    g.world.loadedScene = 0xFF;  // no scene in memory
    c.endState = 0;
    c.uiMode = 0x80;  // the title
    c.menuLock = 0;
    g.world.camStepX = 0;
    g.world.camStepY = 0;
    g.player.obj.dx = 0;
    g.player.obj.dy = 0;
    g.player.searchOpenBaseL0 = 0x32;  // DS:8FB4: the first opened picture of level 0
    c.unknown928B = 6;
    g.player.shownTreasure = -1;
    g.player.showcasePick = 0;
    g.player.storedShowcase = -1;  // DS:9254 = 0xFF
    g.player.shackX = 0x1A;
    g.player.shackY = 0x44;
    g.world.slowScroll = c.slowCpu == 1 ? 1 : 0;
    animInitSlots(g);
    g.world.slowCpuSaved = c.slowCpu;
}

// 1460:02c6 Sprites_LoadBegin: BEGIN.PAK into the sprite bank (two reads of 0xFFF0 bytes in the
// original; the engine's PAK is decoded already).
int spritesLoadBegin(DosGame& g) {
    loadSpriteBank(g, FileIndex::BeginPak);
    return 1;
}

}  // namespace tmport::dos
