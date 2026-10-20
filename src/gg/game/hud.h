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

#include "gg/data/dgroup_offsets.h"
#include "gg/gfx/sprites.h"

namespace tmport::gg {

class GgGame;

class Archive;
class Fonts;
class GgExe;
class PicCache;
class Screen;

// The HUD's run-time state (1a04, code_vehicles_races.md §4): the dirty bits and the strip selection
// in DGROUP's initialised part, the flash and the shown counters in BSS. GgState::world.hud holds the
// game's; a Hud draws from the one it is bound to (its own by default: tools and tests).
struct HudState {
    int16_t colorScheme = 0;      // DS:1318 g_hudColorScheme: digit colour scheme in effect
    int16_t selectedPart = -1;    // DS:1326 g_hudSelectedPart: part on the cursor, -1 none
    int16_t labelsShown = 1;      // DS:132a g_hudLabelsShown: 0 while the name box covers the labels
    uint16_t dirty = 0;           // DS:132c g_hudDirty: Hud::Dirty bits
    int16_t flashPeriod = 0;      // DS:8746 (unnamed): calls between flash toggles (3)
    int16_t flashCountdown = 0;   // DS:8748 (unnamed): calls left to the next toggle
    int16_t flashPhase = 0;       // DS:874a (unnamed): 0 clear next, 1 draw next
    int16_t flashCount = -1;      // DS:874c (unnamed): toggles left, -1 no flash (strip commands ignored while >= 0)
    int16_t flashPart = -1;       // engine: the part index whose sprite flashes (DS:8742 far -> its +0x3A)
    int16_t flashPartner = -1;    // engine: its partner (DS:873e far, NULL = -1)
    int16_t stripFirst = -1;      // engine: first shown part of the strip (DS:8758 far -> its record)
    int16_t stripLast = -1;       // engine: last shown part (DS:8754 far)
    int16_t shownPartsNeeded = 0; // DS:8792 (unnamed): values Hud_DrawCounters drew
    int16_t shownProjectiles = 0; // DS:8794 (unnamed)
    uint32_t shownScore = 0;      // DS:8796 (unnamed)

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_hudColorScheme", dg::kHudColorScheme, s.colorScheme);
        v("g_hudSelectedPart", dg::kHudSelectedPart, s.selectedPart);
        v("g_hudLabelsShown", dg::kHudLabelsShown, s.labelsShown);
        v("g_hudDirty", dg::kHudDirty, s.dirty);
        v("DS_8746_hudFlashPeriod", uint16_t{0x8746}, s.flashPeriod);
        v("DS_8748_hudFlashCountdown", uint16_t{0x8748}, s.flashCountdown);
        v("DS_874a_hudFlashPhase", uint16_t{0x874A}, s.flashPhase);
        v("DS_874c_hudFlashCount", uint16_t{0x874C}, s.flashCount);
        v("DS_8792_hudShownPartsNeeded", uint16_t{0x8792}, s.shownPartsNeeded);
        v("DS_8794_hudShownProjectiles", uint16_t{0x8794}, s.shownProjectiles);
        v("DS_8796_hudShownScore", uint16_t{0x8796}, s.shownScore);
    }
};

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

    Hud() = default;
    // Bound to a state that lives elsewhere: not copyable.
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    // The state the HUD draws from and updates: GgState::world.hud in the game (bind), else its own.
    void bind(HudState& state) { st_ = &state; }
    HudState& state() { return *st_; }
    const HudState& state() const { return *st_; }

    // 1a04:025b Hud_Init: the sprite bundle into the cache (its palette member applied), the digit
    // copy and its ten pictures, the colour triples DS:131a, the scheme in effect DS:1318 and the
    // divisors DS:1384 of the user's ssg.exe; the dirty bits 0, the labels shown. (The flash fields are
    // GgState's start-up values, gameInitState.)
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
    // After the bound state's DS:1318 was loaded from elsewhere (a DOSBox-X state's DGROUP, whose digit
    // copy is recoloured to it): the engine's digit pictures recoloured to that scheme.
    void syncDigitColors();
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
    // (178, 173). The zones (5000 + part, DS:1330's arrows) are the game's (hudDrawStrip).
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

    int scheme() const { return st_->colorScheme; }  // DS:1318
    HudCounters shown() const;                        // DS:8792..8798

private:
    std::vector<std::span<const uint8_t>> sprites_;  // DS:875c: labels 0..2, recycle 3, arrow 4
    std::vector<uint8_t> digitCopy_;                  // DS:878e: the 0x105a copy
    std::array<std::span<uint8_t>, 10> digits_{};     // DS:8766
    std::array<std::array<uint8_t, 4>, 3> triples_{}; // DS:131a
    std::array<uint32_t, 7> pow10_{};                 // DS:1384
    int pictureScheme_ = 0;                           // the scheme the digit pictures are recoloured to
    HudState own_;
    HudState* st_ = &own_;
};


// ---- wave 3: the run (owner: world agent; game/hud_run.cpp) -----------------------------------------
// The HUD on the game: GgState::world.hud, GgGame::hud (the pictures), the carried list and the part
// records (the strip), the counters of the player record, the global effects (DS:9dc0) through the
// facade, the zones (HUD strip and carry zones: widgets the world module holds).

// 1a04:0b69 Hud_Update(skip): code_vehicles_races.md §4.2 entirely: the labels' return (g_keyHeldSeen),
// the flash, the full redraw with the strip zones, the bits, the effects (global 5 bananas, 1 score),
// Gfx_SaveToBack(0, 165, 320, 16).
void hudUpdate(GgGame& g, uint16_t skip);
// 1a04:02c2 Hud_DrawAll(bits).
void hudDrawAll(GgGame& g, unsigned bits);
// 1a04:0588 Hud_DrawStrip on the game: Hud::drawStrip of the player's list (the records' strip sprites)
// and the zones: each shown record's (Zone_SetFromObject: its sprite's rectangle, priority 10) and the
// arrows' DS:1330, added to the local list in that order.
void hudDrawStrip(GgGame& g);
// 1a04:0348 Hud_LayoutStrip(anchor part, mode): the strip's x positions around an anchor.
void hudLayoutStrip(GgGame& g, int anchorPart, int mode);
// 1a04:01fa Hud_StripNeighbour(part): the column to show after a part leaves (partner, next lower,
// previous lower), -1 none.
int hudStripNeighbour(GgGame& g, int part);
// 1a04:0690 Hud_FlashPart(part, pair): the held part of that type made visible and flashed 6 toggles.
void hudFlashPart(GgGame& g, int part, bool pair);
// 1a04:0754 Hud_SetInfoText(text): the name box at the next Hud_Update (bit 0x10).
void hudSetInfoText(GgGame& g, std::span<const uint8_t> text);
// 1a04:076b Hud_AddPart(part): the list's new tail gets its strip sprite and zone (5000 + part), its
// name shown, the strip laid out from it.
void hudAddPart(GgGame& g, int part);
int hudStripAnchorFor(GgGame& g, int part);     // 1a04:0809 Hud_StripAnchorFor: Hud_StripNeighbour of a part
void hudRefreshStrip(GgGame& g, int anchorPart); // 1a04:0823 Hud_RefreshStrip: full redraw, a new layout
void hudScrollRightArrow(GgGame& g);            // 1a04:084c Hud_ScrollRightArrow (code 6003)
void hudScrollLeftArrow(GgGame& g);             // 1a04:08ad Hud_ScrollLeftArrow (code 6002)
void hudSelectPart(GgGame& g, int part);        // 1a04:090e Hud_SelectPart (codes 5000..5100)
void hudCancelCursorPart(GgGame& g);            // 1a04:0a58 Hud_CancelCursorPart
void hudTrashCursorPart(GgGame& g);             // 1a04:0a82 Hud_TrashCursorPart (code 6000: rand)
void hudPutBackCursorPart(GgGame& g);           // 1a04:0b5b Hud_PutBackCursorPart (code 6001)
// 1a04:0d36 Hud_AddScoreNow(points): Score_Add, the score drawn at once and flushed (puzzle rewards).
void hudAddScoreNow(GgGame& g, int points);
// 1a04:0d65 Hud_EscHook: puts a part held on the cursor back (Hud_PutBackCursorPart, Hud_Update).
void hudEscHook(GgGame& g);
// Input_GetCode's HUD hooks (6000..6003, 5000..5100): the original's fetch calls the 1a04 commands in
// every state (the codes occur only while the HUD's zones are linked); the world installs them at a
// building's set-up and leaves them. `on` false removes them (tools, tests). The Esc hook
// (g_escHandler = Hud_EscHook) is the warehouse loop's own, set at its entry and cleared at its exit.
void hudInstallHooks(GgGame& g, bool on);

}  // namespace tmport::gg
