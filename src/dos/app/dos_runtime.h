#pragma once
// The DOS edition's time base and frame loop (re/dos/docs/code_runtime.md §1-3, §9-10, §12;
// PORTING_NOTES.md §2.3-2.4, §4).
//
// The original: PIT channel 0 at 1193181.67 / 5041 Hz (Timer_Install 1000:03C5). Every interrupt
// (Timer_ISR 1000:0428) counts g_ticks and runs the sound sequencer; every 5th runs Game_Tick
// (130f:00FE) inside the interrupt. The main loop (main 130f:0007) runs as fast as the CPU allows:
// Frame_Begin (latches, clears the step flag g_stepDone DS:910D), UI updaters, drawing, present.
// A game tick that finds g_stepDone set (an animation script advanced or the camera moved since the
// frame began) runs only Anim_Tick and Input_Tick.
//
// The engine (PORTING_NOTES.md §2.3):
//
//   host clock (ns) --> virtual PIT (exact rational time base, no drift)
//                        |-- every PIT tick: RuntimeClient::soundTick()
//                        '-- every 5th PIT tick: one ORIGINAL FRAME ("lockstep"):
//                                scripted input of this tick, keyboard bytes due (Kbd_ISR, BIOS)
//                                frameBegin()   Frame_Begin; the runtime clears g_stepDone first
//                                gameTick()     Game_Tick 130f:00FE, unchanged order (the client)
//                                               then its two counters (TickCounters, here)
//                                frameBody()    main loop body 130f:0037..00C8 minus the present
//   presentation: the host shows the newest completed back buffer at each display refresh.
//
// In lockstep the step gate never blocks (it is cleared right before each tick), which is what the
// DOSBox-X reference showed. FrameMode::SlowMachine keeps the gate's meaning: an original frame
// spans several game ticks, and the ticks after the first see g_stepDone (only Anim_Tick and
// Input_Tick run when it is set), so the game slows down with the frame rate as on a slow PC.
//
// Per-frame features (search, coin count-up, CHAMPION flash, title sparkles, dissolve) take their
// step counts from the OriginalFrameClock: "reference" rates measured in DOSBox-X (code_runtime.md
// §9-10) or "period" = one step per original frame.

#include <array>
#include <cstdint>

#include "dos/app/script_input.h"
#include "dos/input/keyboard.h"
#include "dos/input/mouse.h"
#include "edition/dos_options.h"

namespace tmport::dos {

// ------------------------------------------------------------------------------- the time base

inline constexpr uint32_t kPitDivisor = 0x13B1;      // 5041, written to port 40h by Timer_Install
inline constexpr uint32_t kPitTicksPerGameTick = 5;  // DS:3D34 counts 4..0: Game_Tick on interrupt 5, 10, ...
inline constexpr uint32_t kPitTicksPerBiosTick = 13; // DS:3D33 counts 12..0 (the BIOS clock chain; dropped)

// The PIT input clock is 14.31818 MHz / 12 = 1193181.67 Hz = 3579545 / 3 Hz exactly; one PIT tick
// lasts 3 * 5041 / 3579545 s = 4.22484 ms (236.69543 Hz), one game tick 21.1242 ms (47.33909 Hz).
struct PitClock {
    static constexpr double kPitHz = 3579545.0 / 3.0 / kPitDivisor;
    static constexpr double kGameTickHz = kPitHz / kPitTicksPerGameTick;
    // Virtual time (ns) of PIT tick k (k = 1 is the first interrupt after Timer_Install):
    // ceil(k * 15123e9 / 3579545), exact for any k below 10^12.
    static uint64_t timeOfPitTick(uint64_t k);
    // The number of PIT ticks whose time is <= t.
    static uint64_t pitTicksAt(uint64_t tNs);
    static uint64_t timeOfGameTick(uint64_t n) { return timeOfPitTick(n * kPitTicksPerGameTick); }
};

// g_stepDone (DS:910D).
class StepGate {
public:
    void clear() { done_ = false; }  // Frame_Begin 130f:02BE (also when game code calls Frame_Begin)
    void mark() { done_ = true; }    // Anim_Tick 4306:0298 (a script advanced), Player_Move 18cf:175F (camera moved)
    bool done() const { return done_; }

private:
    bool done_ = false;
};

// The two counters at the end of Game_Tick (130f:0186..01BF), run in both of its paths.
struct TickCounters {
    int16_t cycle13 = 12;         // DS:90FE: 12..0, read by the name-entry caret blink (DS:90FE <= 6)
    uint8_t secondCountdown = 60; // DS:9100 (byte): 60-tick "second" of the hint timers
    uint16_t seconds = 0;         // g_seconds DS:9115

    // DS:90FE -= 1 (below 0: 12); DS:9100 -= 1, and when it reaches 0 with no UI mode open: 60 and
    // g_seconds += 1. With a UI mode open the byte keeps counting through 0 to 255 (period 256).
    void advance(uint8_t uiMode);
};

// ----------------------------------------------------------------------- the original frame clock

// FrameSpeed (edition/dos_options.h, the frame_speed option): Reference = the per-frame rates of
// the DOSBox-X reference machine (default); Period = one step per original frame, as on a period PC
// whose loop runs about once per tick.

enum class FrameFeature : uint8_t {
    Search,         // Search_Update 4a03:0003, g_searchTimer per frame (kept per frame in both modes)
    CoinCountUp,    // Hud_Redraw 130f:07A5, one coin (and effect 4) per frame
    ChampionFlash,  // Rank_Update 130f:0EDF, DS:0100 -1 per frame
    TitleSparkle,   // title loop, one sparkle attempt (rand() & 0x7F == 0) per frame
    Dissolve,       // Frame_Present 130f:01C2, 350 cells (counted in cells)
    Count
};

inline constexpr int kDissolveCells = 0x15E;  // 350

class OriginalFrameClock {
public:
    explicit OriginalFrameClock(FrameSpeed speed = FrameSpeed::Reference) : speed_(speed) {}
    FrameSpeed speed() const { return speed_; }
    void setSpeed(FrameSpeed s) { speed_ = s; }

    // Reference rate of a feature in steps per 1000 s (code_runtime.md §9, PORTING_NOTES.md §2.3);
    // 0 = one step per original frame.
    //   CoinCountUp   76.336/s  (13.1 ms per coin: 20 coins in 262 ms, r2/fl_countup_after)
    //   ChampionFlash 914.286/s (640 flash frames in 0.70 s, avi/ega_r2_crown_nokey.avi)
    //   TitleSparkle  1690/s    (idle loop frames per second)
    //   Dissolve      2755.9/s  (350 cells in 127 ms, r2/fl_l2door_after)
    static constexpr uint64_t referenceRateMilli(FrameFeature f) {
        switch (f) {
            case FrameFeature::CoinCountUp: return 76'336;
            case FrameFeature::ChampionFlash: return 914'286;
            case FrameFeature::TitleSparkle: return 1'690'000;
            case FrameFeature::Dissolve: return 2'755'906;
            default: return 0;
        }
    }

    // The runtime calls this at the start of each original frame with its length in PIT ticks.
    void beginFrame(uint32_t pitTicks);
    // How many of its per-frame steps a feature runs in the current original frame. Period mode: 1
    // (the dissolve: all 350 cells in the frame); Search: 1 in both modes.
    int steps(FrameFeature f) const { return steps_[static_cast<size_t>(f)]; }
    // A feature starts: drop its fractional remainder (the steps of the current frame stay).
    void reset(FrameFeature f) { acc_[static_cast<size_t>(f)] = 0; }

private:
    FrameSpeed speed_;
    std::array<uint64_t, static_cast<size_t>(FrameFeature::Count)> acc_{};
    std::array<int, static_cast<size_t>(FrameFeature::Count)> steps_{};
};

// ----------------------------------------------------------------------------------- the runtime

enum class FrameMode : uint8_t {
    Lockstep,     // one original frame per game tick (default)
    SlowMachine,  // one original frame per slowMachineTicksPerFrame game ticks, the step gate active
};

struct RuntimeOptions {
    FrameMode frameMode = FrameMode::Lockstep;
    uint32_t slowMachineTicksPerFrame = 2;
    // advance(): at most this many game ticks per call; a longer host stall is dropped (the virtual
    // clock falls behind) instead of running many logic steps per presented frame. 0 = no cap.
    uint32_t maxCatchUpGameTicks = 4;
    FrameSpeed frameSpeed = FrameSpeed::Reference;
    KeyboardOptions keyboard;
};

struct TickInfo {
    uint64_t gameTick = 0;  // 1, 2, ... since Timer_Install
    uint32_t ticks = 0;     // g_ticks DS:3D2B (u32 PIT interrupt count)
    bool gated = false;     // g_stepDone was set: run only Anim_Tick and Input_Tick (130f:0190)
    uint64_t timeNs = 0;    // virtual time of the tick
};

// What the game (wave 3 main_loop) implements. Every callback runs on the caller's thread inside
// advance() / stepGameTicks().
class RuntimeClient {
public:
    virtual ~RuntimeClient() = default;
    // Timer_ISR step 4: every PIT tick (the client applies the Sound_Tick gate (DS:64C0 & DS:37CE) |
    // DS:6448 itself, or renders the sequencer by sample count in the audio callback instead).
    virtual void soundTick() {}
    // Frame_Begin 130f:025A (g_stepDone is already clear).
    virtual void frameBegin() {}
    // Game_Tick 130f:00FE without its early g_quit return and its two counters (both done here).
    virtual void gameTick(const TickInfo&) {}
    // The main loop body (130f:003D..00C8) up to and without Frame_Present.
    virtual void frameBody() {}
    // g_uiMode DS:9119, for the hint second.
    virtual uint8_t uiMode() const { return 0; }
    // g_quit DS:912E: game ticks return at once (130f:0103) and the loop ends after the frame.
    virtual bool quitRequested() const { return false; }
};

// Host wiring (dos::runDosEdition, dos/app/dos_entry.cpp): per host frame,
// translate SDL key events with hostKey(scancode, down, event.key.repeat), the pointer with
// mouse().setLogicalPosition(x, y) in 320x200 canvas pixels and mouse().setButton(...), call
// releaseAllKeys() on focus loss, then advance(host ns since the last frame) and present the newest
// finished back buffer. Headless --keys runs: setScript(ScriptedInput::parse(options.keys)) and
// stepGameTicks() until script().finished().
class DosRuntime {
public:
    explicit DosRuntime(RuntimeOptions options = {});

    void setClient(RuntimeClient* client) { client_ = client; }
    const RuntimeOptions& options() const { return options_; }
    void setFrameSpeed(FrameSpeed s) { options_.frameSpeed = s; frameClock_.setSpeed(s); }

    // Real time: feeds host time and runs every PIT tick that became due (sound ticks, game ticks
    // with their frames), at most maxCatchUpGameTicks game ticks per call. Returns game ticks run.
    uint32_t advance(uint64_t hostDeltaNs);
    // Deterministic stepping (tests, --keys runs): exactly n more game ticks / PIT ticks.
    void stepGameTicks(uint64_t n);
    void stepPitTicks(uint64_t n);

    uint64_t nowNs() const { return PitClock::timeOfPitTick(pit_) + phaseNs_; }
    uint64_t pitTicks() const { return pit_; }
    uint32_t ticks() const { return static_cast<uint32_t>(pit_); }  // g_ticks (wraps at 2^32)
    uint64_t gameTicks() const { return pit_ / kPitTicksPerGameTick; }
    uint64_t frames() const { return frames_; }
    uint64_t droppedPitTicks() const { return droppedPit_; }
    // False once the client asked to quit (after that frame); advance() and stepping then do nothing.
    bool running() const { return running_; }

    StepGate& stepGate() { return stepGate_; }
    TickCounters& counters() { return counters_; }
    OriginalFrameClock& frameClock() { return frameClock_; }
    PcKeyboard& keyboard() { return keyboard_; }
    MouseDriver& mouse() { return mouse_; }

    // Host input, stamped with the current virtual time (applied before the next game tick).
    void keyDownNow(PcKey k) { keyboard_.keyDown(k, nowNs()); }
    void keyUpNow(PcKey k) { keyboard_.keyUp(k, nowNs()); }
    // The host's SDL key event (SDL_Scancode value): mapped by pcKeyForSdlScancode; host auto-repeats
    // are ignored (the PcKeyboard makes the repeats). Returns false for keys a PC keyboard lacks.
    bool hostKey(uint32_t sdlScancode, bool down, bool hostRepeat);
    // Focus loss: every held key goes up.
    void releaseAllKeys() { keyboard_.releaseAll(nowNs()); }
    // Scripted input in game ticks: events of tick n are applied right before game tick n.
    void setScript(ScriptedInput script) { script_ = std::move(script); }
    const ScriptedInput& script() const { return script_; }

private:
    void runPitTicksTo(uint64_t lastPit);
    void gameStep(uint64_t n);
    void applyScript(uint64_t n, uint64_t timeNs);
    uint32_t ticksPerFrame() const;

    RuntimeOptions options_;
    RuntimeClient* client_ = nullptr;
    RuntimeClient nullClient_;
    uint64_t pit_ = 0;        // PIT ticks run
    uint64_t phaseNs_ = 0;    // virtual time since PIT tick pit_
    uint64_t droppedPit_ = 0;
    uint64_t frames_ = 0;
    bool running_ = true;
    StepGate stepGate_;
    TickCounters counters_;
    OriginalFrameClock frameClock_;
    PcKeyboard keyboard_;
    MouseDriver mouse_;
    ScriptedInput script_;
};

}  // namespace tmport::dos
