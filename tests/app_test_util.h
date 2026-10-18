#pragma once
// Helpers for the app-area tests (menu bar, engine glue, bootstrap): synthetic SDL events in
// 640x480 canvas coordinates (windowID 0) for Engine::postEvent. Nothing opens a window.

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "util/file_util.h"

namespace apptest {

// Output folder of this test process: $TM_APP_SHOTS/<name> when that variable is set (to look at
// the PNGs), else a fresh folder under the system temp directory (unique per process, because
// other test runs of the shared test binary may run at the same time).
inline std::filesystem::path outDir(const std::string& name) {
    std::filesystem::path base;
    if (auto env = tmport::getEnv("TM_APP_SHOTS"); env && !env->empty()) {
        base = tmport::pathFromUtf8(*env);
    } else {
        static const std::string unique = [] {
            std::random_device rd;
            const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
            return std::to_string(static_cast<unsigned long long>(t) ^ rd());
        }();
        base = std::filesystem::temp_directory_path() / ("tmport_app_" + unique);
        // Removed again when the test process ends.
        static const struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(p, ec);
            }
        } cleanup{base};
    }
    const auto d = base / name;
    std::error_code ec;
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d, ec);
    return d;
}

// Records every SDL log message (tmport::log) while it exists, and still passes them on to the
// previous output function. Process-local evidence of what a run did (e.g. "script: canvas -> f").
class LogCapture {
public:
    LogCapture() {
        SDL_GetLogOutputFunction(&prevFn_, &prevData_);
        SDL_SetLogOutputFunction(&LogCapture::output, this);
    }
    ~LogCapture() { SDL_SetLogOutputFunction(prevFn_, prevData_); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    bool has(std::string_view text) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::string& l : lines_)
            if (l.find(text) != std::string::npos) return true;
        return false;
    }
    std::vector<std::string> lines() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_;
    }

private:
    static void SDLCALL output(void* self, int category, SDL_LogPriority priority, const char* message) {
        auto* c = static_cast<LogCapture*>(self);
        {
            std::lock_guard<std::mutex> lock(c->mutex_);
            c->lines_.emplace_back(message ? message : "");
        }
        if (c->prevFn_) c->prevFn_(c->prevData_, category, priority, message);
    }

    SDL_LogOutputFunction prevFn_ = nullptr;
    void* prevData_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
};

inline SDL_Event key(bool down, SDL_Keycode k, SDL_Keymod mod = SDL_KMOD_NONE) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.key = k;
    e.key.mod = mod;
    e.key.down = down;
    return e;
}

inline SDL_Event textInput(const char* utf8) {
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = utf8;
    return e;
}

inline SDL_Event button(bool down, float x, float y, Uint8 b = SDL_BUTTON_LEFT) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = b;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    return e;
}

inline SDL_Event motion(float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = x;
    e.motion.y = y;
    return e;
}

inline SDL_Event simple(SDL_EventType t) {
    SDL_Event e{};
    e.type = t;
    return e;
}

// Posts a key press (down + up) on anything with postEvent(const SDL_Event&).
template <class E>
void press(E& engine, SDL_Keycode k, SDL_Keymod mod = SDL_KMOD_NONE) {
    engine.postEvent(key(true, k, mod));
    engine.postEvent(key(false, k, mod));
}

template <class E>
void click(E& engine, int x, int y) {
    const float fx = static_cast<float>(x) + 0.5f, fy = static_cast<float>(y) + 0.5f;
    engine.postEvent(motion(fx, fy));
    engine.postEvent(button(true, fx, fy));
    engine.postEvent(button(false, fx, fy));
}

}  // namespace apptest
