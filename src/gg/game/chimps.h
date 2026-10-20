#pragma once
// The Cyber Chimps (ssg.exe ovl00 2a80 and the DGROUP tables DS:1adc..1bae, DS:1d2e, DS:1f10;
// re/gg/docs/code_world.md §13): the spawn (rand; the 286 path on GgState's mutable chimp lists:
// chimpList / chimpListFirstRoom), the actors' set-up, the data-driven brain (GgTables::chimpMoves,
// chimpTransitions; R(1000) per firing probe), doors, passages and holes, the stun and the dropped part,
// taking parts (a part of a type the chimps hold twice is destroyed: keep).
//
// Owner: actors agent.

#include <cstdint>

namespace tmport::gg {

class GgGame;

void chimpTakePart(GgGame& g, int actor);     // 2a80:0000 Chimp_TakePart (op -10): a crate moves to a random room (rand); a part when it holds none
void chimpDropPart(GgGame& g, int actor);     // 2a80:00a1 Chimp_DropPart (op -33): its first held part falls where it stands
int chimpBrain(GgGame& g, int actor);         // 2a80:0112 Chimp_Brain: moves and transitions; the next sequence or -1
void chimpEnterRoom(GgGame& g, int actor);    // 2a80:032f Chimp_EnterRoom (+0x8c): g_roomChimpCount[room] + 1, the pending sequence, the door twin's place
void chimpSequenceEnd(GgGame& g, int actor);  // 2a80:0431 Chimp_SequenceEnd (op -11): the target room and entry sequence
void chimpsLoadSets(GgGame& g);               // 2a80:051e Chimps_LoadSets: 0x1042 / 0x1041 (levels 0-2) or 0x103e / 0x103d, cached
void chimpsFreeSets(GgGame& g);               // 2a80:05e6 Chimps_FreeSets
void chimpsSetupActors(GgGame& g);            // 2a80:0635 Chimps_SetupActors: actors 2..11's fields, the shared part list
void chimpsFree(GgGame& g);                   // 2a80:076b Chimps_Free: the sets and the shared part list
void chimpsSpawn(GgGame& g);                  // 2a80:0784 Chimps_Spawn: the level's chimps in their rooms (R(6) / R(3), R(100) each)

}  // namespace tmport::gg
