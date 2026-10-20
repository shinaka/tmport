#pragma once
// The game's diagnostic tools (wave 3): the per-frame trace logger (--gg-trace FILE) and the DGROUP
// state dump (--gg-dump-state TICK FILE), the engine's counterparts of DOSBox-X save states and of
// tools/gg/capture_mem.py, for the fidelity comparisons. Engine tooling only: nothing changes the game.
//
// --gg-trace FILE         a CSV row (game/trace.h GgTraceRecord) at every frame boundary: every
//                         Timer_Wait(0) (the paced loops' frames) and, with --gg-trace-polls, every
//                         Input_GetCode poll, busy-wait turn and Timer_Expired poll too (the unpaced
//                         loops); the sound column is quoted. The logger is GgRuntime's yield hook while
//                         it lives.
// --gg-dump-state T FILE  at the first yield point at or after PIT interrupt T: dumpToDgroup(const
//                         GgGame&) (DS:0000..BD03, 0xBD04 bytes) to FILE and the field registry
//                         (gameStateFields() + serviceFields(): name, ds, size, stride, service, the
//                         modelled-byte mask) to FILE.fields.tsv, to compare with a state's DGROUP
//                         (diffDgroup, tools/gg/capture_mem.py diffds).
//
// Both chain to the yield hook that was installed before them (the test fixture's, each other's) and
// put it back when they go.
//
// Owner: flow agent.

#include <cstdint>
#include <filesystem>
#include <iosfwd>

#include "gg/app/gg_runtime.h"
#include "gg/game/trace.h"

namespace tmport::gg {

class GgGame;

class GgTraceLogger {
public:
    // Writes the CSV header and installs itself as g.rt's yield hook (in front of the previous one,
    // which it calls after each of its own rows). `polls`: a row at every input poll too.
    GgTraceLogger(GgGame& g, std::ostream& out, bool polls);
    ~GgTraceLogger();
    GgTraceLogger(const GgTraceLogger&) = delete;
    GgTraceLogger& operator=(const GgTraceLogger&) = delete;

    uint64_t rows() const { return rows_; }
    // The yield points that make a row (Timer_Wait(0); with polls also GetInputCode, Idle and
    // TimerExpired), as the test fixture counts its frame boundaries.
    static bool isBoundary(YieldPoint p, int slot, bool polls);

private:
    void onYield(YieldPoint p, int slot);

    GgGame& g_;
    std::ostream& out_;
    bool polls_;
    uint64_t rows_ = 0;
    GgRuntime::YieldHook previous_;
};

// --gg-dump-state: writes the dump once, at the first yield point at or after PIT interrupt `tick`.
class GgStateDumper {
public:
    GgStateDumper(GgGame& g, uint64_t tick, std::filesystem::path file);
    ~GgStateDumper();
    GgStateDumper(const GgStateDumper&) = delete;
    GgStateDumper& operator=(const GgStateDumper&) = delete;

    uint64_t tick() const { return tick_; }
    bool written() const { return written_; }

private:
    void onYield(YieldPoint p, int slot);

    GgGame& g_;
    uint64_t tick_;
    std::filesystem::path file_;
    bool written_ = false;
    GgRuntime::YieldHook previous_;
};

// Writes dumpToDgroup(g) to `file` and the field registry to `file` + ".fields.tsv". False on an I/O
// error.
bool writeGgDgroupDump(GgGame& g, const std::filesystem::path& file);
// The field registry as TSV: "name, ds, size, stride, service, mask" (mask: one 0/1 per byte of an
// element, empty when every byte is modelled), one line per field after a header line.
std::string ggDgroupFieldsTsv();

}  // namespace tmport::gg
