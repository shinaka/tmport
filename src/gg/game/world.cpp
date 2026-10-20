// The world's states (world.h; re/gg/docs/code_world.md §4, §6.6, §11.8, §14, §15): building set-up and
// resume, entering and leaving a room, the warehouse loop, the door a solved puzzle opens, the score and
// banana counters, the emergency reset, and the world's part of starting from a DOSBox-X state.
// Owner: world agent.
#include "gg/game/world.h"

#include <algorithm>
#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/actors.h"
#include "gg/game/chimps.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/game/hud.h"
#include "gg/game/parts.h"
#include "gg/game/player.h"
#include "gg/game/rooms.h"
#include "gg/gfx/screen.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/hints.h"
#include "gg/ui/menu_bar.h"
#include "gg/ui/players.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kWarehouseEffects = 0x1039;  // State_07_Warehouse's effect bank
constexpr uint16_t kRoomMusicFirst = 0x035F;    // + 2 g_building (Rooms_Load)
constexpr uint16_t kIdleHint = 0x1048;          // "the gray doors", 0x2238 ticks after a room's entry
constexpr uint16_t kIdleHintDelay = 0x2238;
constexpr uint16_t kHudInfoText = 0x879A;     // DS:879a (unnamed): far pointer to the HUD name box's text

SoundState& banks(GgGame& g) { return g.GgState::sound; }

// The warehouse loop's codes (switch table CS:0808 of 12e0:0619: 6 codes, 6 targets).
constexpr uint16_t kAltW = 0x0111;
constexpr uint16_t kAltS = 0x011F;

}  // namespace

// ------------------------------------------------------------------------------------- WorldObjects

WorldObjects::WorldObjects() = default;
WorldObjects::~WorldObjects() = default;

void WorldObjects::loadHudZones(const GgExe& exe) {
    if (hudZonesLoaded) return;
    for (size_t i = 0; i < stripZones.size(); ++i)
        stripZones[i] = Zone::fromRecord(exe.dsBytes(static_cast<uint16_t>(dg::kHudStripZones + Zone::kRecordSize * i), Zone::kRecordSize));
    for (size_t i = 0; i < carryZones.size(); ++i)
        carryZones[i] = Zone::fromRecord(exe.dsBytes(static_cast<uint16_t>(dg::kHudCarryZones + Zone::kRecordSize * i), Zone::kRecordSize));
    hudZonesLoaded = true;
}

void WorldObjects::unlinkPartZones(GgGame& g) {
    const Zone* first = partZones.data();
    const Zone* last = partZones.data() + partZones.size();
    std::vector<Zone*> linked;
    for (Zone* z = g.zones().local(); z; z = z->next)
        if (z >= first && z < last) linked.push_back(z);
    for (Zone* z : linked) g.zones().removeLocal(z);
}

void WorldObjects::resetPartZones() {
    for (Zone& z : partZones) z = Zone{};  // a fresh catalogue's records hold zeros at +0x48
}

// ------------------------------------------------------------------------------------- states

// 12e0:0820 State_04_BuildingSetup
int16_t state04BuildingSetup(GgGame& g) {
    SoundState& snd = banks(g);
    if (snd.titleMusicBank != -1) {
        g.sound.freeMusicBank(snd.titleMusicBank);
        snd.titleMusicBank = -1;
    }
    PlayerRecord& p = g.player;
    const int b = p.building;
    p.level = static_cast<int16_t>(p.racesWon.at(static_cast<size_t>(b)) + 1);
    p.raceIndex = static_cast<int16_t>(b * 5 + p.level);
    partsLoadCatalogue(g);
    roomsLoad(g);  // rand: the deck, the item grids
    hudDrawAll(g, 7);
    hudInstallHooks(g, true);
    LiveState& live = p.live;
    if (p.buildingInProgress.at(static_cast<size_t>(b)) == 0) {
        buildingNewVisit(g);
        live.partsNeeded = static_cast<int16_t>(partsNeededCount(g));
        if (p.level == 0) {
            live.hintCancel1046 = 0;  // hint 0x1046 may fire again
            live.projectiles = 10;    // 10 bananas per building, not per visit (§20 bug 11: keep)
        }
        live.setupCoin = static_cast<int16_t>(g.rand.random(100) < 50);  // written only
        chimpsSetupActors(g);
        live.blueprintSeen = 0;
        live.vehicleBonusGiven = 0;
        live.paintColor = -1;
        live.tubeRoom = g.tables.tubeRoomByLevel.at(static_cast<size_t>(p.level));
        live.partsInstalled = 0;
        chimpsSpawn(g);  // rand: per chimp R(6) / R(3), R(100)
    } else {
        // Resume without a prompt: the 20 live bytes, the parts, the rooms and the actors of the block
        // (the deck and the grids above were dealt and are discarded; the carried counts +0x70 are not
        // reset: §20 bug 7).
        actorsLoadPlayerSets(g);
        chimpsSetupActors(g);
        const BuildingBlock& block = buildingBlock(p, b);
        live = block.live;
        partsRestoreState(g, block);
        roomsRestoreState(g, block);
        actorsRestoreState(g, block);
        g.world.inBuilding = 1;
    }
    return 5;
}

// 12e0:096a State_05_EnterRoom
int16_t state05EnterRoom(GgGame& g) {
    g.rt.flushInput();
    g.keyboard().setPlayMode(1);  // the arrows and Space are held keys, not codes
    menuSetMenuBar(g, true);
    roomEnter(g, g.playerRoom());
    actorsEnterRoom(g);
    g.world.hud.dirty |= Hud::kFull;
    return g.playerRoom() == 0 ? int16_t{6} : int16_t{7};
}

// 12e0:0619 State_07_Warehouse
int16_t state07Warehouse(GgGame& g, StateEntry entry) {
    SoundState& snd = banks(g);
    int cheat = 0;   // DI: Alt+S armed
    int16_t next = 8;
    bool leave = false;
    g.input().setEscHandler([&g] { hudEscHook(g); });  // g_escHandler = 1a04:0d65
    snd.effectBank = static_cast<int16_t>(g.sound.loadEffectBank(kWarehouseEffects));
    g.sound.setEffectBank(snd.effectBank);
    hudInstallHooks(g, true);
    if (entry == StateEntry::Normal) {
        hudEscHook(g);
        hudUpdate(g, 0);
        g.video.sprites().reset();
        roomQueueEntryLayers(g);
        actorsResetCountdowns(g);
        leave = actorsTick(g) != 0;  // rand: the chimps' brains of the entry tick
        actorsDraw(g);
        roomDrawLayer(g, 3);
        g.video.sprites().drawAll(g.video.screen());
        // 1473:045a Screen_Show: faded (from the workshop) a flush and the fade-in; else the random
        // wipe (rand: the pattern, then the sound 0x19 / 0x1a of the bank, loop 1, priority 10), then
        // Dirty_Clear without a flush.
        g.display.show(g.archive(), g.rand, [&g, &snd](int k, bool start) {
            if (start) g.sound.playEffect(snd.effectBank + k, 1, 10);
            else g.sound.stopEffect(snd.effectBank + k);
        });
        g.input().resetIdleTick();  // g_lastKeyHeldTick DS:1314 = g_ticks
        hintQueue(g, kIdleHint, kIdleHintDelay, dg::kHintCancel1048);
    }
    while (!leave) {
        hintUpdate(g);  // may show a box (time frozen there)
        g.input().keyHeldSeen = 0;
        g.timers().set(0, 4);
        if (actorsTick(g) != 0) {
            leave = true;  // the player's sequence ended: no drawing, no wait, no input this frame
            break;
        }
        g.video.sprites().eraseAll(g.video.screen());
        roomDrawLayer(g, 1);
        roomDrawAnimLayer(g, 0);
        roomDrawLayer(g, 2);
        actorsDraw(g);
        roomDrawAnimLayer(g, 3);
        roomDrawLayer(g, 3);  // and the items: over the actors
        g.video.sprites().drawAll(g.video.screen());
        hudUpdate(g, 0);
        g.display.flush();
        g.rt.timerWait(0);
        const uint16_t c = g.rt.getInputCode();  // the HUD codes and the global keys act inside
        switch (c) {
        case 0:
            break;
        case code::kEsc:
            cmdSaveAndExit(g);  // the Esc hook has put back a part on the cursor; Yes exits the program
            break;
        case kAltW:
            if (cheat == 1) warehouseResetPlayer(g);  // stays armed
            break;
        case kAltS:
            if (cheat == 0) cheat = 1;
            break;
        case code::kMenuRequest1:  // File > Return to Technology Center
            g.flow.leaveRequest = 3;
            next = 16;
            leave = true;
            break;
        case code::kMenuRequest2:  // File > Start New Player
            g.flow.leaveRequest = 2;
            next = 16;
            leave = true;
            break;
        default:
            cheat = 0;
            break;
        }
    }
    // Going to another room shows the wipe of the next entry; the workshop and the menu exits fade.
    if (g.playerRoom() == 0 || next == 16) g.display.fadeOut();
    g.sound.freeEffectBank(snd.effectBank);  // DS:7a44 keeps the stale handle, as in the original
    hintClearQueue(g);
    hudEscHook(g);
    g.input().setEscHandler({});
    return next;
}

// 12e0:09aa State_08_LeaveRoom
int16_t state08LeaveRoom(GgGame& g) {
    int16_t result = 5;
    menuSetMenuBar(g, false);
    roomLeave(g);
    actorsLeaveRoom(g);
    if (g.world.puzzleDoorHit != 0) {
        g.fonts.setColor(0);
        g.world.puzzleDoorHit = 0;
        result = 9;
        g.sound.stopMusic(banks(g).roomMusicBank);
        g.display.fadeOut();
        chimpsFreeSets(g);
        actorsFreePlayerSets(g);
    }
    if (g.world.vehicleDone != 0) {  // the vehicle wins when both are set
        result = 10;
        g.world.vehicleDone = 0;
        g.player.buildingInProgress.at(static_cast<size_t>(g.player.building)) = 1;
        g.world.inBuilding = 1;
        playersSaveCurrent(g, GgSaveMoment::LeaveRoom);
    }
    return result;
}

// 18f5:094d Building_NewVisit
void buildingNewVisit(GgGame& g) {
    Actor& player = g.actors.actors[kPlayerActor];
    player.floor = 0;        // DS:7aa6
    player.immune = 0;       // DS:7ac8
    player.prevRoom = -1;    // DS:7ac6
    player.room = 0;         // g_playerRoom DS:7aa4: every visit starts in the workshop
    g.actors.actors[kProjectileActor].disabled = 1;  // DS:7b54
    player.carried = 0;      // DS:7afa
    player.contacts = 0;     // DS:7ad2
    g.actors.playerParts.clear();  // List_Clear(DS:7afc)
}

// 12e0:0a24 Room_OpenPuzzleDoor
void roomOpenPuzzleDoor(GgGame& g, int solved) {
    if (solved == 0) return;
    const int room = g.playerRoom();
    RoomObject* door = roomGetObject(g, room, g.actors.actors[kPlayerActor].doorObj);
    if (!door || door->type != 7) return;
    door->pictureId = roomIsHall(g, room) ? 0x33C : 0x30E;
    door->type = 9;
    const int target = door->targetRoom();
    if (g.puzzles.goThrough != 0) g.actors.actors[kPlayerActor].room = target;  // out of the twin
    door->hit.kind = 4;
    RoomObject* twin = roomFindLinkedDoor(g, target, door->link);
    if (!twin) return;  // the original writes past the target room's last object
    twin->pictureId = roomIsHall(g, target) ? 0x33C : 0x30E;
    twin->type = 9;
    twin->hit.kind = 4;
}

// 12e0:0ace Score_Add(points, hud)
void scoreAdd(GgGame& g, int points, int hud) {
    g.player.points += static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(points)));  // CWD, ADD, ADC
    if (hud != 0) g.world.hud.dirty |= Hud::kScore;
}

// 12e0:0aea Projectiles_Add
void projectilesAdd(GgGame& g, int n) {
    g.player.live.projectiles = static_cast<int16_t>(g.player.live.projectiles + n);
    g.world.hud.dirty |= Hud::kBananas;
}

// 12e0:0765..07c5 the emergency reset (Alt+W after Alt+S)
void warehouseResetPlayer(GgGame& g) {
    Actor& p = g.actors.actors[kPlayerActor];
    const int room = g.playerRoom();
    p.anchorX = 10;
    if (roomIsHall(g, room)) {
        p.anchorY = 0x90;
        p.floor = 5;
    } else {
        p.anchorY = 0x7F;
        p.floor = (roomGetFlags(g, room) & 2) ? 5 : 2;  // a small room under the hall's floors 3..5
    }
    actorSetSequence(g, kPlayerActor, 0);
    p.step = 0;
    p.contacts = 0;
    p.contactsOn = 1;
    p.wallsOn = 1;
}

// ------------------------------------------------------------------------- start from a DOSBox-X state

namespace {

int16_t s16At(std::span<const uint8_t> b, size_t o) {
    return o + 2 <= b.size() ? static_cast<int16_t>(b[o] | (b[o + 1] << 8)) : int16_t{0};
}

// The far pointer stored at a physical address of the state's RAM, as a physical address (0: NULL).
uint32_t farAt(const GgStateFile& s, uint32_t linear) {
    const auto b = s.bytesAt(linear, 4);
    if (b.size() < 4) return 0;
    const uint32_t off = static_cast<uint32_t>(b[0] | (b[1] << 8));
    const uint32_t seg = static_cast<uint32_t>(b[2] | (b[3] << 8));
    return off == 0 && seg == 0 ? 0 : seg * 16 + off;
}

// The nodes of a 19c0 list (header {far head, far tail}; node +0 far next), head first.
std::vector<uint32_t> listNodes(const GgStateFile& s, uint32_t header, size_t limit) {
    std::vector<uint32_t> out;
    if (header == 0) return out;
    for (uint32_t n = farAt(s, header); n != 0 && out.size() < limit; n = farAt(s, n)) out.push_back(n);
    return out;
}

// The part catalogue as the state's RAM holds it: the copy g_levelDesc with the run-time fields of its
// records (holders, pairs, flags, strip sprites), the icons and names loaded again, the strip zones of
// the records and the carried lists.
bool restoreCatalogue(GgGame& g, const GgStateFile& s, std::vector<std::string>& notes, std::string& why) {
    const uint32_t desc = s.farPointerAt(dg::kLevelDesc);
    const uint32_t recs = s.farPointerAt(dg::kParts);
    const auto head = s.bytesAt(desc, 0x2D);
    if (desc == 0 || recs == 0 || head.size() < 0x2D) {
        why = "the part catalogue g_levelDesc is not in the state's RAM";
        return false;
    }
    const int n = s16At(head, 0x02), k = s16At(head, 0x06);
    if (n < 0 || n > kPartStates || k < 0 || k > 64) {
        why = std::format("the part catalogue holds {} parts of {} needed categories", n, k);
        return false;
    }
    const size_t size = 0x2D + 2 * static_cast<size_t>(k) + PartRecord::kSize * static_cast<size_t>(n) + 2 * 32;
    const auto bytes = s.bytesAt(desc, size);
    if (bytes.size() < size || recs != desc + 0x2D + 2 * static_cast<uint32_t>(k)) {
        why = "the part catalogue's records are not where g_parts points";
        return false;
    }
    PartCatalogue c = PartCatalogue::parse(bytes);
    const auto& icons = g.cache.loadBundle(c.iconBundle);  // Parts_LoadCatalogue's PicCache_LoadBundle
    WorldObjects& w = g.worldObjects();
    w.unlinkPartZones(g);
    w.resetPartZones();
    for (PartRecord& r : c.parts) {
        const uint32_t at = recs + PartRecord::kSize * static_cast<uint32_t>(r.index);
        if (r.iconMember < icons.size()) r.icon = icons[r.iconMember];
        const auto name = g.archive().read(r.nameId);
        r.name.assign(name.begin(), name.end());
        // The strip sprite's picture (+0x44): Hud_AddPart gives a record its own icon (+0x82).
        const uint32_t pic = farAt(s, at + 0x44);
        if (pic != 0) {
            for (const PartRecord& q : c.parts)
                if (farAt(s, recs + PartRecord::kSize * static_cast<uint32_t>(q.index) + 0x82) == pic) {
                    r.hudSprite.picture = q.iconMember < icons.size() ? icons[q.iconMember] : std::span<const uint8_t>{};
                    break;
                }
        }
        Zone z = Zone::fromRecord(s.bytesAt(at + 0x48, Zone::kRecordSize));
        z.next = nullptr;
        w.partZones[static_cast<size_t>(r.index)] = z;
    }
    g.world.parts = std::move(c);
    // The carried lists: the player's own (actor 0 +0x72) and the chimps' (g_chimpParts DS:a03c).
    auto partsOf = [&](uint32_t header, std::vector<int16_t>& out) {
        out.clear();
        for (const uint32_t node : listNodes(s, header, static_cast<size_t>(n))) {
            const uint32_t d = node - recs;
            if (node < recs || d % PartRecord::kSize != 0 || d / PartRecord::kSize >= static_cast<uint32_t>(n)) {
                notes.push_back("a carried list holds a node outside the part records");
                continue;
            }
            out.push_back(static_cast<int16_t>(d / PartRecord::kSize));
        }
    };
    partsOf(s.farPointerAt(static_cast<uint16_t>(dg::kActors + 0x72)), g.actors.playerParts);
    partsOf(s.farPointerAt(dg::kChimpParts), g.actors.chimpParts);
    return true;
}

// The room set copy g_roomSet with its run-time changes, g_roomItemCounts and the item lists of the
// room table (the nodes are g_itemPool's, loaded with DGROUP).
bool restoreRooms(GgGame& g, const GgStateFile& s, std::vector<std::string>& notes, std::string& why) {
    const uint32_t setAt = s.farPointerAt(dg::kRoomSet);
    const uint32_t countsAt = s.farPointerAt(dg::kRoomItemCounts);
    const int race = g.player.raceIndex;
    if (setAt == 0 || countsAt == 0 || race < 0 || race >= kRaceCount) {
        why = "the room set g_roomSet is not in the state's RAM";
        return false;
    }
    const size_t setSize = g.archive().read(Rooms::roomSetId(g.exe(), race)).size();
    const size_t countsSize = g.archive().read(static_cast<uint16_t>(Rooms::kItemCountsFirst + race)).size();
    const auto setBytes = s.bytesAt(setAt, setSize);
    const auto countBytes = s.bytesAt(countsAt, countsSize);
    if (setBytes.size() < setSize || countBytes.size() < countsSize) {
        why = "the room set copy runs past the state's RAM";
        return false;
    }
    RoomSet set = RoomSet::parse(setBytes);
    std::vector<int16_t> counts;
    for (size_t o = 0; o + 2 <= countBytes.size(); o += 2) counts.push_back(s16At(countBytes, o));
    Rooms& rooms = g.world.rooms;
    const int nRooms = static_cast<int>(set.rooms.size());
    rooms.adopt(std::move(set), std::move(counts));
    const uint32_t pool = static_cast<uint32_t>(s.dgroupSegment()) * 16 + dg::kItemPool;
    for (int r = 0; r < nRooms; ++r) {
        const uint32_t header = s.farPointerAt(static_cast<uint16_t>(dg::kRoomTable + 4 * r));
        Room& room = rooms.room(r);
        if (header == 0) {
            notes.push_back(std::format("room {} has no table entry", r));
            continue;
        }
        const auto h = s.bytesAt(header, 0x120);
        room.itemCount = s16At(h, 0x9E);
        const uint32_t list = farAt(s, header + 0xA0);
        room.hasItemList = list != 0;
        room.items.clear();
        for (const uint32_t node : listNodes(s, list, Rooms::kItemPoolSize)) {
            const uint32_t d = node - pool;
            if (node < pool || d % dg::kItemPoolStride != 0 || d / dg::kItemPoolStride >= Rooms::kItemPoolSize) {
                notes.push_back(std::format("room {}'s item list holds a node outside g_itemPool", r));
                continue;
            }
            room.items.push_back(static_cast<int>(d / dg::kItemPoolStride));
        }
    }
    // g_room DS:08c6 is the displayed room's header copy: its item count DS:0964 runs in step with the
    // table's (the engine has one).
    if (rooms.displayRoom >= 0 && rooms.displayRoom < rooms.count() &&
        s.dsWord(static_cast<uint16_t>(dg::kRoom + 0x9E)) != static_cast<uint16_t>(rooms.room(rooms.displayRoom).itemCount))
        notes.push_back("the displayed room's item count DS:0964 differs from its table entry's");
    return true;
}

// The pictures Room_Enter left loaded for the displayed room: the static objects of layers 1..3, every
// animated object's bundle (its frame found by the picture bytes its sprite points at), the items'.
void restoreRoomPictures(GgGame& g, const GgStateFile& s) {
    Rooms& rooms = g.world.rooms;
    if (!rooms.roomLoaded || rooms.displayRoom < 0 || rooms.displayRoom >= rooms.count()) return;
    Room& room = rooms.room(rooms.displayRoom);
    const uint32_t header = s.farPointerAt(static_cast<uint16_t>(dg::kRoomTable + 4 * rooms.displayRoom));
    for (size_t l = 0; l < 4; ++l) {
        for (RoomObject& o : room.layers[l]) o.sprite.picture = l != 0 && o.type != 3 ? g.cache.load(o.pictureId) : std::span<const uint8_t>{};
        const uint32_t anims = header ? farAt(s, header + 0x8E + 4 * static_cast<uint32_t>(l)) : 0;
        for (size_t i = 0; i < room.anims[l].size(); ++i) {
            RoomAnim& a = room.anims[l][i];
            a.pictures = &g.cache.loadBundle(a.bundle);
            a.sprite.picture = {};
            const uint32_t pic = anims ? farAt(s, anims + 0x48 * static_cast<uint32_t>(i) + 0x0A) : 0;
            if (pic == 0) continue;
            for (const auto& member : *a.pictures) {
                const auto ram = s.bytesAt(pic, member.size());
                if (ram.size() == member.size() && std::equal(ram.begin(), ram.end(), member.begin())) {
                    a.sprite.picture = member;
                    break;
                }
            }
        }
    }
    int n = 0;
    for (const int i : room.items) {
        if (n++ >= room.itemCount) break;
        ItemNode& it = rooms.pool()[static_cast<size_t>(i)];
        it.sprite.picture = g.cache.load(it.pictureId);
    }
}

// The HUD's zones of the local list (the strip's and the carry zones, in the list's order), the part on
// the cursor and the name box.
void restoreHud(GgGame& g, const GgStateFile& s, std::vector<std::string>& notes) {
    WorldObjects& w = g.worldObjects();
    w.loadHudZones(g.exe());
    const uint32_t ds = static_cast<uint32_t>(s.dgroupSegment()) * 16;
    const uint32_t recs = s.farPointerAt(dg::kParts);
    const auto n = static_cast<uint32_t>(g.world.parts.count());
    std::vector<Zone*> hud;
    int others = 0;
    for (uint32_t z = s.farPointerAt(dg::kZonesLocal), guard = 0; z != 0 && guard < 400; z = farAt(s, z + 0x0C), ++guard) {
        Zone* mine = nullptr;
        const uint32_t d = z - recs - 0x48;
        if (recs && z >= recs + 0x48 && d % PartRecord::kSize == 0 && d / PartRecord::kSize < n) mine = &w.partZones[d / PartRecord::kSize];
        else if (z == ds + dg::kHudStripZones) mine = &w.stripZones[0];
        else if (z == ds + dg::kHudStripZones + Zone::kRecordSize) mine = &w.stripZones[1];
        else if (z == ds + dg::kHudCarryZones) mine = &w.carryZones[0];
        else if (z == ds + dg::kHudCarryZones + Zone::kRecordSize) mine = &w.carryZones[1];
        if (!mine) {
            ++others;
            continue;
        }
        const Zone from = Zone::fromRecord(s.bytesAt(z, Zone::kRecordSize));
        *mine = from;
        mine->next = nullptr;
        hud.push_back(mine);
    }
    for (auto it = hud.rbegin(); it != hud.rend(); ++it) g.zones().addLocal(**it);
    if (others) notes.push_back(std::format("{} local zones are not the HUD's (their modules restore them)", others));
    // The far pointers of the strip (DS:8758 first shown, DS:8754 last shown: records) and the flash
    // (DS:8742, DS:873e: a record's strip sprite +0x3a) as part indexes.
    auto partAt = [&](uint16_t dsOff, uint32_t field) -> int16_t {
        const uint32_t p = s.farPointerAt(dsOff);
        if (p == 0 || recs == 0 || p < recs + field) return -1;
        const uint32_t d = p - recs - field;
        return d % PartRecord::kSize == 0 && d / PartRecord::kSize < n ? static_cast<int16_t>(d / PartRecord::kSize) : int16_t{-1};
    };
    HudState& h = g.world.hud;
    h.stripFirst = partAt(0x8758, 0);
    h.stripLast = partAt(0x8754, 0);
    h.flashPart = partAt(0x8742, 0x3A);
    h.flashPartner = partAt(0x873E, 0x3A);
    // A part on the cursor (Hud_SelectPart): its icon is the cursor, the pointer kept on the HUD.
    const int sel = g.world.hud.selectedPart;
    if (sel >= 0 && static_cast<size_t>(sel) < g.world.parts.parts.size()) {
        const PartRecord& r = g.world.parts.parts[static_cast<size_t>(sel)];
        w.partCursors.push_back(CursorRecord::fromPicture(r.hudSprite.picture));
        Cursor& cursor = g.video.cursor();
        cursor.setCursorPicture(w.partCursors.back().get());
        const GgCursorFields cf = s.cursor();
        cursor.setHotSpot(nullptr, cf.hotX, cf.hotY);
        cursor.setBounds(0, 0xA5, 0x13F, 0x22);
    }
    // The name box's text (DS:879a): a part's name copy (+0x86).
    w.infoText.clear();
    if (const uint32_t text = s.farPointerAt(kHudInfoText); text != 0) {
        bool found = false;
        for (const PartRecord& r : g.world.parts.parts)
            if (recs && farAt(s, recs + PartRecord::kSize * static_cast<uint32_t>(r.index) + 0x86) == text) {
                w.infoText = r.name;
                found = true;
                break;
            }
        if (!found) notes.push_back("the HUD's name box points at no part name");
    }
}

}  // namespace

// The picture cache's records g_picCache DS:6640 (ids and use counts), set after the restores' own loads
// so they count once.
void picCacheRestoreFromState(GgGame& g, const GgStateFile& s) {
    std::array<PicCache::Slot, PicCache::kSlots> slots{};
    for (int i = 0; i < PicCache::kSlots; ++i) {
        const auto at = static_cast<uint16_t>(dg::kPicCache + 8 * i);
        slots[static_cast<size_t>(i)].id = static_cast<int16_t>(s.dsWord(at));
        slots[static_cast<size_t>(i)].uses = static_cast<int16_t>(s.dsWord(static_cast<uint16_t>(at + 2)));
    }
    g.cache.setSlots(slots);
}

bool worldRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    // The HUD's digit copy is recoloured in place (Hud_SetDigitColors): the state's DS:1318 names the
    // scheme its digits are in, in every state.
    g.hud.syncDigitColors();
    // In a warehouse room, the frame's sprite list g_sprites DS:05f6 (g_spriteCount DS:05f4), unless a
    // loader restored it already (game/load_state.cpp): the next frame's Sprites_EraseAll restores these
    // rectangles from the back page; their pictures are not restored, so they are marked not to draw.
    // Other screens resume their own sprites.
    SpriteList& sprites = g.video.sprites();
    const int nSprites = g.flow.state != 7 || sprites.count() != 0
                             ? 0
                             : std::clamp<int>(static_cast<int16_t>(s.dsWord(dg::kSpriteCount)), 0, static_cast<int>(SpriteList::kCapacity));
    for (int i = 0; i < nSprites; ++i) {
        const auto e = static_cast<uint16_t>(dg::kSprites + 14 * i);
        auto w16 = [&](int o) { return static_cast<int16_t>(s.dsWord(static_cast<uint16_t>(e + o))); };
        Sprite sp;
        sp.flags = static_cast<uint16_t>(s.dsWord(e) | SpriteList::kNoDraw);
        sp.x = w16(2);
        sp.y = w16(4);
        sp.w = w16(6);
        sp.h = w16(8);
        sprites.add(sp);
    }
    const int state = g.flow.state;
    if (state < 5 || state > 15) {  // no building loaded: nothing of the world's in the heap
        picCacheRestoreFromState(g, s);
        return true;
    }
    std::vector<std::string> notes;
    if (!restoreCatalogue(g, s, notes, why)) return false;
    if (!restoreRooms(g, s, notes, why)) return false;
    hudInstallHooks(g, true);
    // The room music: the bank of the building again (the facade's handle), playing in a room.
    SoundState& snd = banks(g);
    if (snd.roomMusicBank != -1) {
        snd.roomMusicBank = static_cast<int16_t>(g.sound.loadMusicBank(static_cast<uint16_t>(kRoomMusicFirst + 2 * g.player.building)));
        g.sound.setRoomMusic(snd.roomMusicBank, g.player.building);
        const int room = g.world.rooms.displayRoom;
        if (g.world.rooms.roomLoaded && room != 0) {
            roomMusicSelectLayers(g, static_cast<unsigned>(room) <= 3 ? room : 4);
            roomMusicStart(g);
        }
    }
    restoreRoomPictures(g, s);
    restoreHud(g, s, notes);
    picCacheRestoreFromState(g, s);
    for (const std::string& n : notes) log::info("world restore: {}", n);
    return true;
}

}  // namespace tmport::gg
