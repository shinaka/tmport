#include "audio/audio_cli.h"

#include <SDL3/SDL_filesystem.h>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string_view>

#include "audio/audio_tables.h"
#include "audio/midi_backend.h"
#include "audio/midi_driver.h"
#include "audio/midi_render.h"
#include "audio/midi_sequencer.h"
#include "audio/sf2_synth.h"
#include "audio/sf2_writer.h"
#include "audio/smf.h"
#include "audio/sound.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/util/log.h"
#include "mischief/vfs/data_source.h"
#include "res/game_files.h"

namespace tmport::audio {

namespace {

template <class... Args>
void out(std::format_string<Args...> fmt, Args&&... args) {
    const std::string s = std::format(fmt, std::forward<Args>(args)...);
    std::fwrite(s.data(), 1, s.size(), stdout);
}

bool parseInt(std::string_view s, int& v) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc() && p == s.data() + s.size() && !s.empty();
}

bool parseDouble(std::string_view s, double& v) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc() && p == s.data() + s.size() && !s.empty();
}

struct AudioCliOptions {
    enum class Cmd { None, DumpSsnd, MidiDump, MidiRender, MakeTestSf2 } cmd = Cmd::None;
    std::string dataPath;
    int soundId = 0;
    std::string outPath;
    std::string midiArg;
    MusicSet set = MusicSet::Extended;
    bool brief = false;
    bool verbose = false;
    // --midi-render
    std::string soundFont;
    double seconds = 0.0;
    int rate = 44100;
    bool loop = false;
};

std::optional<std::filesystem::path> resolveDataPath(const AudioCliOptions& o) {
    if (!o.dataPath.empty()) return mischief::pathFromUtf8(o.dataPath);
    if (auto env = mischief::getEnv("TM_DATA")) return mischief::pathFromUtf8(*env);
    const char* base = SDL_GetBasePath();
    return mischief::vfs::autoDetectDataPath(base ? mischief::pathFromUtf8(base) : std::filesystem::path());
}

std::string formatMs(uint64_t us) {
    const uint64_t ms = us / 1000;
    return std::format("{}:{:02}.{:03}", ms / 60000, (ms / 1000) % 60, ms % 1000);
}

int dumpSsnd(const AudioCliOptions& o) {
    const auto dataPath = resolveDataPath(o);
    if (!dataPath) {
        std::fprintf(stderr, "tmport: the game data was not found. Pass --data <TMTWINCD folder or CD image> or set TM_DATA.\n");
        return 2;
    }
    ResourceManager::Options ro;
    ro.requireSounds = false;
    GameFiles files = GameFiles::open(*dataPath, ro);
    ResourceSoundLoader loader(*files.res);
    const auto s = loader.load(o.soundId);
    if (!s) {
        std::fprintf(stderr, "tmport: SSND %d does not exist\n", o.soundId);
        return 1;
    }
    const auto wav = makeWav(*s);
    if (!mischief::writeWholeFile(mischief::pathFromUtf8(o.outPath), wav)) {
        std::fprintf(stderr, "tmport: cannot write %s\n", o.outPath.c_str());
        return 1;
    }
    out("SSND {}: {} Hz, {} samples, {:.3f} s -> {} ({} bytes)\n", o.soundId, s->sampleRate, s->pcm.size(), s->seconds(),
        o.outPath, wav.size());
    return 0;
}

// Plays the song through the sequencer on a simulated 1 ms clock and checks every message
// against its scheduled time.
void simulate(const std::shared_ptr<const MidiSong>& song) {
    uint64_t now = 0;
    RecordingMidiBackend rec([&now] { return now; });
    rec.open();
    MidiSequencer seq(rec);
    seq.setSong(song);
    seq.start(0, 0);
    bool ended = false;
    while (!ended && now <= song->lengthUs + 1'000'000) {
        ended = seq.advance(now);
        if (!ended) now += 1000;
    }
    std::vector<const MidiEvent*> ch;
    for (const auto& e : song->events)
        if (e.isChannel()) ch.push_back(&e);
    size_t mismatched = 0;
    uint64_t maxLate = 0;
    for (size_t i = 0; i < ch.size() && i < rec.log.size(); ++i) {
        const auto& m = rec.log[i];
        if (m.status != ch[i]->status || m.d1 != ch[i]->data1 || m.d2 != ch[i]->data2 || m.timeUs < ch[i]->us) ++mismatched;
        if (m.timeUs >= ch[i]->us) maxLate = std::max(maxLate, m.timeUs - ch[i]->us);
    }
    out("sequencer check (simulated 1 ms clock, recording back end): {} of {} channel events sent, {} mismatched, "
        "max lateness {} us, end notification at {} us ({}), {}\n",
        rec.log.size(), ch.size(), mismatched, maxLate, now, formatMs(now), ended ? "ended" : "DID NOT END");
}

// A MIDI file named on the command line: a file on disk, a path inside the game folder, or a tune id.
struct LoadedMidi {
    std::shared_ptr<const MidiSong> song;
    std::string name;
    std::optional<int> loopPos;  // tune ids: the game's loop rule (DS:0a0e)
};

// 0 on success, else the exit code (the message is printed).
int loadMidiArg(const AudioCliOptions& o, LoadedMidi& m) {
    std::vector<uint8_t> bytes;
    std::string name = o.midiArg;
    std::optional<int> loopPos;
    const auto fsPath = mischief::pathFromUtf8(o.midiArg);
    std::error_code ec;
    if (std::filesystem::is_regular_file(fsPath, ec)) {
        auto b = mischief::readWholeFile(fsPath);
        if (!b) {
            std::fprintf(stderr, "tmport: cannot read %s\n", o.midiArg.c_str());
            return 1;
        }
        bytes = std::move(*b);
    } else {
        const auto dataPath = resolveDataPath(o);
        if (!dataPath) {
            std::fprintf(stderr, "tmport: %s is not a file and the game data was not found (--data / TM_DATA)\n",
                         o.midiArg.c_str());
            return 2;
        }
        auto source = mischief::vfs::openDataSource(*dataPath);
        std::string rel = o.midiArg;
        int tuneId = 0;
        if (parseInt(o.midiArg, tuneId)) {
            auto exe = ExeTables::load(*source);
            const AudioTables tables = AudioTables::fromExe(*exe);
            const int idx = tables.tuneIndex(tuneId);
            if (idx < 0) {
                std::fprintf(stderr, "tmport: %d is not a tune id (DS:09fa)\n", tuneId);
                return 1;
            }
            // Resolve the name exactly as Midi_OpenTrack does.
            std::string file = tables.tuneFiles[static_cast<size_t>(idx)];
            if (!file.empty()) file[0] = o.set == MusicSet::Extended ? 'E' : 'B';
            rel = tables.soundsDir + file;
            loopPos = tables.loopPos[static_cast<size_t>(idx)];
        }
        auto f = source->open(rel);
        if (!f) {
            std::fprintf(stderr, "tmport: %s not found in %s\n", rel.c_str(), source->describe().c_str());
            return 1;
        }
        bytes = f->readAll();
        name = rel;
    }
    m.song = std::make_shared<const MidiSong>(parseSmf(std::move(bytes)));
    m.name = std::move(name);
    m.loopPos = loopPos;
    return 0;
}

int midiDump(const AudioCliOptions& o) {
    LoadedMidi m;
    if (const int rc = loadMidiArg(o, m)) return rc;
    const auto& song = m.song;
    const std::string& name = m.name;
    const std::optional<int>& loopPos = m.loopPos;
    const MidiSong& s = *song;
    out("{}: SMF format {}, {} track(s), division {:#06x} ", name, s.format, s.tracks.size(), s.division);
    if (s.ppqn)
        out("({} ticks per quarter note)", s.ppqn);
    else
        out("(SMPTE {} fps x {} ticks per frame)", s.smpteFps, s.smpteTicksPerFrame);
    out(", {} bytes\n", s.bytes.size());
    out("tempo map:\n");
    for (const auto& t : s.tempoMap)
        out("  tick {:>7}  {:>10} us  {:>8} us/quarter ({:.2f} bpm)\n", t.tick, t.us, t.usPerQuarter, 60e6 / t.usPerQuarter);
    for (size_t i = 0; i < s.tracks.size(); ++i)
        out("track {}: {} events, end tick {}{}\n", i, s.tracks[i].eventCount, s.tracks[i].endTick,
            s.tracks[i].hasEndOfTrack ? "" : " (no end-of-track event)");

    if (!o.brief) {
        out("events:\n  {:>7} {:>10} {:>3}  event\n", "tick", "time ms", "trk");
        for (const auto& e : s.events)
            out("  {:>7} {:>10.3f} {:>3}  {}\n", e.tick, e.us / 1000.0, e.track, describeEvent(s, e));
    }

    // Summary.
    size_t metas = 0, sysex = 0, offs = 0;
    std::map<int, size_t> notesPerChannel;
    std::map<int, std::set<int>> programs;
    std::set<int> controllers;
    for (const auto& e : s.events) {
        if (e.isMeta()) ++metas;
        if (e.isSysEx()) ++sysex;
        if (!e.isChannel()) continue;
        if (e.isNoteOn()) notesPerChannel[e.channel() + 1]++;
        if (e.isNoteOff()) ++offs;
        if (e.command() == 0xC0) programs[e.channel() + 1].insert(e.data1);
        if (e.command() == 0xB0) controllers.insert(e.data1);
    }
    out("summary: {} channel events ({} note-on, {} note-off), {} meta, {} sysex\n", s.channelEventCount(), s.noteOnCount(),
        offs, metas, sysex);
    std::string chans;
    for (const auto& [ch, n] : notesPerChannel) {
        std::string progs;
        for (int p : programs[ch]) progs += std::format("{}{}", progs.empty() ? "" : "/", p);
        chans += std::format(" {}:{}notes{}", ch, n, progs.empty() ? "" : " prog " + progs);
    }
    out("  channels (1-based):{}\n", chans);
    std::string ctl;
    for (int c : controllers) ctl += std::format(" {}", c);
    out("  controllers used:{}\n", ctl.empty() ? " none" : ctl);
    out("  end tick {}, length {} us ({}), last note-on at tick {} ({} us)\n", s.endTick, s.lengthUs, formatMs(s.lengthUs),
        s.lastNoteOnTick(), s.tickToUs(s.lastNoteOnTick()));
    if (loopPos) out("  game loop rule (DS:0a0e): {}\n", *loopPos == -1 ? "plays once" : std::format("loops from position {}", *loopPos));
    for (const auto& w : s.warnings) out("  warning: {}\n", w);
    simulate(song);
    return 0;
}

double dbfs(double v) { return v > 0 ? 20.0 * std::log10(v) : -999.0; }

int midiRender(const AudioCliOptions& o) {
    LoadedMidi m;
    if (const int rc = loadMidiArg(o, m)) return rc;
    std::string error;
    std::shared_ptr<Sf2Synth> synth = Sf2Synth::loadFile(mischief::pathFromUtf8(o.soundFont), o.rate, error);
    if (!synth) {
        std::fprintf(stderr, "tmport: SoundFont %s: %s\n", o.soundFont.c_str(), error.c_str());
        return 1;
    }
    const MidiSong& s = *m.song;
    out("{}: song length {} ({:.3f} s), {} channel events\n", m.name, formatMs(s.lengthUs), s.lengthUs / 1e6,
        s.channelEventCount());
    out("SoundFont: {} ({} presets)\n", o.soundFont, synth->presetCount());
    // Programs the song selects that the SoundFont lacks (they play program 0 / drum kit 0).
    std::set<std::pair<int, int>> used, missing;
    for (const auto& e : s.events)
        if (e.isChannel() && e.command() == 0xC0) used.insert({e.channel(), e.data1});
    for (const auto& [ch, prog] : used) {
        const int bank = ch == 9 ? 128 : 0;
        if (synth->presetName(bank, prog).empty()) missing.insert({ch, prog});
    }
    if (!missing.empty()) {
        std::string list;
        for (const auto& [ch, prog] : missing) list += std::format(" ch{}:{}", ch + 1, prog);
        out("  {} of {} program selections are not in this SoundFont (they play program 0):{}\n", missing.size(),
            used.size(), list);
    }

    MidiRenderOptions ro;
    ro.seconds = o.seconds;
    if (o.loop) ro.loopFromUs = m.loopPos ? (*m.loopPos < 0 ? -1 : static_cast<int64_t>(*m.loopPos) * 1000) : 0;
    const MidiRenderResult r = renderMidiSong(m.song, synth, ro);
    const PcmStats st = analysePcm(r.pcm);
    const auto wav = makeWavStereo16(r.pcm, r.sampleRate);
    if (!mischief::writeWholeFile(mischief::pathFromUtf8(o.outPath), wav)) {
        std::fprintf(stderr, "tmport: cannot write %s\n", o.outPath.c_str());
        return 1;
    }
    const double secs = static_cast<double>(r.frames) / r.sampleRate;
    out("rendered {:.3f} s = {} frames at {} Hz, 16-bit stereo ({}) -> {} ({} bytes)\n", secs, r.frames, r.sampleRate,
        o.seconds > 0 ? std::string("--seconds") : std::format("song + {:.1f} s release tail", MidiRenderOptions{}.tailSeconds),
        o.outPath, wav.size());
    out("sequencer (simulated clock): {} messages to the synthesizer, {}, {} play(s){}, at most {} voices\n", r.messages,
        r.songEndUs ? std::format("end of song at {:.3f} s", r.songEndUs / 1e6) : std::string("song not finished"), r.plays,
        ro.loopFromUs >= 0 ? std::format(" (looping from {:.3f} s)", ro.loopFromUs / 1e6) : std::string(), r.maxVoices);
    out("level: peak {:.4f} ({:.1f} dBFS), RMS {:.4f} ({:.1f} dBFS), {} clipped samples\n", st.peak, dbfs(st.peak), st.rms,
        dbfs(st.rms), st.clipped);
    if (st.firstSound < 0)
        out("sound: none (silent output)\n");
    else
        out("sound: first at {:.3f} s, last at {:.3f} s\n", static_cast<double>(st.firstSound) / r.sampleRate,
            static_cast<double>(st.lastSound) / r.sampleRate);
    return 0;
}

int makeTestSf2(const AudioCliOptions& o) {
    const auto sf2 = makeTestSoundFont();
    if (!mischief::writeWholeFile(mischief::pathFromUtf8(o.outPath), sf2)) {
        std::fprintf(stderr, "tmport: cannot write %s\n", o.outPath.c_str());
        return 1;
    }
    out("{}: synthetic test SoundFont, {} bytes (sine bank 0:{}, square bank 0:{}, drum kit bank {}:0; 440 Hz at key {})\n",
        o.outPath, sf2.size(), testsf2::kSineProgram, testsf2::kSquareProgram, testsf2::kDrumBank, testsf2::kRootKey);
    return 0;
}

}  // namespace

std::string audioCliUsage() {
    return
        "\n"
        "Audio tools (offline; nothing is played):\n"
        "  --dump-ssnd <id> <out.wav>       write SSND resource <id> as a WAV file\n"
        "  --midi-dump <file|tune id>       parse a MIDI file (a path on disk, a path inside the\n"
        "                                   game folder such as SOUNDS\\<name>.MID, or a tune id\n"
        "                                   9000..9009) and print its events and timing\n"
        "      --midi-set B|E               tune ids: base (B*.MID) or extended (E*.MID, default) set\n"
        "      --brief                      omit the per-event listing\n"
        "  --midi-render <file|tune id> <out.wav> --soundfont <file.sf2>\n"
        "                                   play a MIDI file through the SoundFont synthesizer on a\n"
        "                                   simulated clock and write a 16-bit stereo WAV, with level\n"
        "                                   statistics (nothing is played)\n"
        "      --seconds <n>                output length (default: the song plus a 2 s release tail)\n"
        "      --loop                       loop the song as the game does (one-shot tunes play once)\n"
        "      --rate <hz>                  sample rate (default 44100)\n"
        "      --midi-set B|E               as for --midi-dump\n"
        "  --make-test-soundfont <out.sf2>  write a tiny synthetic SoundFont (sine, square and drum test\n"
        "                                   tones) for checking the SoundFont path without a real one\n";
}

bool runAudioCli(int argc, char** argv, int& exitCode) {
    bool mine = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--dump-ssnd" || a == "--midi-dump" || a == "--midi-render" || a == "--make-test-soundfont") mine = true;
    }
    if (!mine) return false;

    AudioCliOptions o;
    auto fail = [&](const std::string& msg) {
        std::fprintf(stderr, "tmport: %s\n%s", msg.c_str(), audioCliUsage().c_str());
        exitCode = 2;
        return true;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const bool hasArg = i + 1 < argc;
        if (a == "--data") {
            if (!hasArg) return fail("--data needs a path");
            o.dataPath = argv[++i];
        } else if (a == "--dump-ssnd") {
            if (o.cmd != AudioCliOptions::Cmd::None) return fail("only one command may be given");
            if (i + 2 >= argc || !parseInt(argv[i + 1], o.soundId)) return fail("--dump-ssnd needs <id> <out.wav>");
            o.outPath = argv[i + 2];
            o.cmd = AudioCliOptions::Cmd::DumpSsnd;
            i += 2;
        } else if (a == "--midi-dump") {
            if (o.cmd != AudioCliOptions::Cmd::None) return fail("only one command may be given");
            if (!hasArg) return fail("--midi-dump needs a file or tune id");
            o.midiArg = argv[++i];
            o.cmd = AudioCliOptions::Cmd::MidiDump;
        } else if (a == "--midi-render") {
            if (o.cmd != AudioCliOptions::Cmd::None) return fail("only one command may be given");
            if (i + 2 >= argc) return fail("--midi-render needs <file|tune id> <out.wav>");
            o.midiArg = argv[i + 1];
            o.outPath = argv[i + 2];
            o.cmd = AudioCliOptions::Cmd::MidiRender;
            i += 2;
        } else if (a == "--make-test-soundfont") {
            if (o.cmd != AudioCliOptions::Cmd::None) return fail("only one command may be given");
            if (!hasArg) return fail("--make-test-soundfont needs <out.sf2>");
            o.outPath = argv[++i];
            o.cmd = AudioCliOptions::Cmd::MakeTestSf2;
        } else if (a == "--soundfont") {
            if (!hasArg) return fail("--soundfont needs a .sf2 file");
            o.soundFont = argv[++i];
        } else if (a == "--seconds") {
            if (!hasArg || !parseDouble(argv[i + 1], o.seconds) || !(o.seconds > 0 && o.seconds <= 3600))
                return fail("--seconds needs a number of seconds (0 < n <= 3600)");
            ++i;
        } else if (a == "--rate") {
            if (!hasArg || !parseInt(argv[i + 1], o.rate) || o.rate < 8000 || o.rate > 192000)
                return fail("--rate needs a sample rate (8000..192000)");
            ++i;
        } else if (a == "--loop") {
            o.loop = true;
        } else if (a == "--midi-set") {
            if (!hasArg) return fail("--midi-set needs B or E");
            const std::string v = mischief::toUpperAscii(argv[++i]);
            if (v == "B")
                o.set = MusicSet::Base;
            else if (v == "E")
                o.set = MusicSet::Extended;
            else
                return fail("--midi-set needs B or E");
        } else if (a == "--brief") {
            o.brief = true;
        } else if (a == "--verbose") {
            o.verbose = true;
        } else {
            return fail(std::format("unknown argument '{}' for the audio tools", a));
        }
    }
    if (o.cmd == AudioCliOptions::Cmd::MidiRender && o.soundFont.empty())
        return fail("--midi-render needs --soundfont <file.sf2> (or make one with --make-test-soundfont)");
    mischief::log::setVerbose(o.verbose);
    try {
        switch (o.cmd) {
            case AudioCliOptions::Cmd::DumpSsnd: exitCode = dumpSsnd(o); break;
            case AudioCliOptions::Cmd::MidiRender: exitCode = midiRender(o); break;
            case AudioCliOptions::Cmd::MakeTestSf2: exitCode = makeTestSf2(o); break;
            default: exitCode = midiDump(o); break;
        }
    } catch (const mischief::MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        exitCode = 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        exitCode = 3;
    }
    return true;
}

}  // namespace tmport::audio
