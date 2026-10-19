#include "dos/input/keyboard.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <charconv>
#include <string>

#include "util/file_util.h"

namespace tmport::dos {

using namespace keybits;

// ------------------------------------------------------------------------------- key mapping

std::optional<PcKey> pcKeyForSdlScancode(uint32_t sdlScancode) {
    const auto sc = static_cast<SDL_Scancode>(sdlScancode);
    // Letters: SDL orders them alphabetically, the PC by keyboard rows.
    static constexpr uint8_t kLetters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                             0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                             0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return PcKey{kLetters[sc - SDL_SCANCODE_A], false};
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return PcKey{static_cast<uint8_t>(0x02 + (sc - SDL_SCANCODE_1)), false};
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return PcKey{static_cast<uint8_t>(0x3B + (sc - SDL_SCANCODE_F1)), false};
    switch (sc) {
        case SDL_SCANCODE_RETURN: return PcKey{0x1C, false};
        case SDL_SCANCODE_ESCAPE: return PcKey{0x01, false};
        case SDL_SCANCODE_BACKSPACE: return PcKey{0x0E, false};
        case SDL_SCANCODE_TAB: return PcKey{0x0F, false};
        case SDL_SCANCODE_SPACE: return PcKey{0x39, false};
        case SDL_SCANCODE_MINUS: return PcKey{0x0C, false};
        case SDL_SCANCODE_EQUALS: return PcKey{0x0D, false};
        case SDL_SCANCODE_LEFTBRACKET: return PcKey{0x1A, false};
        case SDL_SCANCODE_RIGHTBRACKET: return PcKey{0x1B, false};
        case SDL_SCANCODE_BACKSLASH:
        case SDL_SCANCODE_NONUSHASH: return PcKey{0x2B, false};
        case SDL_SCANCODE_SEMICOLON: return PcKey{0x27, false};
        case SDL_SCANCODE_APOSTROPHE: return PcKey{0x28, false};
        case SDL_SCANCODE_GRAVE: return PcKey{0x29, false};
        case SDL_SCANCODE_COMMA: return PcKey{0x33, false};
        case SDL_SCANCODE_PERIOD: return PcKey{0x34, false};
        case SDL_SCANCODE_SLASH: return PcKey{0x35, false};
        case SDL_SCANCODE_CAPSLOCK: return PcKey{0x3A, false};
        case SDL_SCANCODE_F11: return PcKey{0x57, false};
        case SDL_SCANCODE_F12: return PcKey{0x58, false};
        case SDL_SCANCODE_PRINTSCREEN: return PcKey{0x37, true};  // E0 37 (Kbd_ISR swallows 37)
        case SDL_SCANCODE_SCROLLLOCK: return PcKey{0x46, false};
        case SDL_SCANCODE_INSERT: return PcKey{0x52, true};
        case SDL_SCANCODE_HOME: return PcKey{0x47, true};
        case SDL_SCANCODE_PAGEUP: return PcKey{0x49, true};
        case SDL_SCANCODE_DELETE: return PcKey{0x53, true};
        case SDL_SCANCODE_END: return PcKey{0x4F, true};
        case SDL_SCANCODE_PAGEDOWN: return PcKey{0x51, true};
        case SDL_SCANCODE_RIGHT: return PcKey{0x4D, true};
        case SDL_SCANCODE_LEFT: return PcKey{0x4B, true};
        case SDL_SCANCODE_DOWN: return PcKey{0x50, true};
        case SDL_SCANCODE_UP: return PcKey{0x48, true};
        case SDL_SCANCODE_NUMLOCKCLEAR: return PcKey{0x45, false};
        case SDL_SCANCODE_KP_DIVIDE: return PcKey{0x35, true};
        case SDL_SCANCODE_KP_MULTIPLY: return PcKey{0x37, false};
        case SDL_SCANCODE_KP_MINUS: return PcKey{0x4A, false};
        case SDL_SCANCODE_KP_PLUS: return PcKey{0x4E, false};
        case SDL_SCANCODE_KP_ENTER: return PcKey{0x1C, true};
        case SDL_SCANCODE_KP_1: return PcKey{0x4F, false};
        case SDL_SCANCODE_KP_2: return PcKey{0x50, false};
        case SDL_SCANCODE_KP_3: return PcKey{0x51, false};
        case SDL_SCANCODE_KP_4: return PcKey{0x4B, false};
        case SDL_SCANCODE_KP_5: return PcKey{0x4C, false};
        case SDL_SCANCODE_KP_6: return PcKey{0x4D, false};
        case SDL_SCANCODE_KP_7: return PcKey{0x47, false};
        case SDL_SCANCODE_KP_8: return PcKey{0x48, false};
        case SDL_SCANCODE_KP_9: return PcKey{0x49, false};
        case SDL_SCANCODE_KP_0: return PcKey{0x52, false};
        case SDL_SCANCODE_KP_PERIOD: return PcKey{0x53, false};
        case SDL_SCANCODE_NONUSBACKSLASH: return PcKey{0x56, false};
        case SDL_SCANCODE_LCTRL: return PcKey{0x1D, false};
        case SDL_SCANCODE_LSHIFT: return PcKey{0x2A, false};
        case SDL_SCANCODE_LALT: return PcKey{0x38, false};
        case SDL_SCANCODE_RCTRL: return PcKey{0x1D, true};
        case SDL_SCANCODE_RSHIFT: return PcKey{0x36, false};
        case SDL_SCANCODE_RALT: return PcKey{0x38, true};
        default: return std::nullopt;
    }
}

namespace {

struct NamedKey {
    std::string_view name;
    PcKey key;
};
constexpr NamedKey kNamedKeys[] = {
    {"up", {0x48, true}},        {"down", {0x50, true}},      {"left", {0x4B, true}},     {"right", {0x4D, true}},
    {"home", {0x47, true}},      {"end", {0x4F, true}},       {"pgup", {0x49, true}},     {"pgdn", {0x51, true}},
    {"ins", {0x52, true}},       {"del", {0x53, true}},       {"kp0", {0x52, false}},     {"kp1", {0x4F, false}},
    {"kp2", {0x50, false}},      {"kp3", {0x51, false}},      {"kp4", {0x4B, false}},     {"kp5", {0x4C, false}},
    {"kp6", {0x4D, false}},      {"kp7", {0x47, false}},      {"kp8", {0x48, false}},     {"kp9", {0x49, false}},
    {"kp.", {0x53, false}},      {"kp+", {0x4E, false}},      {"kp-", {0x4A, false}},     {"kp*", {0x37, false}},
    {"kp/", {0x35, true}},       {"kpenter", {0x1C, true}},   {"enter", {0x1C, false}},   {"esc", {0x01, false}},
    {"space", {0x39, false}},    {"tab", {0x0F, false}},      {"backspace", {0x0E, false}}, {"caps", {0x3A, false}},
    {"lshift", {0x2A, false}},   {"rshift", {0x36, false}},   {"lctrl", {0x1D, false}},   {"rctrl", {0x1D, true}},
    {"lalt", {0x38, false}},     {"ralt", {0x38, true}},      {"numlock", {0x45, false}}, {"scrolllock", {0x46, false}},
    {"minus", {0x0C, false}},    {"equals", {0x0D, false}},   {"lbracket", {0x1A, false}}, {"rbracket", {0x1B, false}},
    {"semicolon", {0x27, false}}, {"quote", {0x28, false}},   {"backquote", {0x29, false}}, {"backslash", {0x2B, false}},
    {"comma", {0x33, false}},    {"period", {0x34, false}},   {"slash", {0x35, false}},   {"f11", {0x57, false}},
    {"f12", {0x58, false}},
};

}  // namespace

std::optional<PcKey> pcKeyByName(std::string_view name) {
    const std::string n = toLowerAscii(name);
    for (const NamedKey& k : kNamedKeys)
        if (n == k.name) return k.key;
    if (n.size() == 1) {
        const char c = n[0];
        if (c >= 'a' && c <= 'z') return pcKeyForSdlScancode(SDL_SCANCODE_A + static_cast<uint32_t>(c - 'a'));
        if (c >= '1' && c <= '9') return PcKey{static_cast<uint8_t>(0x02 + (c - '1')), false};
        if (c == '0') return PcKey{0x0B, false};
    }
    if (n.size() >= 2 && n[0] == 'f' && n.size() <= 3) {
        int f = 0;
        const auto [p, ec] = std::from_chars(n.data() + 1, n.data() + n.size(), f);
        if (ec == std::errc() && p == n.data() + n.size() && f >= 1 && f <= 10) return PcKey{static_cast<uint8_t>(0x3B + f - 1), false};
    }
    for (const auto& [prefix, ext] : {std::pair<std::string_view, bool>{"escan", true}, {"scan", false}}) {
        if (!n.starts_with(prefix)) continue;
        unsigned v = 0;
        const char* b = n.data() + prefix.size();
        const auto [p, ec] = std::from_chars(b, n.data() + n.size(), v, 16);
        if (ec == std::errc() && p == n.data() + n.size() && p != b && v >= 1 && v <= 0x7F) return PcKey{static_cast<uint8_t>(v), ext};
    }
    return std::nullopt;
}

// --------------------------------------------------------------------------------------- KbdIsr

// 43a0:0071 Kbd_ISR. The E0/E1 prefix test at its start compares the sign-extended byte with
// 0x00E0 / 0x00E1 and never matches, so a prefix byte takes the break path with scan 0x60 / 0x61
// (no table entry) and only runs the diagonal rule.
bool KbdIsr::onScanByte(uint8_t byte) {
    bool swallow = false;
    if (byte & 0x80) {
        // Release table CS 43a0:00DC (17 scan words, targets CS:00FE), scan = byte & 0x7F.
        switch (byte & 0x7F) {
            case 0x48: keys &= static_cast<uint16_t>(~kUp); break;
            case 0x50: keys &= static_cast<uint16_t>(~kDown); break;
            case 0x4B: keys &= static_cast<uint16_t>(~kLeft); break;
            case 0x4D: keys &= static_cast<uint16_t>(~kRight); break;
            case 0x47:  // Home
            case 0x58:  // F12 (Tandy Home)
                keys &= static_cast<uint16_t>(~(kUp | kLeft));
                break;
            case 0x49: keys &= static_cast<uint16_t>(~(kUp | kRight)); break;  // PgUp
            case 0x39: keys &= static_cast<uint16_t>(~kNet); break;
            case 0x29: if (tandy) keys &= static_cast<uint16_t>(~kUp); break;     // Tandy arrows
            case 0x4A: if (tandy) keys &= static_cast<uint16_t>(~kDown); break;
            case 0x2B: if (tandy) keys &= static_cast<uint16_t>(~kLeft); break;
            case 0x4E: if (tandy) keys &= static_cast<uint16_t>(~kRight); break;
            case 0x1D: ctrlReleased = true; break;
            case 0x37: case 0x45: case 0x46: case 0x54: swallow = true; break;
            default: break;
        }
    } else {
        // Press table CS 43a0:019F (targets CS:01C1).
        switch (byte) {
            case 0x48: keys |= kUp; break;
            case 0x50: keys |= kDown; break;
            case 0x4B: keys |= kLeft; break;
            case 0x4D: keys |= kRight; break;
            case 0x47:
            case 0x58: keys |= kUp | kLeft; break;
            case 0x49: keys |= kUp | kRight; break;
            case 0x39: keys |= kNet; break;
            case 0x29: if (tandy) keys |= kUp; break;
            case 0x4A: if (tandy) keys |= kDown; break;
            case 0x2B: if (tandy) keys |= kLeft; break;
            case 0x4E: if (tandy) keys |= kRight; break;
            case 0x1D: ctrlReleased = false; break;
            case 0x37: case 0x45: case 0x46: case 0x54: swallow = true; break;
            default: break;
        }
    }
    // 43a0:0245, after every byte: Up+Right or Up+Left becomes a jump (Up/Down dropped); the jump
    // bit made that way lasts until the next byte that does not form a diagonal.
    if ((keys & (kUp | kRight)) == (kUp | kRight) || (keys & (kUp | kLeft)) == (kUp | kLeft)) {
        keys = static_cast<uint16_t>((keys & ~(kUp | kDown)) | kJump);
        jumpFromDiagonal = true;
    } else if (jumpFromDiagonal) {
        keys &= static_cast<uint16_t>(~kJump);
        jumpFromDiagonal = false;
    }
    return !swallow;
}

// ----------------------------------------------------------------------------------- PcKeyboard

PcKeyboard::PcKeyboard(KeyboardOptions options) : options_(options) { isr_.tandy = options_.tandy; }

void PcKeyboard::setOptions(const KeyboardOptions& o) {
    options_ = o;
    isr_.tandy = o.tandy;
}

bool PcKeyboard::isHeld(PcKey key) const { return std::find(held_.begin(), held_.end(), key) != held_.end(); }

uint64_t PcKeyboard::clampTime(uint64_t t) {
    lastTime_ = std::max(lastTime_, t);
    return lastTime_;
}

void PcKeyboard::queueByte(uint8_t byte, uint64_t t, uint64_t repeatId) {
    // The keyboard sends its bytes one after another: the queue only grows at its end.
    t = std::max(t, lastQueued_);
    pending_.push_back({t, seq_++, byte, repeatId});
    lastQueued_ = t;
}

void PcKeyboard::queueKey(PcKey key, bool release, uint64_t t, uint64_t repeatId) {
    const uint8_t code = static_cast<uint8_t>(release ? (key.scan | 0x80) : key.scan);
    if (key.extended) {
        const uint64_t lead = leadNs(key);
        queueByte(0xE0, t >= lead ? t - lead : 0, repeatId);
    }
    queueByte(code, t, repeatId);
}

// Typematic repeat (the keyboard itself re-sends the make code of the last key pressed): first
// typematicDelayMs after the press, then every typematicRateMs. forDelivery also queues a repeat
// whose E0 byte (lead) is already due while its key byte is not.
void PcKeyboard::materializeRepeats(uint64_t t, bool forDelivery) {
    while (repeatKey_) {
        const uint64_t lead = forDelivery ? leadNs(*repeatKey_) : 0;
        const uint64_t first = nextRepeat_ >= lead ? nextRepeat_ - lead : 0;
        if (first > t) break;
        inflightRepeat_ = ++repeatIds_;
        queueKey(*repeatKey_, false, nextRepeat_, inflightRepeat_);
        ++repeats_;
        nextRepeat_ += static_cast<uint64_t>(options_.typematicRateMs) * 1'000'000u;
    }
}

void PcKeyboard::keyDown(PcKey key, uint64_t t) {
    t = clampTime(t);
    if (isHeld(key)) return;  // host auto-repeat: the engine makes its own repeats
    materializeRepeats(t, false);
    held_.push_back(key);
    queueKey(key, false, t, 0);
    repeatKey_ = key;
    nextRepeat_ = t + static_cast<uint64_t>(options_.typematicDelayMs) * 1'000'000u;
}

void PcKeyboard::keyUp(PcKey key, uint64_t t) {
    t = clampTime(t);
    const auto it = std::find(held_.begin(), held_.end(), key);
    if (it == held_.end()) return;
    materializeRepeats(t, false);
    held_.erase(it);
    if (repeatKey_ == key) {
        // A repeat whose E0 went out early but whose key byte would follow the release never
        // happens: drop its undelivered bytes.
        if (inflightRepeat_ != 0) {
            const auto last = std::find_if(pending_.rbegin(), pending_.rend(),
                                           [&](const Pending& p) { return p.repeatId == inflightRepeat_; });
            if (last != pending_.rend() && last->time > t) {
                std::erase_if(pending_, [&](const Pending& p) { return p.repeatId == inflightRepeat_; });
                lastQueued_ = pending_.empty() ? lastDelivered_ : std::max(lastDelivered_, pending_.back().time);
            }
        }
        repeatKey_.reset();
        inflightRepeat_ = 0;
    }
    queueKey(key, true, t, 0);
}

void PcKeyboard::releaseAll(uint64_t t) {
    const std::vector<PcKey> keys = held_;
    for (const PcKey& k : keys) keyUp(k, t);
}

void PcKeyboard::deliverByte(uint8_t byte) {
    ++bytesDelivered_;
    const bool prefix = byte == 0xE0 || byte == 0xE1;
    bool toBios = true;
    if (!prefix || options_.greyKeyE0Artefact) toBios = isr_.onScanByte(byte);
    if (toBios) bios_.onScanByte(byte);  // the old INT 09h handler (Kbd_ISR chains to it)
}

void PcKeyboard::deliverUntil(uint64_t t) {
    t = clampTime(t);
    materializeRepeats(t, true);
    size_t n = 0;
    while (n < pending_.size() && pending_[n].time <= t) deliverByte(pending_[n++].byte);
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(n));
    lastDelivered_ = t;
    lastQueued_ = std::max(lastQueued_, t);
}

std::optional<uint64_t> PcKeyboard::nextDue() const {
    std::optional<uint64_t> due;
    if (!pending_.empty()) due = pending_.front().time;
    if (repeatKey_) {
        const uint64_t lead = leadNs(*repeatKey_);
        const uint64_t first = nextRepeat_ >= lead ? nextRepeat_ - lead : 0;
        due = due ? std::min(*due, first) : first;
    }
    return due;
}

}  // namespace tmport::dos
