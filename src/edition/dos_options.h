#pragma once
// Options of the 1990 DOS edition (re/dos/docs/PORTING_NOTES.md §3): the original's command-line
// switches as engine options, the [dos] section of tmport.ini, and DosOptions, which the start-up
// hands to the DOS runtime (dos::runDosEdition, dos/app/dos_entry.h).
//
//   Option         Original switch (1460:0463 ParseCommandLine)   Values
//   look           -E -C -T -H -M (g_videoType 1..5)              ega cga tandy mcga (hercules = cga)
//   sound          -Si -St -Sa / -Ss (g_soundDevice 0 1 2)        speaker tandy adlib, off (engine)
//   slow_cpu       -X (toggles g_slowCpu)                         false / true
//   frame_speed    - (the CPU speed)                              reference / period
//
// Precedence at start-up: defaults < tmport.ini [dos] < --dos-args (the original's switches, in
// order) < --look / --sound / --slow-cpu / --frame-speed.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dos/gfx/palettes.h"

namespace tmport::dos {

// The picture is dos::Look of dos/gfx/palettes.h (Ega, Tandy, Mcga, Cga: EGA and Tandy give
// identical pixels, MCGA its own softer colours, CGA a 4-colour dither). The original's Hercules
// mode (-H, g_videoType 4) draws exactly like CGA (fmt_graphics.md §2), so it is Look::Cga here.
// Sound output: PC speaker (SFX.IBM + TUNES.IBM), Tandy 3-voice PSG (SFX.TAN + TUNES.TAN), AdLib
// (TUNES.TAN on OPL2, effects on the speaker; needs an OPL2 core), or none at all.
enum class SoundDevice : uint8_t { Speaker, Tandy, AdLib, Off };
// Per-frame features (search blink, HUD count-up, CHAMPION flash, ...): at the rates of the
// DOSBox-X reference recordings, or one step per original frame like a period PC (§2.3).
enum class FrameSpeed : uint8_t { Reference, Period };

// The [dos] section of tmport.ini (save::EngineSettings::dos).
struct DosSettings {
    Look look = Look::Ega;
    SoundDevice sound = SoundDevice::Tandy;  // until an OPL2 core exists (PORTING_NOTES §3)
    bool slowCpu = false;
    FrameSpeed frameSpeed = FrameSpeed::Reference;
    // Presentation (used by the start-up for the host's layout and window):
    bool squarePixels = false;  // square pixels (320x200 at 16:10) instead of the 4:3 picture
    int windowScale = 3;        // initial window = 320x240 x this (960x720)

    bool operator==(const DosSettings&) const = default;
};

// Everything the DOS runtime gets from the start-up.
struct DosOptions {
    Look look = Look::Ega;
    SoundDevice sound = SoundDevice::Tandy;
    bool slowCpu = false;
    FrameSpeed frameSpeed = FrameSpeed::Reference;
    bool squarePixels = false;  // informational: the host's layout already reflects it

    // Run context (from the launch options and the paths):
    bool headless = false;          // --headless: no window, no sound; end when the --keys script is used up
    std::string keys;               // --keys script in game ticks (dos/app/script_input.h, PORTING_NOTES §4)
    std::filesystem::path prefDir;  // the engine's preference folder (DOS saves keep their own file there, §5)
    bool verbose = false;           // --verbose
    // --frame-dump given as a folder (an existing folder or a name without an extension): the DOS
    // runtime writes a PNG of each screen it shows there (and, headless, the sound as a WAV) instead
    // of the start-up writing one canvas at the end.
    std::filesystem::path frameDumpDir;

    bool operator==(const DosOptions&) const = default;
};

// The game options of the settings (run context left at its defaults).
DosOptions dosOptionsFrom(const DosSettings& s);

// The original's values: g_videoType (1 EGA, 2 CGA, 3 Tandy, 5 MCGA; 4 Hercules is never produced)
// and g_soundDevice (0 speaker, 1 Tandy, 2 AdLib; -1 for Off, which the original cannot select).
int videoTypeOf(Look look);
int soundDeviceOf(SoundDevice sound);

// Option values (case-insensitive) and their names in tmport.ini / on the command line. A look is
// one of dos::parseLook's names or "hercules" (= cga); its name is dos::lookName.
std::optional<Look> parseLookOption(std::string_view text);
std::optional<SoundDevice> parseSoundDevice(std::string_view text);
std::optional<FrameSpeed> parseFrameSpeed(std::string_view text);
std::string_view iniValue(SoundDevice v);
std::string_view iniValue(FrameSpeed v);

// 1460:0463 ParseCommandLine on a list of switches: every argument starting with '-' or '/' is
// looked up by its second character (case-insensitive): E C T M set the look (H = C), X toggles slowCpu,
// S<c> sets the sound for c in "iItTaAsS" (i speaker, t Tandy, a / s AdLib; anything else leaves it).
// Later switches win; the rest of an argument is ignored ("-EGA" = "-E"). Returns the arguments
// that had no effect (not a switch, unknown letter, -S without a known device), for a warning.
std::vector<std::string> applyDosSwitches(DosOptions& o, const std::vector<std::string>& args);
// The same for one string of whitespace-separated arguments (--dos-args "-T -St").
std::vector<std::string> applyDosSwitches(DosOptions& o, std::string_view args);

}  // namespace tmport::dos
