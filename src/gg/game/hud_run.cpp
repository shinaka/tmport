// The HUD on the game (hud.h, "wave 3: the run"; re/gg/docs/code_vehicles_races.md §4, code_world.md
// §17): Hud_Update with the label return and the flash, the carried-part strip laid out over the
// player's list (the records' strip sprites +0x3a and zones +0x48), the strip commands Input_GetCode
// consumes (5000 + part, 6000..6003), the part on the cursor, the name box. The drawing primitives are
// wave 2's Hud (GgGame::hud, bound to GgState::world.hud); the widgets (zones, the part cursor, the name
// box's text) are WorldObjects'. Owner: world agent.
#include "gg/game/hud.h"

#include "gg/game/actors.h"
#include "gg/game/game.h"
#include "gg/game/parts.h"
#include "gg/game/rooms.h"
#include "gg/game/world.h"
#include "gg/gfx/cursor.h"
#include "gg/gfx/screen.h"
#include "gg/ui/hints.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr int kStripGroup = 4;          // the strip's zones (+0x5c of a record, DS:1330's records)
constexpr int kStripLeft = 0x14;        // the first column's x
constexpr int kStripRowLower = 0xA8;    // y of a lower (or unpaired) part
constexpr int kStripRowUpper = 0xB8;    // y of its upper partner
constexpr int kStripLimit = 0x99;       // the icons' widths + 4 stay below 153 px
constexpr uint8_t kBarColour = 0x16;

HudCounters countersOf(const GgGame& g) {
    return HudCounters{g.player.live.partsNeeded, g.player.live.projectiles, g.player.points};
}

PartRecord* recordOf(GgGame& g, int part) {
    auto& parts = g.world.parts.parts;
    return part >= 0 && static_cast<size_t>(part) < parts.size() ? &parts[static_cast<size_t>(part)] : nullptr;
}

// The upper partner of a lower part (+0x1c with bit 15: the partner's index), -1 none.
int upperOf(const PartRecord& r) { return r.pair != -1 && (r.pair & 0x8000) ? (r.pair & 0x7FFF) : -1; }

// 1a04:0166 Hud_FillRect(sprite): colour 0x16 over the sprite's rectangle.
void hudFillRect(GgGame& g, const Sprite& s) {
    Screen& screen = g.video.screen();
    screen.setDrawColor(kBarColour);
    screen.fillRect(s.x, s.y, s.w, s.h);
}

// Zones_Add/RemoveLocalArray of DS:135c: the "put back" zone alone in room 0 (the workshop), with the
// trash can's elsewhere.
std::span<Zone> carryZones(GgGame& g) {
    WorldObjects& w = g.worldObjects();
    w.loadHudZones(g.exe());
    return std::span<Zone>(w.carryZones).first(g.world.rooms.displayRoom == 0 ? 1 : 2);
}

void playGlobalEffect(GgGame& g, int member) { g.sound.playEffect(g.sound.globalEffects() + member, 0, 5); }

}  // namespace

// 1a04:0588 Hud_DrawStrip: the strip area cleared, every record of the player's list whose strip x is
// not -1 drawn keyed and its zone set from its sprite (Zone_SetFromObject 2bd4:00c1: rectangle,
// priority 10, next NULL) and added to the local list, in list order; the recycle icon and the two
// arrows; the arrow zones DS:1330 added. (DS:132e and DS:874e, written 0 here, are read nowhere.)
void hudDrawStrip(GgGame& g) {
    WorldObjects& w = g.worldObjects();
    w.loadHudZones(g.exe());
    std::vector<HudStripEntry> entries;
    for (const int16_t p : g.actors.playerParts)
        if (const PartRecord* r = recordOf(g, p)) entries.push_back(HudStripEntry{r->hudSprite, p});
    g.hud.drawStrip(g.video.screen(), entries);
    for (const HudStripEntry& e : entries) {
        if (e.sprite.x == -1 || static_cast<size_t>(e.part) >= w.partZones.size()) continue;
        Zone& z = w.partZones[static_cast<size_t>(e.part)];
        z.shape = 0;
        z.x = static_cast<int16_t>(e.sprite.x);
        z.y = static_cast<int16_t>(e.sprite.y);
        z.w = static_cast<int16_t>(e.sprite.w);
        z.h = static_cast<int16_t>(e.sprite.h);
        z.next = nullptr;
        z.priority = 10;
        g.zones().addLocal(z);
    }
    g.zones().addLocalArray(w.stripZones);
}

// 1a04:0b69 Hud_Update(skip)
void hudUpdate(GgGame& g, uint16_t skip) {
    HudState& h = g.world.hud;
    Screen& screen = g.video.screen();
    h.dirty &= static_cast<uint16_t>(~skip);
    // The labels come back once a key was held (g_keyHeldSeen DS:5c1f): a part on the cursor is put
    // down (the strip redrawn), labels and counters drawn at once (and not saved to the back page when
    // nothing else is dirty).
    if (h.labelsShown == 0 && g.input().keyHeldSeen != 0) {
        h.labelsShown = 1;
        if (h.selectedPart != -1) {
            hudCancelCursorPart(g);
            h.dirty |= Hud::kStrip;
        }
        g.hud.drawLabels(screen, 7);
        g.hud.drawCounters(screen, countersOf(g));
    }
    if (h.dirty == 0) return;
    if (h.dirty & Hud::kFlash) {
        if (--h.flashCountdown < 0) {
            h.flashCountdown = h.flashPeriod;
            const PartRecord* a = recordOf(g, h.flashPart);
            const PartRecord* b = recordOf(g, h.flashPartner);
            if (h.flashPhase != 0) {  // Obj_DrawKeyed of the flashed sprites
                if (a) screen.objDrawKeyed(a->hudSprite.flags, a->hudSprite.x, a->hudSprite.y, 0, a->hudSprite.picture);
                if (b) screen.objDrawKeyed(b->hudSprite.flags, b->hudSprite.x, b->hudSprite.y, 0, b->hudSprite.picture);
            } else {
                if (a) hudFillRect(g, a->hudSprite);
                if (b) hudFillRect(g, b->hudSprite);
            }
            h.flashPhase ^= 1;
            // The original also sets DS:1328 to -1 here when it is not: nothing else writes it (-1 in
            // the exe), so it is not modelled.
            if (--h.flashCount < 0) h.dirty &= static_cast<uint16_t>(~Hud::kFlash);
        }
    }
    if (h.dirty & Hud::kFull) {
        g.zones().removeLocalGroup(kStripGroup);
        g.hud.drawBar(screen);
        g.hud.drawLabels(screen, 7);
        g.hud.drawCounters(screen, countersOf(g));
        if (g.world.rooms.displayRoom != 0) hudDrawStrip(g);
        h.dirty = 0;
    }
    if (h.dirty & Hud::kBar) {
        g.hud.drawBar(screen);
        g.hud.drawLabels(screen, 1);
        h.dirty = Hud::kScore;
    }
    if (h.dirty & Hud::kStrip) {
        hudDrawStrip(g);
        h.dirty &= static_cast<uint16_t>(~Hud::kStrip);
    }
    if (h.dirty & Hud::kBananas) {
        playGlobalEffect(g, 5);
        g.hud.drawNumber(screen, 0x11E, 0xB1, static_cast<uint16_t>(g.player.live.projectiles), 3, 0);
        h.dirty &= static_cast<uint16_t>(~Hud::kBananas);
    }
    if (h.dirty & Hud::kInfoText) {
        g.hud.drawInfoText(screen, g.fonts, g.worldObjects().infoText);
        h.dirty &= static_cast<uint16_t>(~Hud::kInfoText);
    }
    if (h.dirty & Hud::kPartsNeeded) {
        g.hud.drawNumber(screen, 0x129, 0xA6, static_cast<uint16_t>(g.player.live.partsNeeded), 2, 1);
        h.dirty &= static_cast<uint16_t>(~Hud::kPartsNeeded);
    }
    if (h.dirty & Hud::kScore) {
        g.hud.drawNumber(screen, 0xFD, 0xBC, g.player.points, 6, 2);
        playGlobalEffect(g, 1);  // every score redraw clicks
        h.dirty &= static_cast<uint16_t>(~Hud::kScore);
    }
    // Rows 165..180 (the bar, the labels, the strip) into the back page, so a sprite erase restores them.
    screen.saveToBack(0, 0xA5, 0x140, 0x10);
}

// 1a04:02c2 Hud_DrawAll
void hudDrawAll(GgGame& g, unsigned bits) { g.hud.drawAll(g.video.screen(), bits); }

// 1a04:0348 Hud_LayoutStrip(anchor, mode): which columns of the player's list are shown and where.
// From the lower part of the anchor's pair (shown, its width counted without the 4-px gap), the lower
// parts before it (unless mode 2) and after it (unless mode 1) are taken alternately, one of each per
// turn, each shown (x 0) while its width + the widths so far stay below 153 px, else hidden (x -1, its
// upper partner too). Then the shown lower parts get x from 20 in list order, 4 px apart, y 168, their
// upper partners the same x and y 184; DS:8758 / DS:8754 = the first and last shown.
void hudLayoutStrip(GgGame& g, int anchorPart, int mode) {
    PartRecord* anchor = recordOf(g, anchorPart);
    if (!anchor) return;
    if (anchor->pair >= 0) {  // an upper part: lay out from its lower partner
        anchor = recordOf(g, anchor->pair);
        if (!anchor) return;
    }
    const auto& list = g.actors.playerParts;
    auto hide = [&](PartRecord& r) {
        r.hudSprite.x = -1;
        if (r.pair != -1)
            if (PartRecord* partner = recordOf(g, r.pair & 0x7FFF)) partner->hudSprite.x = -1;
    };
    anchor->hudSprite.x = 0;
    int width = anchor->hudSprite.w;
    int prev = mode == 2 ? -1 : partListPrevColumn(g, list, anchor->index);
    int next = mode == 1 ? -1 : partListNextColumn(g, list, anchor->index);
    while (prev != -1 || next != -1) {
        if (prev != -1) {
            PartRecord& r = *recordOf(g, prev);
            if (r.hudSprite.w + width < kStripLimit) {
                r.hudSprite.x = 0;
                width += r.hudSprite.w + 4;
            } else {
                hide(r);
            }
            prev = partListPrevColumn(g, list, prev);
        }
        if (next != -1) {
            PartRecord& r = *recordOf(g, next);
            if (r.hudSprite.w + width < kStripLimit) {
                r.hudSprite.x = 0;
                width += r.hudSprite.w + 4;
            } else {
                hide(r);
            }
            next = partListNextColumn(g, list, next);
        }
    }
    HudState& h = g.world.hud;
    int x = kStripLeft;
    h.stripFirst = h.stripLast = -1;
    // From List_Head on through the lower parts (PartList_NextColumn); the head is never an upper part
    // (Actor_RemovePart moves an upper part into its lower partner's place).
    for (int p = list.empty() ? -1 : list.front(); p != -1; p = partListNextColumn(g, list, p)) {
        PartRecord& r = *recordOf(g, p);
        if (r.hudSprite.x != 0) continue;
        r.hudSprite.x = x;
        r.hudSprite.y = kStripRowLower;
        if (h.stripFirst == -1) h.stripFirst = static_cast<int16_t>(p);
        h.stripLast = static_cast<int16_t>(p);
        if (PartRecord* upper = recordOf(g, upperOf(r))) {
            upper->hudSprite.x = x;
            upper->hudSprite.y = kStripRowUpper;
        }
        x += r.hudSprite.w + 4;
    }
}

// 1a04:01fa Hud_StripNeighbour(part): the record that anchors the strip once the part is gone: its
// pair partner, else the next lower part, else the previous one; -1 none.
int hudStripNeighbour(GgGame& g, int part) {
    const PartRecord* r = recordOf(g, part);
    if (!r) return -1;
    if (r->pair != -1) return r->pair & 0x7FFF;
    const int next = partListNextColumn(g, g.actors.playerParts, part);
    return next != -1 ? next : partListPrevColumn(g, g.actors.playerParts, part);
}

// 1a04:0690 Hud_FlashPart(part, pair): the first carried part of the same category (the lower of the
// pair; the original walks the list without an end check) is laid out into view if it is hidden, then
// flashed with its partner when `pair`: 6 toggles (DS:874c = 5), one every DS:8746 + 1 Hud_Update calls.
void hudFlashPart(GgGame& g, int part, bool pair) {
    HudState& h = g.world.hud;
    const int type = partGetType(g, part);
    h.flashCount = 5;
    int q = -1;
    for (const int16_t p : g.actors.playerParts)
        if (partRecord(g, p).category == type) {
            q = p;
            break;
        }
    if (q == -1) {
        log::debug("Hud_FlashPart: the player carries no part of type {} (the original walks past the list)", type);
        return;
    }
    const PartRecord& r = partRecord(g, q);
    if (r.hudSprite.x == -1) {
        g.zones().removeLocalGroup(kStripGroup);
        hudLayoutStrip(g, q, 0);
        h.dirty |= Hud::kStrip;
    }
    h.dirty |= Hud::kFlash;
    h.flashCountdown = h.flashPeriod;
    h.flashPhase = 0;
    h.flashPart = static_cast<int16_t>(q);
    h.flashPartner = pair ? static_cast<int16_t>(r.pair & 0x7FFF) : int16_t{-1};
}

// 1a04:0754 Hud_SetInfoText(text): the name box shows it at the next Hud_Update (bit 0x10).
void hudSetInfoText(GgGame& g, std::span<const uint8_t> text) {
    g.worldObjects().infoText.assign(text.begin(), text.end());
    g.world.hud.dirty |= Hud::kInfoText;
}

// 1a04:076b Hud_AddPart(part): the tail of the player's list (the part Actor_AddPart just appended) gets
// a fresh strip sprite with part's icon (+0x82; size from the picture), x -1, its zone the release code
// 5000 + part and group 4; the strip zones are removed, the tail's name shown and the strip laid out
// from it.
void hudAddPart(GgGame& g, int part) {
    if (g.actors.playerParts.empty()) {
        log::debug("Hud_AddPart({}): the player's list is empty (the original takes List_Tail of it)", part);
        return;
    }
    const int tail = g.actors.playerParts.back();
    g.zones().removeLocalGroup(kStripGroup);
    PartRecord& t = partRecord(g, tail);
    t.hudSprite = Sprite{};
    t.hudSprite.picture = partRecord(g, part).icon;
    const auto& pic = t.hudSprite.picture;  // 15df:0414 Obj_SetSizeFromPicture
    t.hudSprite.w = pic.size() >= 10 ? (pic[6] | (pic[7] << 8)) : 0;
    t.hudSprite.h = pic.size() >= 10 ? (pic[8] | (pic[9] << 8)) : 0;
    t.hudSprite.x = -1;
    WorldObjects& w = g.worldObjects();
    if (static_cast<size_t>(tail) < w.partZones.size()) {
        w.partZones[static_cast<size_t>(tail)].releaseCode = static_cast<uint16_t>(code::kHudPartFirst + part);
        w.partZones[static_cast<size_t>(tail)].group = kStripGroup;
    }
    hudSetInfoText(g, t.name);
    hudLayoutStrip(g, tail, 0);
}

// 1a04:0809 Hud_StripAnchorFor
int hudStripAnchorFor(GgGame& g, int part) { return hudStripNeighbour(g, part); }

// 1a04:0823 Hud_RefreshStrip(anchor): the strip zones removed, a full HUD redraw, the strip laid out
// from the anchor (none: as it is).
void hudRefreshStrip(GgGame& g, int anchorPart) {
    g.zones().removeLocalGroup(kStripGroup);
    g.world.hud.dirty |= Hud::kFull;
    if (anchorPart != -1) hudLayoutStrip(g, anchorPart, 0);
}

// 1a04:084c Hud_ScrollRightArrow (code 6003, the right arrow): unless a part flashes, the strip from
// the lower part before the first shown one on (mode 2), effect 6 of the global bank.
void hudScrollRightArrow(GgGame& g) {
    HudState& h = g.world.hud;
    if (h.stripFirst == -1 || h.flashCount >= 0) return;
    const int q = partListPrevColumn(g, g.actors.playerParts, h.stripFirst);
    if (q == -1) return;
    playGlobalEffect(g, 6);
    g.zones().removeLocalGroup(kStripGroup);
    hudLayoutStrip(g, q, 2);
    h.dirty |= Hud::kStrip;
}

// 1a04:08ad Hud_ScrollLeftArrow (code 6002, the left arrow): the same from the lower part after the last
// shown one backwards (mode 1).
void hudScrollLeftArrow(GgGame& g) {
    HudState& h = g.world.hud;
    if (h.stripLast == -1 || h.flashCount >= 0) return;
    const int q = partListNextColumn(g, g.actors.playerParts, h.stripLast);
    if (q == -1) return;
    playGlobalEffect(g, 6);
    g.zones().removeLocalGroup(kStripGroup);
    hudLayoutStrip(g, q, 1);
    h.dirty |= Hud::kStrip;
}

// 1a04:090e Hud_SelectPart(part) (codes 5000..5100): nothing while a part flashes; a carried part not
// built in goes onto the cursor (g_hudSelectedPart, the icon as the cursor with its hot spot at its
// centre, the icon's place cleared, the pointer kept on the HUD, the carry zones, effect 4); its name
// is shown in either case.
void hudSelectPart(GgGame& g, int part) {
    HudState& h = g.world.hud;
    if (h.flashCount >= 0) return;
    bool carried = false;
    for (const int16_t p : g.actors.playerParts) carried = carried || p == part;
    if (!carried) {
        log::debug("Hud_SelectPart({}): not in the player's list (the original walks past it)", part);
        return;
    }
    PartRecord& r = partRecord(g, part);
    if (!(r.flags & PartRecord::kInstalled)) {
        g.zones().removeLocalGroup(kStripGroup);
        h.selectedPart = static_cast<int16_t>(part);
        WorldObjects& w = g.worldObjects();
        w.partCursors.push_back(CursorRecord::fromPicture(r.hudSprite.picture));  // Mouse_LoadCursors
        Cursor& cursor = g.video.cursor();
        cursor.setCursorPicture(w.partCursors.back().get());
        hudFillRect(g, r.hudSprite);
        cursor.setHotSpot(nullptr, r.hudSprite.w >> 1, r.hudSprite.h >> 1);
        cursor.setBounds(0, 0xA5, 0x13F, 0x22);
        g.zones().addLocalArray(carryZones(g));
        playGlobalEffect(g, 4);
    }
    h.dirty |= Hud::kInfoText;
    g.worldObjects().infoText.assign(r.name.begin(), r.name.end());
}

// The cursor back to the arrow; the part cursors the HUD loaded are freed (Mouse_FreeCursor).
void WorldObjects::restoreCursor(GgGame& g) {
    g.video.cursor().restoreDefaultCursor();
    partCursors.clear();
}

// 1a04:0a58 Hud_CancelCursorPart: the arrow back, the full pointer range, the carry zones removed,
// g_hudSelectedPart = -1.
void hudCancelCursorPart(GgGame& g) {
    g.worldObjects().restoreCursor(g);
    g.video.cursor().setBoundsFull();
    g.zones().removeLocalArray(carryZones(g));
    g.world.hud.selectedPart = -1;
}

// 1a04:0a82 Hud_TrashCursorPart (code 6000, the recycle can): the part on the cursor leaves the player
// (Actor_RemovePart), is flagged 0x4000 (no more points) and placed in a random other room (rand); the
// strip is laid out from its neighbour; a needed type that is no longer carried counts again; a full
// redraw, effect 3, hint 0x1049.
void hudTrashCursorPart(GgGame& g) {
    HudState& h = g.world.hud;
    g.zones().removeLocalGroup(kStripGroup);
    const int part = h.selectedPart;
    if (!recordOf(g, part)) {
        log::debug("Hud_TrashCursorPart: no part on the cursor ({})", part);
        return;
    }
    const int neighbour = hudStripNeighbour(g, part);
    const bool needed = partIsNeeded(g, part);
    const int left = actorRemovePart(g, kPlayerActor, part, true);
    g.worldObjects().restoreCursor(g);
    g.video.cursor().setBoundsFull();
    partRecord(g, part).flags |= PartRecord::kNoPoints;
    itemPlaceInRandomRoom(g, part);
    h.selectedPart = -1;
    g.worldObjects().loadHudZones(g.exe());
    g.zones().removeLocalArray(g.worldObjects().carryZones);
    if (neighbour != -1) hudLayoutStrip(g, neighbour, 0);
    g.input().keyHeldSeen = 1;
    if (left == 0 && needed) ++g.player.live.partsNeeded;
    h.dirty |= Hud::kFull;
    playGlobalEffect(g, 3);
    hintQueue(g, 0x1049, 0, 0);
}

// 1a04:0b5b Hud_PutBackCursorPart (code 6001): Hud_CancelCursorPart and a full redraw.
void hudPutBackCursorPart(GgGame& g) {
    hudCancelCursorPart(g);
    g.world.hud.dirty |= Hud::kFull;
}

// 1a04:0d36 Hud_AddScoreNow(points): Score_Add with the HUD bit, the score drawn and flushed at once
// (the bit stays set: the next Hud_Update draws it again and clicks).
void hudAddScoreNow(GgGame& g, int points) {
    scoreAdd(g, points, 1);
    g.hud.drawNumber(g.video.screen(), 0xFD, 0xBC, g.player.points, 6, 2);
    g.display.flush();
}

// 1a04:0d65 Hud_EscHook (g_escHandler in the warehouse; also called at its entry and exit and by
// Player_HitByChimp and Hint_Show): a part on the cursor is put back and the HUD updated.
void hudEscHook(GgGame& g) {
    if (g.world.hud.selectedPart < 0) return;
    hudPutBackCursorPart(g);
    hudUpdate(g, 0);
}

// Input_GetCode's HUD codes (198d:0106's global table: 6000..6003 and 5000..5100 call the 1a04 commands
// in every state of the original; the codes only occur while the HUD's zones are linked). The world
// installs them at a building's set-up and leaves them; `on` false removes them (a tool, a test).
void hudInstallHooks(GgGame& g, bool on) {
    GlobalKeyHooks& k = g.input().hooks();
    if (!on) {
        k.hudTrash = {};
        k.hudPutBack = {};
        k.hudScrollLeft = {};
        k.hudScrollRight = {};
        k.hudSelectPart = {};
        return;
    }
    k.hudTrash = [&g] { hudTrashCursorPart(g); };
    k.hudPutBack = [&g] { hudPutBackCursorPart(g); };
    k.hudScrollLeft = [&g] { hudScrollLeftArrow(g); };
    k.hudScrollRight = [&g] { hudScrollRightArrow(g); };
    k.hudSelectPart = [&g](uint16_t part) { hudSelectPart(g, part); };
}

}  // namespace tmport::gg
