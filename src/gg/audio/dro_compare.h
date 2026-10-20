#pragma once
// DOSBox-X raw OPL captures (.dro v2.0) and a write-for-write comparison of the driver's OPL
// register writes with them (re/gg/docs/fmt_sound.md §5, REFERENCE_CAPTURES.md §10.6).
//
// How DOSBox-X captures (observed in the G&G captures, as tools/dos/oplcmp.py describes it for TM
// DOS): it opens the file at the first key-on after the capture was requested (a B0h..B8h write with
// bit 5, or a BDh write whose low six bits exceed 20h), writes its register cache first (every
// non-zero register in ascending order, the key bits of B0h..B8h and the drum bits of BDh masked:
// "about 100 writes at t = 0"), then the key-on, then every write that changes a register, in whole
// milliseconds. The timer registers 02h..04h are never cached nor logged.
//
// simulateCapture() applies those rules to the engine's write log; compareWithDro() looks for the
// key-on of the engine's log where the capture continues (the capture starts at an unknown moment
// of a looping song) and counts the identical writes from there, register and value in order, and
// how far each lies from the sound-tick grid (13.731 ms).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dos/audio/opl_log.h"

namespace tmport::gg {

struct DroWrite {
    uint32_t ms = 0;
    uint16_t reg = 0;  // 100h + r for the second chip of an OPL3 capture
    uint8_t value = 0;
};

struct DroFile {
    uint32_t lengthMs = 0;
    uint8_t hardware = 0;  // 0 OPL2
    std::vector<DroWrite> writes;
    size_t dumpSize = 0;   // writes before the first key-on (the register dump)
};

// Parses a .dro v2.0 file. Throws tmport::FormatError.
DroFile parseDro(std::span<const uint8_t> file);
// Reads 'tick reg value[ c]' lines (opl_log.h's text). Throws tmport::FormatError for a bad line.
std::vector<dos::OplWriteLog::Entry> parseOplLogText(std::string_view text);

// True for a write that makes DOSBox-X open its capture.
bool isCaptureTrigger(uint8_t reg, uint8_t value);

struct CaptureWrite {
    uint64_t tick = 0;
    uint8_t reg = 0, value = 0;
    bool command = false;
    size_t index = 0;    // in the engine log
    int inTick = 0;      // writes before it in its tick (logged or not), for the time model
};

struct SimulatedCapture {
    std::vector<DroWrite> dump;         // register, value (ms 0)
    std::vector<CaptureWrite> writes;   // the trigger, then the writes that change a register
};

// DOSBox-X's capture of the engine log when it is requested before entry `from`.
SimulatedCapture simulateCapture(std::span<const dos::OplWriteLog::Entry> log, size_t from = 0);

struct DroComparison {
    bool found = false;           // a key-on of the engine log continues the capture
    size_t triggerIndex = 0;      // the engine log entry of the capture's first key-on
    uint64_t triggerTick = 0;
    size_t refWrites = 0;         // .dro writes after the dump (the trigger included)
    size_t engineWrites = 0;      // simulated capture writes after the dump
    size_t matched = 0;           // identical in order (register and value) from the trigger on
    size_t dumpEqual = 0, dumpDiffer = 0;  // dump registers equal / different
    std::string dumpDiffs;        // 'reg engine/dro' of the first differing dump registers
    std::string firstMismatch;    // empty when all ref writes matched
    // Timing of the matched writes made inside a tick (not by a game call): .dro ms against
    // a + 13.731 * tick + w * inTick, a fitted (median); deviations in ticks.
    double offsetMs = 0;
    double minDev = 0, maxDev = 0;
    size_t timed = 0, within025 = 0, within05 = 0;
    // Identical writes before the first inside-tick write that lies more than half a tick off.
    size_t matchedOnTime = 0;
    uint64_t offGridTick = 0;  // the engine tick of that write
    // The matched writes: .dro ms, engine tick, writes before it in its tick, made by a game call.
    struct Pair {
        uint32_t ms = 0;
        uint64_t tick = 0;
        int inTick = 0;
        bool command = false;
    };
    std::vector<Pair> pairs;
    std::string summary() const;
};

// Where the filter "does this write change a register" starts: the engine's registers at the
// trigger (for a run that reproduces everything before the capture), or the capture's dump (for a
// capture made after screens the run does not reproduce: what happened before only shows in the
// dump, which is then reported, not used).
enum class CompareBase { Engine, CaptureDump };

// Compares; the capture's first key-on is searched among the engine's trigger writes at or after
// `fromIndex` (the first with the longest identical run wins). `writeMs` = the time model's time per
// AdLib_Write.
DroComparison compareWithDro(const DroFile& ref, std::span<const dos::OplWriteLog::Entry> log, size_t fromIndex = 0,
                             double writeMs = 0.015, CompareBase base = CompareBase::Engine);

class SndDriver;

// A run that continues a capture made after screens it does not reproduce: the driver's B0h
// shadows of voices 0..5 (which Opl_NoteOff writes back) from the capture's register dump.
void seedB0Shadows(SndDriver& driver, const DroFile& capture);

}  // namespace tmport::gg
