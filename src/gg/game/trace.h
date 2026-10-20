#pragma once
// The per-frame trace record of Gizmos & Gadgets (wave 3): what the test fixture
// (tests/gg/gg_game_fixture.h) records at every frame boundary and the --gg-trace logger (the flow
// agent's, game/trace_log.h) writes as CSV, so engine runs can be compared with DOSBox-X state
// sequences and with each other. Engine tooling only: nothing here changes the game.
//
// CSV (traceCsvHeader / traceCsvRow; '#' lines are comments):
//
//   frame,yield,interrupts,ticks,state,displayroom,playerroom,x,y,seq,randcalls,randstate,code,pictures,sound
//
//   frame       the boundary's number (1, 2, ...)
//   yield       the yield point that ended it (gg/app/gg_runtime.h YieldPoint: timerWait.0, getInputCode, ...)
//   interrupts  PIT interrupts so far (the runtime's own count; script ticks count these)
//   ticks       g_ticks DS:021a (the dialog freeze writes it back)
//   state       g_state DS:455c          displayroom g_displayRoom DS:08c2
//   playerroom  g_playerRoom DS:7aa4 (actor 0 +0x1a); x, y, seq actor 0 +0x02, +0x04, +0x18
//   randcalls   rand() calls so far      randstate g_randState DS:442a (hex)
//   code        the last Input_GetCode result (hex)
//   pictures    pictures presented so far (GgRuntime::framesShown)
//   sound       the sound calls since the previous row, when the facade records them (RecordingSound
//               describe(): "playEffect(12,0,4); stopAllEffects()"), quoted (the calls hold commas),
//               else empty
//
// Owner: skeleton.

#include <cstdint>
#include <string>
#include <string_view>

#include "gg/app/gg_runtime.h"

namespace tmport::gg {

class GgGame;

struct GgTraceRecord {
    uint64_t frame = 0;
    YieldPoint yield = YieldPoint::TimerWait;
    int slot = -1;              // the timer slot of TimerWait / TimerExpired, else -1
    uint64_t interrupts = 0;
    uint32_t ticks = 0;
    int16_t state = 0;
    int16_t displayRoom = 0;
    int16_t playerRoom = 0;
    int16_t x = 0, y = 0, seq = 0;
    uint64_t randCalls = 0;
    uint32_t randState = 0;
    uint16_t lastCode = 0;
    uint64_t pictures = 0;
    std::string sound;
    bool operator==(const GgTraceRecord&) const = default;
};

// The record of the game's current moment (frame, yield and sound are the caller's).
GgTraceRecord traceSnapshot(GgGame& g);
std::string_view traceCsvHeader();
std::string traceCsvRow(const GgTraceRecord& r);
// One line per record whose state, rooms, x, y, seq or rand() count changed (test messages).
std::string traceChanges(const GgTraceRecord* prev, const GgTraceRecord& r);

}  // namespace tmport::gg
