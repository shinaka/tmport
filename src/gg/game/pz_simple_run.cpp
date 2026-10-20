// PzSimple's run on the game (pz_simple.h, "wave 3: the run"; ovl20 3938,
// re/gg/docs/code_puzzles_a.md §6.3-§6.6). Owner: puzzles_a agent.
#include "gg/game/pz_simple.h"

#include <algorithm>
#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/hints.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

std::span<const uint8_t> memberOf(const Bundle& b, int i) {
    return i >= 0 && static_cast<size_t>(i) < b.size() ? b.member(static_cast<size_t>(i)).payload : std::span<const uint8_t>{};
}

constexpr uint16_t kReturnCode = 0x44C, kSlotFirst = 0x44D, kSlotLast = 0x454, kHintCode = 0x458, kCheckCode = 0x4B0,
                   kWorkCode = 0x4B1, kGoBack = 0x5DD, kPanel = 0x5DE;
constexpr int kWorkX = 66, kWorkY = 14, kWorkW = 245, kWorkH = 142;
constexpr size_t kMaxHint = 2000;  // the hint buffer (Mem_Alloc(2000), an assert past it)

}  // namespace

// 3938:0000 PzSimple_Run
int PzSimple::run(GgGame& g, int variant) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);  // the set-up yields too (the fades, the hint box)
    g.sound.pzLoadBank(kEffectBank);
    st_->held = kNone;
    loadResources(variant);
    loadVariant(variant);
    cursor_ = loadCursor(archive_, kPuzzleCursor);  // Mouse_LoadCursors(3, 0)
    screenRegion_ = cursorRegionAt(exe_, dg::kSimpleScreenRegion, cursor_.get());
    g.video.cursor().addRegion(&screenRegion_);  // MouseRegion_Add(DS:338e) with the hand
    // 3938:0e76 PzSimple_DrawScreen
    puzzleLoadScreen(g, puzzleKindInfo(1).picture, puzzleKindInfo(1).palette);
    g.video.screen().lpxDrawKeyed(s16(st_->data, 8), s16(st_->data, 10), memberOf(machine_, 0), 0);
    puzzleInitPanel(g, panelText_);
    g.video.screen().saveToBack(0, 0, 320, 200);
    g.display.fadeOutIn();
    buildHintText(g, variant);
    initCursorRegions(g);
    addCursorRegions(g);
    drawInventory(g.video);
    hintShow(g, 0x1052);
    return loop(g, false);
}

// 3938:0075..020a: the main loop and the exit.
int PzSimple::loop(GgGame& g, bool solved) {
    game_ = &g;
    PuzzleUnwindGuard guard(g);
    auto& p = g.puzzles;
    for (bool leave = false; !leave;) {
        const uint16_t code = puzzleGetCode(g);
        if (code >= kSlotFirst && code <= kSlotLast) {
            takeFromInventory(g, code - kSlotFirst);
            continue;
        }
        switch (code) {
        case code::kMenuRequest1:
            p.leave = 2;
            leave = true;
            break;
        case code::kMenuRequest2:
            p.leave = 1;
            leave = true;
            break;
        case kReturnCode: returnHeld(g); break;
        case kWorkCode:
            if (st_->held == kNone) pickUpPlaced(g, g.mouse().x(), g.mouse().y());
            else dropPart(g, g.mouse().x(), g.mouse().y());
            drawPlaced(g.video, true);
            break;
        case kCheckCode:
            g.video.screen().lpxDrawKeyed(10, 150, checkPressed_, 0);
            g.display.flush();
            g.sound.pzPlay(0x12, 5);
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(10, 150, s16(checkPressed_, 6), s16(checkPressed_, 8));
            g.display.flush();
            checkParts(g);
            if (!solved && judgeAndReveal(g)) {
                solved = true;
                puzzleShowSolved(g, 3);  // jingle 3..6
                puzzleAwardPoints(g, 0x16);
            }
            break;
        case kHintCode:
            g.video.screen().lpxDrawKeyed(11, 13, hintPressed_, 0);
            g.display.flush();
            g.sound.pzPlay(0x12, 5);
            puzzleWaitRelease(g);
            g.video.screen().restoreRect(11, 13, s16(hintPressed_, 6), s16(hintPressed_, 8));
            g.display.flush();
            puzzleShowHintText(g, hintText_);
            break;
        case kGoBack:
            g.sound.pzPlay(0x15, 5);
            puzzlePressGoBack(g);
            p.goThrough = 0;
            leave = true;
            break;
        case code::kEnter:
        case kPanel:
            if (solved) {
                p.goThrough = 1;
                leave = true;
            }
            break;
        case code::kEsc: cmdSaveAndExit(g); break;
        default: break;
        }
    }
    g.display.fadeOut();
    cleanup(g);
    g.sound.pzFreeBank();
    game_ = nullptr;
    return solved ? 1 : 0;
}

// 3938:0233 PzSimple_DropPart
void PzSimple::dropPart(GgGame& g, int x, int y) {
    int s = 0;
    while (s < kRecords && st_->placed[static_cast<size_t>(s)].type != kNone) ++s;
    const int held = st_->held;
    const auto pic = memberOf(parts_, held);
    const int w = s16(pic, 6), h = s16(pic, 8);
    if (!(x > 0x41 && y > 0x0D && x + w < 0x138 && y + h < 0x9D)) return;  // outside the area nothing happens
    if (s == kRecords) {
        log::debug("PzSimple_DropPart: an 11th placed part (the original writes past DS:b416)");
        return;
    }
    SimplePlacedRec& slot = st_->placed[static_cast<size_t>(s)];
    slot.type = static_cast<int16_t>(held);
    slot.x = static_cast<int16_t>(x);
    slot.y = static_cast<int16_t>(y);
    slot.w = static_cast<int16_t>(w);
    slot.h = static_cast<int16_t>(h);
    slot.testX = static_cast<int16_t>(x + (held_ ? held_->hotX : 0));  // the held cursor's offset
    slot.testY = static_cast<int16_t>(y + (held_ ? held_->hotY : 0));
    g.video.screen().lpxDrawKeyed(x, y, pic, 0);
    g.display.flush();
    g.sound.pzPlay(1, 3);
    g.video.cursor().setCursorPicture(cursor_.get());
    held_ = nullptr;  // Mouse_FreeCursor (the record stays until the cleanup)
    addCursorRegions(g);
    g.zones().removeLocal(&inventoryZone_);
    st_->held = kNone;
}

// 3938:0345 PzSimple_PickUpPlaced
void PzSimple::pickUpPlaced(GgGame& g, int x, int y) {
    for (int s = kRecords - 1; s >= 0; --s) {  // the topmost first
        SimplePlacedRec& slot = st_->placed[static_cast<size_t>(s)];
        if (slot.type == kNone || !pointInRect(x, y, slot.x, slot.y, slot.w, slot.h)) continue;
        g.sound.pzPlay(0, 3);
        const int type = slot.type;
        auto rec = CursorRecord::fromPicture(memberOf(parts_, type));
        rec->hotX = layout_[static_cast<size_t>(type)].testDx;
        rec->hotY = layout_[static_cast<size_t>(type)].testDy;
        heldRecords_.push_back(std::move(rec));
        held_ = heldRecords_.back().get();
        g.video.cursor().setCursorPicture(held_);
        st_->held = static_cast<int16_t>(type);
        g.zones().addLocal(inventoryZone_);
        g.video.cursor().clearRegions();
        slot.type = kNone;
        return;
    }
}

// 3938:046b PzSimple_PointInRect
bool PzSimple::pointInRect(int px, int py, int x, int y, int w, int h) {
    return !(px < x || x + w <= px || py < y || y + h <= py);
}

// 3938:04a1 PzSimple_InitCursorRegions
void PzSimple::initCursorRegions(GgGame&) {
    for (int t = 0; t < kTypes; ++t) {
        const Zone& z = slotZones_[static_cast<size_t>(t)];
        labelCursors_[static_cast<size_t>(t)] = CursorRecord::fromPicture(memberOf(labels_, t));
        slotRegions_[static_cast<size_t>(t)] = {z.shape, z.x, z.y, z.w, z.h, 0, labelCursors_[static_cast<size_t>(t)].get()};
    }
}

// 3938:0524 PzSimple_AddCursorRegions
void PzSimple::addCursorRegions(GgGame& g) {
    for (int t = 0; t < kTypes; ++t)
        if (count(t) > 0) g.video.cursor().addRegion(&slotRegions_[static_cast<size_t>(t)]);
}

// 3938:054d PzSimple_CheckParts
void PzSimple::checkParts(GgGame& g) {
    g.sound.pzPlay(0x0E, 3);
    for (int s = 0; s < kRecords; ++s) {
        SimplePlacedRec& slot = st_->placed[static_cast<size_t>(s)];
        if (slot.type == kNone) continue;
        const int r = findRecord(s);
        if (r == -1 || recordWord(r, 0x88) == 8) {
            // Bounce back: the part returns to the inventory.
            if (slot.type >= 0 && slot.type < kTypes) st_->data[static_cast<size_t>(slot.type)] = static_cast<uint8_t>(st_->data[static_cast<size_t>(slot.type)] + 1);
            bounceBack(g, s);
            drawInventory(g.video);
        } else {
            const int type = slot.type;
            slot.type = kNone;
            animateFit(g, type, slot.x, slot.y);
            g.video.screen().restoreRect(kWorkX, kWorkY, kWorkW, kWorkH);
            const auto filled = memberOf(machine_, r + 2);  // the record's filled picture
            const int fx = recordWord(r, 0), fy = recordWord(r, 2);
            g.video.screen().lpxDrawKeyed(fx, fy, filled, 0);
            g.video.screen().saveToBack(fx, fy, s16(filled, 6), s16(filled, 8));
            setRecordWord(r, 0x88, 8);
        }
        drawPlaced(g.video, true);
    }
}

// 3938:0671 PzSimple_BounceBack
void PzSimple::bounceBack(GgGame& g, int s) {
    SimplePlacedRec& slot = st_->placed[static_cast<size_t>(s)];
    const int type = slot.type;
    const Layout& l = layout_[static_cast<size_t>(std::clamp(type, 0, kTypes - 1))];
    int x = slot.x, y = slot.y;
    const int dx = (slot.x - l.iconX) / 5, dy = (slot.y - l.iconY) / 5;  // signed, toward zero
    slot.type = kNone;
    drawPlaced(g.video, false);
    g.sound.pzPlay(0x11, 3);
    const auto pic = memberOf(parts_, type);
    for (int i = 0; i < 5; ++i) {
        drawPlaced(g.video, false);
        g.video.screen().lpxDrawKeyed(x, y, pic, 0);
        g.display.flush();
        g.video.screen().restoreRect(x, y, s16(pic, 6), s16(pic, 8));
        g.timers().set(7, 5);
        g.rt.timerWait(7);
        x -= dx;
        y -= dy;
    }
}

// 3938:0750 PzSimple_JudgeAndReveal
bool PzSimple::judgeAndReveal(GgGame& g) {
    for (int r = 0; r < kRecords; ++r)
        if (recordWord(r, 0x88) != 8) return false;
    g.timers().set(7, 100);
    g.rt.timerWait(7);
    Screen& screen = g.video.screen();
    const int mx = s16(st_->data, 8), my = s16(st_->data, 10);
    screen.lpxDrawKeyed(mx, my, memberOf(machine_, 1), 0);  // the finished machine into the back page
    screen.saveToBack(kWorkX, kWorkY, kWorkW, kWorkH);
    screen.lpxDrawKeyed(mx, my, memberOf(machine_, 0), 0);  // the screen shows the machine again
    g.display.flush();
    g.sound.pzPlay(0x13, 5);
    int x = 187, y = 84, w = 1, h = 1;
    for (bool changed = true; changed;) {  // 42 passes, the last rectangle (64, 14, 247, 145)
        screen.restoreRect(x, y, w, h);
        g.display.flush();
        changed = false;
        if (x >= 0x43) {
            x -= 3;
            changed = true;
        }
        if (y > 0x0E) {
            y -= 2;
            changed = true;
        }
        if (w < 0xF5) {
            w += 6;
            changed = true;
        }
        if (h < 0x8E) {
            h += 4;
            changed = true;
        }
        g.timers().set(7, 3);
        g.rt.timerWait(7);
    }
    screen.restoreRect(kWorkX, kWorkY, kWorkW, kWorkH);
    g.display.flush();
    g.sound.pzStop(0x13);
    return true;
}

// 3938:0886 PzSimple_AnimateFit
void PzSimple::animateFit(GgGame& g, int type, int x, int y) {
    const size_t t = static_cast<size_t>(std::clamp(type, 0, kTypes - 1));
    x -= fitDx_[t];
    y -= fitDy_[t];
    g.sound.pzPlayLoop(layout_[t].fitEffect, 7);
    const Bundle& b = partBundles_[t];
    for (size_t k = 1; k < b.size(); ++k) {
        const auto frame = b.member(k).payload;
        drawPlaced(g.video, false);
        g.video.screen().lpxDrawKeyed(x, y, frame, 0);
        g.display.flush();
        g.video.screen().restoreRect(x, y, s16(frame, 6), s16(frame, 8));
        g.timers().set(7, 0x28);
        g.rt.timerWait(7);
    }
    g.sound.pzStop(layout_[t].fitEffect);
}

// 3938:099f PzSimple_ReturnHeld
void PzSimple::returnHeld(GgGame& g) {
    g.sound.pzPlay(1, 3);
    g.zones().removeLocal(&inventoryZone_);
    if (st_->held >= 0 && st_->held < kTypes)
        st_->data[static_cast<size_t>(st_->held)] = static_cast<uint8_t>(st_->data[static_cast<size_t>(st_->held)] + 1);
    g.video.cursor().setCursorPicture(cursor_.get());  // the held record is not freed (keep)
    addCursorRegions(g);
    removeRecordZones(g);
    st_->held = kNone;
    drawInventory(g.video);
}

// 3938:09df PzSimple_TakeFromInventory
void PzSimple::takeFromInventory(GgGame& g, int type) {
    if (count(type) > 0) {
        g.sound.pzPlay(0, 3);
        st_->data[static_cast<size_t>(type)] = static_cast<uint8_t>(st_->data[static_cast<size_t>(type)] - 1);
        auto rec = CursorRecord::fromPicture(memberOf(parts_, type));
        rec->hotX = layout_[static_cast<size_t>(type)].testDx;
        rec->hotY = layout_[static_cast<size_t>(type)].testDy;
        heldRecords_.push_back(std::move(rec));
        held_ = heldRecords_.back().get();
        g.video.cursor().setCursorPicture(held_);
        st_->held = static_cast<int16_t>(type);
        g.zones().addLocal(inventoryZone_);
        g.video.cursor().clearRegions();
    }
    drawInventory(g.video);
}

// 3938:0a64 PzSimple_FindRecord
int PzSimple::findRecord(int s) {
    const SimplePlacedRec& slot = st_->placed[static_cast<size_t>(s)];
    for (int r = 0; r < kRecords; ++r) {
        if (recordWord(r, 0x88) != slot.type) continue;
        for (size_t k = 0; k < 6; ++k) {
            const size_t z = 4 + 22 * k;  // the acceptance rectangle's zone layout
            if (recordWord(r, z) == 2) continue;  // shape 2: unused
            if (pointInRect(slot.testX, slot.testY, recordWord(r, z + 2), recordWord(r, z + 4), recordWord(r, z + 6), recordWord(r, z + 8)))
                return r;
        }
    }
    return -1;
}

// 3938:0af7 PzSimple_RemoveRecordZones
void PzSimple::removeRecordZones(GgGame&) {
    // Zones_RemoveLocal of the acceptance rectangles of the held type: they are never added, so
    // nothing is unlinked (keep: no effect).
}

void PzSimple::buildHint(int variant) {
    hintText_.clear();
    auto add = [&](std::span<const uint8_t> t) {
        hintText_.insert(hintText_.end(), t.begin(), t.end());
        hintText_.push_back('\n');
        hintText_.push_back('\n');
    };
    if (variant >= 0 && variant < 3) add(hintHeader_);
    for (int t = 0; t < kTypes; ++t)
        if (count(t) != 0) add(textBytes(memberOf(hintTexts_, t)));
    if (hintText_.size() >= kMaxHint) log::debug("PzSimple_BuildHintText: the hint is past its buffer (the original asserts)");
    if (hintText_.size() >= 2) hintText_.resize(hintText_.size() - 2);  // the last LF LF removed
}

// 3938:0b59 PzSimple_BuildHintText
void PzSimple::buildHintText(GgGame& g, int variant) {
    buildHint(variant);
    for (int t = 0; t < kTypes; ++t) {
        const auto icon = memberOf(partBundles_[static_cast<size_t>(t)], 0);
        Zone& z = slotZones_[static_cast<size_t>(t)];
        z.shape = 0;
        z.init(static_cast<int16_t>(layout_[static_cast<size_t>(t)].iconX), static_cast<int16_t>(layout_[static_cast<size_t>(t)].iconY),
               s16(icon, 6), s16(icon, 8), 0, static_cast<uint16_t>(kSlotFirst + t), 0);
        z.group = 0;
        g.zones().addLocal(z);
    }
    workZone_ = zoneFromDs(exe_, dg::kSimpleWorkZone);
    hintZone_ = zoneFromDs(exe_, dg::kSimpleHintZone);
    checkZone_ = zoneFromDs(exe_, dg::kSimpleCheckZone);
    inventoryZone_ = zoneFromDs(exe_, dg::kSimpleInventoryZone);
    g.zones().addLocal(workZone_);
    g.zones().addLocal(hintZone_);
    g.zones().addLocal(checkZone_);
}

// 3938:1004 PzSimple_Cleanup
void PzSimple::cleanup(GgGame& g) {
    g.zones().clearLocal();
    g.video.cursor().clearRegions();
    g.video.cursor().setCursorPicture(g.video.cursor().arrow());
    for (auto& c : labelCursors_) c.reset();
    puzzleFreePanel(g);
}

// ---- starting from a DOSBox-X state (puzzles.h)

bool PzSimple::restoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    // 3938:0075 / 007b the main loop's poll (Input_GetCode, Input_StripHeld); the check (0142, 014c), the success (015a, 0162), the hint text
    // (01ba), Go Back (01d0) and Save & Exit (01ec).
    static constexpr uint16_t kMain[] = {0x007A, 0x0080};
    static constexpr uint16_t kSub[] = {0x0145, 0x014F, 0x015F, 0x0167, 0x01BF, 0x01D5, 0x01F1};
    if (!stateAtMainLoop(s, {kMain, kSub, 0x0075, 0x01F7}, why)) return false;
    game_ = &g;
    bind(g.puzzles.simple);
    const int variant = g.puzzles.variant;
    variant_ = variant;
    // The variant copy DS:b1b4 (heap, 0x570 bytes): the counts and the filled records as they are now.
    const auto copy = s.bytesAt(s.farPointerAt(dg::kSimpleData), 0x570);
    if (copy.size() != 0x570) {
        why = "the variant copy DS:b1b4 is not in the state's RAM";
        return false;
    }
    loadResources(variant);
    st_->data.assign(copy.begin(), copy.end());
    g.puzzlesObjects().panel.load(g.archive());
    // The puzzle's effect bank, as the state's RAM holds it (DS:b1b0, the slots DS:b146): without it the
    // slots keep their start-up handles and PzSnd_WaitEnd after a solve waits on the room's music.
    g.sound.pzLoadBank(kEffectBank);
    cursor_ = loadCursor(archive_, kPuzzleCursor);
    screenRegion_ = cursorRegionAt(exe_, dg::kSimpleScreenRegion, cursor_.get());
    buildHint(variant);
    workZone_ = zoneFromDs(exe_, dg::kSimpleWorkZone);
    hintZone_ = zoneFromDs(exe_, dg::kSimpleHintZone);
    checkZone_ = zoneFromDs(exe_, dg::kSimpleCheckZone);
    inventoryZone_ = zoneFromDs(exe_, dg::kSimpleInventoryZone);
    std::vector<std::pair<uint16_t, Zone*>> zones = {{dg::kSimpleWorkZone, &workZone_},
                                                     {dg::kSimpleHintZone, &hintZone_},
                                                     {dg::kSimpleCheckZone, &checkZone_},
                                                     {dg::kSimpleInventoryZone, &inventoryZone_}};
    for (int t = 0; t < kTypes; ++t) {
        const auto off = static_cast<uint16_t>(dg::kSimpleSlotZones + 0x16 * t);
        slotZones_[static_cast<size_t>(t)] = Zone::fromRecord(s.dgroup().subspan(off, Zone::kRecordSize));
        zones.emplace_back(off, &slotZones_[static_cast<size_t>(t)]);
    }
    if (!relinkStateZones(g, s, zones, why)) return false;
    initCursorRegions(g);
    std::vector<CursorRegion*> regions;
    for (const uint16_t off : stateCursorRegions(s)) {
        if (off == 0x0A54) break;
        if (off == dg::kSimpleScreenRegion) {
            regions.push_back(&screenRegion_);
            continue;
        }
        const int rel = off - 0xB41E;  // g_simpleSlotRegions DS:b41e + 0x1c t
        if (rel < 0 || rel % 0x1C != 0 || rel / 0x1C >= kTypes) {
            why = std::format("a cursor region the puzzle does not know (DS:{:04x})", off);
            return false;
        }
        regions.push_back(&slotRegions_[static_cast<size_t>(rel / 0x1C)]);
    }
    g.video.cursor().clearRegions();
    for (auto r = regions.rbegin(); r != regions.rend(); ++r) g.video.cursor().addRegion(*r);
    // The default region's cursor: the arrow (never changed before a part is taken), the hand
    // DS:b41a or the held part DS:b416.
    if (stateSamePointer(s, 0x0A6C, 0xB416) && st_->held >= 0 && st_->held < kTypes) {
        auto rec = CursorRecord::fromPicture(memberOf(parts_, st_->held));
        rec->hotX = layout_[static_cast<size_t>(st_->held)].testDx;
        rec->hotY = layout_[static_cast<size_t>(st_->held)].testDy;
        heldRecords_.push_back(std::move(rec));
        held_ = heldRecords_.back().get();
        restoreCursorPicture(g, s, held_);
    } else if (stateSamePointer(s, 0x0A6C, 0xB41A)) {
        restoreCursorPicture(g, s, cursor_.get());
    } else if (stateSamePointer(s, 0x0A6C, dg::kCursorArrow)) {
        restoreCursorPicture(g, s, g.video.cursor().arrow());
    } else {
        why = "the cursor is neither the arrow, the hand nor the held part";
        return false;
    }
    return true;
}

int PzSimple::resume(GgGame& g) {
    bool solved = false;
    for (const Zone* z = g.zones().local(); z; z = z->next)
        if (z == &g.puzzlesObjects().panelZone) solved = true;
    return loop(g, solved);
}

}  // namespace tmport::gg
