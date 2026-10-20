#include "gg/ui/dialog.h"

#include <algorithm>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/text/text.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

int half(int v) { return v / 2; }  // C '/' on 16-bit ints (CWD; SUB AX,DX; SAR AX,1)

text::Bytes dsText(const GgExe& exe, uint16_t off) { return text::asBytes(exe.dsString(off)); }

// A far string pointer stored at DS:off (it points into DGROUP).
text::Bytes dsFarText(const GgExe& exe, uint16_t off) {
    return text::asBytes(exe.dsString(exe.dsOffsetOf(exe.dsFarPtr(off))));
}

// The u16 a member starts with (an 'I' member's value); nullopt when it is shorter.
std::optional<uint16_t> firstWord(const BundleMember& m) {
    if (m.payload.size() < 2) return std::nullopt;
    return static_cast<uint16_t>(m.payload[0] | (m.payload[1] << 8));
}

}  // namespace

// 3f56:0000 Dlg_Layout
DialogLayout dlgLayout(Fonts& fonts, const DialogDesc& desc) {
    DialogLayout L;
    L.lineH = fonts.height();
    const int lh = L.lineH;
    const text::TextExtent body = textMeasure(fonts, desc.body, 0x118);  // outW -> g_dlgBodyW
    L.bodyW = desc.bodyW != -1 ? desc.bodyW : body.width;
    const int h = desc.bodyH != -1 ? desc.bodyH + 6 : body.height;
    int W = L.bodyW;
    int H = h + 17;
    int titleY = 6, bodyY = 6, btnY = h + 11, footerY = h + 11;
    if (desc.flags & kDlgCompact) {
        H -= 2;
        titleY -= 2;
        bodyY -= 2;
        btnY -= 2;
        footerY -= 2;
    }
    int tw = 0, fw = 0;
    if (desc.title) {
        tw = textWidth(fonts, *desc.title);
        W = std::max(W, tw);
        H += lh + 4;
        bodyY += lh + 4;
        btnY += lh + 4;
        footerY += lh + 4;
    }
    if (desc.footer) {
        fw = textWidth(fonts, *desc.footer);
        W = std::max(W, fw);
        H += lh + 3;
        footerY += 2;
    }
    W += 12;
    L.innerW = W;
    int n = desc.buttonCount();
    if (n > DialogLayout::kMaxButtons) {
        log::warn("Dlg_Layout: {} buttons, only {} fit", n, DialogLayout::kMaxButtons);
        n = DialogLayout::kMaxButtons;
    }
    if (n > 0) {
        int widest = 0;
        for (int i = 0; i < n; ++i) widest = std::max(widest, textWidth(fonts, desc.labels[static_cast<size_t>(i)]));
        L.buttonW = widest + 20;
        L.buttonH = lh + 2;
        L.rowW = n * (L.buttonW + 10) + 10;
        W = std::max(W, L.rowW);
        H += L.buttonH + 8;
        btnY += 4;
        footerY += L.buttonH + 8;
    }
    W += 10;
    L.w = W;
    L.h = H;
    L.x = desc.x == -1 ? half(320 - W) : desc.x;
    L.y = desc.y == -1 ? half(200 - H) : desc.y;
    if (desc.title) {
        L.titleX = L.x + half(W - 2 - tw);
        L.titleY = titleY + L.y;
    }
    L.bodyX = L.x + half(W - 2 - L.bodyW);
    L.bodyY = bodyY + L.y;
    if (desc.footer) L.footerX = L.x + half(W - 2 - fw);
    L.footerY = footerY + L.y;
    if (n > 0) {
        int bx = 0;
        switch (desc.align) {
            case DlgAlign::Left: bx = L.x + 10; break;
            case DlgAlign::Centre: bx = L.x + half(W - 2 - L.rowW) + 10; break;
            case DlgAlign::Right: bx = L.x + W - 6 - n * (L.buttonW + 10); break;
        }
        for (int i = 0; i < n; ++i) {
            const auto key = i < static_cast<int>(desc.keys.size()) ? desc.keys[static_cast<size_t>(i)] : uint16_t{0};
            DlgButton& b = L.buttons[static_cast<size_t>(i)];
            b.zone = Zone::rect(static_cast<int16_t>(bx), static_cast<int16_t>(btnY + L.y), static_cast<int16_t>(L.buttonW),
                                static_cast<int16_t>(L.buttonH), 5, key, key);
            b.label = desc.labels[static_cast<size_t>(i)];
            b.enabled = true;
            bx += L.buttonW + 10;
        }
        L.buttonCount = n;
    }
    if (!desc.pictures.empty()) {
        L.picX = L.x + 4;
        L.picY = L.bodyY - 2;
    }
    L.footerZone = Zone::rect(0, 0, 0, 10, 5, 0x1B, 0x1B);  // DS:3dd4 as stored
    return L;
}

// 3f56:0389 Dlg_Draw
DialogLayout dlgDraw(const UiCanvas& ui, const DialogDesc& desc, SavedImage* saveUnder) {
    Screen& s = ui.screen;
    Fonts& fonts = ui.fonts;
    const uint8_t savedColor = s.drawColor();
    const int prevFont = fonts.select(0);
    DialogLayout L = dlgLayout(fonts, desc);
    if (saveUnder) s.getImage(L.x, L.y, L.w, L.h, *saveUnder);  // 3f56:033e Dlg_SaveUnder
    s.setDrawColor(15);
    s.fillRect(L.x, L.y, L.w, L.h);
    int fx = L.x + 4, fy = L.y + 4, fw = L.w - 10, fh = L.h - 10;
    if (desc.flags & kDlgCompact) {
        fy -= 2;
        fh += 2;
    }
    s.setDrawColor(0);
    for (int i = 0; i < 2; ++i) {
        fw += 2;
        fh += 2;
        --fy;
        --fx;
        s.frameRect(fx, fy, fw, fh);
    }
    s.setDrawColor(7);  // the shadow
    for (int i = 0; i < 2; ++i) s.vLine(fx + fw + i, fy + 2, fh);
    for (int i = 0; i < 2; ++i) s.hLine(fx + 2, fy + fh + i, fw);
    fx += 2;
    fw -= 4;  // the rules: x + 4, width W - 10
    if (desc.title) {
        fonts.setColor(0);
        fonts.drawString(s, L.titleX, L.titleY, *desc.title);
        s.setDrawColor(0);
        s.hLine(fx, L.titleY + L.lineH + 1, fw);
    }
    fonts.setColor(0);
    textDrawBox(s, fonts, L.bodyX, L.bodyY, L.bodyW, desc.body);
    if (desc.footer) {
        s.setDrawColor(0);
        s.hLine(fx, L.footerY - 2, fw);
        fonts.setColor(0);
        fonts.drawString(s, L.footerX, L.footerY, *desc.footer);
    }
    for (const DlgPicture& p : desc.pictures) s.lpxDraw(L.picX + p.dx, L.picY + p.dy, p.lpx);
    if (L.buttonCount > 0) {
        for (int i = 0; i < L.buttonCount; ++i) btnDraw(ui, L.buttons[static_cast<size_t>(i)]);
        const int focus = std::clamp(desc.focus, 0, L.buttonCount - 1);
        if (focus != desc.focus) log::warn("Dlg_Draw: focus {} of {} buttons", desc.focus, L.buttonCount);
        btnToggleFocus(ui, L.buttons[static_cast<size_t>(focus)]);
    }
    ui.dirtyFlush();
    s.setDrawColor(savedColor);
    fonts.select(prevFont);
    return L;
}

// 3f56:0367 Dlg_RestoreUnder
void dlgRestoreUnder(const UiCanvas& ui, const DialogLayout& layout, const SavedImage& saved) {
    ui.screen.putImage(layout.x, layout.y, saved);
    ui.dirtyFlush();
}

// 3f56:05d7 Dlg_AddZones
void dlgAddZones(ClickZones& zones, DialogLayout& layout, const DialogDesc& desc) {
    for (int i = 0; i < layout.buttonCount; ++i) zones.addLocal(layout.buttons[static_cast<size_t>(i)].zone);
    if (desc.footer) {
        layout.footerZone.x = static_cast<int16_t>(layout.x);
        layout.footerZone.y = static_cast<int16_t>(layout.footerY);
        layout.footerZone.w = static_cast<int16_t>(layout.w);
        zones.addLocal(layout.footerZone);
    }
}

// 3f56:061f Dlg_RemoveZones
void dlgRemoveZones(ClickZones& zones, DialogLayout& layout, const DialogDesc& desc) {
    for (int i = 0; i < layout.buttonCount; ++i) zones.removeLocal(&layout.buttons[static_cast<size_t>(i)].zone);
    if (desc.footer) zones.removeLocal(&layout.footerZone);
}

DialogStrings DialogStrings::fromExe(const GgExe& exe) {
    DialogStrings d;
    d.footerGoBack = dsText(exe, dg::kStrFooterGoBack);
    d.footerGoOn = dsText(exe, dg::kStrFooterGoOn);
    for (size_t i = 0; i < d.buttonLabels.size(); ++i)
        d.buttonLabels[i] = dsFarText(exe, static_cast<uint16_t>(dg::kYesNoButtons + 4 * i));
    for (size_t i = 0; i < d.keys.size(); ++i) d.keys[i] = exe.ds16(static_cast<uint16_t>(dg::kDlgKeys + 2 * i));
    for (size_t i = 0; i < d.pagerKeys.size(); ++i)
        d.pagerKeys[i] = exe.ds16(static_cast<uint16_t>(dg::kPagerKeys + 2 * i));
    for (size_t i = 0; i < d.chimpLabels.size(); ++i)
        d.chimpLabels[i] = dsFarText(exe, static_cast<uint16_t>(dg::kChimpButtons + 4 * i));
    d.hintTitle = dsText(exe, dg::kStrNewPlayerHint);
    d.hintFooter = dsText(exe, dg::kStrHintFooter);
    return d;
}

// 3f56:0b1c Dlg_MessageBox
DialogDesc messageBoxDesc(std::optional<text::Bytes> title, text::Bytes body, std::optional<text::Bytes> footer) {
    DialogDesc d;
    d.title = title;
    d.body = body;
    d.footer = footer;
    d.align = DlgAlign::Centre;
    return d;
}

// 3f56:0bb6 Dlg_YesNo
DialogDesc yesNoDesc(const DialogStrings& strings, std::optional<text::Bytes> title, text::Bytes body, bool defYes) {
    DialogDesc d;
    d.title = title;
    d.body = body;
    d.footer = strings.footerGoBack;
    d.labels = {strings.buttonLabels[0], strings.buttonLabels[1]};  // DS:3dea
    d.keys = {strings.keys[0], strings.keys[1]};                    // DS:3e16
    d.focus = defYes ? 0 : 1;
    d.align = DlgAlign::Right;
    return d;
}

// 3f56:0c5e Dlg_AskChimps
OwnedDialog askChimpsDesc(const DialogStrings& strings, const Archive& archive) {
    OwnedDialog o;
    o.bundle = Bundle::load(archive, kChimpsBundle);  // Res_LoadCopy
    o.desc.body = text::untilNul(o.bundle.member(0).payload);
    o.desc.labels = {strings.chimpLabels[0], strings.chimpLabels[1]};  // DS:3e2c
    o.desc.keys = {strings.keys[0], strings.keys[1]};
    o.desc.focus = 0;
    o.desc.align = DlgAlign::Centre;
    o.desc.flags = kDlgEscDisabled;
    return o;
}

// 3f56:0d2a Dlg_MessageRes
OwnedDialog messageResDesc(const DialogStrings& strings, const Archive& archive, uint16_t id) {
    OwnedDialog o;
    o.bundle = Bundle::load(archive, id);  // Res_LoadCopy
    const BundleMember title = o.bundle.member(0);
    const auto titleWord = firstWord(title);
    std::optional<text::Bytes> t;
    if (!titleWord || *titleWord != 0) t = text::untilNul(title.payload);
    const text::Bytes body = text::untilNul(o.bundle.member(1).payload);
    std::optional<uint16_t> flag;
    if (o.bundle.size() > 2) {
        const BundleMember m2 = o.bundle.member(2);
        flag = firstWord(m2);
        // Not 0 or 1: the original's footer pointer is uninitialised (only 0x106F, whose member 2 is a
        // string); use its member 3 instead (PORTING_NOTES.md §10).
        if ((!flag || *flag > 1) && o.bundle.size() > 3 && o.bundle.member(3).resType() == ResType::I) {
            log::debug("Dlg_MessageRes({:04x}): footer flag from member 3", id);
            flag = firstWord(o.bundle.member(3));
        }
    }
    std::optional<text::Bytes> footer;
    if (flag && *flag == 0) footer = strings.footerGoBack;
    else if (flag && *flag == 1) footer = strings.footerGoOn;
    o.desc = messageBoxDesc(t, body, footer);
    return o;
}

// 3f56:0907 Dlg_RunPager: the descriptor of one page
DialogDesc pagerPageDesc(const DialogStrings& strings, int pages, int page, text::Bytes title, text::Bytes text,
                         std::vector<DlgPicture> pictures) {
    DialogDesc d;
    d.title = title;
    d.body = text;
    d.x = 19;
    d.y = 14;
    d.bodyW = 260;
    d.bodyH = 120;
    d.pictures = std::move(pictures);
    const auto& lab = strings.buttonLabels;
    const auto& keys = strings.pagerKeys;
    if (pages == 1) {
        d.labels = {lab[5]};  // DS:3dfe Done
        d.keys = {keys[0]};   // DS:3e20 PgDn
        d.focus = 0;
        d.align = DlgAlign::Right;
    } else if (page == 0) {
        d.labels = {lab[6]};  // DS:3e02 See Next Page
        d.keys = {keys[1]};   // DS:3e22
        d.focus = 0;
        d.align = DlgAlign::Right;
    } else if (page < pages - 1) {
        d.labels = {lab[7], lab[8]};  // DS:3e06 Go Back a Page, See Next Page
        d.keys = {keys[2], keys[3]};  // DS:3e24 PgUp, PgDn
        d.focus = 1;
        d.align = DlgAlign::Centre;
    } else {
        d.labels = {lab[9], lab[10]};  // DS:3e0e Go Back a Page, Done
        d.keys = {keys[4], keys[5]};   // DS:3e28 PgUp, PgDn
        d.focus = 1;
        d.align = DlgAlign::Centre;
    }
    return d;
}

}  // namespace tmport::gg
