#include "gg/ui/button.h"

#include "gg/data/archive.h"
#include "gg/data/gg_exe.h"
#include "gg/text/text.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

int half(int v) { return v / 2; }  // C '/' on 16-bit ints: truncates toward zero (CWD; SUB; SAR)

}  // namespace

// 4034:0000 Btn_Draw
void btnDraw(const UiCanvas& ui, const DlgButton& b) {
    Screen& s = ui.screen;
    const int x = b.zone.x, y = b.zone.y, w = b.zone.w, h = b.zone.h;
    s.setDrawColor(15);
    s.fillRect(x - 1, y, w + 2, h);
    s.fillRect(x, y - 1, w, 1);
    s.fillRect(x, y + h, w, 1);
    s.setDrawColor(b.enabled ? 0 : 7);
    s.hLine(x + 1, y, w - 2);
    s.hLine(x + 1, y + h - 1, w - 2);
    s.vLine(x, y + 1, h - 2);
    s.vLine(x + w - 1, y + 1, h - 2);
    s.hLine(x + 1, y + 1, 1);
    s.hLine(x + 1, y + h - 2, 1);
    s.hLine(x + w - 2, y + 1, 1);
    s.hLine(x + w - 2, y + h - 2, 1);
    const int ly = y + half(h - ui.fonts.height()) + 1;
    const int lx = x + half(w - textWidth(ui.fonts, b.label));
    ui.fonts.drawString(s, lx, ly, b.label);
}

// 4034:016b Btn_ToggleFocus
void btnToggleFocus(const UiCanvas& ui, const DlgButton& b) {
    Screen& s = ui.screen;
    const int x = b.zone.x, y = b.zone.y, w = b.zone.w, h = b.zone.h;
    s.xorRect(x + 1, y + 2, w - 2, h - 4);
    s.xorRect(x + 2, y + 1, w - 4, 1);
    s.xorRect(x + 2, y + h - 2, w - 4, 1);
    ui.dirtyFlush();
}

// 4034:01d6 Btn_Flash
void btnFlash(const UiCanvas& ui, const DlgButton& b, const std::function<void(int)>& delay) {
    for (int i = 0; i < 4; ++i) {
        btnToggleFocus(ui, b);
        if (delay) delay(4);  // 152c:0072 Delay(4)
    }
}

// 15df:023b PicCache_LoadBundle: the members it gives cache entries
std::vector<std::span<const uint8_t>> bundlePictures(const Bundle& bundle) {
    std::vector<std::span<const uint8_t>> pics;
    for (const BundleMember& m : bundle.members()) {
        if (!m.payload.empty() && m.payload[0] == 0) break;
        if (m.resType() == ResType::P && isPaletteRange(m.payload)) continue;
        pics.push_back(m.payload);
    }
    return pics;
}

PicButton PicButton::fromRecord(const GgExe& exe, uint16_t dsOff) {
    PicButton b;
    b.zone = Zone::fromRecord(exe.dsBytes(dsOff, Zone::kRecordSize));
    b.bundleId = exe.ds16(static_cast<uint16_t>(dsOff + 0x16));
    b.enabled = exe.ds16(static_cast<uint16_t>(dsOff + 0x18)) != 0;
    return b;
}

// 4034:0201 PicBtn_Load
void picBtnLoad(const Archive& archive, PicButton& btn, int x, int y) {
    btn.bundle = Bundle::load(archive, btn.bundleId);
    btn.pictures = bundlePictures(btn.bundle);
    if (btn.pictures.empty()) throw FormatError("picture button bundle without pictures");
    btn.enabled = true;
    btn.zone.x = static_cast<int16_t>(x);
    btn.zone.y = static_cast<int16_t>(y);
    const LpxHeader first = parseLpx(btn.pictures[0]);
    btn.zone.w = static_cast<int16_t>(first.width);
    btn.zone.h = static_cast<int16_t>(first.height);
}

// 4034:027c PicBtn_Draw
void picBtnDraw(const UiCanvas& ui, const PicButton& btn) {
    ui.screen.lpxDrawKeyed(btn.zone.x, btn.zone.y, btn.enabled ? btn.normal() : btn.disabled(), 0);
}

// 4034:0304 PicBtn_DrawPressed
void picBtnDrawPressed(const UiCanvas& ui, const PicButton& btn) {
    ui.screen.lpxDrawKeyed(btn.zone.x, btn.zone.y, btn.pressed(), 0);
    ui.dirtyFlush();
}

// 4034:032f PicBtn_DrawNormal
void picBtnDrawNormal(const UiCanvas& ui, const PicButton& btn) {
    ui.screen.lpxDrawKeyed(btn.zone.x, btn.zone.y, btn.normal(), 0);
    ui.dirtyFlush();
}

// 4034:02c4 PicBtn_Erase
void picBtnErase(const UiCanvas& ui, const PicButton& btn) {
    ui.screen.restoreRect(btn.zone.x, btn.zone.y, btn.zone.w, btn.zone.h);
}

// 4034:026a PicBtn_Free
void picBtnFree(PicButton& btn) {
    btn.pictures.clear();
    btn.bundle = Bundle{};
}

}  // namespace tmport::gg
