#pragma once
// The DOS edition's mouse (re/dos/docs/code_runtime.md §3.12, §4.20-4.21, PORTING_NOTES.md §4):
//  * MouseDriver: INT 33h (the engine's model, mischief/dos/input/int33_mouse.h) as Mouse_Setup (1460:0A74)
//    programs it: x range 0..159 (AX=7, the game's x unit is one byte = 2 pixels), y range 0..199
//    (AX=8), buttons from AX=3. The engine feeds it the host pointer in the 320x200 logical screen
//    (absolute; the original's mickey ratio AX=0Fh does not apply).
//  * MousePointer: Mouse_Poll (130f:0D94), once per executed game tick: latches position and
//    buttons (with the previous values Input_HitZone needs), picks the pointer animation of sprite
//    slot 0 and hides the pointer after 600 ticks without mouse activity.

#include <array>
#include <cstdint>

#include "dos/gfx/palettes.h"
#include "mischief/dos/input/int33_mouse.h"

namespace tmport::dos {

// g_mouse DS:912A == -1 (Int33Mouse::present): a driver answered Mouse_Detect (1460:0A5E). Without
// one the game never reads the mouse (Mouse_Poll and the mouse part of Input_MouseWalk return at once).
// setLogicalPosition clamps like the driver's range; x(), y(), buttons() are INT 33h AX=3: CX = x
// (0..159), DX = y (0..199), BX = buttons. No event handler is installed (the game polls).
class MouseDriver final : public mischief::dos::Int33Mouse {
public:
    static constexpr int kMaxX = 0x9F;  // Mouse_Setup AX=7 CX=0 DX=0x9F
    static constexpr int kMaxY = 0xC7;  // AX=8 CX=0 DX=0xC7

    // The EGA start position until resetPosition() picks the look's (gameInit, Mouse_Setup).
    MouseDriver() : mischief::dos::Int33Mouse(mischief::dos::Int33MouseConfig{0, kMaxX, 0, kMaxY, 1, 2, kMaxX, 101}) {}

    // Where the driver leaves the pointer after Mouse_Detect's reset (INT 33h AX=0, 1460:0A5E) in
    // the game's video mode and Mouse_Setup's ranges (x in bytes, y in rows): the DOSBox-X driver of
    // the reference captures has it at (159, 101) in the EGA and Tandy modes, (158, 82) in MCGA and
    // (159, 146) in CGA (REFERENCE_CAPTURES.md §6; every untouched pointer of the captures).
    static void startPosition(Look look, uint16_t& x, uint16_t& y);
    void resetPosition(Look look) {
        uint16_t x = 0, y = 0;
        startPosition(look, x, y);
        setPosition(x, y);
    }
};

// One call Mouse_Poll makes on sprite slot 0 (Anim_Set 4306:07FA, Anim_SetIfIdle 4306:0784,
// Anim_Clear 4306:092F).
struct PointerAnimCall {
    enum class Op : uint8_t {
        Set,                 // Anim_Set(0, script)
        SetIfIdleCurrent,    // Anim_SetIfIdle(0, DS:8FE2), DS:8FE2 read after the calls before it
        Clear,               // Anim_Clear(0)
    };
    Op op = Op::Set;
    int16_t script = 0;
};

class MousePointer {
public:
    static constexpr int16_t kScriptArrow = 1;      // pointer outside the play-field or a UI mode open
    static constexpr int16_t kScriptFaceLeft = 7;   // moving left over the play-field
    static constexpr int16_t kScriptFaceRight = 8;  // moving right
    static constexpr uint16_t kHideTicks = 600;     // 0x258 executed game ticks = 12.7 s

    uint16_t x = 0;            // DS:911A (0..159)
    uint16_t y = 0;            // DS:911E (0..199)
    uint16_t buttons = 0;      // DS:90F4
    uint16_t prevX = 0;        // DS:925E
    uint16_t prevY = 0;        // DS:9266
    uint16_t prevButtons = 0;  // DS:9143
    bool visible = false;      // g_mousePtrVisible DS:92AC (BSS: hidden until the first activity)
    uint16_t idleTicks = 0;    // g_mouseIdleTicks DS:92AE

    struct Result {
        bool polled = false;  // a driver is present: x/y/buttons were read
        // Sprite slot 0 follows the pointer (DS:8FD6 = x, DS:8FD8 = y, DS:8FDA = 0) when polled.
        std::array<PointerAnimCall, 3> calls{};
        int callCount = 0;
    };

    // 130f:0D94 Mouse_Poll. slot0Script = DS:8FE2 before the call (slot 0's current script [I]).
    Result poll(const MouseDriver& driver, uint8_t uiMode, int16_t slot0Script);
};

}  // namespace tmport::dos
