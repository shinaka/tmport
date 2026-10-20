#pragma once
// Buttons of the TLC UI library (ovl28 4034; re/gg/docs/code_ui.md §5.4): the text buttons of the
// dialog boxes and the picture buttons of the sign-in and the workshop.
//
//   text button (28 bytes, g_dlgButtons DS:bbbc): a 22-byte click zone {0, x, y, w, h, 5, next,
//     key, key, -}, +0x16 far label, +0x1a enabled. Drawn as a rounded rectangle with the label
//     centred in the selected font's colour; the focused button is shown inverted (XOR 0x0F).
//   picture button (32 bytes, the sign-in's DS:20f0/2110/2130): a 22-byte zone, +0x16 bundle id,
//     +0x18 enabled, +0x1a/+0x1c/+0x1e picture-cache entries of the bundle's first three pictures
//     (normal, pressed, disabled; the order is inferred from their use, code_ui.md open question 5).

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "gg/data/resources.h"
#include "gg/input/zones.h"
#include "gg/ui/ui_canvas.h"
#include "text/text_engine.h"

namespace tmport::gg {

class Archive;
class GgExe;

// A dialog button record (Dlg_Layout 3f56:0000 fills them; Btn_* take them by value).
struct DlgButton {
    Zone zone;            // +0x00: shape 0, x, y, w, h, priority 5, press = release = the key
    text::Bytes label;    // +0x16 (the caller keeps the bytes alive)
    bool enabled = true;  // +0x1a: 0 draws the outline in colour 7 (no caller disables one)
};

// 4034:0000 Btn_Draw: colour 15 FillRect(x - 1, y, w + 2, h), (x, y - 1, w, 1), (x, y + h, w, 1);
// the outline in colour 0 (7 when disabled): HLine(x + 1, y, w - 2), HLine(x + 1, y + h - 1, w - 2),
// VLine(x, y + 1, h - 2), VLine(x + w - 1, y + 1, h - 2) and the four inner corner pixels; the label
// with Font_DrawString at (x + half(w - Text_Width), y + half(h - Font_GetHeight) + 1) in the selected
// font's current colour. g_drawColor is left at the outline colour.
void btnDraw(const UiCanvas& ui, const DlgButton& b);
// 4034:016b Btn_ToggleFocus: Gfx_XorRect (x + 1, y + 2, w - 2, h - 4), (x + 2, y + 1, w - 4, 1),
// (x + 2, y + h - 2, w - 4, 1), then Dirty_Flush.
void btnToggleFocus(const UiCanvas& ui, const DlgButton& b);
// 4034:01d6 Btn_Flash: 4 x {Btn_ToggleFocus; Delay(4)}, ending in the starting state. `delay` is
// the runtime's Delay 152c:0072 (n PIT ticks, Input_GetCode inside; empty: no wait).
void btnFlash(const UiCanvas& ui, const DlgButton& b, const std::function<void(int ticks)>& delay);

// The pictures PicCache_LoadBundle (15df:023b) caches for a bundle, in order: every member but the
// palette ('P' ranges, applied to the shadow palette instead), up to a member whose payload starts
// with 0 (the 'L' end marker of the button and scroll-bar bundles).
std::vector<std::span<const uint8_t>> bundlePictures(const Bundle& bundle);

// A picture button.
struct PicButton {
    Zone zone;                 // +0x00
    uint16_t bundleId = 0;     // +0x16
    bool enabled = false;      // +0x18
    Bundle bundle;             // the private copy PicCache_LoadBundle keeps (empty until loaded)
    std::vector<std::span<const uint8_t>> pictures;  // +0x1a.. normal, pressed, disabled (into `bundle`)

    // The record at DS:off of ssg.exe (zone and bundle id; the cache entries are not stored).
    static PicButton fromRecord(const GgExe& exe, uint16_t dsOff);
    // Move-only: the pictures point into the bundle's bytes.
    PicButton() = default;
    PicButton(PicButton&&) noexcept = default;
    PicButton& operator=(PicButton&&) noexcept = default;
    PicButton(const PicButton&) = delete;
    PicButton& operator=(const PicButton&) = delete;

    std::span<const uint8_t> normal() const { return picture(0); }
    std::span<const uint8_t> pressed() const { return picture(1); }
    std::span<const uint8_t> disabled() const { return picture(2); }

private:
    std::span<const uint8_t> picture(size_t i) const { return i < pictures.size() ? pictures[i] : std::span<const uint8_t>{}; }
};

// 4034:0201 PicBtn_Load(x, y, btn): PicCache_LoadBundle(+0x16, +0x1a); enabled = 1; zone x, y; w and h
// from the first picture's header. Throws tmport::FormatError for a bundle without pictures.
void picBtnLoad(const Archive& archive, PicButton& btn, int x, int y);
// 4034:027c PicBtn_Draw: Lpx_DrawKeyed(x, y, enabled ? normal : disabled, 0); no flush.
void picBtnDraw(const UiCanvas& ui, const PicButton& btn);
// 4034:0304 PicBtn_DrawPressed / 4034:032f PicBtn_DrawNormal: the keyed picture, then Dirty_Flush.
void picBtnDrawPressed(const UiCanvas& ui, const PicButton& btn);
void picBtnDrawNormal(const UiCanvas& ui, const PicButton& btn);
// 4034:02c4 PicBtn_Erase: Gfx_RestoreRect(x, y, w, h) from the back page.
void picBtnErase(const UiCanvas& ui, const PicButton& btn);
// 4034:026a PicBtn_Free: PicCache_ReleaseBundle (the pictures are dropped).
void picBtnFree(PicButton& btn);

// 4034:02e4 Btn_AddZone / 4034:02f4 Btn_RemoveZone: Zones_AddLocal / Zones_RemoveLocal of the
// record's zone (the record must stay where it is while linked).
inline void btnAddZone(ClickZones& zones, Zone& zone) { zones.addLocal(zone); }
inline void btnRemoveZone(ClickZones& zones, Zone& zone) { zones.removeLocal(&zone); }

}  // namespace tmport::gg
