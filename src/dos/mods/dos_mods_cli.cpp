#include "dos/mods/dos_mods_cli.h"

#include <SDL3/SDL_filesystem.h>

#include <chrono>
#include <cstdio>
#include <format>

#include "dos/data/dos_files.h"
#include "dos/mods/dos_mod_kit.h"
#include "dos/mods/dos_mods.h"
#include "edition/edition.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"

namespace tmport::dos {

namespace {

template <class... Args>
void out(std::format_string<Args...> fmt, Args&&... args) {
    const std::string s = std::format(fmt, std::forward<Args>(args)...);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

}  // namespace

std::optional<std::filesystem::path> dosModsDataPath(const std::string& dataArg, const save::Paths& paths,
                                                     const std::filesystem::path& exeDir) {
    std::filesystem::path p;
    std::optional<std::string> iniData;
    if (!paths.iniPath.empty())
        if (auto ini = mischief::save::IniFile::load(paths.iniPath)) iniData = ini->get("dos", "data");
    if (!dataArg.empty()) {
        p = mischief::pathFromUtf8(dataArg);
    } else if (auto env = mischief::getEnv("TM_DATA_DOS"); env && !env->empty()) {
        p = mischief::pathFromUtf8(*env);
    } else if (iniData && !iniData->empty()) {
        p = mischief::pathFromUtf8(*iniData);
    } else if (auto d = autoDetectDosPath(exeDir)) {
        p = *d;
    } else {
        return std::nullopt;
    }
    return detectEdition(p, Edition::Dos1990).path;  // the folder holding SST.EXE; throws EditionError
}

int runDosModsCommand(const DosModsCommand& c) {
    const char* base = SDL_GetBasePath();
    std::optional<std::filesystem::path> folder;
    try {
        folder = dosModsDataPath(c.dataArg, c.paths, base ? mischief::pathFromUtf8(base) : std::filesystem::path());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
    if (!folder) {
        std::fprintf(stderr, "tmport: the 1990 DOS edition was not found. Pass --data <folder with SST.EXE> or set TM_DATA_DOS.\n");
        return 2;
    }
    try {
        const DosFiles originals = DosFiles::open(*folder, nullptr);
        if (c.exportKit) {
            const auto t0 = std::chrono::steady_clock::now();
            out("Exporting the DOS mod kit of {} to {}{}\n", mischief::pathToUtf8(*folder), c.outDir, c.verify ? " (with round-trip check)" : "");
            DosModKitOptions o;
            o.force = c.force;
            o.verify = c.verify;
            const DosModKitReport r = exportDosModKit(originals, mischief::pathFromUtf8(c.outDir), o);
            for (const auto& [g, n] : r.counts) out("  {:<7} {:>5} files\n", g, n);
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            out("{} files, {:.1f} MB in {:.1f} s\n", r.files, static_cast<double>(r.bytes) / (1024.0 * 1024.0), secs);
            if (c.verify) {
                out("encoders: PAK {} of {} frames, PAN {} of {} files reproduced byte for byte\n", r.framesReproduced, r.frames,
                    r.picturesReproduced, r.pictureFiles);
                out("round trip: {} files byte-exact (the data files and SST.EXE's DGROUP), {} failure(s)\n", r.verified, r.failures.size());
            }
            for (const auto& f : r.failures) out("  FAIL: {}\n", f);
            out("The kit is your private copy of the game's data: keep it to yourself.\n");
            return r.failures.empty() ? 0 : 1;
        }

        std::vector<std::filesystem::path> dirs = c.modDirs;
        if (dirs.empty()) dirs = configuredDosModDirs(c.paths);
        std::vector<DosModFolder> folders;
        for (const auto& d : dirs) {
            std::error_code ec;
            if (std::filesystem::is_directory(d, ec)) folders.push_back(scanDosModFolder(d));
        }
        DosExe exe = originals.exe();
        DosFileSet files = originals.fileSet();
        const DosModsReport rep = applyDosModFolders(folders, exe, files);
        out("DOS mod folders ({}):\n", rep.folders.size());
        for (const auto& d : rep.folders) out("  {}\n", mischief::pathToUtf8(d));
        if (rep.folders.empty()) {
            out("  (none found; looked in:");
            for (const auto& d : dirs) out(" {}", mischief::pathToUtf8(d));
            out(")\n");
        }
        std::string changed;
        for (const auto& f : rep.changedFiles) changed += (changed.empty() ? "" : ", ") + f;
        out("{} mod file(s), {} applied; data files changed: {}; SST.EXE strings/colours {}\n", rep.modFiles, rep.applied,
            changed.empty() ? "none" : changed, rep.exeChanged ? "changed" : "unchanged");
        for (const auto& w : rep.warnings) out("  warning: {}\n", w);
        for (const auto& e : rep.errors) out("  ERROR: {}\n", e);
        if (c.compare) {
            size_t same = 0, different = 0;
            for (int i = 0; i < FileTable::kCount; ++i) {
                if (static_cast<FileIndex>(i) == FileIndex::UsersDat) continue;
                (files[static_cast<size_t>(i)] == originals.fileSet()[static_cast<size_t>(i)] ? same : different) += 1;
            }
            out("compared with the game's data: {} file(s) identical, {} different; SST.EXE DGROUP {}\n", same, different,
                rep.exeChanged ? "changed" : "identical");
            for (const auto& f : rep.changedFiles) out("  differs: {}\n", f);
        }
        out("Result: {}\n", rep.errors.empty() ? "OK" : "PROBLEMS FOUND");
        return rep.errors.empty() ? 0 : 1;
    } catch (const mischief::MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
}

}  // namespace tmport::dos
