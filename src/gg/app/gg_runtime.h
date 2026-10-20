#pragma once
// GgRuntime: the engine's model of the PC under Gizmos & Gadgets (re/gg/docs/PORTING_NOTES.md §2.2,
// code_runtime.md §1, §5-§9, §20). The original is a tree of ordinary nested loops; no game logic
// runs in an interrupt. Its loops pace themselves on the deadline timers and poll the input:
//
//   frame loop:     Timer_Set(0, 4) ... draw ... Dirty_Flush ... Timer_Wait(0); Input_GetCode()
//   unpaced loop:   Input_GetCode() as fast as the machine runs (puzzles, dialogs, menus,
//                   Input_WaitCode, busy-waits on sound or on the mouse)
//
// Every wait and every poll is a YIELD POINT of this runtime, where the host is pumped and time
// passes:
//
//   timerWait(slot)     Timer_Wait 152c:0029: until g_ticks >= the slot's deadline
//   delay(n)            Delay 152c:0072: n ticks, calling Input_GetCode (codes lost, global keys act)
//   getInputCode()      Input_GetCode 198d:0106 as a poll
//   timerExpired(slot)  Timer_Expired 152c:004a as a poll
//   waitInputCode()     Input_WaitCode 198d:02a5
//   idle()              one turn of any other busy-wait (sound, mouse latches): the loop body must
//                       call it (or another poll), or a deterministic run cannot move on
//   waitRetrace()       the retrace waits of Pal_SetRange 1399:05d6 (two per palette load)
//
// Time. A virtual PIT (GgClock: 145.652 Hz, exact rational time base) runs Timer_ISR's work at its
// interrupts: g_ticks, the sound tick on even ticks. Keyboard bytes (Kbd_ISR) are delivered in time
// order between the interrupts and stamped with the g_ticks of their moment; mouse events go through
// the INT 33h event handler at once (both run "at interrupt time" in the original).
//
//   Real time (a window): each yield point pumps the host (SDL events, focus freeze, rendering at
//   most once per display refresh) and moves the virtual time by the host time that passed (at most
//   maxCatchUpTicks ticks after a stall: the rest is dropped). Waits sleep between polls.
//   Deterministic (headless, tests): the virtual time moves only at yield points: a wait jumps to its
//   end, a palette load to the next retrace; the work between two yield points costs nothing. The
//   first poll after a wait is free; every further poll lets pollStepNs pass (never across a tick
//   boundary in one step), so busy-waits and unpaced loops move on and frame loops keep their exact
//   4-tick (or 8-tick) periods. A wait that ends at once (its deadline already reached) counts as a
//   poll, so a loop of such waits moves on too.
//
// Observers (the per-frame trace, the test fixture's frame stepping) see every yield point through
// the yield hook (setYieldHook), called when the yield point is about to return; it may throw (e.g.
// GgRunEnd or a test's stop request) to unwind the game's call tree from there.
//
// Scripted input (dos::ScriptedInput, --keys / --mouse) is in PIT ticks: an event of tick n happens
// at the time of interrupt n, right before it (N.F: F of a tick after interrupt N). The run limit
// (setRunLimit) ends a run with GgRunEnd from a yield point before interrupt limit + 1; a closed
// window ends it too. The caller catches GgRunEnd where the original's program would have ended.
//
// Pictures: the graphics module hands over each new VGA picture as 320x200 RGBA (showFrame, the
// presenter's pixel convention); the runtime presents it on the host and passes it to the frame sink.

#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "dos/app/script_input.h"
#include "gg/app/gg_clock.h"
#include "gg/app/timers.h"
#include "gg/input/input_codes.h"
#include "gg/input/keyboard.h"
#include "gg/input/mouse.h"
#include "gg/input/zones.h"
#include "timing/clock.h"

namespace tmport::platform {
class Host;
}

namespace tmport::gg {

// Thrown from a yield point when the run is over: the run limit was reached (headless runs, --ticks)
// or the host window was closed.
struct GgRunEnd : std::exception {
    enum class Reason : uint8_t { Limit, HostClosed };
    Reason reason = Reason::Limit;
    explicit GgRunEnd(Reason r) : reason(r) {}
    const char* what() const noexcept override {
        return reason == Reason::Limit ? "run limit reached" : "window closed";
    }
};

// The yield points, for the yield hook.
enum class YieldPoint : uint8_t {
    TimerWait,     // Timer_Wait(slot): slot 0 ends a paced frame
    TimerExpired,  // Timer_Expired(slot)
    Delay,         // Delay(n) (after its own Input_GetCode polls, which the hook sees too)
    GetInputCode,  // Input_GetCode
    WaitInputCode, // Input_WaitCode (after its polls)
    Idle,          // a busy-wait's poll
    WaitRetrace,   // a retrace wait of a palette load
    WaitTicks,     // waitTicks (engine tools and tests)
};
const char* yieldPointName(YieldPoint p);

struct RuntimeConfig {
    // Deterministic virtual time (headless runs, tests); false: the host clock drives it.
    bool deterministic = true;
    // Deterministic: the time each poll after the first since the last wait lets pass (at most up to
    // the next PIT tick). 1/16 of a PIT tick: 16 polls per tick.
    uint64_t pollStepNs = 429'105;
    // Real time: at most this many PIT ticks per pump; a longer host stall is dropped.
    uint32_t maxCatchUpTicks = 32;
    // The time of retrace 0 (the retrace phase against the PIT; unknown for the reference, 0).
    uint64_t retracePhaseNs = 0;
    GgKeyboardOptions keyboard;
    GgMouseOptions mouse;
    // Real time without a host: the clock to read and sleep on (nullptr: Clock::system()).
    Clock* clock = nullptr;
};

class GgRuntime {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 200;
    // The frame sink: the interrupt count when the picture was shown, and the picture.
    using FrameSink = std::function<void(uint64_t interrupt, std::span<const uint32_t> rgba)>;

    // `host` (optional, not owned): its handlers are replaced by the runtime's (keyboard, mouse,
    // focus) until the runtime is destroyed. Without a host, real time reads config.clock.
    explicit GgRuntime(RuntimeConfig config = {}, platform::Host* host = nullptr);
    ~GgRuntime();
    GgRuntime(const GgRuntime&) = delete;
    GgRuntime& operator=(const GgRuntime&) = delete;

    const RuntimeConfig& config() const { return config_; }
    bool deterministic() const { return config_.deterministic; }
    platform::Host* host() const { return host_; }

    GgClock& clock() { return clock_; }
    const GgClock& clock() const { return clock_; }
    GgTimers& timers() { return timers_; }
    GgKeyboard& keyboard() { return keyboard_; }
    GgMouse& mouse() { return mouse_; }
    ClickZones& zones() { return zones_; }
    InputCodes& input() { return input_; }
    // g_ticks DS:021a.
    uint32_t ticks() const { return clock_.ticks(); }
    uint64_t nowNs() const { return clock_.nowNs(); }

    // ---- start-up and shutdown
    // Input_InstallHandlers 1454:0000: the mouse event handler (INT 33h AX=0Ch, mask 1Fh) and the
    // game's INT 9 / INT 8 (the keyboard and the clock run from the start in the engine).
    void installHandlers();
    // Input_RemoveHandlers 1454:0058 (Init_Shutdown 303a:0250): the mouse handler goes.
    void removeHandlers();
    bool handlersInstalled() const { return mouse_.handlerInstalled(); }

    // ---- yield points (see the file comment)
    void timerWait(int slot);                // 152c:0029 Timer_Wait
    bool timerExpired(int slot);             // 152c:004a Timer_Expired (a poll)
    void delay(uint16_t n);                  // 152c:0072 Delay
    uint16_t getInputCode();                 // 198d:0106 Input_GetCode (a poll)
    uint16_t waitInputCode();                // 198d:02a5 Input_WaitCode
    void flushInput() { input_.flush(); }    // 198d:00bf Input_Flush (no time passes)
    void idle();                             // a busy-wait's poll
    void waitRetrace();                      // to the start of the next vertical retrace
    // Lets n PIT ticks pass (engine tools and tests; not an original call).
    void waitTicks(uint64_t n);
    // The yield hook (see the file comment): (the yield point, the timer slot for TimerWait and
    // TimerExpired, else -1). Empty: none.
    using YieldHook = std::function<void(YieldPoint, int slot)>;
    void setYieldHook(YieldHook hook) { yieldHook_ = std::move(hook); }
    const YieldHook& yieldHook() const { return yieldHook_; }

    // ---- pictures
    // A new VGA picture (kWidth x kHeight RGBA, presenter convention); other sizes are ignored.
    void showFrame(std::span<const uint32_t> rgba);
    const std::vector<uint32_t>& frame() const { return frame_; }
    uint64_t framesShown() const { return framesShown_; }
    void setFrameSink(FrameSink sink) { frameSink_ = std::move(sink); }

    // ---- scripted input and the run limit
    void setScript(dos::ScriptedInput script);
    // Adds events to the script (tests, tools): `more`'s ticks + `offset` (e.g. the interrupts so far
    // for a script relative to now), merged with the events not applied yet.
    void addScript(const dos::ScriptedInput& more, uint64_t offset = 0);
    const dos::ScriptedInput& script() const { return script_; }
    bool scriptFinished() const { return scriptNext_ >= script_.events().size(); }
    // The interrupt at whose time the last event happens (0 without events).
    uint64_t scriptEndTick() const;
    // The run ends (GgRunEnd) at a yield point before interrupt lastTick + 1 would run; nullopt: no
    // limit.
    void setRunLimit(std::optional<uint64_t> lastTick) { limit_ = lastTick; }
    std::optional<uint64_t> runLimit() const { return limit_; }

    // ---- driving the time directly (deterministic tools and tests; real time does it in pump())
    // Moves the virtual time to t (never back): script events, keyboard bytes and interrupts in
    // time order (an event at an interrupt's own time first). May throw GgRunEnd.
    void advanceTo(uint64_t tNs);
    // To the time of interrupt interrupts() + n.
    void advanceTicks(uint64_t n);
    // Real time: one round of host work (events, focus freeze, the time that passed, rendering).
    // Deterministic: the host's queued events only. May throw GgRunEnd.
    void pump();

    // ---- host input (what the runtime's host handlers call; tests may call them directly)
    // An SDL key event (SDL_Scancode value); host auto-repeats are ignored (the keyboard makes its
    // own). False for keys a PC keyboard lacks.
    bool hostKey(uint32_t sdlScancode, bool down, bool repeat);
    // The pointer at canvas pixel (x, y) of the 320x200 picture (clamped by the driver's range).
    void hostPointer(float canvasX, float canvasY);
    void hostButton(uint16_t button, bool down);  // dos::Int33Mouse::kLeft / kRight / kMiddle
    void releaseAllKeys();

    // ---- diagnostics
    uint64_t polls() const { return polls_; }
    uint64_t waits() const { return waits_; }
    uint64_t droppedNs() const { return droppedNs_; }

private:
    void installHostHandlers();
    void poll();
    void endWait(uint64_t startNs);
    void sleepHost();
    uint64_t hostNowNs() const;
    std::optional<uint64_t> nextScriptTimeNs() const;
    uint64_t scriptTimeNs(const dos::ScriptEvent& e) const;
    void applyScriptEvent(const dos::ScriptEvent& e);
    void runInterrupt();
    void yielded(YieldPoint p, int slot = -1) {
        if (yieldHook_) yieldHook_(p, slot);
    }

    RuntimeConfig config_;
    platform::Host* host_;
    GgClock clock_;
    GgTimers timers_;
    GgKeyboard keyboard_;
    GgMouse mouse_;
    ClickZones zones_;
    InputCodes input_;

    dos::ScriptedInput script_;
    size_t scriptNext_ = 0;
    std::optional<uint64_t> limit_;

    std::vector<uint32_t> frame_;
    uint64_t framesShown_ = 0;
    FrameSink frameSink_;
    YieldHook yieldHook_;

    uint64_t pollsSinceWait_ = 0;
    uint64_t polls_ = 0, waits_ = 0;
    bool lastPollIdle_ = false;
    bool resync_ = true;
    uint64_t lastHostNs_ = 0;
    uint64_t droppedNs_ = 0;
};

}  // namespace tmport::gg
