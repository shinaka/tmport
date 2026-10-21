#pragma once
// DosGame: the 1990 DOS edition's game, as one context object handed to every module function
// (`void sceneUpdate(DosGame& g)`): the state (GameState, inherited, so `g.world.cameraX`,
// `g.core.coins`), the read-only tables, the user's files, the back buffers, the sound facade, the
// one Turbo C rand(), the runtime services (virtual PIT counters, step gate, frame clock), the
// input objects and the users store. It is the DosRuntime's client: the runtime calls frameBegin /
// gameTick / frameBody once per game tick (lockstep, dos/app/dos_runtime.h), and DosGame runs the
// original's Frame_Begin, Game_Tick and main-loop body through the module functions.
//
// Runtime model, module ownership and conventions: re/dos/docs/ENGINE_MAP.md.
// Owner: mainloop agent (the RuntimeClient part); the constructor and accessors are the skeleton.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "dos/app/dos_runtime.h"
#include "dos/data/help_file.h"
#include "dos/data/puzzles.h"
#include "dos/game/game_state.h"
#include "dos/game/sound.h"
#include "dos/gfx/back_buffer.h"
#include "dos/gfx/blitter.h"
#include "dos/gfx/dos_screens.h"
#include "dos/gfx/palettes.h"
#include "dos/input/input_poll.h"
#include "dos/input/mouse.h"
#include "dos/save/users_store.h"
#include "edition/dos_options.h"
#include "mischief/rtl/borland_rand.h"

namespace tmport::dos {

class DosGame : public GameState, public RuntimeClient {
public:
    // `files` (SST.EXE, every data file, decoded PANs/PAKs and the font), `runtime`, `sound` and
    // `users` must outlive the game. Loads the tables, sets every GameState field to its EXE value
    // (BSS 0) and builds the services; nothing of Game_Init runs yet (init.h: gameInit).
    DosGame(const DosGfxFiles& files, DosRuntime& runtime, SoundFacade& sound, DosUsersStore& users,
            const DosOptions& options);
    DosGame(const DosGame&) = delete;
    DosGame& operator=(const DosGame&) = delete;
    ~DosGame() override;

    // ---- files and read-only data
    const DosGfxFiles& files;
    const DosOptions options;
    const DosExe& exe() const { return files.exe(); }
    const DosFont& font() const { return files.font(); }
    const GameTables tables;
    const HelpFile help;              // HELP.DAT (g_helpData DS:9325)
    const PuzzleFile puzzles;         // ALL_PUZS
    const Dictionary dictionary;      // PRG_DICT (g_dictionary DS:B204)
    const QuizTiers quizTiers;        // DS:5FD2 with word 5 computed (Quiz_FindTierEnd)

    // ---- services
    DosRuntime& rt;
    SoundFacade& sound;
    DosUsersStore& users;             // the USERS.DAT image g_usersFile DS:BA14 and its writes
    mischief::BorlandRand rand;                 // the one Turbo C rand() (seed DS:8F48 = 1; srand only in Layout_Generate)
    InputTables inputTables;          // key/command/zone lists of the 6 input contexts (mutable: DS:7D84..80DA, 6194)
    InputPoll input;                  // g_cmd DS:9226, DS:90F6, DS:9238, DS:9285, g_inputContext DS:922C
    MousePointer mouse;               // DS:911A/911E/90F4, previous 925E/9266/9143, 92AC/92AE (Mouse_Poll)
    SpriteRenderer sprites;           // g_drawRecs DS:9145, g_drawOrder DS:92DC, DS:92F6, DS:92F8, heap DS:37C8..37CC, g_saveHeapHigh DS:929F
    BackBuffer back;                  // segment 2005: the frame being composed (clip window DS:37DA..37E0)
    BackBuffer screen;                // the picture the player sees (Present_Dirty / Present_Full target)
    LookPalette palette;              // the look's colours (Pal_Apply 1437:0001 happens at presentation)
    CgaBinding cgaBinding{back, screen, palette};  // CGA look: back and screen keep the 2-bpp picture (looks agent)
    const PanFile* pan = nullptr;     // the PAN in the map/tile buffers (Scene_LoadPan 1460:05e8)
    const PanFile* prevPan = nullptr; // the PAN loaded before it: its blocks still show where the new,
                                      // smaller PAN did not overwrite the buffers (the chute, code_world.md §6.10)
    const PakFile* bank = nullptr;    // the PAK in the sprite bank g_spriteBank DS:37BC
    std::optional<SavedRect> boxSave; // slow CPU: the background MsgBox_Draw saved (DS:37C8 handle, g_boxSaved)
    std::optional<uint16_t> layoutSeedOverride;  // tests: the srand value Layout_Generate uses instead of time()

    // ---- the frame loop's engine-side state (no DGROUP counterpart; owner: mainloop agent)
    bool titleRunning = false;     // Title_Run's loop is running (main's loop has not started yet)
    bool dissolveRunning = false;  // a dissolve spans frames: the original's one long Frame_Present
    bool dissolveGate = false;     // g_stepDone at the end of the last frame of that long frame
    bool flashGate = false;        // g_stepDone at the end of the last frame of a Button_Flash wait (ui.h)
    bool searchRunning = false;    // a search ran in the last frame (main_loop.cpp times its frames)
    uint64_t searchAt = 0;         // where the next search frame begins (FrameTiming units of the runtime's PIT count)
    uint32_t dissolveTail = 0;     // the reference time from the dissolve's last frame's tick to its end (FrameTiming units)
    bool latchedLoad = false;      // this frame's Frame_Begin latched a scene load (frameBeginTiming)
    bool latchedRedraw = false;    // ... or a full play-field redraw (no transition, no search band)
    bool redrawCostDue = false;    // that redraw's cost is still to be reported (before Playfield_Redraw)
    bool dissolveAfterHud = false; // this frame redrew the HUD (and with it the play field) before its present
    uint32_t musicPlays = 0;       // tunes started so far (Music_Play with the music on, sound.cpp)
    uint32_t musicPlaysAtBegin = 0;      // musicPlays at this frame's start and at the one before (a tick
    uint32_t musicPlaysAtPrevBegin = 0;  // between them started a tune: the dissolve's timing)
    bool shutdownDone = false;     // Game_Shutdown ran
    uint64_t screenVersion = 0;    // copies into `screen` so far (the host shows a new picture)
    // When the original presented the picture in `screen`, in FrameTiming units of the runtime's PIT
    // count, where it is later than the runtime's frame end (DosRuntime::frameEndUnits): the title
    // loop's page draws, which keep their own clock (title.cpp).
    uint64_t screenShownAt = 0;

    // ---- globals held by the services (DS address)
    uint16_t& keys() { return rt.keyboard().isr().keys; }                    // g_keys DS:9105
    bool& jumpFromDiagonal() { return rt.keyboard().isr().jumpFromDiagonal; } // g_jumpFromDiagonal DS:9257
    StepGate& stepGate() { return rt.stepGate(); }                           // g_stepDone DS:910D
    bool stepDone() const;                                                   // g_stepDone DS:910D
    TickCounters& counters() { return rt.counters(); }                       // DS:90FE, DS:9100, g_seconds DS:9115
    uint32_t ticks() const;                                                  // g_ticks DS:3D2B
    OriginalFrameClock& frameClock() { return rt.frameClock(); }
    mischief::dos::BiosKeyboard& bios() { return rt.keyboard().bios(); }                    // the BIOS buffer (name entry, Kbd_Remove)

    // *g_userRec DS:9228: the selected player record (ui.userRec); throws std::logic_error if none.
    UserRecord& rec();
    const UserRecord& rec() const;
    bool hasRec() const { return ui.userRec >= 0 && ui.userRec < 9; }

    // A NUL-terminated string at DS offset `off` as the game would read it there: the writable
    // message texts (DS:6BC0..7CA2), the quiz buffers (g_answerText, g_questionText, the expanded
    // questions), the selected record's name (DS:BA17 + 360 i), otherwise the EXE's bytes.
    std::string_view dsString(uint16_t off) const;
    // The PAK frame of a frame code (frame >> 1) in the loaded sprite bank; nullptr if out of range.
    const PakFrame* bankFrame(uint16_t frameCode) const;

    // ---- RuntimeClient (owner: mainloop agent)
    void soundTick() override;
    void frameBegin() override;
    void gameTick(const TickInfo& info) override;
    void frameBody() override;
    uint8_t uiMode() const override { return core.uiMode; }
    bool quitRequested() const override { return core.quit != 0; }
};

// The state and the service-held globals in the DGROUP layout (gameStateFields() plus
// serviceFields()), on the EXE's initialised image: what wave 4 compares with savmem.py dumps.
std::vector<uint8_t> dumpToDgroup(const DosGame& g);
// The service-held globals dumpToDgroup(const DosGame&) writes (DgroupField::service = true).
const std::vector<DgroupField>& serviceFields();

}  // namespace tmport::dos
