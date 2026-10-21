#pragma once
// Engine: the Windows CD edition's runtime. Owns the frame buffer, palette, input model, cursors,
// timers and RNG for one run, on top of the engine's mischief::platform::Host (mischief/platform/host.h: the
// SDL window, the presenter, the event pump, focus pause, fullscreen, screenshots, the clock).
// Designed for the original's nested blocking loops (docs/DESIGN.md "Runtime model"): game code
// runs as ordinary nested functions on the main thread and every wait primitive calls
// Engine::pump(), which processes SDL events and presents the window picture at most once per
// display refresh.
//
// Input timing follows the original (re/docs/code_1008-1030.md §6, PORTING_NOTES.md §3/§4.7):
// pump() only *posts* input to the application message queue; messages are dispatched (held
// arrows, mouse position, events) inside the input fetch (getInputCode / peekInputCode /
// flushInput). Busy waits (delayMs, timerWait, timerExpired) pump and present but deliver no
// input: what arrives during them is seen by the next fetch.

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/frame_buffer.h"
#include "input/cursor_controller.h"
#include "input/cursors.h"
#include "input/hotspot_table.h"
#include "input/input_queue.h"
#include "mischief/gfx/palette.h"
#include "mischief/gfx/presenter.h"
#include "mischief/platform/host.h"
#include "mischief/rtl/borland_rand.h"
#include "mischief/timing/clock.h"
#include "mischief/timing/deadline_timers.h"
#include "res/game_files.h"

union SDL_Event;

namespace tmport {

// Why a pump runs (Engine::addPumpHook). The original dispatched window messages only inside the
// event fetch, so work that stands for a dispatched message (menu commands) belongs to Fetch.
enum class PumpPhase : uint8_t {
    Busy,    // pump() of a busy wait (delayMs, timerWait, timerExpired) or of tool code
    Fetch,   // the pump of an input fetch (getInputCode, peekInputCode, flushInput, waitInputCode)
    Frozen,  // one iteration of a freeze (inactive / minimised window, or holdWhile)
};

struct EngineConfig {
    bool integerScale = false;      // integer scaling inside the letterbox
    bool fullscreen = false;        // start in (borderless desktop) fullscreen; Alt+Enter / F11 toggle at run time
    bool pauseOnFocusLoss = true;   // the original freezes while inactive (PORTING_NOTES §3.4); minimised always pauses
    int windowScale = 2;            // initial window size = 640x480 * scale (reduced to fit)
    InputMode inputMode = InputMode::Lossless;  // FirstEventPerDrain = the original's event loss
    bool tapLatch = false;          // keep arrow taps shorter than one drain visible for one poll
    int cursorScale = 0;            // 0: follow the window scale; n: fixed magnification of the EXE cursors
    bool gameCursors = true;        // use the EXE's cursors (false: system arrow only)
    mischief::Clock* clock = nullptr;         // nullptr: SDL clock (tests pass a ManualClock)
    std::filesystem::path screenshotDir;  // empty: <pref path>/screenshots
    std::string title = "Treasure Mountain! (TM Port)";
    bool debugKeys = false;         // F9 toggles showTreasure ([debug] keys, --debug)
    bool showTreasure = false;      // boxes around hiding places with a treasure or the key ([debug] show_treasure)
};

class Engine {
public:
    Engine(GameFiles& files, mischief::gfx::ColorMode mode, EngineConfig config = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Interactive mode only: initialises SDL video and creates the window and renderer.
    // Headless commands never call this. Returns false (and logs) on failure.
    bool openWindow();
    void closeWindow();
    bool hasWindow() const { return host_.hasWindow(); }
    // The platform layer under the engine (window, presenter, event pump, clock).
    mischief::platform::Host& host() { return host_; }

    // --- pump and input fetch ---------------------------------------------------------------------
    // Processes pending SDL events (and events queued with postEvent): window events at once,
    // input events are translated and posted to the input queue (not dispatched). While the
    // window is minimised, or unfocused with pauseOnFocusLoss, it blocks (the game is frozen,
    // like the original's event fetch) and calls the activation hook. Renders the window picture
    // if it changed, at most once per display refresh. Headless: only queued events.
    void pump();
    // 1028:0131 Input_GetCode: pump() + next input code (0 if none).
    uint16_t getInputCode();
    // 1028:01f6 Input_PeekCode.
    uint16_t peekInputCode();
    // 1028:0126 Input_Flush: pump() + discard pending input (side effects such as held keys stay).
    void flushInput();
    // 10a0:0000 Input_WaitCode: repeats getInputCode() until it is non-zero (yields between polls).
    uint16_t waitInputCode();

    // --- timing -----------------------------------------------------------------------------------
    // 10a8:0000 Delay_ms: waits ms milliseconds while pumping and presenting, without delivering
    // input (it is seen by the next fetch, as in the original's busy wait).
    void delayMs(uint32_t ms);
    // 10a8:002a Timer_Set.
    void timerSet(int slot, int32_t ms) { timers_.set(slot, ms); }
    // 10a8:0049 Timer_Wait: like delayMs until the slot's deadline.
    void timerWait(int slot);
    // 10a8:0066 Timer_Expired for spin loops: pumps, and yields up to 1 ms while not expired.
    bool timerExpired(int slot);
    mischief::Clock& clock() { return host_.clock(); }

    // --- presentation -----------------------------------------------------------------------------
    // Gfx_BlitToWindow(0,0,512,384) and Gfx_FlushDirtyRects.
    void presentAll() { presenter().blitAll(frame_); }
    void flushDirtyRects() { presenter().flushDirtyRects(frame_); }
    // F12: game area (512x384) as PNG into the screenshot directory.
    bool screenshot() { return host_.screenshot(); }

    void setPalette(const mischief::gfx::Palette& p);
    void setTitle(std::string_view title);

    // --- fullscreen -----------------------------------------------------------------------------------
    // Borderless desktop fullscreen; the picture keeps the presenter's letterbox / integer scaling.
    // Alt+Enter and F11 toggle it. Those keys are taken before the event filter, so neither the menu
    // bar nor the game sees them (nor the Alt release that follows Alt+Enter). The window icon is the
    // EXE's RT_GROUP_ICON 15000 (platform/exe_icon_ne.h). Headless: only the flag changes.
    bool fullscreen() const { return host_.fullscreen(); }
    void setFullscreen(bool on) { host_.setFullscreen(on); }
    void toggleFullscreen() { host_.toggleFullscreen(); }

    // --- window / app state -----------------------------------------------------------------------
    // Sticky: a window close / quit request arrived (tools; the game reacts to input code 0x1B).
    bool quitRequested() const { return host_.quitRequested(); }
    void clearQuitRequest() { host_.clearQuitRequest(); }
    bool paused() const { return host_.paused(); }
    bool appActive() const { return host_.appActive(); }
    // App_Deactivate / App_Activate (1010:05c8 / 05db): called with false when the engine freezes
    // (focus loss, minimised, holdWhile = the menu loop) and true when it resumes. The app installs
    // audio.appDeactivate() / appActivate() here (app/engine_glue.h).
    void setActivationHook(std::function<void(bool active)> hook) { activationHook_ = std::move(hook); }
    // Sees every SDL event before the engine; returning true consumes it. The in-engine menu bar
    // (ui/menu_bar.h) intercepts its mouse clicks and Alt/F10 keys here.
    void setEventFilter(std::function<bool(const SDL_Event&)> filter) { eventFilter_ = std::move(filter); }
    // Queues an SDL event for the next pump() (scripted input, tests; works headless). Mouse
    // events with windowID 0 are taken as 640x480 canvas coordinates (not converted).
    void postEvent(const SDL_Event& e) { host_.postEvent(e); }
    // Evt_PostKey: queues a key event with this input code.
    void postKey(uint16_t code) { input_.push(code); }
    size_t pendingPostedEvents() const { return host_.pendingPostedEvents(); }

    // --- per-pump services --------------------------------------------------------------------------
    // Service hooks stand for the window-procedure duties of the original that are not input:
    // audio.service() (MM_WOM_DONE / MM_MCINOTIFY), menu tracking and WM_COMMAND delivery, scripted
    // input. Each hook runs at the end of every pump (after the events, before rendering) with the
    // pump's phase, and in every iteration of a freeze with PumpPhase::Frozen. Hooks run in the order
    // added; a hook may pump again (nested pumps call the hooks again, so hooks must guard their own
    // re-entrancy) and may throw (RestartToSignIn / QuitGame leave through pump()). Hooks added or
    // removed while hooks run take effect for the next pump. Returns an id for removePumpHook.
    using PumpHook = std::function<void(PumpPhase)>;
    int addPumpHook(PumpHook hook);
    void removePumpHook(int id);
    // Modal hold (the Win16 menu loop, WM_ENTERMENULOOP .. WM_EXITMENULOOP): while active() is true
    // the game is frozen exactly like an inactive window: the activation hook is called with false
    // (App_Deactivate), events go through the event filter and the engine, the frame is rendered,
    // Frozen hooks run; on return the activation hook is called with true (App_Activate) unless the
    // window is inactive or minimised by then. Headless: processes the queued events and returns when
    // none are left (active() may still be true). Re-entrant calls return at once.
    void holdWhile(const std::function<bool()>& active);
    bool holding() const { return holding_; }

    GameFiles& files() { return files_; }
    mischief::gfx::ColorMode colorMode() const { return mode_; }
    gfx::FrameBuffer& frame() { return frame_; }
    const mischief::gfx::Palette& palette() const { return palette_; }
    mischief::gfx::Presenter& presenter() { return host_.presenter(); }
    InputQueue& input() { return input_; }
    HotspotTable& hotspots() { return input_.hotspots(); }
    CursorSet& cursors() { return cursors_; }
    CursorController& cursor() { return cursorCtl_; }
    mischief::DeadlineTimers& timers() { return timers_; }
    mischief::BorlandRand& rng() { return rng_; }
    const EngineConfig& config() const { return config_; }
    // Debug: the room loop boxes the hiding places that hold a treasure or the key (F9 with debugKeys).
    bool showTreasure() const { return config_.showTreasure; }
    void setShowTreasure(bool on) { config_.showTreasure = on; }

private:
    void applyWindowIcon(SDL_Window* window);
    void handleWindowEvent(const SDL_Event& e);
    void handleInputEvent(const SDL_Event& e);
    void pumpPhase(PumpPhase phase);
    void runPumpHooks(PumpPhase phase);
    void updateCursorScale();
    bool liveMousePos(int& x, int& y) const;

    GameFiles& files_;
    mischief::gfx::ColorMode mode_;
    EngineConfig config_;
    mischief::platform::Host host_;
    mischief::Clock* clock_;
    mischief::gfx::Palette palette_;
    gfx::FrameBuffer frame_;
    InputQueue input_;
    CursorSet cursors_;
    CursorController cursorCtl_;
    mischief::DeadlineTimers timers_;
    mischief::BorlandRand rng_;

    std::function<void(bool)> activationHook_;
    std::function<bool(const SDL_Event&)> eventFilter_;
    struct PumpHookEntry {
        int id = 0;
        std::shared_ptr<PumpHook> fn;
    };
    std::vector<PumpHookEntry> pumpHooks_;
    int nextPumpHookId_ = 1;
    bool holding_ = false;
};

}  // namespace tmport
