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

std::string toolsOverview() {
    return
        "Tools: tmport <command> [options]. They run without a window (except --view) and without\n"
        "sound; `tmport --help <topic>` lists the options of a group.\n"
        "\n"
        "  Game data (topic: inspect)\n"
        "    --info                             check the game files, print the inventory\n"
        "    --dump-pmap <id> <out.png>         one picture (PMAP) as a PNG\n"
        "    --dump-sheet <first> <last> <out.png>  contact sheet of the pictures in an id range\n"
        "    --headless-frame <out.png>         compose a test frame through the presenter\n"
        "    --view [id]                        picture browser (opens a window)\n"
        "  Text and dialogs (topic: text)\n"
        "    --render-dialog <name|offset> <out.png>, --render-help <topic> <page> <out.png>,\n"
        "    --render-text <othrId> <x> <y> <w> <out.png>, --render-font <out.png>,\n"
        "    --text-metrics <othrId> [<w>]\n"
        "  Animation (topic: anim)\n"
        "    --render-dam <script> <bndl> <frameBase> <tick> <out.png>, --dam-info <script>,\n"
        "    --dam-check\n"
        "  Screens, scenes and the room loop: scripted input on a simulated clock\n"
        "    --screen title|signin|attract      title / Club Sign-In (topic: screen)\n"
        "    --scene quiz <q> | hint <n> | cutscene <level> | celebrate <climbs> | rank <total> |\n"
        "            seerank <total> | castle   a modal scene of the game (topic: scene)\n"
        "    --room-run --new-player <name> | --record <users.dat> <slot|name>\n"
        "                                       climb set-up and the room loop (topic: room)\n"
        "  Audio, offline (topic: audio)\n"
        "    --dump-ssnd <id> <out.wav>, --midi-dump <file|tune id>,\n"
        "    --midi-render <file|tune id> <out.wav> --soundfont <sf2>, --make-test-soundfont <out.sf2>\n"
        "  Mods (topic: mods; docs/MODDING.md)\n"
        "    --export-mod-kit <dir>             your game's assets in editable formats\n"
        "    --check-mods [<dir> ...]           convert and check mod folders\n"
        "  1990 DOS edition (topics: dos-gfx, dos-sound, dos-autoplay; --edition dos for the mods)\n"
        "    --dos-pan, --dos-pak, --dos-screen scenery, sprites and screens to PNG\n"
        "    --dos-sound <device> sfx|tune|soundtest|all  tunes and effects to WAV (AdLib: OPL2)\n"
        "    --edition dos --autoplay           the game played headless by a bot, rules checked\n"
        "\n"
        "  Options of every tool that reads the game:\n"
        "    --data <path>        game folder or CD image (default: TM_DATA, then auto-detection)\n"
        "    --mods <dir>         also load this mod folder (repeatable, later ones win); every tool\n"
        "                         except --info, --export-mod-kit and the audio tools. The tools load\n"
        "                         only these folders, not the [mods] folders of tmport.ini\n"
        "    --16                 16-colour art (not --scene and the audio tools)\n"
        "    --verbose            debug logging\n"
        "\n"
        "Help topics: game, inspect, text, anim, screen, scene, room, audio, mods, dos-gfx, dos-sound,\n"
        "dos-autoplay, all.\n"
        "More: README.md, docs/BUILDING.md (tools and scripts), docs/MODDING.md.\n";
}

std::string inspectUsage() {
    return
        "Game data tools (tmport [--data <path>] <command> [options]):\n"
        "  --info                           check the game files: inventory, NE resources per DLL,\n"
        "                                   DGROUP validation, decode check of every PMAP, BNDL and\n"
        "                                   SSND summary (always the original files, no mods)\n"
        "  --dump-pmap <id> <out.png>       one PMAP as an indexed PNG\n"
        "  --dump-sheet <first> <last> <out.png>\n"
        "                                   contact sheet of the PMAPs in [first, last]\n"
        "  --headless-frame <out.png>       compose a test frame through the frame buffer and the\n"
        "                                   presenter and write the 640x480 canvas (no window)\n"
        "  --view [id]                      interactive PMAP browser in a window (Left/Right:\n"
        "                                   previous/next, PgUp/PgDn: -/+10, Home/End, M: mirror,\n"
        "                                   O: opaque, T: 16/256 colours, I: integer scale,\n"
        "                                   F12: screenshot, Esc: quit)\n"
        "  options:\n"
        "  --data <path>        game folder (TMTWINCD) or CD image (.cue/.bin/.iso); default: the\n"
        "                       TM_DATA environment variable, then [engine] data in tmport.ini,\n"
        "                       then auto-detection\n"
        "  --16                 the 16-colour art (TMT16.DLL) and the VGA palette\n"
        "  --opaque             dumps: do not make the key colour transparent\n"
        "  --fb-dump <png>      with --headless-frame: also write the whole 512x1055 frame buffer\n"
        "  --mods <dir>         also load this mod folder (repeatable; later ones win)\n"
        "  --verbose            debug logging\n"
        "  --view only: --integer-scale, --screenshot-dir <dir>, --config <ini>,\n"
        "               --input-mode lossless|first-event, --tap-latch (as for the game)\n";
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
            if (i + 1 < argc && argv[i + 1][0] != '-') o.helpTopic = argv[++i];
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
            // Accepted like the game's option; the viewer never pauses on focus loss anyway.
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
