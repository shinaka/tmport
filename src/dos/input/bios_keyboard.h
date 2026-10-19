#pragma once
// The PC BIOS keyboard the game chains to (not a module of SST.EXE; the machine it ran on), modelled
// on the BIOS of DOSBox-X, the reference machine of every capture:
//  * INT 09h: set-1 scan bytes (with the E0 prefix of the grey keys) -> 16-bit key words
//    scan << 8 | ASCII, by the standard IBM table and the shift states of 0040:0017 (Shift, Ctrl,
//    Alt, Caps Lock, Num Lock, Scroll Lock, Insert);
//  * the 16-word ring buffer at 0040:001E: 15 keys, a key arriving at a full buffer is dropped;
//  * INT 16h AH=01h / AH=00h as Turbo C's bioskey(1) / bioskey(0) call them: "enhanced" words
//    (scan part > 0x84, or low byte 0xF0: F11/F12, Alt+Esc, Ctrl+arrows on the grey block, keypad 5)
//    stay in the buffer until a read reaches them and are then dropped; grey-key words stored with
//    low byte 0xE0 are returned with ASCII 0 (grey Up 0x48E0 -> 0x4800), grey Enter and grey /
//    (0xE00D / 0xE02F) as 0x1C0D / 0x352F.
// The game reads it through Input_Poll (4a85:000E: one key per game tick) and empties it in
// Kbd_Remove (43a0:0043). Num Lock (scan 0x45) and Scroll Lock (0x46) never reach the BIOS because
// Kbd_ISR swallows them, and Game_Init clears Num Lock and Caps Lock at start (1460:0003 step 4), so
// the keypad always gives cursor codes unless Shift is held.

#include <array>
#include <cstdint>

namespace tmport::dos {

class BiosKeyboard {
public:
    static constexpr int kBufferWords = 16;  // 0040:001E..003D
    static constexpr int kCapacity = kBufferWords - 1;

    // 0040:0017 bits.
    static constexpr uint8_t kRightShift = 0x01, kLeftShift = 0x02, kCtrl = 0x04, kAlt = 0x08;
    static constexpr uint8_t kScrollLock = 0x10, kNumLock = 0x20, kCapsLock = 0x40, kInsert = 0x80;

    // INT 09h for one scan byte the game did not swallow.
    void onScanByte(uint8_t byte);

    // INT 16h AH=01h (bioskey(1)): the next key word without removing it, 0 if none. Enhanced words
    // at the head are removed first.
    uint16_t peek();
    // INT 16h AH=00h (bioskey(0)): removes and returns the next key word. The real call waits for a
    // key; the game only calls it after bioskey(1) reported one, so an empty buffer returns 0 here.
    uint16_t read();
    // Words in the buffer (including enhanced ones not yet skipped by a read).
    int size() const { return (tail_ - head_ + kBufferWords) % kBufferWords; }
    bool full() const { return size() == kCapacity; }
    // Adds a stored word (as INT 09h does); false and nothing stored when the buffer is full.
    bool push(uint16_t storedWord);
    // Kbd_Remove (43a0:0043): while (bioskey(1)) bioskey(0).
    void flush() { head_ = tail_; }
    // Keys dropped because the buffer was full.
    uint32_t dropped() const { return dropped_; }

    uint8_t shiftFlags() const { return flags_; }
    // Game_Init writes 0040:0017 back with Num Lock and Caps Lock cleared (1460:0003 step 4).
    void setShiftFlags(uint8_t f) { flags_ = f; }

private:
    // DOSBox-X int16.cpp IsEnhancedKey + the AH=00h/01h conversion: false = drop the word.
    static bool translateForAh00(uint16_t& word);
    uint16_t storedWordFor(uint8_t scan, bool extended) const;

    std::array<uint16_t, kBufferWords> ring_{};
    int head_ = 0;
    int tail_ = 0;
    uint8_t flags_ = 0;    // 0040:0017
    bool e0_ = false;      // last byte was the E0 prefix (0040:0096 bit 1)
    int e1Left_ = 0;       // bytes of a Pause sequence still to ignore
    bool lShift_ = false, rShift_ = false, lCtrl_ = false, rCtrl_ = false, lAlt_ = false, rAlt_ = false;
    bool capsHeld_ = false, numHeld_ = false, scrollHeld_ = false, insertHeld_ = false;
    uint32_t dropped_ = 0;
};

}  // namespace tmport::dos
