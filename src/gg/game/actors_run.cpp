// The actor system on the game (actors.h, "wave 3: the run"; re/gg/docs/code_world.md §9). Owner: actors
// agent.
//
// GgState::actors holds the 12 records (the DGROUP image's bytes) plus the two choices the original
// keeps in far pointers (Actor::hallSet for +0x52, Actor::frame for the picture +0x0a); the sets are
// ActorsObjects', the bundles the picture cache's. The callbacks of a record follow from its slot
// (actorBrain .. actorOnEnter below).
#include "gg/game/actors.h"

#include <algorithm>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/game/chimps.h"
#include "gg/game/collision.h"
#include "gg/game/game.h"
#include "gg/game/parts.h"
#include "gg/game/player.h"
#include "gg/game/rooms.h"
#include "gg/save/gg_state_file.h"
#include "gg/save/players_dat.h"
#include "gg/ui/hints.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// Commands Actor_RunStep runs without reaching a frame before the engine gives up: the original loops
// for ever on a sequence of commands only (none in the shipped sets).
constexpr int kMaxCommandsPerStep = 1000;
constexpr uint16_t kHintNoBananas = 0x1051;
constexpr uint16_t kHintTubeWithChimps = 0x104B;
constexpr uint16_t kHintTubeWithoutChimps = 0x1055;

Actor& actorAt(GgGame& g, int i) { return g.actors.actors.at(static_cast<size_t>(i)); }
// The original's 16-bit stores.
int w16(int v) { return static_cast<int16_t>(v); }

// The table +0x52 (the hall set's sequences or the room set's) and the W/H tables of a scale (+0x7a /
// +0x82 for the halls, +0x76 / +0x7e for the small rooms).
const ActorSet& sequenceSet(GgGame& g, int i) {
    const ActorKind k = actorKind(g, i);
    return actorSet(g, actorAt(g, i).hallSet ? k.hallSet : k.roomSet);
}
const ActorSet& sizeSet(GgGame& g, int i, bool hall) {
    const ActorKind k = actorKind(g, i);
    return actorSet(g, hall ? k.hallSet : k.roomSet);
}
int tableAt(const std::vector<int16_t>& t, int frame) {
    // W[op] / H[op]: past the table the original reads on (never in the shipped sets).
    return frame >= 0 && static_cast<size_t>(frame) < t.size() ? t[static_cast<size_t>(frame)] : 0;
}

// The frame step of Actor_RunStep (1834:05ae..06cc): flags, size, the move by a with the wall sweep,
// the screen clamp, the picture; the step index advances.
void runFrameStep(GgGame& g, int i, const ActorSet& sizes, const AnimStep& s) {
    Actor& a = actorAt(g, i);
    a.sprite.flags = s.c;
    const int w = tableAt(sizes.widths, s.op);
    const int h = tableAt(sizes.heights, s.op);
    a.sprite.w = w;
    a.sprite.h = h;
    const int oldX = a.anchorX;
    a.anchorX = w16(a.anchorX + s.a);
    a.sprite.x = a.anchorX;
    a.sprite.y = w16(a.anchorY + s.b);
    if (a.wallsOn != 0) {
        // The sweep: moving right (or standing) the old place widened by the move; moving left only
        // the path, 1 - a wide (quirk kept: the asymmetric sweep, code_world.md §20 item 10). Its
        // flags word is an uninitialised local in the original (Actor_CollideWalls does not read it).
        HitRecord sweep;
        if (a.anchorX >= oldX) {
            sweep.x = static_cast<int16_t>(oldX);
            sweep.w = static_cast<int16_t>(w + (a.anchorX - oldX));
        } else {
            sweep.x = static_cast<int16_t>(a.anchorX);
            sweep.w = static_cast<int16_t>(1 - s.a);
        }
        sweep.y = static_cast<int16_t>(a.sprite.y);
        sweep.h = static_cast<int16_t>(h);
        const int r = actorCollideWalls(g, i, sweep, -1);
        if (r == 0) {
            a.blocked = 0;
        } else if (r == 2) {
            if (a.dir < 2) {  // a wall at the left: stand right of it
                a.anchorX = a.sprite.x = w16(a.wallX + 1);
                a.blocked = -1;
            }
        } else if (a.dir > -2) {  // a wall at the right: stand left of it
            a.anchorX = a.sprite.x = w16(a.wallX - a.sprite.w + 1);
            a.blocked = 1;
        }
    }
    if (a.anchorX < 1) a.anchorX = a.sprite.x = 1;
    if (a.sprite.x + a.sprite.w > 0x13F) a.anchorX = a.sprite.x = w16(0x13F - a.sprite.w);
    a.frame = s.op;
    a.sprite.picture = actorPicture(g, a);
    ++a.step;
}

// The physical address a far pointer stored at `linear` points to (0 for NULL).
uint32_t farAt(const GgStateFile& s, uint32_t linear) {
    const auto b = s.bytesAt(linear, 4);
    if (b.size() < 4) return 0;
    const uint32_t off = b[0] | (b[1] << 8), seg = b[2] | (b[3] << 8);
    return seg * 16 + off;
}

// A carried-part list of a state (List_New header {far head, far tail}; the nodes are part records,
// own index +0x8c, next at +0): the part indexes in list order.
std::vector<int16_t> partListOfState(const GgStateFile& s, uint32_t header) {
    std::vector<int16_t> out;
    if (header == 0) return out;
    uint32_t node = farAt(s, header);
    for (int guard = 0; node != 0 && guard < 64; ++guard) {
        const auto rec = s.bytesAt(node, PartRecord::kSize);
        if (rec.size() < PartRecord::kSize) break;
        out.push_back(static_cast<int16_t>(rec[0x8C] | (rec[0x8D] << 8)));
        node = farAt(s, node);
    }
    return out;
}

}  // namespace

const ActorSet& actorSet(GgGame& g, uint16_t id) {
    auto& sets = g.actorsObjects().sets;
    auto it = sets.find(id);
    if (it == sets.end()) it = sets.emplace(id, ActorSet::parse(g.archive().read(id))).first;
    return it->second;
}

ActorKind actorKind(GgGame& g, int actor) {
    if (actor == kPlayerActor) return kPlayerKind;
    if (actor == kProjectileActor) return kProjectileKind;
    const ActorsObjects& o = g.actorsObjects();
    return ActorKind{o.chimpSeqHall, o.chimpSeqRoom, actorAt(g, actor).spriteBase};
}

const AnimSequence* actorSequence(GgGame& g, int actor) { return sequenceSet(g, actor).sequence(actorAt(g, actor).seq); }

std::vector<int16_t>* actorPartList(GgGame& g, int actor) {
    if (actor == kPlayerActor) return &g.actors.playerParts;
    if (actor >= kFirstChimpActor && actor < kActorCount) return &g.actors.chimpParts;
    return nullptr;
}

std::span<const uint8_t> actorPicture(GgGame& g, const Actor& a) {
    if (a.bundle < 0 || a.frame < 0) return {};
    const auto* pics = g.cache.pictures(static_cast<uint16_t>(a.bundle));
    if (!pics || static_cast<size_t>(a.frame) >= pics->size()) return {};
    return (*pics)[static_cast<size_t>(a.frame)];
}

// 1834:003b Actor_SetSequence
void actorSetSequence(GgGame& g, int actor, int seq) {
    Actor& a = actorAt(g, actor);
    a.prevSeq = a.seq;
    a.seq = seq;
    a.step = 0;
    if (const AnimSequence* s = sequenceSet(g, actor).sequence(seq)) {
        a.delay = s->delay;
        a.dir = s->dir;
    } else {
        log::debug("Actor_SetSequence: actor {} has no sequence {}", actor, seq);
    }
}

// 1834:015b Actor_SelectScale. AnimSeq_BindPictures (2bd4:01c1) gives every frame step the cache slot of
// its picture; the engine takes the picture from the bundle when it shows the frame (actorPicture).
void actorSelectScale(GgGame& g, int actor, bool hall) { actorAt(g, actor).hallSet = hall; }

// 1834:01b6 Actor_RunStep
int actorRunStep(GgGame& g, int actor) {
    Actor& a = actorAt(g, actor);
    // The W/H tables of the actor's room's scale, read once: a chimp that changes rooms at op -11 shows
    // its next frame with the old room's sizes (kept).
    const bool hall = roomIsHall(g, a.room);
    const ActorSet& sizes = sizeSet(g, actor, hall);
    const AnimSequence* seq = sequenceSet(g, actor).sequence(a.seq);
    for (int n = 0; n < kMaxCommandsPerStep; ++n) {
        if (!seq || a.step < 0 || static_cast<size_t>(a.step) >= seq->steps.size()) {
            log::debug("Actor_RunStep: actor {} step {} lies outside sequence {}", actor, a.step, a.seq);
            return 0;
        }
        const AnimStep s = seq->steps[static_cast<size_t>(a.step)];
        if (s.op >= 0) {
            runFrameStep(g, actor, sizes, s);
            return 0;
        }
        switch (s.op) {  // the jump table CS:06d7
            case -46:  // the right edge
                a.anchorX = w16((hall ? 0x13E : 0x13C) - tableAt(sizes.widths, s.a));
                break;
            case -43:  // a hint: -1 the first tube exit's
                if (s.a == -1) hintQueue(g, g.player.withChimps != 0 ? kHintTubeWithChimps : kHintTubeWithoutChimps, 5);
                else hintQueue(g, static_cast<uint16_t>(s.a), 0);
                break;
            case -42:  // on a floor edge go to step a
                if (a.contacts & 1) {
                    a.step = s.a;
                    continue;
                }
                break;
            case -41:  // at the edge object
                if (const RoomObject* o = roomGetObject(g, a.room, a.edgeObj)) a.anchorX = w16(o->sprite.x + 4);
                break;
            case -40:
                if (const RoomObject* o = roomGetObject(g, a.room, a.edgeObj))
                    a.anchorX = w16(o->sprite.x + tableAt(sizes.widths, s.a) - 7);
                break;
            case -37:  // stop effect a (the displayed room only)
                if (a.room == g.world.rooms.displayRoom) g.sound.stopEffect(g.GgState::sound.effectBank + s.a);
                break;
            case -36:  // play effect a unless it plays (the displayed room only)
                if (a.room == g.world.rooms.displayRoom) {
                    const int h = g.GgState::sound.effectBank + s.a;
                    if (g.sound.isPlaying(h) == 0) g.sound.playEffect(h, s.b >> 8, s.b & 0xFF);
                }
                break;
            case -33:
                chimpDropPart(g, actor);
                break;
            case -32:  // the loop's end: back to its start while the counter runs (16-bit, unsigned)
                a.loopCount = w16(a.loopCount - 1);
                if (a.loopCount != 0) {
                    a.step = a.loopStart;
                    continue;
                }
                break;
            case -31: {  // the loop's start: g_chimpStunLoops[g_level]
                const int level = g.player.level;
                a.loopCount = level >= 0 && level < static_cast<int>(g.tables.chimpStunLoops.size())
                                  ? w16(g.tables.chimpStunLoops[static_cast<size_t>(level)])
                                  : 0;
                ++a.step;
                a.loopStart = a.step;
                continue;
            }
            case -30:  // the banana is gone (DS:7b54 = the projectile's +0x2a)
                actorAt(g, kProjectileActor).disabled = 1;
                ++a.step;
                if (g.player.live.projectiles == 0) hintQueue(g, kHintNoBananas, 0);
                continue;
            case -29:
                actorOnHit(g, actor);
                break;
            case -28:
                a.contactsOn = s.a;
                break;
            case -27:
                projectileLaunch(g, s.a, s.b);
                break;
            case -26:  // at the door object
                if (const RoomObject* o = roomGetObject(g, a.room, a.doorObj)) a.anchorX = o->sprite.x;
                break;
            case -25:  // disable itself; the step stays
                a.disabled = 1;
                if (g.player.live.projectiles == 0) hintQueue(g, kHintNoBananas, 0);
                return 0;
            case -24:
                a.delay = s.a;
                break;
            case -23:  // show: the pad's object (-1) or object b of layer a of the displayed room
                if (s.b == -1) {
                    if (RoomObject* o = roomGetObject(g, a.room, a.pad)) objShow(*o);
                } else {
                    objShowAt(g, s.a, s.b);
                }
                break;
            case -22:  // hide (quirk: the player's hall sequences 52 / 53 name layer 1, NULL in every room)
                if (s.b == -1) {
                    if (RoomObject* o = roomGetObject(g, a.room, a.pad)) objHide(*o);
                } else {
                    objHideAt(g, s.a, s.b);
                }
                break;
            case -21:  // at the pad (0x8000: keep)
                if (const RoomAnim* p = roomGetAnim(g, a.room, a.pad)) {
                    if (s.a != static_cast<int16_t>(0x8000)) a.anchorX = w16(p->sprite.x + s.a);
                    if (s.b != static_cast<int16_t>(0x8000)) a.anchorY = w16(p->sprite.y + s.b);
                }
                break;
            case -17:
                a.wallsOn = 0;
                break;
            case -16:
                a.wallsOn = 1;
                a.blocked = 0;
                break;
            case -15:
                a.anchorY = s.a;
                break;
            case -14:
                a.anchorX = s.a;
                break;
            case -13:
                a.anchorY = w16(a.anchorY + s.a);
                a.floor = w16(a.floor + s.b);
                break;
            case -12:
                a.anchorX = w16(a.anchorX + s.a);
                break;
            case -11:  // the end: the player leaves the room (the step stays); others go on in their new room
                actorOnEnd(g, actor);
                if (actor == kPlayerActor) return 1;
                actorOnEnter(g, actor);
                a.step = 0;
                seq = sequenceSet(g, actor).sequence(a.seq);
                continue;
            case -10:
                actorOnTake(g, actor);
                break;
            case -9:  // restart the pad's script
                if (RoomAnim* p = roomGetAnim(g, a.room, a.pad)) {
                    p->countdown = 0;
                    p->sprite.flags &= static_cast<uint16_t>(~0x14u);
                    p->pos = 1;
                }
                break;
            case -8:  // hidden until the next frame step
                ++a.step;
                a.sprite.flags |= Actor::kHidden;
                return 0;
            case -7:  // back to the sequence before
                actorSetSequence(g, actor, a.prevSeq);
                seq = sequenceSet(g, actor).sequence(a.seq);
                a.step = 0;
                continue;
            case -6:
                actorSetSequence(g, actor, s.a);
                seq = sequenceSet(g, actor).sequence(a.seq);
                a.step = 0;
                continue;
            case -4:
                a.step = s.a;
                continue;
            case -1:
                a.step = 0;
                continue;
            default:
                // -45, -44, -39, -38, -35, -34, -20..-18, -5, -3, -2 and anything below -46 (the end marker
                // -1000): the original re-reads the same step for ever; no shipped sequence reaches one.
                log::debug("Actor_RunStep: actor {} sequence {} step {}: op {} hangs the original", actor, a.seq,
                           a.step, s.op);
                return 0;
        }
        ++a.step;
    }
    log::debug("Actor_RunStep: actor {} sequence {}: no frame after {} commands", actor, a.seq, kMaxCommandsPerStep);
    return 0;
}

// 1834:0733 Actors_Draw
void actorsDraw(GgGame& g) {
    for (Actor& a : g.actors.actors) a.sprite.picture = actorPicture(g, a);
    actorsDraw(g.actors.actors, g.world.rooms.displayRoom, g.actors.playerBlink, g.video.sprites());
}

// 1834:07ad Actors_EnterRoom
void actorsEnterRoom(GgGame& g) {
    const int room = g.world.rooms.displayRoom;
    const int scale = roomIsHall(g, room) ? 1 : 0;
    if (room == 0) return;
    actorsLoadPlayerSets(g);
    chimpsLoadSets(g);
    g.actors.actorSpritesLoaded = 1;
    const auto loadBundle = [&g, scale](Actor& a) {
        if (a.spriteBase == 0) {  // a record no set-up has filled (the original would load bundle 0)
            log::debug("Actors_EnterRoom: actor {} has no sprite base", a.id);
            return;
        }
        a.bundle = a.spriteBase + scale;
        g.cache.loadBundle(static_cast<uint16_t>(a.bundle));
    };
    loadBundle(actorAt(g, kPlayerActor));
    actorOnEnter(g, kPlayerActor);
    loadBundle(actorAt(g, kProjectileActor));
    actorOnEnter(g, kProjectileActor);
    if (g.player.withChimps != 1) return;
    for (int i = kFirstChimpActor; i < kActorCount; ++i) {
        Actor& a = actorAt(g, i);
        loadBundle(a);
        if (a.disabled == 0 && a.room == room) actorOnEnter(g, i);
    }
}

// 1834:0870 Actors_LeaveRoom
void actorsLeaveRoom(GgGame& g) {
    if (g.world.rooms.displayRoom == 0 || g.actors.actorSpritesLoaded == 0) return;
    g.actors.actorSpritesLoaded = 0;
    const auto release = [&g](Actor& a) {
        if (a.bundle >= 0) g.cache.releaseBundle(static_cast<uint16_t>(a.bundle));
        a.bundle = -1;
        a.sprite.picture = {};  // engine: the released pictures are gone (the original keeps a dangling pointer)
    };
    release(actorAt(g, kPlayerActor));
    release(actorAt(g, kProjectileActor));
    if (g.player.withChimps != 1) return;
    for (int i = kFirstChimpActor; i < kActorCount; ++i) release(actorAt(g, i));
}

// 1834:08d7 Actor_AddPart
int actorAddPart(GgGame& g, int actor, int part) {
    std::vector<int16_t>* list = actorPartList(g, actor);
    if (!list) {
        log::debug("Actor_AddPart: actor {} has no part list", actor);
        return 0;
    }
    const int type = partGetType(g, part);
    int last = -1;
    if (partListCountType(g, *list, type, &last) >= 2) return 0;
    PartRecord& r = partRecord(g, part);
    // +0x8a = 0 and +0x1a = 0x2238 are written too; nothing reads them, the engine's record has no field.
    r.flags &= PartRecord::kNoPoints;
    r.pair = static_cast<int16_t>(last);
    r.holder = static_cast<int16_t>(actor);
    int second = 0;
    if (last != -1) {
        partRecord(g, last).pair = static_cast<int16_t>(r.index | 0x8000);
        second = 1;
    }
    Actor& a = actorAt(g, actor);
    a.carried = w16(a.carried + 1);
    list->push_back(static_cast<int16_t>(part));
    return second;
}

// 1834:0990 Actor_RemovePart
int actorRemovePart(GgGame& g, int actor, int part, bool checkHolder) {
    std::vector<int16_t>* list = actorPartList(g, actor);
    if (!list) {
        log::debug("Actor_RemovePart: actor {} has no part list", actor);
        return -1;
    }
    const int type = partGetType(g, part);
    int last = -1;
    const int n = partListCountType(g, *list, type, &last);
    for (size_t pos = 0; pos < list->size(); ++pos) {
        PartRecord& r = partRecord(g, (*list)[pos]);
        if (r.index != part) continue;
        const int holder = r.holder;
        if (holder != actor && checkHolder) continue;
        if (holder >= 0 && holder < kActorCount) {
            Actor& h = actorAt(g, holder);
            h.carried = w16(h.carried - 1);
        } else {
            log::debug("Actor_RemovePart: part {} has holder {}", part, holder);  // the original writes past g_actors
        }
        r.holder = -1;
        list->erase(list->begin() + static_cast<std::ptrdiff_t>(pos));
        if (r.pair != -1) {
            int partner = r.pair;
            if (r.pair & 0x8000) {
                // It was the first of its pair: the second takes its place (List_InsertAt at its index).
                partner = r.pair & 0x7FFF;
                const auto it = std::find(list->begin(), list->end(), static_cast<int16_t>(partner));
                if (it != list->end()) list->erase(it);
                list->insert(list->begin() + static_cast<std::ptrdiff_t>(std::min(pos, list->size())),
                             static_cast<int16_t>(partner));
            }
            partRecord(g, partner).pair = -1;
        }
        break;
    }
    return n - 1;
}

// 1834:0ace Actors_ResetCountdowns
void actorsResetCountdowns(GgGame& g) {
    for (Actor& a : g.actors.actors) a.countdown = 0;
}

// 1834:0ae9 Actors_Tick
int actorsTick(GgGame& g) {
    int result = 0;
    for (int i = 0; i < kActorCount; ++i) {
        Actor& a = actorAt(g, i);
        if (a.disabled != 0) continue;
        actorUpdateContacts(g, i);
        const auto next = static_cast<uint16_t>(actorBrain(g, i));
        if (next != 0xFFFF && (next & 0x7FFF) != a.seq) {
            actorSetSequence(g, i, next & 0x7FFF);
            if (!(next & 0x8000)) {  // start it now; bit 15 keeps the countdown and the step 0 of SetSequence
                a.step = 0;
                a.countdown = 0;
            }
        }
        a.countdown = w16(a.countdown - 1);
        if (a.countdown < 0) {
            a.countdown = a.delay;
            result |= actorRunStep(g, i);
        }
    }
    return result;
}

// 1834:0b5e Actors_SaveState
void actorsSaveState(GgGame& g, BuildingBlock& block) {
    for (size_t i = 0; i < block.actors.size(); ++i) {
        const Actor& a = g.actors.actors[i];
        ActorState& s = block.actors[i];
        s = ActorState{static_cast<int16_t>(a.anchorX),   static_cast<int16_t>(a.anchorY),
                       static_cast<int16_t>(a.seq),       static_cast<int16_t>(a.room),
                       static_cast<int16_t>(a.floor),     static_cast<int16_t>(a.prevSeq),
                       static_cast<int16_t>(a.exitSeq),   static_cast<int16_t>(a.step),
                       static_cast<int16_t>(a.doorCooldown), static_cast<int16_t>(a.loopCount),
                       static_cast<int16_t>(a.loopStart), static_cast<int16_t>(a.disabled),
                       static_cast<int16_t>(a.pendingSeq), static_cast<int16_t>(a.wallsOn),
                       static_cast<int16_t>(a.contactsOn), static_cast<int16_t>(a.blocked),
                       static_cast<int16_t>(a.unused34),  static_cast<int16_t>(a.doorObj),
                       static_cast<int16_t>(a.pad),       static_cast<int16_t>(a.edgeObj),
                       static_cast<int16_t>(a.prevRoom),  static_cast<int16_t>(a.immune),
                       static_cast<int16_t>(a.item)};
    }
}

// 1834:0b95 Actors_RestoreState
void actorsRestoreState(GgGame& g, const BuildingBlock& block) {
    for (int i = 0; i < kActorCount; ++i) {
        Actor& a = actorAt(g, i);
        const ActorState& s = block.actors[static_cast<size_t>(i)];
        a.anchorX = s.anchorX;
        a.anchorY = s.anchorY;
        a.seq = s.seq;
        a.room = s.room;
        a.floor = s.floor;
        a.prevSeq = s.prevSeq;
        a.exitSeq = s.exitSeq;
        a.step = s.step;
        a.doorCooldown = s.doorCooldown;
        a.loopCount = s.loopCounter;
        a.loopStart = s.loopStart;
        a.disabled = s.disabled;
        a.pendingSeq = s.pendingSeq;
        a.wallsOn = s.wallsOn;
        a.contactsOn = s.contactsOn;
        a.blocked = s.blocked;
        a.unused34 = s.unused34;
        a.doorObj = s.doorObject;
        a.pad = s.pad;
        a.edgeObj = s.edgeObject;
        a.prevRoom = s.prevRoom;
        a.immune = s.immune;
        a.item = s.item;
        actorSelectScale(g, i, roomIsHall(g, a.room));
        if (const AnimSequence* q = sequenceSet(g, i).sequence(a.seq)) {
            a.delay = q->delay;
            a.dir = q->dir;
        }
    }
}

// The callbacks by slot: the player's and the projectile's (18f5), the chimps' (2a80 through the stub
// segment 1e0d). The NULL callbacks of the original (the projectile's +0x66, +0x6a and +0x88, the chimps'
// +0x6a) are never reached by the shipped sequences; the engine skips them.
int actorBrain(GgGame& g, int actor) {
    if (actor == kPlayerActor) return playerBrain(g, actor);
    if (actor == kProjectileActor) return projectileBrain(g, actor);
    return chimpBrain(g, actor);
}

void actorOnTake(GgGame& g, int actor) {
    if (actor == kPlayerActor) playerPickUpItem(g, actor);
    else if (actor >= kFirstChimpActor) chimpTakePart(g, actor);
}

void actorOnHit(GgGame& g, int actor) {
    if (actor == kPlayerActor) playerHitByChimp(g, actor);
}

void actorOnEnd(GgGame& g, int actor) {
    if (actor == kPlayerActor) playerSequenceEnd(g, actor);
    else if (actor >= kFirstChimpActor) chimpSequenceEnd(g, actor);
}

void actorOnEnter(GgGame& g, int actor) {
    if (actor == kPlayerActor) playerEnterRoom(g, actor);
    else if (actor == kProjectileActor) projectileEnterRoom(g, actor);
    else chimpEnterRoom(g, actor);
}

bool actorsRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    const auto ds = s.dgroup();
    if (ds.size() < dg::kActors + kActorCount * 0xA0u) {
        why = "the state's DGROUP is too short for g_actors";
        return false;
    }
    ActorsObjects& o = g.actorsObjects();
    // The sets the state has loaded (g_playerSets DS:0a9e, g_chimpSetHall DS:9f22, g_chimpSetRoom DS:9f1e).
    constexpr uint16_t kPlayerSetIds[4] = {kPlayerKind.hallSet, kPlayerKind.roomSet, kProjectileKind.hallSet,
                                           kProjectileKind.roomSet};
    for (size_t k = 0; k < o.playerSetsLoaded.size(); ++k) {
        o.playerSetsLoaded[k] = s.dsDword(static_cast<uint16_t>(dg::kPlayerSets + 4 * k)) != 0;
        if (o.playerSetsLoaded[k]) actorSet(g, kPlayerSetIds[k]);
    }
    // The chimps' kind: their sprite base (Chimps_SetupActors wrote it with the sets' level).
    const bool big = g.actors.actors[kFirstChimpActor].spriteBase == kBigChimpKind.spriteBase ||
                     (g.actors.actors[kFirstChimpActor].spriteBase == 0 && g.player.level > 2);
    const ActorKind& chimps = big ? kBigChimpKind : kChimpKind;
    o.chimpSeqHall = chimps.hallSet;
    o.chimpSeqRoom = chimps.roomSet;
    o.chimpSetHall = s.dsDword(dg::kChimpSetHall) != 0 ? chimps.hallSet : 0;
    o.chimpSetRoom = s.dsDword(dg::kChimpSetRoom) != 0 ? chimps.roomSet : 0;
    if (o.chimpSetHall) actorSet(g, o.chimpSetHall);
    if (o.chimpSetRoom) actorSet(g, o.chimpSetRoom);
    // Per actor: the table +0x52 (equal to the hall table +0x56 or the room table +0x5a), the last frame,
    // the bundle.
    for (int i = 0; i < kActorCount; ++i) {
        Actor& a = actorAt(g, i);
        const auto rec = ds.subspan(dg::kActors + static_cast<size_t>(i) * 0xA0, 0xA0);
        const bool isHall = std::equal(rec.begin() + 0x52, rec.begin() + 0x56, rec.begin() + 0x56);
        const bool isRoom = std::equal(rec.begin() + 0x52, rec.begin() + 0x56, rec.begin() + 0x5A);
        a.hallSet = isHall && !isRoom ? true : isRoom && !isHall ? false : roomIsHall(g, a.room);
        const AnimStep* st = lastFrameStep(a, sequenceSet(g, i));
        a.frame = st ? st->op : -1;
        if (g.actors.actorSpritesLoaded != 0 && a.bundle >= 0) g.cache.loadBundle(static_cast<uint16_t>(a.bundle));
        a.sprite.picture = actorPicture(g, a);
    }
    // The carried lists.
    g.actors.playerParts = partListOfState(s, s.farPointerAt(static_cast<uint16_t>(dg::kActors + 0x72)));
    g.actors.chimpParts = partListOfState(s, s.farPointerAt(dg::kChimpParts));
    return true;
}

}  // namespace tmport::gg
