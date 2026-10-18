#include "cli/options.h"

#include <charconv>
#include <format>
#include <string_view>

namespace tmport::cli {

namespace {

bool parseInt(std::string_view s, int& out) {
    if (s.empty()) return false;
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
        base = 16;
    }
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out, base);
    return ec == std::errc() && p == s.data() + s.size();
}

}  // namespace

std::string usage() {
    return
        "tmport - Treasure Mountain! (Windows CD) engine, milestone 1 tools\n"
        "\n"
        "Usage: tmport [--data <dir|cue|bin|iso>] <command> [options]\n"
        "\n"
        "Commands:\n"
        "  --info                           print the file/resource inventory and validation\n"
        "  --dump-pmap <id> <out.png>       write one PMAP as an indexed PNG (headless)\n"
        "  --dump-sheet <first> <last> <out.png>\n"
        "                                   contact sheet of the PMAPs in [first, last] (headless)\n"
        "  --headless-frame <out.png>       compose a test frame and write the 640x480 canvas\n"
        "                                   (no window is created)\n"
        "  --view [id]                      interactive PMAP browser (Left/Right: previous/next,\n"
        "                                   PgUp/PgDn: -/+10, Home/End, M: mirror, O: opaque,\n"
        "                                   T: 16/256 colours, I: integer scale, F12: screenshot,\n"
        "                                   Esc: quit)\n"
        "  --help                           this text\n"
        "\n"
        "Options:\n"
        "  --data <path>        game folder (TMTWINCD) or CD image (.cue/.bin/.iso);\n"
        "                       default: environment variable TM_DATA, then auto-detection\n"
        "  --16                 use the 16-colour art (TMT16.DLL) and VGA palette\n"
        "  --opaque             dumps: do not make the key colour transparent\n"
        "  --fb-dump <png>      with --headless-frame: also dump the whole 512x1055 buffer\n"
        "  --integer-scale      window: integer scaling only\n"
        "  --screenshot-dir <d> F12 screenshot directory (default: the preference directory)\n"
        "  --config <ini>       engine settings file (default: tmport.ini next to the executable,\n"
        "                       else in the preference directory)\n"
        "  --input-mode <m>     lossless (default) or first-event (the original drops all but the\n"
        "                       first input event of each message drain)\n"
        "  --tap-latch          arrow taps shorter than one tick are not missed\n"
        "  --no-pause           keep running while the window is unfocused\n"
        "  --mods <dir>         also load this mod folder (repeatable; later ones win)\n"
        "  --verbose            debug logging\n";
}

std::optional<Options> parseArgs(int argc, char** argv, std::string& error) {
    Options o;
    bool haveCommand = false;
    auto setCommand = [&](Command c) {
        if (haveCommand) {
            error = "only one command may be given";
            return false;
        }
        o.command = c;
        haveCommand = true;
        return true;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto need = [&](int n) {
            if (i + n >= argc) {
                error = std::format("{} needs {} argument(s)", a, n);
                return false;
            }
            return true;
        };
        auto intArg = [&](int idx, int& out) {
            if (!parseInt(argv[idx], out)) {
                error = std::format("{}: '{}' is not a number", a, argv[idx]);
                return false;
            }
            return true;
        };
        if (a == "--help" || a == "-h" || a == "/?") {
            if (!setCommand(Command::Help)) return std::nullopt;
        } else if (a == "--data") {
            if (!need(1)) return std::nullopt;
            o.dataPath = argv[++i];
        } else if (a == "--info") {
            if (!setCommand(Command::Info)) return std::nullopt;
        } else if (a == "--dump-pmap") {
            if (!need(2) || !setCommand(Command::DumpPmap) || !intArg(i + 1, o.pmapId)) return std::nullopt;
            o.outPath = argv[i + 2];
            i += 2;
        } else if (a == "--dump-sheet") {
            if (!need(3) || !setCommand(Command::DumpSheet) || !intArg(i + 1, o.firstId) || !intArg(i + 2, o.lastId))
                return std::nullopt;
            o.outPath = argv[i + 3];
            i += 3;
        } else if (a == "--headless-frame") {
            if (!need(1) || !setCommand(Command::HeadlessFrame)) return std::nullopt;
            o.outPath = argv[++i];
        } else if (a == "--view") {
            if (!setCommand(Command::View)) return std::nullopt;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                if (!intArg(i + 1, o.pmapId)) return std::nullopt;
                ++i;
            }
        } else if (a == "--16") {
            o.colour16 = true;
        } else if (a == "--opaque") {
            o.opaque = true;
        } else if (a == "--integer-scale") {
            o.integerScale = true;
        } else if (a == "--verbose") {
            o.verbose = true;
        } else if (a == "--fb-dump") {
            if (!need(1)) return std::nullopt;
            o.fbDumpPath = argv[++i];
        } else if (a == "--screenshot-dir") {
            if (!need(1)) return std::nullopt;
            o.screenshotDir = argv[++i];
        } else if (a == "--config") {
            if (!need(1)) return std::nullopt;
            o.configPath = argv[++i];
        } else if (a == "--input-mode") {
            if (!need(1)) return std::nullopt;
            o.inputMode = argv[++i];
            if (o.inputMode != "lossless" && o.inputMode != "first-event") {
                error = std::format("--input-mode: '{}' is not lossless or first-event", o.inputMode);
                return std::nullopt;
            }
        } else if (a == "--tap-latch") {
            o.tapLatch = true;
        } else if (a == "--no-pause") {
            o.noPause = true;
        } else if (a == "--mods") {
            if (!need(1)) return std::nullopt;
            o.modDirs.emplace_back(argv[++i]);
        } else {
            error = std::format("unknown argument '{}'", a);
            return std::nullopt;
        }
    }
    if (o.command == Command::DumpSheet && o.lastId < o.firstId) {
        error = "--dump-sheet: last id is smaller than first id";
        return std::nullopt;
    }
    return o;
}

}  // namespace tmport::cli
