#pragma once
// Elf actors (segment 1178 elves.cpp; re/docs/code_1168-1198.md §4, ARCHITECTURE.md §5.5,
// verified against the export 1178:*): spawn and random placement, the per-tick behaviour
// scripts (wander, hop, carry, flee) with room changes, side doors and the room-36 fountain, the
// question carriers, the net catch (+1 coin; the question trigger at net step 7; release at step
// 9), the sparkle thrower (only one throw at a time; -1 coin on a hit, the coin flies back) and
// the flight of the elves when the player opens a gate.
//
// Every random number is Borland R(n) in the original's call order (ARCHITECTURE.md §5.16), so a
// fixed seed and fixed input reproduce the original's elves.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "game/game_hooks.h"
#include "game/game_state.h"
#include "game/room/room_state.h"
#include "game/room/room_tables.h"
#include "gfx/frame_buffer.h"
#include "gfx/pixmap.h"
#include "rtl/borland_rand.h"

namespace tmport {
class ResourceManager;
}

namespace tmport::game::room {

// ElfAnimSet (DS:6228 + 0x32*set).
struct ElfAnimSet {
    int16_t frameCount = 0;  // +0x00 Bndl_Count
    int16_t one = 0;         // +0x02 always 1, never read
    int16_t behaviour = 0;   // +0x04 = the set index (handler index)
    std::vector<std::shared_ptr<const gfx::PixMap>> frames;  // +0x06 far[10]
    bool loaded = false;     // +0x2e BNDL held
};

// Elf (DS:6386 + 0x12*i).
struct Elf {
    int16_t set = -1;        // +0x00 (-1 = free)
    int16_t behaviour = 0;   // +0x02 index into the handler table DS:2c8f
    int16_t step = 0;        // +0x04
    int16_t x = 0, y = 0;    // +0x06 / +0x08
    int16_t facing = 0;      // +0x0a 0 = right, 1 = left
    int16_t room = 0;        // +0x0c
    int16_t inNet = 0;       // +0x0e
    int16_t hasItem = 0;     // +0x10 carries a question
    bool operator==(const Elf&) const = default;
};

// What one elf tick reads and writes besides the elves.
struct ElfTick {
    GameState& s;           // player pose and room, coins, room table, quiz slots
    PlayerRuntime& p;       // g_netStep, g_netX, g_gateWalkStarted, g_jumping
    SceneRuntime& scene;    // g_elfQuizResult
    GameHooks& hooks;       // Sfx_Play, HUD redraw (via the inventory rules)
    BorlandRand& rng;
};

class ElfSystem {
public:
    explicit ElfSystem(const RoomTables& rt) : rt_(rt) {}

    // 1178:01ac Elf_InitSystem.
    void init();
    // 1178:0251 Elf_SpawnForLevel(count): sets 0..6 and the projectile are loaded from res (when
    // given; tests run without pictures), then count elves are placed with R(250), R(2), 15 x R(3).
    void spawnForLevel(GameState& s, int count, BorlandRand& rng, const ResourceManager* res);
    // 1178:0364 Elf_UnloadAll.
    void unloadAll();
    // The loading part of Elf_SpawnForLevel (sets 0..6 and the projectile), and the elf count:
    // for tools and tests that place elves by hand.
    void loadSets(const ResourceManager* res);
    void setCount(int n) { count_ = static_cast<int16_t>(n < 0 ? 0 : n > kMaxElves ? kMaxElves : n); }

    // 1178:0453 Elf_EraseAll / 1178:048d Elf_UpdateAll / 1178:051f Elf_DrawAll.
    void eraseAll(gfx::FrameBuffer& fb, int playerRoom);
    void updateAll(ElfTick& t);
    // netStep = g_netStep (picks the net-ride frame of a caught elf).
    void drawAll(gfx::FrameBuffer& fb, int playerRoom, int yoff, int netStep, GameHooks& hooks);

    // 1178:01e3 Elf_ResetBehaviour.
    void resetBehaviour(int i);

    // State (DS:2ab6..2ca7, 6386, 65ca, 65cc).
    std::array<Elf, kMaxElves>& elves() { return elves_; }
    const std::array<Elf, kMaxElves>& elves() const { return elves_; }
    std::array<ElfAnimSet, 7>& sets() { return sets_; }
    int16_t count() const { return count_; }           // DS:2ab7 elf_count
    uint8_t caughtElf = 0;                              // DS:2ab6 g_caughtElf (elf index + 1)
    uint8_t confined = 0;                               // DS:2ac1 elf_confined
    int16_t netBusy = 0;                                // DS:2ac2 elf_netBusy
    int16_t throwState = 0;                             // DS:2ac4 elf_throwState 0..13
    int16_t projFrame = -1;                             // DS:2ac6 elf_projFrame
    int16_t projSteps = 0;                              // DS:2ca7 elf_projSteps
    int16_t projX = 0;                                  // DS:65ca elf_projX
    int16_t drawYOff = 0;                               // DS:65cc elf_drawYOff

private:
    void placeRandomly(int i, const GameState& s, BorlandRand& rng);  // 1178:00d7
    void checkNetCatch(int i, ElfTick& t);                            // 1178:03bb
    void caughtInNet(int mode, int i, ElfTick* t, gfx::FrameBuffer* fb, int playerRoom);  // 1178:0b1d
    void handleRoomEdges(int i, const GameState& s, BorlandRand& rng);  // 1178:0565
    void runScript(int mode, int i, const ElfScript& sc, ElfTick* t, gfx::FrameBuffer* fb, int playerRoom,
                   GameHooks* hooks);                                  // 1178:0766
    void throwHandler(int mode, int i, ElfTick* t, gfx::FrameBuffer* fb, int playerRoom);  // 1178:0cb0
    void dispatch(int mode, int i, ElfTick* t, gfx::FrameBuffer* fb, int playerRoom, GameHooks* hooks);
    const gfx::PixMap* setFrame(int set, int frame) const;
    const gfx::PixMap* projectileFrame(int frame) const;

    const RoomTables& rt_;
    std::array<ElfAnimSet, 7> sets_{};
    std::array<Elf, kMaxElves> elves_{};
    std::vector<std::shared_ptr<const gfx::PixMap>> proj_;  // DS:65c6 elf_projBndl (BNDL 310)
    int16_t count_ = 0;
};

}  // namespace tmport::game::room
