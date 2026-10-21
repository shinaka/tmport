#include "mods/mods.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <system_error>

#include "audio/audio_tables.h"
#include "mischief/util/file_util.h"
#include "mischief/util/log.h"
#include "mods/mod_common.h"
#include "mods/mod_data_source.h"
#include "mods/mod_folder.h"
#include "mods/resource_import.h"
#include "mods/table_overrides.h"

namespace tmport::mods {

namespace {

bool sameDir(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::exists(a, ec) && std::filesystem::exists(b, ec)) return std::filesystem::equivalent(a, b, ec);
    return a.lexically_normal() == b.lexically_normal();
}

bool truthy(std::string_view v) {
    const std::string l = mischief::toLowerAscii(trimView(v));
    return !(l == "false" || l == "0" || l == "no" || l == "off");
}

}  // namespace

std::vector<std::filesystem::path> modDirsFromIni(const mischief::save::IniFile* ini, const std::filesystem::path& iniDir,
                                                  const std::filesystem::path& prefDir, const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> dirs;
    if (ini)
        if (auto en = ini->get("mods", "enabled"); en && !truthy(*en)) return dirs;
    auto add = [&](const std::filesystem::path& p) {
        if (p.empty()) return;
        for (const auto& d : dirs)
            if (sameDir(d, p)) return;
        dirs.push_back(p);
    };
    if (ini)
        if (auto paths = ini->get("mods", "paths")) {
            std::string_view s = *paths;
            while (!s.empty()) {
                const size_t semi = s.find(';');
                const std::string_view part = trimView(s.substr(0, semi));
                s = semi == std::string_view::npos ? std::string_view() : s.substr(semi + 1);
                if (part.empty()) continue;
                std::filesystem::path p = mischief::pathFromUtf8(part);
                if (p.is_relative()) p = iniDir / p;
                add(p);
            }
            return dirs;
        }
    if (!prefDir.empty()) add(prefDir / "mods");
    if (!exeDir.empty()) add(exeDir / "mods");
    return dirs;
}

std::vector<std::filesystem::path> configuredModDirs(const save::Paths& paths) {
    std::optional<mischief::save::IniFile> ini;
    if (!paths.iniPath.empty()) ini = mischief::save::IniFile::load(paths.iniPath);
    return modDirsFromIni(ini ? &*ini : nullptr, paths.iniPath.parent_path(), paths.prefDir, paths.exeDir);
}

ModsReport applyMods(GameFiles& files, const std::vector<std::filesystem::path>& dirs) {
    ModsReport rep;
    std::vector<ModFolder> folders;
    for (const auto& d : dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(d, ec)) {
            mischief::log::debug("mods: {} does not exist (skipped)", mischief::pathToUtf8(d));
            continue;
        }
        ModFolder f = scanModFolder(d);
        for (auto& w : f.warnings) rep.warnings.push_back(std::move(w));
        for (auto& e : f.errors) rep.errors.push_back(std::move(e));
        f.warnings.clear();
        f.errors.clear();
        rep.folders.push_back(d);
        folders.push_back(std::move(f));
    }
    if (folders.empty()) return rep;

    // 1. Table overrides (folders in order: later files win).
    for (const auto& f : folders)
        for (const auto& t : f.tableFiles) {
            const std::string where = mischief::pathToUtf8(t);
            try {
                const auto text = mischief::readWholeFile(t);
                if (!text) throw mischief::FormatError("cannot read the file");
                const auto r = applyTablesText(*files.exe, std::string_view(reinterpret_cast<const char*>(text->data()), text->size()));
                std::string names;
                for (const auto& n : r.tables) names += (names.empty() ? "" : ", ") + n;
                mischief::log::info("mods: {}: tables {} ({} byte(s) changed, {} string(s) replaced)", where, names, r.changedBytes, r.repointedStrings);
                for (const auto& w : r.warnings) rep.warnings.push_back(where + ": " + w);
                ++rep.tableFiles;
            } catch (const std::exception& e) {
                rep.errors.push_back(std::format("{}: {} (the file is ignored)", where, e.what()));
            }
        }

    // 2. MIDI tunes: SOUNDS\<B|E>name.MID per tune id; set-specific files beat <id>.mid of the same
    //    folder, later folders beat earlier ones.
    std::map<std::string, std::filesystem::path> midi;
    {
        const audio::AudioTables at = audio::AudioTables::fromExe(*files.exe);
        for (size_t i = 0; i < audio::AudioTables::kTunes; ++i) {
            const int id = at.tuneIds[i];
            if (at.tuneFiles[i].empty()) continue;
            std::string e = at.tuneFiles[i], b = at.tuneFiles[i];
            e[0] = 'E';
            b[0] = 'B';
            const std::string ePath = at.soundsDir + e, bPath = at.soundsDir + b;
            for (const auto& f : folders) {
                for (int pass = 0; pass < 2; ++pass)
                    for (const auto& mf : f.files) {
                        if (mf.id != id) continue;
                        const bool generic = mf.kind == ModFileKind::Midi;
                        if ((pass == 0) != generic) continue;
                        if (mf.kind != ModFileKind::Midi && mf.kind != ModFileKind::MidiE && mf.kind != ModFileKind::MidiB) continue;
                        try {
                            const auto bytes = mischief::readWholeFile(mf.path);
                            if (!bytes) throw mischief::FormatError("cannot read the file");
                            validateMidiFile(id, *bytes);
                        } catch (const std::exception& ex) {
                            rep.errors.push_back(std::format("{}: {} (the game's own tune is used)", mischief::pathToUtf8(mf.path), ex.what()));
                            continue;
                        }
                        if (mf.kind != ModFileKind::MidiB) midi[ModDataSource::key(ePath)] = mf.path;
                        if (mf.kind != ModFileKind::MidiE) midi[ModDataSource::key(bPath)] = mf.path;
                    }
            }
        }
        for (const auto& f : folders)
            for (const auto& mf : f.files)
                if ((mf.kind == ModFileKind::Midi || mf.kind == ModFileKind::MidiE || mf.kind == ModFileKind::MidiB) &&
                    at.tuneIndex(mf.id) < 0)
                    rep.errors.push_back(std::format("{}: music ids are 9000-9009 (the file is ignored)", mischief::pathToUtf8(mf.path)));
    }
    rep.tunes = midi.size();
    if (!midi.empty()) files.source = std::make_unique<ModDataSource>(std::move(files.source), midi);

    // 3. Resource overrides.
    files.res->mods().setFolders(std::move(folders));
    files.res->clearCache();
    rep.resourceOverrides = files.res->mods().overriddenIds().size();
    for (const auto& s : files.res->mods().shadowed()) mischief::log::debug("mods: {}", s);

    std::string names;
    for (const auto& d : rep.folders) names += (names.empty() ? "" : ", ") + mischief::pathToUtf8(d);
    mischief::log::info("mods: {} folder(s) [{}]: {} resource override(s), {} table file(s), {} MIDI file(s)", rep.folders.size(), names,
              rep.resourceOverrides, rep.tableFiles, rep.tunes);
    for (const auto& w : rep.warnings) mischief::log::warn("mods: {}", w);
    for (const auto& e : rep.errors) mischief::log::error("mods: {}", e);
    return rep;
}

void validateInstalledMods(const GameFiles& files, ModsReport& report) {
    const ResourceManager& res = *files.res;
    for (const ModFolder& f : res.mods().folders()) {
        for (const ModFile& mf : f.files) {
            const std::string where = mischief::pathToUtf8(mf.path);
            try {
                if (!resTypeOf(mf.kind)) {
                    const auto bytes = mischief::readWholeFile(mf.path);
                    if (!bytes) throw mischief::FormatError("cannot read the file");
                    validateMidiFile(mf.id, *bytes);
                    continue;
                }
                const ImportResult r = importResourceFile(mf, importContextFor(res, &f.manifest));
                for (const auto& w : r.warnings) report.warnings.push_back(where + ": " + w);
            } catch (const std::exception& e) {
                report.errors.push_back(where + ": " + e.what());
            }
        }
    }
    for (const auto& s : res.mods().shadowed()) report.warnings.push_back(s);
}

ModComparison compareWithOriginals(const GameFiles& files) {
    ModComparison c;
    const ResourceManager& res = *files.res;
    const auto* overlay = dynamic_cast<const ModDataSource*>(files.source.get());
    const mischief::vfs::DataSource& base = overlay ? overlay->base() : *files.source;
    const audio::AudioTables at = audio::AudioTables::fromExe(*files.exe);
    for (const ModFolder& f : res.mods().folders()) {
        for (const ModFile& mf : f.files) {
            const std::string what = std::format("{} {} ({})", resTypeOf(mf.kind) ? std::string(resTypeName(*resTypeOf(mf.kind))) : std::string("MIDI"),
                                                 mf.id, mischief::pathToUtf8(mf.path));
            const auto t = resTypeOf(mf.kind);
            if (!t) continue;  // MIDI: compared per served file below
            const mischief::gfx::ColorMode mode = mf.kind == ModFileKind::Pmap16 ? mischief::gfx::ColorMode::Colour16 : mischief::gfx::ColorMode::Colour256;
            const auto win = res.mods().lookup(*t, mf.id, mode);
            if (!win || win->file != &mf) continue;  // shadowed by another file
            try {
                const ImportResult r = importResourceFile(mf, importContextFor(res, &f.manifest));
                const auto orig = res.loadOriginal(*t, mf.id, mode);
                const bool same = orig && *orig == r.body;
                (same ? c.identical : c.different) += 1;
                if (!same) c.differing.push_back(what);
            } catch (const std::exception&) {
                ++c.failed;
            }
        }
    }
    // MIDI: every game file the overlay serves from a mod.
    if (overlay)
        for (size_t i = 0; i < audio::AudioTables::kTunes; ++i) {
            if (at.tuneFiles[i].empty()) continue;
            for (const char set : {'E', 'B'}) {
                std::string name = at.tuneFiles[i];
                name[0] = set;
                const std::string path = at.soundsDir + name;
                if (!overlay->overrides(path)) continue;
                const auto a = overlay->open(path), b = base.open(path);
                const bool same = a && b && a->readAll() == b->readAll();
                (same ? c.identical : c.different) += 1;
                if (!same) c.differing.push_back(std::format("MIDI {} ({})", at.tuneIds[i], path));
            }
        }
    const auto pristine = ExeTables::load(base);
    const auto a = pristine->raw(), b = files.exe->raw();
    c.tablesIdentical = a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    return c;
}

}  // namespace tmport::mods
