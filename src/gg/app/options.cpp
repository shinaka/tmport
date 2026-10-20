#include "gg/app/options.h"

#include <cctype>
#include <charconv>
#include <format>

#include "util/file_util.h"

namespace tmport::gg {

std::string_view cpuName(GgCpu c) { return c == GgCpu::Cpu286 ? "286" : "386"; }

std::optional<GgSound> parseSound(std::string_view s) {
    const std::string v = toLowerAscii(s);
    if (v == "off" || v == "none") return GgSound::Off;
    if (v == "speaker" || v == "pc") return GgSound::Speaker;
    if (v == "fm" || v == "adlib" || v == "opl2") return GgSound::Fm;
    return std::nullopt;
}

std::optional<GgCpu> parseCpu(std::string_view s) {
    if (s == "286") return GgCpu::Cpu286;
    if (s == "386" || s == "486") return GgCpu::Cpu386;
    return std::nullopt;
}

// ------------------------------------------------------------------------- the original switches

namespace {

char upperAt(const std::string& s, size_t i) {
    // The argument's characters up to its NUL; toupper as the RTL does it for ASCII.
    if (i >= s.size()) return '\0';
    return static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
}

}  // namespace

OriginalSwitches parseOriginalSwitches(std::span<const std::string> args) {
    // 303a:00aa: argv[1..argc-1].
    OriginalSwitches o;
    for (const std::string& a : args) {
        if (a.empty() || (a[0] != '-' && a[0] != '/')) continue;  // 303a:00b3: not a switch
        const char c = upperAt(a, 1);
        bool usage = false;
        if (c == '?') {
            usage = true;
        } else if (c == 'G') {
            // 303a:00e4: -GV only.
            if (upperAt(a, 2) == 'V') o.displayCode = 8;
            else usage = true;
        } else if (c == 'S') {
            // 303a:0104: the 7-entry table CS:0234 (ovl11).
            switch (upperAt(a, 2)) {
                case '\0':
                case '-':
                case 'N': o.soundDevice = 0; break;
                case 'I': o.soundDevice = 1; break;
                case 'A':
                case 'B':
                case 'M': o.soundDevice = 4; break;
                default: usage = true; break;
            }
        } else {
            usage = true;
        }
        if (usage) {
            // 303a:0147: Init_FatalMessage(0x368), exit(1): the first one ends the parse.
            o.usage = true;
            if (c != '?') o.bad = a;
            return o;
        }
    }
    return o;
}

std::vector<std::string> splitArgs(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        const size_t b = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > b) out.emplace_back(s.substr(b, i - b));
    }
    return out;
}

GgSound soundForDevice(int16_t soundDevice) {
    switch (soundDevice) {
        case 0: return GgSound::Off;
        case 1: return GgSound::Speaker;
        default: return GgSound::Fm;  // -1: Snd_Init detects the AdLib (the emulated OPL2); 4: FM
    }
}

// ------------------------------------------------------------------------------- engine options

namespace {

struct OptionSpec {
    std::string_view name;
    int values;  // 0 flag, else the number of values it takes
};

constexpr OptionSpec kOptions[] = {
    {"--data", 1},        {"--pref-dir", 1},   {"--headless", 0},       {"--frame-dump", 1},
    {"--ticks", 1},       {"--keys", 1},       {"--mouse", 1},          {"--gg-args", 1},
    {"--sound", 1},       {"--cpu", 1},        {"--seed", 1},           {"--fullscreen", 0},
    {"--windowed", 0},    {"--square-pixels", 0}, {"--scale", 1},       {"--integer-scale", 0},
    {"--no-pause", 0},    {"--screenshot-dir", 1}, {"--verbose", 0},    {"--help", 0},
    {"-h", 0},            {"--play", 0},       {"--wav-dump", 1},       {"--opl-log", 1},
    {"--gg-preview", 0},  {"--gg-trace", 1},   {"--gg-trace-polls", 0}, {"--gg-dump-state", 2},
    {"--gg-load-state", 1},
};

template <class T>
bool parseNumber(std::string_view s, T& out) {
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return !s.empty() && ec == std::errc() && p == s.data() + s.size();
}

}  // namespace

std::optional<GgOptions> parseGgOptions(std::span<const std::string> args, std::string& error) {
    GgOptions o;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const OptionSpec* spec = nullptr;
        for (const OptionSpec& s : kOptions)
            if (a == s.name) spec = &s;
        if (!spec) {
            error = std::format("unknown option '{}'", a);
            return std::nullopt;
        }
        std::string v, v2;
        if (spec->values >= 1) {
            if (i + static_cast<size_t>(spec->values) >= args.size()) {
                error = spec->values == 1 ? std::format("{} needs a value", a) : std::format("{} needs {} values", a, spec->values);
                return std::nullopt;
            }
            v = args[++i];
            if (spec->values == 2) v2 = args[++i];
        }
        auto bad = [&](std::string_view what) {
            error = std::format("{} {}: {}", a, v, what);
            return std::nullopt;
        };
        if (a == "--data") o.data = v;
        else if (a == "--pref-dir") o.prefDir = v;
        else if (a == "--headless") o.headless = true;
        else if (a == "--frame-dump") o.frameDump = v;
        else if (a == "--ticks") {
            uint64_t n = 0;
            if (!parseNumber(v, n)) return bad("expected a number of PIT ticks");
            o.ticks = n;
        } else if (a == "--keys") o.keys = o.keys.empty() ? v : o.keys + " " + v;
        else if (a == "--mouse") o.mouse = o.mouse.empty() ? v : o.mouse + " " + v;
        else if (a == "--gg-args") o.ggArgs = o.ggArgs.empty() ? v : o.ggArgs + " " + v;
        else if (a == "--sound") {
            o.sound = parseSound(v);
            if (!o.sound) return bad("expected off, speaker or fm");
        } else if (a == "--cpu") {
            o.cpu = parseCpu(v);
            if (!o.cpu) return bad("expected 286 or 386");
        } else if (a == "--seed") {
            uint16_t s = 0;
            if (!parseNumber(v, s)) return bad("expected 0..65535");
            o.seed = s;
        } else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--windowed") o.windowed = true;
        else if (a == "--square-pixels") o.squarePixels = true;
        else if (a == "--scale") {
            int s = 0;
            if (!parseNumber(v, s) || s < 1 || s > 16) return bad("expected 1..16");
            o.scale = s;
        } else if (a == "--integer-scale") o.integerScale = true;
        else if (a == "--no-pause") o.noPause = true;
        else if (a == "--screenshot-dir") o.screenshotDir = v;
        else if (a == "--verbose") o.verbose = true;
        else if (a == "--wav-dump") o.wavDump = v;
        else if (a == "--opl-log") o.oplLog = v;
        else if (a == "--gg-preview") o.preview = true;
        else if (a == "--gg-trace") o.traceFile = v;
        else if (a == "--gg-trace-polls") o.tracePolls = true;
        else if (a == "--gg-dump-state") {
            uint64_t n = 0;
            if (!parseNumber(v, n)) return bad("expected a PIT tick, then a file");
            o.dumpStateTick = n;
            o.dumpStateFile = v2;
        } else if (a == "--gg-load-state") o.loadState = v;
        else if (a == "--help" || a == "-h") o.help = true;
        // --play: the game (the default), accepted for symmetry with tmport.
    }
    return o;
}

std::string ggUsage() {
    return "ggport: Super Solvers: Gizmos & Gadgets! (DOS v1.0) on your own copy of the game\n"
           "\n"
           "usage: ggport [options]\n"
           "  --data <dir>          the Gizmos & Gadgets folder (ssg.exe, gizmo000..003.dat); default:\n"
           "                        ggport.ini, then GG_DATA, then next to the program\n"
           "  --pref-dir <dir>      folder for players.dat, ggport.ini and screenshots\n"
           "  --gg-args \"<sw>\"      the original's switches: -S / -SN / -S- (no sound), -SI (PC\n"
           "                        speaker), -SA / -SB / -SM (FM), -GV (ignored), -? (this help)\n"
           "  --sound off|speaker|fm  sound device (default fm: an AdLib-compatible OPL2)\n"
           "  --cpu 286|386         the CPU class the game sees (286: one Cyber Chimp fewer)\n"
           "  --seed <n>            the random seed (default: the clock, as the original)\n"
           "  --fullscreen, --windowed   start in (borderless) fullscreen or in a window;\n"
           "                        Alt+Enter and F11 toggle\n"
           "  --square-pixels       show 320x200 with square pixels instead of at 4:3\n"
           "  --scale <n>           initial window size: n x 320x240\n"
           "  --integer-scale       whole-number scale factors only\n"
           "  --no-pause            keep running while the window is inactive\n"
           "  --screenshot-dir <d>  F12 screenshots (default: <pref-dir>/screenshots)\n"
           "  --headless            no window and no sound; the virtual clock runs as fast as possible;\n"
           "                        the run ends when the --keys/--mouse script is used up (and --ticks)\n"
           "  --ticks <n>           run at least n PIT ticks (145.65 per second); in a window: end after n\n"
           "  --keys <script>       scripted input in PIT ticks, e.g. \"146:tap:enter, +73:click:160:100\"\n"
           "                        (TICK:down|up|tap:KEY[:N], TICK:move|click:X:Y, TICK:buttons:MASK; an\n"
           "                        event of tick n comes right before PIT tick n; click holds the button\n"
           "                        for one tick: use buttons:1 ... buttons:0 for a longer press)\n"
           "  --mouse <script>      the same (merged with --keys)\n"
           "  --frame-dump <path>   a folder: one PNG per new picture shown and end.png; a .png file:\n"
           "                        the last picture\n"
           "  --wav-dump <file>     headless: the sound of the run, rendered offline, as a WAV file\n"
           "  --opl-log <file>      the OPL register writes of the run ('tick reg value [c]' lines)\n"
           "  --verbose             debug messages (with every sound call)\n"
           "  --help                this text\n"
           "\n"
           "the game's diagnostic tools:\n"
           "  --gg-trace <file>     a CSV row per frame (PIT tick, g_state, rooms, the player, rand() calls)\n"
           "  --gg-trace-polls      with --gg-trace: a row per input poll too (dialogs, menus, puzzles)\n"
           "  --gg-dump-state <tick> <file>   the game's DGROUP (DS:0000..BD03) at that PIT tick, and\n"
           "                        <file>.fields.tsv, to compare with a DOSBox-X state\n"
           "  --gg-load-state <sav> start from a DOSBox-X save state of ssg.exe (the hub, the workshop, a\n"
           "                        warehouse room, a puzzle or a race) and its .players copy\n"
           "  --gg-preview          the wave 2 preview instead of the game: the attract, then Enter or\n"
           "                        Space steps through the sign-in, the hub, a warehouse room and the\n"
           "                        eight puzzle screens; number keys play the screen's effects\n"
           "\n"
           "tools (ggport-cli): --gg-info, --gg-list, --gg-selftest, --gg-extract (data); --gg-pic,\n"
           "--gg-pal, --gg-font, --gg-movie, --gg-title, --gg-fade, --gg-wipe (graphics); --gg-sound,\n"
           "--gg-oplog, --gg-dro-compare, --gg-songs, --gg-soundtest-check (sound); --gg-clock,\n"
           "--gg-input-trace (runtime); --gg-screen, --gg-ui-info (screens), --gg-rooms (world); each\n"
           "with --data <dir>.\n";
}

}  // namespace tmport::gg
