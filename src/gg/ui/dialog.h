#pragma once
// Dialog boxes of the TLC UI library (ovl27 3f56; re/gg/docs/code_ui.md §5): the 0x32-byte
// descriptor, the layout (Dlg_Layout), the draw recipe (Dlg_Draw) with its save-under, the click
// zones of a box, and the descriptors the prebuilt boxes build on the stack (message box, Yes/No,
// the chimps question, message bundles, the help pager's pages). The modal loop (Dlg_RunModal
// 3f56:0655: time freeze, sound pause, key handling) is the game's (wave 3); everything it draws
// and the zones it adds are here.
//
// Layout (3f56:0000, font 0, line height lh = 10; half(v) = v / 2 truncated toward zero):
//   (bw, bh) = Text_Measure(body, 280); bodyW = desc or bw; h = bh, or desc.bodyH + 6
//   W = bodyW; H = h + 17; titleY = bodyY = 6; btnY = footerY = h + 11; compact: all but W - 2
//   title:  W = max(W, Text_Width(title)); H, bodyY, btnY, footerY += lh + 4
//   footer: W = max(W, Text_Width(footer)); H += lh + 3; footerY += 2
//   W += 12; innerW = W
//   buttons: bW = max label width + 20; bH = lh + 2; rowW = n (bW + 10) + 10; W = max(W, rowW);
//            H += bH + 8; btnY += 4; footerY += bH + 8
//   W += 10; x, y = desc or centred on 320 x 200; text x = x + half(W - 2 - its width)
//   button row start: align 0 x + 10; 1 x + half(W - 2 - rowW) + 10; 2 x + W - 6 - n (bW + 10)
// Drawing (3f56:0389): colour 15 box, two colour-0 frames (x+3, y+3, W-8, H-8) and (x+2, y+2, W-6,
// H-6) (compact: one row higher), a 2-pixel colour-7 shadow inside the box, the title and its rule,
// the body, the footer rule and footer, the pictures (opaque, over the body), the buttons and the
// focused button inverted, Dirty_Flush. The box's outer two pixels stay colour 15.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "gg/data/resources.h"
#include "gg/input/zones.h"
#include "gg/ui/button.h"
#include "gg/ui/ui_canvas.h"
#include "text/text_engine.h"

namespace tmport::gg {

class GgGame;

class Archive;
class GgExe;

// Flags (+0x30).
inline constexpr uint16_t kDlgCompact = 0x0001;    // 2 pixels less at the top (the drop-downs)
inline constexpr uint16_t kDlgEscDisabled = 0x0002; // Esc does not close it (the chimps question)

// Alignment of the button row (+0x18).
enum class DlgAlign : int16_t { Left = 0, Centre = 1, Right = 2 };

// A picture of a dialog (+0x22 count, +0x24 far LPX pointers, +0x28 / +0x2c dx / dy arrays): drawn
// opaque with Lpx_Draw at (box x + 4 + dx, body y - 2 + dy).
struct DlgPicture {
    std::span<const uint8_t> lpx;
    int dx = 0;
    int dy = 0;
};

// The 0x32-byte descriptor (passed by value with F_SPUSH). The strings are raw bytes up to their NUL;
// the caller keeps them alive while the descriptor is used.
struct DialogDesc {
    std::optional<text::Bytes> title;   // +0x00: NULL = no title line
    text::Bytes body;                   // +0x04: wrapped by Text_Render
    std::optional<text::Bytes> footer;  // +0x08: NULL = none; a click on the footer line is Esc
    std::vector<text::Bytes> labels;    // +0x0c count (0..3), +0x0e labels
    std::vector<uint16_t> keys;         // +0x12: key i is the press and the release code of button i
    int focus = 0;                      // +0x16: the initially focused (inverted) button
    DlgAlign align = DlgAlign::Centre;  // +0x18 (other values: an unset x in the original)
    int x = -1, y = -1;                 // +0x1a, +0x1c: -1 = centred on 320 x 200
    int bodyW = -1;                     // +0x1e: -1 = measured
    int bodyH = -1;                     // +0x20: -1 = measured; else value + 6
    std::vector<DlgPicture> pictures;   // +0x22..+0x2e
    uint16_t flags = 0;                 // +0x30: kDlgCompact, kDlgEscDisabled

    int buttonCount() const { return static_cast<int>(labels.size()); }
};

// The layout globals of 3f56 (g_dlgX DS:bc26 .. g_dlgPicX/Y DS:bbba/bbb8, the button records
// g_dlgButtons DS:bbbc and the footer zone g_dlgFooterZone DS:3dd4).
struct DialogLayout {
    static constexpr int kMaxButtons = 3;  // DS:bbbc holds three records; a fourth would overrun

    int x = 0, y = 0, w = 0, h = 0;        // g_dlgX/Y/W/H
    int innerW = 0;                        // g_dlgInnerW: content width + 12 (the menu highlight width)
    int lineH = 0;                         // g_dlgLineH (Font_GetHeight of font 0)
    int bodyX = 0, bodyY = 0, bodyW = 0;   // g_dlgBodyX/Y/W
    int titleX = 0, titleY = 0;            // g_dlgTitleX (set only with a title), g_dlgTitleY (box-relative without one)
    int footerX = 0, footerY = 0;          // g_dlgFooterX/Y (footerX only with a footer; footerY relative to the box without one)
    int picX = 0, picY = 0;                // g_dlgPicX/Y (set only with pictures)
    int buttonW = 0, buttonH = 0, rowW = 0;
    std::array<DlgButton, kMaxButtons> buttons{};
    int buttonCount = 0;
    // g_dlgFooterZone DS:3dd4: {rect, x = box x, y = footer y, w = box W, h 10, priority 5, press =
    // release = 0x1B}; x, y, w are written by Dlg_AddZones.
    Zone footerZone;

    // The box rectangle (what the save-under covers).
    int right() const { return x + w; }
    int bottom() const { return y + h; }
};

// 3f56:0000 Dlg_Layout(desc), with font 0 selected (Dlg_Draw selects it). FF 03 escapes in the body
// change font 0's colour already here (Text_Measure), as in the original. More than three buttons
// are cut to three (the original overruns its records; no caller passes more).
DialogLayout dlgLayout(Fonts& fonts, const DialogDesc& desc);

// 3f56:0389 Dlg_Draw(desc, saveUnder): selects font 0, lays out, saves the box area into `saveUnder`
// when given (Dlg_SaveUnder 3f56:033e: Gfx_GetImage(x, y, W, H); the original uses the shared
// resource buffer, the engine the caller's buffer), draws, Dirty_Flush; restores g_drawColor and the
// previous font (font 0's colour stays 0, or what the body's last FF 03 set when there is no footer).
DialogLayout dlgDraw(const UiCanvas& ui, const DialogDesc& desc, SavedImage* saveUnder = nullptr);

// 3f56:0367 Dlg_RestoreUnder: Gfx_PutImage(x, y) of the saved area, Dirty_Flush.
void dlgRestoreUnder(const UiCanvas& ui, const DialogLayout& layout, const SavedImage& saved);

// 3f56:05d7 Dlg_AddZones: the button zones (Zones_AddLocal, in order) and, with a footer, the footer
// zone. 3f56:061f Dlg_RemoveZones removes the same. The layout must stay where it is while linked.
void dlgAddZones(ClickZones& zones, DialogLayout& layout, const DialogDesc& desc);
void dlgRemoveZones(ClickZones& zones, DialogLayout& layout, const DialogDesc& desc);

// The DGROUP strings and tables the prebuilt boxes use (code_ui.md §18).
struct DialogStrings {
    text::Bytes footerGoBack;                   // s_footerGoBack DS:3e38 (Esc key-cap)
    text::Bytes footerGoOn;                     // s_footerGoOn DS:3e45 (Enter key-cap)
    std::array<text::Bytes, 11> buttonLabels{};  // g_yesNoButtons DS:3dea: Yes, No, .. (far table)
    std::array<uint16_t, 5> keys{};             // g_dlgKeys DS:3e16: 'y' 'n' 'c' 'd' 'o'
    std::array<uint16_t, 6> pagerKeys{};        // g_pagerKeys DS:3e20: PgDn | PgDn | PgUp PgDn | PgUp PgDn
    std::array<text::Bytes, 2> chimpLabels{};   // g_chimpButtons DS:3e2c: With / Without Chimps
    text::Bytes hintTitle;                      // s_newPlayerHint DS:2462
    text::Bytes hintFooter;                     // s_hintFooter DS:2472 (Enter key-cap)

    static DialogStrings fromExe(const GgExe& exe);
};

// A descriptor and the private resource copy its strings point into (Res_LoadCopy). Move-only: a
// copy's descriptor would still point into the original's bytes.
struct OwnedDialog {
    DialogDesc desc;
    Bundle bundle;

    OwnedDialog() = default;
    OwnedDialog(OwnedDialog&&) noexcept = default;
    OwnedDialog& operator=(OwnedDialog&&) noexcept = default;
    OwnedDialog(const OwnedDialog&) = delete;
    OwnedDialog& operator=(const OwnedDialog&) = delete;
};

// 3f56:0b1c Dlg_MessageBox({title, body, footer}): no buttons, centred, measured, flags 0. Returns
// (through the modal loop) 0 for Enter, -1 for Esc or a footer click.
DialogDesc messageBoxDesc(std::optional<text::Bytes> title, text::Bytes body, std::optional<text::Bytes> footer);
// 3f56:0bb6 Dlg_YesNo({title, body, defYes}): footer "Esc to Go Back", labels Yes/No, keys y/n,
// focus defYes ? 0 : 1, buttons right-aligned. The modal result 0 (Yes) gives 1, 1 (No) gives 0.
DialogDesc yesNoDesc(const DialogStrings& strings, std::optional<text::Bytes> title, text::Bytes body, bool defYes);
// 3f56:0c5e Dlg_AskChimps(): body = member 0 of bundle 0x1067, labels With / Without Chimps, keys
// y/n, focus 0, centred, Esc disabled; no title or footer.
inline constexpr uint16_t kChimpsBundle = 0x1067;
OwnedDialog askChimpsDesc(const DialogStrings& strings, const Archive& archive);
// 3f56:0d2a Dlg_MessageRes(id): bundle {member 0 title (NULL when its first word is 0, an 'I 0'
// member), member 1 body, member 2 footer flag}: flag 0 "Esc to Go Back", 1 "Enter to Go On". Any
// other flag leaves the original's footer pointer uninitialised (0x106F: its member 2 is a string);
// the engine then uses member 3 when it is an integer (0x106F's real flag) and no footer otherwise
// (PORTING_NOTES.md §10).
OwnedDialog messageResDesc(const DialogStrings& strings, const Archive& archive, uint16_t id);

// One page of the help pager (Dlg_RunPager 3f56:0907): x 19, y 14, body 260 x 120, no footer, flags
// 0. Buttons: the only page Done (key PgDn, focus 0, right); the first of several See Next Page
// (PgDn, 0, right); a middle page Go Back a Page + See Next Page (PgUp, PgDn, focus 1, centred); the
// last Go Back a Page + Done (PgUp, PgDn, 1, centred).
DialogDesc pagerPageDesc(const DialogStrings& strings, int pages, int page, text::Bytes title, text::Bytes text,
                         std::vector<DlgPicture> pictures = {});

// The modal loop's answers (Dlg_RunModal): the selected button, or -1 for Esc / a footer click.
inline constexpr int kDlgCancelled = -1;


// ---- wave 3: the run (owner: ui agent; ui/dialog_run.cpp) -------------------------------------------
// The modal loop and the prebuilt boxes on the game (code_ui.md §5.6-§5.8). The layout globals the
// boxes write are GgState::ui (dlgX .. dlgInnerW); the save-under buffers are the caller's.

// 3f56:0655 Dlg_RunModal(desc, saveUnder, restoreUnder): Snd_Pause, the menu hook saved and cleared,
// play mode saved and 0, Mouse_ForceDefault(1), g_savedTicks = g_ticks; Dlg_Draw (saving the box area
// into *saveUnder when given), Zones_Push, Dlg_AddZones, Input_Flush; the busy poll (Enter flashes the
// focused button, Esc unless flags & 2, Left / Right move the focus, a button's key or release picks
// it, a press moves the focus); then Dlg_RestoreUnder from *restoreUnder when given, everything
// restored, Input_Flush, the font, and g_ticks = g_savedTicks (time stands still: GgClock::setTicks).
// Returns the button chosen, or -1 (Esc, a footer click; kDlgCancelled).
int dlgRunModal(GgGame& g, const DialogDesc& desc, SavedImage* saveUnder, SavedImage* restoreUnder);
// 3f56:08ee Dlg_Show(desc): Dlg_RunModal(desc, saving and restoring the box area).
int dlgShow(GgGame& g, const DialogDesc& desc);
// 3f56:0b1c Dlg_MessageBox({title, body, footer}): 0 Enter, -1 Esc or a footer click.
int dlgMessageBox(GgGame& g, std::optional<text::Bytes> title, text::Bytes body, std::optional<text::Bytes> footer);
// 3f56:0bb6 Dlg_YesNo({title, body, defYes}): 1 Yes, 0 No, -1 Esc.
int dlgYesNo(GgGame& g, std::optional<text::Bytes> title, text::Bytes body, bool defYes);
// 3f56:0c5e Dlg_AskChimps(): 1 With Chimps, 0 Without (Esc disabled).
int dlgAskChimps(GgGame& g);
// 3f56:0d2a Dlg_MessageRes(id): the message bundle id as a message box (0x106F's footer: member 3).
int dlgMessageRes(GgGame& g, uint16_t id);
// 4106:006d Dlg_YesNoRes(id): MsgDesc_LoadRes (4106:0000) and Dlg_YesNo: 1 Yes, 0 No, -1 Esc.
int dlgYesNoRes(GgGame& g, uint16_t id);
// 3f56:0907 Dlg_RunPager(n, title, pages, pictures): one modal box per page (time frozen per page),
// See Next Page / Go Back a Page / Done (PgDn / PgUp), the area restored at the end. 1 Done, 0 Esc.
struct HelpTopic;
int dlgRunPager(GgGame& g, const HelpTopic& topic);

}  // namespace tmport::gg
