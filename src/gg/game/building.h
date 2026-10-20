#pragma once
// A building in progress as the warehouse draws it: the drawing steps of states 4, 5 and 7 (12e0:0820,
// 12e0:096a, 12e0:0619; re/gg/docs/code_world.md §4, §8.2, §14, §15.1) over a GgState's building (its
// part catalogue, deck, rooms, twelve actors and the HUD values of the player record), with the actor
// sets and sprite bundles they draw with. The game's Building is bound to GgGame's state (wave 3); a
// tool's or test's owns a GgState of its own. The brains, the step interpreter, the chimps' set-up and
// every input are wave 3's: they change the state between the frames drawn here.

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include <memory>

#include "gg/game/actors.h"
#include "gg/game/game_state.h"
#include "gg/game/hud.h"
#include "gg/game/parts.h"
#include "gg/game/rooms.h"

namespace tmport {
class BorlandRand;
}

namespace tmport::gg {

class Archive;
class GgExe;
class PicCache;
class Video;

class Building {
public:
    static constexpr int kPlayer = 0;
    static constexpr int kProjectile = 1;
    static constexpr int kFirstChimp = 2;

    // Bound to `state` (the game's GgState: GgGame).
    Building(const GgExe& exe, const Archive& archive, PicCache& cache, GgState& state);
    // With a GgState of its own (tools, tests); its player record has g_withChimps = 1.
    Building(const GgExe& exe, const Archive& archive, PicCache& cache);
    Building(const Building&) = delete;
    Building& operator=(const Building&) = delete;
    ~Building();

    GgState& state() { return *st_; }
    const GgState& state() const { return *st_; }

    // State 4's loads (12e0:0820): g_building, g_level, g_raceIndex, Parts_LoadCatalogue (its icon
    // bundle's palette applied), Rooms_Load (the deck and the item grids: 20 * I[0] + 20 * cells rand()
    // calls per room with items; g_roomCount), Hud_DrawAll(7), g_partsNeeded = the catalogue's count.
    // The actor set-up of a new visit or a resume is wave 3's.
    void setup(int building, int level, BorlandRand& rng, Hud& hud, Video& video);

    // The actor sets of the level (Actors_LoadPlayerSets 18f5:05a3, Chimps_LoadSets 2a80:051e) and
    // every actor's kind: slot 0 the player, 1 the projectile, 2..11 chimps (big from level 3).
    void loadActorSets();
    // State 5 (12e0:096a), the drawing: Room_Enter(room), then Actors_EnterRoom's loads (1834:07ad:
    // the sprite bundle of the room's scale for the player, the projectile and, with chimps, every
    // chimp, palettes applied) and g_hudDirty |= 0x8000. Room 0 (the workshop) loads no actor sprites.
    void enterRoom(int room, Video& video, Hud& hud, const RoomMusicHook& music = {});
    // The sets and pictures an actor draws with in a room of this scale (Actor_SelectScale).
    ActorScale scaleOf(int slot, bool hall) const;

    // State 7's entry frame (12e0:0619..069a without the actor tick): Hud_Update(0), Sprites_Reset,
    // Room_QueueEntryLayers, Actors_Draw, Room_DrawLayer(3), Sprites_DrawAll.
    void drawEntryFrame(Video& video, Hud& hud);
    // A warehouse frame's drawing (12e0:06bc..06f6, code_world.md §8.2): Sprites_EraseAll, layer 1,
    // animated list 0 (stepping the halls' pads and flaps), layer 2, Actors_Draw, list 3, layer 3
    // and the items, Sprites_DrawAll, Hud_Update(0). The flush is the caller's.
    void drawFrame(Video& video, Hud& hud);
    // The same frame from a saved state (tools): the animated objects are queued as they are
    // instead of being stepped.
    void drawSavedFrame(Video& video, Hud& hud);

    // The state's building (GgState::world, ::actors, ::player).
    const PartCatalogue& parts() const { return st_->world.parts; }
    PartCatalogue& parts() { return st_->world.parts; }
    PartDeck& deck() { return st_->world.deck; }
    Rooms& rooms() { return st_->world.rooms; }
    const Rooms& rooms() const { return st_->world.rooms; }
    std::array<Actor, 12>& actors() { return st_->actors.actors; }
    const ActorSet* actorSet(uint16_t id) const;

    int building() const { return st_->player.building; }        // g_building DS:4592
    int level() const { return st_->player.level; }              // g_level DS:4594
    int raceIndex() const { return st_->player.raceIndex; }      // g_raceIndex DS:4590
    bool withChimps() const { return st_->player.withChimps != 0; }  // g_withChimps DS:4578
    // The values the HUD counters show: g_partsNeeded DS:4582, g_projectiles DS:457e, g_score DS:4572.
    HudCounters counters() const;
    std::vector<HudStripEntry> strip;  // the player's carried list as the strip draws it (engine-side)

private:
    void queueFrame(Video& video, bool stepAnims);

    const GgExe& exe_;
    const Archive& archive_;
    PicCache& cache_;
    std::unique_ptr<GgState> own_;
    GgState* st_;
    std::map<uint16_t, ActorSet> sets_;
    std::array<ActorKind, 12> kinds_{};
};

}  // namespace tmport::gg
