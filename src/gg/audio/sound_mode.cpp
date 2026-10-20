#include "gg/audio/sound_mode.h"

#include <cctype>

namespace tmport::gg {

std::string_view soundModeName(SoundMode m) {
    switch (m) {
        case SoundMode::Off: return "off";
        case SoundMode::Speaker: return "speaker";
        case SoundMode::Fm: return "fm";
    }
    return "?";
}

// Init_CommandLine 303a:005c: '-' or '/', then 'S' and the table CS:0234 (ovl11): NUL, '-', 'N'
// -> 0; 'I' -> 1; 'A', 'B', 'M' -> 4 (upper-cased; the rest of the argument is ignored).
std::optional<SoundMode> parseSoundMode(std::string_view s) {
    if (s == "off") return SoundMode::Off;
    if (s == "speaker") return SoundMode::Speaker;
    if (s == "fm") return SoundMode::Fm;
    if (s.size() < 2 || (s[0] != '-' && s[0] != '/') || std::toupper(static_cast<unsigned char>(s[1])) != 'S') return std::nullopt;
    const char c = s.size() > 2 ? static_cast<char>(std::toupper(static_cast<unsigned char>(s[2]))) : '\0';
    switch (c) {
        case '\0':
        case '-':
        case 'N': return SoundMode::Off;
        case 'I': return SoundMode::Speaker;
        case 'A':
        case 'B':
        case 'M': return SoundMode::Fm;
        default: return std::nullopt;
    }
}

}  // namespace tmport::gg
