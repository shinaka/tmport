#pragma once
// Input codes of Gizmos & Gadgets (ssg.exe 198d: Input_GetCode and its helpers; re/gg/docs/
// code_runtime.md §6.5, §9, ARCHITECTURE.md §4.6).
//
// Input_GetCode 198d:0106 turns the mouse latches into zone codes, then pops one key-ring record,
// which REPLACES the mouse code by the waiting BIOS key or by 0 (so a Shift make or a repeat whose
// key was already taken erases a click of the same poll), then acts on the 12 global keys:
//
//   0x0013 Ctrl+S            the sound toggle hook (Snd_ToggleOption 1cdd:05c8)       -> 0
//   0x001B Esc               the Esc hook g_escHandler DS:8656, if set                -> 0x1B
//   0x0123 Alt+H, 0x013B F1  menu 0 (Help)    \  code = 0x13B + index; the Esc hook;
//   0x0121 Alt+F, 0x013C F2  menu 1 (File)     > then, if the menu hook g_menuHandler DS:865a is
//   0x0118 Alt+O, 0x013D F3  menu 2 (Options) /  set and g_menuDisabled DS:054e is 0, the hook
//                                                (the drop-down) with the index, and the code
//                                                becomes 0, or 0x17A2 / 0x17A3 when a menu command
//                                                set g_menuRequest DS:130e to 1 / 2
//   6000..6003               the HUD hooks (trash, put back, scroll left, scroll right) -> 0
//   5000..5100 (unsigned)    the HUD part selection hook with code - 5000              -> 0
//
// The codes are matched exactly (a press code with 0x4000 does not match). The hooks are the
// game's (installed by the sound, HUD, menu and state modules); an unset hook does nothing.
//
// This is the pure fetch: it does no host work and lets no time pass. The game calls it through
// GgRuntime::getInputCode(), which pumps the host first (an input poll is a yield point).

#include <cstdint>
#include <functional>

#include "gg/input/keyboard.h"
#include "gg/input/mouse.h"
#include "gg/input/zones.h"

namespace tmport::gg {

// Input codes the engine needs by value (keys and code blocks of code_runtime.md §9).
namespace code {
inline constexpr uint16_t kCtrlS = 0x0013;
inline constexpr uint16_t kEsc = 0x001B;
inline constexpr uint16_t kEnter = 0x000D;
inline constexpr uint16_t kAltO = 0x0118;
inline constexpr uint16_t kAltF = 0x0121;
inline constexpr uint16_t kAltH = 0x0123;
inline constexpr uint16_t kF1 = 0x013B;
inline constexpr uint16_t kF2 = 0x013C;
inline constexpr uint16_t kF3 = 0x013D;
inline constexpr uint16_t kMenuBase = 0x013B;    // menu index i -> 0x13B + i (also the title zones)
inline constexpr uint16_t kMenuRequest1 = 0x17A2;  // g_menuRequest == 1 after the menu hook
inline constexpr uint16_t kMenuRequest2 = 0x17A3;  // g_menuRequest == 2
inline constexpr uint16_t kHudTrash = 6000;      // 6000..6003: Hud_TrashCursorPart 1a04:0a82,
inline constexpr uint16_t kHudPutBack = 6001;    //   Hud_PutBackCursorPart 0b5b,
inline constexpr uint16_t kHudScrollLeft = 6002;   //   Hud_ScrollLeftArrow 08ad,
inline constexpr uint16_t kHudScrollRight = 6003;  //   Hud_ScrollRightArrow 084c
inline constexpr uint16_t kHudPartFirst = 5000;  // 5000..5100: Hud_SelectPart(code - 5000) 1a04:090e
inline constexpr uint16_t kHudPartLast = 5100;
inline constexpr uint16_t kFirstPoll = 0x4000;   // a press code on the first poll after the press
inline constexpr uint16_t kStillHeld = 0x8000;   // a press code on later polls while held
}  // namespace code

// The 12-entry global key table CS:0275 (codes; handlers CS:028d) in its order, for checks.
inline constexpr uint16_t kGlobalKeyCodes[12] = {0x0013, 0x001B, 0x0118, 0x0121, 0x0123, 0x013B,
                                                 0x013C, 0x013D, 6000,   6001,   6002,   6003};

// The game's handlers for the global keys (unset: nothing happens, the code is still consumed).
struct GlobalKeyHooks {
    std::function<void()> toggleSound;                  // Ctrl+S: Snd_ToggleOption 1cdd:05c8
    std::function<void()> hudTrash;                     // 6000
    std::function<void()> hudPutBack;                   // 6001
    std::function<void()> hudScrollLeft;                // 6002
    std::function<void()> hudScrollRight;               // 6003
    std::function<void(uint16_t part)> hudSelectPart;   // 5000..5100: part = code - 5000
};

class InputCodes {
public:
    using EscHandler = std::function<void()>;
    using MenuHandler = std::function<void(int index)>;
    static constexpr uint32_t kIdleTicks = 0x111C;  // 30.07 s (198d:02f0)

    InputCodes(GgKeyboard& keyboard, GgMouse& mouse, ClickZones& zones, std::function<uint32_t()> ticks = {});

    GlobalKeyHooks& hooks() { return hooks_; }
    // g_escHandler DS:8656 (the warehouse sets 1a04:0d65, the workshop its own; both clear it on exit).
    void setEscHandler(EscHandler h) { esc_ = std::move(h); }
    const EscHandler& escHandler() const { return esc_; }
    // g_menuHandler DS:865a (the menu bar sets the drop-down 40a4:020b; Dlg_RunModal saves, clears
    // and restores it).
    void setMenuHandler(MenuHandler h) { menu_ = std::move(h); }
    const MenuHandler& menuHandler() const { return menu_; }
    uint16_t menuDisabled = 0;  // g_menuDisabled DS:054e (only set inside Res_Read)
    uint16_t menuRequest = 0;   // g_menuRequest DS:130e (set to 1 / 2 by the menu commands 4106:0166 / 00ee)

    // 198d:0106 Input_GetCode.
    uint16_t getCode();
    // 198d:00bf Input_Flush: Input_GetCode while the ring is not empty (codes discarded, global keys
    // act); the mouse latches stay.
    void flush();
    // 198d:0008 Input_HitZones.
    uint16_t hitZones(const InputRecord& record) const { return zones_.hit(record); }
    // 198d:0328 Input_StripHeld: 0 when bit 15 is set, else the code without bit 14.
    static uint16_t stripHeld(uint16_t code) {
        return (code & code::kStillHeld) ? uint16_t{0} : static_cast<uint16_t>(code & ~code::kFirstPoll);
    }

    // 198d:02b8 Input_GetHeldKeys: low word = g_heldKeys, plus held::kIdle once after more than
    // 0x111C ticks without a held key; high word = `highWord` (the caller's DS:7ad2, actor 0 + 48h).
    // Held keys set g_keyHeldSeen DS:5c1f = 1, reset the idle tick DS:1314 and re-arm the idle bit.
    uint32_t getHeldKeys(uint16_t highWord = 0);
    // DS:1314 = g_ticks (12e0:0683 a warehouse room starts, 2dee:1cd6 the workshop starts).
    void resetIdleTick() { lastHeldTick_ = now(); }
    uint32_t lastHeldTick() const { return lastHeldTick_; }  // DS:1314
    void setLastHeldTick(uint32_t t) { lastHeldTick_ = t; }
    uint16_t idleReported = 0;  // g_idleReported DS:8554
    uint16_t keyHeldSeen = 0;   // g_keyHeldSeen DS:5c1f (the HUD and the states also write it)

    // Diagnostics: the last code returned by getCode() and the clicks a key record erased.
    uint16_t lastCode() const { return last_; }
    uint64_t clicksErased() const { return erased_; }

private:
    uint32_t now() const { return ticks_ ? ticks_() : 0; }
    uint16_t globalKeys(uint16_t code);

    GgKeyboard& keyboard_;
    GgMouse& mouse_;
    ClickZones& zones_;
    std::function<uint32_t()> ticks_;
    GlobalKeyHooks hooks_;
    EscHandler esc_;
    MenuHandler menu_;
    uint32_t lastHeldTick_ = 0;
    uint16_t last_ = 0;
    uint64_t erased_ = 0;
};

}  // namespace tmport::gg
