#include "dos/mods/dos_mod_folder.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <system_error>

#include "mods/json.h"
#include "mods/mod_common.h"
#include "util/error.h"
#include "util/file_util.h"

namespace tmport::dos {

namespace {

std::optional<int> parseIndex(std::string_view s) {
    if (s.empty() || s.size() > 5) return std::nullopt;
    int v = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || p != s.data() + s.size() || v < 0) return std::nullopt;
    return v;
}

bool sameDir(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::exists(a, ec) && std::filesystem::exists(b, ec)) return std::filesystem::equivalent(a, b, ec);
    return a.lexically_normal() == b.lexically_normal();
}

bool truthy(std::string_view v) {
    const std::string l = toLowerAscii(mods::trimView(v));
    return !(l == "false" || l == "0" || l == "no" || l == "off");
}

std::vector<std::filesystem::path> regularFiles(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

// "<NAME>.txt" -> "NAME" (upper case), nullopt for another extension.
std::optional<std::string> stemOfTxt(const std::filesystem::path& p) {
    const std::string fn = pathToUtf8(p.filename());
    if (!toLowerAscii(fn).ends_with(".txt") || fn.size() <= 4) return std::nullopt;
    return toUpperAscii(fn.substr(0, fn.size() - 4));
}

}  // namespace

bool DosModManifest::frameResizable(std::string_view file, int frame) const {
    return std::any_of(resizable.begin(), resizable.end(),
                       [&](const DosResizeRule& r) { return iequalsAscii(r.file, file) && frame >= r.lo && frame <= r.hi; });
}

DosModManifest parseDosManifest(std::string_view text) {
    const mods::json::Value v = mods::json::parse(text);
    if (!v.isObject()) throw FormatError("mod.json must hold a JSON object");
    DosModManifest m;
    if (const auto* n = v.find("name")) {
        if (!n->isString()) throw FormatError("mod.json: \"name\" must be a string");
        m.name = n->asString();
    }
    if (const auto* r = v.find("resizable")) {
        if (!r->isArray()) throw FormatError("mod.json: \"resizable\" must be an array like [\"SPRITES.PAK:72\", \"BEGIN.PAK:31-35\"]");
        for (const auto& e : r->asArray()) {
            if (!e.isString()) throw FormatError("mod.json: \"resizable\" holds strings like \"SPRITES.PAK:72\"");
            const std::string& s = e.asString();
            const size_t colon = s.find(':');
            if (colon == std::string::npos) throw FormatError(std::format("mod.json: \"resizable\": '{}' is not FILE:frames", s));
            DosResizeRule rule;
            rule.file = toUpperAscii(s.substr(0, colon));
            const std::string range = s.substr(colon + 1);
            if (range == "*") {
                rule.lo = 0;
                rule.hi = 0x7FFF;
            } else {
                const size_t dash = range.find('-');
                const auto lo = parseIndex(std::string_view(range).substr(0, dash));
                const auto hi = dash == std::string::npos ? lo : parseIndex(std::string_view(range).substr(dash + 1));
                if (!lo || !hi || *hi < *lo) throw FormatError(std::format("mod.json: \"resizable\": '{}' is not FILE:N, FILE:N-M or FILE:*", s));
                rule.lo = *lo;
                rule.hi = *hi;
            }
            m.resizable.push_back(std::move(rule));
        }
    }
    if (const auto* e = v.find("edition"); e && !(e->isString() && iequalsAscii(e->asString(), "dos")))
        throw FormatError("mod.json: \"edition\" is \"dos\" for a mod of the 1990 DOS edition");
    return m;
}

DosModFolder scanDosModFolder(const std::filesystem::path& dir) {
    DosModFolder f;
    f.dir = dir;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return f;
    f.manifest.name = pathToUtf8(dir.filename());
    auto ignored = [&](const std::filesystem::path& p, const std::string& expected) {
        f.warnings.push_back(std::format("{}: ignored (expected {})", pathToUtf8(p), expected));
    };
    for (const auto& top : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = pathToUtf8(top.path().filename());
        if (top.is_regular_file(ec)) {
            if (iequalsAscii(name, "mod.json")) {
                try {
                    const auto text = readWholeFile(top.path());
                    if (!text) throw FormatError("cannot read the file");
                    DosModManifest m = parseDosManifest(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()));
                    if (m.name.empty()) m.name = f.manifest.name;
                    f.manifest = std::move(m);
                } catch (const std::exception& e) {
                    f.errors.push_back(std::format("{}: {}", pathToUtf8(top.path()), e.what()));
                }
            }
            continue;  // README files at the top level are ignored
        }
        if (!top.is_directory(ec)) continue;
        const std::string upper = toUpperAscii(name);
        if (upper == "PAK" || upper == "PAN") {
            const bool pak = upper == "PAK";
            for (const auto& e : std::filesystem::directory_iterator(top.path(), ec)) {
                const std::string en = pathToUtf8(e.path().filename());
                if (e.is_directory(ec)) {
                    const std::string target = toUpperAscii(en) + (pak ? ".PAK" : ".PAN");
                    for (const auto& p : regularFiles(e.path())) {
                        const std::string fn = toLowerAscii(pathToUtf8(p.filename()));
                        const auto idx = fn.ends_with(".png") ? parseIndex(std::string_view(fn).substr(0, fn.size() - 4)) : std::nullopt;
                        if (!idx) {
                            ignored(p, pak ? "PAK/<STEM>/<frame>.png" : "PAN/<STEM>/<picture>.png");
                            continue;
                        }
                        f.files.push_back({pak ? DosModFileKind::PakFrame : DosModFileKind::PanPicture, target, *idx, p});
                    }
                } else if (pak && toLowerAscii(en).ends_with(".json") && en.size() > 5) {
                    f.files.push_back({DosModFileKind::PakHeaders, toUpperAscii(en.substr(0, en.size() - 5)) + ".PAK", -1, e.path()});
                } else if (e.is_regular_file(ec)) {
                    ignored(e.path(), pak ? "PAK/<STEM>/ folders and PAK/<STEM>.json" : "PAN/<STEM>/ folders");
                }
            }
        } else if (upper == "SOUND" || upper == "TEXT") {
            for (const auto& p : regularFiles(top.path())) {
                const auto stem = stemOfTxt(p);
                if (!stem) {
                    ignored(p, upper == "SOUND" ? "SOUND/<NAME>.txt, e.g. SOUND/TUNES.TAN.txt" : "TEXT/<NAME>.txt, e.g. TEXT/ALL_PUZS.txt");
                    continue;
                }
                f.files.push_back({upper == "SOUND" ? DosModFileKind::SoundText : DosModFileKind::TextFile, *stem, -1, p});
            }
        } else if (upper == "EXE") {
            for (const auto& p : regularFiles(top.path())) {
                const std::string fn = toLowerAscii(pathToUtf8(p.filename()));
                if (fn == "strings.json")
                    f.files.push_back({DosModFileKind::Strings, "SST.EXE", -1, p});
                else if (fn == "palettes.json")
                    f.files.push_back({DosModFileKind::Palettes, "SST.EXE", -1, p});
                else
                    ignored(p, "EXE/strings.json or EXE/palettes.json");
            }
        } else if (upper == "FILES") {
            for (const auto& p : regularFiles(top.path()))
                f.files.push_back({DosModFileKind::RawFile, toUpperAscii(pathToUtf8(p.filename())), -1, p});
        } else {
            f.warnings.push_back(std::format("{}: unknown folder, ignored (a DOS mod has PAK, PAN, SOUND, TEXT, EXE and FILES; "
                                             "PMAP, SSND, ... belong to the Windows edition)",
                                             pathToUtf8(top.path())));
        }
    }
    std::stable_sort(f.files.begin(), f.files.end(), [](const DosModFile& a, const DosModFile& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.target != b.target) return a.target < b.target;
        return a.index < b.index;
    });
    return f;
}

std::vector<std::filesystem::path> dosModDirsFromIni(const save::IniFile* ini, const std::filesystem::path& iniDir,
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
        if (auto paths = ini->get("mods", "dos_paths")) {
            std::string_view s = *paths;
            while (!s.empty()) {
                const size_t semi = s.find(';');
                const std::string_view part = mods::trimView(s.substr(0, semi));
                s = semi == std::string_view::npos ? std::string_view() : s.substr(semi + 1);
                if (part.empty()) continue;
                std::filesystem::path p = pathFromUtf8(part);
                if (p.is_relative()) p = iniDir / p;
                add(p);
            }
            return dirs;
        }
    if (!prefDir.empty()) add(prefDir / "dos_mods");
    if (!exeDir.empty()) add(exeDir / "dos_mods");
    return dirs;
}

std::vector<std::filesystem::path> configuredDosModDirs(const save::Paths& paths, const std::vector<std::filesystem::path>& extra) {
    std::optional<save::IniFile> ini;
    if (!paths.iniPath.empty()) ini = save::IniFile::load(paths.iniPath);
    auto dirs = dosModDirsFromIni(ini ? &*ini : nullptr, paths.iniPath.parent_path(), paths.prefDir, paths.exeDir);
    dirs.insert(dirs.end(), extra.begin(), extra.end());
    return dirs;
}

}  // namespace tmport::dos
