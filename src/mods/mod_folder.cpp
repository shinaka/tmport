#include "mods/mod_folder.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>

#include "mods/json.h"
#include "util/file_util.h"

namespace tmport::mods {

std::optional<ResType> resTypeOf(ModFileKind k) {
    switch (k) {
        case ModFileKind::Pmap:
        case ModFileKind::Pmap16: return ResType::PMAP;
        case ModFileKind::Ssnd: return ResType::SSND;
        case ModFileKind::OthrBin:
        case ModFileKind::OthrText:
        case ModFileKind::Dam: return ResType::OTHR;
        case ModFileKind::Bndl: return ResType::BNDL;
        case ModFileKind::Ffnt: return ResType::FFNT;
        case ModFileKind::Rrgb: return ResType::RRGB;
        case ModFileKind::Midi:
        case ModFileKind::MidiE:
        case ModFileKind::MidiB: return std::nullopt;
    }
    return std::nullopt;
}

std::string describeKind(ModFileKind k) {
    switch (k) {
        case ModFileKind::Pmap: return "PMAP/<id>.png";
        case ModFileKind::Pmap16: return "PMAP16/<id>.png";
        case ModFileKind::Ssnd: return "SSND/<id>.wav";
        case ModFileKind::OthrBin: return "OTHR/<id>.bin";
        case ModFileKind::OthrText: return "OTHR/<id>.txt";
        case ModFileKind::Dam: return "DAM/<id>.dam.txt";
        case ModFileKind::Bndl: return "BNDL/<id>.json";
        case ModFileKind::Ffnt: return "FFNT/<id>.ffnt";
        case ModFileKind::Rrgb: return "RRGB/<id>.pal";
        case ModFileKind::Midi: return "MIDI/<id>.mid";
        case ModFileKind::MidiE: return "MIDI/<id>.e.mid";
        case ModFileKind::MidiB: return "MIDI/<id>.b.mid";
    }
    return "?";
}

std::string idRangeText(ResType t) {
    switch (t) {
        case ResType::BNDL: return "0-899";
        case ResType::RRGB: return "900-949";
        case ResType::FFNT: return "950-999";
        case ResType::PMAP: return "1000-3999";
        case ResType::SSND: return "4000-9999";
        case ResType::OTHR: return "10000-32767";
    }
    return "?";
}

bool IdSet::contains(int id) const {
    return std::any_of(ranges_.begin(), ranges_.end(), [&](const auto& r) { return id >= r.first && id <= r.second; });
}

namespace {

std::optional<int> parseId(std::string_view s) {
    if (s.empty() || s.size() > 5) return std::nullopt;
    int v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || p != s.data() + s.size() || v < 0 || v > 32767) return std::nullopt;
    return v;
}

void parseIdSet(const json::Value* v, IdSet& out, const char* key) {
    if (!v) return;
    if (!v->isArray()) throw FormatError(std::format("mod.json: \"{}\" must be an array of ids or \"lo-hi\" ranges", key));
    for (const auto& e : v->asArray()) {
        if (e.isInteger()) {
            const int64_t id = e.asInt();
            if (id < 0 || id > 32767) throw FormatError(std::format("mod.json: \"{}\": id {} is outside 0-32767", key, id));
            out.add(static_cast<int>(id), static_cast<int>(id));
        } else if (e.isString()) {
            const std::string& s = e.asString();
            const size_t dash = s.find('-');
            const auto lo = parseId(s.substr(0, dash));
            const auto hi = dash == std::string::npos ? lo : parseId(s.substr(dash + 1));
            if (!lo || !hi || *hi < *lo) throw FormatError(std::format("mod.json: \"{}\": '{}' is not an id or a lo-hi range", key, s));
            out.add(*lo, *hi);
        } else {
            throw FormatError(std::format("mod.json: \"{}\" must contain numbers or \"lo-hi\" strings", key));
        }
    }
}

struct DirKind {
    const char* dir;
    std::vector<std::pair<const char*, ModFileKind>> exts;  // suffix (lower case) -> kind
};

const std::vector<DirKind>& dirKinds() {
    static const std::vector<DirKind> k = {
        {"PMAP", {{".png", ModFileKind::Pmap}}},
        {"PMAP16", {{".png", ModFileKind::Pmap16}}},
        {"SSND", {{".wav", ModFileKind::Ssnd}}},
        {"OTHR", {{".txt", ModFileKind::OthrText}, {".bin", ModFileKind::OthrBin}}},
        {"DAM", {{".dam.txt", ModFileKind::Dam}}},
        {"BNDL", {{".json", ModFileKind::Bndl}}},
        {"FFNT", {{".ffnt", ModFileKind::Ffnt}, {".bin", ModFileKind::Ffnt}}},
        {"RRGB", {{".pal", ModFileKind::Rrgb}}},
        {"MIDI", {{".e.mid", ModFileKind::MidiE}, {".b.mid", ModFileKind::MidiB}, {".mid", ModFileKind::Midi}}},
    };
    return k;
}

}  // namespace

ModManifest parseManifest(std::string_view text) {
    const json::Value v = json::parse(text);
    if (!v.isObject()) throw FormatError("mod.json must hold a JSON object");
    ModManifest m;
    if (const auto* n = v.find("name")) {
        if (!n->isString()) throw FormatError("mod.json: \"name\" must be a string");
        m.name = n->asString();
    }
    parseIdSet(v.find("newIds"), m.newIds, "newIds");
    parseIdSet(v.find("resizable"), m.resizable, "resizable");
    return m;
}

ModFolder scanModFolder(const std::filesystem::path& dir) {
    ModFolder f;
    f.dir = dir;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return f;
    f.manifest.name = pathToUtf8(dir.filename());
    for (const auto& top : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = pathToUtf8(top.path().filename());
        if (top.is_regular_file(ec)) {
            if (iequalsAscii(name, "mod.json")) {
                try {
                    const auto text = readWholeFile(top.path());
                    if (!text) throw FormatError("cannot read the file");
                    ModManifest m = parseManifest(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()));
                    if (m.name.empty()) m.name = f.manifest.name;
                    f.manifest = std::move(m);
                } catch (const std::exception& e) {
                    f.errors.push_back(std::format("{}: {}", pathToUtf8(top.path()), e.what()));
                }
            }
            continue;  // README files etc. at the top level are ignored
        }
        if (!top.is_directory(ec)) continue;
        if (iequalsAscii(name, "TABLES")) {
            for (const auto& e : std::filesystem::directory_iterator(top.path(), ec)) {
                const std::string fn = pathToUtf8(e.path().filename());
                if (e.is_regular_file(ec) && toLowerAscii(fn).ends_with(".json"))
                    f.tableFiles.push_back(e.path());
                else if (e.is_regular_file(ec))
                    f.warnings.push_back(std::format("{}: ignored (TABLES/ holds .json files)", pathToUtf8(e.path())));
            }
            std::sort(f.tableFiles.begin(), f.tableFiles.end(),
                      [](const auto& a, const auto& b) { return toLowerAscii(pathToUtf8(a.filename())) < toLowerAscii(pathToUtf8(b.filename())); });
            continue;
        }
        const DirKind* dk = nullptr;
        for (const auto& k : dirKinds())
            if (iequalsAscii(name, k.dir)) dk = &k;
        if (!dk) {
            f.warnings.push_back(std::format("{}: unknown folder, ignored", pathToUtf8(top.path())));
            continue;
        }
        std::vector<std::filesystem::path> entries;
        for (const auto& e : std::filesystem::directory_iterator(top.path(), ec))
            if (e.is_regular_file(ec)) entries.push_back(e.path());
        std::sort(entries.begin(), entries.end());
        for (const auto& p : entries) {
            const std::string fn = pathToUtf8(p.filename());
            const std::string lfn = toLowerAscii(fn);
            bool matched = false;
            for (const auto& [ext, kind] : dk->exts) {
                if (!lfn.ends_with(ext)) continue;
                const auto id = parseId(std::string_view(lfn).substr(0, lfn.size() - std::char_traits<char>::length(ext)));
                if (!id) continue;
                f.files.push_back(ModFile{kind, *id, p});
                matched = true;
                break;
            }
            if (!matched) {
                std::string expected;
                for (const auto& [ext, kind] : dk->exts) expected += (expected.empty() ? "" : " or ") + describeKind(kind);
                f.warnings.push_back(std::format("{}: ignored (expected {})", pathToUtf8(p), expected));
            }
        }
    }
    return f;
}

}  // namespace tmport::mods
