#include "platform/engine.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "data/dgroup_offsets.h"
#include "platform/exe_icon_ne.h"
#include "util/log.h"

namespace tmport {

namespace {

// The Windows CD layout and the EngineConfig fields the host takes over.
platform::HostConfig hostConfigFor(const EngineConfig& c) {
    platform::HostConfig h;
    h.layout = gfx::PresenterLayout::windowsCd();
    h.title = c.title;
    h.windowScale = c.windowScale < 1 ? 1 : c.windowScale;
    h.integerScale = c.integerScale;
    h.fullscreen = c.fullscreen;
    h.pauseOnFocusLoss = c.pauseOnFocusLoss;
    h.clock = c.clock;
    h.screenshotDir = c.screenshotDir;
    return h;
}

}  // namespace

Engine::Engine(GameFiles& files, gfx::ColorMode mode, EngineConfig config)
    : files_(files),
      mode_(mode),
      config_(std::move(config)),
      host_(hostConfigFor(config_)),
      clock_(&host_.clock()),
      palette_(files.makePalette(mode)),
      frame_(mode),
      cursorCtl_(input_, cursors_),
      timers_(clock_) {
    frame_.setPalette(palette_);
    presenter().setPalette(palette_);
    // Start-up clears the buffer to black (Screen_Clear 1070:0000 after palette creation) and
    // the window shows that black frame until the game presents something.
    frame_.clear(palette_.pen(0));
    presenter().blitAll(frame_);

    input_.setMode(config_.inputMode);
    input_.setTapLatch(config_.tapLatch);
    // The quit event re-presents the whole screen (1028:004f -> Gfx_BlitToWindow(0,0,512,384)).
    input_.setQuitPresentHook([this] { presentAll(); });
    input_.setMouseSource([this](int& x, int& y) { return liveMousePos(x, y); });

    // The host's event pump feeds the Win16 message model: the menu bar's filter first, window
    // state for the cursor scale, everything else into the input queue (posted, dispatched by the
    // next fetch).
    platform::HostHandlers h;
    h.filter = [this](const SDL_Event& e) { return eventFilter_ && eventFilter_(e); };
    h.window = [this](const SDL_Event& e) { handleWindowEvent(e); };
    h.input = [this](const SDL_Event& e) { handleInputEvent(e); };
    h.activation = [this](bool active) {
        if (activationHook_) activationHook_(active);
    };
    host_.setHandlers(std::move(h));
    host_.setWindowIconHook([this](SDL_Window* w) { applyWindowIcon(w); });

    // Cursor_LoadSet with the game's id list (DS:1292 = {128, 129, 130, 0xFFFF}); decoding is
    // headless, SDL cursors are created when the window opens.
    if (config_.gameCursors && files_.exe) {
        std::vector<uint16_t> ids;
        try {
            for (uint32_t off = dg::kCursorIds; ids.size() < CursorSet::kMaxCursors; off += 2) {
                const uint16_t id = files_.exe->u16(off);
                if (id == 0xFFFF) break;
                ids.push_back(id);
            }
            cursors_.load(files_.exe->exe(), ids);
        } catch (const std::exception& e) {
            log::warn("cursors: {}", e.what());
        }
    }
}

Engine::~Engine() { closeWindow(); }

bool Engine::openWindow() {
    if (host_.hasWindow()) return true;
    if (!host_.openWindow()) return false;
    input_.setRenderer(host_.renderer());
    cursors_.attach(1);
    updateCursorScale();
    return true;
}

void Engine::closeWindow() {
    input_.setRenderer(nullptr);
    cursors_.detach();
    host_.closeWindow();
}

void Engine::setPalette(const gfx::Palette& p) {
    palette_ = p;
    frame_.setPalette(palette_);
    presenter().setPalette(palette_);
}

void Engine::setTitle(std::string_view title) { host_.setTitle(title); }

void Engine::applyWindowIcon(SDL_Window* window) {
    // 1018:00a8 RegisterMainWindowClass: the class icon is LoadIcon(hInst, 15000), from the user's EXE.
    if (!window || !files_.exe) return;
    try {
        const IconImage icon = loadGroupIcon(files_.exe->exe(), kWindowIconGroupId);
        if (!setWindowIcon(window, icon)) log::warn("window icon: {}", SDL_GetError());
    } catch (const std::exception& e) {
        log::warn("window icon: {}", e.what());
    }
}

void Engine::updateCursorScale() {
    SDL_Renderer* renderer = host_.renderer();
    SDL_Window* window = host_.window();
    if (!renderer || !window) return;
    int scale = config_.cursorScale;
    if (scale <= 0) {
        SDL_FRect r{};
        float density = SDL_GetWindowPixelDensity(window);
        if (density <= 0.0f) density = 1.0f;
        if (SDL_GetRenderLogicalPresentationRect(renderer, &r) && r.w > 0.0f)
            scale = static_cast<int>(std::lround(r.w / density / gfx::Presenter::kCanvasWidth));
        scale = std::clamp(scale, 1, 8);
    }
    cursors_.setScale(scale);
}

bool Engine::liveMousePos(int& x, int& y) const {
    SDL_Window* window = host_.window();
    SDL_Renderer* renderer = host_.renderer();
    if (!window || !renderer) return false;
    // GetCursorPos + ScreenToClient: the pointer position even outside the window.
    float wx = 0.0f, wy = 0.0f;
    float gx = 0.0f, gy = 0.0f;
    int px = 0, py = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    if (SDL_GetWindowPosition(window, &px, &py)) {
        wx = gx - static_cast<float>(px);
        wy = gy - static_cast<float>(py);
    } else {
        SDL_GetMouseState(&wx, &wy);
    }
    float cx = 0.0f, cy = 0.0f;
    if (!SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &cx, &cy)) return false;
    x = EventTranslator::canvasToGameX(cx);
    y = EventTranslator::canvasToGameY(cy);
    return true;
}

void Engine::handleWindowEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: updateCursorScale(); break;
        default: break;
    }
}

void Engine::handleInputEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F9 && config_.debugKeys &&
        (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT)) == 0) {
        if (!e.key.repeat) {
            setShowTreasure(!showTreasure());
            log::info("debug: treasure boxes {}", showTreasure() ? "on" : "off");
        }
        return;
    }
    // Close requests become input code 0x1B (the game asks); everything else is posted.
    input_.handleEvent(e);
}

void Engine::pump() { pumpPhase(PumpPhase::Busy); }

void Engine::pumpPhase(PumpPhase phase) {
    host_.dispatchPending();
    // Evt_GetNextEvent does not return while the application is inactive: freeze here.
    if (host_.paused()) host_.freezeWhilePaused([this] { runPumpHooks(PumpPhase::Frozen); });
    runPumpHooks(phase);
    host_.renderIfDue();
}

int Engine::addPumpHook(PumpHook hook) {
    const int id = nextPumpHookId_++;
    pumpHooks_.push_back({id, std::make_shared<PumpHook>(std::move(hook))});
    return id;
}

void Engine::removePumpHook(int id) {
    std::erase_if(pumpHooks_, [id](const PumpHookEntry& h) { return h.id == id; });
}

void Engine::runPumpHooks(PumpPhase phase) {
    if (pumpHooks_.empty()) return;
    // A copy: hooks may add or remove hooks, or pump again (which runs the hooks again).
    const std::vector<PumpHookEntry> hooks = pumpHooks_;
    for (const PumpHookEntry& h : hooks) {
        const bool stillThere =
            std::any_of(pumpHooks_.begin(), pumpHooks_.end(), [&](const PumpHookEntry& x) { return x.id == h.id; });
        if (stillThere && h.fn && *h.fn) (*h.fn)(phase);
    }
}

void Engine::holdWhile(const std::function<bool()>& active) {
    if (holding_ || !active()) return;
    holding_ = true;
    // WM_ENTERMENULOOP -> App_Deactivate (1010:030f); WM_EXITMENULOOP -> App_Activate unless iconic
    // (1010:0317). The restore also runs when an exception leaves the hold.
    struct Restore {
        Engine& e;
        ~Restore() {
            e.holding_ = false;
            if (!e.paused()) e.host_.setAppActive(true);
        }
    } restore{*this};
    host_.setAppActive(false);
    while (active()) {
        bool any = false;
        while (active() && host_.dispatchPosted()) any = true;
        if (!active()) break;
        if (!host_.hasWindow()) {
            if (!any) break;  // headless: nothing else can arrive
            runPumpHooks(PumpPhase::Frozen);
            continue;
        }
        host_.renderIfDue();
        host_.waitAndDispatch(20, active);
        runPumpHooks(PumpPhase::Frozen);
    }
    host_.renderIfDue();
}

uint16_t Engine::getInputCode() {
    pumpPhase(PumpPhase::Fetch);
    return input_.getCode();
}

uint16_t Engine::peekInputCode() {
    pumpPhase(PumpPhase::Fetch);
    return input_.peekCode();
}

void Engine::flushInput() {
    pumpPhase(PumpPhase::Fetch);
    input_.flushInput();
}

uint16_t Engine::waitInputCode() {
    for (;;) {
        if (const uint16_t c = getInputCode()) return c;
        clock_->sleepMs(1);
    }
}

void Engine::delayMs(uint32_t ms) {
    const uint64_t end = clock_->nowMs() + ms;
    for (;;) {
        pump();
        const uint64_t now = clock_->nowMs();
        if (now >= end) break;
        const uint64_t left = end - now;
        clock_->sleepMs(static_cast<uint32_t>(left < 2 ? left : 2));
    }
}

void Engine::timerWait(int slot) {
    for (;;) {
        pump();
        const uint64_t left = timers_.remaining(slot);
        if (left == 0) break;
        clock_->sleepMs(static_cast<uint32_t>(left < 2 ? left : 2));
    }
}

bool Engine::timerExpired(int slot) {
    pump();
    if (timers_.expired(slot)) return true;
    const uint64_t left = timers_.remaining(slot);
    clock_->sleepMs(static_cast<uint32_t>(left < 1 ? left : 1));
    return timers_.expired(slot);
}

}  // namespace tmport
