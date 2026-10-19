#include "save/settings.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <charconv>
#include <system_error>

#include "util/file_util.h"
#include "util/log.h"

namespace tmport::save {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::optional<bool> parseBool(std::string_view v) {
    const std::string s = toLowerAscii(trim(v));
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return std::nullopt;
}

std::optional<int> parseInt(std::string_view v) {
    v = trim(v);
    int out = 0;
    auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    if (ec != std::errc() || p != v.data() + v.size()) return std::nullopt;
    return out;
}

void warnBad(std::string_view section, std::string_view key, std::string_view value) {
    log::warn("tmport.ini: [{}] {} = '{}' is not valid; using the default", section, key, value);
}

}  // namespace

// ------------------------------------------------------------------------------------- IniFile

IniFile IniFile::parse(std::string_view text) {
    IniFile ini;
    std::string section;
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF && static_cast<uint8_t>(text[1]) == 0xBB &&
        static_cast<uint8_t>(text[2]) == 0xBF)
        text.remove_prefix(3);  // UTF-8 BOM
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view raw = text.substr(pos, end - pos);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        {
            Line line;
            line.raw = std::string(raw);
            const std::string_view t = trim(raw);
            if (!t.empty() && t.front() == '[' && t.find(']') != std::string_view::npos) {
                section = toLowerAscii(trim(t.substr(1, t.find(']') - 1)));
                line.isSection = true;
                line.section = section;
            } else {
                line.section = section;
                if (!t.empty() && t.front() != ';' && t.front() != '#') {
                    const size_t eq = t.find('=');
                    if (eq != std::string_view::npos) {
                        line.key = toLowerAscii(trim(t.substr(0, eq)));
                        line.value = std::string(trim(t.substr(eq + 1)));
                    }
                }
            }
            ini.lines_.push_back(std::move(line));
        }
        pos = end + 1;
    }
    return ini;
}

std::optional<IniFile> IniFile::load(const std::filesystem::path& path) {
    auto data = readWholeFile(path);
    if (!data) return std::nullopt;
    return parse(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
}

std::string IniFile::text() const {
    std::string out;
    for (const auto& l : lines_) {
        out += l.raw;
        out += '\n';
    }
    return out;
}

bool IniFile::save(const std::filesystem::path& path) const {
    const std::string t = text();
    // Atomic, keeping the replaced file as tmport.ini.bak (REVIEW_1 H-1).
    return replaceFileKeepingBackup(path, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(t.data()), t.size()));
}

std::optional<std::string> IniFile::get(std::string_view section, std::string_view key) const {
    const std::string s = toLowerAscii(section), k = toLowerAscii(key);
    for (const auto& l : lines_)
        if (!l.isSection && l.section == s && l.key == k) return l.value;
    return std::nullopt;
}

void IniFile::set(std::string_view section, std::string_view key, std::string_view value) {
    const std::string s = toLowerAscii(section), k = toLowerAscii(key);
    Line line;
    line.section = s;
    line.key = k;
    line.value = std::string(value);
    line.raw = std::string(key) + " = " + std::string(value);
    for (auto& l : lines_)
        if (!l.isSection && l.section == s && l.key == k) {
            l = line;
            return;
        }
    // Insert after the last non-blank line of the section.
    int header = -1, lastLine = -1;
    for (size_t i = 0; i < lines_.size(); ++i) {
        const Line& l = lines_[i];
        if (l.isSection && l.section == s && header < 0) header = static_cast<int>(i);
        if (l.section == s && (header >= 0 || s.empty()) && !trim(l.raw).empty()) lastLine = static_cast<int>(i);
    }
    if (header < 0 && !s.empty()) {
        if (!lines_.empty() && !trim(lines_.back().raw).empty()) lines_.push_back(Line{});
        Line h;
        h.raw = "[" + std::string(section) + "]";
        h.section = s;
        h.isSection = true;
        lines_.push_back(h);
        lines_.push_back(line);
        return;
    }
    const size_t at = lastLine < 0 ? (header < 0 ? 0 : static_cast<size_t>(header) + 1) : static_cast<size_t>(lastLine) + 1;
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(at), line);
}

// ------------------------------------------------------------------------------ EngineSettings

std::string_view iniValue(MidiSet v) { return v == MidiSet::Base ? "B" : "E"; }
std::string_view iniValue(MidiBackend v) {
    switch (v) {
        case MidiBackend::WinMM: return "winmm";
        case MidiBackend::Null: return "null";
        case MidiBackend::SoundFont: return "soundfont";
        default: return "auto";
    }
}
std::string_view iniValue(InputModeSetting v) { return v == InputModeSetting::FirstEvent ? "first-event" : "lossless"; }

EngineSettings EngineSettings::fromIni(const IniFile& ini) {
    EngineSettings s;
    auto boolKey = [&](const char* sec, const char* key, bool& out) {
        if (auto v = ini.get(sec, key)) {
            if (auto b = parseBool(*v)) out = *b;
            else warnBad(sec, key, *v);
        }
    };
    auto intKey = [&](const char* sec, const char* key, int& out, int lo, int hi) {
        if (auto v = ini.get(sec, key)) {
            auto n = parseInt(*v);
            if (n && *n >= lo && *n <= hi) out = *n;
            else warnBad(sec, key, *v);
        }
    };
    if (auto v = ini.get("engine", "data")) s.dataPath = *v;
    intKey("engine", "window_scale", s.windowScale, 1, 16);
    boolKey("engine", "integer_scale", s.integerScale);
    boolKey("engine", "fullscreen", s.fullscreen);
    boolKey("engine", "pause_on_focus_loss", s.pauseOnFocusLoss);
    intKey("engine", "cursor_scale", s.cursorScale, 0, 8);
    if (auto v = ini.get("input", "mode")) {
        const std::string m = toLowerAscii(*v);
        if (m == "lossless") s.inputMode = InputModeSetting::Lossless;
        else if (m == "first-event" || m == "first_event" || m == "original") s.inputMode = InputModeSetting::FirstEvent;
        else warnBad("input", "mode", *v);
    }
    boolKey("input", "tap_latch", s.tapLatch);
    if (auto v = ini.get("audio", "midi_set")) {
        const std::string m = toLowerAscii(*v);
        if (m == "e" || m == "extended") s.midiSet = MidiSet::Extended;
        else if (m == "b" || m == "base") s.midiSet = MidiSet::Base;
        else warnBad("audio", "midi_set", *v);
    }
    for (const char* key : {"midi_backend", "midi"}) {  // "midi" is an alias; read last, it wins
        if (auto v = ini.get("audio", key)) {
            const std::string m = toLowerAscii(*v);
            if (m == "auto") s.midiBackend = MidiBackend::Auto;
            else if (m == "winmm") s.midiBackend = MidiBackend::WinMM;
            else if (m == "null" || m == "none") s.midiBackend = MidiBackend::Null;
            else if (m == "soundfont" || m == "sf2") s.midiBackend = MidiBackend::SoundFont;
            else warnBad("audio", key, *v);
        }
    }
    if (auto v = ini.get("audio", "soundfont")) s.soundFont = *v;
    intKey("audio", "music_volume", s.musicVolume, 0, 100);
    boolKey("saves", "imported_original", s.importedOriginal);
    if (auto v = ini.get("saves", "import_from")) s.importFrom = *v;
    boolKey("debug", "keys", s.debugKeys);
    boolKey("debug", "show_treasure", s.showTreasure);
    if (auto v = ini.get("engine", "edition")) {
        const std::string m = toLowerAscii(trim(*v));
        if (m.empty() || m == "auto") s.edition.reset();
        else if (auto e = parseEditionKey(m)) s.edition = *e;
        else warnBad("engine", "edition", *v);
    }
    if (auto v = ini.get("dos", "data")) s.dosDataPath = *v;
    if (auto v = ini.get("dos", "look")) {
        if (auto l = dos::parseLookOption(trim(*v))) s.dos.look = *l;
        else warnBad("dos", "look", *v);
    }
    if (auto v = ini.get("dos", "sound")) {
        if (auto d = dos::parseSoundDevice(trim(*v))) s.dos.sound = *d;
        else warnBad("dos", "sound", *v);
    }
    boolKey("dos", "slow_cpu", s.dos.slowCpu);
    if (auto v = ini.get("dos", "frame_speed")) {
        if (auto f = dos::parseFrameSpeed(trim(*v))) s.dos.frameSpeed = *f;
        else warnBad("dos", "frame_speed", *v);
    }
    boolKey("dos", "square_pixels", s.dos.squarePixels);
    intKey("dos", "window_scale", s.dos.windowScale, 1, 16);
    return s;
}

void EngineSettings::toIni(IniFile& ini) const {
    auto b = [](bool v) { return v ? "true" : "false"; };
    ini.set("engine", "data", dataPath);
    ini.set("engine", "window_scale", std::to_string(windowScale));
    ini.set("engine", "integer_scale", b(integerScale));
    ini.set("engine", "fullscreen", b(fullscreen));
    ini.set("engine", "pause_on_focus_loss", b(pauseOnFocusLoss));
    ini.set("engine", "cursor_scale", std::to_string(cursorScale));
    ini.set("input", "mode", iniValue(inputMode));
    ini.set("input", "tap_latch", b(tapLatch));
    ini.set("audio", "midi_set", iniValue(midiSet));
    ini.set("audio", "midi_backend", iniValue(midiBackend));
    if (ini.get("audio", "midi")) ini.set("audio", "midi", iniValue(midiBackend));  // keep the alias in step
    ini.set("audio", "soundfont", soundFont);
    ini.set("audio", "music_volume", std::to_string(musicVolume));
    ini.set("saves", "imported_original", b(importedOriginal));
    ini.set("saves", "import_from", importFrom);
    ini.set("debug", "keys", b(debugKeys));
    ini.set("debug", "show_treasure", b(showTreasure));
    ini.set("engine", "edition", edition ? editionKey(*edition) : "auto");
    ini.set("dos", "data", dosDataPath);
    ini.set("dos", "look", dos::lookName(dos.look));
    ini.set("dos", "sound", dos::iniValue(dos.sound));
    ini.set("dos", "slow_cpu", b(dos.slowCpu));
    ini.set("dos", "frame_speed", dos::iniValue(dos.frameSpeed));
    ini.set("dos", "square_pixels", b(dos.squarePixels));
    ini.set("dos", "window_scale", std::to_string(dos.windowScale));
}

// ------------------------------------------------------------------------------------- Paths

Paths Paths::under(const std::filesystem::path& prefDir, const std::filesystem::path& exeDir) {
    Paths p;
    p.prefDir = prefDir;
    p.exeDir = exeDir;
    std::error_code ec;
    if (!exeDir.empty() && std::filesystem::exists(exeDir / "tmport.ini", ec))
        p.iniPath = exeDir / "tmport.ini";
    else if (!prefDir.empty())
        p.iniPath = prefDir / "tmport.ini";
    if (!prefDir.empty()) {
        p.usersDat = prefDir / "users.dat";
        p.screenshotDir = prefDir / "screenshots";
    }
    return p;
}

Paths Paths::detect() {
    std::filesystem::path pref, base;
    if (char* s = SDL_GetPrefPath("TMPort", "TMPort")) {
        pref = pathFromUtf8(s);
        SDL_free(s);
    } else {
        log::warn("no preference directory: {}", SDL_GetError());
    }
    if (const char* b = SDL_GetBasePath()) base = pathFromUtf8(b);
    return under(pref, base);
}

std::filesystem::path defaultOriginalUsersDat() {
#if defined(_WIN32)
    return std::filesystem::path(L"C:\\TLCWIN\\TMTWINCD\\users.dat");
#else
    return {};
#endif
}

ImportResult importOriginalUsersDat(EngineSettings& settings, const std::filesystem::path& source,
                                    const std::filesystem::path& dest) {
    if (settings.importedOriginal) return ImportResult::AlreadyDone;
    std::error_code ec;
    if (std::filesystem::exists(dest, ec)) {
        settings.importedOriginal = true;
        return ImportResult::DestExists;
    }
    if (source.empty() || !std::filesystem::is_regular_file(source, ec)) return ImportResult::SourceMissing;
    auto data = readWholeFile(source);
    if (!data) {
        log::warn("users.dat import: cannot read {}", pathToUtf8(source));
        return ImportResult::Failed;
    }
    if (!writeFileAtomically(dest, *data)) {
        log::warn("users.dat import: cannot write {}", pathToUtf8(dest));
        return ImportResult::Failed;
    }
    settings.importedOriginal = true;
    log::info("imported the original users.dat from {} ({} bytes)", pathToUtf8(source), data->size());
    return ImportResult::Imported;
}

EngineSettings loadSettingsAndImport(const Paths& paths, ImportResult* importResult) {
    IniFile ini;
    if (!paths.iniPath.empty())
        if (auto loaded = IniFile::load(paths.iniPath)) ini = std::move(*loaded);
    EngineSettings s = EngineSettings::fromIni(ini);
    ImportResult r = ImportResult::SourceMissing;
    if (!paths.usersDat.empty()) {
        const auto source = s.importFrom.empty() ? defaultOriginalUsersDat() : pathFromUtf8(s.importFrom);
        const bool before = s.importedOriginal;
        r = importOriginalUsersDat(s, source, paths.usersDat);
        if (s.importedOriginal != before && !paths.iniPath.empty()) {
            ini.set("saves", "imported_original", "true");
            if (!ini.save(paths.iniPath)) log::warn("cannot write {}", pathToUtf8(paths.iniPath));
        }
    }
    if (importResult) *importResult = r;
    return s;
}

}  // namespace tmport::save
