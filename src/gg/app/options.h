#pragma once
// The command line of ggport / ggport-cli and the original program's switches (re/gg/docs/
// PORTING_NOTES.md §7, code_runtime.md §2.2).
//
// Engine options:
//   --data DIR            the Gizmos & Gadgets folder (else ggport.ini, GG_DATA, next to the program)
//   --pref-dir DIR        players.dat, ggport.ini, screenshots (default: the SDL preference folder
//                         of "GGPort")
//   --headless            no window, no sound; deterministic virtual time; the run ends when the
//                         --keys/--mouse script is used up and --ticks have run
//   --frame-dump PATH     a folder (existing, or a name without an extension): one PNG per new
//                         picture shown, plus end.png; anything else: the last picture as that PNG
//   --ticks N             run at least N PIT ticks (145.65 per second); in a window: end after N
//   --keys SCRIPT, --mouse SCRIPT   scripted input in PIT ticks (dos::ScriptedInput: tap, down, up,
//                         move, click, buttons); both may be given, they are merged
//   --gg-args "SWITCHES"  the original's switches (-S, -SN, -S-, -SI, -SA, -SB, -SM, -GV, -?)
//   --sound off|speaker|fm, --cpu 286|386, --seed N (srand seed instead of the clock's)
//   --fullscreen, --windowed, --square-pixels, --scale N (window = N x 320x240), --integer-scale,
//   --no-pause (keep running without focus), --screenshot-dir DIR, --verbose, --help
//   --wav-dump FILE       headless: the run's sound rendered offline (the sound ticks of the virtual
//                         PIT) as a 16-bit mono WAV
//   --opl-log FILE        the run's OPL register writes, as tools/dos/oplcmp.py and --gg-dro-compare
//                         read them
//
// The original's switches (Init_CommandLine 303a:005c step 3): an argument whose first character is
// not '-' or '/' is ignored; the second character, upper-cased, selects: '?' usage; 'G' with a
// third character 'V' (any case) the display code 8, anything else usage; 'S' with the third
// character NUL (-S alone), '-' or 'N' sound device 0, 'I' 1, 'A' / 'B' / 'M' 4, anything else
// usage; any other letter usage. Only three characters are read (-SBLASTER = -SB).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gg/audio/sound_mode.h"

namespace tmport::gg {

// The engine's sound option (PORTING_NOTES §7): off = -SN (FM banks, every song run silently),
// speaker = -SI, fm = -SA/-SB/-SM or no switch (an AdLib is detected: the emulated OPL2 is there).
using GgSound = SoundMode;
// The CPU class Cpu_Detect reports (2c08:0032): only the chimp set-up's 286 path depends on it.
enum class GgCpu : uint8_t { Cpu286, Cpu386 };

std::string_view cpuName(GgCpu c);
std::optional<GgSound> parseSound(std::string_view s);
std::optional<GgCpu> parseCpu(std::string_view s);

// Init_CommandLine 303a:005c step 3.
struct OriginalSwitches {
    int16_t soundDevice = -1;   // g_soundDevice DS:2452: -1 none (detect), 0 off, 1 speaker, 4 FM
    int16_t displayCode = -1;   // g_displayCode DS:244e: 8 after -GV (the display check is dropped)
    bool usage = false;         // -? or a bad switch: the original prints message 0x368, exit(1)
    std::string bad;            // the first argument that asked for the usage (empty for -?)
};
OriginalSwitches parseOriginalSwitches(std::span<const std::string> args);
// The arguments of a --gg-args string (split at white space).
std::vector<std::string> splitArgs(std::string_view s);
// g_soundDevice -> the engine's option: -1 (detect) and 4 FM, 0 off, 1 speaker.
GgSound soundForDevice(int16_t soundDevice);

struct GgOptions {
    std::string data;
    std::string prefDir;
    bool headless = false;
    std::string frameDump;
    std::optional<uint64_t> ticks;
    std::string keys;
    std::string mouse;
    std::string ggArgs;
    std::optional<GgSound> sound;
    std::optional<GgCpu> cpu;
    std::optional<uint16_t> seed;
    bool fullscreen = false;
    bool windowed = false;
    bool squarePixels = false;
    std::optional<int> scale;
    bool integerScale = false;
    bool noPause = false;
    std::string screenshotDir;
    bool verbose = false;
    bool help = false;
    std::string wavDump;  // --wav-dump (headless runs)
    std::string oplLog;   // --opl-log
};

// Parses the arguments (without argv[0]); nullopt with `error` set on an unknown option or a bad
// value.
std::optional<GgOptions> parseGgOptions(std::span<const std::string> args, std::string& error);
// The help text of the game options.
std::string ggUsage();

}  // namespace tmport::gg
