// The rooms on the game (rooms.h, "wave 3: the run"; re/gg/docs/code_world.md §5-§8, §19.1): the room
// set of the building in GgState::world.rooms with the room music through the sound facade, entering
// and leaving the displayed room with its pictures in GgGame's cache, the layers into the game's sprite
// list, doors, the item routines of play (remove, drop, place in a random room: rand) and the room half
// of the building block; ScreenAnim_PlayModal on the game's running list. Owner: world agent.
#include "gg/game/rooms.h"

#include "gg/game/actors.h"
#include "gg/game/game.h"
#include "gg/game/parts.h"
#include "gg/game/screen_anim.h"
#include "gg/gfx/screen.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kRoomMusicFirst = 0x035F;  // + 2 g_building

Rooms& roomsOf(GgGame& g) { return g.world.rooms; }

// The displayed room (g_room DS:08c6: the engine shares the table entry), nullptr outside.
Room* displayed(GgGame& g) {
    Rooms& r = roomsOf(g);
    return r.displayRoom >= 0 && r.displayRoom < r.count() ? &r.room(r.displayRoom) : nullptr;
}

// The screen animations' sound ops on the effect bank of the screen (DS:7a44).
ScreenAnimSound animSound(GgGame& g) {
    ScreenAnimSound s;
    s.play = [&g](int member, int loop, int priority) { g.sound.playEffect(g.GgState::sound.effectBank + member, loop, priority); };
    s.stop = [&g](int member) { g.sound.stopEffect(g.GgState::sound.effectBank + member); };
    s.isPlaying = [&g](int member) { return g.sound.isPlaying(g.GgState::sound.effectBank + member) != 0; };
    return s;
}

}  // namespace

// 1622:0085 Rooms_Load: g_roomMusicBank = SndRes_LoadMusicBank(0x35f + 2 g_building) (handed to the
// facade with g_building for Music_RoomCue), g_roomMusicStep 0, then the room set of g_raceIndex, the
// item counts, Parts_ShuffleDeck(I[0]) and the item grids (Rooms::load: the rand() calls of a set-up),
// g_roomCount, g_displayRoom 0.
void roomsLoad(GgGame& g) {
    const int building = g.player.building;
    g.GgState::sound.roomMusicBank = static_cast<int16_t>(g.sound.loadMusicBank(static_cast<uint16_t>(kRoomMusicFirst + 2 * building)));
    g.sound.setRoomMusic(g.GgState::sound.roomMusicBank, building);
    Rooms& r = roomsOf(g);
    r.load(g.archive(), g.exe(), g.player.raceIndex, g.world.parts, g.world.deck, g.rand);
    g.world.roomCount = static_cast<int16_t>(r.count());
}

// 1622:02dd Rooms_Free: the item lists of rooms 1.., g_roomItemCounts and the room set copy freed (the
// item pool, g_roomCount and the room globals are DGROUP and stay); the room music bank freed, -1.
void roomsFree(GgGame& g) {
    roomsOf(g).freeSet();
    if (g.GgState::sound.roomMusicBank != -1) {
        g.sound.freeMusicBank(g.GgState::sound.roomMusicBank);
        g.GgState::sound.roomMusicBank = -1;
    }
}

// 1622:0411 Room_Enter(room): the header copy and g_displayRoom, Screen_ClearPlayArea, the music (room 0
// stops it; rooms 1..3 select their arrangement, the others arrangement 4, then RoomMusic_Start), the
// pictures and the background into the back page (Rooms::enter).
void roomEnter(GgGame& g, int room) {
    const RoomMusicHook music = [&g](int r) {
        if (r == 0) {
            g.sound.stopMusic(g.GgState::sound.roomMusicBank);
            return;
        }
        roomMusicSelectLayers(g, static_cast<unsigned>(r) <= 3 ? r : 4);
        roomMusicStart(g);
    };
    roomsOf(g).enter(room, g.video.screen(), g.cache, music);
}

// 1622:05f2 Room_Leave: when g_roomLoaded, for layers 0..3 every animated object's bundle and every
// static object's picture id released (type 3 was never cached and layer 0 was released at the entry:
// those releases find nothing, as in the original), then the pictures of the first DS:0964 items;
// g_roomLoaded = 0.
void roomLeave(GgGame& g) {
    Rooms& r = roomsOf(g);
    if (!r.roomLoaded) return;
    if (Room* room = displayed(g)) {
        for (size_t l = 0; l < 4; ++l) {
            for (const RoomAnim& a : room->anims[l]) g.cache.releaseBundle(a.bundle);
            for (const RoomObject& o : room->layers[l]) g.cache.release(o.pictureId);
        }
        int n = 0;
        for (const int i : room->items) {
            if (n++ >= room->itemCount) break;
            g.cache.release(r.pool()[static_cast<size_t>(i)].pictureId);
        }
    }
    r.roomLoaded = false;
}

// 1622:06cf Room_QueueEntryLayers
void roomQueueEntryLayers(GgGame& g) { roomsOf(g).queueEntryLayers(g.video.screen(), g.video.sprites()); }

// 1622:0922 Room_DrawLayer
void roomDrawLayer(GgGame& g, int layer) {
    if (displayed(g)) roomsOf(g).drawLayer(layer, g.video.sprites());
}

// 1622:0706 Room_DrawAnimLayer
void roomDrawAnimLayer(GgGame& g, int list) {
    if (roomsOf(g).count() > 0) roomsOf(g).drawAnimLayer(list, g.video.sprites());
}

// 1622:087f Room_FindLinkedDoor(room, link): the first layer-0 object of type 7..11 whose link id
// ((+0x12 unsigned) >> 6) is (link signed) >> 6; room < 0: the displayed room's header copy. The
// original returns the address past the last object when none matches (never with the shipped links):
// nullptr here.
RoomObject* roomFindLinkedDoor(GgGame& g, int room, uint16_t link) {
    Rooms& r = roomsOf(g);
    Room* rm = room < 0 ? displayed(g) : room < r.count() ? &r.room(room) : nullptr;
    if (!rm) return nullptr;
    const int want = static_cast<int16_t>(link) >> 6;
    for (RoomObject& o : rm->layers[0])
        if (o.type > 6 && o.type < 12 && static_cast<int>(o.link >> 6) == want) return &o;
    log::debug("Room_FindLinkedDoor: no door with link {} in room {} (the original returns past the last object)", want, room);
    return nullptr;
}

// 1622:08fb Room_GetFlags
uint16_t roomGetFlags(GgGame& g, int room) {
    Rooms& r = roomsOf(g);
    return room >= 0 && room < r.count() ? r.room(room).flags : uint16_t{0};
}

// 174b:0522 ScreenAnim_PlayModal(done): Sprites_Reset, then 4-tick frames of ScreenAnim_Frame and
// Dirty_Flush until *done != 0 (the caller passes a running animation's state word +0x08).
void screenAnimPlayModal(GgGame& g, const int16_t& done) {
    g.video.sprites().reset();
    while (done == 0) {
        g.timers().set(0, 4);
        g.screenAnims.frame(g.video.screen(), g.video.sprites(), g.rand, animSound(g));
        g.display.flush();
        g.rt.timerWait(0);
    }
}

// The same with the running animation (the workshop passes its state word +0x08: 2dee:0156, 1cb2, 1d56):
// until it has finished (1) or holds (2).
void screenAnimPlayModal(GgGame& g, const ScreenAnim& anim) {
    g.video.sprites().reset();
    while (anim.state() == 0) {
        g.timers().set(0, 4);
        g.screenAnims.frame(g.video.screen(), g.video.sprites(), g.rand, animSound(g));
        g.display.flush();
        g.rt.timerWait(0);
    }
}

// 1622:0f37 Room_OfActor
Room* roomOfActor(GgGame& g, int actorSlot) {
    if (actorSlot < 0 || actorSlot >= kActorCount) return nullptr;
    Rooms& r = roomsOf(g);
    const int room = g.actors.actors[static_cast<size_t>(actorSlot)].room;
    return room >= 0 && room < r.count() ? &r.room(room) : nullptr;
}

// 1622:090d Room_IsHall
bool roomIsHall(GgGame& g, int room) { return (roomGetFlags(g, room) & 1) != 0; }

// 1622:11f7 Room_GetObject
RoomObject* roomGetObject(GgGame& g, int room, int index) {
    Rooms& r = roomsOf(g);
    if (index < 0 || room < 0 || room >= r.count()) return nullptr;
    auto& layer = r.room(room).layers[0];
    return static_cast<size_t>(index) < layer.size() ? &layer[static_cast<size_t>(index)] : nullptr;
}

// 1622:1234 Room_GetAnim
RoomAnim* roomGetAnim(GgGame& g, int room, int index) {
    Rooms& r = roomsOf(g);
    if (index < 0 || room < 0 || room >= r.count()) return nullptr;
    auto& list = r.room(room).anims[0];
    return static_cast<size_t>(index) < list.size() ? &list[static_cast<size_t>(index)] : nullptr;
}

// 1622:1265 Room_GetItem
int roomGetItem(GgGame& g, int room, int n) {
    Rooms& r = roomsOf(g);
    if (room < 0 || room >= r.count()) return -1;
    const auto& items = r.room(room).items;
    if (items.empty()) return -1;
    // List_GetAt counts with an unsigned compare: past the end (or a negative index) gives the tail.
    const auto at = static_cast<uint16_t>(n);
    return at < items.size() ? items[at] : items.back();
}

// 1622:0d8f Room_RemoveItem(room, item): the node marked free (+8 = 1), the room's count - 1; in the
// displayed room its picture released (and the header copy's count DS:0964, the same counter here);
// unlinked.
void roomRemoveItem(GgGame& g, int room, int poolIndex) {
    Rooms& r = roomsOf(g);
    if (room < 0 || room >= r.count() || poolIndex < 0 || poolIndex >= Rooms::kItemPoolSize) {
        log::debug("Room_RemoveItem: room {} item {} outside the building", room, poolIndex);
        return;
    }
    ItemNode& node = r.pool()[static_cast<size_t>(poolIndex)];
    Room& rm = r.room(room);
    node.free = true;
    --rm.itemCount;
    if (room == r.displayRoom) g.cache.release(node.pictureId);
    std::erase(rm.items, poolIndex);  // List_Remove
}

// 1622:0df0 Room_DropItemAt(room, x, floor, kind, restoring)
void roomDropItemAt(GgGame& g, int room, int x, int floor, int kind, bool restoring) {
    Rooms& r = roomsOf(g);
    if (room < 0 || room >= r.count()) {
        log::debug("Room_DropItemAt: room {} outside the building", room);
        return;
    }
    r.dropItemAt(room, x, floor, kind, restoring, g.world.parts, &g.cache);
}

// 1622:0f4e Item_PlaceInRandomRoom(kind): r = R(g_roomCount - 1) + 1, drawn again while it is the
// displayed room, and the whole draw again while the room had no items at load (g_roomItemCounts[r] =
// 0); then Room_PlaceItems(r, 1, kind) (a grid shuffle: 20 * cells rand() calls), and everything again
// when it found no free cell. The original loops forever when no room qualifies; the engine gives up
// after many draws and logs.
void itemPlaceInRandomRoom(GgGame& g, int kind) {
    Rooms& r = roomsOf(g);
    const auto& counts = r.itemCounts();
    auto countOf = [&](int room) { return room >= 0 && static_cast<size_t>(room) < counts.size() ? counts[static_cast<size_t>(room)] : 0; };
    for (int attempt = 0; attempt < 10000; ++attempt) {
        int room = 0;
        do {
            do {
                room = g.rand.random(g.world.roomCount - 1) + 1;
            } while (room == r.displayRoom);
        } while (countOf(room) == 0);
        if (room >= r.count()) continue;  // a g_roomCount that does not match the set
        if (r.placeItems(room, 1, kind, g.world.parts, g.world.deck, g.rand) != 0) return;
    }
    log::warn("Item_PlaceInRandomRoom({}): no room takes the item (the original loops forever)", kind);
}

// 1622:09e5 Obj_HideAt(layer, index): the object of the displayed room (g_room's layer pointer): a
// visible one (no 0x14) gets the hide request 0x40. The player's hall sequences 52 / 53 name layer 1,
// whose pointer is NULL in every room: the original writes into the interrupt table; ignored here.
void objHideAt(GgGame& g, int layer, int index) {
    Room* room = displayed(g);
    if (!room || layer < 0 || layer > 3 || index < 0 || static_cast<size_t>(index) >= room->layers[static_cast<size_t>(layer)].size()) {
        log::debug("Obj_HideAt({}, {}): no such object in the displayed room (ignored)", layer, index);
        return;
    }
    objHide(room->layers[static_cast<size_t>(layer)][static_cast<size_t>(index)]);
}

// 1622:0a17 Obj_ShowAt(layer, index): flags &= ~0x10 (the same guard).
void objShowAt(GgGame& g, int layer, int index) {
    Room* room = displayed(g);
    if (!room || layer < 0 || layer > 3 || index < 0 || static_cast<size_t>(index) >= room->layers[static_cast<size_t>(layer)].size()) {
        log::debug("Obj_ShowAt({}, {}): no such object in the displayed room (ignored)", layer, index);
        return;
    }
    objShow(room->layers[static_cast<size_t>(layer)][static_cast<size_t>(index)]);
}

// 1622:0a4e Obj_Hide: flags |= 0x40 unless 0x14
void objHide(RoomObject& obj) {
    if (!(obj.sprite.flags & RoomObject::kNoDraw)) obj.sprite.flags |= RoomObject::kHideRequest;
}

// 1622:0a42 Obj_Show: flags &= ~0x10
void objShow(RoomObject& obj) { obj.sprite.flags &= static_cast<uint16_t>(~0x10u); }

// 1622:0346 RoomMusic_Start: unless the bank plays: started, the step 0, the building's first transpose
// (read with stride 6: a kept bug) and the cue (Music_RoomCue) - the facade's MusicCues.
void roomMusicStart(GgGame& g) { g.sound.roomMusicStart(); }

// 1622:03b6 RoomMusic_SelectLayers(set): arrangement `set`, channels 4 and 5 muted, the chimp voice (4
// up to level 2, else 5) unmuted when g_roomChimpCount[g_displayRoom] != 0.
void roomMusicSelectLayers(GgGame& g, int set) {
    const int room = roomsOf(g).displayRoom;
    const bool chimps = room >= 0 && static_cast<size_t>(room) < g.actors.roomChimpCount.size() &&
                        g.actors.roomChimpCount[static_cast<size_t>(room)] != 0;
    g.sound.roomMusicSelectLayers(set, chimps, g.player.level);
}

// 1622:0fb3 Rooms_SaveState(doors, items): block +0x26c a byte 7 or 9 per layer-0 object of type 7 or 9
// in room order, block +0x302 per room {n, n x {x, floor, kind}}; the bytes after what is written keep
// their values (the original writes into the record in place).
void roomsSaveState(GgGame& g, BuildingBlock& block) {
    std::vector<uint8_t> doors;
    std::vector<int16_t> items;
    roomsOf(g).saveState(doors, items);
    if (doors.size() > block.doorTypes.size() || items.size() * 2 > block.roomItems.size())
        log::warn("Rooms_SaveState: {} door bytes and {} item words do not fit the block (the original writes on)", doors.size(),
                  items.size());
    std::copy_n(doors.begin(), std::min(doors.size(), block.doorTypes.size()), block.doorTypes.begin());
    for (size_t i = 0; i < items.size() && 2 * i + 1 < block.roomItems.size(); ++i) {
        block.roomItems[2 * i] = static_cast<uint8_t>(items[i] & 0xFF);
        block.roomItems[2 * i + 1] = static_cast<uint8_t>((items[i] >> 8) & 0xFF);
    }
}

// 1622:10e7 Rooms_RestoreState(doors, items): ItemPool_Reset; per room: the door types (a 9 gets hit kind
// 4 and the open-door picture), the item list emptied (List_Clear: room 0's NULL list makes the original
// clear the first 8 bytes of the interrupt table), its count, g_cratesLeft counted anew per room (the
// last room's crates: §20 bug 6), every item dropped back (restoring).
void roomsRestoreState(GgGame& g, const BuildingBlock& block) {
    std::vector<int16_t> words(block.roomItems.size() / 2);
    for (size_t i = 0; i < words.size(); ++i)
        words[i] = static_cast<int16_t>(block.roomItems[2 * i] | (block.roomItems[2 * i + 1] << 8));
    roomsOf(g).restoreState(block.doorTypes, words, g.world.parts);
}

}  // namespace tmport::gg
