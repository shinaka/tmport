#include "dos/input/bios_keyboard.h"

#include <iterator>

namespace tmport::dos {

namespace {

// The standard IBM PC (enhanced keyboard) INT 09h table of set-1 scan codes 0x00..0x58:
// key word without modifiers, with Shift, with Ctrl, with Alt; 0 = no key. Words whose low byte is
// 0xF0 or whose scan part exceeds 0x84 are "enhanced" and invisible to INT 16h AH=00h/01h.
struct ScanEntry {
    uint16_t normal, shift, ctrl, alt;
};
constexpr ScanEntry kScanTable[0x59] = {
    {0, 0, 0, 0},                           // 00
    {0x011B, 0x011B, 0x011B, 0x01F0},       // 01 Esc
    {0x0231, 0x0221, 0, 0x7800},            // 02 1 !
    {0x0332, 0x0340, 0x0300, 0x7900},       // 03 2 @
    {0x0433, 0x0423, 0, 0x7A00},            // 04 3 #
    {0x0534, 0x0524, 0, 0x7B00},            // 05 4 $
    {0x0635, 0x0625, 0, 0x7C00},            // 06 5 %
    {0x0736, 0x075E, 0x071E, 0x7D00},       // 07 6 ^
    {0x0837, 0x0826, 0, 0x7E00},            // 08 7 &
    {0x0938, 0x092A, 0, 0x7F00},            // 09 8 *
    {0x0A39, 0x0A28, 0, 0x8000},            // 0A 9 (
    {0x0B30, 0x0B29, 0, 0x8100},            // 0B 0 )
    {0x0C2D, 0x0C5F, 0x0C1F, 0x8200},       // 0C - _
    {0x0D3D, 0x0D2B, 0, 0x8300},            // 0D = +
    {0x0E08, 0x0E08, 0x0E7F, 0x0EF0},       // 0E Backspace
    {0x0F09, 0x0F00, 0x9400, 0xA500},       // 0F Tab
    {0x1071, 0x1051, 0x1011, 0x1000},       // 10 Q
    {0x1177, 0x1157, 0x1117, 0x1100},       // 11 W
    {0x1265, 0x1245, 0x1205, 0x1200},       // 12 E
    {0x1372, 0x1352, 0x1312, 0x1300},       // 13 R
    {0x1474, 0x1454, 0x1414, 0x1400},       // 14 T
    {0x1579, 0x1559, 0x1519, 0x1500},       // 15 Y
    {0x1675, 0x1655, 0x1615, 0x1600},       // 16 U
    {0x1769, 0x1749, 0x1709, 0x1700},       // 17 I
    {0x186F, 0x184F, 0x180F, 0x1800},       // 18 O
    {0x1970, 0x1950, 0x1910, 0x1900},       // 19 P
    {0x1A5B, 0x1A7B, 0x1A1B, 0x1AF0},       // 1A [ {
    {0x1B5D, 0x1B7D, 0x1B1D, 0x1BF0},       // 1B ] }
    {0x1C0D, 0x1C0D, 0x1C0A, 0x1CF0},       // 1C Enter
    {0, 0, 0, 0},                           // 1D Ctrl
    {0x1E61, 0x1E41, 0x1E01, 0x1E00},       // 1E A
    {0x1F73, 0x1F53, 0x1F13, 0x1F00},       // 1F S
    {0x2064, 0x2044, 0x2004, 0x2000},       // 20 D
    {0x2166, 0x2146, 0x2106, 0x2100},       // 21 F
    {0x2267, 0x2247, 0x2207, 0x2200},       // 22 G
    {0x2368, 0x2348, 0x2308, 0x2300},       // 23 H
    {0x246A, 0x244A, 0x240A, 0x2400},       // 24 J
    {0x256B, 0x254B, 0x250B, 0x2500},       // 25 K
    {0x266C, 0x264C, 0x260C, 0x2600},       // 26 L
    {0x273B, 0x273A, 0, 0x27F0},            // 27 ; :
    {0x2827, 0x2822, 0, 0x28F0},            // 28 ' "
    {0x2960, 0x297E, 0, 0x29F0},            // 29 ` ~
    {0, 0, 0, 0},                           // 2A Left Shift
    {0x2B5C, 0x2B7C, 0x2B1C, 0x2BF0},       // 2B \ |
    {0x2C7A, 0x2C5A, 0x2C1A, 0x2C00},       // 2C Z
    {0x2D78, 0x2D58, 0x2D18, 0x2D00},       // 2D X
    {0x2E63, 0x2E43, 0x2E03, 0x2E00},       // 2E C
    {0x2F76, 0x2F56, 0x2F16, 0x2F00},       // 2F V
    {0x3062, 0x3042, 0x3002, 0x3000},       // 30 B
    {0x316E, 0x314E, 0x310E, 0x3100},       // 31 N
    {0x326D, 0x324D, 0x320D, 0x3200},       // 32 M
    {0x332C, 0x333C, 0, 0x33F0},            // 33 , <
    {0x342E, 0x343E, 0, 0x34F0},            // 34 . >
    {0x352F, 0x353F, 0, 0x35F0},            // 35 / ?
    {0, 0, 0, 0},                           // 36 Right Shift
    {0x372A, 0x372A, 0x9600, 0x37F0},       // 37 keypad *
    {0, 0, 0, 0},                           // 38 Alt
    {0x3920, 0x3920, 0x3920, 0x3920},       // 39 Space
    {0, 0, 0, 0},                           // 3A Caps Lock
    {0x3B00, 0x5400, 0x5E00, 0x6800},       // 3B F1
    {0x3C00, 0x5500, 0x5F00, 0x6900},       // 3C F2
    {0x3D00, 0x5600, 0x6000, 0x6A00},       // 3D F3
    {0x3E00, 0x5700, 0x6100, 0x6B00},       // 3E F4
    {0x3F00, 0x5800, 0x6200, 0x6C00},       // 3F F5
    {0x4000, 0x5900, 0x6300, 0x6D00},       // 40 F6
    {0x4100, 0x5A00, 0x6400, 0x6E00},       // 41 F7
    {0x4200, 0x5B00, 0x6500, 0x6F00},       // 42 F8
    {0x4300, 0x5C00, 0x6600, 0x7000},       // 43 F9
    {0x4400, 0x5D00, 0x6700, 0x7100},       // 44 F10
    {0, 0, 0, 0},                           // 45 Num Lock
    {0, 0, 0, 0},                           // 46 Scroll Lock
    {0x4700, 0x4737, 0x7700, 0},            // 47 keypad 7 Home
    {0x4800, 0x4838, 0x8D00, 0},            // 48 keypad 8 Up
    {0x4900, 0x4939, 0x8400, 0},            // 49 keypad 9 PgUp
    {0x4A2D, 0x4A2D, 0x8E00, 0x4AF0},       // 4A keypad -
    {0x4B00, 0x4B34, 0x7300, 0},            // 4B keypad 4 Left
    {0x4CF0, 0x4C35, 0x8F00, 0},            // 4C keypad 5
    {0x4D00, 0x4D36, 0x7400, 0},            // 4D keypad 6 Right
    {0x4E2B, 0x4E2B, 0x9000, 0x4EF0},       // 4E keypad +
    {0x4F00, 0x4F31, 0x7500, 0},            // 4F keypad 1 End
    {0x5000, 0x5032, 0x9100, 0},            // 50 keypad 2 Down
    {0x5100, 0x5133, 0x7600, 0},            // 51 keypad 3 PgDn
    {0x5200, 0x5230, 0x9200, 0},            // 52 keypad 0 Ins
    {0x5300, 0x532E, 0x9300, 0},            // 53 keypad . Del
    {0, 0, 0, 0},                           // 54 SysRq
    {0, 0, 0, 0},                           // 55
    {0x565C, 0x567C, 0, 0},                 // 56 102nd key \ |
    {0x8500, 0x8700, 0x8900, 0x8B00},       // 57 F11
    {0x8600, 0x8800, 0x8A00, 0x8C00},       // 58 F12
};

bool isKeypad(uint8_t scan) { return scan >= 0x47 && scan <= 0x53; }

}  // namespace

bool BiosKeyboard::translateForAh00(uint16_t& word) {
    const uint8_t hi = static_cast<uint8_t>(word >> 8), lo = static_cast<uint8_t>(word);
    if (hi == 0xE0) {  // grey Enter / grey slash: not enhanced, returned with their main-block scan
        word = static_cast<uint16_t>(lo | ((lo == 0x0A || lo == 0x0D) ? 0x1C00 : 0x3500));
        return true;
    }
    if (hi > 0x84 || (lo == 0xF0 && hi != 0)) return false;
    if (hi != 0 && lo == 0xE0) word &= 0xFF00;
    return true;
}

uint16_t BiosKeyboard::storedWordFor(uint8_t scan, bool extended) const {
    if (scan >= std::size(kScanTable)) return 0;
    const ScanEntry& e = kScanTable[scan];
    const bool shift = (flags_ & (kLeftShift | kRightShift)) != 0;
    if (isKeypad(scan)) {
        if (extended) {  // the grey block: Num Lock and Shift do not matter
            if (flags_ & kAlt) return static_cast<uint16_t>(e.normal + 0x5000);
            if (flags_ & kCtrl) return static_cast<uint16_t>((e.ctrl & 0xFF00) | 0xE0);
            return static_cast<uint16_t>((e.normal & 0xFF00) | 0xE0);
        }
        if (flags_ & kAlt) return 0;  // Alt + keypad digits compose a character code: not modelled
        if (flags_ & kCtrl) return e.ctrl;
        return (shift != ((flags_ & kNumLock) != 0)) ? e.shift : e.normal;
    }
    uint16_t w;
    if (flags_ & kAlt) w = e.alt;
    else if (flags_ & kCtrl) w = e.ctrl;
    else if (shift) w = e.shift;
    else w = e.normal;
    if (flags_ & kCapsLock) {  // Caps Lock inverts Shift for letters only
        const uint8_t a = static_cast<uint8_t>(w);
        if (shift && a > 0x40 && a < 0x5B) w = e.normal;
        else if (!shift && a > 0x60 && a < 0x7B) w = e.shift;
    }
    if (extended && scan == 0x1C) w = (flags_ & kAlt) ? 0xA600 : static_cast<uint16_t>((w & 0xFF) | 0xE000);
    if (extended && scan == 0x35) w = (flags_ & kAlt) ? 0xA400 : (flags_ & kCtrl) ? 0x9500 : 0xE02F;
    return w;
}

// INT 09h.
void BiosKeyboard::onScanByte(uint8_t byte) {
    if (byte == 0xE0) {
        e0_ = true;
        return;
    }
    if (byte == 0xE1) {  // Pause: E1 1D 45 E1 9D C5, two bytes after each E1, not a key
        e1Left_ = 2;
        return;
    }
    if (e1Left_ > 0) {
        --e1Left_;
        return;
    }
    const bool extended = e0_;
    e0_ = false;
    const bool release = (byte & 0x80) != 0;
    const uint8_t scan = byte & 0x7F;
    auto setFlag = [&](uint8_t bit, bool on) { flags_ = on ? (flags_ | bit) : static_cast<uint8_t>(flags_ & ~bit); };
    switch (scan) {
        case 0x2A:
            if (!extended) lShift_ = !release;  // E0 2A is a "fake shift": ignored
            setFlag(kLeftShift, lShift_);
            return;
        case 0x36:
            if (!extended) rShift_ = !release;
            setFlag(kRightShift, rShift_);
            return;
        case 0x1D:
            (extended ? rCtrl_ : lCtrl_) = !release;
            setFlag(kCtrl, lCtrl_ || rCtrl_);
            return;
        case 0x38:
            (extended ? rAlt_ : lAlt_) = !release;
            setFlag(kAlt, lAlt_ || rAlt_);
            return;
        case 0x3A:
            if (!release && !capsHeld_) flags_ ^= kCapsLock;
            capsHeld_ = !release;
            return;
        case 0x45:
            if (!release && !numHeld_) flags_ ^= kNumLock;
            numHeld_ = !release;
            return;
        case 0x46:
            if (!release && !scrollHeld_) flags_ ^= kScrollLock;
            scrollHeld_ = !release;
            return;
        case 0x52:
            if (release) {
                insertHeld_ = false;
                return;
            }
            if (!insertHeld_ && (extended || (((flags_ & (kLeftShift | kRightShift)) != 0) == ((flags_ & kNumLock) != 0))))
                flags_ ^= kInsert;
            insertHeld_ = true;
            break;
        default:
            if (release) return;
            break;
    }
    if (const uint16_t w = storedWordFor(scan, extended)) push(w);
}

bool BiosKeyboard::push(uint16_t storedWord) {
    if (full()) {
        ++dropped_;  // the BIOS beeps and drops the key
        return false;
    }
    ring_[static_cast<size_t>(tail_)] = storedWord;
    tail_ = (tail_ + 1) % kBufferWords;
    return true;
}

// INT 16h AH=01h.
uint16_t BiosKeyboard::peek() {
    while (head_ != tail_) {
        uint16_t w = ring_[static_cast<size_t>(head_)];
        if (translateForAh00(w)) return w;
        head_ = (head_ + 1) % kBufferWords;  // an enhanced key is removed and ignored
    }
    return 0;
}

// INT 16h AH=00h.
uint16_t BiosKeyboard::read() {
    const uint16_t w = peek();
    if (w != 0) head_ = (head_ + 1) % kBufferWords;
    return w;
}

}  // namespace tmport::dos
