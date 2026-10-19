#pragma once
// Host: the edition-neutral platform layer under both editions. It owns the SDL window and
// renderer, the presenter (canvas, texture upload, letterboxing for a PresenterLayout), the event
// pump, the focus pause, fullscreen, the screenshot key and the clock. What an event *means* is the
// edition's business: the host keeps the window state for itself and hands everything else to the
// edition's HostHandlers.
//
//   Windows CD edition: Engine (platform/engine.h) builds its Win16 message / input-code model on a
//     Host with PresenterLayout::windowsCd(); the game never sees the Host.
//   1990 DOS edition:   dos::runDosEdition (dos/app/dos_entry.h) gets a Host with
//     PresenterLayout::dos1990() and drives it directly: setHandlers, pump() per frame,
//     presenter().presentFrame(rgba) for each finished 320x200 frame.
//
// Event order (dispatch): the fullscreen hot keys (Alt+Enter, F11) first, then handlers.filter,
// then the host's own window-state events and F12, then handlers.input. Headless (no window): only
// the events queued with postEvent arrive, nothing freezes and nothing is rendered; the canvas
// still exists (frame dumps, scripted shots).

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "gfx/presenter.h"
#include "timing/clock.h"

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

namespace tmport::platform {

struct HostConfig {
    gfx::PresenterLayout layout = gfx::PresenterLayout::windowsCd();
    std::string title = "Treasure Mountain! (TM Port)";
    int windowScale = 0;            // initial window = display size x this (<= 0: the layout's default)
    bool integerScale = false;      // integer scaling inside the letterbox (gfx::integerScaleFor)
    bool fullscreen = false;        // start in borderless desktop fullscreen; Alt+Enter / F11 toggle
    bool pauseOnFocusLoss = true;   // freeze while the window is inactive (minimised always freezes)
    Clock* clock = nullptr;         // nullptr: SDL clock (tests pass a ManualClock)
    std::filesystem::path screenshotDir;    // empty: <pref path>/screenshots
    std::string screenshotPrefix = "tmport";  // F12 files: <prefix>_YYYYMMDD_hhmmss_mmm.png
};

// The edition's side of the event pump. Every member may be empty.
struct HostHandlers {
    // Sees every event after the fullscreen hot keys and before the host; true consumes it (the
    // Windows edition's in-engine menu bar intercepts its clicks and keys here).
    std::function<bool(const SDL_Event&)> filter;
    // A window-state event after the host has updated its own state: focus gained/lost, minimised,
    // restored, maximised, exposed, resized, pixel size or display scale changed, fullscreen
    // entered/left.
    std::function<void(const SDL_Event&)> window;
    // Everything else: keys, text input, mouse, quit and close requests (quitRequested() is already
    // set), other window events. Also the release of the Alt key of an Alt+Enter, which skips the
    // filter (held-key models must see every release).
    std::function<void(const SDL_Event&)> input;
    // App_Deactivate / App_Activate: false when the host freezes (focus loss with pauseOnFocusLoss,
    // minimised) or setAppActive(false) is called; true when it resumes.
    std::function<void(bool active)> activation;
};

class Host {
public:
    explicit Host(HostConfig config = {});
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // --- window ---------------------------------------------------------------------------------
    // Interactive mode only: initialises SDL video, creates the window (gfx::initialWindowSize),
    // the renderer (vsync) and the presenter's texture, and starts text input. Headless commands
    // never call this. Returns false (and logs) on failure.
    bool openWindow();
    void closeWindow();
    bool hasWindow() const { return window_ != nullptr; }
    SDL_Window* window() const { return window_; }
    SDL_Renderer* renderer() const { return renderer_; }
    // Called right after the window is created (the Windows edition sets the EXE's icon).
    void setWindowIconHook(std::function<void(SDL_Window*)> hook) { iconHook_ = std::move(hook); }
    void setTitle(std::string_view title);

    // Borderless desktop fullscreen; the picture keeps the presenter's letterbox / integer scaling.
    // Headless: only the flag changes.
    bool fullscreen() const { return fullscreen_; }
    void setFullscreen(bool on);
    void toggleFullscreen() { setFullscreen(!fullscreen_); }

    // --- presentation -----------------------------------------------------------------------------
    gfx::Presenter& presenter() { return presenter_; }
    const gfx::Presenter& presenter() const { return presenter_; }
    const gfx::PresenterLayout& layout() const { return presenter_.layout(); }
    // Renders the canvas if it changed (at most once per display refresh) or the window needs a
    // repaint. No-op headless.
    void renderIfDue();
    // Window coordinates of an SDL event / the pointer -> canvas coordinates of the layout.
    bool windowToCanvas(float wx, float wy, float& cx, float& cy) const { return presenter_.windowToCanvas(wx, wy, cx, cy); }
    // F12: the game area as a PNG into the screenshot directory (the path is logged).
    bool screenshot();
    // The canvas as a PNG (--frame-dump, scripted shots).
    bool writeFrameDump(const std::filesystem::path& path) const { return presenter_.writeCanvasPng(path); }

    // --- events -----------------------------------------------------------------------------------
    void setHandlers(HostHandlers handlers) { handlers_ = std::move(handlers); }
    HostHandlers& handlers() { return handlers_; }
    // Queues an SDL event for the next dispatch (scripted input, tests; works headless). The text
    // of SDL_EVENT_TEXT_INPUT is copied.
    void postEvent(const SDL_Event& e);
    size_t pendingPostedEvents() const { return posted_.size(); }
    // One event through the order of the file comment.
    void dispatch(const SDL_Event& e);
    // Dispatches one queued event; false when none is queued.
    bool dispatchPosted();
    // Dispatches every queued event, then (with a window) every pending SDL event.
    void dispatchPending();
    // Waits up to timeoutMs for an SDL event; if one came, dispatches it and then the pending ones
    // while more() (empty: always) holds. Returns whether an event came.
    bool waitAndDispatch(int timeoutMs, const std::function<bool()>& more = {});
    // The original's frozen event fetch: while paused() (and no new close request arrived) render
    // exposed frames, wait for events and call eachIteration; activation(false) before, (true)
    // after. Returns at once when not paused.
    void freezeWhilePaused(const std::function<void()>& eachIteration = {});
    // A frame's worth of platform work for an edition with a flat frame loop (DOS): dispatch
    // pending events, freeze while paused, render if due.
    void pump();

    // --- state ------------------------------------------------------------------------------------
    // Sticky: a window close / quit request arrived.
    bool quitRequested() const { return quit_; }
    void clearQuitRequest() { quit_ = false; }
    uint32_t closeRequests() const { return closeCount_; }
    bool paused() const;
    bool focused() const { return focused_; }
    bool minimized() const { return minimized_; }
    bool appActive() const { return appActive_; }
    // Calls handlers.activation when the state changes (the Windows menu loop uses it too).
    void setAppActive(bool active);

    // --- clock ------------------------------------------------------------------------------------
    Clock& clock() { return *clock_; }
    // Monotonic nanoseconds (SDL_GetTicksNS; a custom clock: its milliseconds x 1e6).
    uint64_t nowNs() const;

    const HostConfig& config() const { return config_; }

private:
    bool handleFullscreenHotkey(const SDL_Event& e);

    HostConfig config_;
    Clock* clock_;
    gfx::Presenter presenter_;
    HostHandlers handlers_;
    std::function<void(SDL_Window*)> iconHook_;

    struct PostedEvent {
        alignas(8) unsigned char raw[128];  // an SDL_Event (128 bytes, checked in host.cpp)
        std::string text;                   // owned copy of SDL_EVENT_TEXT_INPUT text
    };
    std::deque<PostedEvent> posted_;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool sdlVideoInit_ = false;
    bool quit_ = false;
    uint32_t closeCount_ = 0;
    bool focused_ = true;
    bool minimized_ = false;
    bool exposed_ = false;
    bool appActive_ = true;
    bool fullscreen_ = false;
    uint32_t eatKeyUp_ = 0;      // SDL_Keycode of a hot key whose release is swallowed too
    bool swallowAltUp_ = false;  // after Alt+Enter: the Alt release must not open the menu bar
    uint64_t refreshNs_ = 16'666'667;
    uint64_t lastPresentNs_ = 0;
};

}  // namespace tmport::platform
