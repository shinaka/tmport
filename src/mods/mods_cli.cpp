#include "mods/mods_cli.h"

#include <SDL3/SDL_filesystem.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <string_view>
#include <vector>

#include "dos/mods/dos_mods_cli.h"
#include "edition/edition.h"
#include "mods/mod_kit.h"
#include "mods/mods.h"
#include "res/game_files.h"
#include "save/settings.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"
#include "vfs/data_source.h"

namespace tmport::mods {

namespace {

template <class... Args>
void out(std::format_string<Args...> fmt, Args&&... args) {
    const std::string s = std::format(fmt, std::forward<Args>(args)...);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

std::filesystem::path exeDirectory() {
    const char* base = SDL_GetBasePath();
    return base ? pathFromUtf8(base) : std::filesystem::path();
}

std::optional<std::filesystem::path> dataPathFrom(const std::string& arg, const save::Paths& paths) {
    if (!arg.empty()) return pathFromUtf8(arg);
    if (auto env = getEnv("TM_DATA")) return pathFromUtf8(*env);
    if (!paths.iniPath.empty())
        if (auto ini = save::IniFile::load(paths.iniPath))
            if (auto d = ini->get("engine", "data"); d && !d->empty()) return pathFromUtf8(*d);
    return vfs::autoDetectDataPath(exeDirectory());
}

// --edition dos, or (no --edition) a --data path that holds only the DOS edition. Without either,
// the commands work on the Windows edition exactly as before.
bool isDosEdition(const std::string& edition, const std::string& dataArg) {
    if (!edition.empty() && edition != "auto") return parseEditionKey(edition) == Edition::Dos1990;
    if (dataArg.empty()) return false;
    const EditionScan scan = scanEditions(pathFromUtf8(dataArg));
    return scan.dos && !scan.win;
}

}  // namespace

std::string modsUsage() {
    return "\nMods (docs/MODDING.md):\n"
           "  --export-mod-kit <dir>           write your game's assets in editable formats (PNG, WAV,\n"
           "                                   text, DAM assembler, JSON, MIDI) into <dir>; every file is\n"
           "                                   re-imported and compared byte for byte\n"
           "      --force                      allow a non-empty <dir> (files are overwritten)\n"
           "      --no-verify                  skip the round-trip check\n"
           "  --check-mods [<dir> ...]         convert and check every file of the given mod folders\n"
           "                                   (default: the folders configured in tmport.ini [mods])\n"
           "      --compare                    also compare every override with the game's own data\n"
           "  --mods <dir>                     also load this mod folder; may be repeated (later ones win).\n"
           "                                   Works with the game and every tool that reads the game\n"
           "                                   files except --info, --export-mod-kit and the audio\n"
           "                                   tools; with --check-mods it names a folder to check\n"
           "  --edition dos                    the 1990 DOS edition's kit and mod folders (also chosen when\n"
           "                                   --data names its folder; default data: TM_DATA_DOS, [dos] data)\n";
}

std::optional<int> runModsCommand(int argc, char** argv) {
    enum class Cmd { None, Export, Check } cmd = Cmd::None;
    std::string outDir, dataPath, configPath, edition;
    std::vector<std::string> dirs;
    bool force = false, verify = true, verbose = false, colour16 = false, compare = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--export-mod-kit") cmd = Cmd::Export;
        if (a == "--check-mods") cmd = Cmd::Check;
    }
    if (cmd == Cmd::None) return std::nullopt;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto need = [&]() -> bool {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "tmport: %s needs an argument\n", argv[i]);
                return false;
            }
            return true;
        };
        if (a == "--export-mod-kit") {
            if (!need()) return 2;
            outDir = argv[++i];
        } else if (a == "--check-mods") {
            while (i + 1 < argc && argv[i + 1][0] != '-') dirs.emplace_back(argv[++i]);
        } else if (a == "--data") {
            if (!need()) return 2;
            dataPath = argv[++i];
        } else if (a == "--config") {
            if (!need()) return 2;
            configPath = argv[++i];
        } else if (a == "--mods") {
            if (!need()) return 2;
            dirs.emplace_back(argv[++i]);
        } else if (a == "--force") {
            force = true;
        } else if (a == "--no-verify") {
            verify = false;
        } else if (a == "--verbose") {
            verbose = true;
        } else if (a == "--16") {
            colour16 = true;
        } else if (a == "--compare") {
            compare = true;
        } else if (a == "--edition") {
            if (!need()) return 2;
            edition = argv[++i];
        } else {
            std::fprintf(stderr, "tmport: unknown argument '%s' for the mods tools\n%s", argv[i], modsUsage().c_str());
            return 2;
        }
    }
    log::setVerbose(verbose);
    save::Paths paths = save::Paths::detect();
    if (!configPath.empty()) paths.iniPath = pathFromUtf8(configPath);
    // The 1990 DOS edition (dos/mods/dos_mods_cli.h): --edition dos, or --data naming a DOS folder.
    if (!edition.empty() && edition != "auto" && !parseEditionKey(edition)) {
        std::fprintf(stderr, "tmport: --edition takes win, dos or auto, not '%s'\n", edition.c_str());
        return 2;
    }
    if (isDosEdition(edition, dataPath)) {
        if (colour16) {
            std::fprintf(stderr, "tmport: --16 is an option of the Windows edition\n");
            return 2;
        }
        dos::DosModsCommand dc;
        dc.exportKit = cmd == Cmd::Export;
        dc.outDir = outDir;
        dc.dataArg = dataPath;
        for (const auto& d : dirs) dc.modDirs.push_back(pathFromUtf8(d));
        dc.force = force;
        dc.verify = verify;
        dc.compare = compare;
        dc.paths = paths;
        return dos::runDosModsCommand(dc);
    }
    const auto data = dataPathFrom(dataPath, paths);
    if (!data) {
        std::fprintf(stderr, "tmport: the game data was not found. Pass --data <TMTWINCD folder or CD image> or set TM_DATA.\n");
        return 2;
    }
    try {
        ResourceManager::Options ro;
        ro.colorMode = colour16 ? gfx::ColorMode::Colour16 : gfx::ColorMode::Colour256;
        ro.requireSounds = cmd == Cmd::Export;
        ro.cachePixmaps = false;
        GameFiles files = GameFiles::open(*data, ro);
        if (cmd == Cmd::Export) {
            const auto t0 = std::chrono::steady_clock::now();
            out("Exporting the mod kit of {} to {}{}\n", files.source->describe(), outDir, verify ? " (with round-trip check)" : "");
            ModKitOptions mo;
            mo.force = force;
            mo.verify = verify;
            const ModKitReport r = exportModKit(files, pathFromUtf8(outDir), mo);
            for (const auto& [g, n] : r.counts) out("  {:<7} {:>5} files\n", g, n);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            out("{} files, {:.1f} MB in {:.1f} s\n", r.files, static_cast<double>(r.bytes) / (1024.0 * 1024.0), secs);
            if (verify) out("round trip: {} files byte-exact, {} failure(s)\n", r.verified, r.failures.size());
            for (const auto& f : r.failures) out("  FAIL: {}\n", f);
            out("The kit is your private copy of the game's data: keep it to yourself.\n");
            return r.failures.empty() ? 0 : 1;
        }
        std::vector<std::filesystem::path> modDirs;
        for (const auto& d : dirs) modDirs.push_back(pathFromUtf8(d));
        if (modDirs.empty()) modDirs = configuredModDirs(paths);
        ModsReport rep = applyMods(files, modDirs);
        validateInstalledMods(files, rep);
        out("Mod folders ({}):\n", rep.folders.size());
        for (const auto& d : rep.folders) out("  {}\n", pathToUtf8(d));
        if (rep.folders.empty()) {
            out("  (none found; looked in:");
            for (const auto& d : modDirs) out(" {}", pathToUtf8(d));
            out(")\n");
        }
        out("{} resource override(s), {} table file(s), {} MIDI file(s)\n", rep.resourceOverrides, rep.tableFiles, rep.tunes);
        for (const auto& w : rep.warnings) out("  warning: {}\n", w);
        for (const auto& e : rep.errors) out("  ERROR: {}\n", e);
        if (compare) {
            const ModComparison c = compareWithOriginals(files);
            out("compared with the game's data: {} identical, {} different, {} not convertible; EXE tables {}\n", c.identical,
                c.different, c.failed, c.tablesIdentical ? "identical" : "changed");
            for (const auto& d : c.differing) out("  differs: {}\n", d);
        }
        out("Result: {}\n", rep.errors.empty() ? "OK" : "PROBLEMS FOUND");
        return rep.errors.empty() ? 0 : 1;
    } catch (const MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
}

}  // namespace tmport::mods
