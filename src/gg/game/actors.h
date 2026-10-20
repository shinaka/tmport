#pragma once
// The actor system of segment 1834 (re/gg/docs/code_world.md §9): the actor sets ('S' 0x1033..0x1036,
// 0x103d, 0x103e, 0x1041, 0x1042), the 12 actor records (0 the player, 1 the projectile, 2..11 the
// Cyber Chimps), a frame shown at a given place and Actors_Draw (the wave 2 drawing half, used by the
// tools), and on the game (wave 3, "the run" below; game/actors_run.cpp): the step interpreter, the
// per-frame tick, room entry and exit, the carried parts and the building block. The player and the
// projectile are game/player.h (18f5), collision game/collision.h (1adb), the chimps game/chimps.h
// (2a80).
//
// Actor set (Actors_LoadSet 1834:0088):
//   u16 nFrames; i16 W[nFrames]; i16 H[nFrames]; u16 nSeq;
//   nSeq sequences: header {i16 delay, i16 dir, far steps (patched), u16 count}, count steps
//   {i16 op, a, b, u16 c, u16 d}; the last step of a sequence has op -1000, and the very last
//   sequence of a set stores only that step's op word.
// A frame step: op = frame (picture op of the sprite bundle +0x98 + scale, and W[op] x H[op]),
// a = x delta, b = y offset, c = sprite flags.

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "gg/game/screen_anim.h"
#include "gg/gfx/sprites.h"

namespace tmport::gg {

class GgGame;
class GgStateFile;
class PicCache;
struct BuildingBlock;

struct ActorSet {
    std::vector<int16_t> widths;   // W, the far pointer +0x76 / +0x7A points here
    std::vector<int16_t> heights;  // H
    std::vector<AnimSequence> sequences;

    // 1834:0088 Actors_LoadSet. Throws tmport::FormatError when the set does not parse.
    static ActorSet parse(std::span<const uint8_t> payload);
    const AnimSequence* sequence(int n) const {
        return n >= 0 && static_cast<size_t>(n) < sequences.size() ? &sequences[static_cast<size_t>(n)] : nullptr;
    }
};

// The resources of one kind of actor (code_world.md §9.1 table).
struct ActorKind {
    uint16_t hallSet = 0;     // 'S' set of the halls
    uint16_t roomSet = 0;     // 'S' set of the small rooms (and the workshop)
    uint16_t spriteBase = 0;  // +0x98: sprite bundle of the large scale; + 1 = the hall scale
};
inline constexpr ActorKind kPlayerKind{0x1034, 0x1033, 0x1031};
inline constexpr ActorKind kProjectileKind{0x1036, 0x1035, 0x1037};
inline constexpr ActorKind kChimpKind{0x1042, 0x1041, 0x103F};     // levels 0..2
inline constexpr ActorKind kBigChimpKind{0x103E, 0x103D, 0x103B};  // levels 3..4 (Chimps_SetupActors)
// Chimps_SetupActors 2a80:0635: the big chimps from level 3 on.
inline const ActorKind& chimpKindForLevel(int level) { return level > 2 ? kBigChimpKind : kChimpKind; }

// An actor record (12 x 0xA0 bytes at g_actors DS:7a8a). The fields +0x14..+0x41 are what
// Actors_SaveState writes into the building block (players_dat.h ActorState).
struct Actor {
    static constexpr uint16_t kHidden = 0x8000;  // +0x00 flag of op -8

    Sprite sprite;                // +0x00 flags, +0x02 x, +0x04 y, +0x06 w, +0x08 h, +0x0A picture
    int bundle = -1;              // +0x0E loaded sprite bundle (+0x98 + scale), -1 none
    int countdown = 0;            // +0x10
    int delay = 0;                // +0x12
    int anchorX = 0, anchorY = 0; // +0x14, +0x16
    int seq = 0;                  // +0x18
    int room = 0;                 // +0x1A
    int floor = 0;                // +0x1C hall floors 0..5
    int prevSeq = 0;              // +0x1E
    int exitSeq = 0;              // +0x20
    int step = 0;                 // +0x22
    int doorCooldown = 0;         // +0x24
    int loopCount = 0;            // +0x26
    int loopStart = 0;            // +0x28
    int disabled = 0;             // +0x2A
    int pendingSeq = -1;          // +0x2C
    int wallsOn = 0;              // +0x2E
    int contactsOn = 0;           // +0x30
    int blocked = 0;              // +0x32
    int unused34 = 0;             // +0x34
    int doorObj = 0;              // +0x36
    int pad = 0;                  // +0x38
    int edgeObj = 0;              // +0x3A
    int prevRoom = 0;             // +0x3C
    int immune = 0;               // +0x3E
    int item = 0;                 // +0x40
    int id = 0;                   // +0x42
    int contacts = 0;             // +0x48 contact flags (code_world.md §10.3)
    int wallBits = 0;             // +0x4A Actor_CollideWalls' result
    int dir = 0;                  // +0x4C
    int wallX = 0;                // +0x4E the wall edge Actor_CollideWalls found
    int toucher = 0;              // +0x6E +0x42 of the actor touched last
    int carried = 0;              // +0x70
    uint16_t transTable = 0;      // +0x90 near pointer: player DS:127e, projectile DS:1276, chimps 0 (written only)
    uint32_t field94 = 0;         // +0x94 chimps: contacts << 16 after a brain call; the spawn's side 1 / 2 (written only)
    uint16_t spriteBase = 0;      // +0x98
    // The other offsets are far pointers (picture +0x0A, sequence tables +0x52..+0x5D, callbacks
    // +0x62..+0x6D and +0x88..+0x8F, the carried list +0x72, the W/H tables +0x76..+0x85, the slots
    // +0x9A): what they point at follows from the slot (0 player, 1 projectile, 2..11 chimps) and the
    // room's scale; and words never accessed (+0x44, +0x46, +0x50, +0x5E, +0x86, +0x92, +0x9E).
    // Two of them carry a choice the engine keeps (not in the DGROUP image; actorsRestoreFromState
    // reads them from a state's pointers):
    bool hallSet = false;         // +0x52: Actor_SelectScale chose the hall set's table (+0x56), else the room set's (+0x5A)
    int frame = -1;               // +0x0A: the frame (op) of the last frame step; the picture is that slot of the
                                  // bundle +0x0E (AnimSeq_BindPictures bound d = slots[op] when the scale was chosen)

    bool hidden() const { return (sprite.flags & kHidden) != 0; }
};

// The pictures and sets an actor draws with at one scale.
struct ActorScale {
    const ActorSet* set = nullptr;                                 // +0x52 (the hall or room set)
    const std::vector<std::span<const uint8_t>>* pictures = nullptr; // the slots +0x9A of the bundle
};

// What Actor_RunStep does with a frame step, without the motion and the wall collision: flags =
// c, w = W[op], h = H[op], picture = slot op; x = anchor x, y = anchor y + b (the anchor already
// moved by a). Draws nothing.
void actorShowFrame(Actor& a, const ActorScale& scale, const AnimStep& s);
// The frame Actor_RunStep showed last: the frame step before the actor's step index in its
// sequence (save states hold +0x22 after the increment). Returns nullptr when there is none.
const AnimStep* lastFrameStep(const Actor& a, const ActorSet& set);

// 1834:0733 Actors_Draw: actors 1..11 that are enabled, in the displayed room and not hidden,
// then the player (always last): when not immune the blink phase g_playerBlink DS:0a9a resets to
// 0; a visible player is queued while blink < 2, and while immune blink = (blink + 1) % 4.
void actorsDraw(const std::array<Actor, 12>& actors, int displayRoom, int16_t& playerBlink, SpriteList& sprites);


// ---- wave 3: the run (owner: actors agent; game/actors_run.cpp) -------------------------------------
// The actor system on the game: GgState::actors (the 12 records, the blink phase, the chimp counts,
// the mutable chimp lists, the carried lists), the sets and bundles in ActorsObjects. The callbacks the
// original keeps in the records (+0x62 brain, +0x66 op -10, +0x6A op -29, +0x88 op -11, +0x8C room
// entry) follow from the slot: 0 the player (player.h), 1 the projectile (player.h), 2..11 the chimps
// (chimps.h); actorBrain .. actorOnEnter dispatch them.

// The actors module's engine-side objects (GgGame::actorsObjects).
//   * The sets: Actors_LoadSet's copies (Res_LoadCopy) by resource id. What the DGROUP pointers say is
//     loaded is kept apart: g_playerSets DS:0a9e[4] (0x1034, 0x1033, 0x1036, 0x1035) and g_chimpSetHall
//     DS:9f22 / g_chimpSetRoom DS:9f1e, which gate the next load. The chimps' sequence tables DS:9e72 /
//     DS:9dc6 and their W/H pointers DS:a010..a01a keep naming the chimp sets loaded last after a free
//     (dangling in the original); a set used while freed is read again (actorSet), which gives the same
//     bytes.
//   * The sprite bundles each actor loaded are the picture cache's (Actor::bundle, PicCache's use
//     counts); a frame's picture is resolved from the bundle when it is drawn (Actor::frame).
struct ActorsObjects {
    std::map<uint16_t, ActorSet> sets;
    std::array<bool, 4> playerSetsLoaded{};  // g_playerSets DS:0a9e[4] != NULL
    uint16_t chimpSetHall = 0;               // g_chimpSetHall DS:9f22: the id loaded, 0 = NULL
    uint16_t chimpSetRoom = 0;               // g_chimpSetRoom DS:9f1e
    uint16_t chimpSeqHall = kChimpKind.hallSet;  // DS:9e72 / DS:a018: the hall set the chimps' tables hold
    uint16_t chimpSeqRoom = kChimpKind.roomSet;  // DS:9dc6 / DS:a010: the room set
};

// The set of resource `id` (a cached Actors_LoadSet copy; read now if it is not cached).
const ActorSet& actorSet(GgGame& g, uint16_t id);
// The kind of an actor slot as its record's far pointers name it: 0 the player, 1 the projectile,
// 2..11 the chimps (the sets their tables hold, the sprite base +0x98 of the record).
ActorKind actorKind(GgGame& g, int actor);
// The current sequence through the table +0x52: (+0x52)[+0x18]; nullptr past the table.
const AnimSequence* actorSequence(GgGame& g, int actor);
// The carried-part list +0x72: the player's own, the chimps' shared g_chimpParts DS:a03c; nullptr for
// the projectile (its +0x72 is NULL).
std::vector<int16_t>* actorPartList(GgGame& g, int actor);
// The picture of the actor's last frame (slot `frame` of its bundle +0x0e), empty when the bundle is not
// loaded (an actor outside the displayed room, or after Actors_LeaveRoom).
std::span<const uint8_t> actorPicture(GgGame& g, const Actor& a);

// 1834:003b Actor_SetSequence(i, seq): +0x1e = +0x18, +0x18 = seq, step 0, delay and dir from the
// sequence header of the table +0x52.
void actorSetSequence(GgGame& g, int actor, int seq);
// 1834:015b Actor_SelectScale(i, hall): +0x52 = the hall set's table when hall == 1, else the room
// set's, and its pictures bound to the slots +0x9a.
void actorSelectScale(GgGame& g, int actor, bool hall);
// 1834:01b6 Actor_RunStep(i): the step interpreter of code_world.md §9.3 (46 ops, the frame step with
// the wall sweep and the screen clamp); 1 when the player's sequence ended (op -11), else 0.
int actorRunStep(GgGame& g, int actor);
// 1834:0733 Actors_Draw on the game's sprite list (the wave 2 actorsDraw over GgState::actors, each
// actor's picture taken from its bundle first).
void actorsDraw(GgGame& g);
// 1834:07ad Actors_EnterRoom: not in the workshop: the player's and the projectile's sets, the chimps'
// sets, g_actorSpritesLoaded = 1, each actor's bundle of the room's scale (the chimps' only with
// chimps), Player_EnterRoom, Projectile_EnterRoom, Chimp_EnterRoom of the enabled chimps in the room.
void actorsEnterRoom(GgGame& g);
// 1834:0870 Actors_LeaveRoom: not in the workshop and when g_actorSpritesLoaded: the bundles released
// (+0x0e = -1), g_actorSpritesLoaded = 0.
void actorsLeaveRoom(GgGame& g);
// 1834:08d7 Actor_AddPart(i, part): unless the list +0x72 holds two of its type: flags &= 0x4000, the
// pair links (+0x1c), holder i, +0x70 + 1, appended; 1 when it is the second of its type, else 0.
int actorAddPart(GgGame& g, int actor, int part);
// 1834:0990 Actor_RemovePart(i, part, checkHolder): the original's third argument: non-zero takes the
// part only when actor i holds it, 0 whoever holds it (the chimps' shared list). The holder's +0x70 - 1,
// holder -1, unlinked; a partner it was the first of moves to its place; the partner's link -1.
// Returns (parts of its type in the list before) - 1.
int actorRemovePart(GgGame& g, int actor, int part, bool checkHolder);
// 1834:0ace Actors_ResetCountdowns: +0x10 = 0 for all 12 (the warehouse entry).
void actorsResetCountdowns(GgGame& g);
// 1834:0ae9 Actors_Tick: actors 0..11 in slot order (every room): contacts, the brain, a new sequence,
// the countdown and Actor_RunStep; non-zero when the player's sequence ended (leave the room).
int actorsTick(GgGame& g);
// 1834:0b5e Actors_SaveState / 1834:0b95 Actors_RestoreState: the fields +0x14..+0x41 of the 12
// actors in the block (+0x492); the restore then selects the scale and the delay / direction.
void actorsSaveState(GgGame& g, BuildingBlock& block);
void actorsRestoreState(GgGame& g, const BuildingBlock& block);
// The callbacks by slot (the record's far pointers): +0x62 the brain (returns the next sequence | 0x8000
// to keep the countdown, or -1), +0x66 step op -10, +0x6A op -29, +0x88 op -11, +0x8C the room entry.
int actorBrain(GgGame& g, int actor);
void actorOnTake(GgGame& g, int actor);
void actorOnHit(GgGame& g, int actor);
void actorOnEnd(GgGame& g, int actor);
void actorOnEnter(GgGame& g, int actor);
// The actors' part of starting from a DOSBox-X state (game/load_state.h), after GgStateFile::apply and
// the world's restore (the part catalogue): the sets the state has loaded, each actor's table +0x52 and
// last frame, the bundles (loaded into the cache when g_actorSpritesLoaded), and the carried lists
// (actor 0 +0x72, g_chimpParts DS:a03c) from the state's RAM. False with a reason when the state's
// actor data cannot be read.
bool actorsRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why);

}  // namespace tmport::gg
