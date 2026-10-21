#pragma once
// The DOS edition's keyboard: the game's INT 09h handler Kbd_ISR (43a0:0071, kbd.asm) and the PC
// keyboard in front of it (re/dos/docs/code_runtime.md §5, PORTING_NOTES.md §4, §6).
//
//  host key down/up (SDL scancode)
//    -> PcKey (set-1 scan code, E0 prefix for the grey keys)
//    -> PcKeyboard (the engine's ScanKeyboard, mischief/dos/input/pc_keyboard.h): scan bytes in time
//       order, plus SYNTHESISED typematic repeats of the last key pressed (DOSBox-X: first repeat
//       500 ms after the press, then every 33 ms; host key repeats are ignored)
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

#include "mischief/dos/input/pc_keyboard.h"

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

// The PC keyboard (mischief/dos/input/pc_keyboard.h) with Kbd_ISR as the game's handler.
class PcKeyboard final : public mischief::dos::ScanKeyboard {
public:
    explicit PcKeyboard(KeyboardOptions options = {});

    const KeyboardOptions& options() const { return options_; }
    void setOptions(const KeyboardOptions& o);

    KbdIsr& isr() { return isr_; }
    const KbdIsr& isr() const { return isr_; }

private:
    static mischief::dos::ScanKeyboardOptions scanOptions(const KeyboardOptions& o);
    bool handleScanByte(uint8_t byte) override { return isr_.onScanByte(byte); }

    KeyboardOptions options_;
    KbdIsr isr_;
};

}  // namespace tmport::dos
