#pragma once
// A PC/AT keyboard in front of a DOS game's INT 09h handler and the BIOS (both DOS games; TM DOS:
// dos/input/keyboard.h, G&G: its own Kbd_ISR 1454:00e4 as the handler):
//
//  host key down/up (SDL scancode)
//    -> PcKey (set-1 scan code, E0 prefix for the grey keys)
//    -> ScanKeyboard: scan bytes in time order, plus SYNTHESISED typematic repeats of the last key
//       pressed (DOSBox-X: first repeat 500 ms after the press, then every 33 ms; host key repeats
//       are ignored)
//    -> the game's handler (ScanByteHandler, or a subclass's handleScanByte): sees each byte first
//       and says whether it reaches the BIOS
//    -> BiosKeyboard (dos/input/bios_keyboard.h: the key words of INT 16h).
//
// Bytes are delivered with deliverUntil(t) by the runtime (before each game tick in TM DOS), so the
// game sees every key event that happened before t, in order. Times are virtual nanoseconds.
// E0/E1 prefix bytes go to the BIOS; they reach the handler only with prefixBytesToHandler (TM
// DOS's grey-key artefact), and then go prefixLeadUs before their key byte.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "dos/input/bios_keyboard.h"

namespace tmport::dos {

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

// The game's part of INT 09h for one scan byte: false = swallowed (the BIOS does not see it).
class ScanByteHandler {
public:
    virtual ~ScanByteHandler() = default;
    virtual bool onScanByte(uint8_t byte) = 0;
};

struct ScanKeyboardOptions {
    uint32_t typematicDelayMs = 500;    // DOSBox-X keyboard repeat delay
    uint32_t typematicRateMs = 33;      // DOSBox-X repeat period
    bool prefixBytesToHandler = false;  // E0/E1 bytes also go to the handler (else only to the BIOS)
    uint32_t prefixLeadUs = 1000;       // with prefixBytesToHandler: an E0 byte goes this long before its key byte
};

class ScanKeyboard {
public:
    explicit ScanKeyboard(ScanKeyboardOptions options = {});
    virtual ~ScanKeyboard() = default;

    const ScanKeyboardOptions& scanOptions() const { return options_; }
    void setScanOptions(const ScanKeyboardOptions& o) { options_ = o; }
    // The game's INT 09h handler (not owned; nullptr: every byte goes to the BIOS). A subclass may
    // override handleScanByte instead.
    void setHandler(ScanByteHandler* handler) { handler_ = handler; }

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
    // the game's handler and the BIOS, in time order.
    void deliverUntil(uint64_t t);
    // Time of the next byte not yet delivered (a typematic repeat or a queued event), if any.
    std::optional<uint64_t> nextDue() const;

    BiosKeyboard& bios() { return bios_; }
    const BiosKeyboard& bios() const { return bios_; }

    // Diagnostics for tests.
    uint64_t bytesDelivered() const { return bytesDelivered_; }
    uint64_t repeatsGenerated() const { return repeats_; }

protected:
    // The game's handler for one byte (prefix bytes only with prefixBytesToHandler).
    virtual bool handleScanByte(uint8_t byte);

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
    // The E0 lead only matters (and is only applied) when prefix bytes reach the handler.
    uint64_t leadNs(PcKey k) const {
        return (k.extended && options_.prefixBytesToHandler) ? static_cast<uint64_t>(options_.prefixLeadUs) * 1000u : 0;
    }

    ScanKeyboardOptions options_;
    ScanByteHandler* handler_ = nullptr;
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
