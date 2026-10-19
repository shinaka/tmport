#pragma once
// The DOS edition's keyboard: the game's INT 09h handler Kbd_ISR (43a0:0071, kbd.asm) and the PC
// keyboard in front of it (re/dos/docs/code_runtime.md §5, PORTING_NOTES.md §4, §6).
//
//  host key down/up (SDL scancode)
//    -> PcKey (set-1 scan code, E0 prefix for the grey keys)
//    -> PcKeyboard: scan bytes in time order, plus SYNTHESISED typematic repeats of the last key
//       pressed (DOSBox-X: first repeat 500 ms after the press, then every 33 ms; host key repeats
//       are ignored)
//    -> KbdIsr (held bits g_keys DS:9105, the diagonal/jump rule, swallowed scans)
//    -> BiosKeyboard (key words for Input_Poll), unless Kbd_ISR swallowed the byte.
//
// Bytes are delivered with PcKeyboard::deliverUntil(t) right before each game tick, so a tick sees
// every key event that happened before it, in order. Times are the runtime's virtual nanoseconds.
//
// The grey-key E0 artefact [O r2/fl_jumps]: Kbd_ISR's E0/E1 test never matches, so the E0 prefix
// byte runs the diagonal rule like a key event and clears a diagonal jump bit until the key byte
// sets it again; a tick in between sees g_keys without the jump. Off by default (PORTING_NOTES §6,
// "make optional"): the E0 byte then only reaches the BIOS. When on, a prefix byte is delivered
// prefixLeadUs before its key byte.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "dos/input/bios_keyboard.h"

namespace tmport::dos {

// g_keys (DS:9105) bits. The high byte holds Player_Update's terrain-mask copy.
namespace keybits {
inline constexpr uint16_t kUp = 0x01;
inline constexpr uint16_t kDown = 0x02;
inline constexpr uint16_t kLeft = 0x04;
inline constexpr uint16_t kRight = 0x08;
inline constexpr uint16_t kNet = 0x10;    // Space
inline constexpr uint16_t kJump = 0x20;   // a diagonal (Up+Left / Up+Right) or a mouse jump
inline constexpr uint16_t kCoin = 0x40;   // drop a coin (Input_MouseWalk: Down where Down cannot climb)
}  // namespace keybits

// A PC keyboard key: its set-1 make code and whether it sends the E0 prefix (grey keys).
struct PcKey {
    uint8_t scan = 0;
    bool extended = false;
    bool operator==(const PcKey&) const = default;
};

// SDL scancode (SDL_Scancode value) -> PC key, by position (a US keyboard without a layout driver, as
// DOS and DOSBox-X default to). nullopt for keys an AT keyboard does not have (GUI, media, Pause).
std::optional<PcKey> pcKeyForSdlScancode(uint32_t sdlScancode);
// Key names for scripted input: up down left right home end pgup pgdn ins del (grey block),
// kp0..kp9 kp. kp+ kp- kp* kp/ kpenter (keypad), enter esc space tab backspace caps, lshift rshift
// lctrl rctrl lalt ralt, f1..f12, a..z, 0..9, minus equals lbracket rbracket semicolon quote
// backquote backslash comma period slash, scanHH and escanHH (raw hex make code; escan = E0-prefixed).
// Case-insensitive.
std::optional<PcKey> pcKeyByName(std::string_view name);

// 43a0:0071 Kbd_ISR: the game's part of INT 09h for one scan byte.
struct KbdIsr {
    uint16_t keys = 0;              // g_keys DS:9105
    bool jumpFromDiagonal = false;  // DS:9257 (also set by Input_MouseWalk for mouse jumps)
    bool ctrlReleased = false;      // DS:92FA: 0 on Ctrl make, 1 on break; nothing reads it
    bool tandy = false;             // g_machineId DS:1300 == 0x21: scans 29/4A/2B/4E are the arrows

    // Applies the press/release tables (CS 43a0:019F / 00DC) and the diagonal rule. Returns false
    // when the byte is swallowed (scans 37, 45, 46, 54: not passed to the BIOS).
    bool onScanByte(uint8_t byte);

    // Kbd_Install (43a0:0002): DS:9257 = DS:92FA = 0.
    void install() {
        jumpFromDiagonal = false;
        ctrlReleased = false;
    }
};

struct KeyboardOptions {
    uint32_t typematicDelayMs = 500;  // DOSBox-X keyboard repeat delay; 24 ticks measured (r2/fl_typematic)
    uint32_t typematicRateMs = 33;    // DOSBox-X repeat period; 32.9 ms measured over 95 repeats (r2/fl_shack_left)
    bool greyKeyE0Artefact = false;   // feed E0 prefix bytes to Kbd_ISR (see the header comment)
    uint32_t prefixLeadUs = 1000;     // an E0 byte goes this long before its key byte
    bool tandy = false;               // KbdIsr::tandy
};

class PcKeyboard {
public:
    explicit PcKeyboard(KeyboardOptions options = {});

    const KeyboardOptions& options() const { return options_; }
    void setOptions(const KeyboardOptions& o);

    // A key went down / up at virtual time t (ns, non-decreasing; earlier times are taken as the
    // latest time seen). A down for a key already held (host auto-repeat) is ignored, as is an up
    // for a key not held. The last key pressed repeats until it is released; releasing another key
    // does not stop it, and a released repeat key does not hand the repeat back to older keys.
    void keyDown(PcKey key, uint64_t t);
    void keyUp(PcKey key, uint64_t t);
    // Releases every held key (focus loss), in press order.
    void releaseAll(uint64_t t);
    bool isHeld(PcKey key) const;

    // Delivers every scan byte due at or before t (key events and typematic repeats) through
    // Kbd_ISR and the BIOS, in time order.
    void deliverUntil(uint64_t t);
    // Time of the next byte not yet delivered (a typematic repeat or a queued event), if any.
    std::optional<uint64_t> nextDue() const;

    KbdIsr& isr() { return isr_; }
    const KbdIsr& isr() const { return isr_; }
    BiosKeyboard& bios() { return bios_; }
    const BiosKeyboard& bios() const { return bios_; }

    // Diagnostics for tests.
    uint64_t bytesDelivered() const { return bytesDelivered_; }
    uint64_t repeatsGenerated() const { return repeats_; }

private:
    struct Pending {
        uint64_t time;
        uint64_t seq;
        uint8_t byte;
        uint64_t repeatId;  // 0 for key events, else the typematic repeat it belongs to
    };
    void queueKey(PcKey key, bool release, uint64_t t, uint64_t repeatId);
    void queueByte(uint8_t byte, uint64_t t, uint64_t repeatId);
    // Queues the bytes of every typematic repeat due at or before t (forDelivery: whose first byte
    // is due, so an E0 lead can go out before a tick that its key byte follows).
    void materializeRepeats(uint64_t t, bool forDelivery);
    void deliverByte(uint8_t byte);
    uint64_t clampTime(uint64_t t);
    // The E0 lead only matters (and is only applied) when prefix bytes reach Kbd_ISR.
    uint64_t leadNs(PcKey k) const {
        return (k.extended && options_.greyKeyE0Artefact) ? static_cast<uint64_t>(options_.prefixLeadUs) * 1000u : 0;
    }

    KeyboardOptions options_;
    KbdIsr isr_;
    BiosKeyboard bios_;
    std::vector<Pending> pending_;  // in (time, seq) order: bytes are only appended
    std::vector<PcKey> held_;       // in press order
    std::optional<PcKey> repeatKey_;
    uint64_t nextRepeat_ = 0;
    uint64_t inflightRepeat_ = 0;   // id of the last repeat queued for the current repeat key
    uint64_t repeatIds_ = 0;
    uint64_t seq_ = 0;
    uint64_t lastTime_ = 0;         // latest event or delivery time seen
    uint64_t lastQueued_ = 0;       // time of the last byte queued (or delivered up to)
    uint64_t lastDelivered_ = 0;
    uint64_t bytesDelivered_ = 0;
    uint64_t repeats_ = 0;
};

}  // namespace tmport::dos
