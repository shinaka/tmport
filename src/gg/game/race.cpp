// The races (race.h): states 10 and 11, the land and alternative-energy race (ovl24 3dd6), the aircraft
// race (ovl25 3e77) and Math_DivRound (ovl26 3f50). Owner: vehicles agent.
#include "gg/game/race.h"

#include <algorithm>
#include <format>
#include <tuple>

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/game.h"
#include "gg/game/hud.h"
#include "gg/game/parts.h"
#include "gg/game/vehicles_common.h"
#include "gg/game/workshop.h"
#include "gg/gfx/lpx.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

using vehicles::cachedPictureBytes;
using vehicles::sizeFromPicture;
using vehicles::snd;

constexpr int kRaceFramePeriod = 8;          // Timer_Set(0, 8): 18.21 frames per second
constexpr uint16_t kLandEffectBank = 0xF4;   // + g_fmPresent: 0xF5 on FM
constexpr uint16_t kRacePalette = 0xF6;      // colours 32..63: the race paint ramps
constexpr uint16_t kAirPalette = 0x245;
constexpr uint16_t kAirMusicBank = 0x262;
constexpr uint16_t kAirEffectBank = 0x260;
constexpr uint16_t kAirSceneryBundle = 0x248;
constexpr uint16_t kAirScenery = 0x264;      // W 0x264 + level
constexpr uint16_t kAirGroundFar = 0x247;    // opaque at (0, 131)
constexpr uint16_t kAirGroundNear = 0x246;   // keyed at (0, 111)
constexpr uint16_t kAirLaunchBundle = 0x24F; // level 1: the tow plane and its rope
constexpr uint16_t kAirLaunchPath1 = 0x252;
constexpr uint16_t kAirLaunchPath2 = 0x253;
constexpr int kAirLaunchFrames = 58;         // 0x3a
constexpr uint8_t kAirSky = 0xE0;
constexpr int kAirStartDelay = 146;          // Delay(0x92) after the first frame's fade-in
constexpr uint32_t kPointTwo = 0x3333;       // 0.2: the slowing of a limited section
constexpr uint32_t kRace0Acc = 0x4CCD;       // 0.3

// The paint of the player's vehicle frames: colours 0xC1..0xC5 to the race ramp of the vehicle colour
// (Race_LandEnergy 3dd6:07f6, Race_Aircraft 3e77:0b29).
constexpr uint8_t kPaintSourceFirst = 0xC1;

int32_t s32(uint32_t v) { return static_cast<int32_t>(v); }

// The paint lists of the race's vehicle frames: from = 0xC1..0xC5, to = 0x20 + 5 colour + t.
void paintRaceFrames(const std::vector<std::span<const uint8_t>>& frames, int colour) {
    std::array<uint8_t, 6> from{}, to{};
    for (int t = 0; t < 5; ++t) {
        from[static_cast<size_t>(t)] = static_cast<uint8_t>(kPaintSourceFirst + t);
        to[static_cast<size_t>(t)] = static_cast<uint8_t>(colour * 5 + t + 0x20);
    }
    from[5] = to[5] = 0xFF;
    for (const auto& pic : frames) recolorPicture(cachedPictureBytes(pic), from, to);
}

// A path record, guarded (the original reads on past a path's end; no shipped path gets there).
const RacePathPoint& pathAt(std::span<const RacePathPoint> path, int index) {
    static const RacePathPoint kEnd{0, 0, -1};
    if (index < 0 || static_cast<size_t>(index) >= path.size()) {
        log::debug("race path: index {} outside its {} records", index, path.size());
        return kEnd;
    }
    return path[static_cast<size_t>(index)];
}

// A frame picture of a bundle (a path code, plus the aircraft's animation step), guarded.
std::span<const uint8_t> frameAt(const std::vector<std::span<const uint8_t>>* frames, int frame) {
    if (!frames || frame < 0 || static_cast<size_t>(frame) >= frames->size()) {
        if (frames) log::debug("race: frame {} outside its bundle of {}", frame, frames->size());
        return {};
    }
    return (*frames)[static_cast<size_t>(frame)];
}

// The part list of a race (g_playerVehicle DS:6616 or Morty's list) up to its -1.
std::vector<int16_t> partList(std::span<const int16_t> list) {
    std::vector<int16_t> out;
    for (const int16_t p : list) {
        if (p == -1) break;
        out.push_back(p);
    }
    return out;
}

// A u32 of a 16.16 table: DS:table + 4 variant.
uint32_t tableAt(const GgExe& exe, uint16_t table, int variant) {
    return exe.ds32(static_cast<uint16_t>(table + 4 * variant));
}

const PartRecord* partOf(const PartCatalogue& parts, int index) {
    if (index < 0 || static_cast<size_t>(index) >= parts.count()) {
        log::debug("race performance: part {} outside the catalogue of {}", index, parts.count());
        return nullptr;
    }
    return &parts.parts[static_cast<size_t>(index)];
}

// The sprite queue entry of a racer's sprite record and picture.
Sprite spriteOf(const SpriteRec& r, std::span<const uint8_t> pic) {
    return Sprite{r.flags, r.x, r.y, r.w, r.h, pic};
}

// The land racers' start of Race_Run (3dd6:01d2): ids, finished, speed and index 0, the laps.
void landRaceStart(RacerRec& player, RacerRec& morty, int laps) {
    player.id = 1;
    morty.id = 2;
    player.finished = morty.finished = 0;
    player.speed = morty.speed = 0;
    player.index = morty.index = 0;
    player.laps = morty.laps = static_cast<int16_t>(laps);
}

// The racers' advance of one frame: int(speed) points (a signed 16-bit count), stopping at a finish.
void landAdvanceFrame(RacerRec& r, std::span<const RacePathPoint> path, uint16_t& bits, const RacePathSound& sound) {
    if (r.finished) return;
    const int n = static_cast<int16_t>(r.speed >> 16);
    for (int i = 0; i < n; ++i)
        if (raceAdvancePoint(r, path, bits, sound)) break;
}

// Race_Run's result (3dd6:040e): bits 1 won, 2 lost, 3 the faster wins (the player on equal speed);
// the result is also left in g_raceFinishBits.
int landResult(uint16_t& bits, const RacerRec& player, const RacerRec& morty) {
    if (bits == 1) bits = 0;
    else if (bits == 2) bits = 1;
    else if (bits == 3) bits = s32(player.speed) >= s32(morty.speed) ? 0 : 1;
    return bits;
}

void airRaceStart(AirRacerRec& player, AirRacerRec& morty, std::span<const RacePathPoint> p1, std::span<const RacePathPoint> p2) {
    player.speed = morty.speed = 0;
    player.index = morty.index = 0;
    player.finished = morty.finished = 0;
    // The distance starts at the first record's x (3e77:062b: the high word, the low word 0).
    player.dist = static_cast<uint32_t>(static_cast<uint16_t>(pathAt(p1, 0).x)) << 16;
    morty.dist = static_cast<uint32_t>(static_cast<uint16_t>(pathAt(p2, 0).x)) << 16;
}

// The aircraft racers' step and advance of one frame (3e77:069e..0748).
void airAdvanceFrame(AirRacerRec& r, std::span<const RacePathPoint> path, int16_t& pathEnd, const RacePathSound& sound) {
    if (r.finished) return;
    const int n = static_cast<int16_t>(static_cast<int16_t>(r.dist >> 16) - static_cast<int16_t>(r.prevDist >> 16));
    for (int i = 0; i < n; ++i)
        if (airRaceAdvancePoint(r, path, pathEnd, sound)) break;
}

void airStepAndMove(AirRacerRec& r) {
    airRaceStepSpeed(r);
    r.prevDist = r.dist;
    r.dist += r.speed;
}

}  // namespace

std::vector<RacePathPoint> parseRacePath(std::span<const uint8_t> payload) {
    std::vector<RacePathPoint> path;
    for (size_t o = 0; o + 6 <= payload.size(); o += 6) {
        const RacePathPoint p{static_cast<int16_t>(payload[o] | (payload[o + 1] << 8)),
                              static_cast<int16_t>(payload[o + 2] | (payload[o + 3] << 8)),
                              static_cast<int16_t>(payload[o + 4] | (payload[o + 5] << 8))};
        path.push_back(p);
        if (p.code == -1) break;
    }
    if (path.empty()) throw FormatError("race path: no records");
    return path;
}

// ---- states 10 and 11 -----------------------------------------------------------------------------

// 12e0:0506 State_10_ListVehicleParts
int16_t state10ListVehicleParts(GgGame& g) {
    auto& list = g.vehicles.playerVehicle;
    const auto& parts = g.world.parts.parts;
    size_t k = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].holder != 0 || !(parts[i].flags & PartRecord::kInstalled)) continue;
        if (k + 1 >= list.size()) {  // the original writes on into Morty's pointer DS:6634
            log::warn("State_10_ListVehicleParts: more than {} parts built in", list.size() - 1);
            break;
        }
        list[k++] = static_cast<int16_t>(i);
    }
    list[k] = -1;
    g.vehicles.rematch = 0;
    return 0xB;
}

// 12e0:0564 State_11_Race
int16_t state11Race(GgGame& g, StateEntry) {
    g.video.palette().save();      // Pal_Save
    g.video.cursor().hide();       // Mouse_Hide 17ab:0890
    g.input().keyHeldSeen = 1;     // g_keyHeldSeen: the HUD's labels come back
    hudUpdate(g, 0);
    const int b = g.building();
    if (b == 1) g.vehicles.raceResult = static_cast<int16_t>(raceAircraft(g));
    else if (b == 0 || b == 2) g.vehicles.raceResult = static_cast<int16_t>(raceLandEnergy(g));
    if (g.vehicles.rematch == 0 && g.vehicles.raceResult == 0 && b >= 0 && b < 3) {
        ++g.player.racesWon[static_cast<size_t>(b)];
        ++g.player.racesWonTotal;
    }
    g.video.cursor().show();       // Mouse_Show 17ab:0885
    return g.vehicles.rematch == 0 ? int16_t{0xC} : int16_t{0xE};
}

// ---- ovl26 ------------------------------------------------------------------------------------------

// 3f50:0000 Math_DivRound
int32_t mathDivRound(int32_t a, int32_t b) {
    bool neg = false;
    uint32_t ua = static_cast<uint32_t>(a), ub = static_cast<uint32_t>(b);
    if (a < 0) {
        ua = 0u - ua;
        neg = !neg;
    }
    if (b < 0) {
        ub = 0u - ub;
        neg = !neg;
    }
    const uint32_t d = ub >> 16;  // the high word only
    if (d == 0) {
        log::warn("Math_DivRound: division by a zero high word (the original faults)");
        return 0;
    }
    uint32_t q = ua / d;
    const uint32_t r = ua % d;
    const uint32_t halfMinus1 = ((d >> 1) + (d & 1)) - 1;  // SHR, ADC 0, DEC
    if (halfMinus1 < r) ++q;
    return neg ? -static_cast<int32_t>(q) : static_cast<int32_t>(q);
}

// ---- ovl24, the land race ---------------------------------------------------------------------------

// 3dd6:044e Race_ComputePerformance (the category switch CS:0716, the race switch CS:06f8)
LandPerformance landPerformance(const GgExe& exe, const PartCatalogue& parts, int raceIndex, std::span<const int16_t> list,
                                int16_t initialB, int16_t initialA) {
    const uint32_t base = (raceIndex >= 0 && raceIndex < 15) ? exe.ds32(static_cast<uint16_t>(dg::kLandBaseSpeed + 4 * raceIndex)) : 0;
    uint32_t max = base, cap = base, acc = 0, acc2 = 0, gear = 0, power = 0;
    int16_t a = initialA, b = initialB;
    for (const int16_t index : partList(list)) {
        const PartRecord* p = partOf(parts, index);
        if (!p) continue;
        const int v = p->variant;
        switch (p->category) {
        case 0: max -= tableAt(exe, 0x3A64, v); break;
        case 1:
        case 53: max -= tableAt(exe, 0x3A84, v); break;
        case 3: cap = tableAt(exe, 0x3A54, v); break;
        case 4:
        case 52: acc += tableAt(exe, 0x3AC4, v); break;
        case 5: gear = tableAt(exe, 0x3AD4, v); break;
        case 6:
        case 57: a = static_cast<int16_t>(v); break;
        case 7:
        case 56:
        case 58: b = static_cast<int16_t>(v); break;
        case 8: max -= tableAt(exe, 0x3AE4, v); break;
        case 9:
            max -= tableAt(exe, 0x3AB4, v);
            power = tableAt(exe, 0x3A94, v);
            break;
        case 48: acc += tableAt(exe, 0x3AF4, v); break;
        case 50: max -= tableAt(exe, 0x3B04, v); break;
        case 51:
            acc += tableAt(exe, 0x3B14, v);
            max -= tableAt(exe, 0x3B24, v);
            break;
        case 55:
            acc += tableAt(exe, 0x3A74, v);
            max -= tableAt(exe, 0x3A64, v);
            break;
        default: break;  // 2, 10, 11, 12, 54, 59 and the others: nothing
        }
    }
    switch (raceIndex) {
    case 0: acc = kRace0Acc; break;  // replaces any sum; acc2 stays 0
    case 2:
    case 4:
        cap = max;
        [[fallthrough]];
    case 1:
    case 3:
        acc += static_cast<uint32_t>(mathDivRound(s32(power), s32(gear)));
        acc2 = acc;
        break;
    case 10: acc2 = acc; break;
    case 11:
    case 12:
    case 13:
    case 14: {
        // power = DS:3aa4[min(A, B)] (signed); race 14 has no A part (the uninitialised local: B).
        const int idx = std::min(a, b);
        power = tableAt(exe, 0x3AA4, idx);
        if (gear != 0) acc += static_cast<uint32_t>(mathDivRound(s32(power), s32(gear)));
        acc2 = acc;
        break;
    }
    default: break;  // 5..9 (aircraft): never reached
    }
    LandPerformance out;
    out.max = max;
    out.limit = s32(cap) > s32(max) ? max : cap;
    out.acc = acc;
    out.acc2 = acc2;
    return out;
}

// 3dd6:044e Race_ComputePerformance
void raceComputePerformance(GgGame& g, RacerRec& r, std::span<const int16_t> parts) {
    const auto* frames = g.vehiclesObjects().playerFrames;
    const auto b = static_cast<int16_t>(frames ? frames->size() : 0);  // the caller's DI (3dd6:0864)
    const LandPerformance p = landPerformance(g.exe(), g.world.parts, g.raceIndex(), parts, b);
    r.acc2 = p.acc2;
    r.acc = p.acc;
    r.max = p.max;
    r.limit = p.limit;
    r.mode = 0;
}

// 3dd6:0000 Race_MapPathFrames
void raceMapPathFrames(GgGame& g, std::vector<RacePathPoint>& path, uint16_t bundle) {
    const auto* pics = g.cache.pictures(bundle);
    const size_t n = pics ? pics->size() : 0;
    for (const RacePathPoint& p : path) {
        if (p.code == -1) break;
        if (p.code >= 0 && static_cast<size_t>(p.code) >= n)
            log::debug("Race_MapPathFrames: frame {} outside bundle {:04x} ({} pictures)", p.code, bundle, n);
    }
}

// 3dd6:0034 Race_StepSpeed
void raceStepSpeed(RacerRec& r) {
    if (r.mode == 0) {
        r.speed += r.acc;
        if (s32(r.speed) > s32(r.max)) r.speed = r.max;
        return;
    }
    if (s32(r.speed) > s32(r.limit)) {
        r.speed -= kPointTwo;
        return;
    }
    r.speed += r.acc2;
    if (s32(r.speed) > s32(r.limit)) r.speed = r.limit;
}

// 3dd6:00e0 Race_AdvancePoint (table CS:01b0, code + 14)
int raceAdvancePoint(RacerRec& r, std::span<const RacePathPoint> path, uint16_t& finishBits, const RacePathSound& sound) {
    int crossed = 0;
    for (int guard = 0; guard < 0x10000; ++guard) {
        ++r.index;
        const RacePathPoint& p = pathAt(path, r.index);
        switch (p.code) {
        case -14:
        case -13:
            if (sound) sound(p.code, p.x, p.y);
            continue;
        case -5:  // a lap: back to the start (also with no lap left: never reached so)
            if (r.laps != 0) --r.laps;
            r.index = 0;
            continue;
        case -4: r.mode = 0; continue;
        case -3: r.mode = 1; continue;
        case -2:
            if (r.laps == 0) {
                r.finished = 1;
                finishBits |= r.id;
                crossed = 1;
            }
            continue;
        default: return crossed;  // a point (and -12..-6, -1)
        }
    }
    log::warn("Race_AdvancePoint: no point found");
    return crossed;
}

// 3dd6:01ca Race_Run
int raceRun(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    const GgExe& exe = g.exe();
    const int b = g.building();
    landRaceStart(v.racePlayer, v.raceMorty, exe.ds16(static_cast<uint16_t>(dg::kRaceExtraLaps + 2 * g.raceIndex())));
    v.raceFinishBits = 0;
    const int music = snd(g).raceMusic;
    const int effects = snd(g).raceEffects;
    // The music intro: no frames, no input until the song has played 300 (200) sound ticks.
    const auto intro = exe.ds16(static_cast<uint16_t>(dg::kRaceIntroTicks + 2 * b));
    while (static_cast<uint16_t>(g.sound.musicTick(music)) < intro) g.rt.idle();
    if (b == 0 && g.level() > 0) {  // Automotive from level 1: the start effects, then wait for effect 0
        g.sound.playEffect(effects, 0, 10);
        g.sound.playEffect(effects + 1, 1, 10);
        while (g.sound.isPlaying(effects)) g.rt.idle();  // a speaker hole is never playing (SndRes_GetSlotB)
    }
    const RacePathSound sound = [&g, effects](int code, int x, int) {
        if (code == -13) g.sound.playEffect(effects + x, 0, 10);
        else if (code == -14) g.sound.stopEffect(effects + x);
    };
    Screen& screen = g.video.screen();
    SpriteList& sprites = g.video.sprites();
    int arrangement = -1, last = -1;
    while (!v.racePlayer.finished && !v.raceMorty.finished) {
        g.timers().set(0, kRaceFramePeriod);
        if (arrangement != last) {  // from frame 2 on: who leads chooses the music
            g.sound.selectArrangement(music, arrangement);
            last = arrangement;
        }
        raceStepSpeed(v.racePlayer);
        raceStepSpeed(v.raceMorty);
        sprites.eraseAll(screen);
        landAdvanceFrame(v.racePlayer, o.playerPath, v.raceFinishBits, sound);
        landAdvanceFrame(v.raceMorty, o.mortyPath, v.raceFinishBits, sound);
        // Morty first, the player on top.
        for (auto [r, path, frames, pic] : {std::tuple{&v.raceMorty, &o.mortyPath, o.mortyFrames, &o.mortyPic},
                                            std::tuple{&v.racePlayer, &o.playerPath, o.playerFrames, &o.playerPic}}) {
            const RacePathPoint& p = pathAt(*path, r->index);
            r->sprite.x = p.x;
            r->sprite.y = p.y;
            *pic = frameAt(frames, p.code);
            sizeFromPicture(*pic, r->sprite.w, r->sprite.h);
            sprites.add(spriteOf(r->sprite, *pic));
        }
        sprites.drawAll(screen);
        arrangement = v.racePlayer.index >= v.raceMorty.index ? 2 : 3;
        g.display.flush();
        g.keyboard().popQueuedKey();  // 198d:00d4: one typed key thrown away per frame
        g.rt.timerWait(0);
    }
    g.sound.stopAllEffects();
    return landResult(v.raceFinishBits, v.racePlayer, v.raceMorty);
}

// 3dd6:0762 Race_LandEnergy
int raceLandEnergy(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    const GgExe& exe = g.exe();
    const Archive& arc = g.archive();
    const int b = g.building();
    const int r = g.raceIndex();
    SoundState& s = snd(g);
    s.raceMusic = static_cast<int16_t>(g.sound.loadMusicBank(exe.ds16(static_cast<uint16_t>(dg::kRaceMusicBanks + 2 * b))));
    g.sound.selectArrangement(s.raceMusic, 1);
    g.sound.playMusic(s.raceMusic, 0, 1);
    s.raceEffects = static_cast<int16_t>(g.sound.loadEffectBank(kLandEffectBank));
    // The course: picture DS:399a[r] with its palette (+1), the race palette.
    const auto background = exe.ds16(static_cast<uint16_t>(dg::kRaceBackgrounds + 2 * r));
    std::vector<uint8_t> picture;
    g.display.runLoad(g.video.loadPicture(arc, background, background + 1, picture));  // Screen_LoadPicture
    Screen& screen = g.video.screen();
    screen.lpxDraw(0, 0, picture);
    screen.savePlayArea();
    g.video.palette().store(parsePaletteRange(arc.read(kRacePalette)));  // Pal_LoadRes(0xf6)
    o.playerPath = parseRacePath(arc.read(exe.ds16(static_cast<uint16_t>(dg::kRacePaths1 + 2 * r))));
    o.mortyPath = parseRacePath(arc.read(exe.ds16(static_cast<uint16_t>(dg::kRacePaths2 + 2 * r))));
    // The player's frames in the vehicle's colour; Morty's keep theirs.
    const auto sprites1 = exe.ds16(static_cast<uint16_t>(dg::kRaceSprites1 + 2 * r));
    const auto sprites2 = exe.ds16(static_cast<uint16_t>(dg::kRaceSprites2 + 2 * r));
    o.playerFrames = &g.cache.loadBundle(sprites1);
    paintRaceFrames(*o.playerFrames, g.player.vehicleColor[static_cast<size_t>(r)]);
    raceMapPathFrames(g, o.playerPath, sprites1);
    o.mortyFrames = &g.cache.loadBundle(sprites2);
    raceMapPathFrames(g, o.mortyPath, sprites2);
    raceComputePerformance(g, v.racePlayer, v.playerVehicle);
    raceComputePerformance(g, v.raceMorty, g.world.parts.morty);
    SpriteList& sprites = g.video.sprites();
    sprites.reset();
    for (auto [rec, path, frames, pic] : {std::tuple{&v.raceMorty, &o.mortyPath, o.mortyFrames, &o.mortyPic},
                                          std::tuple{&v.racePlayer, &o.playerPath, o.playerFrames, &o.playerPic}}) {
        const RacePathPoint& p = pathAt(*path, 0);
        rec->sprite.flags = 0;
        rec->sprite.x = p.x;
        rec->sprite.y = p.y;
        *pic = frameAt(frames, p.code);
        sizeFromPicture(*pic, rec->sprite.w, rec->sprite.h);
        sprites.add(spriteOf(rec->sprite, *pic));
    }
    sprites.drawAll(screen);
    g.display.fadeOutIn();
    const int result = raceRun(g);
    g.cache.releaseBundle(sprites1);
    g.cache.releaseBundle(sprites2);
    o.playerFrames = o.mortyFrames = nullptr;
    o.playerPic = o.mortyPic = {};
    o.playerPath.clear();
    o.mortyPath.clear();
    g.display.fadeOut();
    g.sound.freeMusicBank(s.raceMusic);
    g.sound.freeEffectBank(s.raceEffects);
    return result;
}

// ---- ovl25, the aircraft race -----------------------------------------------------------------------

// 3e77:0036 AirRace_ComputePerformance (category switch CS:0381, level switch CS:0377)
AirPerformance airPerformance(const GgExe& exe, const PartCatalogue& parts, int level, std::span<const int16_t> list) {
    // The table of each category from 13 on (0: nothing) and what it does.
    enum Op : uint8_t { kNone, kSpeed, kAcc, kAccSet };
    struct Rule {
        uint16_t table;
        Op op;
    };
    static constexpr Rule kRules[] = {
        {0x3BAA, kSpeed}, {0x3BB6, kSpeed}, {0x3BC6, kAccSet}, {0x3BCE, kAcc},   // 13..16
        {0x3BDE, kAcc},   {0x3BEE, kAcc},   {0x3BFA, kAcc},    {0x3C0A, kAcc},   // 17..20
        {0x3C1A, kAcc},   {0x3C2A, kAcc},   {0x3C3A, kAcc},    {0x3C4A, kSpeed}, // 21..24
        {0x3C4A, kSpeed}, {0x3C6A, kSpeed}, {0x3C7A, kSpeed},  {0x3C8A, kSpeed}, // 25..28 (24 and 25 share)
        {0x3C9A, kSpeed}, {0x3D2A, kSpeed}, {0x3D3A, kSpeed},  {0x3D4A, kAcc},   // 29..32
        {0x3D5A, kSpeed}, {0, kNone},       {0, kNone},        {0x3D6A, kSpeed}, // 33..36
        {0x3D7A, kSpeed}, {0x3D8A, kSpeed}, {0x3D9A, kSpeed},  {0x3CAA, kSpeed}, // 37..40
        {0x3CBA, kSpeed}, {0x3CCA, kSpeed}, {0x3CDA, kSpeed},  {0x3CEA, kAcc},   // 41..44
        {0x3CFA, kAcc},   {0x3D0A, kSpeed}, {0x3D1A, kSpeed},                    // 45..47
    };
    uint32_t speed0 = (level >= 0 && level < 5) ? exe.ds32(static_cast<uint16_t>(dg::kAirBaseSpeed + 4 * level)) : 0;
    uint32_t acc0 = 0;
    for (const int16_t index : partList(list)) {
        const PartRecord* p = partOf(parts, index);
        if (!p) continue;
        const int k = p->category - 13;
        if (k < 0 || static_cast<size_t>(k) >= std::size(kRules)) continue;
        const Rule& rule = kRules[static_cast<size_t>(k)];
        switch (rule.op) {
        case kSpeed: speed0 -= tableAt(exe, rule.table, p->variant); break;
        case kAcc: acc0 += tableAt(exe, rule.table, p->variant); break;
        case kAccSet: acc0 = tableAt(exe, rule.table, p->variant); break;  // replaces the sum
        case kNone: break;
        }
    }
    AirPerformance out;
    if (level == 1) {  // the glider: towed up at 0.3 per frame to 5.0, then down by the drag to 1.5
        out.speeds = {0x18000, 0x50000, 0x18000};
        const uint32_t neg = 0u - acc0;
        out.accs = {neg, 0x4CCD, neg};
    } else if (level >= 0 && level <= 4) {
        out.speeds = {speed0, speed0, speed0};
        out.accs = {acc0, acc0, acc0};
    }
    return out;
}

// 3e77:0036 AirRace_ComputePerformance
void airRaceComputePerformance(GgGame& g, AirRacerRec& r, std::span<const int16_t> parts) {
    const AirPerformance p = airPerformance(g.exe(), g.world.parts, g.level(), parts);
    if (g.level() > 4) return;  // the level switch has no case: nothing stored
    r.speeds = p.speeds;
    r.accs = p.accs;
}

// 3e77:0000 AirRace_MapScenery
void airRaceMapScenery(GgGame& g, uint16_t bundle) {
    const VehiclesObjects& o = g.vehiclesObjects();
    const auto* pics = g.cache.pictures(bundle);
    const size_t n = pics ? pics->size() : 0;
    for (const auto* layer : {&o.scenery1, &o.scenery2})
        for (const auto& rec : *layer)
            if (rec.frame >= 0 && static_cast<size_t>(rec.frame) >= n)
                log::debug("AirRace_MapScenery: frame {} outside bundle {:04x}", rec.frame, bundle);
}

// 3e77:03c7 AirRace_DrawScenery
void airRaceDrawScenery(GgGame& g, int layer, uint32_t scroll) {
    VehiclesObjects& o = g.vehiclesObjects();
    const auto& list = layer == 1 ? o.scenery1 : o.scenery2;
    const auto x0 = static_cast<int16_t>(scroll >> 16);  // int(scroll): the high word
    for (const auto& rec : list)
        g.video.screen().lpxDrawKeyed(static_cast<int16_t>(rec.x - x0), rec.y, frameAt(o.sceneryFrames, rec.frame), 0);
}

// 3e77:0428 AirRace_StepSpeed
void airRaceStepSpeed(AirRacerRec& r) {
    const auto m = static_cast<size_t>(std::clamp<int>(r.mode, 0, 2));
    if (m != static_cast<size_t>(r.mode)) log::debug("AirRace_StepSpeed: flight mode {} (the original reads past)", r.mode);
    const uint32_t acc = r.accs[m];
    r.speed += acc;
    if (s32(acc) < 0) {
        if (s32(r.speed) < s32(r.speeds[m])) r.speed = r.speeds[m];
    } else if (s32(r.speed) > s32(r.speeds[m])) {
        r.speed = r.speeds[m];
    }
}

// 3e77:04cc AirRace_AdvancePoint (table CS:05c7, code + 14)
int airRaceAdvancePoint(AirRacerRec& r, std::span<const RacePathPoint> path, int16_t& pathEnd, const RacePathSound& sound) {
    int crossed = 0;
    for (int guard = 0; guard < 0x10000; ++guard) {
        ++r.index;
        const RacePathPoint& p = pathAt(path, r.index);
        switch (p.code) {
        case -14:
        case -13:
            if (sound) sound(p.code, p.x, p.y);
            continue;
        case -12: r.index = static_cast<int16_t>(r.index + p.x - 1); continue;  // circles over the last points
        case -11:
        case -9: r.mode = 0; continue;
        case -10: r.mode = 2; continue;
        case -8: r.mode = 1; continue;
        case -7:
        case -6: continue;
        case -2:
            r.finished = 1;
            crossed = 1;
            continue;
        case -1:
            pathEnd = 1;
            return crossed;
        default: return crossed;  // a point (and -5..-3)
        }
    }
    log::warn("AirRace_AdvancePoint: no point found");
    return crossed;
}

// 3e77:05e3 AirRace_Run
int airRaceRun(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    const GgExe& exe = g.exe();
    const int level = g.level();
    const int music = snd(g).airMusic;
    const int effects = snd(g).airEffects;
    int arrangement = 1, launch = 0, anim = 0;
    bool first = true;
    v.airCamera = 0;
    v.airCamera10 = 0;
    airRaceStart(v.airPlayer, v.airMorty, o.playerPath, o.mortyPath);
    v.airUnusedBb24 = 0;
    v.airPathEnd = 0;
    const int animFrames = std::max<int16_t>(1, exe.dsS16(static_cast<uint16_t>(dg::kAirAnimFrames + 2 * level)));
    const int16_t limitPlayer = exe.dsS16(static_cast<uint16_t>(dg::kAirCameraLimit + 4 * level));
    const int16_t limitMorty = exe.dsS16(static_cast<uint16_t>(dg::kAirCameraLimit + 4 * level + 2));
    const RacePathSound sound = [&g, effects](int code, int x, int y) {
        if (code == -13) {
            if (!g.sound.isPlaying(effects + x)) g.sound.playEffect(effects + x, static_cast<int16_t>(y) >> 8, y & 0xFF);
        } else if (code == -14) {
            g.sound.stopEffect(effects + x);
        }
    };
    Screen& screen = g.video.screen();
    SpriteList& sprites = g.video.sprites();
    sprites.reset();
    while (!v.airPlayer.finished && !v.airMorty.finished) {
        g.timers().set(0, kRaceFramePeriod);
        g.sound.selectArrangement(music, arrangement);  // every frame
        sprites.eraseAll(screen);
        screen.restoreRect(0, 80, 320, 85);  // the low sky band
        airStepAndMove(v.airPlayer);
        airStepAndMove(v.airMorty);
        airAdvanceFrame(v.airPlayer, o.playerPath, v.airPathEnd, sound);
        airAdvanceFrame(v.airMorty, o.mortyPath, v.airPathEnd, sound);
        airRaceDrawScenery(g, 1, v.airCamera10);
        airRaceDrawScenery(g, 2, v.airCamera);
        const auto camera = static_cast<int16_t>(v.airCamera >> 16);
        // The player first, Morty on top; the picture is the frame's plus the propeller step.
        for (auto [r, path, frames, pic] : {std::tuple{&v.airPlayer, &o.playerPath, o.playerFrames, &o.playerPic},
                                            std::tuple{&v.airMorty, &o.mortyPath, o.mortyFrames, &o.mortyPic}}) {
            const RacePathPoint& p = pathAt(*path, r->index);
            r->sprite.x = static_cast<int16_t>(p.x - camera);
            r->sprite.y = p.y;
            *pic = frameAt(frames, p.code + anim);
            sizeFromPicture(*pic, r->sprite.w, r->sprite.h);
            sprites.add(spriteOf(r->sprite, *pic));
        }
        if (level == 1 && launch < kAirLaunchFrames) {  // the glider's tow at the start
            if (launch == 0) g.sound.playEffect(effects + 2, 1, 3);
            for (const auto* path : {&o.launchPath1, &o.launchPath2}) {
                const RacePathPoint& p = pathAt(*path, launch);
                SpriteRec rec;
                rec.x = static_cast<int16_t>(p.x - camera);
                rec.y = p.y;
                const auto pic = frameAt(o.launchFrames, p.code);
                sizeFromPicture(pic, rec.w, rec.h);
                sprites.add(spriteOf(rec, pic));
            }
            if (++launch == kAirLaunchFrames) g.sound.stopEffect(effects + 2);
        }
        sprites.drawAll(screen);
        // Who leads chooses the music; the camera follows the leader past the middle of the screen (the
        // limits compare a screen x and never bind: keep).
        uint32_t delta = 0;
        if (v.airPlayer.index >= v.airMorty.index) {
            arrangement = 2;
            if (v.airPlayer.sprite.x > 160 && limitPlayer > v.airPlayer.sprite.x) delta = v.airPlayer.speed;
        }
        if (v.airMorty.index > v.airPlayer.index) {
            arrangement = 3;
            if (v.airMorty.sprite.x > 160 && limitMorty > v.airMorty.sprite.x) delta = v.airMorty.speed;
        }
        v.airCamera += delta;
        v.airCamera10 = static_cast<uint32_t>(mathDivRound(s32(v.airCamera), 10 << 16));
        if (first) {
            g.display.fadeOutIn();
            g.rt.delay(kAirStartDelay);
            first = false;
        } else {
            g.display.flush();
        }
        anim = (anim + 1) % animFrames;
        g.keyboard().popQueuedKey();  // 198d:00d4: one typed key thrown away per frame
        g.rt.timerWait(0);
    }
    return v.airPlayer.finished ? 0 : 1;
}

// 3e77:0a23 Race_Aircraft
int raceAircraft(GgGame& g) {
    VehiclesObjects& o = g.vehiclesObjects();
    VehiclesState& v = g.vehicles;
    const GgExe& exe = g.exe();
    const Archive& arc = g.archive();
    const int level = g.level();
    Screen& screen = g.video.screen();
    g.video.palette().store(parsePaletteRange(arc.read(kAirPalette)));   // Pal_LoadRes(0x245)
    g.video.palette().store(parsePaletteRange(arc.read(kRacePalette)));  // Pal_LoadRes(0xf6)
    screen.setDrawColor(kAirSky);
    screen.fillRect(0, 9, 320, 155);
    // The scenery W 0x264 + level: u16 c1, c1 records {x, y, frame, slot}; u16 c2, c2 records.
    const auto scenery = arc.read(static_cast<uint16_t>(kAirScenery + level));
    auto word = [&](size_t off) { return off + 2 <= scenery.size() ? static_cast<int16_t>(scenery[off] | (scenery[off + 1] << 8)) : int16_t{-1}; };
    auto readLayer = [&](size_t& off, std::vector<VehiclesObjects::SceneryRec>& out) {
        const int count = word(off);
        off += 2;
        out.clear();
        for (int i = 0; i < count && off + 8 <= scenery.size(); ++i, off += 8) {
            if (word(off + 4) == -1) {  // the end record (counted in c)
                off += 8 * static_cast<size_t>(count - i);
                break;
            }
            out.push_back({word(off), word(off + 2), word(off + 4)});
        }
    };
    size_t off = 0;
    readLayer(off, o.scenery1);
    readLayer(off, o.scenery2);
    screen.lpxDraw(0, 131, arc.read(kAirGroundFar));           // Res_Read, Lpx_Draw
    screen.lpxDrawKeyed(0, 111, arc.read(kAirGroundNear), 0);  // Res_Read, Lpx_DrawKeyed
    screen.savePlayArea();
    SoundState& s = snd(g);
    s.airMusic = static_cast<int16_t>(g.sound.loadMusicBank(kAirMusicBank));
    s.airEffects = static_cast<int16_t>(g.sound.loadEffectBank(kAirEffectBank));
    o.sceneryFrames = &g.cache.loadBundle(kAirSceneryBundle);
    airRaceMapScenery(g, kAirSceneryBundle);
    const auto sprites1 = exe.ds16(static_cast<uint16_t>(dg::kAirSprites1 + 2 * level));
    const auto sprites2 = exe.ds16(static_cast<uint16_t>(dg::kAirSprites2 + 2 * level));
    o.playerPath = parseRacePath(arc.read(exe.ds16(static_cast<uint16_t>(dg::kAirPaths1 + 2 * level))));
    o.playerFrames = &g.cache.loadBundle(sprites1);
    raceMapPathFrames(g, o.playerPath, sprites1);
    paintRaceFrames(*o.playerFrames, g.player.vehicleColor[static_cast<size_t>(g.raceIndex())]);
    o.mortyPath = parseRacePath(arc.read(exe.ds16(static_cast<uint16_t>(dg::kAirPaths2 + 2 * level))));
    o.mortyFrames = &g.cache.loadBundle(sprites2);
    raceMapPathFrames(g, o.mortyPath, sprites2);
    if (level == 1) {
        o.launchFrames = &g.cache.loadBundle(kAirLaunchBundle);
        o.launchPath1 = parseRacePath(arc.read(kAirLaunchPath1));
        o.launchPath2 = parseRacePath(arc.read(kAirLaunchPath2));
        raceMapPathFrames(g, o.launchPath1, kAirLaunchBundle);
        raceMapPathFrames(g, o.launchPath2, kAirLaunchBundle);
    } else {
        o.launchPath1.clear();
        o.launchPath2.clear();
    }
    airRaceComputePerformance(g, v.airPlayer, v.playerVehicle);
    airRaceComputePerformance(g, v.airMorty, g.world.parts.morty);
    g.sound.playMusic(s.airMusic, 0, 1);
    g.sound.selectArrangement(s.airMusic, 1);
    const int result = airRaceRun(g);
    g.display.fadeOut();
    g.sound.stopMusic(s.airMusic);
    g.sound.freeEffectBank(s.airEffects);
    g.sound.freeMusicBank(s.airMusic);
    if (level == 1) {
        o.launchPath1.clear();
        o.launchPath2.clear();
        g.cache.releaseBundle(kAirLaunchBundle);
        o.launchFrames = nullptr;
    }
    g.cache.releaseBundle(kAirSceneryBundle);
    g.cache.releaseBundle(sprites1);
    g.cache.releaseBundle(sprites2);
    o.sceneryFrames = o.playerFrames = o.mortyFrames = nullptr;
    o.playerPic = o.mortyPic = {};
    o.playerPath.clear();
    o.mortyPath.clear();
    o.scenery1.clear();
    o.scenery2.clear();
    return result;
}

// ---- the simulations ---------------------------------------------------------------------------------

RaceSimResult simulateLandRace(const GgExe& exe, const Archive& archive, int raceIndex, const LandPerformance& player,
                               const LandPerformance& morty, int maxFrames) {
    const auto p1 = parseRacePath(archive.read(exe.ds16(static_cast<uint16_t>(dg::kRacePaths1 + 2 * raceIndex))));
    const auto p2 = parseRacePath(archive.read(exe.ds16(static_cast<uint16_t>(dg::kRacePaths2 + 2 * raceIndex))));
    RacerRec a, b;
    for (auto [r, p] : {std::pair{&a, &player}, std::pair{&b, &morty}}) {
        r->max = p->max;
        r->limit = p->limit;
        r->acc = p->acc;
        r->acc2 = p->acc2;
        r->mode = 0;
    }
    landRaceStart(a, b, exe.ds16(static_cast<uint16_t>(dg::kRaceExtraLaps + 2 * raceIndex)));
    uint16_t bits = 0;
    RaceSimResult out;
    for (int f = 1; f <= maxFrames; ++f) {
        raceStepSpeed(a);
        raceStepSpeed(b);
        landAdvanceFrame(a, p1, bits, {});
        landAdvanceFrame(b, p2, bits, {});
        if (a.finished || b.finished) {
            out.frames = f;
            out.result = landResult(bits, a, b);
            break;
        }
    }
    out.playerIndex = a.index;
    out.mortyIndex = b.index;
    return out;
}

RaceSimResult simulateAirRace(const GgExe& exe, const Archive& archive, int level, const AirPerformance& player,
                              const AirPerformance& morty, int maxFrames) {
    const auto p1 = parseRacePath(archive.read(exe.ds16(static_cast<uint16_t>(dg::kAirPaths1 + 2 * level))));
    const auto p2 = parseRacePath(archive.read(exe.ds16(static_cast<uint16_t>(dg::kAirPaths2 + 2 * level))));
    AirRacerRec a, b;
    a.speeds = player.speeds;
    a.accs = player.accs;
    b.speeds = morty.speeds;
    b.accs = morty.accs;
    airRaceStart(a, b, p1, p2);
    int16_t pathEnd = 0;
    RaceSimResult out;
    for (int f = 1; f <= maxFrames; ++f) {
        airStepAndMove(a);
        airStepAndMove(b);
        airAdvanceFrame(a, p1, pathEnd, {});
        airAdvanceFrame(b, p2, pathEnd, {});
        if (a.finished || b.finished) {
            out.frames = f;
            out.result = a.finished ? 0 : 1;
            break;
        }
    }
    out.playerIndex = a.index;
    out.mortyIndex = b.index;
    return out;
}

}  // namespace tmport::gg
