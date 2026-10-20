#pragma once
// The keyboard of Gizmos & Gadgets (ssg.exe 1454 Kbd_ISR and its key handlers, 198d KbdRing_Pop and
// Kbd_PopQueuedKey, 1d3c the BIOS helpers; re/gg/docs/code_runtime.md §6, PORTING_NOTES.md §8):
//
//   host key down/up (SDL scancode) -> dos::PcKey
//     -> dos::ScanKeyboard: scan bytes in time order, plus SYNTHESISED typematic repeats of the last
//        key pressed (DOSBox-X: first repeat 500 ms after the press, then every 33 ms)
//     -> KbdIsr (Kbd_ISR 1454:00e4): held bits, play-mode swallowing, Ctrl+C, E0/E1 swallowed, the
//        key ring record of every make code passed on
//     -> dos::BiosKeyboard (INT 16h key words), unless KbdIsr swallowed the byte.
//
// E0/E1 prefix bytes reach KbdIsr (ScanKeyboardOptions::prefixBytesToHandler, no lead) and are
// swallowed there, so the BIOS never sees them: the grey keys arrive as their keypad twins (grey Up
// = keypad 8 = key word 0x148 with Num Lock off; the BIOS model starts with Num Lock off).
//
// Times are the runtime's virtual nanoseconds; the ring records carry g_ticks, which the runtime
// supplies (setTickSource).

#include <array>
#include <cstdint>
#include <functional>

#include "dos/input/pc_keyboard.h"

namespace tmport::gg {

// g_heldKeys DS:0218 bits (the make/break handlers 1454:0187..01ef) and the idle bit that
// Input_GetHeldKeys 198d:02b8 adds.
namespace held {
inline constexpr uint16_t kLeft = 0x01;
inline constexpr uint16_t kRight = 0x02;
inline constexpr uint16_t kUp = 0x04;
inline constexpr uint16_t kDown = 0x08;
inline constexpr uint16_t kCtrl = 0x10;
inline constexpr uint16_t kSpace = 0x20;
inline constexpr uint16_t kIdle = 0x40;  // not a key: 0x111C ticks without a held key
}  // namespace held

// One 16-byte input record. The key ring (DS:8556) and the two mouse latches (press DS:0a10,
// release DS:0a20) share the layout; Input_HitZones 198d:0008 reads +00h and +0Ah/+0Ch of either.
struct InputRecord {
    uint16_t kind = 0;   // +00h: ring 3 = one BIOS key may be taken (1 and 2 are never written);
                         //       press latch 1 = left button held; release latch 2 = pending
    uint32_t tick = 0;   // +06h: g_ticks when written (ring records, the release latch)
    int16_t x = 0;       // +0Ah: position (mouse latches: pointer + hot spot)
    int16_t y = 0;       // +0Ch
    uint16_t first = 0;  // +0Eh: press latch only: g_mouseNewPress DS:0a1e (the first poll is due)
    bool operator==(const InputRecord&) const = default;
};

// The key ring g_kbdRing DS:8556..8655: 16 records, write index DS:1310, read index DS:1312.
class KeyRing {
public:
    static constexpr int kSize = 16;

    // Kbd_ISR 1454:0148..0175: record[write] +00h = 3, +06h = g_ticks (the other ten bytes keep what
    // they held), write = (write + 1) & 15. The full test compares a handler-table address with the
    // read index and never fires, so the ring overruns silently: 16 unread records look empty.
    void push(uint32_t ticks);
    // KbdRing_Pop 198d:0084: nullptr when write == read, else the record at read (read advances).
    const InputRecord* pop();

    bool empty() const { return write_ == read_; }
    int pending() const { return (write_ - read_) & (kSize - 1); }
    uint16_t writeIndex() const { return write_; }
    uint16_t readIndex() const { return read_; }
    const InputRecord& record(int i) const { return records_[static_cast<size_t>(i & (kSize - 1))]; }
    uint64_t overruns() const { return overruns_; }  // pushes that made the ring look empty (diagnostics)

private:
    std::array<InputRecord, kSize> records_{};
    uint16_t write_ = 0, read_ = 0;
    uint64_t overruns_ = 0;
};

// Kbd_ISR 1454:00e4: the game's part of INT 09h for one scan byte.
struct KbdIsr {
    uint16_t heldKeys = 0;  // g_heldKeys DS:0218 (held:: bits)
    uint16_t ctrlHeld = 0;  // g_ctrlHeld DS:020c
    uint16_t playMode = 0;  // g_kbdPlayMode DS:0096: 1 in the warehouse rooms (State_05_EnterRoom
                            // 12e0:0972); the hub, sign-in and workshop clear it; Dlg_RunModal and
                            // the menu drop-down clear it and restore it
    KeyRing ring;
    uint64_t swallowed = 0;  // bytes not passed to the BIOS (diagnostics)

    // The scan table DS:0224 (REPNE SCASB over 8 bytes; a match on its trailing 0 is no match).
    static constexpr std::array<uint8_t, 8> kScanTable = {0x4B, 0x4D, 0x48, 0x50, 0x39, 0x1D, 0x2E, 0x00};

    // Returns true when the byte is passed on to the BIOS (and, for a make code, a ring record was
    // written stamped with `ticks`), false when it is acknowledged and swallowed.
    bool onScanByte(uint8_t byte, uint32_t ticks);

    // The make / break handlers (tables DS:022c / DS:023a), by scan table index: their result
    // (< 0 swallow, 0 swallow in play mode, > 0 pass on). Index 6 (C) is the same function for both.
    int makeHandler(int index);
    int breakHandler(int index);
};

struct GgKeyboardOptions {
    uint32_t typematicDelayMs = 500;  // DOSBox-X keyboard repeat delay (as measured for TM DOS)
    uint32_t typematicRateMs = 33;    // DOSBox-X repeat period
};

// The PC keyboard (dos::ScanKeyboard) with Kbd_ISR as the game's handler, and the BIOS and ring
// readers of 198d / 1d3c.
class GgKeyboard final : public dos::ScanKeyboard {
public:
    explicit GgKeyboard(GgKeyboardOptions options = {});

    const GgKeyboardOptions& options() const { return options_; }
    void setOptions(const GgKeyboardOptions& o);
    // g_ticks for the ring records (the runtime's clock); nullptr reads 0.
    void setTickSource(std::function<uint32_t()> ticks) { ticks_ = std::move(ticks); }

    KbdIsr& isr() { return isr_; }
    const KbdIsr& isr() const { return isr_; }
    KeyRing& ring() { return isr_.ring; }
    uint16_t heldKeys() const { return isr_.heldKeys; }
    uint16_t playMode() const { return isr_.playMode; }
    void setPlayMode(uint16_t mode) { isr_.playMode = mode; }

    // 1d3c:0004 Kbd_BiosGetKey: INT 16h AH=00h; ASCII when it is not 0, else 0x100 | scan. 0 when the
    // buffer is empty (the game only calls it after a peek found a key).
    uint16_t biosGetKey();
    // 1d3c:0015 Kbd_BiosPeekKey: INT 16h AH=01h; 0 when no key waits, else the raw word.
    uint16_t biosPeekKey();
    // 198d:00d4 Kbd_PopQueuedKey: pops one ring record; when it is of type 3 and the BIOS has a key,
    // takes and returns its key word (Kbd_BiosGetKey), else 0. An empty ring dereferences the NULL
    // record (0000:0000, never 3 in practice): 0.
    uint16_t popQueuedKey();
    // The LZH input refill 1b48:002e after each 4000-byte read: Kbd_PopQueuedKey until it returns 0
    // (keys typed while a compressed resource is decoded are lost). Returns the keys thrown away.
    int drainQueuedKeys();

private:
    static dos::ScanKeyboardOptions scanOptions(const GgKeyboardOptions& o);
    bool handleScanByte(uint8_t byte) override;

    GgKeyboardOptions options_;
    KbdIsr isr_;
    std::function<uint32_t()> ticks_;
};

}  // namespace tmport::gg
