#include "gg/app/settings.h"

#include <charconv>
#include <system_error>

#include "save/ini_file.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

std::optional<bool> parseBool(std::string_view v) {
    const std::string s = toLowerAscii(v);
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return std::nullopt;
}

template <class T>
std::optional<T> parseUnsigned(std::string_view v) {
    T out{};
    const auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    if (v.empty() || ec != std::errc() || p != v.data() + v.size()) return std::nullopt;
    return out;
}

}  // namespace

GgSettings GgSettings::load(const std::filesystem::path& path, bool* existed) {
    GgSettings s;
    const auto ini = save::IniFile::load(path);
    if (existed) *existed = ini.has_value();
    if (!ini) return s;
    auto warn = [&](std::string_view key, const std::string& v) {
        log::warn("{}: [{}] {} = {} is not a valid value; the default is used", pathToUtf8(path), kSection, key, v);
    };
    auto boolKey = [&](std::string_view key, bool& out) {
        if (const auto v = ini->get(kSection, key)) {
            if (const auto b = parseBool(*v)) out = *b;
            else warn(key, *v);
        }
    };
    if (const auto v = ini->get(kSection, "data")) s.data = *v;
    if (const auto v = ini->get(kSection, "sound")) {
        if (const auto x = parseSound(*v)) s.sound = *x;
        else warn("sound", *v);
    }
    if (const auto v = ini->get(kSection, "cpu")) {
        if (const auto x = parseCpu(*v)) s.cpu = *x;
        else warn("cpu", *v);
    }
    boolKey("fullscreen", s.fullscreen);
    boolKey("square_pixels", s.squarePixels);
    boolKey("integer_scale", s.integerScale);
    boolKey("pause_on_focus_loss", s.pauseOnFocusLoss);
    if (const auto v = ini->get(kSection, "window_scale")) {
        if (const auto n = parseUnsigned<int>(*v); n && *n <= 16) s.windowScale = *n;
        else warn("window_scale", *v);
    }
    if (const auto v = ini->get(kSection, "typematic_delay_ms")) {
        if (const auto n = parseUnsigned<uint32_t>(*v); n && *n >= 1 && *n <= 5000) s.typematicDelayMs = *n;
        else warn("typematic_delay_ms", *v);
    }
    if (const auto v = ini->get(kSection, "typematic_rate_ms")) {
        if (const auto n = parseUnsigned<uint32_t>(*v); n && *n >= 1 && *n <= 1000) s.typematicRateMs = *n;
        else warn("typematic_rate_ms", *v);
    }
    return s;
}

bool GgSettings::save(const std::filesystem::path& path) const {
    save::IniFile ini = save::IniFile::load(path).value_or(save::IniFile::parse(
        "; ggport settings (Gizmos & Gadgets port). The command line overrides them for one run.\n"));
    const auto b = [](bool v) { return v ? "true" : "false"; };
    ini.set(kSection, "data", data);
    ini.set(kSection, "sound", soundModeName(sound));
    ini.set(kSection, "cpu", cpuName(cpu));
    ini.set(kSection, "fullscreen", b(fullscreen));
    ini.set(kSection, "square_pixels", b(squarePixels));
    ini.set(kSection, "integer_scale", b(integerScale));
    ini.set(kSection, "window_scale", std::to_string(windowScale));
    ini.set(kSection, "pause_on_focus_loss", b(pauseOnFocusLoss));
    ini.set(kSection, "typematic_delay_ms", std::to_string(typematicDelayMs));
    ini.set(kSection, "typematic_rate_ms", std::to_string(typematicRateMs));
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return ini.save(path);
}

}  // namespace tmport::gg
