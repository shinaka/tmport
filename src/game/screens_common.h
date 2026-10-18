#pragma once
// Helpers shared by the title (segment 1138) and Club Sign-In (segment 1160) screens: the wait
// primitives they spin on, presenting, the cursor, DGROUP strings/dialogs, and the Borland
// _ctype test both screens inline. Everything is read from the user's TMTWINCD.EXE.

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "app/app_context.h"
#include "ui/dialog_desc.h"

namespace tmport {
class ExeTables;
}

namespace tmport::game {

// DGROUP offsets used only by these screens (not yet in data/dgroup_offsets.h).
// TODO(integration): move into data/dgroup_offsets.h when that file is next edited.
namespace screens_dg {
inline constexpr uint16_t kCType = 0x3049;         // Borland _ctype[c + 1] (table at 3048, index c+1)
inline constexpr uint16_t kStrNameBlank2 = 0x2399; // "________" copy used by the name-entry Esc path
}  // namespace screens_dg

// Borland's character classification table (code_1000.md §11). The screens test
// `_ctype[c + 1] & 0xDE` inline ("printable": 0x20..0x7E) and call toupper (bit 8 = lower case).
struct CType {
    std::array<uint8_t, 256> flags{};  // flags[c] = _ctype[c + 1]

    // The table as read from DS:3049 of the user's EXE.
    static CType fromExe(const ExeTables& exe);
    // The table the Borland RTL ships (identical to the EXE's; for tests without game data).
    static CType borland();

    // _ctype[c + 1] & 0xDE for c < 0x100 (false for anything larger).
    bool printable(unsigned c) const { return c < 0x100 && (flags[c] & 0xDE) != 0; }
    // 1000:0998 toupper: lower case letters -> c - 0x20, else c & 0xFF.
    uint8_t toUpper(unsigned c) const {
        const uint8_t b = static_cast<uint8_t>(c);
        return (flags[b] & 0x08) ? static_cast<uint8_t>(b - 0x20) : b;
    }
};

// Optional trace of screen events (tools / tests print them with the clock time).
using ScreenTrace = std::function<void(std::string_view event)>;

// The wait/draw primitives the two screens use, on top of the AppContext.
class ScreenIo {
public:
    explicit ScreenIo(AppContext& ctx) : ctx_(ctx) {}

    AppContext& ctx() { return ctx_; }
    Engine& engine() { return ctx_.engine; }
    audio::AudioSystem* audio() { return ctx_.audio; }
    const ExeTables& exe() const;

    // 1028:0131 Input_GetCode (also services the audio system, as the window procedure would).
    uint16_t getCode();
    // 10a0:0000 Input_WaitCode: getCode() until non-zero, yielding 1 ms between polls.
    uint16_t waitCode();
    // 1028:0126 Input_Flush.
    void flushInput();
    // 10a8:002a Timer_Set / 10a8:0066 Timer_Expired (pumps; yields up to 1 ms when not expired).
    void timerSet(int slot, int32_t ms);
    bool timerExpired(int slot);
    // 10e0:0063 SetCursorForced.
    void setCursorForced(bool on);
    // 1058:05bc Gfx_BlitToWindow.
    void present(int x, int y, int w, int h);
    void presentAll() { present(0, 0, 512, 384); }
    // MM_WOM_DONE / MM_MCINOTIFY duties (AudioSystem::service) when audio exists.
    void serviceAudio();

    // NUL-terminated DGROUP string at off.
    std::span<const uint8_t> dgString(uint16_t off) const;
    // DlgDesc at a DGROUP offset.
    ui::DlgDesc dialog(uint16_t off) const;

    // Optional event trace (tools).
    void setTrace(ScreenTrace t) { trace_ = std::move(t); }
    void trace(std::string_view event) const {
        if (trace_) trace_(event);
    }

private:
    AppContext& ctx_;
    ScreenTrace trace_;
};

// Raw bytes of a std::string / string_view for the text renderer.
inline std::span<const uint8_t> bytesOf(std::string_view s) {
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

}  // namespace tmport::game
