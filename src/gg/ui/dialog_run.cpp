// The modal loop and the prebuilt boxes on the game (dialog.h, "wave 3: the run"; code_ui.md §5.6-§5.8).
// Owner: ui agent.
//
// Every loop here is a busy poll of Input_GetCode (each poll a GgRuntime yield point). Time stands
// still in a box: Dlg_RunModal writes g_ticks back to its entry value when it closes (GgClock::setTicks;
// the PIT keeps running, so the sound tick's parity follows the written value as in the original).
#include "gg/ui/dialog.h"

#include <cctype>

#include "gg/game/game.h"
#include "gg/ui/help.h"
#include "gg/ui/menu_commands.h"
#include "gg/ui/ui_run.h"

namespace tmport::gg {

void storeDlgLayout(UiState& ui, const DialogLayout& L, const DialogDesc& desc) {
    ui.dlgX = static_cast<int16_t>(L.x);
    ui.dlgY = static_cast<int16_t>(L.y);
    ui.dlgW = static_cast<int16_t>(L.w);
    ui.dlgH = static_cast<int16_t>(L.h);
    ui.dlgInnerW = static_cast<int16_t>(L.innerW);
    ui.dlgLineH = static_cast<int16_t>(L.lineH);
    ui.dlgBodyX = static_cast<int16_t>(L.bodyX);
    ui.dlgBodyY = static_cast<int16_t>(L.bodyY);
    ui.dlgBodyW = static_cast<int16_t>(L.bodyW);
    ui.dlgFooterY = static_cast<int16_t>(L.footerY);
    ui.dlgTitleY = static_cast<int16_t>(L.titleY);  // written by every layout (box-relative without a title)
    if (desc.title) ui.dlgTitleX = static_cast<int16_t>(L.titleX);
    if (desc.footer) ui.dlgFooterX = static_cast<int16_t>(L.footerX);
    if (!desc.pictures.empty()) {
        ui.dlgPicX = static_cast<int16_t>(L.picX);
        ui.dlgPicY = static_cast<int16_t>(L.picY);
    }
}

// 3f56:0367 Dlg_RestoreUnder
void dlgRestoreUnderAt(GgGame& g, const SavedImage& saved) {
    DialogLayout at;
    at.x = g.ui.dlgX;
    at.y = g.ui.dlgY;
    dlgRestoreUnder(g.canvas(), at, saved);
}

// 3f56:0655 Dlg_RunModal
int dlgRunModal(GgGame& g, const DialogDesc& desc, SavedImage* saveUnder, SavedImage* restoreUnder) {
    // The entry, in the original's order: font 0, g_savedTicks, play mode 0, the arrow forced, the menu
    // hook saved and cleared, Snd_Pause.
    const int prevFont = g.fonts.select(0);
    g.ui.savedTicks = g.ticks();
    const uint16_t prevPlay = g.keyboard().playMode();
    g.keyboard().setPlayMode(0);
    const int prevForce = g.video.cursor().forceDefault(1);
    const InputCodes::MenuHandler prevMenu = g.input().menuHandler();
    g.input().setMenuHandler({});
    g.sound.pause();

    const UiCanvas ui = g.canvas();
    DialogLayout L = dlgDraw(ui, desc, saveUnder);  // 3f56:0389 Dlg_Draw (Dlg_SaveUnder when asked)
    storeDlgLayout(g.ui, L, desc);
    g.zones().push();
    dlgAddZones(g.zones(), L, desc);
    // The zones live in this frame: an exception from a yield point (a test's stop, the end of a run,
    // exit) unlinks them and puts the hooks back.
    OnUnwind unwind([&] {
        dlgRemoveZones(g.zones(), L, desc);
        g.zones().pop();
        g.input().setMenuHandler(prevMenu);
        g.keyboard().setPlayMode(prevPlay);
        g.video.cursor().forceDefault(prevForce);
        g.fonts.select(prevFont);
    });
    g.rt.flushInput();

    const int n = L.buttonCount;
    const auto button = [&L](int i) -> const DlgButton& { return L.buttons[static_cast<size_t>(i)]; };
    const auto flash = [&g, &ui](const DlgButton& b) { btnFlash(ui, b, [&g](int t) { g.rt.delay(static_cast<uint16_t>(t)); }); };
    int sel = desc.focus;
    for (bool done = false; !done;) {
        uint16_t code = g.rt.getInputCode();  // the busy poll
        switch (code) {                       // the table CS:08da
            case 0x0000: break;
            case 0x000D:  // Enter: the focused button flashes (a box without buttons answers its focus)
                if (n > 0) flash(button(sel));
                done = true;
                break;
            case 0x001B:  // Esc (and a click on the footer line), unless flags & 2
                if (!(desc.flags & kDlgEscDisabled)) {
                    sel = kDlgCancelled;
                    done = true;
                }
                break;
            case 0x014B:  // Left: the focus moves, wrapping
                if (n > 1) {
                    btnToggleFocus(ui, button(sel));
                    if (--sel < 0) sel = n - 1;
                    btnToggleFocus(ui, button(sel));
                }
                break;
            case 0x014D:  // Right
                if (n > 1) {
                    btnToggleFocus(ui, button(sel));
                    if (++sel >= n) sel = 0;
                    btnToggleFocus(ui, button(sel));
                }
                break;
            default: {
                if (code < 0x80) code = static_cast<uint16_t>(std::tolower(code));  // the key arrays are lower case
                for (int i = 0; i < n; ++i) {
                    const uint16_t key = button(i).zone.pressCode;
                    if (code == (key | code::kFirstPoll) || code == (key | code::kStillHeld)) {
                        // pressed or held over button i: the focus goes there
                        if (sel != i) {
                            btnToggleFocus(ui, button(sel));
                            sel = i;
                            btnToggleFocus(ui, button(sel));
                        }
                        break;
                    }
                    if (code == key) {
                        // its key, or a release over it: chosen. A button that was not focused flashes
                        // without being focused first, so it ends unfocused (3f56:081f; the box closes).
                        if (sel != i) btnToggleFocus(ui, button(sel));
                        sel = i;
                        flash(button(sel));
                        done = true;
                        break;
                    }
                }
                break;
            }
        }
    }
    // The exit, in the original's order (3f56:0872).
    if (restoreUnder) dlgRestoreUnderAt(g, *restoreUnder);
    g.input().setMenuHandler(prevMenu);
    g.keyboard().setPlayMode(prevPlay);
    g.sound.resume();
    dlgRemoveZones(g.zones(), L, desc);
    g.zones().pop();
    g.rt.flushInput();
    g.video.cursor().forceDefault(prevForce);
    g.fonts.select(prevFont);
    g.clock().setTicks(g.ui.savedTicks);  // time stood still (Quirk kept: PORTING_NOTES §11)
    return sel;
}

// 3f56:08ee Dlg_Show
int dlgShow(GgGame& g, const DialogDesc& desc) {
    SavedImage save;
    return dlgRunModal(g, desc, &save, &save);
}

// 3f56:0b1c Dlg_MessageBox
int dlgMessageBox(GgGame& g, std::optional<text::Bytes> title, text::Bytes body, std::optional<text::Bytes> footer) {
    return dlgShow(g, messageBoxDesc(title, body, footer));
}

// 3f56:0bb6 Dlg_YesNo: the modal result 0 (Yes) gives 1, 1 (No) gives 0, -1 stays
int dlgYesNo(GgGame& g, std::optional<text::Bytes> title, text::Bytes body, bool defYes) {
    const int r = dlgShow(g, yesNoDesc(g.strings, title, body, defYes));
    return r < 0 ? -1 : r == 0 ? 1 : 0;
}

// 3f56:0c5e Dlg_AskChimps: button 0 (With Chimps) gives 1; Esc is disabled
int dlgAskChimps(GgGame& g) {
    const OwnedDialog box = askChimpsDesc(g.strings, g.archive());  // Res_LoadCopy(0x1067)
    const int r = dlgShow(g, box.desc);
    return r < 0 ? -1 : r == 0 ? 1 : 0;
}

// 3f56:0d2a Dlg_MessageRes
int dlgMessageRes(GgGame& g, uint16_t id) {
    const OwnedDialog box = messageResDesc(g.strings, g.archive(), id);  // Res_LoadCopy; 0x106F's footer guarded
    return dlgShow(g, box.desc);
}

// 4106:006d Dlg_YesNoRes
int dlgYesNoRes(GgGame& g, uint16_t id) {
    const YesNoMessage m = YesNoMessage::load(g.archive(), id);  // 4106:0000 MsgDesc_LoadRes
    return dlgYesNo(g, m.title, m.body, m.defYes);
}

// 3f56:0907 Dlg_RunPager
int dlgRunPager(GgGame& g, const HelpTopic& topic) {
    const int n = topic.pageCount();
    if (n <= 0) return 1;  // no shipped topic is empty (the original would restore an unsaved area)
    if (n == 1) return dlgShow(g, topic.pageDesc(g.strings, 0)) >= 0 ? 1 : 0;
    // Several pages: one modal loop per page (time frozen per page), the area saved by the first page
    // only and restored once at the end.
    SavedImage save;
    int page = 0;
    bool first = true;
    while (page >= 0 && page < n) {
        const int r = dlgRunModal(g, topic.pageDesc(g.strings, page), first ? &save : nullptr, nullptr);
        first = false;
        if (r == kDlgCancelled) page = -1;            // Esc
        else if (r == 0) page = page == 0 ? 1 : page - 1;  // the first page's See Next Page, or Go Back a Page
        else page += 1;                               // See Next Page / Done
    }
    dlgRestoreUnderAt(g, save);
    return page < 0 ? 0 : 1;
}

}  // namespace tmport::gg
