// Collision and contacts (collision.h; re/gg/docs/code_world.md §10). Owner: actors agent.
//
// Every test is Rect_Overlap (rooms.h rectOverlap) with the original's argument order: its result bits
// say where the first rectangle sticks out of the second (1 left, 2 right, 4 above, 8 below; 0xF inside).
// The rooms are the table entries (g_roomTable DS:7a04[actor +0x1a]); a floor counts in hall floors
// 0..5, so a small room uses floor - 3 from floor 3 on.
#include "gg/game/collision.h"

#include "gg/game/actors.h"
#include "gg/game/game.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// A floor of the actor's room as the room's per-floor lists index it (1adb:00fd, 0228, 03f8); -1 outside
// the six lists (the original reads past them).
int listFloor(const Room& room, int floor) {
    if (!room.isHall() && floor >= 3) floor -= 3;
    return floor >= 0 && floor < 6 ? floor : -1;
}

int overlap(const HitRecord& a, const HitRecord& b) { return rectOverlap(a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h); }
int overlap(const Sprite& a, const HitRecord& b) { return rectOverlap(a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h); }
int overlap(const HitRecord& a, const Sprite& b) { return rectOverlap(a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h); }
int overlap(const Sprite& a, const Sprite& b) { return rectOverlap(a.x, a.y, a.w, a.h, b.x, b.y, b.w, b.h); }

// The hit record of floor reference `ref` (code_world.md §5.2): bit 14 an animated object of list
// ref & 3 (+0x3e), else a static object of layer ref & 3 (+0x16); index (ref & 0x3fff) >> 2.
const HitRecord* referencedHit(const Room& room, uint16_t ref) {
    const auto list = static_cast<size_t>(ref & 3);
    const auto index = static_cast<size_t>((ref & 0x3FFF) >> 2);
    if (ref & 0x4000) return index < room.anims[list].size() ? &room.anims[list][index].hit : nullptr;
    return index < room.layers[list].size() ? &room.layers[list][index].hit : nullptr;
}

// The floor references of a floor in order: f(index, ref) for each that is not skipped (bit 15) and
// whose object exists; f returns false to stop.
template <class F>
void forFloorReferences(const Room& room, int floor, F&& f) {
    const int count = room.floorRefCount[static_cast<size_t>(floor)];
    const int first = room.floorRefFirst[static_cast<size_t>(floor)];
    for (int k = 0; k < count; ++k) {
        const int at = first + k;
        if (at < 0 || static_cast<size_t>(at) >= room.floorRefs.size()) {
            log::debug("floor references: index {} lies past the room's 50", at);
            return;
        }
        const uint16_t ref = room.floorRefs[static_cast<size_t>(at)];
        if (ref & 0x8000) continue;
        const HitRecord* hit = referencedHit(room, ref);
        if (!hit) {
            log::debug("floor references: {:#06x} names no object", ref);
            continue;
        }
        if (!f((ref & 0x3FFF) >> 2, *hit)) return;
    }
}

}  // namespace

// 1adb:0093 Actor_CollideWalls
int actorCollideWalls(GgGame& g, int actor, const HitRecord& sweep, int dir) {
    Actor& a = g.actors.actors.at(static_cast<size_t>(actor));
    // The side for a sweep that sticks out on both sides: the blocked side, else the direction of the
    // current sequence's header (through the table +0x52).
    int side = a.blocked;
    if (side == 0) {
        const AnimSequence* s = actorSequence(g, actor);
        side = s ? s->dir : 0;
    }
    int bits = 0;
    const Room* room = roomOfActor(g, actor);
    const int floor = room ? listFloor(*room, a.floor) : -1;
    if (floor >= 0) {
        for (const WallRecord& wall : room->walls[static_cast<size_t>(floor)]) {
            const HitRecord w{wall.flags, wall.x, wall.y, wall.w, wall.h};
            const int r = overlap(sweep, w);
            if (r == 0) continue;
            int found = 0;  // the table CS:01d3 by r & 3
            int edge = w.x;
            switch (r & 3) {
                case 0:  // overlapping above or below only
                    continue;
                case 1:  // the wall at the right: its left edge
                    found = 1;
                    break;
                case 2:  // the wall at the left: its right edge (the argument -1 selects it)
                    found = 2;
                    if (dir == -1) edge = w.x + w.w - 1;
                    break;
                default:  // the sweep sticks out on both sides: by the side
                    if (side < 0) {
                        found = 2;
                        edge = w.x + w.w - 1;
                    } else {
                        found = 1;
                    }
                    break;
            }
            a.wallX = static_cast<int16_t>(edge);
            bits |= found;
            break;
        }
    }
    a.wallBits = bits;
    return bits;
}

// 1adb:01ef Chimp_ProbeContacts
uint16_t chimpProbeContacts(GgGame& g, int actor, const HitRecord& probe) {
    Actor& a = g.actors.actors.at(static_cast<size_t>(actor));
    const Room* room = roomOfActor(g, actor);
    if (!room) return 0;
    uint16_t result = 0;
    // The first item the probe touches: +0x48 = 0x800 (assigned, not or-ed: the other contact bits are
    // gone until the next Actor_UpdateContacts) and +0x40 its index in the room's list.
    const auto& pool = g.world.rooms.pool();
    for (int n = 0; n < room->itemCount && static_cast<size_t>(n) < room->items.size(); ++n) {
        const ItemNode& item = pool.at(static_cast<size_t>(room->items[static_cast<size_t>(n)]));
        if (overlap(item.sprite, probe) != 0) {
            a.contacts = 0x800;
            result = 0x800;
            a.item = n;
            break;
        }
    }
    // Every floor object the probe touches adds its kind; pads and flaps also set +0x38.
    const int floor = listFloor(*room, a.floor);
    if (floor >= 0)
        forFloorReferences(*room, floor, [&](int index, const HitRecord& hit) {
            if (overlap(hit, probe) != 0) {
                result |= hit.kind;
                if (hit.kind & 0x780) a.pad = index;
            }
            return true;
        });
    return result;
}

// 1adb:03b0 Actor_UpdateContacts
void actorUpdateContacts(GgGame& g, int actor) {
    Actor& a = g.actors.actors.at(static_cast<size_t>(actor));
    const Room* room = roomOfActor(g, actor);
    a.contacts = 0;
    if (!room) return;
    // The first item of the room the actor touches.
    const auto& pool = g.world.rooms.pool();
    for (int n = 0; n < room->itemCount && static_cast<size_t>(n) < room->items.size(); ++n) {
        const ItemNode& item = pool.at(static_cast<size_t>(room->items[static_cast<size_t>(n)]));
        if (overlap(item.sprite, a.sprite) != 0) {
            a.contacts |= 0x800;
            a.item = n;
            break;
        }
    }
    // The first actor of the contact list (DS:13a0 the player's, DS:13b6 the projectile's, DS:13cc the
    // chimps') that is enabled, has contacts on and stands on the same floor of the same room: 0x1000
    // when it moves left, else 0x2000; +0x6e its id.
    if (a.contactsOn != 0) {
        const std::vector<int16_t>& list = actor == kPlayerActor       ? g.tables.contactsPlayer
                                           : actor == kProjectileActor ? g.tables.contactsProjectile
                                                                       : g.tables.contactsChimp;
        for (const int16_t j : list) {
            if (j < 0 || j >= kActorCount) continue;
            const Actor& o = g.actors.actors[static_cast<size_t>(j)];
            if (o.disabled != 0 || o.contactsOn == 0 || a.room != o.room || a.floor != o.floor) continue;
            if (overlap(a.sprite, o.sprite) == 0) continue;
            a.contacts |= o.dir < 0 ? 0x1000 : 0x2000;
            a.toucher = o.id;
            break;
        }
    }
    // The floor objects in order, until one of the kinds that stop the scan.
    const int floor = listFloor(*room, a.floor);
    if (floor < 0) return;
    forFloorReferences(*room, floor, [&](int index, const HitRecord& hit) {
        const int r = overlap(hit, a.sprite);
        if (r == 0) return true;
        a.contacts = (a.contacts | hit.kind) & 0xFFFF;
        if (hit.kind & 0x4C) {  // an open or locked door, the tube
            a.doorObj = index;
        } else if (hit.kind & 0x30) {  // a side passage
            a.doorObj = index;
        } else if (hit.kind & 0x780) {  // a pad or a flap
            a.pad = index;
        } else if (hit.kind & 1) {  // a floor edge: only the edge bit survives when the edge sticks out on
                                    // the side the actor walks to (r is the edge's side, the sequence's dir)
            a.edgeObj = index;
            if ((r == 2 && a.dir == -1) || (r == 1 && a.dir == 1)) a.contacts &= 1;
        } else if (hit.kind & 2) {  // a hole
            a.doorObj = index;
            a.edgeObj = index;
        } else {
            return true;
        }
        return false;
    });
}

}  // namespace tmport::gg
