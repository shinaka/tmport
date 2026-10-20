// The Cyber Chimps (chimps.h; ssg.exe ovl00 2a80 and the DGROUP tables, re/gg/docs/code_world.md §13).
// Owner: actors agent.
//
// A chimp is data-driven: its move list (g_chimpMoves DS:1d2e[seq], GgTables::chimpMoves) probes the
// room around it and fires with a chance per mille (one R(1000) per probe that touches, in entry order,
// until one succeeds), then the transition list (g_chimpTransitions DS:1f10[next]) turns the contacts of
// the frame into the next sequence. The sequences do the rest through the step interpreter.
#include "gg/game/chimps.h"

#include "gg/game/actors.h"
#include "gg/game/collision.h"
#include "gg/game/game.h"
#include "gg/game/parts.h"
#include "gg/game/rooms.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// The sequences the brain and the room changes name (code_world.md §13.4).
constexpr int kSeqWalkLeft = 2, kSeqWalkRight = 3;
constexpr int kSeqTurnToRight = 10, kSeqTurnToLeft = 11;      // blocked at the left / right
constexpr int kSeqOutOfDoor = 21;
constexpr int kSeqInLeft = 24, kSeqInRight = 25;             // in through a passage
constexpr int kSeqTakeLeft = 28, kSeqTakeRight = 29;
constexpr int kSeqFromHoleLeft = 34, kSeqFromHoleRight = 35;
constexpr int kDoorCooldown = 0x2DA;  // brain ticks (20 s) before doors attract again
constexpr uint16_t kDoorKinds = 4;    // an open door: probed with a 4-px probe at the centre
constexpr int16_t kStay = -2;         // a move list that only lets the transitions act

Actor& chimp(GgGame& g, int i) { return g.actors.actors.at(static_cast<size_t>(i)); }
int w16(int v) { return static_cast<int16_t>(v); }
bool bigChimps(GgGame& g) { return g.player.level > 2; }
int16_t* roomChimpCount(GgGame& g, int room) {
    auto& counts = g.actors.roomChimpCount;
    if (room >= 0 && static_cast<size_t>(room) < counts.size()) return &counts[static_cast<size_t>(room)];
    log::debug("g_roomChimpCount: room {} lies outside the 16 words", room);
    return nullptr;
}

}  // namespace

// 2a80:0000 Chimp_TakePart (op -10)
void chimpTakePart(GgGame& g, int actor) {
    Actor& c = chimp(g, actor);
    const int node = roomGetItem(g, c.room, c.item);
    if (node < 0) return;  // an empty list: the original reads the NULL node
    const ItemNode& item = g.world.rooms.pool().at(static_cast<size_t>(node));
    if (item.free) return;  // taken this frame already
    if (item.kind == ItemNode::kCrate) {  // a crate moves to a random other room (rand)
        roomRemoveItem(g, c.room, node);
        itemPlaceInRandomRoom(g, ItemNode::kCrate);
        return;
    }
    if (partListCountHeldBy(g, c.id) != 0) return;  // one part per chimp
    // Quirk kept (code_world.md §20 item 14): when the chimps hold two of the type Actor_AddPart refuses
    // the part and it is removed from the room all the same: lost for the visit.
    actorAddPart(g, c.id, item.kind);
    roomRemoveItem(g, c.room, node);
}

// 2a80:00a1 Chimp_DropPart (op -33, stunned)
void chimpDropPart(GgGame& g, int actor) {
    Actor& c = chimp(g, actor);
    const int part = partListFindHeld(g, g.actors.chimpParts, actor, 0);
    if (part < 0) return;
    actorRemovePart(g, actor, part, true);
    roomDropItemAt(g, c.room, c.anchorX, c.floor, part, false);  // where the chimp stands
}

// 2a80:0112 Chimp_Brain
int chimpBrain(GgGame& g, int actor) {
    Actor& c = chimp(g, actor);
    int next = -1;
    // The contacts Actor_UpdateContacts has just computed; the probes below may overwrite +0x48.
    const uint32_t input = static_cast<uint32_t>(c.contacts & 0xFFFF) << 16;
    if (c.doorCooldown >= 0) --c.doorCooldown;
    const std::optional<ChimpMoveList>* moves =
        c.seq >= 0 && c.seq < GgTables::kChimpSequences ? &g.tables.chimpMoves[static_cast<size_t>(c.seq)] : nullptr;
    c.field94 = 0;
    if (moves && moves->has_value()) {
        const ChimpMoveList& m = **moves;
        if (m.marker == kStay) {
            next = c.seq;
        } else {
            next = static_cast<int16_t>(m.defaultNext);
            for (const ChimpMoveEntry& e : m.entries) {
                // The probe: the chimp's rectangle; a door entry looks with 4 px at the centre and not
                // while the cooldown runs.
                HitRecord probe{c.sprite.flags, static_cast<int16_t>(c.sprite.x), static_cast<int16_t>(c.sprite.y),
                                static_cast<int16_t>(c.sprite.w), static_cast<int16_t>(c.sprite.h)};
                if (e.kinds == kDoorKinds) {
                    probe.x = static_cast<int16_t>(probe.x + probe.w / 2 - 2);  // signed halving toward 0
                    probe.w = 4;
                    if (c.doorCooldown >= 0) continue;
                }
                bool fire = false;
                if (e.dist == -1) {
                    fire = true;
                } else {
                    // Quirk kept: the probe grows to the left from x, to the right from the far edge
                    // (asymmetric probes, code_world.md §20 item 10).
                    if (c.dir < 0) probe.x = static_cast<int16_t>(probe.x - e.dist);
                    else probe.w = static_cast<int16_t>(probe.w + e.dist);
                    fire = (e.kinds & chimpProbeContacts(g, actor, probe)) != 0 && e.flag == 0;
                }
                if (fire && g.rand.random(1000) < static_cast<int16_t>(e.chance)) {  // 2a80:0212 R(1000)
                    next = static_cast<int16_t>(e.next);
                    break;
                }
            }
        }
    }
    if (next == kSeqTakeLeft || next == kSeqTakeRight) {
        roomGetItem(g, c.room, c.item);  // read, the result unused
        if (c.carried != 0) next = next == kSeqTakeLeft ? kSeqWalkLeft : kSeqWalkRight;  // one part only
    }
    if (next >= 0 && next < GgTables::kChimpSequences) {
        if (const auto& trans = g.tables.chimpTransitions[static_cast<size_t>(next)])
            for (const TransitionEntry& e : *trans)
                if ((input & ~e.ignore) == e.match || e.match == 0) {
                    next = e.next;
                    break;
                }
    } else if (next != -1) {
        log::debug("Chimp_Brain: chimp {} next {} lies past g_chimpTransitions", actor, next);
    }
    if (next == kSeqWalkLeft && c.blocked < 0) next = kSeqTurnToRight;
    if (next == kSeqWalkRight && c.blocked > 0) next = kSeqTurnToLeft;
    c.field94 = static_cast<uint32_t>(c.contacts & 0xFFFF) << 16;  // +0x96 = +0x48 (written only)
    return next;
}

// 2a80:032f Chimp_EnterRoom
void chimpEnterRoom(GgGame& g, int actor) {
    Actor& c = chimp(g, actor);
    const bool big = bigChimps(g);
    // Quirk kept (code_world.md §20 item 8): counted at every entry, also each time the player enters the
    // room (Actors_EnterRoom), so the chimp voice of the room music lingers.
    if (int16_t* n = roomChimpCount(g, c.room)) ++*n;
    const bool hall = roomIsHall(g, c.room);
    actorSelectScale(g, actor, hall);
    if (c.pendingSeq == -1) return;
    const int seq = c.pendingSeq;
    if (seq == kSeqOutOfDoor) c.doorCooldown = kDoorCooldown;
    c.pendingSeq = -1;
    c.countdown = 0;
    c.seq = seq;
    actorSetSequence(g, actor, seq);
    c.step = 0;
    if (seq == kSeqOutOfDoor || seq == kSeqInLeft || seq == kSeqInRight) {
        // At the twin of the door or passage used (the object +0x36 of the room left +0x3c).
        const RoomObject* used = roomGetObject(g, c.prevRoom, c.doorObj);
        const uint16_t link = used ? used->link : 0;
        if (const RoomObject* twin = roomFindLinkedDoor(g, c.room, link)) {
            c.anchorX = c.sprite.x = w16(twin->sprite.x + (hall ? 4 : 10));
            c.anchorY = c.sprite.y = w16(twin->sprite.y + (hall ? (big ? 5 : 6) : (big ? 14 : 18)));
        } else {
            log::debug("Chimp_EnterRoom: room {} has no door linked to {:#06x}", c.room, link);
        }
    }
}

// 2a80:0431 Chimp_SequenceEnd (op -11)
void chimpSequenceEnd(GgGame& g, int actor) {
    Actor& c = chimp(g, actor);
    const RoomObject* obj = roomGetObject(g, c.room, c.doorObj);
    int target = c.room, seq = -1;
    switch (obj ? obj->type : -1) {  // the table CS:0512 (chimps use holes, open doors, passages)
        case 6:  // a hole: +0x12 is the target room
            target = obj->link;
            c.step = 0;
            seq = c.dir >= 0 ? kSeqFromHoleRight : kSeqFromHoleLeft;
            break;
        case 9:
            target = obj->targetRoom();
            c.step = 0;
            seq = kSeqOutOfDoor;
            break;
        case 10:
        case 11:
            target = obj->targetRoom();
            c.step = 0;
            seq = obj->type == 10 ? kSeqInRight : kSeqInLeft;
            break;
        default:
            // Other objects leave the target and the sequence uninitialised in the original (no chimp
            // sequence ends there); the engine keeps the room and starts no entry sequence.
            log::warn("Chimp_SequenceEnd: chimp {} at object {} of room {}", actor, c.doorObj, c.room);
            break;
    }
    c.field94 = 0;
    c.exitSeq = seq;
    c.pendingSeq = seq;
    if (int16_t* n = roomChimpCount(g, c.room)) --*n;
    c.prevRoom = c.room;
    c.room = target;
}

// 2a80:051e Chimps_LoadSets: the hall and room sets of the level's chimps, each unless one is loaded
// (g_chimpSetHall DS:9f22, g_chimpSetRoom DS:9f1e); the chimps' tables name the sets loaded.
void chimpsLoadSets(GgGame& g) {
    ActorsObjects& o = g.actorsObjects();
    const ActorKind& kind = chimpKindForLevel(g.player.level);
    if (o.chimpSetHall == 0) {
        o.chimpSetHall = kind.hallSet;
        actorSet(g, o.chimpSetHall);
        o.chimpSeqHall = o.chimpSetHall;
    }
    if (o.chimpSetRoom == 0) {
        o.chimpSetRoom = kind.roomSet;
        actorSet(g, o.chimpSetRoom);
        o.chimpSeqRoom = o.chimpSetRoom;
    }
}

// 2a80:05e6 Chimps_FreeSets
void chimpsFreeSets(GgGame& g) {
    ActorsObjects& o = g.actorsObjects();
    if (o.chimpSetHall != 0) {
        o.sets.erase(o.chimpSetHall);
        o.chimpSetHall = 0;
    }
    if (o.chimpSetRoom != 0) {
        o.sets.erase(o.chimpSetRoom);
        o.chimpSetRoom = 0;
    }
}

// 2a80:0635 Chimps_SetupActors (every building set-up, new visit or resume): the shared part list, the
// sets, the fixed fields of slots 2..11. +0x2a (disabled) and the carried count +0x70 are not touched
// (Chimps_Spawn sets both on a new visit; a resume keeps the old count: code_world.md §20 bug 7).
void chimpsSetupActors(GgGame& g) {
    g.actors.chimpParts.clear();  // g_chimpParts DS:a03c = List_New()
    const bool big = bigChimps(g);
    chimpsLoadSets(g);
    for (int i = kFirstChimpActor; i < kActorCount; ++i) {
        Actor& c = chimp(g, i);
        c.id = i;
        c.countdown = 0;
        c.immune = 0;
        c.delay = 2;
        c.wallsOn = 1;
        c.contactsOn = 1;
        c.spriteBase = big ? kBigChimpKind.spriteBase : kChimpKind.spriteBase;
        c.exitSeq = -1;
        c.bundle = -1;
        c.pendingSeq = -1;
        c.transTable = 0;
        c.doorCooldown = -1;
    }
}

// 2a80:076b Chimps_Free (Building_Free): the sets and the shared part list (List_Free).
void chimpsFree(GgGame& g) {
    chimpsFreeSets(g);
    g.actors.chimpParts.clear();
}

// 2a80:0784 Chimps_Spawn (a new visit)
void chimpsSpawn(GgGame& g) {
    const int race = g.player.raceIndex;
    if (race < 0 || race >= static_cast<int>(g.tables.chimpListOffsets.size())) {
        log::warn("Chimps_Spawn: race index {} has no chimp list", race);
        return;
    }
    // The 286 path (PORTING_NOTES §5): the first room word of the race's list halved in place, in the
    // session's mutable copy of DS:1adc..1bad (1 -> 0: that chimp stays in the workshop).
    if (g.GgState::runtime.cpuType == 2) {
        uint16_t& first = chimpListFirstRoom(g, g.tables, race);
        first = static_cast<uint16_t>(static_cast<int16_t>(first) / 2);
    }
    const ChimpListView list = chimpList(g, g.tables, race);
    g.actors.roomChimpCount.fill(0);
    for (int i = kFirstChimpActor; i < kActorCount; ++i) {
        Actor& c = chimp(g, i);
        const int k = i - kFirstChimpActor;
        if (k >= static_cast<int16_t>(list.count) || static_cast<size_t>(k) >= list.rooms.size()) {
            c.disabled = 1;
            continue;
        }
        c.disabled = g.player.withChimps == 0 ? 1 : 0;  // Without Chimps: placed (same rand calls), disabled
        c.contacts = 0;
        c.step = 0;
        c.carried = 0;
        c.field94 = 0;
        c.toucher = -1;
        c.prevRoom = -1;
        const int room = list.rooms[static_cast<size_t>(k)];
        if (int16_t* n = roomChimpCount(g, room)) ++*n;
        c.room = room;
        const uint16_t flags = roomGetFlags(g, room);
        const bool hall = roomIsHall(g, room);
        const bool big = c.spriteBase == kBigChimpKind.spriteBase;
        if (hall) {
            c.floor = g.rand.random(6);  // 2a80:0875 R(6)
            c.anchorY = w16(c.floor * 26 + (big ? 14 : 15));
        } else {
            c.floor = g.rand.random(3);  // 2a80:08a8 R(3)
            c.anchorY = w16(c.floor * 52 + (big ? 24 : 28));
            if (flags & 2) c.floor += 3;  // a small room under hall floors 3..5
        }
        c.prevSeq = -1;
        c.seq = 0;
        if (g.rand.random(100) < 50) {  // 2a80:08ee R(100): the side it starts at
            c.anchorX = 10;
            c.field94 = 2;
        } else {
            c.anchorX = 300;
            c.field94 = 1;
        }
        actorSelectScale(g, i, hall);
    }
}

}  // namespace tmport::gg
