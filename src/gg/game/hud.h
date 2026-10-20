#pragma once
// The HUD strip of segment 1a04, the drawing half (re/gg/docs/code_vehicles_races.md §4, the master
// copy; code_world.md §17): rows 165..199 with the bar, the three labels, the counters (parts needed,
// bananas, score), the part-name box, the carried-part strip with its arrows and the recycle icon,
// and Hud_Update's redraw by dirty bits. The strip layout, the strip commands, the flash and the
// zones are wave 3's (they decide which part goes where; this file draws what they decided).
//
// Resources (Hud_Init 1a04:025b): sprite bundle 0x1058 (three labels, the recycle icon, the arrow,
// and a palette member applied at start-up), digit bundle 0x105a (a private copy whose ten pictures
// Hud_SetDigitColors recolours in place from one colour triple of DS:131a to another).

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "gg/gfx/sprites.h"

namespace tmport::gg {

class Archive;
class Fonts;
class GgExe;
class PicCache;
class Screen;

// The values the counters show (g_partsNeeded DS:4582, g_projectiles DS:457e, g_score DS:4572).
struct HudCounters {
    int partsNeeded = 0;
    int projectiles = 0;
    uint32_t score = 0;
};

// One part shown in the strip: its HUD sprite (+0x3A of the part record: x = -1 means not shown)
// and its zone release code 5000 + index (zones are wave 3's).
struct HudStripEntry {
    Sprite sprite;
    int part = 0;
};

class Hud {
public:
    static constexpr uint16_t kSpriteBundle = 0x1058;
    static constexpr uint16_t kDigitBundle = 0x105A;
    static constexpr uint16_t kHudDirtyFull = 0x8000;

    // Dirty bits of g_hudDirty DS:132c (Hud_Update).
    enum Dirty : uint16_t {
        kStrip = 0x0001,
        kPartsNeeded = 0x0002,
        kScore = 0x0004,
        kBananas = 0x0008,
        kInfoText = 0x0010,
        kFlash = 0x0040,
        kBar = 0x4000,
        kFull = 0x8000,
    };

    // 1a04:025b Hud_Init: the sprite bundle into the cache (its palette member applied), the digit
    // copy and its ten pictures, the colour triples DS:131a, the scheme in effect DS:1318 and the
    // divisors DS:1384 of the user's ssg.exe; flash state off.
    void init(const GgExe& exe, const Archive& archive, PicCache& cache);

    // 1a04:00eb Hud_DrawBar: fill (0, 165, 320, 35) colour 0x16, (0, 165, 320, 2) colour 0x17.
    void drawBar(Screen& screen) const;
    // 1a04:011c Hud_ClearInfo: the same over the label column (218, 165, 102, 35).
    void clearInfo(Screen& screen) const;
    // 1a04:018e Hud_DrawLabels(bits): bit 4 "parts needed" (218, 166), 2 "bananas" (218, 177), 1
    // "score" (218, 188), Lpx_Draw (unkeyed).
    void drawLabels(Screen& screen, unsigned bits) const;
    // 1a04:000e Hud_SetDigitColors(scheme): recolours the ten digit pictures from the triple of the
    // scheme in effect to the new one (nothing when it is already in effect).
    void setDigitColors(int scheme);
    // 1a04:0058 Hud_DrawNumber(x, y, value, digits, scheme): the digits left to right 11 px apart
    // (Lpx_Draw), value / 10^(digits-1) ... with u32 division; a value with more digits draws a
    // first "digit" >= 10, which the original takes from past the table (the engine skips it).
    void drawNumber(Screen& screen, int x, int y, uint32_t value, int digits, int scheme);
    // 1a04:02e9 Hud_DrawCounters: parts needed (297, 166) 2 digits scheme 1, bananas (286, 177) 3
    // digits scheme 0, score (253, 188) 6 digits scheme 2; remembers the shown values (DS:8792..).
    void drawCounters(Screen& screen, const HudCounters& c);
    // 1a04:02c2 Hud_DrawAll(bits): the bar, the labels of `bits`, Gfx_SaveToBack(0, 165, 320, 35).
    void drawAll(Screen& screen, unsigned bits);
    // 1a04:0588 Hud_DrawStrip: fill (20, 167, 157, 33) colour 0x16, every entry with x != -1 keyed in
    // list order, the recycle icon at (191, 172), the arrow at (2, 173) and mirrored (mode 2) at
    // (178, 173). TODO(wave 3): the strip zones (5000 + part, DS:1330 arrows) are wave 3's.
    void drawStrip(Screen& screen, std::span<const HudStripEntry> strip) const;
    // 1a04:0a11 Hud_DrawInfoText: font 1, labels hidden (DS:132a = 0), Hud_ClearInfo, colour 0,
    // Text_DrawBox(218, 168, 102, text), the previous font back.
    void drawInfoText(Screen& screen, Fonts& fonts, std::span<const uint8_t> text);
    // 1a04:0b69 Hud_Update(skip), the drawing of the dirty bits other than the flash and the label
    // return (wave 3): bits in `skip` are cleared first; 0x8000 full redraw (bar, labels, counters,
    // the strip when the display room is not 0) clears every bit; 0x4000 bar and score label (then
    // only the score); 1 strip; 8 bananas; 0x10 the name box; 2 parts needed; 4 score; if any bit was
    // set, Gfx_SaveToBack(0, 165, 320, 16). The two sound effects (bits 8 and 4) are the caller's.
    // Returns the bits that were drawn.
    uint16_t update(Screen& screen, uint16_t skip, const HudCounters& c, int displayRoom,
                    std::span<const HudStripEntry> strip, Fonts* fonts = nullptr, std::span<const uint8_t> infoText = {});

    uint16_t dirty = 0;       // g_hudDirty DS:132c
    bool labelsShown = true;  // g_hudLabelsShown DS:132a
    int scheme() const { return scheme_; }
    const HudCounters& shown() const { return shown_; }  // DS:8792..8798

private:
    std::vector<std::span<const uint8_t>> sprites_;  // DS:875c: labels 0..2, recycle 3, arrow 4
    std::vector<uint8_t> digitCopy_;                  // DS:878e: the 0x105a copy
    std::array<std::span<uint8_t>, 10> digits_{};     // DS:8766
    std::array<std::array<uint8_t, 4>, 3> triples_{}; // DS:131a
    std::array<uint32_t, 7> pow10_{};                 // DS:1384
    int scheme_ = 0;                                  // DS:1318
    HudCounters shown_;
};

}  // namespace tmport::gg
