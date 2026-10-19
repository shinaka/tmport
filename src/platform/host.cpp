#include "platform/host.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <format>

#include "util/file_util.h"
#include "util/log.h"

namespace tmport::platform {

static_assert(sizeof(SDL_Event) == 128, "Host::PostedEvent assumes a 128-byte SDL_Event");

Host::Host(HostConfig config)
    : config_(std::move(config)),
      clock_(config_.clock ? config_.clock : &Clock::system()),
      presenter_(config_.layout),
      fullscreen_(config_.fullscreen) {}

Host::~Host() { closeWindow(); }

// ------------------------------------------------------------------------------------- window

bool Host::openWindow() {
    if (window_) return true;
    // Window close must arrive only as SDL_EVENT_WINDOW_CLOSE_REQUESTED; the editions ask the
    // player and quit on their own.
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        log::error("SDL_Init(VIDEO) failed: {}", SDL_GetError());
        return false;
    }
    sdlVideoInit_ = true;

    int desktopW = 0, desktopH = 0;
    if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())) {
        desktopW = dm->w;
        desktopH = dm->h;
    }
    const gfx::WindowSize size = gfx::initialWindowSize(layout(), config_.windowScale, config_.integerScale, desktopW, desktopH);
    // The windowed size is kept for leaving fullscreen; SDL_WINDOW_FULLSCREEN without a display mode
    // is borderless desktop fullscreen.
    window_ = SDL_CreateWindow(config_.title.c_str(), size.w, size.h,
                               SDL_WINDOW_RESIZABLE | (fullscreen_ ? SDL_WINDOW_FULLSCREEN : SDL_WindowFlags{0}));
    if (!window_) {
        log::error("SDL_CreateWindow failed: {}", SDL_GetError());
        closeWindow();
        return false;
    }
    if (iconHook_) iconHook_(window_);
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
    // Printable characters arrive as text input (WM_CHAR; the DOS edition's name entry).
    SDL_StartTextInput(window_);
    exposed_ = true;
    focused_ = true;
    minimized_ = false;
    return true;
}

void Host::closeWindow() {
    if (window_) SDL_StopTextInput(window_);
    presenter_.detach();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    if (sdlVideoInit_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    sdlVideoInit_ = false;
}

void Host::setTitle(std::string_view title) {
    config_.title = std::string(title);
    if (window_) SDL_SetWindowTitle(window_, config_.title.c_str());
}

void Host::setFullscreen(bool on) {
    fullscreen_ = on;
    if (!window_) return;
    if (!SDL_SetWindowFullscreen(window_, on)) log::warn("fullscreen {}: {}", on ? "on" : "off", SDL_GetError());
    exposed_ = true;
}

// --------------------------------------------------------------------------------- presentation

void Host::renderIfDue() {
    if (!renderer_) return;
    if (!presenter_.canvasChanged() && !exposed_) return;
    const uint64_t now = SDL_GetTicksNS();
    if (!exposed_ && now - lastPresentNs_ < refreshNs_) return;
    presenter_.render(exposed_);
    exposed_ = false;
    lastPresentNs_ = now;
}

bool Host::screenshot() {
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
    const std::string name =
        std::format("{}_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}.png", config_.screenshotPrefix, local.tm_year + 1900,
                    local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, ms);
    const auto path = dir / name;
    if (!presenter_.writeGameAreaPng(path)) {
        log::error("screenshot: cannot write {}", pathToUtf8(path));
        return false;
    }
    log::info("screenshot: {}", pathToUtf8(path));
    return true;
}

// ------------------------------------------------------------------------------------- events

bool Host::handleFullscreenHotkey(const SDL_Event& e) {
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
        // A menu bar armed itself on the Alt press; it must not see the Alt release as a lone Alt.
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
            if (handlers_.input) handlers_.input(e);  // key-ups always reach the input model (held flags never stick)
            return true;
        }
    }
    return false;
}

void Host::dispatch(const SDL_Event& e) {
    if (handleFullscreenHotkey(e)) return;  // Alt+Enter / F11: before the edition sees anything
    if (handlers_.filter && handlers_.filter(e)) return;
    switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            quit_ = true;
            ++closeCount_;
            if (handlers_.input) handlers_.input(e);
            return;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: focused_ = true; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: focused_ = false; break;
        case SDL_EVENT_WINDOW_MINIMIZED: minimized_ = true; break;
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
            minimized_ = false;
            exposed_ = true;
            break;
        case SDL_EVENT_WINDOW_EXPOSED: exposed_ = true; break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            // Also when the system changed it (macOS green button, Win+Up, ...).
            fullscreen_ = e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
            exposed_ = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: exposed_ = true; break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_F12 && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT)) == 0) {
                if (!e.key.repeat) screenshot();
                return;
            }
            if (handlers_.input) handlers_.input(e);
            return;
        default:
            if (handlers_.input) handlers_.input(e);
            return;
    }
    if (handlers_.window) handlers_.window(e);
}

void Host::postEvent(const SDL_Event& e) {
    PostedEvent p{};
    std::memcpy(p.raw, &e, sizeof(SDL_Event));
    if (e.type == SDL_EVENT_TEXT_INPUT && e.text.text) p.text = e.text.text;
    posted_.push_back(std::move(p));
}

bool Host::dispatchPosted() {
    if (posted_.empty()) return false;
    PostedEvent p = std::move(posted_.front());
    posted_.pop_front();
    SDL_Event e;
    std::memcpy(&e, p.raw, sizeof(SDL_Event));
    if (e.type == SDL_EVENT_TEXT_INPUT) e.text.text = p.text.c_str();
    dispatch(e);
    return true;
}

void Host::dispatchPending() {
    while (dispatchPosted()) {
    }
    if (!window_) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) dispatch(e);
}

bool Host::waitAndDispatch(int timeoutMs, const std::function<bool()>& more) {
    SDL_Event e;
    if (!SDL_WaitEventTimeout(&e, timeoutMs)) return false;
    dispatch(e);
    while ((!more || more()) && SDL_PollEvent(&e)) dispatch(e);
    return true;
}

void Host::freezeWhilePaused(const std::function<void()>& eachIteration) {
    if (!paused()) return;
    // The original's event fetch does not return while the application is inactive: freeze here.
    const uint32_t closes = closeCount_;
    setAppActive(false);
    while (paused() && closeCount_ == closes) {
        if (exposed_ && !minimized_) renderIfDue();
        waitAndDispatch(100);
        if (eachIteration) eachIteration();
    }
    setAppActive(true);
}

void Host::pump() {
    dispatchPending();
    freezeWhilePaused();
    renderIfDue();
}

// -------------------------------------------------------------------------------------- state

bool Host::paused() const {
    if (!window_) return false;
    return minimized_ || (config_.pauseOnFocusLoss && !focused_);
}

void Host::setAppActive(bool active) {
    if (appActive_ == active) return;
    appActive_ = active;
    if (handlers_.activation) handlers_.activation(active);
}

uint64_t Host::nowNs() const {
    if (config_.clock) return config_.clock->nowMs() * 1'000'000ull;
    return SDL_GetTicksNS();
}

}  // namespace tmport::platform
