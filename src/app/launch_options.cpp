#include "app/launch_options.h"

#include <charconv>
#include <cstdint>
#include <format>
#include <string_view>

#include "edition/dos_options.h"
#include "edition/edition.h"

namespace tmport::app {

namespace {

struct Flag {
    std::string_view name;
    int args;  // number of values that follow
};
constexpr Flag kFlags[] = {
    {"--play", 0},         {"--data", 1},        {"--config", 1},      {"--pref-dir", 1},
    {"--screenshot-dir", 1}, {"--16", 0},        {"--integer-scale", 0}, {"--no-pause", 0},
    {"--tap-latch", 0},    {"--input-mode", 1},  {"--midi-set", 1},    {"--midi", 1},
    {"--soundfont", 1},
    {"--no-audio", 0},     {"--verbose", 0},     {"--headless", 0},    {"--keys", 1},
    {"--frame-dump", 1},   {"--fullscreen", 0},  {"--windowed", 0},
    {"--mods", 1},         {"--no-mods", 0},
    {"--debug", 0},        {"--show-treasure", 0},
    {"--edition", 1},      {"--look", 1},        {"--sound", 1},       {"--slow-cpu", 0},
    {"--frame-speed", 1},  {"--dos-args", 1},    {"--square-pixels", 0},
    {"--dos-trace", 1},    {"--dos-dump-state", 1}, {"--dos-ticks", 1}, {"--dos-preview", 0},
    {"--dos-layout-seed", 1},
    {"--dos-load-state", 1}, {"--dos-grey-key-artefact", 0},
};

const Flag* findFlag(std::string_view a) {
    for (const Flag& f : kFlags)
        if (f.name == a) return &f;
    return nullptr;
}

}  // namespace

bool isGameInvocation(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const Flag* f = findFlag(argv[i]);
        if (!f) return false;
        i += f->args;
    }
    return true;
}

std::optional<LaunchOptions> parseLaunchArgs(int argc, char** argv, std::string& error) {
    LaunchOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const Flag* f = findFlag(a);
        if (!f) {
            error = std::format("unknown argument '{}'", a);
            return std::nullopt;
        }
        if (i + f->args >= argc) {
            error = std::format("{} needs a value", a);
            return std::nullopt;
        }
        const std::string v = f->args ? argv[i + 1] : std::string();
        i += f->args;
        if (a == "--data") o.dataPath = v;
        else if (a == "--config") o.configPath = v;
        else if (a == "--pref-dir") o.prefDir = v;
        else if (a == "--screenshot-dir") o.screenshotDir = v;
        else if (a == "--16") o.colour16 = true;
        else if (a == "--integer-scale") o.integerScale = true;
        else if (a == "--fullscreen") o.fullscreen = true, o.windowed = false;
        else if (a == "--windowed") o.windowed = true, o.fullscreen = false;
        else if (a == "--no-pause") o.noPause = true;
        else if (a == "--tap-latch") o.tapLatch = true;
        else if (a == "--debug") o.debugKeys = true;
        else if (a == "--show-treasure") o.showTreasure = true;
        else if (a == "--no-audio") o.noAudio = true;
        else if (a == "--verbose") o.verbose = true;
        else if (a == "--headless") o.headless = true;
        else if (a == "--keys") o.keys = v;
        else if (a == "--frame-dump") o.frameDump = v;
        else if (a == "--mods") o.modDirs.push_back(v);
        else if (a == "--no-mods") o.noMods = true;
        else if (a == "--input-mode") {
            if (v != "lossless" && v != "first-event") {
                error = std::format("--input-mode: '{}' is not lossless or first-event", v);
                return std::nullopt;
            }
            o.inputMode = v;
        } else if (a == "--midi-set") {
            if (v != "E" && v != "B" && v != "e" && v != "b") {
                error = std::format("--midi-set: '{}' is not E or B", v);
                return std::nullopt;
            }
            o.midiSet = v;
        } else if (a == "--midi") {
            if (v != "auto" && v != "winmm" && v != "soundfont" && v != "null") {
                error = std::format("--midi: '{}' is not auto, winmm, soundfont or null", v);
                return std::nullopt;
            }
            o.midiBackend = v;
        } else if (a == "--soundfont") {
            o.soundFont = v;
        } else if (a == "--edition") {
            if (v != "auto" && !parseEditionKey(v)) {
                error = std::format("--edition: '{}' is not win, dos or auto", v);
                return std::nullopt;
            }
            o.edition = v;
        } else if (a == "--look") {
            if (!dos::parseLookOption(v)) {
                error = std::format("--look: '{}' is not ega, tandy, mcga or cga", v);
                return std::nullopt;
            }
            o.dosLook = v;
        } else if (a == "--sound") {
            if (!dos::parseSoundDevice(v)) {
                error = std::format("--sound: '{}' is not tandy, speaker, adlib or off", v);
                return std::nullopt;
            }
            o.dosSound = v;
        } else if (a == "--frame-speed") {
            if (!dos::parseFrameSpeed(v)) {
                error = std::format("--frame-speed: '{}' is not reference or period", v);
                return std::nullopt;
            }
            o.frameSpeed = v;
        } else if (a == "--slow-cpu") {
            o.dosSlowCpu = true;
        } else if (a == "--dos-args") {
            o.dosArgs = v;
        } else if (a == "--square-pixels") {
            o.squarePixels = true;
        } else if (a == "--dos-trace") {
            o.dosTrace = v;
        } else if (a == "--dos-dump-state") {
            const size_t colon = v.find(':');
            uint64_t tick = 0;
            const auto [p, ec] = std::from_chars(v.data(), v.data() + (colon == std::string::npos ? 0 : colon), tick);
            if (colon == std::string::npos || colon == 0 || colon + 1 >= v.size() || ec != std::errc() || p != v.data() + colon) {
                error = std::format("--dos-dump-state: '{}' is not <tick>:<file>", v);
                return std::nullopt;
            }
            o.dosDumpStates.push_back(v);
        } else if (a == "--dos-ticks") {
            uint64_t n = 0;
            const auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
            if (v.empty() || ec != std::errc() || p != v.data() + v.size()) {
                error = std::format("--dos-ticks: '{}' is not a number", v);
                return std::nullopt;
            }
            o.dosTicks = v;
        } else if (a == "--dos-preview") {
            o.dosPreview = true;
        } else if (a == "--dos-layout-seed") {
            // decimal or 0x-prefixed hex, 0..65535 (the srand argument is an unsigned int)
            const bool hex = v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X');
            const char* first = v.data() + (hex ? 2 : 0);
            uint32_t n = 0;
            const auto [p, ec] = std::from_chars(first, v.data() + v.size(), n, hex ? 16 : 10);
            if (v.empty() || ec != std::errc() || p != v.data() + v.size() || n > 0xFFFF) {
                error = std::format("--dos-layout-seed: '{}' is not a number from 0 to 65535", v);
                return std::nullopt;
            }
            o.dosLayoutSeed = v;
        } else if (a == "--dos-load-state") {
            o.dosLoadState = v;
        } else if (a == "--dos-grey-key-artefact") {
            o.dosGreyKeyArtefact = true;
        }
    }
    return o;
}

std::string launchUsage() {
    return "Playing:\n"
           "  tmport [--play] [options]          start the game (this is what a double-click does)\n"
           "\n"
           "Game options:\n"
           "  --data <path>        game folder (TMTWINCD) or CD image (.cue/.bin/.iso); default: the\n"
           "                       TM_DATA environment variable, then [engine] data in tmport.ini,\n"
           "                       then auto-detection next to the executable / current directory\n"
           "  --config <ini>       settings file (default: tmport.ini next to the executable, else in\n"
           "                       the preference directory)\n"
           "  --pref-dir <dir>     directory for users.dat, tmport.ini and screenshots\n"
           "  --16                 16-colour art (TMT16.DLL)\n"
           "  --integer-scale      integer window scaling only\n"
           "  --fullscreen         start in fullscreen; --windowed: start in a window (default:\n"
           "                       [engine] fullscreen in tmport.ini); Alt+Enter or F11 toggles\n"
           "  --no-pause           keep running while the window is unfocused\n"
           "  --input-mode <m>     lossless (default) or first-event\n"
           "  --tap-latch          arrow taps shorter than one tick are not missed\n"
           "  --midi-set E|B       E*.MID (General MIDI, default) or B*.MID music files\n"
           "  --midi auto|winmm|soundfont|null   MIDI output (soundfont: software synthesizer)\n"
           "  --soundfont <sf2>    General MIDI SoundFont for the soundfont output (default: [audio]\n"
           "                       soundfont in tmport.ini); selects that output unless --midi is given\n"
           "  --no-audio           no sound at all\n"
           "  --screenshot-dir <d> F12 screenshot directory\n"
           "  --verbose            debug logging\n"
           "  --show-treasure      debug: box the hiding places that hold a treasure (gold) or the\n"
           "                       key (cyan); F9 toggles ([debug] show_treasure in tmport.ini)\n"
           "  --debug              debug keys: F9 toggles the treasure boxes ([debug] keys)\n"
           "  --mods <dir>         also load this mod folder (repeatable; after the [mods] folders of\n"
           "                       tmport.ini (DOS: [mods] dos_paths), later ones win); --no-mods: load\n"
           "                       no mods (docs/MODDING.md)\n"
           "  --headless           no window, no message boxes, no sound (automation); the run ends\n"
           "                       when the --keys script is used up\n"
           "  --keys <script>      scripted input, comma-separated steps: key names (enter, esc,\n"
           "                       space, tab, up, down, left, right, home, end, pgup, pgdn, f1..f12,\n"
           "                       alt, a..z, 0..9; prefixes alt+ ctrl+ shift+), click:<x>:<y> and\n"
           "                       move:<x>:<y> (640x480 canvas), wait:<ms>, shot:<file.png>, quit.\n"
           "                       DOS edition, in game ticks: TICK:down|up|tap:KEY, TICK:move:X:Y,\n"
           "                       TICK:click:X:Y (+N = N ticks later), e.g. \"50:tap:enter, +10:tap:3\";\n"
           "                       a TICK with a fraction lies inside the tick (806.4923:down:down)\n"
           "  --frame-dump <png>   write the 640x480 canvas when the game ends (DOS edition: the\n"
           "                       320x200 canvas; a folder: a PNG per screen, headless also sound.wav)\n"
           "\n"
           "Editions:\n"
           "  --edition win|dos    which edition to play when the data holds both (default: [engine]\n"
           "                       edition in tmport.ini; detected from the data: TMTWINCD.EXE or a CD\n"
           "                       image = Windows CD edition, SST.EXE = 1990 DOS edition). Without\n"
           "                       --data the DOS edition's folder comes from TM_DATA_DOS, [dos] data,\n"
           "                       or a folder with SST.EXE next to the executable\n"
           "\n"
           "1990 DOS edition (also in the [dos] section of tmport.ini):\n"
           "  --look ega|tandy|mcga|cga   the picture of that video adapter (default ega)\n"
           "  --sound tandy|speaker|adlib|off      the sound device (default tandy)\n"
           "  --slow-cpu           the original's slow-machine mode (-X)\n"
           "  --frame-speed reference|period       per-frame effects at the speed of the reference\n"
           "                       recordings (default) or one step per frame like a period PC\n"
           "  --dos-args \"<sw>\"    the original's switches, e.g. \"-T -St\" (-E -C -T -H -M look, -X\n"
           "                       slow CPU, -Si -St -Sa sound); --look, --sound, --slow-cpu win\n"
           "  --square-pixels      square pixels (320x200 at 16:10) instead of the 4:3 picture\n"
           "  --dos-trace <csv>    write one CSV row per game tick (player, camera, keys, events)\n"
           "  --dos-dump-state <tick>:<file>   write the game state in the original's memory layout\n"
           "                       after that game tick (repeatable; plus <file>.fields.tsv, <file>.png)\n"
           "  --dos-ticks <n>      with --headless: run at least n game ticks (47.34 per second)\n"
           "  --dos-layout-seed <n>  a new game's layout from this srand value (0..65535, or 0x..)\n"
           "                       instead of the clock, for reproducible runs\n"
           "  --dos-load-state <file.sav>   start from a DOSBox-X save state of the original (its\n"
           "                       sibling <name>.users becomes the DOS save of --pref-dir); ticks of\n"
           "                       --keys, --dos-ticks and --dos-dump-state count from the load\n"
           "  --dos-grey-key-artefact  the E0 prefix bytes of the grey keys reach the game's keyboard\n"
           "                       handler as on DOSBox-X (to replay its frame logs)\n";
}

}  // namespace tmport::app
