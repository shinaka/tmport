#include "app/script_input.h"

#include <SDL3/SDL.h>

#include <charconv>
#include <format>

#include "app/app_context.h"
#include "platform/engine.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::app {

namespace {

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

bool toInt(std::string_view s, int& v) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return !s.empty() && ec == std::errc() && p == s.data() + s.size();
}

struct NamedKey {
    std::string_view name;
    SDL_Keycode key;
    SDL_Scancode scan;
};
constexpr NamedKey kKeys[] = {
    {"enter", SDLK_RETURN, SDL_SCANCODE_RETURN},   {"esc", SDLK_ESCAPE, SDL_SCANCODE_ESCAPE},
    {"space", SDLK_SPACE, SDL_SCANCODE_SPACE},     {"tab", SDLK_TAB, SDL_SCANCODE_TAB},
    {"backspace", SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE},
    {"up", SDLK_UP, SDL_SCANCODE_UP},              {"down", SDLK_DOWN, SDL_SCANCODE_DOWN},
    {"left", SDLK_LEFT, SDL_SCANCODE_LEFT},        {"right", SDLK_RIGHT, SDL_SCANCODE_RIGHT},
    {"home", SDLK_HOME, SDL_SCANCODE_HOME},        {"end", SDLK_END, SDL_SCANCODE_END},
    {"pgup", SDLK_PAGEUP, SDL_SCANCODE_PAGEUP},    {"pgdn", SDLK_PAGEDOWN, SDL_SCANCODE_PAGEDOWN},
    {"insert", SDLK_INSERT, SDL_SCANCODE_INSERT},  {"delete", SDLK_DELETE, SDL_SCANCODE_DELETE},
    {"alt", SDLK_LALT, SDL_SCANCODE_LALT},
    {"f1", SDLK_F1, SDL_SCANCODE_F1},   {"f2", SDLK_F2, SDL_SCANCODE_F2},   {"f3", SDLK_F3, SDL_SCANCODE_F3},
    {"f4", SDLK_F4, SDL_SCANCODE_F4},   {"f5", SDLK_F5, SDL_SCANCODE_F5},   {"f6", SDLK_F6, SDL_SCANCODE_F6},
    {"f7", SDLK_F7, SDL_SCANCODE_F7},   {"f8", SDLK_F8, SDL_SCANCODE_F8},   {"f9", SDLK_F9, SDL_SCANCODE_F9},
    {"f10", SDLK_F10, SDL_SCANCODE_F10}, {"f11", SDLK_F11, SDL_SCANCODE_F11}, {"f12", SDLK_F12, SDL_SCANCODE_F12},
};

bool parseKey(std::string_view tok, ScriptStep& s) {
    std::string t = lower(tok);
    uint16_t mod = 0;
    for (;;) {
        if (t.starts_with("alt+")) mod |= SDL_KMOD_LALT, t.erase(0, 4);
        else if (t.starts_with("ctrl+")) mod |= SDL_KMOD_LCTRL, t.erase(0, 5);
        else if (t.starts_with("shift+")) mod |= SDL_KMOD_LSHIFT, t.erase(0, 6);
        else break;
    }
    s.kind = ScriptStep::Kind::Key;
    s.mod = mod;
    for (const NamedKey& k : kKeys) {
        if (k.name == t) {
            s.keycode = k.key;
            s.scancode = k.scan;
            // Printable keys reach the game as text (WM_CHAR), as SDL sends them.
            if (k.key == SDLK_SPACE && (mod & (SDL_KMOD_ALT | SDL_KMOD_CTRL)) == 0) s.text = " ";
            return true;
        }
    }
    if (t.size() == 1 && ((t[0] >= 'a' && t[0] <= 'z') || (t[0] >= '0' && t[0] <= '9'))) {
        const char c = t[0];
        s.keycode = static_cast<SDL_Keycode>(c);
        s.scancode = c >= 'a' ? static_cast<SDL_Scancode>(SDL_SCANCODE_A + (c - 'a'))
                              : (c == '0' ? SDL_SCANCODE_0 : static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (c - '1')));
        if ((mod & (SDL_KMOD_ALT | SDL_KMOD_CTRL)) == 0) {
            char typed = c;
            if ((mod & SDL_KMOD_SHIFT) && c >= 'a' && c <= 'z') typed = static_cast<char>(c - 'a' + 'A');
            s.text = std::string(1, typed);
        }
        return true;
    }
    return false;
}

}  // namespace

std::optional<std::vector<ScriptStep>> parseScript(std::string_view text, std::string& error) {
    std::vector<ScriptStep> steps;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(',', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view tok = text.substr(pos, end - pos);
        while (!tok.empty() && tok.front() == ' ') tok.remove_prefix(1);
        while (!tok.empty() && tok.back() == ' ') tok.remove_suffix(1);
        pos = end + 1;
        if (tok.empty()) {
            if (end == text.size()) break;
            continue;
        }
        ScriptStep s;
        const size_t colon = tok.find(':');
        const std::string head = lower(tok.substr(0, colon));
        const std::string_view rest = colon == std::string_view::npos ? std::string_view() : tok.substr(colon + 1);
        if (head == "wait") {
            int ms = 0;
            if (!toInt(rest, ms) || ms < 0) {
                error = std::format("script: bad wait '{}'", tok);
                return std::nullopt;
            }
            s.kind = ScriptStep::Kind::Wait;
            s.ms = static_cast<uint32_t>(ms);
        } else if (head == "shot") {
            if (rest.empty()) {
                error = "script: shot needs a file name";
                return std::nullopt;
            }
            s.kind = ScriptStep::Kind::Shot;
            s.path = pathFromUtf8(std::string(rest));
        } else if (head == "click" || head == "move") {
            const size_t c2 = rest.find(':');
            if (c2 == std::string_view::npos || !toInt(rest.substr(0, c2), s.x) || !toInt(rest.substr(c2 + 1), s.y)) {
                error = std::format("script: bad position in '{}'", tok);
                return std::nullopt;
            }
            s.kind = head == "click" ? ScriptStep::Kind::Click : ScriptStep::Kind::Move;
        } else if (head == "quit" && colon == std::string_view::npos) {
            s.kind = ScriptStep::Kind::Quit;
        } else if (!parseKey(tok, s)) {
            error = std::format("script: unknown step '{}'", tok);
            return std::nullopt;
        }
        steps.push_back(std::move(s));
        if (end == text.size()) break;
    }
    return steps;
}

ScriptInput::ScriptInput(Engine& engine, std::vector<ScriptStep> steps, bool quitAtEnd)
    : engine_(engine), steps_(std::move(steps)), quitAtEnd_(quitAtEnd) {
    hookId_ = engine_.addPumpHook([this](PumpPhase p) { onPump(p); });
}

ScriptInput::~ScriptInput() { engine_.removePumpHook(hookId_); }

void ScriptInput::post(const ScriptStep& s) {
    SDL_Event e{};
    switch (s.kind) {
        case ScriptStep::Kind::Key: {
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = s.keycode;
            e.key.scancode = static_cast<SDL_Scancode>(s.scancode);
            e.key.mod = s.mod;
            e.key.down = true;
            engine_.postEvent(e);
            if (!s.text.empty()) {
                SDL_Event t{};
                t.type = SDL_EVENT_TEXT_INPUT;
                t.text.text = s.text.c_str();  // copied by postEvent
                engine_.postEvent(t);
            }
            e.type = SDL_EVENT_KEY_UP;
            e.key.down = false;
            engine_.postEvent(e);
            break;
        }
        case ScriptStep::Kind::Move:
        case ScriptStep::Kind::Click: {
            SDL_Event m{};
            m.type = SDL_EVENT_MOUSE_MOTION;
            m.motion.x = static_cast<float>(s.x) + 0.5f;
            m.motion.y = static_cast<float>(s.y) + 0.5f;
            engine_.postEvent(m);
            if (s.kind == ScriptStep::Kind::Move) break;
            e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.down = true;
            e.button.clicks = 1;
            e.button.x = m.motion.x;
            e.button.y = m.motion.y;
            engine_.postEvent(e);
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.down = false;
            engine_.postEvent(e);
            break;
        }
        default: break;
    }
}

void ScriptInput::onPump(PumpPhase phase) {
    if (phase != PumpPhase::Fetch) return;
    if (engine_.pendingPostedEvents() > 0) return;  // let the last step arrive first
    if (waitUntil_ && engine_.clock().nowMs() < waitUntil_) return;
    waitUntil_ = 0;
    while (next_ < steps_.size()) {
        const ScriptStep& s = steps_[next_++];
        switch (s.kind) {
            case ScriptStep::Kind::Wait:
                waitUntil_ = engine_.clock().nowMs() + s.ms;
                return;
            case ScriptStep::Kind::Shot:
                if (engine_.presenter().writeCanvasPng(s.path)) {
                    ++shots_;
                    log::info("script: canvas -> {}", pathToUtf8(s.path));
                } else {
                    log::error("script: cannot write {}", pathToUtf8(s.path));
                }
                continue;  // a shot takes no time: go on with the next step
            case ScriptStep::Kind::Quit: throw QuitGame{};
            default:
                post(s);
                return;  // one input step per fetch
        }
    }
    // Quit once the game has taken the last input (or after a while if it never fetches it).
    if (quitAtEnd_ && (engine_.input().empty() || ++endFetches_ > 200)) {
        log::info("script: finished");
        throw QuitGame{};
    }
}

}  // namespace tmport::app
