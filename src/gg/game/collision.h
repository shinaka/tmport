#pragma once
// Collision and contacts (ssg.exe 1adb; re/gg/docs/code_world.md §10): the wall sweep of a frame step,
// the contact flags before every brain call (items, actors, floor references) and the chimps' probes.
// Rect_Overlap 1adb:000c is rooms.h's rectOverlap.
//
// Owner: actors agent.

#include <cstdint>

#include "gg/game/rooms.h"

namespace tmport::gg {

class GgGame;

// 1adb:0093 Actor_CollideWalls(i, sweep, dir): the sweep against the wall list of the actor's floor
// (-3 in small rooms); +0x4e the wall edge, +0x4a the bits; returns them (1 wall at the right: wall x,
// 2 at the left: wall right - 1).
int actorCollideWalls(GgGame& g, int actor, const HitRecord& sweep, int dir);
// 1adb:01ef Chimp_ProbeContacts(i, probe): the kinds of the items (0x800, assigned to +0x48 with +0x40)
// and floor objects (kinds 0x780 also set +0x38) a probe touches.
uint16_t chimpProbeContacts(GgGame& g, int actor, const HitRecord& probe);
// 1adb:03b0 Actor_UpdateContacts(i): +0x48 from the room's items, the other actors of the contact
// list (GgTables contacts*, 0x1000 / 0x2000 and +0x6e), the floor references of the floor (doors,
// passages, pads, edges, holes: +0x36, +0x38, +0x3a).
void actorUpdateContacts(GgGame& g, int actor);

}  // namespace tmport::gg
