#include "gg/game/rooms.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/parts.h"
#include "gg/game/pic_cache.h"
#include "gg/gfx/screen.h"
#include "rtl/borland_rand.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr size_t kHeaderSize = 0x120;
constexpr size_t kObjectSize = 0x20;
constexpr size_t kAnimSize = 0x48;
constexpr size_t kRecordSize = 10;

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}
uint16_t u16(std::span<const uint8_t> p, size_t o) { return static_cast<uint16_t>(s16(p, o)); }

HitRecord hitAt(std::span<const uint8_t> p, size_t o) {
    return {u16(p, o), s16(p, o + 2), s16(p, o + 4), s16(p, o + 6), s16(p, o + 8)};
}

// The first 10 bytes of a sprite entry {flags, x, y, w, h}.
Sprite spriteAt(std::span<const uint8_t> p, size_t o) {
    Sprite s;
    s.flags = u16(p, o);
    s.x = s16(p, o + 2);
    s.y = s16(p, o + 4);
    s.w = s16(p, o + 6);
    s.h = s16(p, o + 8);
    return s;
}

void need(std::span<const uint8_t> p, size_t end, int room) {
    if (end > p.size()) throw FormatError(std::format("room set: room {} runs past the end ({} > {})", room, end, p.size()));
}

// Obj_SetSizeFromPicture (15df:0414)
void sizeFromPicture(Sprite& s) {
    const auto& p = s.picture;
    s.w = p.size() >= 10 ? (p[6] | (p[7] << 8)) : 0;
    s.h = p.size() >= 10 ? (p[8] | (p[9] << 8)) : 0;
}

// Rect_Overlap of two sprite rectangles.
int overlap(const Sprite& a, int bx, int by, int bw, int bh) { return rectOverlap(a.x, a.y, a.w, a.h, bx, by, bw, bh); }

}  // namespace

// 1adb:000c Rect_Overlap (Pascal: a is the first parameter, pushed first)
int rectOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    const int ar = ax + aw, ab = ay + ah, br = bx + bw, bb = by + bh;
    if (!(ax < br && bx < ar && ay < bb && by < ab)) return 0;
    int r = 0;
    if (ax < bx) r |= 1;
    if (ar > br) r |= 2;
    if (ay < by) r |= 4;
    if (ab > bb) r |= 8;
    return r ? r : 0xF;
}

RoomSet RoomSet::parse(std::span<const uint8_t> p) {
    RoomSet set;
    const int n = s16(p, 0);
    if (n < 0 || n > Rooms::kMaxRooms) throw FormatError(std::format("room set: {} rooms", n));
    size_t o = 2;
    for (int r = 0; r < n; ++r) {
        need(p, o + kHeaderSize, r);
        const auto h = p.subspan(o, kHeaderSize);
        Room room;
        room.flags = u16(h, 0x00);
        for (size_t k = 0; k < 6; ++k) {
            room.floorRefCount[k] = u16(h, 0xA4 + 2 * k);
            room.floorRefFirst[k] = u16(h, 0xB0 + 2 * k);
        }
        for (size_t k = 0; k < 50; ++k) room.floorRefs[k] = u16(h, 0xBC + 2 * k);
        room.itemCount = 0;  // +0x9E: 0 in the files; Rooms_Load sets it
        o += kHeaderSize;
        // Rooms_Load 1622:0085 patches the pointers in this order: layers 3..0, lists 0..3, groups.
        for (int layer = 3; layer >= 0; --layer) {
            const int count = s16(h, 0x02 + 2 * static_cast<size_t>(layer));
            need(p, o + kObjectSize * static_cast<size_t>(std::max(count, 0)), r);
            for (int i = 0; i < count; ++i, o += kObjectSize) {
                RoomObject obj;
                obj.sprite = spriteAt(p, o);
                obj.pictureId = u16(p, o + 0x0E);
                obj.type = s16(p, o + 0x10);
                obj.link = u16(p, o + 0x12);
                obj.field14 = s16(p, o + 0x14);
                obj.hit = hitAt(p, o + 0x16);
                room.layers[static_cast<size_t>(layer)].push_back(obj);
            }
        }
        for (int list = 0; list < 4; ++list) {
            const int count = s16(h, 0x0A + 2 * static_cast<size_t>(list));
            need(p, o + kAnimSize * static_cast<size_t>(std::max(count, 0)), r);
            for (int i = 0; i < count; ++i, o += kAnimSize) {
                RoomAnim a;
                a.sprite = spriteAt(p, o);
                a.bundle = u16(p, o + 0x0E);
                a.field10 = s16(p, o + 0x10);
                a.countdown = s16(p, o + 0x12);
                a.reload = s16(p, o + 0x14);
                a.pos = s16(p, o + 0x16);
                a.field18 = s16(p, o + 0x18);
                for (size_t k = 0; k < a.script.size(); ++k) a.script[k] = s16(p, o + 0x26 + 2 * k);
                a.hit = hitAt(p, o + 0x3E);
                room.anims[static_cast<size_t>(list)].push_back(a);
            }
        }
        for (size_t k = 0; k < 6; ++k) {
            const int count = s16(h, 0x12 + 2 * k);
            need(p, o + kRecordSize * static_cast<size_t>(std::max(count, 0)), r);
            for (int i = 0; i < count; ++i, o += kRecordSize)
                room.walls[k].push_back({u16(p, o), s16(p, o + 2), s16(p, o + 4), s16(p, o + 6), s16(p, o + 8)});
        }
        for (int g = 1; g <= 2; ++g)
            for (size_t k = 0; k < 6; ++k) {
                const int count = s16(h, (g == 1 ? 0x36 : 0x5A) + 2 * k);
                need(p, o + kRecordSize * static_cast<size_t>(std::max(count, 0)), r);
                auto& lists = g == 1 ? room.jumpsLeft : room.jumpsRight;
                for (int i = 0; i < count; ++i, o += kRecordSize)
                    lists[k].push_back({s16(p, o), s16(p, o + 2), s16(p, o + 4), s16(p, o + 6), s16(p, o + 8)});
            }
        set.rooms.push_back(std::move(room));
    }
    set.used = o;
    return set;
}

uint16_t Rooms::roomSetId(const GgExe& exe, int raceIndex) {
    if (raceIndex < 0 || static_cast<size_t>(raceIndex) >= dg::kRoomSetIdsCount)
        throw std::out_of_range(std::format("race index {}", raceIndex));
    return exe.ds16(static_cast<uint16_t>(dg::kRoomSetIds + 2 * raceIndex));
}

// 1622:0085 Rooms_Load
void Rooms::load(const Archive& archive, const GgExe& exe, int raceIndex, const PartCatalogue& parts, PartDeck& deck,
                 BorlandRand& rng) {
    RoomSet set = RoomSet::parse(archive.read(roomSetId(exe, raceIndex)));  // Res_LoadCopy
    const auto counts = archive.read(static_cast<uint16_t>(kItemCountsFirst + raceIndex));  // Res_LoadCopy
    std::vector<int16_t> itemCounts;
    for (size_t o = 0; o + 2 <= counts.size(); o += 2) itemCounts.push_back(s16(counts, o));
    load(std::move(set), std::move(itemCounts), parts, deck, rng);
}

void Rooms::load(RoomSet set, std::vector<int16_t> itemCounts, const PartCatalogue& parts, PartDeck& deck,
                 BorlandRand& rng) {
    rooms_ = std::move(set.rooms);
    itemCounts_ = std::move(itemCounts);
    if (itemCounts_.empty()) itemCounts_.push_back(0);
    resetItemPool();
    const int total = itemCounts_[0];
    deck.shuffle(total, static_cast<int>(parts.count()), rng);  // Parts_ShuffleDeck(I[0])
    cratesLeft = cratesTotal = total - static_cast<int>(parts.count());
    for (int r = 0; r < count(); ++r) {
        Room& room = rooms_[static_cast<size_t>(r)];
        room.items.clear();
        room.itemCount = 0;
        room.hasItemList = r != 0;  // List_New for rooms >= 1; room 0's list stays NULL
        if (r == 0) continue;
        const int n = static_cast<size_t>(r) < itemCounts_.size() ? itemCounts_[static_cast<size_t>(r)] : 0;
        if (n != 0) placeItems(r, n, -1, parts, deck, rng);
    }
    displayRoom = 0;
    roomLoaded = false;
}

// 1622:0071 ItemPool_Reset
void Rooms::resetItemPool() {
    for (ItemNode& n : pool_) n.free = true;
}

// 1622:0a61 ItemPool_Alloc
int Rooms::allocItem() {
    for (size_t i = 0; i < pool_.size(); ++i)
        if (pool_[i].free) return static_cast<int>(i);
    log::debug("ItemPool_Alloc: no free item node (the original walks past the pool)");
    return -1;
}

// 1622:0a76 Room_PlaceItems
int Rooms::placeItems(int r, int n, int kind, const PartCatalogue& parts, PartDeck& deck, BorlandRand& rng) {
    Room& room = rooms_.at(static_cast<size_t>(r));
    const bool hall = room.isHall();
    const int rows = hall ? 6 : 3, cols = hall ? 15 : 7;
    const int x0 = hall ? 10 : 15, y0 = hall ? 26 : 46;
    const int colStep = hall ? 20 : 40, rowStep = hall ? 26 : 52;
    const int probeH = hall ? 7 : 13, itemW = hall ? 10 : 20;
    const uint16_t picture = hall ? ItemNode::kHallPicture : ItemNode::kSmallPicture;
    const int cells = rows * cols;
    std::vector<int> order(static_cast<size_t>(cells));
    randShuffle(order, rng);
    int k = 0;
    for (int placed = 0; placed < n; ++placed) {
        const int node = allocItem();
        int x = 0, y = 0;
        for (bool found = false; !found;) {
            const int c = order[static_cast<size_t>(k)];
            y = (c / cols) * rowStep + y0;
            x = (c % cols) * colStep + x0;
            if (++k >= cells) return 0;  // the last shuffled cell is never tried (code_world.md §20 bug 5)
            bool hit = false;
            for (size_t i = 0; !hit && i < room.items.size(); ++i)
                hit = overlap(pool_[static_cast<size_t>(room.items[i])].sprite, x, y, colStep, probeH) != 0;
            for (size_t i = 0; !hit && i < room.layers[0].size(); ++i) {
                const RoomObject& o = room.layers[0][i];
                hit = o.type > 3 && overlap(o.sprite, x, y, colStep, probeH) != 0;
            }
            for (size_t i = 0; !hit && i < room.anims[0].size(); ++i)
                hit = overlap(room.anims[0][i].sprite, x, y, colStep, probeH) != 0;
            found = !hit;
        }
        if (node < 0) continue;
        ItemNode& it = pool_[static_cast<size_t>(node)];
        x += colStep / 2;
        it.sprite = Sprite{0, x, y, itemW, probeH, {}};
        it.pictureId = picture;
        if (kind == ItemNode::kCrate) {
            it.kind = ItemNode::kCrate;
        } else if (kind == -1) {
            it.kind = deck.dealNext();
        } else {
            it.kind = kind;
            if (static_cast<size_t>(kind) < parts.parts.size() && (parts.parts[static_cast<size_t>(kind)].flags & PartRecord::kNoPoints))
                it.pictureId = static_cast<uint16_t>(picture + 1);
        }
        it.field20 = 0;
        it.hit = {0x800, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(itemW), static_cast<int16_t>(probeH)};
        ++room.itemCount;
        it.free = false;
        room.items.push_back(node);  // List_InsertAt(list, node, 0xffff)
    }
    return 1;
}

std::vector<const ItemNode*> Rooms::itemsOf(int r) const {
    std::vector<const ItemNode*> out;
    for (const int i : room(r).items) out.push_back(&pool_[static_cast<size_t>(i)]);
    return out;
}

// 1622:0df0 Room_DropItemAt
void Rooms::dropItemAt(int r, int x, int floor, int kind, bool restoring, const PartCatalogue& parts, PicCache* cache) {
    Room& room = rooms_.at(static_cast<size_t>(r));
    const int node = allocItem();
    if (node < 0) return;
    int y, w, h;
    uint16_t picture;
    if (!room.isHall()) {
        if (floor > 2) floor -= 3;
        y = floor * 52 + 46;
        w = 20;
        h = 13;
        picture = ItemNode::kSmallPicture;
    } else {
        y = floor * 26 + 26;
        w = 10;
        h = 7;
        picture = ItemNode::kHallPicture;
    }
    if (kind != ItemNode::kCrate && kind >= 0 && static_cast<size_t>(kind) < parts.parts.size() &&
        (parts.parts[static_cast<size_t>(kind)].flags & PartRecord::kNoPoints))
        picture = static_cast<uint16_t>(picture + 1);
    ItemNode& it = pool_[static_cast<size_t>(node)];
    it.sprite = Sprite{0, x, y, w, h, {}};
    it.pictureId = picture;
    it.kind = kind;
    it.field20 = 0;
    it.hit = {0x800, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w), static_cast<int16_t>(h)};
    if (!restoring) {
        ++room.itemCount;
        if (r == displayRoom && cache) it.sprite.picture = cache->load(picture);
    }
    it.free = false;
    room.items.push_back(node);
}

namespace {

// 1622:000a Room_HallFloorFromY / 1622:004d Room_SmallFloorFromY
int floorFromY(bool hall, int y) {
    if (!hall) return y < 0x2F ? 0 : y < 99 ? 1 : 2;
    return y < 0x1C ? 0 : y < 0x36 ? 1 : y < 0x50 ? 2 : y < 0x6A ? 3 : y < 0x84 ? 4 : 5;
}

}  // namespace

// 1622:0fb3 Rooms_SaveState
void Rooms::saveState(std::vector<uint8_t>& doors, std::vector<int16_t>& items) const {
    for (const Room& room : rooms_) {
        for (const RoomObject& o : room.layers[0]) {
            if (o.type == 7) doors.push_back(7);
            if (o.type == 9) doors.push_back(9);
        }
        items.push_back(static_cast<int16_t>(room.items.size()));  // List_Count
        for (const int i : room.items) {
            const ItemNode& it = pool_[static_cast<size_t>(i)];
            items.push_back(static_cast<int16_t>(it.sprite.x));
            items.push_back(static_cast<int16_t>(floorFromY(room.isHall(), it.sprite.y)));
            items.push_back(static_cast<int16_t>(it.kind));
        }
    }
}

size_t Rooms::restoreDoors(std::span<const uint8_t> doors) {
    size_t k = 0;
    for (Room& room : rooms_)
        for (RoomObject& o : room.layers[0]) {
            if (o.type != 7 && o.type != 9) continue;
            o.type = static_cast<int8_t>(k < doors.size() ? doors[k] : 0);  // CBW of the byte
            ++k;
            if (o.type == 9) {
                o.hit.kind = 4;
                o.pictureId = room.isHall() ? 0x33C : 0x30E;
            }
        }
    return k;
}

// 1622:10e7 Rooms_RestoreState
void Rooms::restoreState(std::span<const uint8_t> doors, std::span<const int16_t> items, const PartCatalogue& parts) {
    resetItemPool();
    size_t k = 0, d = 0;
    auto next = [&] { return k < items.size() ? items[k++] : int16_t{0}; };
    for (int r = 0; r < count(); ++r) {
        Room& room = rooms_[static_cast<size_t>(r)];
        for (RoomObject& o : room.layers[0]) {
            if (o.type != 7 && o.type != 9) continue;
            o.type = static_cast<int8_t>(d < doors.size() ? doors[d] : 0);
            ++d;
            if (o.type == 9) {
                o.hit.kind = 4;
                o.pictureId = room.isHall() ? 0x33C : 0x30E;
            }
        }
        room.items.clear();  // List_Clear
        const int n = next();
        room.itemCount = n;
        cratesLeft = 0;  // per room (code_world.md §20 bug 6)
        for (int i = 0; i < n; ++i) {
            const int x = next(), floor = next(), kind = next();
            dropItemAt(r, x, floor, kind, true, parts);
            if (kind == ItemNode::kCrate) ++cratesLeft;
        }
    }
}

// 1622:0411 Room_Enter
void Rooms::enter(int r, Screen& screen, PicCache& cache, const RoomMusicHook& music) {
    Room& room = rooms_.at(static_cast<size_t>(r));
    displayRoom = r;  // memcpy(g_room, table[r], 0x120); g_displayRoom = r
    screen.clearPlayArea();
    if (music) music(r);
    roomLoaded = true;
    for (int layer = 3; layer >= 0; --layer) {
        for (RoomObject& o : room.layers[static_cast<size_t>(layer)]) {
            if (o.type == 3) {
                o.sprite.picture = cache.read(o.pictureId);  // Pic_Read: the shared buffer
                screen.objDrawKeyed(o.sprite.flags, o.sprite.x, o.sprite.y, 0, o.sprite.picture);
            } else {
                o.sprite.picture = cache.load(o.pictureId);
            }
        }
        for (RoomAnim& a : room.anims[static_cast<size_t>(layer)]) {
            a.pictures = &cache.loadBundle(a.bundle);
            a.sprite.picture = a.pictures->empty() ? std::span<const uint8_t>{} : a.pictures->front();  // cache[slot[0]]
        }
    }
    for (RoomObject& o : room.layers[0]) {
        if (o.type == 3) continue;
        screen.objDrawKeyed(o.sprite.flags, o.sprite.x, o.sprite.y, 0, o.sprite.picture);
        cache.release(o.pictureId);  // baked into the background
    }
    // The type-3 pictures were in the shared buffer only, and the layer-0 pictures are released:
    // the engine drops the stale pointers the original keeps (no code draws them again).
    for (auto& layer : room.layers)
        for (RoomObject& o : layer)
            if (o.type == 3) o.sprite.picture = {};
    for (RoomObject& o : room.layers[0])
        if (!cache.cached(o.pictureId)) o.sprite.picture = {};
    int n = 0;
    for (const int i : room.items) {
        if (n++ >= room.itemCount) break;
        ItemNode& it = pool_[static_cast<size_t>(i)];
        it.sprite.picture = cache.load(it.pictureId);
    }
    screen.savePlayArea();
}

// 1622:0922 Room_DrawLayer
void Rooms::drawLayer(int layer, SpriteList& sprites) {
    Room& room = rooms_.at(static_cast<size_t>(displayRoom));
    for (RoomObject& o : room.layers[static_cast<size_t>(layer)]) {
        if (o.sprite.flags & RoomObject::kHideRequest) o.sprite.flags ^= 0x50;
        if (!(o.sprite.flags & RoomObject::kNoDraw)) sprites.add(o.sprite);
    }
    if (layer != 3) return;
    int n = 0;
    for (const int i : room.items) {
        if (n++ >= room.itemCount) break;  // the count DS:0964 bounds the walk
        sprites.add(pool_[static_cast<size_t>(i)].sprite);
    }
}

// 1622:0706 Room_DrawAnimLayer
void Rooms::drawAnimLayer(int list, SpriteList& sprites) {
    for (int r = 1; r <= 3 && r < count(); ++r) {
        const bool displayed = r == displayRoom;
        for (RoomAnim& a : rooms_[static_cast<size_t>(r)].anims[static_cast<size_t>(list)]) {
            if (a.sprite.flags & RoomAnim::kInactive) continue;
            bool draw = true;
            const int left = a.countdown - 1;  // DEC, JL
            if (!(a.sprite.flags & RoomAnim::kPaused)) a.countdown = static_cast<int16_t>(left);
            if (!(a.sprite.flags & RoomAnim::kPaused) && left < 0) {
                a.countdown = a.reload;
                for (int guard = 0; guard < 64; ++guard) {
                    const int pos = a.pos;
                    if (pos < 0 || static_cast<size_t>(pos) >= a.script.size()) {
                        // The original reads on into the hit record; no shipped script gets here.
                        log::debug("Room_DrawAnimLayer: room {} script position {} past the 12 entries", r, pos);
                        break;
                    }
                    a.pos = static_cast<int16_t>(pos + 1);
                    const int e = a.script[static_cast<size_t>(pos)];
                    if (e >= 0) {  // a frame: its picture in the displayed room only
                        if (displayed && a.pictures && static_cast<size_t>(e) < a.pictures->size()) {
                            a.sprite.picture = (*a.pictures)[static_cast<size_t>(e)];
                            sizeFromPicture(a.sprite);
                        }
                        break;
                    }
                    if (e == -1) {
                        a.pos = 0;
                    } else if (e == -3) {
                        a.sprite.flags |= RoomAnim::kPaused;
                        a.pos = 0;
                    } else if (e == -5) {
                        a.pos = 0;
                        a.sprite.flags |= RoomAnim::kPaused;
                        draw = false;
                        break;
                    } else if (e == -8) {
                        a.sprite.picture = {};
                        draw = false;
                        break;
                    }  // -2, -4, -6, -7 and below -8: skipped
                }
            }
            if (draw && displayed && !a.sprite.picture.empty()) sprites.add(a.sprite);
        }
    }
}

void Rooms::queueAnims(int list, SpriteList& sprites) const {
    if (displayRoom < 1 || displayRoom > 3 || displayRoom >= count()) return;  // halls only
    for (const RoomAnim& a : rooms_[static_cast<size_t>(displayRoom)].anims[static_cast<size_t>(list)])
        if (!(a.sprite.flags & RoomAnim::kInactive) && !a.sprite.picture.empty()) sprites.add(a.sprite);
}

// 1622:06cf Room_QueueEntryLayers
void Rooms::queueEntryLayers(Screen& screen, SpriteList& sprites) {
    screen.restoreRect(0, 9, 320, 155);
    drawAnimLayer(0, sprites);
    drawLayer(1, sprites);
    drawAnimLayer(1, sprites);
    drawAnimLayer(2, sprites);
}

}  // namespace tmport::gg
