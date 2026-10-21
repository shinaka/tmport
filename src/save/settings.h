#pragma once
// Engine settings (tmport.ini), the save/preference paths and the one-time import of the
// original's users.dat (docs/DESIGN.md "Data sources and saves", PORTING_NOTES.md §4.10).
//
// The original keeps only start-up check flags in C:\TLCWIN\TMTWINCD\TMTWIN.INI; gameplay state
// lives in users.dat. The port's tmport.ini holds engine options:
//
//   [engine]  data = <game folder or CD image>   window_scale = 2   integer_scale = false
//             fullscreen = false   pause_on_focus_loss = true   cursor_scale = 0 (0 = follow window)
//             edition = auto | win | dos (which edition to play when the data holds both)
//   [input]   mode = lossless | first-event      tap_latch = false
//   [audio]   midi_set = E | B                   midi_backend = auto | winmm | soundfont | null
//             soundfont = <General MIDI .sf2 file>  music_volume = 0..100 (SoundFont output)
//             (midi = ... is accepted as another name of midi_backend and wins when both are set)
//   [saves]   imported_original = false          import_from = C:\TLCWIN\TMTWINCD\users.dat
//   [debug]   keys = false (F9 toggles the treasure boxes)   show_treasure = false (start with them on)
//   [dos]     data = <folder of the 1990 DOS edition>   look = ega | tandy | mcga | cga (hercules = cga)
//             sound = tandy | speaker | adlib | off      slow_cpu = false   frame_speed = reference | period
//             square_pixels = false   window_scale = 3 (320x240 x this)   (edition/dos_options.h)
//
// IniFile (mischief/save/ini_file.h) keeps comments, unknown keys and the order of the user's file when
// values are updated.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "edition/dos_options.h"
#include "edition/edition.h"
#include "mischief/save/ini_file.h"

namespace tmport::save {

enum class MidiSet { Extended /* E*.MID */, Base /* B*.MID */ };
enum class MidiBackend { Auto, WinMM, Null, SoundFont };
enum class InputModeSetting { Lossless, FirstEvent };

struct EngineSettings {
    std::string dataPath;            // empty: --data / TM_DATA / auto-detection
    int windowScale = 2;
    bool integerScale = false;
    bool fullscreen = false;
    bool pauseOnFocusLoss = true;
    int cursorScale = 0;
    InputModeSetting inputMode = InputModeSetting::Lossless;
    bool tapLatch = false;
    MidiSet midiSet = MidiSet::Extended;
    MidiBackend midiBackend = MidiBackend::Auto;
    std::string soundFont;           // .sf2 for the SoundFont MIDI output; empty: none
    int musicVolume = 100;           // 0..100, SoundFont MIDI output
    bool importedOriginal = false;
    std::string importFrom;          // empty: the platform default (Windows: C:\TLCWIN\TMTWINCD\users.dat)
    bool debugKeys = false;          // [debug] keys: F9 toggles the treasure boxes
    bool showTreasure = false;       // [debug] show_treasure: start with the treasure boxes on
    std::optional<Edition> edition;  // [engine] edition: nullopt = auto (detect; see edition/edition.h)
    std::string dosDataPath;         // [dos] data: the DOS edition's folder (used when it is chosen)
    dos::DosSettings dos;            // [dos] the DOS edition's options

    // Reads known keys; malformed values keep their defaults (a warning is logged).
    static EngineSettings fromIni(const mischief::save::IniFile& ini);
    // Writes every key into ini (keeping the rest of the file).
    void toIni(mischief::save::IniFile& ini) const;

    bool operator==(const EngineSettings&) const = default;
};

std::string_view iniValue(MidiSet v);
std::string_view iniValue(MidiBackend v);
std::string_view iniValue(InputModeSetting v);

struct Paths {
    std::filesystem::path prefDir;   // SDL_GetPrefPath("TMPort", "TMPort")
    std::filesystem::path exeDir;    // SDL_GetBasePath()
    std::filesystem::path iniPath;   // tmport.ini next to the executable if it exists there, else prefDir
    std::filesystem::path usersDat;  // prefDir/users.dat
    std::filesystem::path screenshotDir;  // prefDir/screenshots

    // Asks SDL for the directories (no SDL_Init needed). prefDir is empty if SDL cannot create it.
    static Paths detect();
    // Same layout under explicit directories (tests, portable installs).
    static Paths under(const std::filesystem::path& prefDir, const std::filesystem::path& exeDir);
};

// The original's hard-coded save location on Windows (empty path elsewhere).
std::filesystem::path defaultOriginalUsersDat();

enum class ImportResult {
    AlreadyDone,    // the settings say the import happened before
    DestExists,     // the port already has a users.dat: nothing imported (flag set)
    SourceMissing,  // no original users.dat (flag not set: tried again next start)
    Imported,       // copied byte for byte (flag set)
    Failed,         // read or write error (flag not set)
};
// One-time import of the original users.dat into dest. Updates settings.importedOriginal; the
// caller saves the settings afterwards.
ImportResult importOriginalUsersDat(EngineSettings& settings, const std::filesystem::path& source,
                                    const std::filesystem::path& dest);

// Loads tmport.ini (defaults if missing), performs the one-time import into paths.usersDat and
// writes the ini back if the import changed the flag.
EngineSettings loadSettingsAndImport(const Paths& paths, ImportResult* importResult = nullptr);

}  // namespace tmport::save
