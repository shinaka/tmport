#include "dos/input/mouse.h"

#include <algorithm>

namespace tmport::dos {

void MouseDriver::startPosition(Look look, uint16_t& x, uint16_t& y) {
    switch (look) {
        case Look::Mcga: x = 158; y = 82; break;
        case Look::Cga: x = kMaxX; y = 146; break;
        case Look::Ega:
        case Look::Tandy: x = kMaxX; y = 101; break;
    }
}

void MouseDriver::setLogicalPosition(int x, int y) {
    x_ = static_cast<uint16_t>(std::clamp(x / 2, 0, kMaxX));  // driver x unit = 2 logical pixels
    y_ = static_cast<uint16_t>(std::clamp(y, 0, kMaxY));
}

// 130f:0D94 Mouse_Poll.
MousePointer::Result MousePointer::poll(const MouseDriver& driver, uint8_t uiMode, int16_t slot0Script) {
    Result r;
    if (!driver.present) return r;
    auto call = [&](PointerAnimCall::Op op, int16_t script) { r.calls[static_cast<size_t>(r.callCount++)] = {op, script}; };
    prevX = x;
    prevY = y;
    prevButtons = buttons;
    buttons = driver.buttons();  // INT 33h AX=3
    x = driver.x();
    y = driver.y();
    r.polled = true;
    const bool changed = prevX != x || prevY != y || prevButtons != buttons;
    if (!visible) {
        if (changed) {  // any activity shows the pointer again
            idleTicks = 0;
            visible = true;
        }
        return r;
    }
    if (y < 0x10 || y > 0x8F || uiMode != 0) {
        call(PointerAnimCall::Op::Set, kScriptArrow);
    } else {
        // Signed 16-bit difference, as SUB + JLE/JGE.
        const int16_t dx = static_cast<int16_t>(prevX - x);
        if (dx > 0) call(PointerAnimCall::Op::Set, kScriptFaceLeft);
        if (dx < 0) call(PointerAnimCall::Op::Set, kScriptFaceRight);
        if (dx == 0 && slot0Script == 1) call(PointerAnimCall::Op::Set, kScriptFaceLeft);
        if (dx == 0) call(PointerAnimCall::Op::SetIfIdleCurrent, 0);
    }
    if (!changed) {
        if (++idleTicks == kHideTicks) {
            visible = false;
            call(PointerAnimCall::Op::Clear, 0);
        }
    } else {
        idleTicks = 0;
    }
    return r;
}

}  // namespace tmport::dos
