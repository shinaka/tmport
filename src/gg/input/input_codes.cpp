#include "gg/input/input_codes.h"

namespace tmport::gg {

InputCodes::InputCodes(GgKeyboard& keyboard, GgMouse& mouse, ClickZones& zones, std::function<uint32_t()> ticks)
    : keyboard_(keyboard), mouse_(mouse), zones_(zones), ticks_(std::move(ticks)) {}

// 198d:0106 Input_GetCode
uint16_t InputCodes::getCode() {
    uint16_t c = 0;
    // 198d:010f: the press latch (button held): its zone's press code, | 0x4000 on the first poll
    // after the press (which clears the first-poll flag even when no zone is under it), | 0x8000 later.
    InputRecord& press = mouse_.press();
    if (press.kind != 0) {
        c = hitZones(press);
        if (press.first == 1) {
            press.first = 0;
            if (c) c |= code::kFirstPoll;
        } else if (c) {
            c |= code::kStillHeld;
        }
    }
    // 198d:0141: a pending release replaces it with the release code (the latch is cleared first, so
    // Input_HitZones sees +00h = 0 and answers the release code).
    InputRecord& release = mouse_.release();
    if (release.kind != 0) {
        release.kind = 0;
        c = hitZones(release);
    }
    // 198d:015a (CLI): one ring record; type 1 keeps the code, type 2 hits the zones at the record,
    // type 3 takes the BIOS key if one waits and gives 0 otherwise. Either way it replaces the mouse
    // code: a key record erases a click of the same poll.
    if (const InputRecord* rec = keyboard_.ring().pop()) {
        const uint16_t mouseCode = c;
        if (rec->kind == 2) {
            c = hitZones(*rec);
        } else if (rec->kind == 3) {
            c = keyboard_.biosPeekKey() != 0 ? keyboard_.biosGetKey() : uint16_t{0};
            if (mouseCode != 0 && c != mouseCode) ++erased_;
        }
    }
    // 198d:01a5 (STI): the global keys.
    c = globalKeys(c);
    last_ = c;
    return c;
}

// 198d:01a8: the 12-entry table CS:0275, then the HUD part range.
uint16_t InputCodes::globalKeys(uint16_t c) {
    switch (c) {
        case code::kCtrlS:  // 198d:01c2
            if (hooks_.toggleSound) hooks_.toggleSound();
            return 0;
        case code::kEsc:  // 198d:022b: the code stays 0x1B
            if (esc_) esc_();
            return c;
        case code::kAltF:  // 198d:01ca
        case code::kAltO:  // 198d:01cd
        case code::kAltH:  // 198d:01d6
        case code::kF1:    // 198d:01df
        case code::kF2:
        case code::kF3: {
            uint16_t menuCode = c;
            if (c == code::kAltF) menuCode = code::kF2;
            else if (c == code::kAltO) menuCode = code::kF3;
            else if (c == code::kAltH) menuCode = code::kF1;
            // The Esc hook runs for the menu keys too (198d:01df), before the menu.
            if (esc_) esc_();
            if (!menu_ || menuDisabled != 0) return menuCode;
            menu_(menuCode - code::kMenuBase);
            uint16_t result = 0;
            if (menuRequest == 1) {
                menuRequest = 0;
                result = code::kMenuRequest1;
            }
            if (menuRequest == 2) {
                menuRequest = 0;
                result = code::kMenuRequest2;
            }
            return result;
        }
        case code::kHudTrash:  // 198d:023a
            if (hooks_.hudTrash) hooks_.hudTrash();
            return 0;
        case code::kHudPutBack:
            if (hooks_.hudPutBack) hooks_.hudPutBack();
            return 0;
        case code::kHudScrollLeft:
            if (hooks_.hudScrollLeft) hooks_.hudScrollLeft();
            return 0;
        case code::kHudScrollRight:
            if (hooks_.hudScrollRight) hooks_.hudScrollRight();
            return 0;
        default: break;
    }
    // 198d:0256: 5000..5100, unsigned.
    if (c >= code::kHudPartFirst && c <= code::kHudPartLast) {
        if (hooks_.hudSelectPart) hooks_.hudSelectPart(static_cast<uint16_t>(c - code::kHudPartFirst));
        return 0;
    }
    return c;
}

void InputCodes::flush() {
    // 198d:00bf Input_Flush
    while (!keyboard_.ring().empty()) (void)getCode();
}

uint32_t InputCodes::getHeldKeys(uint16_t highWord) {
    // 198d:02b8 Input_GetHeldKeys
    uint16_t low = keyboard_.heldKeys();
    const uint32_t t = now();
    if (low != 0) {
        keyHeldSeen = 1;
        lastHeldTick_ = t;
        idleReported = 0;
    } else if (t > static_cast<uint32_t>(lastHeldTick_ + kIdleTicks) && idleReported == 0) {
        // 198d:02e9: (DS:1314 + 0x111C) compared with g_ticks, high word first: strictly later.
        low |= held::kIdle;
        idleReported = 1;
    }
    return (static_cast<uint32_t>(highWord) << 16) | low;
}

}  // namespace tmport::gg
