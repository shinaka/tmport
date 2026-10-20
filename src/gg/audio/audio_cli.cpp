#include "gg/audio/audio_cli.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <exception>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "gg/audio/bmf.h"
#include "gg/audio/dro_compare.h"
#include "gg/audio/gg_audio.h"
#include "gg/audio/soundtest.h"
#include "gg/data/cli_args.h"
#include "gg/data/gg_files.h"
#include "gg/data/resources.h"
#include "mods/wav_file.h"
#include "util/file_util.h"

namespace tmport::gg {

namespace {

using cli::hasFlag;
using cli::openFolder;
using cli::optionValue;

constexpr std::string_view kCommands[] = {"--gg-sound", "--gg-oplog", "--gg-dro-compare", "--gg-songs", "--gg-soundtest-check"};
constexpr std::string_view kValueOptions[] = {"--data",        "--ticks",   "--rate", "--arrangement", "--priority",
                                             "--speaker-log", "--opl-log", "--from", "--base", "--seed-dro", "--pairs", "--captures"};
constexpr std::string_view kFlags[] = {"--raw", "--loop", "--once"};
constexpr uint32_t kTailTicks = 36;                // half a second after a song's end, for releases
constexpr uint32_t kMaxTicks = 72 * 60 * 15;      // 15 minutes

void out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); }
void err(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stderr); }

// The arguments that are neither options nor option values, after the command word.
std::vector<std::string> positionals(const std::vector<std::string>& args, std::string_view command) {
    std::vector<std::string> p;
    bool seen = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == command) {
            seen = true;
            continue;
        }
        if (std::find(std::begin(kValueOptions), std::end(kValueOptions), a) != std::end(kValueOptions)) {
            ++i;
            continue;
        }
        if (std::find(std::begin(kFlags), std::end(kFlags), a) != std::end(kFlags)) continue;
        if (seen) p.push_back(a);
    }
    return p;
}

unsigned parseUnsigned(std::string_view s, int base = 10) {
    if (base == 16 && (s.starts_with("0x") || s.starts_with("0X"))) s.remove_prefix(2);
    unsigned v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v, base);
    if (s.empty() || r.ec != std::errc() || r.ptr != s.data() + s.size()) throw std::runtime_error(std::format("bad number '{}'", s));
    return v;
}

uint16_t parseId(std::string_view s) {
    const unsigned v = parseUnsigned(s, 16);
    if (v > 0xFFFF) throw std::runtime_error(std::format("bad resource id '{}'", s));
    return static_cast<uint16_t>(v);
}

// A resource payload: an entry, or member M of a bundle ("ID/M").
std::vector<uint8_t> loadPayload(const Archive& arc, std::string_view spec) {
    const size_t slash = spec.find('/');
    const uint16_t id = parseId(spec.substr(0, slash));
    if (slash == std::string_view::npos) return arc.load(id).bytes;
    const Bundle b = Bundle::load(arc, id);
    const unsigned m = parseUnsigned(spec.substr(slash + 1));
    if (m >= b.size()) throw std::runtime_error(std::format("{:04x} has {} members", id, b.size()));
    const auto p = b.member(m).payload;
    return {p.begin(), p.end()};
}

// The song of a music bank as SndRes_LoadMusicBank finds it (after the first member starting with 0).
std::optional<BmfSongInfo> musicBankSong(const Archive& arc, uint16_t id) {
    const Bundle b = Bundle::load(arc, id);
    const auto ms = b.members();
    for (size_t i = 0; i + 1 < ms.size(); ++i)
        if (!ms[i].payload.empty() && ms[i].payload[0] == 0 && isBmfSong(ms[i + 1].payload)) return parseBmfSong(ms[i + 1].payload);
    return std::nullopt;
}

bool writeText(const std::string& path, const std::string& text) {
    return writeWholeFile(pathFromUtf8(path), std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

bool writeWav(const std::string& path, const std::vector<float>& samples, int rate) {
    std::vector<uint8_t> bytes;
    bytes.reserve(samples.size() * 2);
    for (const float f : samples) {
        const auto v = static_cast<int16_t>(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
        bytes.push_back(static_cast<uint8_t>(v));
        bytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
    }
    return writeWholeFile(pathFromUtf8(path), mods::makeWav(static_cast<uint32_t>(rate), 1, 16, bytes));
}

std::string speakerLogText(const std::vector<SpeakerWrite>& log) {
    std::string t;
    for (const SpeakerWrite& w : log)
        t += w.port61 ? std::format("{} p61 {:02x}{}\n", w.tick, w.value, w.command ? " c" : "")
                      : std::format("{} div {:04x}{}\n", w.tick, w.value, w.command ? " c" : "");
    return t;
}

// Runs WHAT on the audio object; returns the tick to stop at.
struct Job {
    GgAudio& audio;
    const Archive& arc;
    const std::vector<std::string>& args;
    std::vector<uint8_t> songMemory;  // `song`: the registered copy

    uint32_t requestedTicks(uint32_t dflt) const {
        if (const auto t = optionValue(args, "--ticks")) return std::min<uint32_t>(parseUnsigned(*t), kMaxTicks);
        return std::min(dflt, kMaxTicks);
    }

    // Runs until `active` is false (+ the tail) or the requested length.
    template <class Active>
    void runWhile(Active active, uint32_t dflt, std::vector<float>* pcm) {
        const uint64_t start = audio.tickIndex();
        if (optionValue(args, "--ticks")) {
            audio.advanceTo(start + requestedTicks(0), pcm);
            return;
        }
        while (audio.tickIndex() < start + std::min(dflt, kMaxTicks) && active()) audio.advanceTo(audio.tickIndex() + 1, pcm);
        audio.advanceTo(audio.tickIndex() + kTailTicks, pcm);
    }

    void run(const std::vector<std::string>& what, std::vector<float>* pcm) {
        if (what.empty()) throw std::runtime_error("nothing to play (song, music, effect or soundtest)");
        const std::string& kind = what[0];
        const bool loop = hasFlag(args, "--loop");
        if (kind == "song" && what.size() == 2) {
            songMemory = loadPayload(arc, what[1]);
            const BmfSongInfo info = parseBmfSong(songMemory);
            const int slot = audio.call([&](SndRes& r) { return r.driver().registerSong(songMemory, 0); });
            if (slot < 0)
                throw std::runtime_error(std::format("the song (class {}) does not play in the {} mode ({})", info.songClass,
                                                     soundModeName(audio.mode()), slot));
            audio.call([&](SndRes& r) { return r.driver().play(slot, 0, 0, loop ? 1 : 0); });
            const uint32_t len = info.ticks + 2;
            if (loop) {
                audio.advanceTo(audio.tickIndex() + requestedTicks(len), pcm);
            } else {
                runWhile([&] { return audio.call([&](SndRes& r) { return r.driver().isActive(slot); }) == 1; }, len + 1, pcm);
            }
            audio.call([&](SndRes& r) { return r.driver().freeSlot(slot); });
        } else if (kind == "music" && what.size() == 2) {
            const uint16_t id = parseId(what[1]);
            const int h = audio.loadMusicBank(id);
            if (const auto a = optionValue(args, "--arrangement")) audio.selectArrangement(h, static_cast<int>(parseUnsigned(*a)));
            const bool once = hasFlag(args, "--once");
            audio.playMusic(h, 0, once ? 0 : 1);
            const auto info = musicBankSong(arc, static_cast<uint16_t>(id + audio.fmPresent()));
            const uint32_t len = (info ? info->ticks : 0) + 2;
            if (once) runWhile([&] { return audio.isPlaying(h) == 1; }, len + 1, pcm);
            else audio.advanceTo(audio.tickIndex() + requestedTicks(len), pcm);
        } else if (kind == "effect" && what.size() == 3) {
            const int h = audio.loadEffectBank(parseId(what[1]));
            const int k = static_cast<int>(parseUnsigned(what[2]));
            const int p = optionValue(args, "--priority") ? static_cast<int>(parseUnsigned(*optionValue(args, "--priority"))) : 4;
            audio.setEffectBank(h);
            audio.playEffect(h + k, loop ? 1 : 0, p);
            if (loop) audio.advanceTo(audio.tickIndex() + requestedTicks(kMaxTicks / 15), pcm);
            else runWhile([&] { return audio.isPlaying(h + k) == 1; }, kMaxTicks, pcm);
        } else if (kind == "soundtest" && what.size() == 2) {
            const auto named = SoundTestSchedule::named(what[1]);
            const SoundTestSchedule schedule = named ? *named : SoundTestSchedule::parse(what[1]);
            SoundTest test(audio);
            test.run(schedule, pcm, optionValue(args, "--ticks") ? std::optional<uint64_t>(requestedTicks(0)) : std::nullopt);
        } else {
            throw std::runtime_error("expected: song ID[/M] | music ID | effect ID K | soundtest NAME|SCRIPT");
        }
    }
};

GgAudioConfig configFor(const std::vector<std::string>& args, SoundMode mode, bool logOpl) {
    GgAudioConfig cfg;
    cfg.mode = mode;
    cfg.logOpl = logOpl;
    cfg.logSpeaker = optionValue(args, "--speaker-log").has_value();
    if (const auto r = optionValue(args, "--rate")) cfg.render.sampleRate = static_cast<int>(parseUnsigned(*r));
    if (hasFlag(args, "--raw")) {
        cfg.render.lowPassHz = 0;
        cfg.render.dcBlockHz = 0;
    }
    return cfg;
}

int finishLogs(const std::vector<std::string>& args, const GgAudio& audio) {
    if (const auto p = optionValue(args, "--speaker-log"); p && !writeText(*p, speakerLogText(audio.speakerLog()))) {
        err(std::format("ggport: cannot write {}\n", *p));
        return 1;
    }
    if (const auto p = optionValue(args, "--opl-log"); p && !audio.oplLog().save(*p)) {
        err(std::format("ggport: cannot write {}\n", *p));
        return 1;
    }
    return 0;
}

int cmdSound(const std::vector<std::string>& args) {
    auto pos = positionals(args, "--gg-sound");
    if (pos.size() < 3) throw std::runtime_error("usage: --gg-sound fm|speaker|off WHAT OUT.wav");
    const auto mode = parseSoundMode(pos[0]);
    if (!mode) throw std::runtime_error(std::format("unknown sound mode '{}' (fm, speaker, off)", pos[0]));
    const std::string outPath = pos.back();
    const std::vector<std::string> what(pos.begin() + 1, pos.end() - 1);
    const GgFiles files = openFolder(args);
    GgAudio audio(files.exe(), files.archive(), configFor(args, *mode, optionValue(args, "--opl-log").has_value()));
    std::vector<float> pcm;
    Job job{audio, files.archive(), args, {}};
    job.run(what, &pcm);
    if (!writeWav(outPath, pcm, audio.sampleRate())) {
        err(std::format("ggport: cannot write {}\n", outPath));
        return 1;
    }
    out(std::format("{}: {} samples ({:.2f} s, {} sound ticks) at {} Hz\n", outPath, pcm.size(),
                    static_cast<double>(pcm.size()) / audio.sampleRate(), audio.tickIndex(), audio.sampleRate()));
    return finishLogs(args, audio);
}

// --seed-dro FILE: the driver's B0h shadows of voices 0..5 from a capture's register dump.
void seedFromDro(const std::vector<std::string>& args, GgAudio& audio) {
    const auto p = optionValue(args, "--seed-dro");
    if (!p) return;
    const auto bytes = readWholeFile(pathFromUtf8(*p));
    if (!bytes) throw std::runtime_error(std::format("cannot read {}", *p));
    const DroFile dro = parseDro(*bytes);
    audio.call([&](SndRes& r) { seedB0Shadows(r.driver(), dro); });
}

int cmdOplLog(const std::vector<std::string>& args) {
    auto pos = positionals(args, "--gg-oplog");
    SoundMode mode = SoundMode::Fm;
    if (!pos.empty() && pos[0] != "song" && pos[0] != "music" && pos[0] != "effect" && pos[0] != "soundtest") {
        const auto m = parseSoundMode(pos[0]);
        if (!m) throw std::runtime_error(std::format("unknown sound mode '{}' (fm, off)", pos[0]));
        mode = *m;
        pos.erase(pos.begin());
    }
    if (pos.size() < 2) throw std::runtime_error("usage: --gg-oplog [fm|off] WHAT OUT.txt");
    const std::string outPath = pos.back();
    const std::vector<std::string> what(pos.begin(), pos.end() - 1);
    const GgFiles files = openFolder(args);
    GgAudio audio(files.exe(), files.archive(), configFor(args, mode, true));
    seedFromDro(args, audio);
    Job job{audio, files.archive(), args, {}};
    job.run(what, nullptr);
    if (!audio.oplLog().save(outPath)) {
        err(std::format("ggport: cannot write {}\n", outPath));
        return 1;
    }
    out(std::format("{}: {} OPL writes in {} sound ticks\n", outPath, audio.oplLog().entries().size(), audio.tickIndex()));
    return finishLogs(args, audio);
}

int cmdDroCompare(const std::vector<std::string>& args) {
    const auto pos = positionals(args, "--gg-dro-compare");
    if (pos.size() != 2) throw std::runtime_error("usage: --gg-dro-compare LOG.txt REF.dro");
    const auto logBytes = readWholeFile(pathFromUtf8(pos[0]));
    const auto droBytes = readWholeFile(pathFromUtf8(pos[1]));
    if (!logBytes || !droBytes) throw std::runtime_error(std::format("cannot read {}", logBytes ? pos[1] : pos[0]));
    const auto log = parseOplLogText(std::string_view(reinterpret_cast<const char*>(logBytes->data()), logBytes->size()));
    const DroFile dro = parseDro(*droBytes);
    const size_t from = optionValue(args, "--from") ? parseUnsigned(*optionValue(args, "--from")) : 0;
    const auto base = optionValue(args, "--base").value_or("engine");
    if (base != "engine" && base != "dump") throw std::runtime_error("--base engine|dump");
    const DroComparison c = compareWithDro(dro, log, from, GgAudio::kOplWriteSeconds * 1000.0,
                                           base == "dump" ? CompareBase::CaptureDump : CompareBase::Engine);
    out(std::format("engine log: {} writes; .dro: {} writes ({} in the dump), {} ms\n", log.size(), dro.writes.size(), dro.dumpSize, dro.lengthMs));
    out(c.summary());
    if (const auto p = optionValue(args, "--pairs")) {
        std::string t = "ms tick k call\n";
        for (const auto& q : c.pairs) t += std::format("{} {} {} {}\n", q.ms, q.tick, q.inTick, q.command ? 1 : 0);
        if (!writeText(*p, t)) throw std::runtime_error(std::format("cannot write {}", *p));
    }
    return c.found && c.matched == c.refWrites ? 0 : 1;
}

// --gg-soundtest-check NAME|all [--captures DIR]: the named schedules against their captures.
int cmdSoundtestCheck(const std::vector<std::string>& args) {
    const auto pos = positionals(args, "--gg-soundtest-check");
    if (pos.size() != 1) throw std::runtime_error("usage: --gg-soundtest-check NAME|all [--captures DIR]");
    std::optional<std::string> dir = optionValue(args, "--captures");
    if (!dir) dir = getEnv("GG_CAPTURES");
    if (!dir) throw std::runtime_error("give --captures DIR (the folder with audio/*.dro) or set GG_CAPTURES");
    const GgFiles files = openFolder(args);
    std::vector<std::string> names;
    if (pos[0] == "all") names = SoundTestSchedule::names();
    else names.push_back(pos[0]);
    int failed = 0;
    for (const std::string& n : names) {
        const auto sch = SoundTestSchedule::named(n);
        if (!sch) throw std::runtime_error(std::format("no schedule '{}'", n));
        const auto file = pathFromUtf8(*dir) / "audio" / sch->capture;
        const DroComparison c = checkSchedule(files.exe(), files.archive(), *sch, file);
        const bool ok = c.found && c.matched == c.refWrites;
        failed += ok ? 0 : 1;
        out(std::format("{:<20} {:<30} {:>5} of {:>5} writes identical, {:>5} within half a tick of {:>5} timed (base {}{}){}\n", n,
                        sch->capture, c.matched, c.refWrites, c.within05, c.timed, sch->compareWithDump ? "dump" : "engine",
                        sch->seedFromCapture ? ", seeded" : "", ok ? "" : "  " + c.firstMismatch));
    }
    return failed ? 1 : 0;
}

int cmdSongs(const std::vector<std::string>& args) {
    const GgFiles files = openFolder(args);
    const Archive& arc = files.archive();
    std::string t = "path\tclass\tbytes\tevents\tticks\tseconds\tpatches\tchannels\n";
    size_t songs = 0, cls1 = 0, cls3 = 0, events = 0, errors = 0;
    const auto add = [&](const std::string& path, std::span<const uint8_t> p) {
        try {
            const BmfSongInfo g = parseBmfSong(p);
            ++songs;
            (g.songClass == 1 ? cls1 : cls3)++;
            events += g.events.size();
            std::string ch;
            for (int c = 0; c < bmf::kChannelCount; ++c)
                if (g.usedChannelMask() & (1u << c)) ch += (ch.empty() ? "" : ",") + std::to_string(c);
            t += std::format("{}\t{}\t{}\t{}\t{}\t{:.3f}\t{}\t{}\n", path, g.songClass, p.size(), g.events.size(), g.ticks,
                             g.ticks / ggSoundTickHz(), g.patches.size(), ch);
        } catch (const std::exception& e) {
            ++errors;
            err(std::format("ERROR {}: {}\n", path, e.what()));
        }
    };
    for (const uint16_t id : arc.ids()) {
        const Resource r = arc.load(id);
        if (isBmfSong(r.bytes)) {
            add(std::format("{:04x}", id), r.bytes);
        } else if (r.resType() == ResType::Bundle && looksLikeBundle(r.bytes)) {
            for (const BundleMember& m : parseBundle(r.bytes))
                if (isBmfSong(m.payload)) add(std::format("{:04x}/{}", id, m.index), m.payload);
        }
    }
    out(t);
    err(std::format("bmf check: {} songs ({} class 1, {} class 3), {} events; {} error{}\n", songs, cls1, cls3, events, errors,
                    errors == 1 ? "" : "s"));
    return errors ? 1 : 0;
}

}  // namespace

bool audioCli(const std::vector<std::string>& args, int& exitCode) {
    const auto it = std::find_if(args.begin(), args.end(), [](const std::string& a) {
        return std::find(std::begin(kCommands), std::end(kCommands), a) != std::end(kCommands);
    });
    if (it == args.end()) return false;
    try {
        if (*it == "--gg-sound") exitCode = cmdSound(args);
        else if (*it == "--gg-oplog") exitCode = cmdOplLog(args);
        else if (*it == "--gg-dro-compare") exitCode = cmdDroCompare(args);
        else if (*it == "--gg-soundtest-check") exitCode = cmdSoundtestCheck(args);
        else exitCode = cmdSongs(args);
    } catch (const std::exception& e) {
        err(std::format("ggport: {}\n", e.what()));
        exitCode = 1;
    }
    std::fflush(stdout);
    return true;
}

}  // namespace tmport::gg
