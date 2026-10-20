#pragma once
// The rooms of segment 1622, the data and drawing half (re/gg/docs/code_world.md §5-§8): the room
// sets ('A' 0x347..0x34f, one per building and level), their typed records, the run-time room table
// with the items lying in the rooms, Rooms_Load (with the deal of parts and crates, the first rand()
// calls of a building set-up), Room_Enter's drawing of the background into the back page and the
// per-frame layers. Music, the item routines of play (remove, drop, place in a random room), doors
// and the save are wave 3's.
//
// Room set: u16 count, then per room a 0x120-byte header and its data: static objects (0x20 bytes,
// layer 3 first, then 2, 1, 0), animated objects (0x48 bytes, lists 0..3), then 3 groups x 6 floors
// of 10-byte records (walls, left and right jump spots).
//
//   header +0x00 flags (bit 0 hall, bit 1 small room under hall floors 3..5)  +0x02 4 static counts
//   +0x0A 4 animated counts  +0x12/+0x36/+0x5A 6 record counts per group  (+0x1E, +0x42, +0x66,
//   +0x7E, +0x8E: far pointers patched at load)  +0x9E items in the room  +0xA0 far item list
//   +0xA4 6 floor reference counts  +0xB0 6 first indexes  +0xBC 50 floor references
//
// The engine keeps one copy of each room header: g_room DS:08c6, the original's copy of the
// displayed room's header, shares its arrays with the table entry and only mirrors the item count
// (DS:0964), so the table entry serves for both.

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "gg/gfx/sprites.h"

namespace tmport {
class BorlandRand;
}

namespace tmport::gg {

class Archive;
class GgExe;
class PartCatalogue;
class PartDeck;
class PicCache;
class Screen;

// {u16 kind, x, y, w, h}: the hit records of objects and items (code_world.md §5.3, §10).
struct HitRecord {
    uint16_t kind = 0;
    int16_t x = 0, y = 0, w = 0, h = 0;
};

// A static object (0x20 bytes, code_world.md §5.3).
struct RoomObject {
    static constexpr uint16_t kNoDraw = 0x14;      // 0x04 / 0x10: not drawn
    static constexpr uint16_t kHideRequest = 0x40; // Obj_Hide*: toggled into 0x10 at the next draw

    Sprite sprite;            // +0x00 flags, x, y, w, h, +0x0A picture (Room_Enter)
    uint16_t pictureId = 0;   // +0x0E
    int16_t type = 0;         // +0x10
    uint16_t link = 0;        // +0x12: (link id << 6) | target room for doors and passages
    int16_t field14 = 0;      // +0x14 (not read by the world)
    HitRecord hit;            // +0x16

    int targetRoom() const { return link & 0xF; }
    int linkId() const { return link >> 6; }
};

// An animated object (0x48 bytes, code_world.md §5.4): pads and flaps of the halls.
struct RoomAnim {
    static constexpr uint16_t kPaused = 0x04;
    static constexpr uint16_t kInactive = 0x10;

    Sprite sprite;            // +0x00..+0x0D (flags 0x04 paused, 0x10 inactive, bits 0..1 mirror)
    uint16_t bundle = 0;      // +0x0E sprite bundle id
    int16_t field10 = 0;      // +0x10
    int16_t countdown = 0;    // +0x12
    int16_t reload = 0;       // +0x14
    int16_t pos = 0;          // +0x16 script position
    int16_t field18 = 0;      // +0x18
    std::array<int16_t, 12> script{};  // +0x26: >= 0 frame, < 0 command (Room_DrawAnimLayer)
    HitRecord hit;            // +0x3E
    // +0x1A: the picture slots of the bundle (PicCache_LoadBundle at Room_Enter).
    const std::vector<std::span<const uint8_t>>* pictures = nullptr;
};

struct WallRecord {   // group 0: {u16 flags, x, y, w, h}
    uint16_t flags = 0;
    int16_t x = 0, y = 0, w = 0, h = 0;
};
struct JumpRecord {   // groups 1 and 2: {u16 trigger sequence, x, u16 replacement, w, u16 pad index}
    int16_t trigger = 0, x = 0;
    int16_t replacement = 0, w = 0;
    int16_t pad = 0;
};

// An item node (60 x 0x2C bytes at g_itemPool DS:6fa8; code_world.md §7.1): a part or a banana crate.
struct ItemNode {
    static constexpr int kCrate = 1000;
    static constexpr uint16_t kHallPicture = 0x343;   // + 1 for a part already scored (0x4000)
    static constexpr uint16_t kSmallPicture = 0x332;

    bool free = true;         // +0x08
    Sprite sprite;            // +0x0C {0, x, y, w, h, picture}: also the contact rectangle
    uint16_t pictureId = 0;   // +0x1A
    int kind = 0;             // +0x1E: part index, or 1000
    int16_t field20 = 0;      // +0x20
    HitRecord hit;            // +0x22 {0x800, x, y, w, h}
};

struct Room {
    uint16_t flags = 0;
    std::array<std::vector<RoomObject>, 4> layers;        // static objects per layer
    std::array<std::vector<RoomAnim>, 4> anims;           // animated objects per list
    std::array<std::vector<WallRecord>, 6> walls;         // group 0, per floor
    std::array<std::vector<JumpRecord>, 6> jumpsLeft;     // group 1
    std::array<std::vector<JumpRecord>, 6> jumpsRight;    // group 2
    std::array<uint16_t, 6> floorRefCount{};
    std::array<uint16_t, 6> floorRefFirst{};
    std::array<uint16_t, 50> floorRefs{};
    int itemCount = 0;          // +0x9E
    bool hasItemList = false;   // +0xA0 (List_New for rooms >= 1)
    std::vector<int> items;     // the list, as indexes into the item pool, in list order

    bool isHall() const { return (flags & 1) != 0; }   // 1622:090d Room_IsHall
};

// A parsed room set.
struct RoomSet {
    std::vector<Room> rooms;
    // The bytes consumed (every shipped set is consumed exactly).
    size_t used = 0;
    // Throws tmport::FormatError when a room runs past the payload.
    static RoomSet parse(std::span<const uint8_t> payload);
};

// 1adb:000c Rect_Overlap(a, b): 0 unless the rectangles overlap (signed, positive area); else bits
// 1 a.x < b.x, 2 a.right > b.right, 4 a.y < b.y, 8 a.bottom > b.bottom, or 0xF when none.
int rectOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh);

// What Room_Enter does about the music (RoomMusic_SelectLayers / RoomMusic_Start / SndRes_StopMusic).
// The game passes GgAudio's room music (MusicCues::roomMusicSelectLayers / roomMusicStart, as the
// wave 2 preview does in gg/app/preview.cpp); empty = silent (the tools).
using RoomMusicHook = std::function<void(int room)>;

// The rooms of the building in progress (g_roomTable DS:7a04 and the state of §5.7).
class Rooms {
public:
    static constexpr int kMaxRooms = 16;
    static constexpr int kItemPoolSize = 60;
    static constexpr uint16_t kItemCountsFirst = 0x0350;  // 'I' 0x350 + race index

    // The room-set id of a race (g_roomSetIds DS:09e8[race]).
    static uint16_t roomSetId(const GgExe& exe, int raceIndex);

    // 1622:0085 Rooms_Load (without the music bank): the room set of the race, the item counts,
    // Parts_ShuffleDeck(I[0]), the crate counts, and the items of every room >= 1 with I[r] != 0
    // (Room_PlaceItems with parts dealt from the deck). The display room becomes 0.
    void load(const Archive& archive, const GgExe& exe, int raceIndex, const PartCatalogue& parts, PartDeck& deck,
              BorlandRand& rng);
    // The same from parsed data (tests).
    void load(RoomSet set, std::vector<int16_t> itemCounts, const PartCatalogue& parts, PartDeck& deck,
              BorlandRand& rng);

    int count() const { return static_cast<int>(rooms_.size()); }  // g_roomCount DS:08c4
    Room& room(int r) { return rooms_.at(static_cast<size_t>(r)); }
    const Room& room(int r) const { return rooms_.at(static_cast<size_t>(r)); }
    std::array<ItemNode, kItemPoolSize>& pool() { return pool_; }
    const std::array<ItemNode, kItemPoolSize>& pool() const { return pool_; }
    const std::vector<int16_t>& itemCounts() const { return itemCounts_; }  // g_roomItemCounts DS:79fc

    int displayRoom = 0;      // g_displayRoom DS:08c2
    bool roomLoaded = false;  // g_roomLoaded DS:08c0
    int cratesLeft = 0;       // g_cratesLeft DS:7a00
    int cratesTotal = 0;      // g_cratesTotal DS:7a02

    // 1622:0071 ItemPool_Reset / 1622:0a61 ItemPool_Alloc (the first free node; -1 when full: the
    // original walks past the pool).
    void resetItemPool();
    int allocItem();
    // 1622:0a76 Room_PlaceItems(room, n, kind): kind -1 deals parts from the deck, 1000 crates, else
    // that part. Returns 0 when it runs out of cells (the last shuffled cell is never tried).
    int placeItems(int r, int n, int kind, const PartCatalogue& parts, PartDeck& deck, BorlandRand& rng);
    // The items of room r in list order.
    std::vector<const ItemNode*> itemsOf(int r) const;
    // 1622:0df0 Room_DropItemAt(room, x, floor, kind, restoring): a node at x on a hall floor (in a
    // small room floors above 2 count from 3), sizes and picture as Room_PlaceItems (+1 for a part
    // flagged 0x4000); unless restoring the room count goes up and, in the displayed room, the
    // picture is loaded (`cache`); appended to the room's list.
    void dropItemAt(int r, int x, int floor, int kind, bool restoring, const PartCatalogue& parts,
                    PicCache* cache = nullptr);

    // The building block's room part (code_world.md §4.2, §7.5): 1622:0fb3 Rooms_SaveState writes,
    // for every room, a byte 7 or 9 per layer-0 object of type 7 or 9 (block +0x26c) and its items
    // as {u16 n, n x {x, floor (Room_HallFloorFromY / Room_SmallFloorFromY), kind}} (block +0x302).
    void saveState(std::vector<uint8_t>& doors, std::vector<int16_t>& items) const;
    // 1622:10e7 Rooms_RestoreState: ItemPool_Reset; per room the door types (a 9 gets hit kind 4 and
    // the open-door picture 0x33c in halls, 0x30e in small rooms), the list cleared, the count, the
    // crates counted anew per room (g_cratesLeft = the last room's crates: §20 bug 6) and every item
    // dropped back (restoring). Reads past the given spans as zeros (the original reads on).
    void restoreState(std::span<const uint8_t> doors, std::span<const int16_t> items, const PartCatalogue& parts);
    // The door half alone; returns the bytes used.
    size_t restoreDoors(std::span<const uint8_t> doors);

    // 1622:0411 Room_Enter, the drawing: Screen_ClearPlayArea, the music hook, every type-3 object
    // drawn as it is met (layers 3 down to 0, through Pic_Read) and every other object's picture
    // cached, every animated object's bundle loaded (its picture = slot 0), then the layer-0 objects
    // that are not type 3 drawn (baked into the background) and released, the items' pictures
    // cached, Screen_SavePlayArea.
    void enter(int r, Screen& screen, PicCache& cache, const RoomMusicHook& music = {});
    // 1622:0922 Room_DrawLayer(L): hide requests toggled, visible objects queued; L = 3 also queues
    // the items of the displayed room.
    void drawLayer(int layer, SpriteList& sprites);
    // 1622:0706 Room_DrawAnimLayer(L): steps the animated objects of list L in the halls 1..3 (all of
    // them, displayed or not) and queues those of the displayed room.
    void drawAnimLayer(int list, SpriteList& sprites);
    // For tools that redraw a frame from a save state: queues the displayed room's animated objects
    // of list L as they are (Room_DrawAnimLayer without the stepping).
    void queueAnims(int list, SpriteList& sprites) const;
    // 1622:06cf Room_QueueEntryLayers: Gfx_RestoreRect(0, 9, 320, 155), anim 0, layer 1, anims 1, 2.
    void queueEntryLayers(Screen& screen, SpriteList& sprites);

private:
    std::vector<Room> rooms_;
    std::vector<int16_t> itemCounts_;
    std::array<ItemNode, kItemPoolSize> pool_{};
};

}  // namespace tmport::gg
