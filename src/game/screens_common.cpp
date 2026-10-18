#include "game/screens_common.h"

#include "audio/audio_system.h"
#include "data/exe_tables.h"
#include "platform/engine.h"
#include "res/game_files.h"

namespace tmport::game {

CType CType::fromExe(const ExeTables& exe) {
    CType t;
    const auto b = exe.bytes(screens_dg::kCType, 256);
    for (size_t i = 0; i < 256; ++i) t.flags[i] = b[i];
    return t;
}

// Borland C++ 4 _ctype (code_1000.md §11): 1 space, 2 digit, 4 upper, 8 lower, 0x10 hex digit,
// 0x20 control, 0x40 punctuation, 0x80 blank.
CType CType::borland() {
    CType t;
    for (unsigned c = 0; c < 256; ++c) {
        uint8_t f = 0;
        if (c < 0x20 || c == 0x7F) f = 0x20;
        if (c >= 0x09 && c <= 0x0D) f |= 0x01;
        if (c == 0x20) f = 0x81;
        if ((c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) || (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E))
            f = 0x40;
        if (c >= '0' && c <= '9') f = 0x12;
        if (c >= 'A' && c <= 'Z') f = (c <= 'F') ? 0x14 : 0x04;
        if (c >= 'a' && c <= 'z') f = (c <= 'f') ? 0x18 : 0x08;
        t.flags[c] = f;
    }
    return t;
}

const ExeTables& ScreenIo::exe() const { return *ctx_.files.exe; }

void ScreenIo::serviceAudio() {
    if (ctx_.audio) ctx_.audio->service();
}

uint16_t ScreenIo::getCode() {
    const uint16_t c = ctx_.engine.getInputCode();
    serviceAudio();
    return c;
}

uint16_t ScreenIo::waitCode() {
    for (;;) {
        if (const uint16_t c = getCode()) return c;
        ctx_.engine.clock().sleepMs(1);
    }
}

void ScreenIo::flushInput() { ctx_.engine.flushInput(); }

void ScreenIo::timerSet(int slot, int32_t ms) { ctx_.engine.timerSet(slot, ms); }

bool ScreenIo::timerExpired(int slot) { return ctx_.engine.timerExpired(slot); }

void ScreenIo::setCursorForced(bool on) { ctx_.engine.cursor().setForced(on); }

void ScreenIo::present(int x, int y, int w, int h) {
    ctx_.engine.presenter().blitToWindow(ctx_.engine.frame(), x, y, w, h);
}

std::span<const uint8_t> ScreenIo::dgString(uint16_t off) const { return exe().cstr(off); }

ui::DlgDesc ScreenIo::dialog(uint16_t off) const { return ui::DlgDesc::fromExe(exe(), off); }

}  // namespace tmport::game
