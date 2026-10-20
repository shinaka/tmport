#include "gg/input/keyboard.h"

namespace tmport::gg {

// ---------------------------------------------------------------------------------------- KeyRing

void KeyRing::push(uint32_t ticks) {
    // 1454:0148: the record at the write index gets type 3 and the tick.
    InputRecord& r = records_[write_];
    r.kind = 3;
    r.tick = ticks;
    // 1454:0168: the full test (CMP BX, [read]) compares the handler-table address (0x22C..0x248)
    // with the read index, so it never refuses the record.
    write_ = static_cast<uint16_t>((write_ + 1) & (kSize - 1));
    if (write_ == read_) ++overruns_;
}

const InputRecord* KeyRing::pop() {
    // 198d:0084 KbdRing_Pop
    if (write_ == read_) return nullptr;
    const InputRecord* r = &records_[read_];
    read_ = static_cast<uint16_t>((read_ + 1) & (kSize - 1));
    return r;
}

// ----------------------------------------------------------------------------------------- KbdIsr

// The make handlers 1454:0187 (Left), 0197 (Right), 01cf (Up), 01df (Down), 01a7 (Space),
// 01b7 (Ctrl), 01ef (C).
int KbdIsr::makeHandler(int index) {
    switch (index) {
        case 0: heldKeys |= held::kLeft; return 0;
        case 1: heldKeys |= held::kRight; return 0;
        case 2: heldKeys |= held::kUp; return 0;
        case 3: heldKeys |= held::kDown; return 0;
        case 4: heldKeys |= held::kSpace; return 0;
        case 5:
            heldKeys |= held::kCtrl;
            ctrlHeld = 1;
            return 1;  // Ctrl always reaches the BIOS (Ctrl+S, Ctrl+R)
        case 6: return ctrlHeld == 1 ? -1 : 1;  // 1454:01ef Kbd_KeyC: no Ctrl+C
        default: return 1;
    }
}

// The break handlers 1454:018f, 019f, 01d7, 01e7, 01af, 01c3, 01ef.
int KbdIsr::breakHandler(int index) {
    switch (index) {
        case 0: heldKeys &= static_cast<uint16_t>(~held::kLeft); return 0;
        case 1: heldKeys &= static_cast<uint16_t>(~held::kRight); return 0;
        case 2: heldKeys &= static_cast<uint16_t>(~held::kUp); return 0;
        case 3: heldKeys &= static_cast<uint16_t>(~held::kDown); return 0;
        case 4: heldKeys &= static_cast<uint16_t>(~held::kSpace); return 0;
        case 5:
            heldKeys &= static_cast<uint16_t>(~held::kCtrl);
            ctrlHeld = 0;
            return 1;
        case 6: return ctrlHeld == 1 ? -1 : 1;
        default: return 1;
    }
}

// 1454:00e4 Kbd_ISR
bool KbdIsr::onScanByte(uint8_t byte, uint32_t ticks) {
    // 1454:00f7: the E0 / E1 prefixes are acknowledged and swallowed.
    if (byte == 0xE0 || byte == 0xE1) {
        ++swallowed;
        return false;
    }
    const bool make = (byte & 0x80) == 0;
    const uint8_t scan = static_cast<uint8_t>(byte & 0x7F);
    // 1454:0110: REPNE SCASB over the 8 bytes of DS:0224; CX = 0 afterwards (no match, or the match
    // on the trailing 0) means not found.
    int index = -1;
    for (int i = 0; i < 7; ++i)
        if (kScanTable[static_cast<size_t>(i)] == scan) {
            index = i;
            break;
        }
    if (index >= 0) {
        const int r = make ? makeHandler(index) : breakHandler(index);
        if (r < 0 || (r == 0 && playMode == 1)) {
            ++swallowed;
            return false;  // 1454:0134: acknowledge and swallow
        }
    }
    // 1454:0144: pass on; a make code first writes a ring record, then the old INT 9 (the BIOS) runs.
    if (make) ring.push(ticks);
    return true;
}

// ------------------------------------------------------------------------------------- GgKeyboard

GgKeyboard::GgKeyboard(GgKeyboardOptions options) : ScanKeyboard(scanOptions(options)), options_(options) {}

void GgKeyboard::setOptions(const GgKeyboardOptions& o) {
    options_ = o;
    setScanOptions(scanOptions(o));
}

dos::ScanKeyboardOptions GgKeyboard::scanOptions(const GgKeyboardOptions& o) {
    dos::ScanKeyboardOptions s;
    s.typematicDelayMs = o.typematicDelayMs;
    s.typematicRateMs = o.typematicRateMs;
    // Kbd_ISR sees (and swallows) the prefix bytes: the BIOS must not get them. They go together
    // with their key byte (no lead time).
    s.prefixBytesToHandler = true;
    s.prefixLeadUs = 0;
    return s;
}

bool GgKeyboard::handleScanByte(uint8_t byte) { return isr_.onScanByte(byte, ticks_ ? ticks_() : 0); }

uint16_t GgKeyboard::biosGetKey() {
    // 1d3c:0004: AL != 0 -> AL (AH cleared); else 0x100 | AH.
    if (bios().size() == 0) return 0;
    const uint16_t w = bios().read();
    const uint8_t ascii = static_cast<uint8_t>(w & 0xFF);
    return ascii != 0 ? ascii : static_cast<uint16_t>(0x100 | (w >> 8));
}

uint16_t GgKeyboard::biosPeekKey() {
    // 1d3c:0015: ZF (no key) -> 0; a waiting word of 0000h (Ctrl+Break) would give 0x100, but the
    // BIOS model reports no such word.
    return bios().peek();
}

uint16_t GgKeyboard::popQueuedKey() {
    // 198d:00d4 Kbd_PopQueuedKey (under CLI).
    const InputRecord* r = isr_.ring.pop();
    if (!r || r->kind != 3) return 0;
    if (biosPeekKey() == 0) return 0;
    return biosGetKey();
}

int GgKeyboard::drainQueuedKeys() {
    int n = 0;
    while (popQueuedKey() != 0) ++n;
    return n;
}

}  // namespace tmport::gg
