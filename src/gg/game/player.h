#pragma once
// The player and the projectile (ssg.exe 18f5; re/gg/docs/code_world.md §11, §12): the callbacks of
// actors 0 and 1, the player's brain on the held keys and the transition lists (GgTables
// ::playerTransitions, with the jump records of the room), the room changes through doors, holes,
// passages and the tube, the pick-up rules (at most 2 of a type), the chimp hit (rand), the banana.
//
// Owner: actors agent.

#include <cstdint>

namespace tmport::gg {

class GgGame;

// 18f5:0002 Player_PickUpItem (op -10): the item touched (+0x40) unless taken this frame: a crate gives
// 5 bananas (Projectiles_Add) and g_cratesLeft - 1; a part: refused as the third of its type
// (Hud_FlashPart, hint 0x1047), else effect 1, Actor_AddPart, Hud_AddPart, hint 0x104a / 0x1044,
// Room_RemoveItem, g_partsNeeded - 1 for the first of a needed type (hint 0x1050 at 0), Score_Add(20)
// unless flagged 0x4000.
void playerPickUpItem(GgGame& g, int actor);
// 18f5:0180 Player_HitByChimp (op -29): Hud_EscHook, 150 frames of immunity, the chimp (+0x6e) takes a
// random carried part (R(+0x70)); the chimps never hold 3 of a type (Item_PlaceInRandomRoom).
void playerHitByChimp(GgGame& g, int actor);
// 18f5:029b Player_Brain: the input mask (Input_GetHeldKeys | contacts << 16, chimp contacts masked
// while immune) through the transition list of the sequence; the jump records of the room (groups 1,
// 2); the next sequence or -1.
int playerBrain(GgGame& g, int actor);
// 18f5:0401 Projectile_Brain: -1.
int projectileBrain(GgGame& g, int actor);
// 18f5:0409 Player_EnterRoom (+0x8c): the scale, the pending sequence (+0x2c) started, the position at
// the linked door (sequences 33, 44, 45) or the tube's floor 5 (53).
void playerEnterRoom(GgGame& g, int actor);
// 18f5:04cf Player_SequenceEnd (op -11; also the workshop's tube exit with object -1): the target room
// and entry sequence from the object used (+0x36: none -> the tube room, holes, doors, passages, the
// tube; a locked door sets g_puzzleDoorHit and DS:458c), +0x20 / +0x2c = S, +0x3c = the room,
// g_playerRoom = T.
void playerSequenceEnd(GgGame& g, int actor);
// 18f5:05a3 Actors_LoadPlayerSets / 18f5:06b7 Actors_FreePlayerSets: the sets 0x1034 / 0x1033 (player)
// and 0x1036 / 0x1035 (projectile), cached (ActorsObjects).
void actorsLoadPlayerSets(GgGame& g);
void actorsFreePlayerSets(GgGame& g);
// 18f5:06fb Projectile_EnterRoom (+0x8c of actor 1): the scale, +0x10 = 0.
void projectileEnterRoom(GgGame& g, int actor);
// 18f5:0721 Actors_Init (Game_Init): the per-slot callbacks and tables, ids, delays, the projectile
// disabled (GgState's gameInitState sets the DGROUP words; this is the engine side).
void actorsInit(GgGame& g);
// 18f5:0895 Projectile_Launch(seq, dx) (the player's op -27): nothing without bananas; else actor 1
// enabled at the player's place, g_projectiles - 1, HUD bit 8; the last banana with no crate left:
// g_cratesTotal crates placed in random rooms (rand), g_cratesLeft = g_cratesTotal.
void projectileLaunch(GgGame& g, int seq, int dx);

}  // namespace tmport::gg
