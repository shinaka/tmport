#include "app/launch_options.h"

#include <format>
#include <string_view>

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
           "  --mods <dir>         also load this mod folder (repeatable; after the [mods] folders of\n"
           "                       tmport.ini, later ones win); --no-mods: load no mods (docs/MODDING.md)\n"
           "  --headless           no window, no message boxes, no sound (automation); the run ends\n"
           "                       when the --keys script is used up\n"
           "  --keys <script>      scripted input, comma-separated steps: key names (enter, esc,\n"
           "                       space, tab, up, down, left, right, home, end, pgup, pgdn, f1..f12,\n"
           "                       alt, a..z, 0..9; prefixes alt+ ctrl+ shift+), click:<x>:<y> and\n"
           "                       move:<x>:<y> (640x480 canvas), wait:<ms>, shot:<file.png>, quit\n"
           "  --frame-dump <png>   write the 640x480 canvas when the game ends\n";
}

}  // namespace tmport::app
