#pragma once
// The actor system of segment 1834, the data and drawing half (re/gg/docs/code_world.md §9): the
// actor sets ('S' 0x1033..0x1036, 0x103d, 0x103e, 0x1041, 0x1042), the 12 actor records (0 the
// player, 1 the projectile, 2..11 the Cyber Chimps), a frame shown at a given place, and
// Actors_Draw. The step interpreter, the brains and the room changes are wave 3's (Actor_RunStep
// 1834:01b6, Actors_Tick 1834:0ae9, 18f5, 2a80).
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
#include <span>
#include <vector>

#include "gg/game/screen_anim.h"
#include "gg/gfx/sprites.h"

namespace tmport::gg {

class PicCache;

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
    int contacts = 0;             // +0x48
    int dir = 0;                  // +0x4C
    int carried = 0;              // +0x70
    uint16_t spriteBase = 0;      // +0x98

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
void actorsDraw(const std::array<Actor, 12>& actors, int displayRoom, int& playerBlink, SpriteList& sprites);

}  // namespace tmport::gg
