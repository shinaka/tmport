#include "input/event_translator.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>

#include <cmath>

#include "input/input_codes.h"

namespace tmport {

namespace {

// cp1252 bytes 0x80..0x9F and their Unicode code points (0 = undefined in cp1252).
constexpr uint16_t kCp1252High[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

int unicodeToAnsi(uint32_t cp) {
    if (cp >= 0x20 && cp < 0x7F) return static_cast<int>(cp);
    if (cp >= 0xA0 && cp <= 0xFF) return static_cast<int>(cp);
    for (int i = 0; i < 32; ++i)
        if (kCp1252High[i] != 0 && kCp1252High[i] == cp) return 0x80 + i;
    return -1;  // control character or no ANSI byte
}

}  // namespace

int EventTranslator::canvasToGameX(float cx) { return static_cast<int>(std::floor(cx)) - kGameOriginX; }
int EventTranslator::canvasToGameY(float cy) { return static_cast<int>(std::floor(cy)) - kGameOriginY; }

uint16_t EventTranslator::navigationVk(uint32_t key, uint32_t scancode, uint16_t mod) {
    switch (key) {
        case SDLK_PAGEUP: return input::vk::kPrior;
        case SDLK_PAGEDOWN: return input::vk::kNext;
        case SDLK_END: return input::vk::kEnd;
        case SDLK_HOME: return input::vk::kHome;
        case SDLK_LEFT: return input::vk::kLeft;
        case SDLK_UP: return input::vk::kUp;
        case SDLK_RIGHT: return input::vk::kRight;
        case SDLK_DOWN: return input::vk::kDown;
        case SDLK_INSERT: return input::vk::kInsert;
        case SDLK_DELETE: return input::vk::kDelete;
        default: break;
    }
    // Keypad with NumLock off: Windows reports the navigation virtual keys.
    if ((mod & SDL_KMOD_NUM) == 0) {
        switch (scancode) {
            case SDL_SCANCODE_KP_9: return input::vk::kPrior;
            case SDL_SCANCODE_KP_3: return input::vk::kNext;
            case SDL_SCANCODE_KP_1: return input::vk::kEnd;
            case SDL_SCANCODE_KP_7: return input::vk::kHome;
            case SDL_SCANCODE_KP_4: return input::vk::kLeft;
            case SDL_SCANCODE_KP_8: return input::vk::kUp;
            case SDL_SCANCODE_KP_6: return input::vk::kRight;
            case SDL_SCANCODE_KP_2: return input::vk::kDown;
            case SDL_SCANCODE_KP_0: return input::vk::kInsert;
            case SDL_SCANCODE_KP_PERIOD: return input::vk::kDelete;
            default: break;
        }
    }
    return 0;
}

uint16_t EventTranslator::controlChar(uint32_t key, uint16_t mod) {
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    const bool alt = (mod & SDL_KMOD_ALT) != 0;
    if (alt) return 0;  // Alt+key is WM_SYSCHAR (menu); Ctrl+Alt is AltGr (text arrives as text input)
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return ctrl ? 0x0A : input::kEnter;
        case SDLK_ESCAPE: return input::kEsc;
        case SDLK_BACKSPACE: return ctrl ? 0x7F : input::kBackspace;
        case SDLK_TAB: return ctrl ? 0 : input::kTab;
        default: break;
    }
    if (!ctrl) return 0;
    if (key >= SDLK_A && key <= SDLK_Z) return static_cast<uint16_t>(key - SDLK_A + 1);  // Ctrl+A = 0x01
    switch (key) {
        case SDLK_LEFTBRACKET: return 0x1B;
        case SDLK_BACKSLASH: return 0x1C;
        case SDLK_RIGHTBRACKET: return 0x1D;
        default: return 0;
    }
}

std::vector<uint8_t> EventTranslator::utf8ToAnsi(std::string_view s) {
    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<uint8_t>(s[i]);
        uint32_t cp = 0;
        size_t len = 1;
        if (b0 < 0x80) {
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1Fu;
            len = 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0Fu;
            len = 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07u;
            len = 4;
        } else {
            ++i;  // stray continuation byte
            continue;
        }
        if (i + len > s.size()) break;
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            const auto b = static_cast<uint8_t>(s[i + k]);
            if ((b & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if (!ok) {
            ++i;
            continue;
        }
        i += len;
        const int a = unicodeToAnsi(cp);
        if (a >= 0) out.push_back(static_cast<uint8_t>(a));
    }
    return out;
}

size_t EventTranslator::translate(const SDL_Event& event, std::vector<WinMsg>& out) const {
    const size_t before = out.size();
    SDL_Event e = event;
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (renderer_) SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            break;
        default: break;
    }

    switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: out.push_back(WinMsg::close()); break;

        case SDL_EVENT_KEY_DOWN: {
            if (const uint16_t vk = navigationVk(e.key.key, e.key.scancode, e.key.mod)) {
                // With Alt held Windows sends WM_SYSKEYDOWN, which the game's WndProc ignores.
                // (Key-ups always pass, so a held flag can never stick.)
                if ((e.key.mod & SDL_KMOD_ALT) == 0) out.push_back(WinMsg::keyDown(vk));
            } else if (const uint16_t c = controlChar(e.key.key, e.key.mod)) {
                out.push_back(WinMsg::chr(c));
            }
            break;
        }
        case SDL_EVENT_KEY_UP:
            if (const uint16_t vk = navigationVk(e.key.key, e.key.scancode, e.key.mod)) out.push_back(WinMsg::keyUp(vk));
            break;

        case SDL_EVENT_TEXT_INPUT:
            if (e.text.text)
                for (uint8_t b : utf8ToAnsi(e.text.text)) out.push_back(WinMsg::chr(b));
            break;

        case SDL_EVENT_MOUSE_MOTION:
            out.push_back(WinMsg::mouse(WinMsg::Type::MouseMove, canvasToGameX(e.motion.x), canvasToGameY(e.motion.y)));
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            WinMsg::Type t;
            if (e.button.button == SDL_BUTTON_LEFT)
                t = down ? WinMsg::Type::LButtonDown : WinMsg::Type::LButtonUp;
            else if (e.button.button == SDL_BUTTON_RIGHT)
                t = down ? WinMsg::Type::RButtonDown : WinMsg::Type::RButtonUp;
            else
                break;  // the original ignores the middle and extra buttons
            out.push_back(WinMsg::mouse(t, canvasToGameX(e.button.x), canvasToGameY(e.button.y)));
            break;
        }
        default: break;
    }
    return out.size() - before;
}

}  // namespace tmport
