#pragma once
// The races (ssg.exe states 10 and 11 of 12e0, ovl24 3dd6 the land and alternative-energy race, ovl25
// 3e77 the aircraft race, ovl26 3f50 Math_DivRound; re/gg/docs/code_vehicles_races.md §7-§10): the
// vehicle's part list, the performance from the parts (16.16 tables of DGROUP), the race loops at 8
// ticks per frame with one key popped per frame (Kbd_PopQueuedKey) and the path commands, the result.
// The races are deterministic functions of the two part lists: no rand(), no input (keep). Their
// globals are GgState::vehicles (the racer records, g_raceFinishBits, the camera, g_playerVehicle,
// g_rematch, g_raceResult) and the player record (g_racesWon, g_racesWonTotal, g_vehicleColor).
//
// The two race models are written twice over the same functions: the game's loops (raceRun,
// airRaceRun) draw and pace each frame, and the pure simulations (simulateLandRace, simulateAirRace)
// step the same records without a game, so the test vectors of code_vehicles_races.md §18 run as unit
// tests. Paths keep the frame index of their 'R' records (the original replaces it by a picture-cache
// slot, Race_MapPathFrames): the engine takes picture `frame` of the bundle, `frame + anim` for the
// aircraft (code_vehicles_races.md §10.5's recommendation).
//
// Owner: vehicles agent.

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "gg/app/seq_run.h"

namespace tmport::gg {

class Archive;
class GgExe;
class GgGame;
class PartCatalogue;
struct AirRacerRec;
struct RacerRec;

// A path record of an 'R' resource {i16 x, i16 y, i16 code} (code >= 0 a frame, < 0 a command; the
// list ends with a record of code -1).
struct RacePathPoint {
    int16_t x = 0, y = 0, code = 0;
};
// An 'R' resource as its records (up to and including the -1 record; the whole payload when it has
// none). Throws tmport::FormatError for an empty path.
std::vector<RacePathPoint> parseRacePath(std::span<const uint8_t> payload);

// What a path command does to the sound: (code, x, y) of a -13 / -14 record. The game's hook starts or
// stops effect bank + x (game/race.cpp); the simulations pass an empty one.
using RacePathSound = std::function<void(int code, int x, int y)>;

// 12e0:0506 State_10_ListVehicleParts: g_playerVehicle = the parts held by the player and built in
// (0x8000), -1 terminated; g_rematch = 0; 0xb.
int16_t state10ListVehicleParts(GgGame& g);
// 12e0:0564 State_11_Race: Pal_Save, Mouse_Hide, g_keyHeldSeen = 1, Hud_Update(0), the building's race
// -> g_raceResult; a won race that is not a rematch: g_racesWon[b] + 1, g_racesWonTotal + 1;
// Mouse_Show; 0xe after a rematch, else 0xc. Resume is not supported (the race's locals are on the
// original's stack): the race starts again from its set-up.
int16_t state11Race(GgGame& g, StateEntry entry = StateEntry::Normal);

// ---- ovl26 3f50
// 3f50:0000 Math_DivRound(a, b): |a| divided by the high word of |b| (the integer part of a 16.16
// value), rounded half up (remainder >= ceil(d / 2) adds 1), negated when the signs differ; a 32-bit
// result. A zero divisor faults in the original; the engine returns 0 and logs.
int32_t mathDivRound(int32_t a, int32_t b);

// ---- ovl24 3dd6, the land and alternative-energy race (§9)

// The numbers Race_ComputePerformance stores (16.16).
struct LandPerformance {
    uint32_t max = 0;    // +0x26 top speed
    uint32_t limit = 0;  // +0x22 min(cap, max): the limit of the limited sections
    uint32_t acc = 0;    // +0x36
    uint32_t acc2 = 0;   // +0x32
    bool operator==(const LandPerformance&) const = default;
};
// 3dd6:044e Race_ComputePerformance's arithmetic on a part list (code_vehicles_races.md §8.1), from the
// tables of `exe`. `initialB` is the caller's DI (the race's first sprite bundle's picture count);
// `initialA` stands for the uninitialised local of race 14 (a heap segment in the original: the engine
// uses INT16_MAX, so min(A, B) = B, the fuel-cell variant; keep).
LandPerformance landPerformance(const GgExe& exe, const PartCatalogue& parts, int raceIndex, std::span<const int16_t> list,
                                int16_t initialB, int16_t initialA = 0x7FFF);
// 3dd6:044e Race_ComputePerformance(racer, parts): landPerformance into the racer (+0x1c = 0).
void raceComputePerformance(GgGame& g, RacerRec& r, std::span<const int16_t> parts);
// 3dd6:0000 Race_MapPathFrames: in the engine the frame index stays (picture `code` of `bundle`); the
// frames that lie outside the bundle are logged (the original would read past its slot list).
void raceMapPathFrames(GgGame& g, std::vector<RacePathPoint>& path, uint16_t bundle);
// 3dd6:0034 Race_StepSpeed: mode 0 speed += acc up to max; mode 1 speed -= 0.2 while above the limit,
// else += acc2 up to the limit (signed 32-bit compares).
void raceStepSpeed(RacerRec& r);
// 3dd6:00e0 Race_AdvancePoint: the next path point, running the commands (-14 / -13 effects, -5 lap:
// laps - 1 if any and back to index 0, -4 / -3 mode, -2 finish when no lap is left: finished and the
// racer's bit into `finishBits`); 1 when the finish was crossed in this call.
int raceAdvancePoint(RacerRec& r, std::span<const RacePathPoint> path, uint16_t& finishBits, const RacePathSound& sound = {});
// 3dd6:01ca Race_Run: the music intro wait, Automotive's start effects, the 8-tick frames of §9.5;
// the result of §9.6 (0 won).
int raceRun(GgGame& g);
// 3dd6:0762 Race_LandEnergy: the set-up of §9.1, Race_Run, the clean-up; the result.
int raceLandEnergy(GgGame& g);

// ---- ovl25 3e77, the aircraft race (§10)

// The numbers AirRace_ComputePerformance stores: per flight mode the target speed and acceleration.
struct AirPerformance {
    std::array<uint32_t, 3> speeds{};
    std::array<uint32_t, 3> accs{};
    bool operator==(const AirPerformance&) const = default;
};
// 3e77:0036 AirRace_ComputePerformance's arithmetic (code_vehicles_races.md §8.2).
AirPerformance airPerformance(const GgExe& exe, const PartCatalogue& parts, int level, std::span<const int16_t> list);
void airRaceComputePerformance(GgGame& g, AirRacerRec& r, std::span<const int16_t> parts);  // 3e77:0036
void airRaceMapScenery(GgGame& g, uint16_t bundle);  // 3e77:0000 AirRace_MapScenery: the frames checked against the bundle
// 3e77:03c7 AirRace_DrawScenery(list, scroll): every record of a layer (1 far, 2 near) keyed at
// (x - int(scroll), y).
void airRaceDrawScenery(GgGame& g, int layer, uint32_t scroll);
// 3e77:0428 AirRace_StepSpeed: speed += acc[mode], clamped towards speed[mode] (at most when the
// acceleration is >= 0, at least when it is negative).
void airRaceStepSpeed(AirRacerRec& r);
// 3e77:04cc AirRace_AdvancePoint: the next path point with the commands (-14 / -13 effects, -12 jump,
// -11 / -9 mode 0, -10 mode 2, -8 mode 1, -7 / -6 nothing, -2 finish, -1 end); 1 when the finish was
// crossed. `pathEnd` is g_airPathEnd.
int airRaceAdvancePoint(AirRacerRec& r, std::span<const RacePathPoint> path, int16_t& pathEnd, const RacePathSound& sound = {});
// 3e77:05e3 AirRace_Run: the 8-tick frames of §10.5 (the camera, level 1's launch, the first frame's
// fade and Delay(146)); 0 when the player won (the player wins a tie). The flight mode is never reset
// (§10.6: keep).
int airRaceRun(GgGame& g);
// 3e77:0a23 Race_Aircraft: the set-up of §10.1, AirRace_Run, the clean-up; the result.
int raceAircraft(GgGame& g);

// ---- the pure simulations (no game, no drawing, no sound): the test vectors of §18

struct RaceSimResult {
    int frames = 0;           // frames until one racer finished (frame 1 = the first Timer_Set(0, 8))
    int result = -1;          // 0 the player won, 1 lost, -1 nobody finished within the limit
    int playerIndex = 0;      // path indexes after the last frame
    int mortyIndex = 0;
};
// Race_Run's model (§9.5, §9.6) for two racers with the given performances on the race's paths.
RaceSimResult simulateLandRace(const GgExe& exe, const Archive& archive, int raceIndex, const LandPerformance& player,
                               const LandPerformance& morty, int maxFrames = 20000);
// AirRace_Run's model (§10.5) for two aircraft on the level's paths (flight modes 0 at the start).
RaceSimResult simulateAirRace(const GgExe& exe, const Archive& archive, int level, const AirPerformance& player,
                              const AirPerformance& morty, int maxFrames = 20000);

}  // namespace tmport::gg
