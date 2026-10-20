#include "gg/game/building.h"

#include "gg/data/archive.h"
#include "gg/data/gg_exe.h"
#include "gg/game/pic_cache.h"
#include "gg/gfx/video.h"
#include "rtl/borland_rand.h"

namespace tmport::gg {

Building::Building(const GgExe& exe, const Archive& archive, PicCache& cache)
    : exe_(exe), archive_(archive), cache_(cache) {}

// 12e0:0820 State_04_BuildingSetup, the loads
void Building::setup(int b, int lvl, BorlandRand& rng, Hud& hud, Video& video) {
    building = b;
    level = lvl;
    raceIndex = b * 5 + lvl;
    parts_ = PartCatalogue::load(archive_, cache_, raceIndex);  // 153f:0002
    rooms_.load(archive_, exe_, raceIndex, parts_, deck_, rng);  // 1622:0085
    hud.drawAll(video.screen(), 7);                              // 1a04:02c2 Hud_DrawAll(7)
    counters.partsNeeded = parts_.neededCount;
    loadActorSets();
}

const ActorSet* Building::actorSet(uint16_t id) const {
    const auto it = sets_.find(id);
    return it == sets_.end() ? nullptr : &it->second;
}

// 18f5:05a3 Actors_LoadPlayerSets, 2a80:051e Chimps_LoadSets (Actors_LoadSet = Res_LoadCopy)
void Building::loadActorSets() {
    kinds_[kPlayer] = kPlayerKind;
    kinds_[kProjectile] = kProjectileKind;
    for (size_t i = kFirstChimp; i < kinds_.size(); ++i) kinds_[i] = chimpKindForLevel(level);
    for (const ActorKind& k : kinds_)
        for (const uint16_t id : {k.hallSet, k.roomSet})
            if (!sets_.count(id)) sets_.emplace(id, ActorSet::parse(archive_.read(id)));
    for (size_t i = 0; i < actors_.size(); ++i) {
        actors_[i].spriteBase = kinds_[i].spriteBase;
        actors_[i].id = static_cast<int>(i);
    }
}

ActorScale Building::scaleOf(int slot, bool hall) const {
    const ActorKind& k = kinds_.at(static_cast<size_t>(slot));
    ActorScale s;
    s.set = actorSet(hall ? k.hallSet : k.roomSet);
    s.pictures = cache_.pictures(static_cast<uint16_t>(k.spriteBase + (hall ? 1 : 0)));
    return s;
}

// 12e0:096a State_05_EnterRoom (Input_Flush, the play mode and the menu bar are the runtime's and
// the UI's), 1622:0411 Room_Enter, 1834:07ad Actors_EnterRoom (the loads)
void Building::enterRoom(int room, Video& video, Hud& hud, const RoomMusicHook& music) {
    rooms_.enter(room, video.screen(), cache_, music);
    if (room != 0) {
        const int scale = rooms_.room(room).isHall() ? 1 : 0;
        for (size_t i = 0; i < actors_.size(); ++i) {
            if (i >= kFirstChimp && !withChimps) break;
            Actor& a = actors_[i];
            const auto bundle = static_cast<uint16_t>(a.spriteBase + scale);
            // Actors_LeaveRoom (1834:0870) released the previous room's bundles.
            if (a.bundle >= 0) cache_.releaseBundle(static_cast<uint16_t>(a.bundle));
            a.bundle = bundle;
            cache_.loadBundle(bundle);
        }
        // TODO(wave 3): Player_EnterRoom, Projectile_EnterRoom and Chimp_EnterRoom (wave 3).
    }
    hud.dirty |= Hud::kFull;
}

void Building::queueFrame(Video& video, bool stepAnims) {
    SpriteList& sprites = video.sprites();
    rooms_.drawLayer(1, sprites);
    if (stepAnims) rooms_.drawAnimLayer(0, sprites);
    else rooms_.queueAnims(0, sprites);
    rooms_.drawLayer(2, sprites);
    actorsDraw(actors_, rooms_.displayRoom, playerBlink, sprites);
    if (stepAnims) rooms_.drawAnimLayer(3, sprites);
    else rooms_.queueAnims(3, sprites);
    rooms_.drawLayer(3, sprites);
    sprites.drawAll(video.screen());
}

// 12e0:0619..069a (State_07_Warehouse entry, without Actors_ResetCountdowns / Actors_Tick)
void Building::drawEntryFrame(Video& video, Hud& hud) {
    hud.update(video.screen(), 0, counters, rooms_.displayRoom, strip);
    video.sprites().reset();
    rooms_.queueEntryLayers(video.screen(), video.sprites());
    actorsDraw(actors_, rooms_.displayRoom, playerBlink, video.sprites());
    rooms_.drawLayer(3, video.sprites());
    video.sprites().drawAll(video.screen());
}

// 12e0:06bc..06f6
void Building::drawFrame(Video& video, Hud& hud) {
    video.sprites().eraseAll(video.screen());
    queueFrame(video, true);
    hud.update(video.screen(), 0, counters, rooms_.displayRoom, strip);
}

void Building::drawSavedFrame(Video& video, Hud& hud) {
    video.sprites().eraseAll(video.screen());
    queueFrame(video, false);
    hud.update(video.screen(), 0, counters, rooms_.displayRoom, strip);
}

}  // namespace tmport::gg
