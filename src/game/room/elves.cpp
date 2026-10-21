#include "game/room/elves.h"

#include "game/room/room_gfx.h"
#include "game/rules.h"
#include "mischief/util/log.h"
#include "res/resource_manager.h"

namespace tmport::game::room {

namespace {

// Code constants of 1178.
constexpr int kElfW = 80, kElfH = 72;              // Elf_RunScript / CaughtInNet erase rect (0x50 x 0x48)
constexpr int kPlaceMinX = 30, kPlaceRangeX = 250, kPlaceY = 175, kPlaceSteps = 15;
constexpr int kEdge = 21;                          // x + 21 >= 512 / x < -21
constexpr int kDoorEntryLeftX = -40, kDoorEntryRightX = 472;
constexpr int kFountainLeft = 176, kFountainFootW = 42, kFountainRight = 326;
constexpr int kFountainTurnLeftX = 124, kFountainTurnRightX = 336;
constexpr int kThrowMiddleRight = 319, kThrowMiddleLeft = 193;  // x > 319 facing left / x < 193 facing right
constexpr int kThrowEraseDx = 10, kThrowEraseDy = 18, kThrowEraseW = 50, kThrowEraseH = 75;
constexpr int kProjW = 58, kProjH = 36, kProjStep = 20, kProjMaxSteps = 15;
constexpr int kSfxThrow = 31, kSfxHit = 8;

int16_t i16(int v) { return static_cast<int16_t>(v); }

}  // namespace

const gfx::PixMap* ElfSystem::setFrame(int set, int frame) const {
    if (set < 0 || set >= 7 || frame < 0) return nullptr;
    const auto& f = sets_[static_cast<size_t>(set)].frames;
    return static_cast<size_t>(frame) < f.size() ? f[static_cast<size_t>(frame)].get() : nullptr;
}

const gfx::PixMap* ElfSystem::projectileFrame(int frame) const {
    return frame >= 0 && static_cast<size_t>(frame) < proj_.size() ? proj_[static_cast<size_t>(frame)].get() : nullptr;
}

// 1178:01ac Elf_InitSystem
void ElfSystem::init() {
    for (auto& s : sets_) s = ElfAnimSet{};
    for (auto& e : elves_) e.set = -1;
    count_ = 0;
}

// 1178:01e3 Elf_ResetBehaviour: carriers (0..2) set 3 or 0, hoppers (3..5) set 2, throwers set 1.
void ElfSystem::resetBehaviour(int i) {
    Elf& e = elves_[static_cast<size_t>(i)];
    int set;
    if (i < 3)
        set = e.hasItem ? 3 : 0;
    else
        set = i < 6 ? 2 : 1;
    e.set = i16(set);
    e.behaviour = i16(set);  // ElfAnimSet.+4 == set index
    e.step = 1;
}

// 1178:00d7 Elf_PlaceRandomly
void ElfSystem::placeRandomly(int i, const GameState& s, mischief::BorlandRand& rng) {
    Elf& e = elves_[static_cast<size_t>(i)];
    e.x = i16(rng.random(kPlaceRangeX) + kPlaceMinX);
    e.y = kPlaceY;
    e.facing = i16(rng.random(2));
    e.inNet = 0;
    const uint8_t level = s.currentLevel();
    int room = level < 3 ? rt_.elfStartRoom[level] : rt_.elfStartRoom[0];
    for (int k = 0; k < kPlaceSteps; ++k) {
        // Exits are not checked for 0xFF: the start rooms lie on each level's ring.
        const RoomDef& r = s.rooms[room];
        room = rng.random(3) == 0 ? r.exitRight : r.exitLeft;
        if (!RoomTable::valid(room)) room = 0;  // (never with shipped data)
    }
    e.room = i16(room);
}

// 1178:0251 Elf_SpawnForLevel
void ElfSystem::spawnForLevel(GameState& s, int count, mischief::BorlandRand& rng, const ResourceManager* res) {
    if (count == 0) return;
    if (count_ > 0) unloadAll();
    if (s.player.room == kCastleRoom) return;
    if (count > kMaxElves) count = kMaxElves;
    loadSets(res);
    const uint8_t level = s.currentLevel();
    for (int i = 0; i < count; ++i) {
        Elf& e = elves_[static_cast<size_t>(i)];
        const size_t slot = static_cast<size_t>(level * 3 + i);
        e.hasItem = (i < 3 && level < 3 && s.quiz.slots[slot].q != kQuizNoQuestion) ? 1 : 0;
        resetBehaviour(i);
        placeRandomly(i, s, rng);
    }
    confined = 0;
    count_ = i16(count);
    netBusy = 0;
    throwState = 0;
}

void ElfSystem::loadSets(const ResourceManager* res) {
    // Elf_LoadAnimSet for sets 0..6 (load order 0,1,2,3,4,6,5 as in the original) and BNDL 310.
    for (const int set : {0, 1, 2, 3, 4, 6, 5}) {
        ElfAnimSet& a = sets_[static_cast<size_t>(set)];
        a = ElfAnimSet{};
        a.one = 1;
        a.behaviour = i16(set);
        a.loaded = true;
        if (res) {
            if (const auto b = res->bundle(kElfSetBndl[static_cast<size_t>(set)])) {
                a.frameCount = i16(static_cast<int>(b->ids.size()));
                for (const int id : b->ids) a.frames.push_back(res->pixmap(id));
            } else {
                mischief::log::warn("elves: BNDL {} not found", kElfSetBndl[static_cast<size_t>(set)]);
            }
        }
    }
    proj_.clear();
    if (res) {
        if (const auto b = res->bundle(kElfProjectileBndl))
            for (const int id : b->ids) proj_.push_back(res->pixmap(id));
    }
}

// 1178:0364 Elf_UnloadAll
void ElfSystem::unloadAll() {
    if (count_ == 0) return;
    for (auto& a : sets_) a = ElfAnimSet{};
    for (int i = 0; i < count_; ++i) elves_[static_cast<size_t>(i)].set = -1;
    count_ = 0;
    proj_.clear();
}

// 1178:03bb Elf_CheckNetCatch: net steps 2..7 (post-increment), nobody in the net, same room,
// |x - netX| <= 30: caught, +1 coin.
void ElfSystem::checkNetCatch(int i, ElfTick& t) {
    Elf& e = elves_[static_cast<size_t>(i)];
    if (t.p.netStep > 1 && t.p.netStep < 8 && netBusy == 0 && e.inNet == 0 && t.s.player.room == e.room &&
        t.p.netX - 30 <= e.x && e.x <= t.p.netX + 30) {
        e.inNet = 1;
        e.x = t.p.netX;
        e.facing = t.s.player.facingLeft;
        netBusy = 1;
        addCoins(t.s, 1, t.hooks);
    }
}

// 1178:0b1d Elf_CaughtInNet
void ElfSystem::caughtInNet(int mode, int i, ElfTick* t, gfx::FrameBuffer* fb, int playerRoom) {
    Elf& e = elves_[static_cast<size_t>(i)];
    if (mode == 0) {
        if (playerRoom == e.room) restoreFromLayer(*fb, e.x, e.y, kElfW, kElfH, kLayer1);
    } else if (mode == 1) {
        const int k = t->p.netStep;
        const int8_t f = k >= 0 && k < 10 ? rt_.elfNetFrames[static_cast<size_t>(k)] : int8_t{-1};
        if (f == -1) {
            // Release: a non-carrier, or a carrier answered correctly, flees; after a wrong
            // answer (or Esc) it keeps its question and can be caught again.
            e.inNet = 0;
            netBusy = 0;
            if (e.hasItem == 0 || t->scene.elfQuizResult != 0) {
                e.hasItem = 0;
                e.set = kElfSetFlee;
                e.behaviour = sets_[kElfSetFlee].behaviour;
                e.step = 1;
            } else {
                resetBehaviour(i);
            }
        } else {
            e.x = i16(e.x + (e.facing == 0 ? 1 : -1) * rt_.elfNetDx[static_cast<size_t>(k < 9 ? k : 8)]);
            e.y = rt_.elfNetY[static_cast<size_t>(k < 9 ? k : 8)];
            if (e.hasItem && k == 7) caughtElf = static_cast<uint8_t>(i + 1);
        }
    }
    // Mode 2 (draw) is done by drawAll, which knows g_netStep.
}

// 1178:0565 Elf_HandleRoomEdges
void ElfSystem::handleRoomEdges(int i, const GameState& s, mischief::BorlandRand& rng) {
    Elf& e = elves_[static_cast<size_t>(i)];
    const int r0 = e.room;
    if (!RoomTable::valid(r0)) return;
    const RoomDef& r = s.rooms[r0];
    bool moved = false;
    if (e.x + kEdge >= 512) {
        if (r.exitRight == 0xFF || confined) {
            e.facing = 1;
        } else {
            moved = true;
            e.room = r.exitRight;
            e.x = i16(e.x - 512);
        }
    } else if (e.x < -kEdge) {
        if (r.exitLeft == 0xFF || confined) {
            e.facing = 0;
        } else {
            moved = true;
            e.room = r.exitLeft;
            e.x = i16(e.x + 512);
        }
    }
    // Side path: after a room change away from the player, maybe take r0's door (same level,
    // not the treasure hall), with probability 1/3.
    int doorZone = -1;
    if (r.exitDoor != 0xFF && RoomTable::valid(r.exitDoor)) doorZone = s.rooms[r.exitDoor].music;
    if (moved && s.player.room != r0 && doorZone != -1 && r.level == s.rooms[r.exitDoor].level && doorZone != 0) {
        if (rng.random(3) == 0) {
            e.room = r.exitDoor;
            if (s.rooms[e.room].exitLeft == r0) {
                e.x = kDoorEntryLeftX;
                e.facing = 0;
            } else {
                e.x = kDoorEntryRightX;
                e.facing = 1;
            }
        }
    }
    // The room-36 fountain (tested against the room the elf was in at entry).
    if (r0 == kFountainRoom && e.x + kFountainFootW > kFountainLeft && e.x < kFountainRight) {
        if (e.facing == 0) {
            e.x = kFountainTurnLeftX;
            e.facing = 1;
        } else {
            e.x = kFountainTurnRightX;
            e.facing = 0;
        }
    }
}

// 1178:0766 Elf_RunScript
void ElfSystem::runScript(int mode, int i, const ElfScript& sc, ElfTick* t, gfx::FrameBuffer* fb, int playerRoom,
                          GameHooks* hooks) {
    Elf& e = elves_[static_cast<size_t>(i)];
    if (mode == 0) {
        if (playerRoom == e.room) restoreFromLayer(*fb, e.x, e.y, kElfW, kElfH, kLayer1);
        return;
    }
    if (mode == 1) {
        GameState& s = t->s;
        int st = e.step;
        e.step = i16(e.step + 1);
        const int8_t f = st >= 0 && static_cast<size_t>(st) < sc.frames.size() ? sc.frames[static_cast<size_t>(st)] : int8_t{-1};
        if (f == -1) {
            if (e.set == 1) {
                if (e.room == s.player.room && ((e.facing != 0 && e.x > kThrowMiddleRight) ||
                                                (e.facing == 0 && e.x < kThrowMiddleLeft))) {
                    if (throwState == 0 && t->rng.random(10) < 8) {
                        e.set = kElfSetThrow;
                        e.behaviour = sets_[kElfSetThrow].behaviour;
                        throwState = 1;
                    }
                } else if (t->rng.random(30) == 0) {
                    e.facing ^= 1;
                }
            } else {
                if (t->rng.random(20) == 0) resetBehaviour(i);
                if (t->rng.random(30) == 0) e.facing ^= 1;
            }
            st = 0;
            e.step = 1;
        } else if (f == -2) {
            // With both exits walls the original moves the elf to room 0xFF and a later flee reads
            // rooms[255]; the elf stays instead (REVIEW_1 M-2; no shipped room an elf flees from
            // has two walls).
            if (RoomTable::valid(e.room)) {
                const RoomDef& r = s.rooms[e.room];
                const uint8_t to = ((e.facing == 0 || r.exitLeft == 0xFF) && r.exitRight != 0xFF) ? r.exitRight : r.exitLeft;
                if (RoomTable::valid(to)) e.room = to;
            }
            resetBehaviour(i);
            st = 0;
        }
        const size_t k = static_cast<size_t>(st);
        if (k < sc.dx.size()) {
            e.x = i16(e.x + (e.facing == 0 ? 1 : -1) * sc.dx[k]);
            e.y = sc.y[k];
        }
        handleRoomEdges(i, s, t->rng);
        return;
    }
    if (mode == 2 && playerRoom == e.room) {
        const size_t k = static_cast<size_t>(e.step - 1);
        if (k >= sc.frames.size()) return;
        const int f = sc.frames[k];
        const bool flip = (e.behaviour == 4 && (f == 5 || f == 6)) ? false : e.facing != 0;
        if (const gfx::PixMap* pm = setFrame(e.set, f)) fb->drawSprite(e.x, e.y + drawYOff, *pm, flip);
        if (hooks && k < sc.sfx.size() && sc.sfx[k] >= 0) hooks->playSfx(sc.sfx[k]);
    }
}

// 1178:0cb0 Elf_Beh5_Throw
void ElfSystem::throwHandler(int mode, int i, ElfTick* t, gfx::FrameBuffer* fb, int playerRoom) {
    Elf& e = elves_[static_cast<size_t>(i)];
    if (mode == 0) {
        if (e.room != playerRoom) return;
        restoreFromLayer(*fb, e.x - kThrowEraseDx, e.y - kThrowEraseDy, kThrowEraseW, kThrowEraseH, kLayer1);
        if (throwState < 5) return;
        const int pf = projFrame >= 0 && projFrame < 9 ? projFrame : 0;
        restoreFromLayer(*fb, projX, rt_.elfProjY[static_cast<size_t>(pf)], kProjW, kProjH, kLayer1);
        return;
    }
    if (mode == 2) {
        if (e.room != playerRoom) return;
        const size_t st = static_cast<size_t>(e.step >= 0 && e.step < 5 ? e.step : 0);
        const int x = e.facing == 0 ? e.x + rt_.elfThrowDxRight[st] : e.x - rt_.elfThrowDxLeft[st];
        if (const gfx::PixMap* pm = setFrame(kElfSetThrow, static_cast<int>(st)))
            fb->drawSprite(x, rt_.elfThrowY[st], *pm, e.facing != 0);
        // Neither draw adds elf_drawYOff (code_1168-1198.md §4.7).
        if (throwState < 6 || throwState > 13) return;
        const int pf = projFrame >= 0 && projFrame < 9 ? projFrame : 0;
        if (const gfx::PixMap* pm = projectileFrame(projFrame))
            fb->drawSprite(projX, rt_.elfProjY[static_cast<size_t>(pf)], *pm, e.facing != 0);
        return;
    }
    if (mode != 1) return;
    GameState& s = t->s;
    if (e.room != s.player.room) throwState = 13;
    switch (throwState) {
        case 0: return;
        case 1:
        case 2:
            e.step = 0;
            e.y = rt_.elfThrowY[0];
            break;
        case 3:
        case 4: e.step = 1; break;
        case 5:
            e.step = 2;
            projFrame = 0;
            projX = i16(e.facing == 0 ? e.x + 30 : e.x - 6);
            t->hooks.playSfx(kSfxThrow);
            break;
        case 6:
            e.step = 3;
            projFrame = 1;
            if (e.facing != 0) projX = i16(projX - 7);
            break;
        case 7:
            e.step = 2;
            projFrame = 2;
            break;
        case 8:
            e.step = 3;
            projFrame = 3;
            if (e.facing != 0) projX = i16(projX - 41);
            projSteps = 0;
            break;
        case 9: {
            e.step = i16((projSteps & 1) + 2);
            projX = i16(projX + (e.facing == 0 ? kProjStep : -kProjStep));
            ++projSteps;
            if (projSteps > kProjMaxSteps) {
                throwState = 13;
                return;
            }
            const PlayerState& pl = s.player;
            if (t->p.jumping || s.inv.coins == 0) return;
            if (projX + kProjW <= pl.x || pl.x + pl.w <= projX) return;
            throwState = 10;
            addCoins(s, -1, t->hooks);
            t->hooks.playSfx(kSfxHit);
            return;
        }
        case 10:
            e.step = i16((projSteps & 1) + 2);
            projFrame = i16((projSteps & 3) + 4);
            projX = i16(projX + (e.facing == 0 ? -kProjStep : kProjStep));
            --projSteps;
            if (projSteps > 0) return;
            throwState = 11;
            return;
        case 11:
        case 12:
            e.step = 4;
            projFrame = 8;
            projX = i16(e.facing == 0 ? e.x + 22 : e.x);
            break;
        case 13:
            resetBehaviour(i);
            e.step = 1;
            throwState = 0;
            return;
        default: return;
    }
    ++throwState;
}

void ElfSystem::dispatch(int mode, int i, ElfTick* t, gfx::FrameBuffer* fb, int playerRoom, GameHooks* hooks) {
    const int b = elves_[static_cast<size_t>(i)].behaviour;
    if (b >= 0 && b <= 4)
        runScript(mode, i, rt_.elfScripts[static_cast<size_t>(b)], t, fb, playerRoom, hooks);
    else if (b == 5)
        throwHandler(mode, i, t, fb, playerRoom);
}

// 1178:0453 Elf_EraseAll
void ElfSystem::eraseAll(gfx::FrameBuffer& fb, int playerRoom) {
    for (int i = 0; i < count_; ++i) {
        if (elves_[static_cast<size_t>(i)].inNet == 0)
            dispatch(0, i, nullptr, &fb, playerRoom, nullptr);
        else
            caughtInNet(0, i, nullptr, &fb, playerRoom);
    }
}

// 1178:048d Elf_UpdateAll
void ElfSystem::updateAll(ElfTick& t) {
    for (int i = 0; i < count_; ++i) {
        Elf& e = elves_[static_cast<size_t>(i)];
        checkNetCatch(i, t);
        if (e.inNet != 0) caughtInNet(1, i, &t, nullptr, t.s.player.room);
        if (e.inNet == 0) dispatch(1, i, &t, nullptr, t.s.player.room, &t.hooks);
        if (t.p.gateWalkStarted && e.room == t.s.player.room) {
            e.set = kElfSetFlee;
            e.behaviour = sets_[kElfSetFlee].behaviour;
            e.step = 1;
        }
    }
    if (t.p.gateWalkStarted) confined = 1;
}

// 1178:051f Elf_DrawAll
void ElfSystem::drawAll(gfx::FrameBuffer& fb, int playerRoom, int yoff, int netStep, GameHooks& hooks) {
    drawYOff = i16(yoff);
    for (int i = 0; i < count_; ++i) {
        const Elf& e = elves_[static_cast<size_t>(i)];
        if (e.inNet == 0) {
            dispatch(2, i, nullptr, &fb, playerRoom, &hooks);
        } else if (playerRoom == e.room) {
            // 1178:0b1d mode 2: set 6 frame elf_netFrames[g_netStep], no elf_drawYOff.
            const int8_t f = netStep >= 0 && netStep < 10 ? rt_.elfNetFrames[static_cast<size_t>(netStep)] : int8_t{0};
            if (const gfx::PixMap* pm = setFrame(kElfSetNet, f)) fb.drawSprite(e.x, e.y, *pm, e.facing != 0);
        }
    }
}

}  // namespace tmport::game::room
