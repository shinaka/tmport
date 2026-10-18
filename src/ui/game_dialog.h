#pragma once
// Game-drawn dialog boxes (segment 10c0, "dialog.c"; re/docs/code_1098-10d8.md §10): layout from
// a DlgDesc, drawing with the FFNT text engine, save-under, buttons, the modal message / Yes-No
// loop (GameDlg_RunModal 10c0:0ae5) and the multi-page help pager (Dlg_RunPager 10c0:0cde).
//
// The layout globals of the original (DS:5c3e..5c72, g_dlgSaveBuf DS:0b70) live in one
// DialogSystem object per engine. Like the original, layout() only writes the fields that the
// descriptor's type uses; the rest keep their previous values (e.g. an unknown footer kind keeps
// the previous footer string, types 4-7 and 9 keep the previous footer y).
//
// Quirk kept (REFERENCE_CAPTURES.md §4.1): the three nested frames are drawn with the WinG frame
// routine, whose bottom edge is one row higher than FrameRect(x, y, w, h) predicts; this lives
// in gfx::FrameBuffer::frameRect and is visible in every box.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

#include "gfx/pixmap.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/dialog_desc.h"

namespace tmport {
class Engine;
class ExeTables;
}  // namespace tmport

namespace tmport::ui {

// Hot-spot ids registered by the dialogs (code_1098-10d8.md §4.1). They come back as input codes:
// id (left up), id|0x8000 (left down), id|0x4000 (right down), id|0x2000 (right up).
namespace dlghot {
inline constexpr uint16_t k44C = 0x044C;  // type 8: "Yes" (button 1, left); pager: button 0
inline constexpr uint16_t k44D = 0x044D;  // type 8: "No" (button 0, right); pager: button 1
inline constexpr uint16_t k44E = 0x044E;  // footer line; pager: the whole screen (cancel)
inline constexpr uint16_t k44F = 0x044F;  // the whole screen; pager: the box (inert)
}  // namespace dlghot

struct DialogButton {
    int x = 0, y = 0, w = 0, h = 0;  // g_btnX/Y/W/H[i]
    uint16_t label = 0;              // g_btnLabel[i]: near DGROUP offset of the label
    int labelX = 0;                  // g_btnLabelX[i]
    bool highlighted = false;        // g_btnState[i]: drawn inverted
};

// The layout globals of 10c0 (DS:5c3e..5c72).
struct DialogGeometry {
    int x = 0, y = 0, w = 0, h = 0;               // g_dlgX/Y/W/H
    int marginL = 0, marginT = 0, marginB = 0;    // g_dlgMarginL/T/B
    int titleX = 0;                               // g_dlgTitleX
    uint16_t footer = 0;                          // g_dlgFooter: near DGROUP offset, 0 = none
    int footerX = 0, footerY = 0;                 // g_dlgFooterX/Y
    std::array<DialogButton, 2> buttons{};
};

// Services of other modules the dialogs call around a modal box. Every member may be empty.
// TODO(integration): the menu bar (game area) installs menuIsEnabled/menuEnableAll and the audio
// area soundPauseAll/soundResumeAll; until then they are no-ops.
struct DialogHooks {
    std::function<bool()> menuIsEnabled;          // 1078:01b1 Menu_IsEnabled
    std::function<void(bool)> menuEnableAll;      // 1078:01b5 Menu_EnableAll
    std::function<void()> soundPauseAll;          // 1090:0536 Sound_PauseAll (Wave_Stop + Midi_Pause)
    std::function<void()> soundResumeAll;         // 1090:0540 Sound_ResumeAll (Midi_Resume)
    // Empty = Engine::cursor() (CursorController, DS:06e2 / 10e0:0063 SetCursorForced).
    std::function<bool()> cursorForced;
    std::function<void(bool)> setCursorForced;
    // Input and delay sources; empty = the engine's (Engine::getInputCode, Engine::flushInput,
    // Engine::delayMs).
    // Tests install scripted input here. A headless engine (no window) without an input hook
    // answers Esc to a modal loop instead of spinning forever.
    std::function<uint16_t()> getInputCode;       // 1028:0131 Input_GetCode (0 = nothing)
    std::function<void()> flushInput;             // 1028:0126 Input_Flush
    std::function<void(uint32_t)> delayMs;        // 10a8:0000 Delay_ms
};

class DialogSystem {
public:
    DialogSystem(Engine& engine, const text::Font& font);

    DialogHooks& hooks() { return hooks_; }
    const DialogGeometry& geometry() const { return g_; }
    text::TextRenderer& text() { return text_; }
    bool hasSaveUnder() const { return saveBuf_.has_value(); }
    int musicPauseDepth() const { return musicPauseDepth_; }

    // 10c0:0000 Dlg_Layout: computes the box, margins, title/footer positions and buttons.
    void layout(const DlgDesc& d);
    // 10c0:0764 Dlg_Draw: layout, optional save-under, box, frames, shadow, picture, title,
    // body, footer, buttons (default highlighted), then presents the box.
    void draw(const DlgDesc& d, bool saveUnder);
    // 10c0:046e Dlg_DrawButton (not presented).
    void drawButton(int i, bool highlighted);
    // 10c0:0640 Dlg_ToggleButton: redraw inverted and present.
    void toggleButton(int i);
    // 10c0:06a4 Dlg_FlashButton: 4 x { toggle; Delay_ms(62) } (ends in the original state).
    void flashButton(int i);
    // 10c0:06ca Dlg_SaveUnder / 10c0:0712 Dlg_RestoreUnder (restore presents the box area;
    // also used directly by the sign-in to close the non-modal name box).
    void saveUnder();
    void restoreUnder();
    // 10c0:0a1c Dlg_AddHotspots / 10c0:0aaa Dlg_RemoveHotspots
    void addHotspots(const DlgDesc& d);
    void removeHotspots(const DlgDesc& d);

    // 10c0:0ae5 GameDlg_RunModal (types 1 and 8): type 8 returns 1 = Yes, 0 = No, -1 = Esc or
    // footer; type 1 returns 0 (Enter / footer) or -1 (Esc / click elsewhere).
    int runModal(const DlgDesc& d);
    // 10c0:0cde Dlg_RunPager: 1 = left with "Done" on the last page, 0 = cancelled.
    int runPager(const PagerDesc& p);
    // The DlgDesc the pager builds for one page (type 4..7, default button, page text/picture).
    static DlgDesc pagerPage(const PagerDesc& p, int page);

    // 10d0:0000 Music_PauseNested / 10d0:000e Music_ResumeNested / 10d0:0025 Music_ResumeForce.
    void musicPauseNested();
    void musicResumeNested();
    void musicResumeForce();

    // Text of a near DGROUP string (footer / button label); empty for offset 0.
    std::span<const uint8_t> dgString(uint16_t off) const;

private:
    int textWidth(std::span<const uint8_t> s) { return text_.width(s); }
    void present(int x, int y, int w, int h);
    uint16_t waitInputCode();
    void flushInput();
    bool cursorForced();
    void setCursorForced(bool on);
    void delay(uint32_t ms);

    Engine& engine_;
    const ExeTables& exe_;
    text::TextRenderer text_;
    DialogHooks hooks_;
    DialogGeometry g_;
    std::optional<gfx::PixMap> saveBuf_;  // g_dlgSaveBuf
    int musicPauseDepth_ = 0;             // g_musicPauseDepth (DS:5cc8, i8)
};

}  // namespace tmport::ui
