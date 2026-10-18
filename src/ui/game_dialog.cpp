#include "ui/game_dialog.h"

#include <SDL3/SDL_timer.h>

#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "gfx/frame_buffer.h"
#include "gfx/presenter.h"
#include "platform/engine.h"
#include "util/log.h"

namespace tmport::ui {

namespace {

// Pixel constants of Dlg_Layout / Dlg_Draw (code_1098-10d8.md §10.2-10.4).
constexpr int kLineStep = 19;        // layout height per text line (the font itself is 17 high)
constexpr int kTitleH = 24;
constexpr int kFooterH = 22;
constexpr int kFrameExtraW = 15;     // added to the content width
constexpr int kFlashDelayMs = 62;    // Dlg_FlashButton
constexpr int kFlashToggles = 4;

constexpr uint16_t kHotMask = 0x0FFF;

bool isPagerType(uint16_t t) { return t >= dlgtype::kPagerOne && t <= dlgtype::kPagerLast; }
bool hasButtonSeparator(uint16_t t) { return isPagerType(t) || t == dlgtype::kDoneCancel; }

}  // namespace

DialogSystem::DialogSystem(Engine& engine, const text::Font& font)
    : engine_(engine),
      exe_(*engine.files().exe),
      text_(font, &engine.frame(), &engine.presenter()) {}

std::span<const uint8_t> DialogSystem::dgString(uint16_t off) const {
    if (off == 0) return {};  // DGROUP:0000 is zero, so the original measures an empty string
    return exe_.cstr(off);
}

void DialogSystem::present(int x, int y, int w, int h) { engine_.presenter().blitToWindow(engine_.frame(), x, y, w, h); }

uint16_t DialogSystem::waitInputCode() {
    // The original spins on Input_GetCode until it is non-zero; here each empty poll pumps the
    // engine and yields for a millisecond.
    for (;;) {
        const uint16_t c = hooks_.getInputCode ? hooks_.getInputCode() : engine_.getInputCode();
        if (c != 0) return c;
        if (hooks_.getInputCode) continue;
        if (!engine_.hasWindow()) {
            // Headless without an input source nothing can ever arrive: answer Esc instead of
            // spinning forever (engine-only safeguard; tests install DialogHooks::getInputCode).
            log::warn("modal dialog without a window or input source: answering Esc");
            return 0x1B;
        }
        SDL_Delay(1);
    }
}

void DialogSystem::flushInput() {
    if (hooks_.flushInput)
        hooks_.flushInput();
    else
        engine_.flushInput();
}

// DS:06e2 g_cursorForced / 10e0:0063 SetCursorForced: the engine's cursor controller unless a
// hook replaces it.
bool DialogSystem::cursorForced() {
    return hooks_.cursorForced ? hooks_.cursorForced() : engine_.cursor().forced();
}

void DialogSystem::setCursorForced(bool on) {
    if (hooks_.setCursorForced)
        hooks_.setCursorForced(on);
    else
        engine_.cursor().setForced(on);
}

void DialogSystem::delay(uint32_t ms) {
    // 10a8:0000 Delay_ms is a busy wait; the engine's wait keeps presenting so the flash shows.
    // Input arriving meanwhile is queued and discarded by the Input_Flush after the dialog.
    if (hooks_.delayMs)
        hooks_.delayMs(ms);
    else
        engine_.delayMs(ms);
}

// --------------------------------------------------------------------------------- layout

// 10c0:0000 Dlg_Layout
void DialogSystem::layout(const DlgDesc& d) {
    int lines = d.lineCount;
    if (lines == -1) {
        lines = 1;
        for (uint8_t b : d.body)
            if (b == 0x0A || b == 0x0D) ++lines;
    }
    g_.h = lines * kLineStep + 15;
    if (d.type == dlgtype::kCompact) g_.h = lines * kLineStep + 12;
    int titleW = 0;
    if (d.title) {
        g_.h += kTitleH;
        titleW = textWidth(*d.title);
    }
    switch (d.footer) {
        case 0: g_.footer = 0; break;
        case 1: g_.h += kFooterH; g_.footer = dg::kStrFooter1; break;
        case 2: g_.h += kFooterH; g_.footer = dg::kStrFooter2; break;
        default: break;  // keeps the previous footer string, adds no height
    }
    g_.w = titleW;
    int minW = textWidth(dgString(g_.footer));

    DialogButton& b0 = g_.buttons[0];
    DialogButton& b1 = g_.buttons[1];
    // Types 4-7 and 9: big buttons; 8: Yes/No (code_1098-10d8.md §10.2 table).
    auto bigButtons = [&](int extraMinW) {
        minW = extraMinW;
        g_.marginB = 21;
        g_.marginL = 9;
        b0.h = 24;
        b0.w = 192;
        g_.marginT = 7;
        g_.h += 27;
    };
    switch (d.type) {
        case dlgtype::kNonModal:
        case dlgtype::kMessage:
            if (!d.title) {
                g_.marginL = 6;
                g_.marginT = 11;
                g_.marginB = 13;
            } else {
                g_.marginL = 12;
                g_.marginT = 7;
                g_.marginB = 17;
            }
            break;
        case dlgtype::kCompact:
        case dlgtype::kCompact3:
            g_.marginL = 0;
            g_.marginT = 3;
            g_.marginB = 7;
            break;
        case dlgtype::kPagerOne:
            b0.label = dg::kStrBtnDone;
            bigButtons(224);
            break;
        case dlgtype::kPagerFirst:
            b0.label = dg::kStrBtnNextPage;
            bigButtons(224);
            break;
        case dlgtype::kPagerMiddle:
        case dlgtype::kPagerLast:
            b1.label = d.type == dlgtype::kPagerMiddle ? dg::kStrBtnNextPage : dg::kStrBtnDone;
            b0.label = dg::kStrBtnPrevPage;
            b1.w = 192;
            b1.h = 24;
            bigButtons(432);
            break;
        case dlgtype::kYesNo:
            g_.marginL = 12;
            g_.marginB = 7;
            b0.label = dg::kStrBtnNo;
            b1.label = dg::kStrBtnYes;
            b0.w = b1.w = 64;
            b0.h = b1.h = 21;
            minW = 176;
            g_.marginT = 7;
            g_.h += 35;
            break;
        case dlgtype::kDoneCancel:
            b0.label = dg::kStrBtnDone;
            b1.label = dg::kStrBtnCancel;
            b1.w = 211;
            b1.h = 24;
            bigButtons(451);
            break;
        default: break;  // unknown type: margins keep their previous values
    }
    if (g_.w < minW) g_.w = minW;
    g_.h += g_.marginT + g_.marginB;
    int textW = d.textWidth;
    if (textW == -1) textW = textWidth(d.body) + g_.marginL * 2 + 2;
    if (g_.w < textW) g_.w = textW;

    // Signed 16-bit halving truncates toward zero, like C++ int division.
    g_.x = d.x == -1 ? (512 - (g_.w + kFrameExtraW)) / 2 : d.x;
    g_.y = d.y == -1 ? (384 - g_.h) / 2 : d.y;
    g_.titleX = ((g_.w + 12) - titleW) / 2 + g_.x;
    g_.w += kFrameExtraW;
    g_.footerX = ((g_.w - 3) - textWidth(dgString(g_.footer))) / 2 + g_.x;

    const int bottom = g_.y + g_.h;
    switch (d.type) {
        case dlgtype::kNonModal:
        case dlgtype::kMessage:
        case dlgtype::kCompact:
        case dlgtype::kCompact3: g_.footerY = bottom - 29; break;
        case dlgtype::kPagerOne:
        case dlgtype::kPagerFirst:
            b0.x = g_.x + g_.w - 217;
            b0.y = bottom - (b0.h + 10);
            break;
        case dlgtype::kPagerMiddle:
        case dlgtype::kPagerLast:
            b1.x = g_.x + g_.w - 217;
            b0.x = g_.x + 22;
            b0.y = b1.y = bottom - (b0.h + 10);
            break;
        case dlgtype::kYesNo:
            g_.footerY = bottom - 29;
            b0.x = g_.x + g_.w - 89;
            b1.x = g_.x + g_.w - 169;
            b0.y = b1.y = bottom - (b0.h + 38);
            break;
        case dlgtype::kDoneCancel:
            b1.x = g_.x + g_.w - 236;
            b0.x = g_.x + 22;
            b0.y = b1.y = bottom - (b0.h + 10);
            break;
        default: break;
    }
    if (d.type >= dlgtype::kPagerMiddle && d.type <= dlgtype::kDoneCancel)
        b1.labelX = (b1.w - textWidth(dgString(b1.label))) / 2 + b1.x;
    if (d.type >= dlgtype::kPagerOne && d.type <= dlgtype::kDoneCancel)
        b0.labelX = (b0.w - textWidth(dgString(b0.label))) / 2 + b0.x;
}

// ----------------------------------------------------------------------------------- draw

// 10c0:046e Dlg_DrawButton: filled box without its four corner pixels, a notched outline, the
// label centred (inverted colours when highlighted). Not presented.
void DialogSystem::drawButton(int i, bool hi) {
    auto& fb = engine_.frame();
    DialogButton& b = g_.buttons[static_cast<size_t>(i & 1)];
    const int x = b.x, y = b.y, w = b.w, h = b.h;
    fb.setForeColor(hi ? 0 : 15);
    fb.fillRect(x, y + 1, w, h - 2);
    fb.fillRect(x + 1, y, w - 2, h);
    fb.setForeColor(0);
    fb.hline(x + 1, y, w - 2);
    fb.vline(x + 1, y, 2);
    fb.vline(x + w - 2, y, 2);
    fb.hline(x, y + 1, 2);
    fb.hline(x + w - 2, y + 1, 2);
    fb.vline(x, y + 1, h - 2);
    fb.vline(x + w - 1, y + 1, h - 2);
    fb.hline(x, y + h - 2, 2);
    fb.hline(x + w - 2, y + h - 2, 2);
    fb.vline(x + 1, y + h - 2, 2);
    fb.vline(x + w - 2, y + h - 2, 2);
    fb.hline(x + 1, y + h - 1, w - 2);
    fb.setForeColor(hi ? 15 : 0);
    text_.draw(b.labelX, (h - 19) / 2 + y + 1, dgString(b.label));
    b.highlighted = hi;
}

// 10c0:0640 Dlg_ToggleButton
void DialogSystem::toggleButton(int i) {
    const DialogButton& b = g_.buttons[static_cast<size_t>(i & 1)];
    const int x = b.x, y = b.y, w = b.w, h = b.h;
    drawButton(i, !b.highlighted);
    present(x, y, w, h);
}

// 10c0:06a4 Dlg_FlashButton
void DialogSystem::flashButton(int i) {
    for (int n = 0; n < kFlashToggles; ++n) {
        toggleButton(i);
        delay(kFlashDelayMs);
    }
}

// 10c0:06ca Dlg_SaveUnder: Gfx_SaveRect rounds x down and w up to even.
void DialogSystem::saveUnder() { saveBuf_ = engine_.frame().saveRect(g_.x, g_.y, g_.w, g_.h); }

// 10c0:0712 Dlg_RestoreUnder
void DialogSystem::restoreUnder() {
    if (!saveBuf_) return;
    engine_.frame().restoreRect(g_.x, g_.y, *saveBuf_);
    saveBuf_.reset();
    present(g_.x, g_.y, g_.w, g_.h);
}

// 10c0:0764 Dlg_Draw
void DialogSystem::draw(const DlgDesc& d, bool save) {
    auto& fb = engine_.frame();
    const int savedColour = fb.foreColor();
    layout(d);
    if (save) saveUnder();
    const int X = g_.x, Y = g_.y, W = g_.w, H = g_.h;
    const int k = d.type == dlgtype::kCompact ? 0 : 3;

    fb.setForeColor(15);
    fb.fillRect(X, Y, W, H);
    if (d.picture) fb.drawOpaqueOrFlippedWhiteKey(X + 6 + d.picDx, Y + 3 + k + d.picDy, *d.picture, false);
    // Three nested frames; FrameBuffer::frameRect reproduces the WinG bottom-edge quirk.
    fb.setForeColor(0);
    for (int i = 0; i < 3; ++i) fb.frameRect(X + 5 - i, Y + 2 + k - i, W - 13 + 2 * i, H - 10 - k + 2 * i);
    // Drop shadow.
    fb.setForeColor(7);
    for (int i = 0; i < 3; ++i) fb.vline(X + W - 6 + i, Y + 3 + k, H - 6 - k);
    for (int i = 0; i < 3; ++i) fb.hline(X + 6, Y + H - 6 + i, W - 9);

    const int cx = X + 6, cw = W - 15;
    int cy = Y + 3 + k;
    if (d.title) {
        fb.setForeColor(0);
        text_.draw(g_.titleX, cy + 3, *d.title);
        cy += 23;
        fb.setForeColor(0);
        fb.hline(cx, cy, cw);  // title separator
    }
    cy += g_.marginT;
    fb.setForeColor(0);
    text_.drawBox(cx + g_.marginL, cy, W - 25, d.body, {}, false);
    if (g_.footer != 0) {
        fb.setForeColor(0);
        fb.hline(cx, g_.footerY - 2, cw);
        fb.setForeColor(0);
        text_.draw(g_.footerX, g_.footerY, dgString(g_.footer));
    }
    if (hasButtonSeparator(d.type)) {
        fb.setForeColor(0);
        fb.hline(cx, g_.buttons[0].y - 2, cw);
    }
    switch (d.type) {
        case dlgtype::kPagerMiddle:
        case dlgtype::kPagerLast:
        case dlgtype::kYesNo:
        case dlgtype::kDoneCancel: drawButton(1, false); [[fallthrough]];
        case dlgtype::kPagerOne:
        case dlgtype::kPagerFirst:
            drawButton(0, false);
            toggleButton(d.defaultButton & 1);  // highlights the default button
            break;
        default: break;
    }
    present(X, Y, W, H);
    fb.setForeColor(savedColour);
}

// ------------------------------------------------------------------------------ hot spots

// 10c0:0a1c Dlg_AddHotspots
void DialogSystem::addHotspots(const DlgDesc& d) {
    auto& hs = engine_.hotspots();
    if (d.type == dlgtype::kCompact)
        hs.set(dlghot::k44F, 0, 13, 512, 371);
    else
        hs.set(dlghot::k44F, 0, 0, 512, 384);
    if (d.type == dlgtype::kYesNo) {
        const auto& b0 = g_.buttons[0];
        const auto& b1 = g_.buttons[1];
        hs.set(dlghot::k44D, b0.x, b0.y, b0.w, b0.h);
        hs.set(dlghot::k44C, b1.x, b1.y, b1.w, b1.h);
    }
    if (g_.footer != 0) hs.set(dlghot::k44E, g_.x, g_.footerY, g_.w, 10);
}

// 10c0:0aaa Dlg_RemoveHotspots
void DialogSystem::removeHotspots(const DlgDesc& d) {
    auto& hs = engine_.hotspots();
    if (d.type == dlgtype::kYesNo) {
        hs.remove(dlghot::k44D);
        hs.remove(dlghot::k44C);
    }
    if (g_.footer != 0) hs.remove(dlghot::k44E);
    hs.remove(dlghot::k44F);
}

// ------------------------------------------------------------------------- music pausing

// 10d0:0000 Music_PauseNested
void DialogSystem::musicPauseNested() {
    if (hooks_.soundPauseAll) hooks_.soundPauseAll();  // TODO(integration): audio area
    ++musicPauseDepth_;
}

// 10d0:000e Music_ResumeNested
void DialogSystem::musicResumeNested() {
    --musicPauseDepth_;
    if (musicPauseDepth_ < 1) {
        musicPauseDepth_ = 0;
        if (hooks_.soundResumeAll) hooks_.soundResumeAll();  // TODO(integration): audio area
    }
}

// 10d0:0025 Music_ResumeForce
void DialogSystem::musicResumeForce() {
    musicPauseDepth_ = 0;
    if (hooks_.soundResumeAll) hooks_.soundResumeAll();
}

// --------------------------------------------------------------------------- modal loops

// 10c0:0ae5 GameDlg_RunModal
int DialogSystem::runModal(const DlgDesc& d) {
    auto& fb = engine_.frame();
    const bool savedDirty = fb.dirtyEnabled();
    fb.setDirtyEnabled(false);
    const int savedColour = fb.foreColor();
    const bool savedMenu = hooks_.menuIsEnabled ? hooks_.menuIsEnabled() : false;
    if (hooks_.menuEnableAll) hooks_.menuEnableAll(false);
    const bool savedCursor = cursorForced();
    setCursorForced(true);
    musicPauseNested();
    flushInput();

    int sel = d.defaultButton;
    draw(d, true);
    addHotspots(d);

    if (d.type != dlgtype::kMessage && d.type != dlgtype::kYesNo)
        log::warn("GameDlg_RunModal: type {} has no input table (the original never returns); treated as type 1",
                  d.type);
    const bool yesNo = d.type == dlgtype::kYesNo;
    bool done = false;
    while (!done) {
        const uint16_t k = waitInputCode();
        if (!yesNo) {
            // Table at 10c0:0cc6.
            switch (k) {
                case 0x0D:
                case dlghot::k44E:
                case 0x2000 | dlghot::k44E: sel = 0; done = true; break;
                case 0x1B:
                case dlghot::k44F:
                case 0x2000 | dlghot::k44F: sel = -1; done = true; break;
                default: break;
            }
            continue;
        }
        // Table at 10c0:0c72 (21 codes). Button 0 = "No" (right), button 1 = "Yes" (left).
        switch (k) {
            case 0x09:
            case 0x125:
            case 0x126:
            case 0x127:
            case 0x128:  // move the highlight
                toggleButton(sel);
                sel = sel == 0 ? 1 : 0;
                toggleButton(sel);
                break;
            case 0x0D:
                flashButton(sel);
                done = true;
                break;
            case 0x1B:
            case dlghot::k44E:
            case 0x2000 | dlghot::k44E:
                sel = -1;
                done = true;
                break;
            case 'N':
            case 'n':
            case dlghot::k44D:
            case 0x2000 | dlghot::k44D:  // No
                if (sel == 1) toggleButton(1);
                sel = 0;
                flashButton(0);
                done = true;
                break;
            case 'Y':
            case 'y':
            case dlghot::k44C:
            case 0x2000 | dlghot::k44C:  // Yes
                if (sel == 0) toggleButton(0);
                sel = 1;
                flashButton(1);
                done = true;
                break;
            case 0x4000 | dlghot::k44C:
            case 0x8000 | dlghot::k44C:  // pressed on Yes: move the highlight only
                if (sel == 0) {
                    toggleButton(0);
                    sel = 1;
                    toggleButton(1);
                }
                break;
            case 0x4000 | dlghot::k44D:
            case 0x8000 | dlghot::k44D:  // pressed on No
                if (sel != 0) {
                    toggleButton(sel);
                    sel = 0;
                    toggleButton(0);
                }
                break;
            default: break;  // 0x44F (outside the buttons) is ignored
        }
    }

    restoreUnder();
    removeHotspots(d);
    setCursorForced(savedCursor);
    fb.setForeColor(savedColour);
    fb.setDirtyEnabled(savedDirty);
    musicResumeNested();
    flushInput();
    if (hooks_.menuEnableAll) hooks_.menuEnableAll(savedMenu);
    return sel;
}

DlgDesc DialogSystem::pagerPage(const PagerDesc& p, int page) {
    DlgDesc d;
    const int count = p.pageCount;
    if (page == 0) {
        d.type = count == 1 ? dlgtype::kPagerOne : dlgtype::kPagerFirst;
        d.defaultButton = 0;
    } else {
        d.type = page < count - 1 ? dlgtype::kPagerMiddle : dlgtype::kPagerLast;
        d.defaultButton = 1;
    }
    d.title = p.title;
    d.footer = p.footer;
    d.x = p.x;
    d.y = p.y;
    d.textWidth = p.textWidth;
    d.lineCount = p.lineCount;
    const auto idx = static_cast<size_t>(page);
    if (idx < p.texts.size()) d.body = p.texts[idx];
    if (idx < p.pics.size()) d.picture = p.pics[idx];
    if (idx < p.picDx.size()) d.picDx = static_cast<uint8_t>(p.picDx[idx] & 0xFF);
    if (idx < p.picDy.size()) d.picDy = static_cast<uint8_t>(p.picDy[idx] & 0xFF);
    return d;
}

// 10c0:0cde Dlg_RunPager
int DialogSystem::runPager(const PagerDesc& p) {
    auto& fb = engine_.frame();
    auto& hs = engine_.hotspots();
    const bool savedDirty = fb.dirtyEnabled();
    fb.setDirtyEnabled(false);
    const int savedColour = fb.foreColor();
    const bool savedMenu = hooks_.menuIsEnabled ? hooks_.menuIsEnabled() : false;
    if (hooks_.menuEnableAll) hooks_.menuEnableAll(false);
    const bool savedCursor = cursorForced();
    setCursorForced(true);
    hs.set(dlghot::k44E, 0, 0, 512, 384);  // a click outside the box cancels
    musicPauseNested();
    flushInput();

    const int count = p.pageCount;
    int page = 0;
    bool first = true, save = true;
    while (page >= 0 && page < count) {
        const DlgDesc d = pagerPage(p, page);
        int sel = d.defaultButton;
        if (first) {
            layout(d);
            hs.set(dlghot::k44F, g_.x, g_.y, g_.w, g_.h);  // above 0x44E: clicks in the box are inert
            first = false;
        }
        draw(d, save);
        save = false;  // the background is saved once
        hs.set(dlghot::k44C, g_.buttons[0].x, g_.buttons[0].y, g_.buttons[0].w, g_.buttons[0].h);
        if (page > 0) hs.set(dlghot::k44D, g_.buttons[1].x, g_.buttons[1].y, g_.buttons[1].w, g_.buttons[1].h);

        // Table at 10c0:1040 (18 codes).
        bool done = false;
        while (!done) {
            const uint16_t k = waitInputCode();
            const int pressed = static_cast<int>(k & kHotMask) - dlghot::k44C;
            switch (k) {
                case 0x09:
                case 0x125:
                case 0x126:
                case 0x127:
                case 0x128:
                    if (page > 0) {
                        toggleButton(sel);
                        sel = sel == 0 ? 1 : 0;
                        toggleButton(sel);
                    }
                    break;
                case 0x4000 | dlghot::k44C:
                case 0x4000 | dlghot::k44D:
                case 0x8000 | dlghot::k44C:
                case 0x8000 | dlghot::k44D:  // press on a button: move the highlight
                    if (pressed != sel && page > 0) {
                        toggleButton(sel);
                        sel = sel == 0 ? 1 : 0;
                        toggleButton(sel);
                    }
                    break;
                case dlghot::k44C:
                case dlghot::k44D:
                case 0x2000 | dlghot::k44C:
                case 0x2000 | dlghot::k44D:  // release on a button
                    if (pressed != sel) {
                        toggleButton(sel);
                        sel = sel == 0 ? 1 : 0;
                    }
                    flashButton(sel);
                    done = true;
                    break;
                case 0x0D:
                    flashButton(sel);
                    done = true;
                    break;
                case 0x1B:
                case dlghot::k44E:  // 0x244E is absent: a right click outside does nothing
                    sel = -1;
                    done = true;
                    break;
                case 0x121:  // PgUp
                    if (page > 0) {
                        if (sel > 0) toggleButton(sel);
                        sel = 0;
                        flashButton(0);
                        done = true;
                    }
                    break;
                case 0x122:  // PgDn
                    if (page == 0) {
                        sel = 0;
                        flashButton(0);
                        done = true;
                    } else if (page < count - 1) {
                        sel = 1;
                        flashButton(1);
                        done = true;
                    }
                    break;
                default: break;  // 0x44F codes (inside the box, off the buttons) are ignored
            }
        }
        setCursorForced(true);
        hs.remove(dlghot::k44C);
        if (page > 0) hs.remove(dlghot::k44D);
        switch (sel) {
            case -1: page = -1; break;
            case 0: page = page == 0 ? 1 : page - 1; break;  // button 0: next/done on page 0, back elsewhere
            case 1: page = page + 1; break;
            case 2: page = count; break;  // unreachable in the original's table too
            default: break;
        }
    }

    restoreUnder();
    setCursorForced(savedCursor);
    hs.remove(dlghot::k44F);
    hs.remove(dlghot::k44E);
    fb.setForeColor(savedColour);
    fb.setDirtyEnabled(savedDirty);
    musicResumeNested();
    flushInput();
    if (hooks_.menuEnableAll) hooks_.menuEnableAll(savedMenu);
    return page >= 0 ? 1 : 0;
}

}  // namespace tmport::ui
