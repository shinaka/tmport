#pragma once
// Diagnostics of the DOS game: the engine's counterpart of the DOSBox-X frame logger
// (extracted/scratch/dos_w1_runtime/framelog.py) and DGROUP state dumps for comparisons with
// DOSBox-X save states (savmem.py). Engine tooling only: nothing here changes the game.
//
// Trace (--dos-trace FILE): a CSV with one row per game tick that ran Game_Tick (the ticks inside the
// original's disk I/O, g_quit, have none, like the frame logger's STEP ring),
//
//   tick,ticks,gated,x,y,camx,camy,state,keys,coins,nets,level,scene,uimode,area,events
//
//   tick      game tick number (1, 2, ...)          ticks    g_ticks DS:3D2B at the tick (PIT count)
//   gated     1 when the tick found g_stepDone set (only Anim_Tick and Input_Tick ran)
//   x .. coins  as read at the START of the tick, before Mouse_Poll: exactly what framelog.py's STEP
//             record holds (g_player x/y DS:37E2/37E4, g_cameraX/Y DS:37EA/37EC, g_playerState
//             DS:913C, g_keys DS:9105, g_coins DS:9134), so engine row n lines up with STEP record n
//             of a DOSBox-X log (whose gated ticks are in its BLOCKED ring instead)
//   nets level scene uimode area   g_nets, g_level, g_loadedScene, g_uiMode, g_area after the frame
//   events    what happened in the tick and its frame, separated by ';' (empty if nothing; what a
//             frame whose own tick fell into its I/O did starts the next row):
//             key=XXXX (BIOS key word read by Input_Poll), cmd=N (command delivered), ui=0xNN,
//             level=N, scene=N, area=N, screen=N (painter), ctx=N (input context), title=N (title
//             phase), transition=N, dissolve=on|off, game=N (g_gameInProgress), end=N (g_endState),
//             save=<moment>, quit, and the sound calls (sfx=N, sfxstop, music=N, musicstop,
//             silence, soundon=B, musicon=B, sfxloop=B, musicloop=B, gate=N)
//
// All numbers are decimal except the key word and g_uiMode. Lines starting with '#' are comments.
//
// State dump (--dos-dump-state TICK:FILE): dumpToDgroup(const DosGame&) after game tick TICK and
// its frame, DS:0000..CC1F in the original's layout (FILE), and next to it FILE.fields.tsv, the
// field registry (gameStateFields() + serviceFields()) to compare it field by field with a DGROUP
// read out of a DOSBox-X save state.

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "dos/app/dos_runtime.h"
#include "dos/game/sound.h"

namespace tmport::dos {

class DosGame;

// A sound facade that forwards every call to `inner` and notes it as a trace event.
class TraceSound final : public SoundFacade {
public:
    explicit TraceSound(SoundFacade& inner) : inner_(inner) {}

    void sfxPlay(int id) override;
    void sfxStop() override;
    void musicPlay(int tune) override;
    void musicStop() override;
    void silenceAll() override;
    void setSoundOn(bool on) override;
    void setMusicOn(bool on) override;
    void setSfxLoop(bool on) override;
    void setMusicLoop(bool on) override;
    void setMusicGate(uint16_t m) override;
    bool sfxActive() const override { return inner_.sfxActive(); }
    void pitTick() override { inner_.pitTick(); }
    void restoreDriver(std::span<const uint8_t> sfxBlock, std::span<const uint8_t> musicBlock, int voices) override {
        inner_.restoreDriver(sfxBlock, musicBlock, voices);
    }

    // The events since the last call.
    std::vector<std::string> takeEvents();

private:
    void note(std::string e) { events_.push_back(std::move(e)); }
    SoundFacade& inner_;
    std::vector<std::string> events_;
};

// The per-tick CSV trace. Install it as the runtime's client in place of the game (it forwards every
// call); `sound` (optional) is the TraceSound the game was built with.
class DosTrace final : public RuntimeClient {
public:
    DosTrace(DosGame& g, std::ostream& out, TraceSound* sound = nullptr);
    ~DosTrace() override;
    DosTrace(const DosTrace&) = delete;
    DosTrace& operator=(const DosTrace&) = delete;

    void soundTick() override;
    void frameBegin() override;
    void gameTick(const TickInfo& info) override;
    void frameBody() override;
    uint8_t uiMode() const override;
    bool quitRequested() const override;

    uint64_t rows() const { return rows_; }

    static constexpr std::string_view kHeader =
        "tick,ticks,gated,x,y,camx,camy,state,keys,coins,nets,level,scene,uimode,area,events";

private:
    struct Watched {
        uint8_t uiMode = 0;
        uint16_t level = 0, scene = 0, area = 0, titlePhase = 0, transition = 0, gameInProgress = 0, endState = 0;
        uint8_t screen = 0;
        int context = 0;
        bool dissolve = false, quit = false;
        int writes = 0;
    };
    Watched watch() const;
    void writeRow();

    DosGame& g_;
    std::ostream& out_;
    TraceSound* sound_;
    Watched prev_;
    // The current tick's row, filled by gameTick and written by frameBody.
    struct Row {
        bool valid = false;
        uint64_t tick = 0;
        uint32_t ticks = 0;
        bool gated = false;
        uint16_t x = 0, y = 0, camX = 0, camY = 0, state = 0, keys = 0, coins = 0;
        std::vector<std::string> events;
    } row_;
    // The events of a frame whose own tick was frozen (no row): the next row starts with them.
    std::vector<std::string> pendingEvents_;
    uint64_t rows_ = 0;
};

// Writes dumpToDgroup(g) to `file` and the field registry to `file` + ".fields.tsv"
// (name, ds, size, farPointer, segWordOffset, stride, skipLen, service; hex DS). False on an I/O error.
bool writeDgroupDump(const DosGame& g, const std::filesystem::path& file);

}  // namespace tmport::dos
