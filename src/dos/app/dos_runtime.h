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
//                                gameTick()     Game_Tick 130f:00FE, unchanged order (the client)
//                                               then its two counters (TickCounters, here)
//                                frameBegin()   Frame_Begin; the runtime clears g_stepDone first
//                                frameBody()    main loop body 130f:0037..00C8 minus the present
//   presentation: the host shows the newest completed back buffer at each display refresh.
//
// The order of a frame against its tick (wave 5, PORTING_NOTES.md §2.5): on the reference machine
// the loop runs idle frames of a fraction of a PIT tick between two game ticks, so the frame that
// does a tick's work begins right after that tick: Game_Tick, then Frame_Begin and the frame body.
// A frame that ends after the next tick (a scroll frame, a search frame, a bubble, a scene load) has
// the ticks inside it run without a frame of their own, and the loop's next frame begins when it
// ends: when that is between two ticks, the runtime runs that frame (frameBegin(), frameBody()) before
// the next tick, which may then fall inside it, and the frame after that tick follows if it did not.
// Period mode and the slow-machine mode run frameBegin(), gameTick(), frameBody() for every frame
// (their frames last a game tick or more).
//
// In lockstep the step gate blocks only inside a long frame (below); otherwise Frame_Begin clears it
// between two ticks, which is what the DOSBox-X reference showed for normal play.
// FrameMode::SlowMachine keeps the gate's meaning: an original frame spans several game ticks, and the
// ticks after the first see g_stepDone (only Anim_Tick and Input_Tick run when it is set), so the
// game slows down with the frame rate as on a slow PC.
//
// Long frames (frame_speed = reference, the default; PORTING_NOTES.md §2.5): on the reference machine
// a frame that loads a scene, writes USERS.DAT or redraws the whole screen lasts several game ticks.
// The game reports what its frame cost (FrameTiming, from what the frame drew and loaded, never from
// host time); the ticks that fall inside the frame then run without a frame of their own (no
// Frame_Begin, so g_stepDone keeps what they set and gates the ticks after a step), and the ticks
// inside its I/O (g_quit DS:912E = 1) do not run Game_Tick at all. frame_speed = period keeps strict
// lockstep: every frame fits into its game tick and I/O is instant.
//
// Per-frame features (search, coin count-up, CHAMPION flash, title sparkles, dissolve) take their
// step counts from the OriginalFrameClock: "reference" rates measured in DOSBox-X (code_runtime.md
// §9-10) or "period" = one step per original frame.

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "dos/app/script_input.h"
#include "dos/input/keyboard.h"
#include "dos/input/mouse.h"
#include "edition/dos_options.h"
#include "timing/pit_clock.h"

namespace tmport::dos {

// ------------------------------------------------------------------------------- the time base

inline constexpr uint32_t kPitDivisor = kPitDivisorTmDos;  // 0x13B1 = 5041, written to port 40h by Timer_Install
inline constexpr uint32_t kPitTicksPerGameTick = 5;  // DS:3D34 counts 4..0: Game_Tick on interrupt 5, 10, ...
inline constexpr uint32_t kPitTicksPerBiosTick = 13; // DS:3D33 counts 12..0 (the BIOS clock chain; dropped)

// The PIT input clock is 14.31818 MHz / 12 = 1193181.67 Hz = 3579545 / 3 Hz exactly; one PIT tick
// lasts 3 * 5041 / 3579545 s = 4.22484 ms (236.69543 Hz), one game tick 21.1242 ms (47.33909 Hz).
// The time base is the shared core's (timing/pit_clock.h) at SST.EXE's divisor.
struct PitClock {
    static constexpr double kPitHz = 3579545.0 / 3.0 / kPitDivisor;
    static constexpr double kGameTickHz = kPitHz / kPitTicksPerGameTick;
    static const PitTimeBase& timeBase();
    // Virtual time (ns) of PIT tick k (k = 1 is the first interrupt after Timer_Install):
    // ceil(k * 15123e9 / 3579545), exact for any k below 10^12.
    static uint64_t timeOfPitTick(uint64_t k) { return timeBase().timeOfTick(k); }
    // The number of PIT ticks whose time is <= t.
    static uint64_t pitTicksAt(uint64_t tNs) { return timeBase().ticksAt(tNs); }
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
    Search,         // Search_Update 4a03:0003, g_searchTimer per frame (one per frame here; at the
                    // reference speed main_loop.cpp times the search frames themselves)
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
    // 0 = one step per original frame. In lockstep at the reference speed hud.cpp and title.cpp time
    // the count-up and the title loop themselves (code_ui.md §8.5, §11.2); these two rates then serve
    // the slow-machine runtime only.
    //   CoinCountUp   71.3/s    (3.32 PIT ticks = 14.0 ms per coin, r2/fl_countup_after)
    //   ChampionFlash 918.2/s   (640 flash frames in 0.697 s: 42 frames of avi/ega_r2_crown_nokey.avi
    //                            at the 60.29 frames/s of the EGA AVIs)
    //   TitleSparkle  1730/s    (title loop attempts per second, code_ui.md §8.5)
    //   Dissolve      2755.9/s  (350 cells in 127 ms, r2/fl_l2door_after)
    static constexpr uint64_t referenceRateMilli(FrameFeature f) {
        switch (f) {
            case FrameFeature::CoinCountUp: return 71'300;
            case FrameFeature::ChampionFlash: return 918'200;
            case FrameFeature::TitleSparkle: return 1'730'000;
            case FrameFeature::Dissolve: return 2'755'906;
            default: return 0;
        }
    }

    // The runtime calls this at the start of each original frame with its length in PIT ticks.
    void beginFrame(uint32_t pitTicks);
    // How many of its per-frame steps a feature runs in the current original frame. Period mode: 1
    // (the dissolve: all 350 cells in the frame).
    int steps(FrameFeature f) const { return steps_[static_cast<size_t>(f)]; }
    // A feature starts: drop its fractional remainder (the steps of the current frame stay).
    void reset(FrameFeature f) { acc_[static_cast<size_t>(f)] = 0; }
    // The steps of the current frame, when the game knows better (a dissolve that starts later in
    // the frame); the following frames count on from the feature's remainder as before.
    void setSteps(FrameFeature f, int n) { steps_[static_cast<size_t>(f)] = n; }

private:
    FrameSpeed speed_;
    std::array<uint64_t, static_cast<size_t>(FrameFeature::Count)> acc_{};
    std::array<int, static_cast<size_t>(FrameFeature::Count)> steps_{};
};

// ------------------------------------------------------------------------- the reference frame timing

// What the original frame being run cost on the reference machine (DOSBox-X, cycles=fixed 6000:
// PORTING_NOTES.md §2.5, code_runtime.md §1.4), in thousandths of a PIT tick. The game reports it
// from what the frame loaded and drew (frameBegin and the frame body); the runtime reads it after the
// body. On the reference machine idle frames take a fraction of a PIT tick, so a costly frame begins
// right after whatever asked for its work, and its cost counts from its start (setStart):
//   * a frame that begins right after its own game tick (beganAfterTick(): the usual case; the runtime
//     runs Game_Tick first): Frame_Begin latches what that tick asked for (a scene, a redraw, a camera
//     step) and the body reads the key it delivered;
//   * a frame that begins when a longer frame ends, before the next game tick (the ticks that fell
//     inside the long one ran Game_Tick, so no Frame_Begin has latched them yet; or the long frame was
//     a dissolve or a Button_Flash wait): the runtime runs it before that tick, which may fall inside
//     it, and inside its I/O;
//   * the game ticks inside a frame start with g_stepDone as its Frame_Begin left it (clear);
//   * costs reported before the first frame (the title's start-up load in main) count from PIT tick 0.
class FrameTiming {
public:
    static constexpr uint32_t kMilli = 1000;  // units per PIT tick

    struct Span {
        uint32_t from = 0, to = 0;  // [from, to) in units after where the frame counts from
        bool operator==(const Span&) const = default;
    };

    // The frame goes on for `units` more (drawing, presenting).
    void spend(uint32_t units) { cursor_ += units; }
    // A g_quit section of `units` starts here (file I/O and unpacking with g_quit DS:912E = 1): the
    // game ticks inside it return at once (130f:0103), without their two counters. One that follows
    // another directly extends it.
    void quitSection(uint32_t units) {
        if (units == 0) return;
        if (!quit_.empty() && quit_.back().to == cursor_) quit_.back().to += units;
        else quit_.push_back({cursor_, cursor_ + units});
        cursor_ += units;
    }
    // The work was asked for by a game tick before the frame (Frame_Begin's latches, the end of a
    // transition, the hint clock). Informational since wave 5: a frame's work counts from its start
    // (the frames before wave 5 began before their own tick, and this placed such work after the
    // previous one).
    void fromPreviousTick() { fromPrevious_ = true; }
    // The original's frame goes on past the next game tick without a Frame_Begin (a dissolve that
    // spans frames, a Button_Flash wait): the engine's next frame continues it (after its tick, g_stepDone
    // left as the ticks set it; DosGame::frameBegin latches nothing then), and the first frame after it
    // begins when it ends, before the next tick. Reported by the frame body; read after the body.
    void continueFrame() { continues_ = true; }
    bool continuesFrame() const { return continues_; }

    // Where the frame stands, set by the runtime before Frame_Begin: its start (units since PIT tick 0)
    // and the first game tick after its body. A frame that begins right after its own tick has the next
    // tick one game tick after its start (beganAfterTick); one that begins when a longer frame ended has
    // its own tick still to come, less than a game tick after its start.
    void setStart(uint64_t startUnits, uint64_t nextTickUnits) {
        start_ = startUnits;
        nextTick_ = nextTickUnits;
    }
    uint64_t startUnits() const { return start_; }
    uint64_t nextTickUnits() const { return nextTick_; }
    // The work the frame reports began earlier than the runtime's start: a wait inside the frame
    // (Button_Flash's Delay_Ticks) ended between two game ticks, before the tick after which the
    // engine noticed. The runtime places the frame's cost from here.
    void backdateStart(uint64_t units) { start_ -= std::min(units, start_); }
    uint32_t untilNextTick() const { return static_cast<uint32_t>(nextTick_ - start_); }
    bool beganAfterTick() const { return nextTick_ - start_ == kTickUnits; }

    static constexpr uint32_t kTickUnits = kPitTicksPerGameTick * kMilli;  // one game tick

    // Open while the runtime runs a frame at the reference timing (Frame_Begin and the body) and
    // during main's start-up (mainStart, before the first frame; clear() closes it). The game reports
    // a cost where the original does the work (wave 5: the I/O routines report themselves only while
    // it is open, main_loop.h), so the cost reported so far is where the frame stands: a sound call
    // made there reaches the driver at that place (DosRuntime::soundDelay).
    void open() { open_ = true; }
    bool isOpen() const { return open_; }
    // A part of the frame that keeps its own clock (the coin count-up of Hud_Redraw) names the time of
    // the calls it makes, in units since PIT tick 0; 0 (or a time before it) = where the reported cost
    // has got to.
    void setCallTime(uint64_t units) { callTime_ = units; }
    uint64_t callTime() const { return std::max(callTime_, start_ + cursor_); }

    uint32_t length() const { return cursor_; }
    const std::vector<Span>& quitSections() const { return quit_; }
    bool countsFromPreviousTick() const { return fromPrevious_; }
    bool empty() const { return cursor_ == 0 && quit_.empty(); }
    void clear() {
        cursor_ = 0;
        quit_.clear();
        fromPrevious_ = false;
        continues_ = false;
        open_ = false;
        callTime_ = 0;
        start_ = 0;
        nextTick_ = kTickUnits;
    }

private:
    uint32_t cursor_ = 0;
    std::vector<Span> quit_;
    bool fromPrevious_ = false;
    bool continues_ = false;
    bool open_ = false;
    uint64_t callTime_ = 0;
    uint64_t start_ = 0;
    uint64_t nextTick_ = kTickUnits;
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
    bool inLongFrame = false;  // the tick falls inside a long frame (no Frame_Begin before it)
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
    // Long frames and I/O stalls are modelled (lockstep at the reference frame speed).
    bool longFrames() const { return options_.frameMode == FrameMode::Lockstep && options_.frameSpeed == FrameSpeed::Reference; }

    // Real time: feeds host time and runs every PIT tick that became due (sound ticks, game ticks
    // with their frames), at most maxCatchUpGameTicks game ticks per call. Returns game ticks run.
    uint32_t advance(uint64_t hostDeltaNs);
    // Deterministic stepping (tests, --keys runs): exactly n more game ticks / PIT ticks.
    void stepGameTicks(uint64_t n);
    void stepPitTicks(uint64_t n);
    // Starts the time base at a saved moment of the original (a DOSBox-X state, dos/game/load_state.h):
    // g_ticks reads `gTicks` now and the next game tick comes after `pitToNextGameTick` PIT ticks
    // (1..5, the countdown DS:3D34 + 1); game ticks and script ticks count from here (game tick 1 is
    // the first one after it). Only on a runtime that has not run yet; the PIT ticks before the next
    // game tick run their sound ticks.
    void resumeAt(uint32_t gTicks, uint32_t pitToNextGameTick);

    uint64_t nowNs() const { return PitClock::timeOfPitTick(pit_) + phaseNs_; }
    uint64_t pitTicks() const { return pit_; }
    uint32_t ticks() const { return static_cast<uint32_t>(pit_) + ticksOffset_; }  // g_ticks (wraps at 2^32)
    uint64_t gameTicks() const { return pit_ / kPitTicksPerGameTick; }
    uint64_t frames() const { return frames_; }
    uint64_t droppedPitTicks() const { return droppedPit_; }
    // False once the client asked to quit (after that frame); advance() and stepping then do nothing.
    bool running() const { return running_; }

    StepGate& stepGate() { return stepGate_; }
    TickCounters& counters() { return counters_; }
    OriginalFrameClock& frameClock() { return frameClock_; }
    // The cost of the frame being run (the game writes it in frameBegin and the frame body, see
    // FrameTiming).
    FrameTiming& frameTiming() { return frameTiming_; }
    // Where a sound call made now reaches the driver (wave 5). The original's driver entry points write
    // the driver's blocks at once and the next timer interrupt plays them, so a call made partway
    // through a long frame sounds that far into it: in a frame of the reference timing the call is at
    // the place the frame has reached (its start plus the cost reported so far, FrameTiming), and the
    // result is the number of PIT ticks between the runtime's next interrupt and the one after that
    // place (DosAudioDevice delays the command by them; FrameTiming::callTime). 0 outside a frame (the
    // calls of a game tick run in its interrupt) and at the period speed.
    uint32_t soundDelay() const;
    PcKeyboard& keyboard() { return keyboard_; }
    MouseDriver& mouse() { return mouse_; }
    // Game ticks so far that ran inside a long frame (without a frame of their own), and those of
    // them inside its I/O (Game_Tick returned at once).
    uint64_t ticksInLongFrames() const { return ticksInLongFrames_; }
    uint64_t ticksFrozen() const { return ticksFrozen_; }
    // Where the last frame ended on the reference machine, in FrameTiming units since PIT tick 0 (the
    // runtime's count): its picture was presented then (the host shows it from the last game tick
    // before that time, dos_entry.cpp).
    uint64_t frameEndUnits() const { return frameEnd_; }

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
    void gameStepReference(uint64_t n, RuntimeClient& c, uint64_t timeNs, uint64_t now);
    void runFrame(RuntimeClient& c, uint64_t start, uint64_t nextTick, bool continued);
    void runGameTick(RuntimeClient& c, uint64_t n, uint64_t timeNs, bool inFrame);
    uint64_t unitsNow() const { return pit_ * FrameTiming::kMilli; }  // FrameTiming units since PIT tick 0
    void applyScript(uint64_t n, uint64_t timeNs);
    uint32_t ticksPerFrame() const;
    void placeFrameTiming(uint64_t base);
    bool frozenAt(uint64_t units) const;

    RuntimeOptions options_;
    RuntimeClient* client_ = nullptr;
    RuntimeClient nullClient_;
    uint64_t pit_ = 0;        // PIT ticks run
    uint32_t ticksOffset_ = 0;  // g_ticks - pit_ (resumeAt)
    uint64_t phaseNs_ = 0;    // virtual time since PIT tick pit_
    uint64_t droppedPit_ = 0;
    uint64_t frames_ = 0;
    bool running_ = true;
    StepGate stepGate_;
    TickCounters counters_;
    OriginalFrameClock frameClock_;
    FrameTiming frameTiming_;
    struct AbsSpan {
        uint64_t from = 0, to = 0;
    };
    uint64_t frameEnd_ = 0;        // end of the current long frame (FrameTiming units since PIT tick 0)
    std::vector<AbsSpan> frozen_;  // its g_quit sections, in the same absolute units
    bool continuing_ = false;  // the last frame body reported FrameTiming::continueFrame
    uint64_t ticksInLongFrames_ = 0;
    uint64_t ticksFrozen_ = 0;
    PcKeyboard keyboard_;
    MouseDriver mouse_;
    ScriptedInput script_;
};

}  // namespace tmport::dos
