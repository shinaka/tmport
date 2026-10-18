#pragma once
// Runtime globals of the room tick that are not part of the persisted GameState: the player
// controller's action state (segment 10e0, DS:0b98..0e87, 5ccb, 5e2a..5e4f), the scene's
// per-room state (1188 gate lock, 1198 fountain, 1190 treasure hall) and the Room_Run flags.
// Kept with the original's meanings and units (DGROUP address in each comment) so the specs map
// line by line (re/docs/code_10e0-10f0.md §3.2, code_1168-1198.md §6.2, §7.1, §8.1).
//
// The structs live as long as the climb loop (several fields carry over from one room visit to
// the next, e.g. g_jumping across a type-0 exit).

#include <array>
#include <cstdint>

namespace tmport::game::room {

// 10e0 player.cpp.
struct PlayerRuntime {
    uint8_t nextRoom = 0;           // DS:5ccb g_nextRoom (Room_Run ends when != g_room)
    uint8_t inputLocked = 0;        // DS:0b99 g_inputLocked (auto-walk: input ignored, flushed)
    uint8_t jumping = 0;            // DS:0b9a g_jumping (frames 12..21)
    uint8_t gateWalkStarted = 0;    // DS:0b98 one-tick pulse read by the elves
    uint8_t coinTargetSlot = 0xFF;  // DS:0b9e slot of the current coin toss (0xFF none)
    uint16_t mouseWalkLatched = 0;  // DS:0e87 keep walking toward the mouse while the button is down
    uint8_t roomTransition = 0;     // DS:5e2a type of the pending room change
    uint8_t doorStep = 0;           // DS:5e2b door entry 0..2
    uint8_t coinStep = 0;           // DS:5e2c coin toss 0..13
    uint8_t walkActive = 0;         // DS:5e2d auto-walk in progress
    uint8_t arriveAction = 0;       // DS:5e2e 1 = through the gate, 2 = net taken
    uint8_t playerHidden = 0;       // DS:5e2f never set in the shipped code (only cleared)
    uint8_t netStep = 0;            // DS:5e30 net swing 0..11
    uint8_t emptySwingStep = 0;     // DS:5e31 empty-handed swing 0..11
    int16_t netX = 0;               // DS:5e32 net head x (elf catch test)
    uint16_t fxFrame = 0;           // DS:5e34 effects sprite frame (coin sparkle / POOF), 0 none
    int16_t fxX = 0, fxY = 0, fxW = 0, fxH = 0;  // DS:5e36..5e3c
    int16_t walkTargetX = 0;        // DS:5e3e
    int16_t walkTargetY = 0;        // DS:5e40
    uint8_t walkArg3 = 0;           // DS:5e42 (stored, never read)
    uint8_t walkArriveAction = 0;   // DS:5e43 copied to arriveAction on arrival
    uint8_t prevShopCoins = 0;      // DS:5e44 g_shopCoinsLeft at the start of the tick (low byte)
    int16_t entryX = 0, entryY = 0; // DS:5e45 / 5e47 pose for the next room
    uint8_t entryFrame = 0;         // DS:5e49
    uint8_t entryFacing = 0;        // DS:5e4a
    uint16_t inputEvent = 0;        // DS:5e4b this tick's input code
    uint16_t bumpStep = 0;          // DS:5e4d bump at the room-36 fountain 0..12
};

// 1188 / 1190 / 1198 scene state and the Room_Run flags.
struct SceneRuntime {
    uint8_t gateUnlocking = 0;      // DS:2d60 gate_unlocking (lock frame 2; cleared by LevelCutscene_Play)
    int16_t fountainFrame = 0;      // DS:2de0 scene_fountainFrame (BNDL 328 frame, advances per draw)
    uint8_t redrawRequest = 0;      // DS:0b9d g_redrawRequest: 1 rebuild the room, 2 restore the play-field
    uint8_t elfQuizResult = 0;      // DS:0b9f g_elfQuizResult (Quiz_AskQuestion's result, 1 = correct)
    std::array<int8_t, 6> hallPedTreasure{-1, -1, -1, -1, -1, -1};  // DS:2d7e hall_pedTreasure
    std::array<int16_t, 6> hallPedAnim{-1, -1, -1, -1, -1, -1};     // DS:2d84 hall_pedAnim (channel)
};

}  // namespace tmport::game::room
