// The workshop and the blueprint viewer's run (workshop.h; ovl08 2dee). Owner: vehicles agent.
#include "gg/game/workshop.h"

#include <algorithm>
#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/actors.h"
#include "gg/game/blueprint.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/game/hud.h"
#include "gg/game/parts.h"
#include "gg/game/rooms.h"
#include "gg/game/vehicles_common.h"
#include "gg/game/world.h"
#include "gg/gfx/lpx.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/hints.h"
#include "gg/ui/ui_canvas.h"
#include "rtl/borland_rand.h"
#include "util/log.h"

namespace tmport::gg {

VehiclesObjects::VehiclesObjects() = default;
VehiclesObjects::~VehiclesObjects() = default;

namespace {

using vehicles::animFrame;
using vehicles::cachedPictureBytes;
using vehicles::freeAnim;
using vehicles::sizeFromPicture;
using vehicles::snd;

constexpr uint16_t kPaintRamps = 0x2B;        // Pal_LoadRes(0x2B): colours 32..63, the 4 paint ramps
constexpr uint16_t kTrophyBundle = 0x1A;      // a trophy per level
constexpr uint16_t kTubeSignBuilding = 0x1F;  // + g_building, keyed at (5, 26)
constexpr uint16_t kTubeSignSector = 0x21;    // + g_tubeRoom, keyed at (104, 25)
constexpr uint16_t kEffectBank = 0x29;
constexpr uint16_t kMusicBank = 0x27;
constexpr uint16_t kTubeAnim = 0x1B;          // sequence 1 the arrival, 0 the departure
constexpr uint16_t kDoorAnim = 0x1D;          // sequence 0 opening, 1 closing
constexpr uint16_t kDoorOpening = 0x26, kDoorClosing = 0x25;
// The conveyor's animation sets as VehiclesObjects::conveyor holds them: DS:a0b8, a0bc, a0c0, a0c4.
constexpr std::array<uint16_t, 4> kConveyorSets = {0x0D, 0x0C, 0x11, 0x0F};
constexpr int kShelfX0 = 160;                 // the first lower part's centre
constexpr int kShelfStep = 100;               // the conveyor's distance between two lower parts
constexpr int kUpperBase = 0x4D, kLowerBase = 0x71;  // the rows' bottom lines (77, 113)
constexpr int kShelfClipLeft = 0x5D, kShelfClipRight = 0xE2;  // the window x 93..226
constexpr int kScrollStep = 4;
constexpr uint16_t kHintBuilt = 0x1053, kHintNotBuilt = 0x1054, kHintRecycled = 0x1049;
constexpr uint16_t kHintParts = 0x1045, kHintPartsDelayed = 0x1046, kHintBlueprint = 0x1043;
constexpr uint16_t kHintPartsDelay = 0x490;
constexpr int kPaintCategory = 11;

// Input codes of the workshop (the zones DS:21ce, DS:21a2, DS:2268; the keys).
namespace wcode {
constexpr uint16_t kTube = 328;     // the tube's zone and the Up key
constexpr uint16_t kLeftKey = 331, kRightKey = 333;
constexpr uint16_t kLeft = 1000, kRight = 1001, kRecycler = 1002, kBlueprint = 1003;
constexpr uint16_t kLowerPart = 1004, kUpperPart = 1005, kWorkbench = 1006, kRaceDoor = 1008, kShelfArea = 1009;
constexpr uint16_t kDone = 1007;
constexpr int kVehiclePart = 4000;  // + part: the installed parts' zones
constexpr int kBlueprintPage = 2000;  // + page: the hot spots
}  // namespace wcode

constexpr int16_t kShelfAreaPriorityIdle = 1, kShelfAreaPriorityCarrying = 10;  // DS:21d8

PartRecord& partOf(GgGame& g, int p) { return g.world.parts.parts.at(static_cast<size_t>(p)); }

// The workshop fields of a part record (+0x5E, +0x8A) and its shelf picture (+0x68).
VehiclesState::ShelfSlot& shelfOf(GgGame& g, int p) {
    auto& shelf = g.vehicles.shelf;
    if (shelf.size() < g.world.parts.count()) shelf.resize(g.world.parts.count());
    return shelf.at(static_cast<size_t>(p));
}
std::span<const uint8_t>& shelfPicOf(GgGame& g, int p) {
    auto& pics = g.vehiclesObjects().shelfPics;
    if (pics.size() < g.world.parts.count()) pics.resize(g.world.parts.count());
    return pics.at(static_cast<size_t>(p));
}

// The click zones of DGROUP's tables (their records are the widgets the lists link).
void buildZones(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    if (o.zonesBuilt) return;
    const GgExe& exe = g.exe();
    auto rec = [&](uint16_t off) { return Zone::fromRecord(exe.dsBytes(off, Zone::kRecordSize)); };
    for (size_t k = 0; k < o.staticZones.size(); ++k)
        o.staticZones[k] = rec(static_cast<uint16_t>(dg::kWorkshopZones + Zone::kRecordSize * k));
    for (size_t k = 0; k < o.shelfZones.size(); ++k)
        o.shelfZones[k] = rec(static_cast<uint16_t>(dg::kShelfZones + dg::kShelfZonesStride * k));
    o.workbench = rec(dg::kWorkbenchZone);
    o.zonesBuilt = true;
}

void restoreDefaultCursor(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    CursorRecord* freed = g.video.cursor().restoreDefaultCursor();  // 17ab:0263 Mouse_RestoreDefaultCursor
    if (freed && freed == o.carriedCursor.get()) o.carriedCursor.reset();
}

// The +0x22 word of a conveyor set alone (2dee:0fb1 / 2dee:1075): the step, the countdown and the
// sprite stay as the old sequence left them.
void setSequenceWord(ScreenAnim& a, int seq) {
    const Sprite keep = a.sprite();
    a.restore(seq, a.stepIndex(), a.countdown(), a.baseX(), a.baseY(), a.state());
    const auto& pics = a.pictures();
    for (size_t k = 0; k < pics.size(); ++k)
        if (keep.picture.data() && pics[k].data() == keep.picture.data()) {
            AnimStep s;
            s.op = static_cast<int16_t>(k);
            s.a = static_cast<int16_t>(keep.x - a.baseX());
            s.b = static_cast<int16_t>(keep.y - a.baseY());
            s.c = keep.flags;
            a.showFrame(s);
            return;
        }
}

// The conveyor's four sets started (2dee:0fc5): a0bc, a0b8, a0c4, a0c0.
void startConveyor(GgGame& g) {
    auto& c = g.vehiclesObjects().conveyor;
    for (const size_t k : {1u, 0u, 3u, 2u})
        if (c[k]) g.screenAnims.start(*c[k]);
}
// ... and stopped at a scroll's end (2dee:08f4): a0bc, a0b8, a0c4, a0c0.
void stopConveyor(GgGame& g) {
    auto& c = g.vehiclesObjects().conveyor;
    for (const size_t k : {1u, 0u, 3u, 2u})
        if (c[k]) g.screenAnims.stop(*c[k]);
}

// The common end of a workshop step of the conveyor (2dee:0880 / 2dee:0986): the motor, 4 px.
int scrollStep(GgGame& g, int dir) {
    VehiclesState& v = g.vehicles;
    const int bank = snd(g).effectBank;
    if (!g.sound.isPlaying(bank + 4) && !g.sound.isPlaying(bank + 5)) g.sound.playEffect(bank + 5, 0, 2);
    const int before = v.scrollLeft;
    v.scrollLeft = static_cast<int16_t>(v.scrollLeft - kScrollStep);
    int moving = dir;
    int step = kScrollStep;
    if (v.scrollLeft < 0) {  // the step after the end: 0 px, the stop effect again (keep)
        v.scrollLeft = 0;
        moving = 0;
        step = before;
    }
    if (v.scrollLeft == 0) {
        stopConveyor(g);
        g.sound.stopEffect(bank + 5);
        g.sound.playEffect(bank + 6, 0, 3);
    }
    for (const int16_t p : g.actors.playerParts) {
        auto& s = shelfOf(g, p).sprite;
        s.x = static_cast<int16_t>(s.x + dir * step);
    }
    return moving;
}

// 2dee:1188..12cf: the end of an install or a paint.
void afterInstall(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    g.world.hud.dirty |= Hud::kFull;
    g.zones().removeLocal(&o.workbench);
    v.carriedRow = -1;
    o.run.vehicleChanged = true;
    o.staticZones[0].priority = kShelfAreaPriorityIdle;
    LiveState& live = g.player.live;
    if (live.partsInstalled == partsNeededCount(g) && live.vehicleBonusGiven == 0) {
        live.vehicleBonusGiven = 1;
        scoreAdd(g, 100);  // once per level
    }
}

// 2dee:0e4a: the tube (or Up): Player_SequenceEnd with no object, to the tube's room through state 8.
void leaveByTube(GgGame& g) {
    g.actors.actors[kPlayerActor].doorObj = -1;
    actorOnEnd(g, kPlayerActor);  // the player's +0x88 callback with 0
    g.player.live.hintCancel1046 = 1;
    g.vehicles.workshopNextState = 8;
    g.vehiclesObjects().run.leave = true;
}

// 2dee:0c89 Workshop_Run's dispatch (switch CS:13ab, 16 codes, then the installed parts).
void workshopDispatch(GgGame& g, uint16_t code) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    WorkshopRunLocals& run = o.run;
    const int bank = snd(g).effectBank;
    switch (code) {
    case 0: return;
    case code::kEsc: cmdSaveAndExit(g); return;  // the Esc hook dropped any carried part already
    case code::kMenuRequest1:                     // File > Return to Technology Center (code 0x17a2)
    case code::kMenuRequest2:                     // Start New Player
        run.leave = true;
        v.workshopNextState = 16;
        g.flow.leaveRequest = code == code::kMenuRequest1 ? 3 : 2;
        return;
    case wcode::kTube: leaveByTube(g); return;
    case wcode::kShelfArea: workshopDropCarried(g); return;
    case wcode::kUpperPart:
        if (run.partner < 0 || (shelfOf(g, run.partner).state & 2)) return;
        o.staticZones[0].priority = kShelfAreaPriorityCarrying;
        v.carriedPart = static_cast<int16_t>(run.partner);
        workshopCarryPart(g, run.partner);
        v.carriedRow = 1;
        v.shelfDirty = 1;
        g.sound.playEffect(bank, 0, 1);
        run.mayInstall = run.current < 0 || !(partOf(g, run.current).flags & PartRecord::kInstalled);
        return;
    case wcode::kLowerPart:
        if (run.current < 0 || (shelfOf(g, run.current).state & 2)) return;
        o.staticZones[0].priority = kShelfAreaPriorityCarrying;
        v.carriedPart = static_cast<int16_t>(run.current);
        workshopCarryPart(g, run.current);
        v.carriedRow = 0;
        v.shelfDirty = 1;
        g.sound.playEffect(bank, 0, 1);
        run.mayInstall = run.partner < 0 || !(partOf(g, run.partner).flags & PartRecord::kInstalled);
        return;
    case wcode::kLeft:
    case wcode::kLeftKey:
    case wcode::kRight:
    case wcode::kRightKey: {
        const bool left = code == wcode::kLeft || code == wcode::kLeftKey;
        if (v.scrollDir != 0 || v.carriedRow != -1) return;
        if (left ? run.index <= 0 : run.index >= run.last) return;
        g.sound.playEffect(bank + 4, 0, 1);
        v.scrollLeft = kShelfStep;
        v.scrollDir = left ? -1 : 1;
        run.index += left ? -1 : 1;
        run.current = static_cast<size_t>(run.index) < run.shelf.size() ? run.shelf[static_cast<size_t>(run.index)] : -1;
        run.partner = run.current >= 0 ? partGetPartner(g, run.current) : -1;
        for (const size_t k : {1u, 0u})  // the gear sets turn the other way
            if (o.conveyor[k]) setSequenceWord(*o.conveyor[k], left ? 0 : 1);
        startConveyor(g);
        v.shelfDirty = 1;
        return;
    }
    case wcode::kRecycler: {
        if (v.carriedRow == -1) return;
        g.sound.playEffect(bank + 2, 0, 1);
        g.zones().removeLocal(&o.workbench);
        restoreDefaultCursor(g);
        const int p = v.carriedPart;
        shelfOf(g, p).state = 2;
        partOf(g, p).flags |= PartRecord::kNoPoints;
        const int anchor = hudStripAnchorFor(g, p);
        const bool needed = partIsNeeded(g, p);
        const int left = actorRemovePart(g, kPlayerActor, p, true);
        hudRefreshStrip(g, anchor);
        itemPlaceInRandomRoom(g, p);  // rand(): a random other room
        g.cache.release(partGetShelfPicId(g, p));
        shelfPicOf(g, p) = {};
        o.staticZones[0].priority = kShelfAreaPriorityIdle;
        if (left == 0 && needed) ++g.player.live.partsNeeded;
        v.carriedRow = -1;
        v.shelfDirty = 1;
        hintShow(g, kHintRecycled);
        g.world.hud.dirty |= Hud::kFull;
        return;
    }
    case wcode::kWorkbench: {
        if (v.carriedRow == -1) return;
        const int p = v.carriedPart;
        PartRecord& part = partOf(g, p);
        if (part.category == kPaintCategory) {
            const int colour = part.variant;
            for (const int16_t k : std::vector<int16_t>(v.vehicleOrder)) {
                if (g.player.live.paintColor != -1) workshopPaintSprite(g, k, g.player.live.paintColor, true);
                g.sound.playEffect(bank + 0xC, 0, 1);
                workshopPaintSprite(g, k, colour, false);
                g.player.vehicleColor[static_cast<size_t>(g.raceIndex())] = static_cast<uint8_t>(colour);
            }
            g.player.live.paintColor = static_cast<int16_t>(colour);
            shelfOf(g, p).state &= static_cast<uint16_t>(~3u);
            restoreDefaultCursor(g);
            v.shelfDirty = 1;
        } else {
            if (!run.mayInstall) return;  // the part stays on the cursor
            restoreDefaultCursor(g);
            shelfOf(g, p).state = 2;
            part.flags |= PartRecord::kInstalled;
            workshopInstallPart(g, p);
            if (part.required != 0) ++g.player.live.partsInstalled;
            g.sound.playEffect(bank + 1, 0, 1);
        }
        afterInstall(g);
        return;
    }
    case wcode::kBlueprint: {
        if (v.carriedRow != -1) return;
        const int r = blueprintRun(g);
        if (r != 0) {
            run.leave = true;
            v.workshopNextState = 16;
            g.flow.leaveRequest = static_cast<int16_t>(r);
        }
        return;
    }
    case wcode::kRaceDoor:
        if (v.carriedRow != -1) return;
        if (g.player.live.partsInstalled == partsNeededCount(g)) {
            run.leave = true;
            v.workshopNextState = 8;
            g.world.vehicleDone = 1;
        } else {
            hintShow(g, kHintNotBuilt);
        }
        return;
    default: break;
    }
    // An installed part (signed: the held codes with bit 15 are negative).
    if (static_cast<int16_t>(code) >= wcode::kVehiclePart && v.carriedRow == -1) {
        o.staticZones[0].priority = kShelfAreaPriorityCarrying;
        const int p = workshopUninstallPart(g, code - wcode::kVehiclePart);
        PartRecord& part = partOf(g, p);
        part.flags &= 0x7FFF;
        v.carriedPart = static_cast<int16_t>(p);
        v.carriedRow = static_cast<int16_t>(part.flags & 0x7FFF);
        workshopCarryPart(g, p);
        run.vehicleChanged = true;
        run.mayInstall = true;
        g.sound.playEffect(bank + 3, 0, 1);
    }
}

}  // namespace

// 2dee:1db5 Workshop_Init
void workshopInit(GgGame& g) {
    g.vehicles.vehicleOrder.clear();  // List_New: the vehicle sprite list
    buildZones(g);
}

// 2dee:1a90 State_06_Workshop
int16_t state06Workshop(GgGame& g, StateEntry entry) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    const GgExe& exe = g.exe();
    Screen& screen = g.video.screen();
    SoundState& s = snd(g);
    buildZones(g);
    if (entry == StateEntry::Normal) {
        g.video.palette().save();                                               // Pal_Save
        g.video.palette().store(parsePaletteRange(g.archive().read(kPaintRamps)));  // Pal_LoadRes(0x2B)
        g.keyboard().setPlayMode(0);
        g.world.inBuilding = 1;
        g.input().setEscHandler([&g] { workshopDropCarried(g); });
        v.raceDoorOpen = 0;
        v.vehicleOrder.clear();  // List_Clear; the pool of 20 records, all free
        for (auto& rec : v.vehiclePool) rec.inUse = 0;
        o.vehiclePics = {};
        // The trophies: one per race won in each building, the level's trophy, 2 px higher each.
        const auto& trophies = g.cache.loadBundle(kTrophyBundle);
        for (int b = 0; b < 3; ++b) {
            int y = exe.dsS16(static_cast<uint16_t>(dg::kTrophyY + 2 * b));
            for (int k = 0; k <= g.player.racesWon[static_cast<size_t>(b)] && k < 5; ++k, y -= 2)
                if (static_cast<size_t>(k) < trophies.size())
                    screen.lpxDrawKeyed(exe.dsS16(static_cast<uint16_t>(dg::kTrophyX + 2 * k)), y, trophies[static_cast<size_t>(k)], 0);
        }
        g.cache.releaseBundle(kTrophyBundle);
        // The tube's signs: the building and the sector the tube leads to.
        screen.lpxDrawKeyed(5, 26, g.cache.read(static_cast<uint16_t>(kTubeSignBuilding + g.building())), 0);
        screen.lpxDrawKeyed(104, 25, g.cache.read(static_cast<uint16_t>(kTubeSignSector + g.player.live.tubeRoom)), 0);
        screen.savePlayArea();
        s.effectBank = static_cast<int16_t>(g.sound.loadEffectBank(kEffectBank));
        g.sound.setEffectBank(s.effectBank);
        s.musicBank = static_cast<int16_t>(g.sound.loadMusicBank(kMusicBank));
        g.sound.workshopMusicStart(s.musicBank);  // g_workshopArrangement 1, the cue count 0, arrangement 1, the cue
        workshopLayoutShelf(g, true, kShelfX0);
        hudUpdate(g, 1);
        workshopDrawShelf(g);
        workshopDrawVehicle(g);
        for (const size_t k : {3u, 2u, 1u, 0u})  // 0x0f, 0x11, 0x0c, 0x0d
            o.conveyor[k] = ScreenAnim::load(g.cache, kConveyorSets[k]);
        g.input().keyHeldSeen = 0;
        g.sound.playEffect(s.effectBank + 10, 0, 5);
        g.rt.delay(15);
        workshopUpdateRaceDoor(g, false);
        g.display.fadeOutIn();
        std::optional<ScreenAnim> tube = ScreenAnim::load(g.cache, kTubeAnim);  // the player arrives through the tube
        tube->setSequence(1);
        g.screenAnims.start(*tube);
        screenAnimPlayModal(g, *tube);
        freeAnim(g, tube);
    }
    workshopRun(g, entry);
    g.input().resetIdleTick();  // DS:1314 = g_ticks
    // Seq_Run(DS:219a, &g_workshopSeqState): entry 0 is NULL, nothing runs (keep).
    workshopReleaseVehiclePics(g);
    for (const size_t k : {0u, 1u, 2u, 3u}) freeAnim(g, o.conveyor[k]);
    if (g.world.vehicleDone == 0) {  // the player leaves by the tube
        std::optional<ScreenAnim> tube = ScreenAnim::load(g.cache, kTubeAnim);
        g.screenAnims.start(*tube);
        screenAnimPlayModal(g, *tube);
        freeAnim(g, tube);
    }
    g.display.fadeOut();
    workshopReleaseShelfPics(g);
    for (size_t k = 0; k < o.vehicleZones.size(); ++k) g.zones().removeLocal(&o.vehicleZones[k]);
    for (auto& rec : v.vehiclePool) rec.inUse = 0;  // Mem_Free(the pool)
    v.vehicleOrder.clear();
    g.zones().clearLocal();
    g.video.palette().restore();  // Pal_Restore
    g.input().setEscHandler({});
    g.sound.freeMusicBank(s.musicBank);
    g.sound.freeEffectBank(s.effectBank);
    return v.workshopNextState;
}

// 2dee:0c89 Workshop_Run
int workshopRun(GgGame& g, StateEntry entry) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    WorkshopRunLocals& run = o.run;
    Screen& screen = g.video.screen();
    buildZones(g);
    if (entry == StateEntry::Normal) {
        const auto& list = g.actors.playerParts;
        run.current = list.empty() ? -1 : list.front();  // List_Head
        run.index = 0;
        run.last = v.shelfCount - 1;
        run.vehicleChanged = false;
        run.mayInstall = false;
        run.leave = false;
        v.shelfDirty = 1;
        v.carriedRow = -1;
        v.scrollDir = 0;
        run.partner = run.current >= 0 ? partGetPartner(g, run.current) : -1;
        o.staticZones[0].priority = kShelfAreaPriorityIdle;
        g.zones().addLocalArray(o.staticZones);
        if (run.current >= 0) workshopSetShelfZones(g, run.current);
        run.shelf = workshopListShelfParts(g);
        hintQueue(g, kHintPartsDelayed, kHintPartsDelay, dg::kHintCancel1046);
        if (g.actors.actors[kPlayerActor].carried != 0) hintShow(g, kHintParts);
        g.video.sprites().reset();
    }
    // A resumed run goes on with the input of the frame the state was saved in; inside an open blueprint
    // with the viewer's loop, whose end is the dispatch of code 1003's.
    bool inputFirst = entry == StateEntry::Resume;
    if (inputFirst && v.blueprintOpen != 0 && o.blueprint) {
        if (const int r = blueprintRun(g, StateEntry::Resume); r != 0) {
            run.leave = true;
            v.workshopNextState = 16;
            g.flow.leaveRequest = static_cast<int16_t>(r);
        }
        inputFirst = false;
    }
    for (;;) {
        if (!inputFirst) {
            if (run.leave) {
                g.zones().removeLocalGroup(5);
                workshopDropCarried(g);
                return -1;
            }
            hintUpdate(g);
            g.timers().set(0, 4);
            if (v.shelfDirty) workshopRestoreShelfBack(g);
            roomDrawAnimLayer(g, 0);  // room 0's animated objects
            animFrame(g);             // the conveyor
            if (v.scrollDir == 1) {
                v.shelfDirty = 1;
                v.scrollDir = static_cast<int16_t>(workshopScrollStepRight(g));
            }
            if (v.scrollDir == -1) {
                v.shelfDirty = -1;
                v.scrollDir = static_cast<int16_t>(workshopScrollStepLeft(g));
            }
            if (v.shelfDirty) {
                workshopDrawShelf(g);
                v.shelfDirty = v.scrollDir;
                if (v.scrollDir == 0 && run.current >= 0) workshopSetShelfZones(g, run.current);
            }
            if (run.vehicleChanged) {
                screen.restoreRect(72, 117, 180, 48);
                workshopDrawVehicle(g);
                run.vehicleChanged = false;
            }
            hudUpdate(g, 1);
            g.display.flush();
            if (g.player.live.vehicleBonusGiven != 0) hintShow(g, kHintBuilt);  // shown once (hint flags)
            workshopUpdateRaceDoor(g, true);
            g.rt.timerWait(0);
        }
        inputFirst = false;
        // The first frame of a level opens the blueprint without input.
        const uint16_t code = g.player.live.blueprintSeen == 0 ? wcode::kBlueprint : g.rt.getInputCode();
        workshopDispatch(g, code);
    }
}

// 2dee:00a6 Workshop_UpdateRaceDoor
void workshopUpdateRaceDoor(GgGame& g, bool sound) {
    VehiclesState& v = g.vehicles;
    const bool built = g.player.live.partsInstalled == partsNeededCount(g);
    uint16_t picture = 0;
    if (v.raceDoorOpen == 0) {
        if (built) picture = kDoorOpening;
    } else if (!built) {
        picture = kDoorClosing;
    }
    if (picture == 0) return;
    Screen& screen = g.video.screen();
    screen.lpxDrawKeyed(306, 80, g.cache.read(picture), 0);  // Pic_Read
    std::optional<ScreenAnim> door = ScreenAnim::load(g.cache, kDoorAnim);
    g.screenAnims.start(*door);
    door->setSequence(v.raceDoorOpen);
    v.raceDoorOpen ^= 1;
    const int bank = snd(g).effectBank;
    if (sound) g.sound.playEffect(bank + 0xD, 0, 3);
    screenAnimPlayModal(g, *door);
    if (sound && v.raceDoorOpen == 0) g.sound.playEffect(bank + 0xE, 0, 4);
    freeAnim(g, door);
    screen.saveToBack(299, 80, 21, 85);  // the door area becomes part of the back page
}

// 2dee:01a0 Workshop_PaintSprite
void workshopPaintSprite(GgGame& g, int poolIndex, int colour, bool undo) {
    if (poolIndex < 0 || poolIndex >= static_cast<int>(g.vehicles.vehiclePool.size())) return;
    const PartCatalogue& cat = g.world.parts;
    const int q = g.vehicles.vehiclePool[static_cast<size_t>(poolIndex)].part;
    const int width = cat.paintWidth;
    // The paint ramp of the colour (each entry twice for 14-colour parts) and the part's source colours.
    std::vector<uint8_t> paint, source;
    const int c = colour % 4;
    for (int t = 0; t < 7; ++t) {
        const auto e = static_cast<uint8_t>(c * 7 + t + 0x20);
        paint.push_back(e);
        if (width == 2) paint.push_back(e);
    }
    paint.push_back(0xFF);
    const auto group = static_cast<size_t>(q / 4);
    if (q < 0 || group >= cat.paintable.size() || cat.paintable[group] == 0) return;
    const uint16_t base = cat.paintBase[static_cast<size_t>(q % 4)];
    for (int t = 0; t < 7 * width; ++t) source.push_back(static_cast<uint8_t>(base + t));
    source.push_back(0xFF);
    const auto pic = g.vehiclesObjects().vehiclePics[static_cast<size_t>(poolIndex)];
    if (pic.empty()) return;
    // Gfx_RecolorPicture(pic, from, to) on the cached picture: every sprite of it changes (keep).
    if (undo) recolorPicture(cachedPictureBytes(pic), paint, source);
    else recolorPicture(cachedPictureBytes(pic), source, paint);
}

// 2dee:028f Workshop_LayoutShelf
void workshopLayoutShelf(GgGame& g, bool load, int x0) {
    VehiclesState& v = g.vehicles;
    v.shelfCount = 0;
    g.video.palette().store(parsePaletteRange(g.archive().read(g.world.parts.workshopPalette)));  // Pal_LoadRes(+0x08)
    for (const int16_t p : g.actors.playerParts) {
        auto& slot = shelfOf(g, p);
        PartRecord& part = partOf(g, p);
        slot.state = 0;
        slot.sprite.flags = 0;
        if (load) shelfPicOf(g, p) = g.cache.load(part.shelfPicture);
        sizeFromPicture(shelfPicOf(g, p), slot.sprite.w, slot.sprite.h);
        if ((static_cast<uint16_t>(part.pair) & 0x8000) == 0) {  // the upper part: above its partner
            slot.sprite.y = static_cast<int16_t>(kUpperBase - slot.sprite.h);
            part.flags |= 1;
            slot.sprite.x = shelfOf(g, part.pair).sprite.x;
        } else {  // a lower or single part: 100 px apart on the conveyor
            slot.sprite.x = static_cast<int16_t>(x0 - (slot.sprite.w >> 1));
            slot.sprite.y = static_cast<int16_t>(kLowerBase - slot.sprite.h);
            part.flags &= static_cast<uint16_t>(~1u);
            x0 -= kShelfStep;
            ++v.shelfCount;
        }
        if (load && (part.flags & PartRecord::kInstalled)) {
            workshopInstallPart(g, p);
            slot.state |= 2;
        }
    }
}

// 2dee:03b2 Workshop_ReleaseShelfPics
void workshopReleaseShelfPics(GgGame& g) {
    for (const int16_t p : g.actors.playerParts) {
        g.cache.release(partOf(g, p).shelfPicture);
        shelfPicOf(g, p) = {};
    }
}

// 2dee:0401 Workshop_CompareLayer
int workshopCompareLayer(int layerA, int layerB) { return layerB - layerA; }

// 2dee:0414 Workshop_EnableVehicleZones
void workshopEnableVehicleZones(GgGame& g, bool on) {
    VehiclesObjects& o = g.vehiclesObjects();
    for (const int16_t k : g.vehicles.vehicleOrder) {
        Zone& z = o.vehicleZones[static_cast<size_t>(k)];
        if (on) g.zones().addLocal(z);
        else g.zones().removeLocal(&z);
    }
}

// 2dee:046d Workshop_InstallPart
void workshopInstallPart(GgGame& g, int p) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    const PartRecord& part = partOf(g, p);
    int free = 0;
    int k = -1;
    for (int j = 0; j < part.instances && j < 4; ++j) {
        for (; free < static_cast<int>(v.vehiclePool.size()); ++free)
            if (v.vehiclePool[static_cast<size_t>(free)].inUse == 0) {
                k = free;
                break;
            }
        if (k < 0 || v.vehiclePool[static_cast<size_t>(k)].inUse) {  // the original reuses the last record
            log::warn("Workshop_InstallPart: no free vehicle sprite for part {}", p);
            return;
        }
        VehicleSpriteRec& rec = v.vehiclePool[static_cast<size_t>(k)];
        const auto jj = static_cast<size_t>(j);
        rec.layer = part.instLayer[jj];
        rec.sprite.flags = 0;
        rec.inUse = 1;
        rec.sprite.x = part.instX[jj];
        rec.sprite.y = part.instY[jj];
        o.vehiclePics[static_cast<size_t>(k)] = g.cache.load(part.vehiclePicture);
        rec.part = static_cast<int16_t>(p);
        auto& colour = g.player.vehicleColor[static_cast<size_t>(g.raceIndex())];
        if (g.player.live.paintColor == -1) {
            if (p < 4) colour = part.bodyColour;  // a body part's default colour
        } else {
            workshopPaintSprite(g, k, g.player.live.paintColor, false);
            colour = static_cast<uint8_t>(g.player.live.paintColor);
        }
        sizeFromPicture(o.vehiclePics[static_cast<size_t>(k)], rec.sprite.w, rec.sprite.h);
        Zone& z = o.vehicleZones[static_cast<size_t>(k)];
        z = Zone::rect(rec.sprite.x, rec.sprite.y, rec.sprite.w, rec.sprite.h, static_cast<int16_t>(rec.layer + 20), 0,
                       static_cast<uint16_t>(wcode::kVehiclePart + p));
        z.group = 6;
        g.zones().addLocal(z);
        // List_InsertSorted with Workshop_CompareLayer: before the first record of a larger layer.
        auto& order = v.vehicleOrder;
        auto at = std::find_if(order.begin(), order.end(), [&](int16_t i) {
            return workshopCompareLayer(rec.layer, v.vehiclePool[static_cast<size_t>(i)].layer) > 0;
        });
        order.insert(at, static_cast<int16_t>(k));
    }
}

// 2dee:060f Workshop_UninstallPart
int workshopUninstallPart(GgGame& g, int p) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    const PartRecord& part = partOf(g, p);
    if (part.required != 0) --g.player.live.partsInstalled;
    for (const int16_t k : std::vector<int16_t>(v.vehicleOrder)) {
        VehicleSpriteRec& rec = v.vehiclePool[static_cast<size_t>(k)];
        if (rec.part != p) continue;
        // A paint on the shelf's own picture is undone first (the shelf shows it); a separate vehicle
        // picture is only released.
        if (g.player.live.paintColor != -1 && part.vehiclePicture == part.shelfPicture)
            workshopPaintSprite(g, k, g.player.live.paintColor, true);
        rec.inUse = 0;
        g.zones().removeLocal(&o.vehicleZones[static_cast<size_t>(k)]);
        std::erase(v.vehicleOrder, k);
        g.cache.release(part.vehiclePicture);
        o.vehiclePics[static_cast<size_t>(k)] = {};
    }
    return p;
}

// 2dee:070a Workshop_DrawVehicle
void workshopDrawVehicle(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    for (const int16_t k : v.vehicleOrder) {
        VehicleSpriteRec& rec = v.vehiclePool[static_cast<size_t>(k)];
        auto& pic = o.vehiclePics[static_cast<size_t>(k)];
        if (pic.empty()) {  // released at the last exit: loaded again
            pic = g.cache.load(partOf(g, rec.part).vehiclePicture);
            sizeFromPicture(pic, rec.sprite.w, rec.sprite.h);
        }
        g.video.screen().objDrawKeyed(rec.sprite.flags, rec.sprite.x, rec.sprite.y, 0, pic);  // Obj_DrawKeyed
    }
}

// 2dee:0797 Workshop_ReleaseVehiclePics
void workshopReleaseVehiclePics(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    for (const int16_t k : g.vehicles.vehicleOrder) {
        auto& pic = o.vehiclePics[static_cast<size_t>(k)];
        if (pic.empty()) continue;
        g.cache.release(partOf(g, g.vehicles.vehiclePool[static_cast<size_t>(k)].part).vehiclePicture);
        pic = {};
    }
}

// 2dee:0804 Workshop_DrawShelf
void workshopDrawShelf(GgGame& g) {
    Screen& screen = g.video.screen();
    ClipRect clip = screen.clip();
    clip.left = kShelfClipLeft;  // g_clipLeft / g_clipRight DS:01e4 / 01e6
    clip.right = kShelfClipRight;
    screen.setClip(clip);
    for (const int16_t p : g.actors.playerParts) {
        const auto& slot = shelfOf(g, p);
        if ((partOf(g, p).flags & PartRecord::kInstalled) || (slot.state & 3)) continue;
        screen.objDrawKeyed(slot.sprite.flags, slot.sprite.x, slot.sprite.y, 0, shelfPicOf(g, p));
    }
    clip.left = 0;
    clip.right = 319;
    screen.setClip(clip);
}

// 2dee:0880 Workshop_ScrollStepLeft
int workshopScrollStepLeft(GgGame& g) { return scrollStep(g, -1); }

// 2dee:0986 Workshop_ScrollStepRight
int workshopScrollStepRight(GgGame& g) { return scrollStep(g, 1); }

// 2dee:0a8c Workshop_RestoreShelfBack
void workshopRestoreShelfBack(GgGame& g) {
    Screen& screen = g.video.screen();
    screen.restoreRect(0x5C, 0x2D, 0x88, 0x20);
    screen.restoreRect(0x5C, 0x52, 0x88, 0x1F);
}

// 2dee:0ab3 Workshop_SetShelfZones
void workshopSetShelfZones(GgGame& g, int p) {
    if (p < 0) return;
    VehiclesObjects& o = g.vehiclesObjects();
    g.zones().removeLocalArray(o.shelfZones);
    auto place = [&](Zone& z, int part) {
        const auto& s = shelfOf(g, part).sprite;
        z.x = s.x;
        z.y = s.y;
        z.w = s.w;
        z.h = s.h;
    };
    place(o.shelfZones[0], p);
    size_t n = 1;
    if (partOf(g, p).pair != -1) {
        place(o.shelfZones[1], partOf(g, p).pair & 0x7FFF);
        n = 2;
    }
    g.zones().addLocalArray(std::span<Zone>(o.shelfZones).first(n));
}

// 2dee:0b3d Workshop_CarryPart
void workshopCarryPart(GgGame& g, int p) {
    VehiclesObjects& o = g.vehiclesObjects();
    PartRecord& part = partOf(g, p);
    auto& slot = shelfOf(g, p);
    hudSetInfoText(g, part.name);
    slot.state |= 1;
    // Mouse_LoadCursors of the shelf picture, its hot spot at the centre.
    auto rec = CursorRecord::fromPicture(shelfPicOf(g, p));
    Cursor& cursor = g.video.cursor();
    cursor.setCursorPicture(rec.get());
    o.carriedCursor = std::move(rec);
    cursor.setHotSpot(nullptr, slot.sprite.w >> 1, slot.sprite.h >> 1);
    cursor.setBounds(0, 9, 319, 154);
    g.zones().addLocal(o.workbench);
}

// 2dee:0bb2 Workshop_ListShelfParts
std::vector<int> workshopListShelfParts(GgGame& g) {
    std::vector<int> out;
    for (const int16_t p : g.actors.playerParts)
        if ((partOf(g, p).flags & 1) == 0) out.push_back(p);
    return out;
}

// 2dee:0c25 Part_GetPartner
int partGetPartner(GgGame& g, int p) {
    const int16_t link = partOf(g, p).pair;
    return link == -1 ? -1 : (link & 0x7FFF);
}

// 2dee:0c4d Workshop_DropCarried
void workshopDropCarried(GgGame& g) {
    VehiclesState& v = g.vehicles;
    if (v.carriedRow == -1) return;
    VehiclesObjects& o = g.vehiclesObjects();
    o.staticZones[0].priority = kShelfAreaPriorityIdle;
    restoreDefaultCursor(g);
    if (v.carriedPart >= 0) shelfOf(g, v.carriedPart).state &= static_cast<uint16_t>(~3u);
    g.zones().removeLocal(&o.workbench);
    v.carriedRow = -1;
    v.shelfDirty = 1;
    g.world.hud.dirty |= Hud::kFull;
}

// ---- the blueprint viewer ----------------------------------------------------------------------------

namespace {

// The hot spots' zones of the loaded blueprint (Blueprint_Load's 'M' records).
void buildHotZones(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    o.hotZones.clear();
    for (const auto& h : o.blueprint->hotSpots()) {
        Zone z = Zone::rect(static_cast<int16_t>(h.x), static_cast<int16_t>(h.y), static_cast<int16_t>(h.w),
                            static_cast<int16_t>(h.h), 5, 0, static_cast<uint16_t>(wcode::kBlueprintPage + h.page));
        z.shape = static_cast<int16_t>(h.shape);
        o.hotZones.push_back(z);
    }
}

// The redraw of Blueprint_Run (2dee:18b2..19e7): Blueprint_ClearBox with the Done button and its zone,
// the title, the hover regions off, the hot spots' zones off; page 1 the overview with the zones and the
// regions back on, page 2 the part page; Dirty_Flush.
void blueprintRedraw(GgGame& g, int page) {
    VehiclesObjects& o = g.vehiclesObjects();
    ClickZones& zones = g.zones();
    zones.removeLocal(&o.done.zone);  // Btn_RemoveZone
    zones.addLocal(o.done.zone);      // Btn_AddZone after the button is drawn
    zones.removeLocalArray(o.hotZones);
    if (page == 1) zones.addLocalArray(o.hotZones);
    const UiCanvas ui = g.canvas();
    o.blueprint->draw(g.video, g.fonts, page, g.vehicles.blueprintPage, [&](Screen&) { picBtnDraw(ui, o.done); });
    g.display.present();
}

}  // namespace

// 2dee:17eb Blueprint_Open
void blueprintOpen(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    g.zones().push();
    v.blueprintOpen = 1;
    g.video.screen().getImage(18, 9, 280, 163, o.blueprintUnder);  // Screen_SaveToSwap: in memory
    g.player.live.blueprintSeen = 1;
    o.blueprint = std::make_unique<BlueprintView>(g.exe(), g.archive());
    // Blueprint_DrawFrame, Font_Select(0), colour 15, the title's level digit, Blueprint_Load.
    o.blueprint->open(g.video, g.fonts, g.raceIndex(), g.level());
    v.blueprintPageCount = static_cast<int16_t>(o.blueprint->pageCount());
    buildHotZones(g);
    o.done = PicButton::fromRecord(g.exe(), dg::kBlueprintDoneButton);
    picBtnLoad(g.archive(), o.done, 248, 142);  // PicBtn_Load
    g.sound.workshopBlueprint(true);            // arrangement 3
}

// 2dee:185d Blueprint_Close
void blueprintClose(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    g.video.screen().putImage(18, 9, o.blueprintUnder);  // Screen_RestoreFromSwap(18, 9)
    picBtnFree(o.done);
    g.zones().pop();  // (after Blueprint_Free in the original: the popped list is not read in between)
    o.hotZones.clear();
    o.blueprint.reset();  // 2dee:174f Blueprint_Free (the label cursors too)
    g.vehicles.blueprintOpen = 0;
    g.sound.workshopBlueprint(false);  // the workshop's arrangement back
}

// 2dee:1894 Blueprint_Run (switch CS:1a70)
int blueprintRun(GgGame& g, StateEntry entry) {
    VehiclesObjects& o = g.vehiclesObjects();
    int redraw = 1, result = 0;
    if (entry == StateEntry::Normal) blueprintOpen(g);
    else redraw = 0;
    const UiCanvas ui = g.canvas();
    for (;;) {
        if (redraw) {
            blueprintRedraw(g, redraw);
            o.blueprintShown = redraw;
            hintShow(g, kHintBlueprint);  // the first time only (the player's hint flags)
            redraw = 0;
        }
        const uint16_t code = g.rt.getInputCode();
        bool close = false;
        switch (code) {
        case 0x83EF:  // Done pressed (held) / first poll
        case 0x43EF: picBtnDrawPressed(ui, o.done); break;
        case 0: break;
        case code::kEnter:
        case wcode::kDone:
            if (o.blueprintShown == 1) close = true;  // the overview: close
            else redraw = 1;                          // a part page: back to the overview
            picBtnDrawNormal(ui, o.done);
            break;
        case code::kMenuRequest1:
            result = 3;
            close = true;
            break;
        case code::kMenuRequest2:
            result = 2;
            close = true;
            break;
        case 0x17D4: picBtnDrawNormal(ui, o.done); break;  // a button release outside it (code_vehicles_races.md §19 q5)
        default:
            if (static_cast<int16_t>(code) >= wcode::kBlueprintPage) {
                g.vehicles.blueprintPage = static_cast<int16_t>(code - wcode::kBlueprintPage);
                redraw = 2;
            }
            break;
        }
        if (close) {
            g.video.cursor().clearRegions();  // MouseRegion_Clear
            blueprintClose(g);
            return result;
        }
    }
}

// ---- starting from a DOSBox-X state --------------------------------------------------------------------

namespace {

uint16_t rd16(std::span<const uint8_t> b, size_t o) { return o + 2 <= b.size() ? static_cast<uint16_t>(b[o] | (b[o + 1] << 8)) : 0; }
// A far pointer stored in RAM (offset, run-time segment) as a physical address; 0 for NULL.
uint32_t farAt(std::span<const uint8_t> b, size_t o) {
    const uint32_t off = rd16(b, o), seg = rd16(b, o + 2);
    return off == 0 && seg == 0 ? 0 : seg * 16 + off;
}

// A cached picture's bytes as the state's RAM holds them (a picture the workshop painted in place).
void copyPictureFromState(const GgStateFile& s, uint32_t linear, std::span<const uint8_t> pic) {
    if (linear == 0 || pic.empty()) return;
    const auto bytes = s.bytesAt(linear, pic.size());
    if (bytes.size() != pic.size() || !std::equal(pic.begin(), pic.begin() + 4, bytes.begin())) return;  // 'LPX' 0x1A
    std::copy(bytes.begin(), bytes.end(), cachedPictureBytes(pic).begin());
}

}  // namespace

bool vehiclesRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    const int state = g.flow.state;
    if (state == 11) {  // a race starts again at its set-up (Race_Run's locals are on the original's stack)
        log::info("vehicles restore: the race of state 11 starts again at its set-up");
        return true;
    }
    if (state == 12 || state == 15) {
        why = std::format("state {}: the results or the movie cannot be resumed (their locals are on the original's stack)", state);
        return false;
    }
    if (state != 6) return true;  // nothing of the vehicles' to restore
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    PartCatalogue& cat = g.world.parts;
    if (cat.count() == 0) {
        why = "the workshop needs the part catalogue (worldRestoreFromState)";
        return false;
    }
    buildZones(g);
    const uint32_t parts = s.farPointerAt(dg::kParts);
    // The part records' workshop fields and shelf pictures.
    std::vector<uint32_t> partAt(cat.count());
    for (size_t i = 0; i < cat.count(); ++i) {
        partAt[i] = parts + static_cast<uint32_t>(i * PartRecord::kSize);
        const auto rec = s.bytesAt(partAt[i], PartRecord::kSize);
        if (rec.size() != PartRecord::kSize) {
            why = "the part records are not in the state's RAM";
            return false;
        }
        auto& slot = shelfOf(g, static_cast<int>(i));
        slot.sprite.flags = rd16(rec, 0x5E);
        slot.sprite.x = static_cast<int16_t>(rd16(rec, 0x60));
        slot.sprite.y = static_cast<int16_t>(rd16(rec, 0x62));
        slot.sprite.w = static_cast<int16_t>(rd16(rec, 0x64));
        slot.sprite.h = static_cast<int16_t>(rd16(rec, 0x66));
        slot.state = rd16(rec, 0x8A);
        if (farAt(rec, 0x68) != 0) {
            shelfPicOf(g, static_cast<int>(i)) = g.cache.load(cat.parts[i].shelfPicture);
            copyPictureFromState(s, farAt(rec, 0x68), shelfPicOf(g, static_cast<int>(i)));
        }
    }
    // The vehicle sprite pool and its layer-sorted list.
    const uint32_t pool = s.farPointerAt(dg::kVehicleSpritePool);
    constexpr size_t kRec = 0x36;
    for (size_t k = 0; k < v.vehiclePool.size(); ++k) {
        const auto rec = s.bytesAt(pool + static_cast<uint32_t>(k * kRec), kRec);
        if (rec.size() != kRec) {
            why = "the vehicle sprite pool is not in the state's RAM";
            return false;
        }
        VehicleSpriteRec& r = v.vehiclePool[k];
        r.inUse = static_cast<int16_t>(rd16(rec, 0x08));
        r.layer = static_cast<int16_t>(rd16(rec, 0x0A));
        r.sprite.load(rec.data() + 0x0C);
        r.part = static_cast<int16_t>(rd16(rec, 0x30));
        o.vehicleZones[k] = Zone::fromRecord(rec.subspan(0x1A, Zone::kRecordSize));
        o.vehiclePics[k] = {};
        if (r.inUse && farAt(rec, 0x16) != 0 && r.part >= 0 && static_cast<size_t>(r.part) < cat.count()) {
            o.vehiclePics[k] = g.cache.load(cat.parts[static_cast<size_t>(r.part)].vehiclePicture);
            copyPictureFromState(s, farAt(rec, 0x16), o.vehiclePics[k]);
        }
    }
    v.vehicleOrder.clear();
    if (const uint32_t list = s.farPointerAt(dg::kVehicleSprites); list != 0) {
        uint32_t node = farAt(s.bytesAt(list, 8), 0);
        for (int guard = 0; node != 0 && guard < 64; ++guard) {
            if (node < pool || (node - pool) % kRec != 0 || (node - pool) / kRec >= v.vehiclePool.size()) {
                why = "a vehicle sprite list node outside the pool";
                return false;
            }
            v.vehicleOrder.push_back(static_cast<int16_t>((node - pool) / kRec));
            node = farAt(s.bytesAt(node, 4), 0);
        }
    }
    // The carried part and its cursor.
    v.carriedPart = -1;
    if (const uint32_t carried = s.farPointerAt(dg::kCarriedPart); carried >= parts && carried != 0)
        if ((carried - parts) % PartRecord::kSize == 0 && (carried - parts) / PartRecord::kSize < cat.count())
            v.carriedPart = static_cast<int16_t>((carried - parts) / PartRecord::kSize);
    if (v.carriedRow != -1 && v.carriedPart >= 0) {
        auto rec = CursorRecord::fromPicture(shelfPicOf(g, v.carriedPart));
        g.video.cursor().setCursorPicture(rec.get());
        o.carriedCursor = std::move(rec);
        const GgCursorFields c = s.cursor();
        g.video.cursor().setHotSpot(nullptr, c.hotX, c.hotY);
        g.video.cursor().setBounds(0, 9, 319, 154);
    }
    // The conveyor animations and the running list.
    const std::array<uint16_t, 4> animPtrs = {0xA0B8, 0xA0BC, 0xA0C0, 0xA0C4};
    std::array<uint32_t, 4> animAt{};
    g.screenAnims.stopAll();
    for (size_t k = 0; k < 4; ++k) {
        o.conveyor[k].reset();
        animAt[k] = s.farPointerAt(animPtrs[k]);
        if (animAt[k] == 0) continue;
        const auto rec = s.bytesAt(animAt[k], 0x28);
        o.conveyor[k] = ScreenAnim::load(g.cache, kConveyorSets[k]);
        o.conveyor[k]->restore(static_cast<int16_t>(rd16(rec, 0x22)), static_cast<int16_t>(rd16(rec, 0x26)),
                               static_cast<int16_t>(rd16(rec, 0x1A)), static_cast<int16_t>(rd16(rec, 0x1E)),
                               static_cast<int16_t>(rd16(rec, 0x20)), static_cast<int16_t>(rd16(rec, 0x08)));
    }
    if (const uint32_t list = s.farPointerAt(dg::kScreenAnims); list != 0) {
        uint32_t node = farAt(s.bytesAt(list, 8), 0);
        for (int guard = 0; node != 0 && guard < 16; ++guard) {
            for (size_t k = 0; k < 4; ++k)
                if (node == animAt[k] && o.conveyor[k]) g.screenAnims.start(*o.conveyor[k]);
            node = farAt(s.bytesAt(node, 4), 0);
        }
    }
    // The blueprint viewer when it is open (its hot spots' zones are linked on page 1).
    const bool blueprint = v.blueprintOpen != 0;
    if (blueprint) {
        o.blueprint = std::make_unique<BlueprintView>(g.exe(), g.archive());
        o.blueprint->load(g.raceIndex());
        buildHotZones(g);
        o.done = PicButton::fromRecord(g.exe(), dg::kBlueprintDoneButton);
        picBtnLoad(g.archive(), o.done, 248, 142);
    }
    // The click zones in the original's list order: the workshop's list (on the stack when the blueprint
    // is open) and the current local list.
    auto dgAt = [&](uint16_t off) { return s.imageLinear(GgStateFile::kDgroupImageSegment, off); };
    auto zoneOf = [&](uint32_t linear) -> Zone* {
        for (size_t k = 0; k < o.staticZones.size(); ++k)
            if (linear == dgAt(static_cast<uint16_t>(dg::kWorkshopZones + Zone::kRecordSize * k))) return &o.staticZones[k];
        for (size_t k = 0; k < o.shelfZones.size(); ++k)
            if (linear == dgAt(static_cast<uint16_t>(dg::kShelfZones + dg::kShelfZonesStride * k))) return &o.shelfZones[k];
        if (linear == dgAt(dg::kWorkbenchZone)) return &o.workbench;
        if (blueprint && linear == dgAt(dg::kBlueprintDoneButton)) return &o.done.zone;
        if (linear >= pool && (linear - pool) % kRec == 0x1A && (linear - pool) / kRec < o.vehicleZones.size())
            return &o.vehicleZones[(linear - pool) / kRec];
        return nullptr;
    };
    auto relink = [&](uint32_t head, bool hotSpotsAllowed, bool& sawHotSpot) {
        std::vector<Zone*> chain;
        for (uint32_t node = head, guard = 0; node != 0 && guard < 256; ++guard) {
            const auto rec = s.bytesAt(node, Zone::kRecordSize);
            Zone* z = zoneOf(node);
            if (!z && hotSpotsAllowed) {  // a blueprint hot spot: by its release code
                const uint16_t release = rd16(rec, 0x12);
                for (Zone& h : o.hotZones)
                    if (h.releaseCode == release && !std::count(chain.begin(), chain.end(), &h)) {
                        z = &h;
                        sawHotSpot = true;
                        break;
                    }
            }
            if (z) {
                const Zone from = Zone::fromRecord(rec);
                z->x = from.x;
                z->y = from.y;
                z->w = from.w;
                z->h = from.h;
                z->priority = from.priority;
                chain.push_back(z);
            } else {
                log::debug("vehiclesRestoreFromState: a local zone (release {}) the workshop does not own", rd16(rec, 0x12));
            }
            node = farAt(rec, 0x0C);
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) g.zones().addLocal(**it);  // newest first
    };
    g.zones().clearLocal();
    bool sawHotSpot = false;
    if (blueprint) {
        // Zones_Push saved the workshop's list on the zone stack g_zoneStack DS:7a4e (depth DS:0a50): the
        // workshop's is the last one pushed.
        const uint16_t depth = s.dsWord(dg::kZoneStackDepth);
        const uint32_t saved = depth > 0 ? s.farPointerAt(static_cast<uint16_t>(dg::kZoneStack + 4 * (depth - 1))) : 0;
        relink(saved, false, sawHotSpot);
        g.zones().push();
        relink(s.farPointerAt(dg::kZonesLocal), true, sawHotSpot);
        o.blueprintShown = sawHotSpot ? 1 : 2;
        o.blueprintUnder = {};
        // The save-under is in gizmo004.dat, not in the state: rebuilt as the workshop's frame under the box
        // (the back page with the shelf and the vehicle drawn over it).
        Screen& screen = g.video.screen();
        SavedImage keep;
        screen.getImage(0, 0, 320, 200, keep);
        screen.restoreRect(18, 9, 280, 163);
        // The player as the tube's arrival left him at the entry (set 0x1b sequence 1's last frame: never
        // erased, not in the back page).
        std::optional<ScreenAnim> tube = ScreenAnim::load(g.cache, kTubeAnim);
        tube->setSequence(1);
        BorlandRand quiet;  // the set has no rand() op
        for (int k = 0; k < 4096 && tube->state() == 0; ++k) tube->step(quiet);
        if (!tube->hidden() && !tube->sprite().picture.empty())
            screen.lpxDrawKeyed(tube->sprite().x, tube->sprite().y, tube->sprite().picture, tube->sprite().flags & 3);
        freeAnim(g, tube);
        workshopDrawShelf(g);
        workshopDrawVehicle(g);
        screen.getImage(18, 9, 280, 163, o.blueprintUnder);
        screen.putImage(0, 0, keep);
    } else {
        relink(s.farPointerAt(dg::kZonesLocal), false, sawHotSpot);
    }
    // Workshop_Run's locals: the current part is the lower part under zone 1004 (DS:21a2).
    WorkshopRunLocals& run = o.run;
    run.shelf = workshopListShelfParts(g);
    run.last = v.shelfCount - 1;
    const Zone cur = Zone::fromRecord(s.bytesAt(dgAt(dg::kShelfZones), Zone::kRecordSize));
    run.current = run.shelf.empty() ? -1 : run.shelf.front();
    run.index = 0;
    for (size_t i = 0; i < run.shelf.size(); ++i) {
        const auto& sp = shelfOf(g, run.shelf[i]).sprite;
        if (sp.x == cur.x && sp.y == cur.y && sp.w == cur.w && sp.h == cur.h) {
            run.current = run.shelf[i];
            run.index = static_cast<int>(i);
            break;
        }
    }
    run.partner = run.current >= 0 ? partGetPartner(g, run.current) : -1;
    run.vehicleChanged = false;
    run.leave = false;
    run.mayInstall = false;
    if (v.carriedRow != -1 && v.carriedPart >= 0) {  // the rule of the carry that took it
        const int p = v.carriedPart;
        const int other = partGetPartner(g, p);
        run.mayInstall = other < 0 || !(partOf(g, other).flags & PartRecord::kInstalled);
    }
    // The workshop's banks again (the state's handles are the original's bank layer's): the effects,
    // the music with its cue from the start, arrangement 3 under an open blueprint.
    SoundState& sn = snd(g);
    sn.effectBank = static_cast<int16_t>(g.sound.loadEffectBank(kEffectBank));
    g.sound.setEffectBank(sn.effectBank);
    sn.musicBank = static_cast<int16_t>(g.sound.loadMusicBank(kMusicBank));
    g.sound.workshopMusicStart(sn.musicBank);
    if (blueprint) g.sound.workshopBlueprint(true);
    g.input().setEscHandler([&g] { workshopDropCarried(g); });
    return true;
}

}  // namespace tmport::gg
