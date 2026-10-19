#include "edition/dos_options.h"

#include "util/file_util.h"

namespace tmport::dos {

DosOptions dosOptionsFrom(const DosSettings& s) {
    DosOptions o;
    o.look = s.look;
    o.sound = s.sound;
    o.slowCpu = s.slowCpu;
    o.frameSpeed = s.frameSpeed;
    o.squarePixels = s.squarePixels;
    return o;
}

int videoTypeOf(Look look) {
    switch (look) {
        case Look::Ega: return 1;
        case Look::Cga: return 2;
        case Look::Tandy: return 3;
        case Look::Mcga: return 5;
    }
    return 1;
}

int soundDeviceOf(SoundDevice sound) {
    switch (sound) {
        case SoundDevice::Speaker: return 0;
        case SoundDevice::Tandy: return 1;
        case SoundDevice::AdLib: return 2;
        case SoundDevice::Off: return -1;
    }
    return 0;
}

std::optional<Look> parseLookOption(std::string_view text) {
    const std::string t = toLowerAscii(text);
    if (t == "hercules" || t == "hgc") return Look::Cga;  // drawn like CGA (g_videoType 4)
    return parseLook(t);
}

std::optional<SoundDevice> parseSoundDevice(std::string_view text) {
    const std::string t = toLowerAscii(text);
    if (t == "speaker" || t == "pc" || t == "ibm") return SoundDevice::Speaker;
    if (t == "tandy" || t == "psg") return SoundDevice::Tandy;
    if (t == "adlib" || t == "opl2") return SoundDevice::AdLib;
    if (t == "off" || t == "none") return SoundDevice::Off;
    return std::nullopt;
}

std::optional<FrameSpeed> parseFrameSpeed(std::string_view text) {
    const std::string t = toLowerAscii(text);
    if (t == "reference") return FrameSpeed::Reference;
    if (t == "period") return FrameSpeed::Period;
    return std::nullopt;
}

std::string_view iniValue(SoundDevice v) {
    switch (v) {
        case SoundDevice::Speaker: return "speaker";
        case SoundDevice::Tandy: return "tandy";
        case SoundDevice::AdLib: return "adlib";
        case SoundDevice::Off: return "off";
    }
    return "tandy";
}

std::string_view iniValue(FrameSpeed v) { return v == FrameSpeed::Period ? "period" : "reference"; }

std::vector<std::string> applyDosSwitches(DosOptions& o, const std::vector<std::string>& args) {
    // The code-segment tables of 1460:04C2 (letters C E H M S T X, both cases) and DS:0620 / DS:0629
    // (the -S device letters "iItTaAsS" -> 0 0 1 1 2 2 2 2).
    std::vector<std::string> ignored;
    for (const std::string& a : args) {
        if (a.size() < 2 || (a[0] != '-' && a[0] != '/')) {
            ignored.push_back(a);
            continue;
        }
        switch (a[1]) {
            case 'E': case 'e': o.look = Look::Ega; break;
            case 'C': case 'c': o.look = Look::Cga; break;
            case 'T': case 't': o.look = Look::Tandy; break;
            case 'H': case 'h': o.look = Look::Cga; break;  // Hercules: the CGA picture
            case 'M': case 'm': o.look = Look::Mcga; break;
            case 'X': case 'x': o.slowCpu = !o.slowCpu; break;
            case 'S': case 's': {
                const char c = a.size() > 2 ? a[2] : '\0';
                if (c == 'i' || c == 'I') o.sound = SoundDevice::Speaker;
                else if (c == 't' || c == 'T') o.sound = SoundDevice::Tandy;
                else if (c == 'a' || c == 'A' || c == 's' || c == 'S') o.sound = SoundDevice::AdLib;
                else ignored.push_back(a);  // unknown or missing device letter: no change
                break;
            }
            default: ignored.push_back(a); break;
        }
    }
    return ignored;
}

std::vector<std::string> applyDosSwitches(DosOptions& o, std::string_view args) {
    std::vector<std::string> list;
    size_t i = 0;
    while (i < args.size()) {
        while (i < args.size() && (args[i] == ' ' || args[i] == '\t')) ++i;
        size_t j = i;
        while (j < args.size() && args[j] != ' ' && args[j] != '\t') ++j;
        if (j > i) list.emplace_back(args.substr(i, j - i));
        i = j;
    }
    return applyDosSwitches(o, list);
}

}  // namespace tmport::dos
