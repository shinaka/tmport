#include "dos/input/keyboard.h"

namespace tmport::dos {

using namespace keybits;

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

PcKeyboard::PcKeyboard(KeyboardOptions options) : ScanKeyboard(scanOptions(options)), options_(options) {
    isr_.tandy = options_.tandy;
}

void PcKeyboard::setOptions(const KeyboardOptions& o) {
    options_ = o;
    isr_.tandy = o.tandy;
    setScanOptions(scanOptions(o));
}

ScanKeyboardOptions PcKeyboard::scanOptions(const KeyboardOptions& o) {
    ScanKeyboardOptions s;
    s.typematicDelayMs = o.typematicDelayMs;
    s.typematicRateMs = o.typematicRateMs;
    s.prefixBytesToHandler = o.greyKeyE0Artefact;
    s.prefixLeadUs = o.prefixLeadUs;
    return s;
}

}  // namespace tmport::dos
