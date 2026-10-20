// The player and the projectile (player.h; ssg.exe 18f5, re/gg/docs/code_world.md §11, §12). Owner:
// actors agent.
//
// The original's callbacks read the player's record through its DGROUP addresses (DS:7a8a + offset) and
// g_playerRoom DS:7aa4 (= actor 0 +0x1a); here they are g.actors.actors[0] and g.playerRoom().
#include "gg/game/player.h"

#include "gg/data/dgroup_offsets.h"
#include "gg/game/actors.h"
#include "gg/game/game.h"
#include "gg/game/hud.h"
#include "gg/game/parts.h"
#include "gg/game/rooms.h"
#include "gg/game/world.h"
#include "gg/ui/hints.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kHintPartNotNeeded = 0x1044;
constexpr uint16_t kHintThirdPart = 0x1047;
constexpr uint16_t kHintPartNeeded = 0x104A;
constexpr uint16_t kHintAllPartsFound = 0x1050;
constexpr int kImmuneFrames = 0x96;      // 150 frames after a chimp hit (the blink)
constexpr int kPickUpEffect = 1;         // member of the warehouse's effect bank (0x1039)
constexpr int kPickUpPoints = 20;

// The entry sequences (code_world.md §11.3).
constexpr int kSeqStand = 0;
constexpr int kSeqThroughDoor = 33;      // out of a door
constexpr int kSeqFromHoleLeft = 40;     // arriving through a hole from above
constexpr int kSeqFromHoleRight = 41;
constexpr int kSeqFromPassageRight = 44; // in through the right passage
constexpr int kSeqFromPassageLeft = 45;
constexpr int kSeqFromTube = 53;         // out of the tube (hall set)

// The player's sequences a jump record can turn into a jump onto a pad: 12..15 (group 1) and 16, 17
// (group 2).
constexpr int kFirstLeftJump = 12, kLastLeftJump = 15, kFirstRightJump = 16, kLastRightJump = 17;

Actor& player(GgGame& g) { return g.actors.actors[kPlayerActor]; }
Actor& projectile(GgGame& g) { return g.actors.actors[kProjectileActor]; }
int w16(int v) { return static_cast<int16_t>(v); }

// The jump records of the displayed room (g_room) for a floor, without the small rooms' - 3 (kept: every
// shipped record is in a hall).
const std::vector<JumpRecord>* jumpRecords(GgGame& g, bool right, int floor) {
    Rooms& rooms = g.world.rooms;
    if (rooms.displayRoom < 0 || rooms.displayRoom >= rooms.count() || floor < 0 || floor >= 6) return nullptr;
    const Room& r = rooms.room(rooms.displayRoom);
    return right ? &r.jumpsRight[static_cast<size_t>(floor)] : &r.jumpsLeft[static_cast<size_t>(floor)];
}

}  // namespace

// 18f5:0002 Player_PickUpItem
void playerPickUpItem(GgGame& g, int actor) {
    Actor& p = player(g);
    const int node = roomGetItem(g, g.playerRoom(), p.item);
    if (node < 0) return;  // an empty list: the original reads the NULL node
    const ItemNode& item = g.world.rooms.pool().at(static_cast<size_t>(node));
    if (item.free) return;  // taken this frame already
    const int room = g.actors.actors.at(static_cast<size_t>(actor)).room;
    if (item.kind == ItemNode::kCrate) {
        projectilesAdd(g, 5);
        roomRemoveItem(g, room, node);
        --g.world.rooms.cratesLeft;
        return;
    }
    const int part = item.kind;
    std::vector<int16_t>& list = *actorPartList(g, kPlayerActor);
    if (partListCountType(g, list, partGetType(g, part)) == 2) {  // at most 2 of a type: the item stays
        hudFlashPart(g, part, true);
        hintQueue(g, kHintThirdPart, 0);
        return;
    }
    g.sound.playEffect(g.GgState::sound.effectBank + kPickUpEffect, 0, 4);
    const bool needed = partIsNeeded(g, part);
    const int second = actorAddPart(g, kPlayerActor, part);
    hudAddPart(g, part);
    hintQueue(g, needed ? kHintPartNeeded : kHintPartNotNeeded, 0);
    roomRemoveItem(g, room, node);
    g.world.hud.dirty |= 1;
    LiveState& live = g.player.live;
    // g_partsNeeded counts needed types: the first part of a type counts, a second does not.
    if (second == 0 && needed) --live.partsNeeded;
    if (live.partsNeeded == 0) hintQueue(g, kHintAllPartsFound, 0);
    // Points unless the part is flagged 0x4000 (the trash can and the workshop set it; a part a chimp
    // stole and dropped scores again).
    if (!(partRecord(g, part).flags & PartRecord::kNoPoints)) scoreAdd(g, kPickUpPoints);
}

// 18f5:0180 Player_HitByChimp
void playerHitByChimp(GgGame& g, int actor) {
    Actor& p = g.actors.actors.at(static_cast<size_t>(actor));
    const int chimp = p.toucher;
    hudEscHook(g);
    player(g).immune = kImmuneFrames;
    if (p.carried == 0) return;
    const int k = g.rand.random(p.carried);  // 18f5:01ac R(+0x70)
    // A start past the list walks its tail (quirk kept by PartList_FindHeld: code_world.md §20 bug 7).
    const int part = partListFindHeld(g, g.actors.playerParts, 0, k);
    if (part < 0) return;
    const int type = partGetType(g, part);
    std::vector<int16_t>& chimps = g.actors.chimpParts;
    if (partListCountType(g, chimps, type) == 2) {
        // The chimps never hold three of a type: one of theirs goes to a random room (rand).
        const int q = partListFindType(g, chimps, type);
        actorRemovePart(g, chimp, q, false);
        itemPlaceInRandomRoom(g, q);
    }
    const bool needed = partIsNeeded(g, part);
    const int anchor = hudStripAnchorFor(g, part);
    const int left = actorRemovePart(g, kPlayerActor, part, true);
    actorAddPart(g, chimp, part);
    hudRefreshStrip(g, anchor);
    if (left == 0 && needed) ++g.player.live.partsNeeded;
}

// 18f5:029b Player_Brain
int playerBrain(GgGame& g, int) {
    Actor& p = player(g);
    // The held keys (Input_GetHeldKeys: the low word, with the idle bit) and the contacts (high word).
    uint32_t mask = g.input().getHeldKeys(static_cast<uint16_t>(p.contacts));
    if (p.immune != 0) {
        --p.immune;
        mask &= 0xCFFFFFFFu;  // the chimps' touches (0x1000, 0x2000) are ignored while immune
    }
    if (p.seq < 0 || static_cast<size_t>(p.seq) >= g.tables.playerTransitions.size() ||
        !g.tables.playerHasTransitions[static_cast<size_t>(p.seq)])
        return -1;
    // The first entry of g_playerTransitions DS:127e[seq] that matches (match 0 always does).
    for (const TransitionEntry& e : g.tables.playerTransitions[static_cast<size_t>(p.seq)]) {
        if ((mask & ~e.ignore) != e.match && e.match != 0) continue;
        int next = e.next;
        const bool left = next >= kFirstLeftJump && next <= kLastLeftJump;
        const bool right = next >= kFirstRightJump && next <= kLastRightJump;
        if (left || right) {
            // A jump spot under the player turns the jump into the jump onto its pad.
            if (const auto* records = jumpRecords(g, right, p.floor))
                for (const JumpRecord& r : *records) {
                    if (r.trigger != next || !(p.sprite.x < r.x + r.w) || !(p.sprite.x + p.sprite.w > r.x)) continue;
                    next = r.replacement;
                    p.pad = r.pad;
                    break;
                }
        }
        return next;
    }
    return -1;
}

// 18f5:0401 Projectile_Brain
int projectileBrain(GgGame&, int) { return -1; }

// 18f5:0409 Player_EnterRoom
void playerEnterRoom(GgGame& g, int) {
    Actor& p = player(g);
    // The link of the object used in the room left (+0x3c, +0x36); only the door and passage entries
    // read it (the tube's exit has no object: the original reads through NULL then).
    const RoomObject* used = roomGetObject(g, p.prevRoom, p.doorObj);
    const uint16_t link = used ? used->link : 0;
    const bool hall = roomIsHall(g, g.playerRoom());
    actorSelectScale(g, kPlayerActor, hall);
    p.countdown = 0;
    if (p.pendingSeq == -1) return;
    const int seq = p.pendingSeq;
    p.pendingSeq = -1;
    p.seq = seq;
    actorSetSequence(g, kPlayerActor, seq);  // so the previous sequence +0x1e is the entry sequence too
    p.step = 0;
    if (seq == kSeqThroughDoor || seq == kSeqFromPassageRight || seq == kSeqFromPassageLeft) {
        if (const RoomObject* door = roomFindLinkedDoor(g, g.playerRoom(), link)) {
            p.anchorX = p.sprite.x = w16(door->sprite.x + (hall ? 3 : 10));
            p.anchorY = p.sprite.y = w16(door->sprite.y + (hall ? 5 : 14));
        } else {
            log::debug("Player_EnterRoom: room {} has no door linked to {:#06x}", g.playerRoom(), link);
        }
    }
    if (seq == kSeqFromTube) p.floor = 5;
}

// 18f5:04cf Player_SequenceEnd
void playerSequenceEnd(GgGame& g, int) {
    Actor& p = player(g);
    const int room = g.playerRoom();
    const RoomObject* obj = roomGetObject(g, room, p.doorObj);
    int target = room, seq = -1;
    if (!obj) {  // no object: the workshop's exit to the warehouse (2dee:0e4a); g_puzzleDoorHit stays
        target = g.player.live.tubeRoom;
        seq = kSeqFromTube;
    } else {
        g.world.puzzleDoorHit = 0;
        switch (obj->type) {  // the table CS:0597
            case 6:  // a hole: +0x12 is the target room
                target = obj->link;
                seq = p.dir > 0 ? kSeqFromHoleRight : kSeqFromHoleLeft;
                break;
            case 7:  // a locked door: a puzzle, then out of the same door (g_displayRoom)
                g.player.live.hintCancel1048 = 1;
                seq = kSeqThroughDoor;
                target = g.world.rooms.displayRoom;
                g.world.puzzleDoorHit = 1;
                break;
            case 8:  // the tube: to the workshop
                g.player.live.tubeRoom = static_cast<int16_t>(room);
                target = 0;
                seq = kSeqStand;
                break;
            case 9:  // an open door
                g.player.live.hintCancel1048 = 1;
                target = obj->targetRoom();
                seq = kSeqThroughDoor;
                break;
            case 10:  // the left passage: in at the right of the next room
            case 11:
                target = obj->targetRoom();
                seq = obj->type == 10 ? kSeqFromPassageLeft : kSeqFromPassageRight;
                break;
            default:
                // The original goes on with uninitialised registers (the caller's step argument and the
                // record's address); no shipped sequence ends at another object. The engine stays in the
                // room with no entry sequence.
                log::warn("Player_SequenceEnd: object {} of room {} has type {}", p.doorObj, room, obj->type);
                break;
        }
    }
    p.exitSeq = seq;
    p.prevRoom = room;
    p.room = target;
    p.pendingSeq = seq;
}

// 18f5:05a3 Actors_LoadPlayerSets: each set read unless g_playerSets DS:0a9e holds it.
void actorsLoadPlayerSets(GgGame& g) {
    ActorsObjects& o = g.actorsObjects();
    constexpr uint16_t kIds[4] = {kPlayerKind.hallSet, kPlayerKind.roomSet, kProjectileKind.hallSet,
                                  kProjectileKind.roomSet};
    for (size_t k = 0; k < o.playerSetsLoaded.size(); ++k) {
        if (o.playerSetsLoaded[k]) continue;
        actorSet(g, kIds[k]);  // Actors_LoadSet (a copy read before is the same bytes)
        o.playerSetsLoaded[k] = true;
    }
}

// 18f5:06b7 Actors_FreePlayerSets
void actorsFreePlayerSets(GgGame& g) {
    ActorsObjects& o = g.actorsObjects();
    constexpr uint16_t kIds[4] = {kPlayerKind.hallSet, kPlayerKind.roomSet, kProjectileKind.hallSet,
                                  kProjectileKind.roomSet};
    for (size_t k = 0; k < o.playerSetsLoaded.size(); ++k) {
        if (!o.playerSetsLoaded[k]) continue;
        o.sets.erase(kIds[k]);
        o.playerSetsLoaded[k] = false;
    }
}

// 18f5:06fb Projectile_EnterRoom
void projectileEnterRoom(GgGame& g, int) {
    Actor& b = projectile(g);
    actorSelectScale(g, kProjectileActor, roomIsHall(g, b.room));
    b.countdown = 0;
}

// 18f5:0721 Actors_Init (Game_Init): the sets, then the player's and the projectile's fixed fields (the
// callbacks, tables and slots follow from the slots in the engine).
void actorsInit(GgGame& g) {
    actorsLoadPlayerSets(g);
    Actor& p = player(g);
    p.id = kPlayerActor;
    p.disabled = 0;
    p.countdown = 0;
    p.room = 0;
    p.delay = 2;
    p.wallsOn = 1;
    p.contactsOn = 1;
    g.actors.playerParts.clear();  // +0x72 = List_New()
    p.exitSeq = -1;
    p.bundle = -1;
    p.pendingSeq = 0;
    p.blocked = 0;
    p.transTable = dg::kPlayerTransitions;
    p.spriteBase = kPlayerKind.spriteBase;
    actorSelectScale(g, kPlayerActor, false);
    Actor& b = projectile(g);
    b.disabled = 1;
    b.id = kProjectileActor;
    b.countdown = 0;
    b.delay = 2;
    b.blocked = 0;
    b.bundle = -1;
    b.spriteBase = kProjectileKind.spriteBase;
    b.exitSeq = -1;
    b.pendingSeq = -1;
    b.wallsOn = 1;
    b.contactsOn = 1;
    b.transTable = dg::kProjectileTransitions;
}

// 18f5:0895 Projectile_Launch
void projectileLaunch(GgGame& g, int seq, int dx) {
    LiveState& live = g.player.live;
    if (live.projectiles == 0) return;
    const Actor& p = player(g);
    Actor& b = projectile(g);
    b.disabled = 0;
    b.anchorX = b.sprite.x = w16(p.anchorX + dx);
    b.anchorY = p.anchorY;
    b.floor = p.floor;
    b.exitSeq = -1;
    b.pendingSeq = -1;
    b.room = g.playerRoom();
    b.step = 0;
    b.countdown = 0;
    b.contacts = 0;
    b.blocked = 0;
    b.prevSeq = -1;
    b.unused34 = -1;
    b.contactsOn = 1;
    actorSelectScale(g, kProjectileActor, roomIsHall(g, b.room));
    actorSetSequence(g, kProjectileActor, seq);  // +0x1e becomes the sequence before after all
    --live.projectiles;
    g.world.hud.dirty |= 8;
    // The last banana with no crate left in the rooms: g_cratesTotal crates are dealt anew (rand).
    Rooms& rooms = g.world.rooms;
    if (live.projectiles == 0 && rooms.cratesLeft == 0) {
        for (int k = 0; k < rooms.cratesTotal; ++k) itemPlaceInRandomRoom(g, ItemNode::kCrate);
        rooms.cratesLeft = rooms.cratesTotal;
    }
}

}  // namespace tmport::gg
