#include "platform/engine.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <format>
#include <vector>

#include "data/dgroup_offsets.h"
#include "platform/exe_icon.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport {

static_assert(sizeof(SDL_Event) == 128, "Engine::PostedEvent assumes a 128-byte SDL_Event");

Engine::Engine(GameFiles& files, gfx::ColorMode mode, EngineConfig config)
    : files_(files),
      mode_(mode),
      config_(std::move(config)),
      clock_(config_.clock ? config_.clock : &Clock::system()),
      palette_(files.makePalette(mode)),
      frame_(mode),
      cursorCtl_(input_, cursors_),
      timers_(clock_),
      fullscreen_(config_.fullscreen) {
    frame_.setPalette(palette_);
    presenter_.setPalette(palette_);
    // Start-up clears the buffer to black (Screen_Clear 1070:0000 after palette creation) and
    // the window shows that black frame until the game presents something.
    frame_.clear(palette_.pen(0));
    presenter_.blitAll(frame_);

    input_.setMode(config_.inputMode);
    input_.setTapLatch(config_.tapLatch);
    // The quit event re-presents the whole screen (1028:004f -> Gfx_BlitToWindow(0,0,512,384)).
    input_.setQuitPresentHook([this] { presentAll(); });
    input_.setMouseSource([this](int& x, int& y) { return liveMousePos(x, y); });

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
    if (window_) return true;
    // Window close must arrive only as SDL_EVENT_WINDOW_CLOSE_REQUESTED (-> input 0x1B); the game
    // asks the player and quits on its own.
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log::error("SDL_Init(VIDEO) failed: {}", SDL_GetError());
        return false;
    }
    sdlVideoInit_ = true;

    int scale = config_.windowScale < 1 ? 1 : config_.windowScale;
    if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())) {
        while (scale > 1 && (gfx::Presenter::kCanvasWidth * scale > dm->w * 9 / 10 ||
                             gfx::Presenter::kCanvasHeight * scale > dm->h * 9 / 10))
            --scale;
    }
    // The windowed size is kept for leaving fullscreen; SDL_WINDOW_FULLSCREEN without a display mode
    // is borderless desktop fullscreen.
    window_ = SDL_CreateWindow(config_.title.c_str(), gfx::Presenter::kCanvasWidth * scale,
                               gfx::Presenter::kCanvasHeight * scale,
                               SDL_WINDOW_RESIZABLE | (fullscreen_ ? SDL_WINDOW_FULLSCREEN : SDL_WindowFlags{0}));
    if (!window_) {
        log::error("SDL_CreateWindow failed: {}", SDL_GetError());
        closeWindow();
        return false;
    }
    applyWindowIcon();
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        log::error("SDL_CreateRenderer failed: {}", SDL_GetError());
        closeWindow();
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    if (const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_)))
        if (dm->refresh_rate > 1.0f) refreshNs_ = static_cast<uint64_t>(1e9 / dm->refresh_rate);
    if (!presenter_.attach(renderer_, config_.integerScale)) {
        closeWindow();
        return false;
    }
    // WM_CHAR equivalent: printable characters arrive as text input.
    SDL_StartTextInput(window_);
    input_.setRenderer(renderer_);
    cursors_.attach(1);
    updateCursorScale();
    exposed_ = true;
    focused_ = true;
    minimized_ = false;
    return true;
}

void Engine::closeWindow() {
    input_.setRenderer(nullptr);
    cursors_.detach();
    if (window_) SDL_StopTextInput(window_);
    presenter_.detach();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    if (sdlVideoInit_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    sdlVideoInit_ = false;
}

void Engine::setPalette(const gfx::Palette& p) {
    palette_ = p;
    frame_.setPalette(palette_);
    presenter_.setPalette(palette_);
}

void Engine::setTitle(std::string_view title) {
    if (window_) SDL_SetWindowTitle(window_, std::string(title).c_str());
}

void Engine::applyWindowIcon() {
    // 1018:00a8 RegisterMainWindowClass: the class icon is LoadIcon(hInst, 15000), from the user's EXE.
    if (!window_ || !files_.exe) return;
    try {
        const IconImage icon = loadGroupIcon(files_.exe->exe(), kWindowIconGroupId);
        if (!setWindowIcon(window_, icon)) log::warn("window icon: {}", SDL_GetError());
    } catch (const std::exception& e) {
        log::warn("window icon: {}", e.what());
    }
}

void Engine::setFullscreen(bool on) {
    fullscreen_ = on;
    if (!window_) return;
    if (!SDL_SetWindowFullscreen(window_, on)) log::warn("fullscreen {}: {}", on ? "on" : "off", SDL_GetError());
    exposed_ = true;
}

bool Engine::handleHotkey(const SDL_Event& e) {
    if (e.type == SDL_EVENT_KEY_DOWN) {
        // A new Alt press: whatever happened to the previous release, the next one is a real one.
        if ((e.key.key == SDLK_LALT || e.key.key == SDLK_RALT) && !e.key.repeat) swallowAltUp_ = false;
        const SDL_Keymod mod = e.key.mod;
        const bool alt = (mod & SDL_KMOD_ALT) != 0;
        const bool other = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;  // Ctrl+Alt = AltGr: not a hot key
        const bool altEnter = (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && alt && !other;
        const bool f11 = e.key.key == SDLK_F11 && !alt && !other;
        if (!altEnter && !f11) return false;
        if (!e.key.repeat) toggleFullscreen();
        eatKeyUp_ = e.key.key;
        // The menu bar armed itself on the Alt press; it must not see the Alt release as a lone Alt.
        if (altEnter) swallowAltUp_ = true;
        return true;
    }
    if (e.type == SDL_EVENT_KEY_UP) {
        if (eatKeyUp_ != 0 && e.key.key == eatKeyUp_) {
            eatKeyUp_ = 0;
            return true;
        }
        if (swallowAltUp_ && (e.key.key == SDLK_LALT || e.key.key == SDLK_RALT)) {
            swallowAltUp_ = false;
            input_.handleEvent(e);  // key-ups always reach the input model (held flags never stick)
            return true;
        }
    }
    return false;
}

bool Engine::paused() const {
    if (!window_) return false;
    return minimized_ || (config_.pauseOnFocusLoss && !focused_);
}

void Engine::setAppActive(bool active) {
    if (appActive_ == active) return;
    appActive_ = active;
    if (activationHook_) activationHook_(active);
}

void Engine::updateCursorScale() {
    if (!renderer_ || !window_) return;
    int scale = config_.cursorScale;
    if (scale <= 0) {
        SDL_FRect r{};
        float density = SDL_GetWindowPixelDensity(window_);
        if (density <= 0.0f) density = 1.0f;
        if (SDL_GetRenderLogicalPresentationRect(renderer_, &r) && r.w > 0.0f)
            scale = static_cast<int>(std::lround(r.w / density / gfx::Presenter::kCanvasWidth));
        scale = std::clamp(scale, 1, 8);
    }
    cursors_.setScale(scale);
}

bool Engine::liveMousePos(int& x, int& y) const {
    if (!window_ || !renderer_) return false;
    // GetCursorPos + ScreenToClient: the pointer position even outside the window.
    float wx = 0.0f, wy = 0.0f;
    float gx = 0.0f, gy = 0.0f;
    int px = 0, py = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    if (SDL_GetWindowPosition(window_, &px, &py)) {
        wx = gx - static_cast<float>(px);
        wy = gy - static_cast<float>(py);
    } else {
        SDL_GetMouseState(&wx, &wy);
    }
    float cx = 0.0f, cy = 0.0f;
    if (!SDL_RenderCoordinatesFromWindow(renderer_, wx, wy, &cx, &cy)) return false;
    x = EventTranslator::canvasToGameX(cx);
    y = EventTranslator::canvasToGameY(cy);
    return true;
}

void Engine::postEvent(const SDL_Event& e) {
    PostedEvent p{};
    std::memcpy(p.raw, &e, sizeof(SDL_Event));
    if (e.type == SDL_EVENT_TEXT_INPUT && e.text.text) p.text = e.text.text;
    posted_.push_back(std::move(p));
}

void Engine::handleEvent(const SDL_Event& e) {
    if (handleHotkey(e)) return;  // Alt+Enter / F11: before the menu bar and the game
    if (eventFilter_ && eventFilter_(e)) return;
    switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            // The original turns close requests into input code 0x1B and lets the game ask.
            quit_ = true;
            ++closeCount_;
            input_.handleEvent(e);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: focused_ = true; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: focused_ = false; break;
        case SDL_EVENT_WINDOW_MINIMIZED: minimized_ = true; break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
            minimized_ = false;
            exposed_ = true;
            updateCursorScale();
            break;
        case SDL_EVENT_WINDOW_EXPOSED: exposed_ = true; break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            // Also when the system changed it (macOS green button, Win+Up, ...).
            fullscreen_ = e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
            exposed_ = true;
            updateCursorScale();
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            exposed_ = true;
            updateCursorScale();
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_F12 && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT)) == 0) {
                if (!e.key.repeat) screenshot();
                break;
            }
            input_.handleEvent(e);
            break;
        default: input_.handleEvent(e); break;
    }
}

bool Engine::processOnePostedEvent() {
    if (posted_.empty()) return false;
    PostedEvent p = std::move(posted_.front());
    posted_.pop_front();
    SDL_Event e;
    std::memcpy(&e, p.raw, sizeof(SDL_Event));
    if (e.type == SDL_EVENT_TEXT_INPUT) e.text.text = p.text.c_str();
    handleEvent(e);
    return true;
}

void Engine::processPendingEvents() {
    while (processOnePostedEvent()) {
    }
    if (!window_) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) handleEvent(e);
}

void Engine::renderIfDue() {
    if (!renderer_) return;
    if (!presenter_.canvasChanged() && !exposed_) return;
    const uint64_t now = SDL_GetTicksNS();
    if (!exposed_ && now - lastPresentNs_ < refreshNs_) return;
    presenter_.render(exposed_);
    exposed_ = false;
    lastPresentNs_ = now;
}

void Engine::pump() { pumpPhase(PumpPhase::Busy); }

void Engine::pumpPhase(PumpPhase phase) {
    processPendingEvents();
    if (paused()) {
        // Evt_GetNextEvent does not return while the application is inactive: freeze here.
        const uint32_t closes = closeCount_;
        setAppActive(false);
        SDL_Event e;
        while (paused() && closeCount_ == closes) {
            if (exposed_ && !minimized_) renderIfDue();
            if (SDL_WaitEventTimeout(&e, 100)) {
                handleEvent(e);
                while (SDL_PollEvent(&e)) handleEvent(e);
            }
            runPumpHooks(PumpPhase::Frozen);
        }
        setAppActive(true);
    }
    runPumpHooks(phase);
    renderIfDue();
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
            if (!e.paused()) e.setAppActive(true);
        }
    } restore{*this};
    setAppActive(false);
    while (active()) {
        bool any = false;
        while (active() && processOnePostedEvent()) any = true;
        if (!active()) break;
        if (!window_) {
            if (!any) break;  // headless: nothing else can arrive
            runPumpHooks(PumpPhase::Frozen);
            continue;
        }
        renderIfDue();
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 20)) {
            handleEvent(e);
            while (active() && SDL_PollEvent(&e)) handleEvent(e);
        }
        runPumpHooks(PumpPhase::Frozen);
    }
    renderIfDue();
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

bool Engine::screenshot() {
    std::filesystem::path dir = config_.screenshotDir;
    if (dir.empty()) {
        char* pref = SDL_GetPrefPath("TMPort", "TMPort");
        if (!pref) {
            log::error("screenshot: no preference directory: {}", SDL_GetError());
            return false;
        }
        dir = pathFromUtf8(pref) / "screenshots";
        SDL_free(pref);
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::string name = std::format("tmport_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}.png", local.tm_year + 1900,
                                         local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, ms);
    const auto path = dir / name;
    if (!presenter_.writeGameAreaPng(path)) {
        log::error("screenshot: cannot write {}", pathToUtf8(path));
        return false;
    }
    log::info("screenshot: {}", pathToUtf8(path));
    return true;
}

}  // namespace tmport
