#include "dos/mods/dos_sound_text.h"

#include <array>
#include <charconv>
#include <format>
#include <optional>

#include "dos/data/dgroup_offsets.h"
#include "mods/mod_common.h"
#include "util/error.h"

namespace tmport::dos {

namespace {

constexpr std::array<const char*, 12> kNoteNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr std::array<const char*, 8> kDurations = {"1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128"};
constexpr std::array<const char*, 7> kCommands = {"release", "tempo+", "tempo-", "rawdur", "tempo", "envelope", "sustain"};

uint16_t rd16(std::span<const uint8_t> b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }

std::optional<std::string> durationText(uint8_t b1) {
    if (b1 == 0x80) return std::string("full");
    if (b1 & 0xE0) return std::nullopt;
    std::string s = kDurations[b1 & 7];
    if (b1 & 8) s += '.';
    if (b1 & 16) s += " tie";
    return s;
}

std::string eventText(uint8_t b0, uint8_t b1) {
    const int n = b0 & 0x0F, oct = b0 >> 4;
    if (n >= 1 && n <= 12)
        if (auto d = durationText(b1)) return std::format("{}{} {}", kNoteNames[static_cast<size_t>(n - 1)], oct + 1, *d);
    if (n == 0 && oct == 0)
        if (auto d = durationText(b1)) return "rest " + *d;
    if (n == 0x0D) {
        if (oct == 0 && b1 == 0) return "legato";
        if (oct < static_cast<int>(kCommands.size())) return std::format("{} {}", kCommands[static_cast<size_t>(oct)], b1);
        return std::format("cmd {} {}", oct, b1);
    }
    if (n == 0x0E) return std::format("raw {} {}", oct, b1);
    if (b0 == 0xFF && b1 == 0xFF) return "end";
    return std::format("bytes {:#04x} {:#04x}", b0, b1);
}

// Stream length up to and including its end event (note 0x0F).
size_t streamLength(std::span<const uint8_t> f, size_t off, std::string_view name) {
    for (size_t i = off; i + 1 < f.size(); i += 2)
        if ((f[i] & 0x0F) == 0x0F) return i + 2 - off;
    throw FormatError(std::format("{}: the stream at {} has no end", name, off));
}

std::vector<std::string_view> tokens(std::string_view line) {
    std::vector<std::string_view> t;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size() || line[i] == '#') break;
        size_t j = i;
        while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;  // C#4: '#' inside a word is a sharp
        t.push_back(line.substr(i, j - i));
        i = j;
    }
    return t;
}

std::optional<int> number(std::string_view s) {
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
        base = 16;
    }
    int v = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
    if (ec != std::errc() || p != s.data() + s.size()) return std::nullopt;
    return v;
}

}  // namespace

DosSoundFormat dosSoundFormat(FileIndex index) {
    DosSoundFormat f;
    f.tandy = index == FileIndex::SfxTan || index == FileIndex::TunesTan;
    f.tunes = index == FileIndex::TunesIbm || index == FileIndex::TunesTan;
    f.entries = f.tunes ? 3 : 12;
    f.voices = f.tandy ? 4 : 1;
    f.headerBytes = f.tandy ? 0x230 : 0x96;
    f.bufferBytes = f.tunes ? 5056 : 1712;
    return f;
}

bool isDosSoundFile(FileIndex index) {
    return index == FileIndex::SfxIbm || index == FileIndex::TunesIbm || index == FileIndex::SfxTan || index == FileIndex::TunesTan;
}

std::string soundFileToText(std::span<const uint8_t> file, FileIndex index, std::string_view name) {
    const DosSoundFormat fmt = dosSoundFormat(index);
    const int streams = fmt.entries * fmt.voices;
    if (file.size() < fmt.headerBytes || rd16(file, 0) != fmt.headerBytes) throw FormatError(std::format("{}: bad header", name));
    std::vector<size_t> offs;
    for (int s = 0; s < streams; ++s) offs.push_back(rd16(file, 2 * static_cast<size_t>(s)));
    const char* entryWord = fmt.tunes ? "tune" : "effect";
    std::string out = std::format(
        "# {}: the {} of the 1990 DOS edition on the {}, {} {}s{}.\n"
        "# Written by tmport --export-mod-kit from your game files. One event per line; the format is in\n"
        "# docs/MODDING.md (\"Sound files\"). Durations count from the tempo (ticks per whole note / 4).\n",
        name, fmt.tunes ? "music" : "sound effects",
        fmt.tandy ? (fmt.tunes ? "Tandy / PS/1 sound chip and the AdLib" : "Tandy / PS/1 sound chip") : "PC speaker", fmt.entries, entryWord,
        fmt.tandy ? " of 4 voices (voice 3 = noise)" : "");
    size_t expect = fmt.headerBytes;
    for (int s = 0; s < streams; ++s) {
        const size_t off = offs[static_cast<size_t>(s)];
        if (off != expect) throw FormatError(std::format("{}: stream {} is not where the previous one ends", name, s));
        const size_t len = streamLength(file, off, name);
        const size_t next = s + 1 < streams ? offs[static_cast<size_t>(s + 1)] : file.size();
        if (next < off + len) throw FormatError(std::format("{}: streams overlap at {}", name, off));
        if (s % fmt.voices == 0) out += std::format("\n{} {}\n", entryWord, s / fmt.voices);
        const std::string indent = fmt.voices > 1 ? "    " : "  ";
        if (fmt.voices > 1) out += std::format("  voice {}\n", s % fmt.voices);
        for (size_t i = off; i < off + len; i += 2) out += indent + eventText(file[i], file[i + 1]) + '\n';
        if (next > off + len) {
            out += indent + "pad";
            for (size_t i = off + len; i < next; ++i) out += std::format(" {:#04x}", file[i]);
            out += '\n';
        }
        expect = next;
    }
    // The header words after the entries: stale in the original files, never read by the game.
    size_t lastNonZero = 0;
    for (size_t w = static_cast<size_t>(streams); w < fmt.headerBytes / 2; ++w)
        if (rd16(file, 2 * w) != 0) lastNonZero = w + 1;
    if (lastNonZero > 0) {
        out += "\n# The header words after the entries: stale data in the original file, never read by the game.\nslots";
        for (size_t w = static_cast<size_t>(streams); w < lastNonZero; ++w) {
            out += std::format(" {:#06x}", rd16(file, 2 * w));
            if ((w - static_cast<size_t>(streams)) % 12 == 11 && w + 1 < lastNonZero) out += "\nslots";
        }
        out += '\n';
    }
    return out;
}

std::vector<uint8_t> soundFileFromText(std::string_view text, FileIndex index, std::string_view name) {
    const DosSoundFormat fmt = dosSoundFormat(index);
    const std::string_view entryWord = fmt.tunes ? "tune" : "effect";
    struct Stream {
        std::vector<uint8_t> bytes;
        bool ended = false;
        int line = 0;
    };
    std::vector<std::vector<Stream>> entries;
    std::vector<uint16_t> slots;
    int lineNo = 0;
    size_t pos = 0;
    auto fail = [&](const std::string& msg) { throw mods::ModSyntaxError(msg, lineNo); };
    auto byteArg = [&](std::string_view t, const char* what) -> uint8_t {
        const auto v = number(t);
        if (!v || *v < 0 || *v > 255) fail(std::format("{} must be a number 0-255, not '{}'", what, t));
        return static_cast<uint8_t>(*v);
    };
    auto current = [&]() -> Stream& {
        if (entries.empty()) fail(std::format("an event before the first '{} 0'", entryWord));
        if (entries.back().empty()) {
            if (fmt.voices > 1) fail("an event before 'voice 0'");
            entries.back().emplace_back();
            entries.back().back().line = lineNo;
        }
        return entries.back().back();
    };
    auto parseDuration = [&](std::span<const std::string_view> t) -> uint8_t {
        if (t.empty()) fail("a duration is missing (1, 1/2, 1/4 ... 1/128 or full)");
        std::string_view d = t[0];
        if (d == "full") {
            if (t.size() > 1) fail("'full' takes no dot or tie");
            return 0x80;
        }
        uint8_t v = 0;
        if (d.ends_with('.')) {
            v |= 8;
            d.remove_suffix(1);
        }
        int n = -1;
        for (int k = 0; k < static_cast<int>(kDurations.size()); ++k)
            if (d == kDurations[static_cast<size_t>(k)]) n = k;
        if (n < 0) fail(std::format("'{}' is not a duration (1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64, 1/128, a dot for dotted)", t[0]));
        v |= static_cast<uint8_t>(n);
        if (t.size() > 1) {
            if (t[1] != "tie" || t.size() > 2) fail("after the duration only 'tie' may follow");
            v |= 16;
        }
        return v;
    };
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto t = tokens(line);
        if (t.empty()) continue;
        const std::string_view w = t[0];
        if (w == entryWord) {
            if (t.size() != 2 || number(t[1]) != static_cast<int>(entries.size()))
                fail(std::format("expected '{} {}' (entries in order)", entryWord, entries.size()));
            entries.emplace_back();
            continue;
        }
        if (w == "tune" || w == "effect") fail(std::format("{} holds {}s, not {}s", name, entryWord, w));
        if (w == "voice") {
            if (entries.empty()) fail(std::format("'voice' before the first '{} 0'", entryWord));
            if (t.size() != 2 || number(t[1]) != static_cast<int>(entries.back().size()))
                fail(std::format("expected 'voice {}' (voices in order)", entries.back().size()));
            if (static_cast<int>(entries.back().size()) >= fmt.voices)
                fail(std::format("{} has {} voice(s) per {}", name, fmt.voices, entryWord));
            entries.back().emplace_back();
            entries.back().back().line = lineNo;
            continue;
        }
        if (w == "slots") {
            for (size_t k = 1; k < t.size(); ++k) {
                const auto v = number(t[k]);
                if (!v || *v < 0 || *v > 0xFFFF) fail(std::format("'{}' is not a 16-bit number", t[k]));
                slots.push_back(static_cast<uint16_t>(*v));
            }
            continue;
        }
        Stream& s = current();
        if (w == "pad") {
            if (!s.ended) fail("'pad' comes after the voice's 'end'");
            for (size_t k = 1; k < t.size(); ++k) s.bytes.push_back(byteArg(t[k], "a pad byte"));
            continue;
        }
        if (s.ended) fail("an event after 'end' (start the next voice or entry first)");
        auto emit = [&](uint8_t b0, uint8_t b1) {
            s.bytes.push_back(b0);
            s.bytes.push_back(b1);
        };
        const std::span<const std::string_view> args(t.data() + 1, t.size() - 1);
        auto needArgs = [&](size_t n) {
            if (args.size() != n) fail(std::format("'{}' takes {} value(s)", w, n));
        };
        if (w == "end") {
            needArgs(0);
            emit(0xFF, 0xFF);
            s.ended = true;
        } else if (w == "rest") {
            emit(0x00, parseDuration(args));
        } else if (w == "legato") {
            needArgs(0);
            emit(0x0D, 0);
        } else if (w == "raw") {
            needArgs(2);
            const auto o = number(args[0]);
            if (!o || *o < 0 || *o > 15) fail("the octave code of 'raw' is 0-15");
            emit(static_cast<uint8_t>(*o << 4 | 0x0E), byteArg(args[1], "the value of 'raw'"));
        } else if (w == "cmd") {
            needArgs(2);
            const auto k = number(args[0]);
            if (!k || *k < 0 || *k > 15) fail("the command number of 'cmd' is 0-15");
            emit(static_cast<uint8_t>(*k << 4 | 0x0D), byteArg(args[1], "the value of 'cmd'"));
        } else if (w == "bytes") {
            needArgs(2);
            const uint8_t b0 = byteArg(args[0], "an event byte"), b1 = byteArg(args[1], "an event byte");
            emit(b0, b1);
            if ((b0 & 0x0F) == 0x0F) s.ended = true;
        } else {
            int cmd = -1;
            for (int k = 0; k < static_cast<int>(kCommands.size()); ++k)
                if (w == kCommands[static_cast<size_t>(k)]) cmd = k;
            if (cmd >= 0) {
                needArgs(1);
                const uint8_t v = byteArg(args[0], "the value");
                if (cmd == 5 && v >= dg::kEnvelopesCount)
                    fail(std::format("envelope {} does not exist (0-{})", v, dg::kEnvelopesCount - 1));
                if (cmd == 6 && v > 15) fail("sustain is an attenuation 0-15");
                emit(static_cast<uint8_t>(cmd << 4 | 0x0D), v);
            } else {
                // A note: name, octave 1-16, duration.
                int note = -1;
                size_t nameLen = 0;
                for (int k = 0; k < static_cast<int>(kNoteNames.size()); ++k) {
                    const std::string_view n = kNoteNames[static_cast<size_t>(k)];
                    if (w.starts_with(n) && n.size() > nameLen) {
                        note = k + 1;
                        nameLen = n.size();
                    }
                }
                if (note > 0 && nameLen == 1 && w.size() > 1 && w[1] == 'b') {  // flats: Db = C#
                    note = note == 1 ? 12 : note - 1;
                    nameLen = 2;
                    if (w[0] == 'C') fail("Cb is not used: write B of the octave below");
                }
                const auto oct = note > 0 ? number(w.substr(nameLen)) : std::nullopt;
                if (note < 0 || !oct)
                    fail(std::format("'{}' is not a note (C C# D D# E F F# G G# A A# B with an octave 1-16) or a command", w));
                if (*oct < 1 || *oct > 16) fail(std::format("octave {} outside 1-16", *oct));
                emit(static_cast<uint8_t>((*oct - 1) << 4 | note), parseDuration(args));
            }
        }
    }
    ++lineNo;
    if (static_cast<int>(entries.size()) != fmt.entries)
        throw FormatError(std::format("{}: {} {}(s); the game plays exactly {} ({} 0-{})", name, entries.size(), entryWord, fmt.entries,
                                      entryWord, fmt.entries - 1));
    std::vector<uint8_t> file(fmt.headerBytes, 0);
    const size_t streamSlots = static_cast<size_t>(fmt.entries * fmt.voices);
    if (streamSlots + slots.size() > fmt.headerBytes / 2)
        throw FormatError(std::format("{}: {} 'slots' words; the header has room for {}", name, slots.size(),
                                      fmt.headerBytes / 2 - streamSlots));
    for (size_t k = 0; k < slots.size(); ++k) {
        file[2 * (streamSlots + k)] = static_cast<uint8_t>(slots[k] & 0xFF);
        file[2 * (streamSlots + k) + 1] = static_cast<uint8_t>(slots[k] >> 8);
    }
    size_t slot = 0;
    for (size_t e = 0; e < entries.size(); ++e) {
        if (static_cast<int>(entries[e].size()) != fmt.voices)
            throw FormatError(std::format("{}: {} {} has {} voice(s), it needs {}", name, entryWord, e, entries[e].size(), fmt.voices));
        for (size_t v = 0; v < entries[e].size(); ++v, ++slot) {
            const Stream& s = entries[e][v];
            if (!s.ended)
                throw mods::ModSyntaxError(std::format("{} {} voice {} has no 'end'", entryWord, e, v), s.line);
            if (file.size() > 0xFFFF) throw FormatError(std::format("{}: larger than 64 KB", name));
            file[2 * slot] = static_cast<uint8_t>(file.size() & 0xFF);
            file[2 * slot + 1] = static_cast<uint8_t>(file.size() >> 8);
            file.insert(file.end(), s.bytes.begin(), s.bytes.end());
        }
    }
    if (file.size() > fmt.bufferBytes)
        throw FormatError(std::format("{}: {} bytes; the sound driver's static buffer holds {} (shorten a {} or drop 'pad' "
                                      "lines)",
                                      name, file.size(), fmt.bufferBytes, entryWord));
    return file;
}

}  // namespace tmport::dos
