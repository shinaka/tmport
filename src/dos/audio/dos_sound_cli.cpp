#include "dos/audio/dos_sound_cli.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <string_view>
#include <vector>

#include "dos/audio/dos_sound_renderer.h"
#include "dos/data/dos_exe.h"
#include "dos/save/dosbox_state.h"
#include "mischief/dos/audio/opl_log.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/vfs/directory_source.h"

namespace tmport::dos {

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

// The schedule of the DOSBox-X sound-test recordings (extracted/captures/dos/audio/*_soundtest_*,
// REFERENCE_CAPTURES.md R2.3): "delay:kind:id" with the delay in PIT ticks after the previous step,
// kind s = Sfx_Play, m = Music_Play, x = Music_Stop.
constexpr std::string_view kSoundTestScript =
    "100:x:0,600:s:0,600:s:1,600:s:2,600:s:3,600:s:4,600:s:5,600:s:6,600:s:7,600:s:8,600:s:9,600:s:10,"
    "600:s:11,700:m:0,12900:m:1,17300:m:2,6000:x:0";

struct Step {
    uint64_t tick = 0;  // absolute: the command runs right before this PIT tick
    char kind = 's';
    int id = 0;
    int variant = 0;  // m: the AdLib orchestration (rand() & 3), "m:tune/variant"
};

bool parseScript(std::string_view text, std::vector<Step>& steps) {
    uint64_t t = 0;
    while (!text.empty()) {
        const size_t comma = text.find(',');
        const std::string_view item = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        const size_t c1 = item.find(':'), c2 = item.rfind(':');
        int delay = 0, id = 0, variant = 0;
        if (c1 == std::string_view::npos || c2 == c1 || !parseInt(item.substr(0, c1), delay) || c2 != c1 + 2 || delay < 0)
            return false;
        std::string_view idText = item.substr(c2 + 1);
        const char kind = item[c1 + 1];
        if (const size_t slash = idText.find('/'); slash != std::string_view::npos) {
            if (kind != 'm' || !parseInt(idText.substr(slash + 1), variant) || variant < 0 || variant > 3) return false;
            idText = idText.substr(0, slash);
        }
        if (!parseInt(idText, id)) return false;
        if (kind != 's' && kind != 'm' && kind != 'x') return false;
        t += static_cast<uint64_t>(delay);
        steps.push_back({t, kind, id, variant});
    }
    return !steps.empty();
}

struct Options {
    SoundCard device = SoundCard::Speaker;
    std::string what;  // sfx | tune | soundtest | all
    int id = 0;
    std::string outPath;
    std::string dosData;
    bool raw = false;
    int rate = 0;  // 0: 44100 raw, 48000 smooth
    int loops = 1;
    std::optional<mischief::dos::NoiseType> noise;  // default: Ti15 with --raw (the oracle's), else Ncr8496
    double maxSeconds = 600.0;
    double lowPassHz = -1.0, dcBlockHz = -1.0;  // < 0: the renderer's defaults
    std::string script{kSoundTestScript};
    std::string stateLog;
    std::string oplLog;
    int variant = 0;  // tune: the AdLib orchestration
    std::string fromState;  // soundtest: start with the driver of this DOSBox-X state
};

// One line per tick in which the audible device state changed.
class StateLog {
public:
    explicit StateLog(bool on) : on_(on) {}
    void sample(uint64_t tick, DosSoundRenderer& r) {
        if (!on_) return;
        const mischief::dos::SpeakerSynth& s = r.speaker();
        if (s.enabled() != spkOn_ || (s.enabled() && s.divisor() != spkDiv_) || first_) {
            text_ += std::format("{} spk {} {} {:.2f}\n", tick, s.enabled() ? 1 : 0, s.divisor(), s.frequency());
            spkOn_ = s.enabled();
            spkDiv_ = s.divisor();
        }
        const mischief::dos::PsgSynth& p = r.psg();
        std::array<int, 8> st{p.period(0), p.period(1), p.period(2), p.attenuation(0),
                              p.attenuation(1), p.attenuation(2), p.attenuation(3), p.noiseControl()};
        if (st != psg_ || first_) {
            text_ += std::format("{} psg {} {} {} {} {} {} {} {}\n", tick, st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7]);
            psg_ = st;
        }
        first_ = false;
    }
    void event(const std::string& line) {
        if (on_) text_ += line;
    }
    bool enabled() const { return on_; }
    bool write(const std::string& path) const { return !on_ || mischief::writeWholeFile(mischief::pathFromUtf8(path), {reinterpret_cast<const uint8_t*>(text_.data()), text_.size()}); }

private:
    bool on_;
    bool first_ = true;
    bool spkOn_ = false;
    uint16_t spkDiv_ = 0;
    std::array<int, 8> psg_{};
    std::string text_;
};

// The OPL register writes, one line per write: 'tick reg value' (hex), tick = the PIT tick the write
// belongs to; a command's writes (Music_Play, Music_Stop between two interrupts) carry a fourth
// column 'c' and the tick they run before. tools/dos/oplcmp.py reads it (mischief/dos/audio/opl_log.h).
using OplLog = mischief::dos::OplWriteLog;

// The AdLib driver with its calls written to the state log ('tick opl tune|on|off ...') and its
// register writes to the OPL log.
class LoggingOpl final : public OplBackend {
public:
    LoggingOpl(const SoundTables& tables, double rate, StateLog& log, OplLog& oplLog) : inner_(tables, rate), log_(log) {
        inner_.setWriteObserver([this, &oplLog](uint8_t reg, uint8_t value) { oplLog.write(tick(), reg, value); });
    }
    void setRenderer(const DosSoundRenderer* r) { r_ = r; }
    void init() override { inner_.init(); }
    void playTune(int tune, int variant) override {
        log_.event(std::format("{} opl tune {} {}\n", tick(), tune, variant));
        inner_.playTune(tune, variant);
    }
    void noteOn(int voice, uint8_t semitone) override {
        log_.event(std::format("{} opl on {} {}\n", tick(), voice, semitone));
        inner_.noteOn(voice, semitone);
    }
    void noteOff(int voice) override {
        log_.event(std::format("{} opl off {} 0\n", tick(), voice));
        inner_.noteOff(voice);
    }
    void render(double* out, size_t n) override { inner_.render(out, n); }
    void skip(size_t n) override { inner_.skip(n); }
    bool restore(std::span<const uint8_t> dgroup) override { return inner_.restore(dgroup); }
    int playTuneTicks() const override { return inner_.playTuneTicks(); }

private:
    uint64_t tick() const { return r_ ? r_->ticks() : 0; }  // the tick being run
    AdLibOpl inner_;
    StateLog& log_;
    const DosSoundRenderer* r_ = nullptr;
};

// A renderer for the bank; with the AdLib device and a state or OPL log, the driver is logged too.
std::unique_ptr<DosSoundRenderer> makeRenderer(const SoundBank& bank, const DosRenderConfig& rc, StateLog& log, OplLog& oplLog) {
    if (bank.device != SoundCard::AdLib || (!log.enabled() && !oplLog.enabled())) return std::make_unique<DosSoundRenderer>(bank, rc);
    auto opl = std::make_unique<LoggingOpl>(*bank.tables, rc.sampleRate, log, oplLog);
    LoggingOpl* raw = opl.get();
    auto r = std::make_unique<DosSoundRenderer>(bank, rc, std::move(opl));
    raw->setRenderer(r.get());
    return r;
}

std::vector<uint8_t> wavMono16(std::span<const int16_t> pcm, int rate) {
    std::vector<uint8_t> w;
    const auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) w.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    const auto put16 = [&](uint16_t v) {
        w.push_back(static_cast<uint8_t>(v));
        w.push_back(static_cast<uint8_t>(v >> 8));
    };
    const auto bytes = static_cast<uint32_t>(pcm.size() * 2);
    w.insert(w.end(), {'R', 'I', 'F', 'F'});
    put32(36 + bytes);
    w.insert(w.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(16);
    put16(1);  // PCM
    put16(1);  // mono
    put32(static_cast<uint32_t>(rate));
    put32(static_cast<uint32_t>(rate) * 2);
    put16(2);
    put16(16);
    w.insert(w.end(), {'d', 'a', 't', 'a'});
    put32(bytes);
    for (int16_t s : pcm) put16(static_cast<uint16_t>(s));
    return w;
}

bool writeWav(const std::string& path, const std::vector<double>& samples, const Options& o, int rate) {
    const auto pcm = o.raw ? toPcm16Normalized(samples) : toPcm16(samples, 0.7);  // 0.7: the device's default volume
    const auto bytes = wavMono16(pcm, rate);
    if (!mischief::writeWholeFile(mischief::pathFromUtf8(path), bytes)) {
        std::fprintf(stderr, "tmport: cannot write %s\n", path.c_str());
        return false;
    }
    return true;
}

DosRenderConfig renderConfig(const Options& o) {
    DosRenderConfig rc;
    rc.mode = o.raw ? mischief::dos::SynthMode::Raw : mischief::dos::SynthMode::Smooth;
    rc.sampleRate = o.rate ? o.rate : (o.raw ? 44100 : 48000);
    rc.noise = o.noise.value_or(o.raw ? mischief::dos::NoiseType::Ti15 : mischief::dos::NoiseType::Ncr8496);
    if (o.lowPassHz >= 0) rc.lowPassHz = o.lowPassHz;
    if (o.dcBlockHz >= 0) rc.dcBlockHz = o.dcBlockHz;
    return rc;
}

// One effect or tune (dos_sound_renderer.h renderEntry), logged per tick.
OfflineRender renderOne(const SoundBank& bank, const Options& o, bool tune, int id, StateLog& log, OplLog& oplLog) {
    auto r = makeRenderer(bank, renderConfig(o), log, oplLog);
    return renderEntry(*r, tune, id, o.loops, o.maxSeconds, [&] { log.sample(r->ticks() - 1, *r); }, o.variant);
}

std::string fileTag(const SoundFile& f) {
    std::string t = mischief::toLowerAscii(f.name());
    std::replace(t.begin(), t.end(), '.', '_');
    return t;  // "sfx_ibm", "tunes_tan" (dos_sound.py's WAV names)
}

int runSoundTest(const SoundBank& bank, const Options& o, StateLog& log, OplLog& oplLog) {
    std::vector<Step> steps;
    if (!parseScript(o.script, steps)) {
        std::fprintf(stderr, "tmport: bad --script (expected delay:kind:id,... with kind s, m or x)\n");
        return 2;
    }
    const DosRenderConfig rc = renderConfig(o);
    auto rp = makeRenderer(bank, rc, log, oplLog);
    DosSoundRenderer& r = *rp;
    Sequencer& s = r.sequencer();
    s.setMusicLoop(true);  // DS:63ba was 1 in the recorded game states (snd_speaker/tandy/adlib)
    if (!o.fromState.empty()) {
        // The driver as a DOSBox-X state left it (as --dos-load-state continues it): the flags, the
        // effect and music blocks, the voice count, and on AdLib the OPL driver (its registers are
        // written again). The OPL log starts here, like a capture started on that state.
        const DosboxState st = DosboxState::load(mischief::pathFromUtf8(o.fromState));
        const auto dg = st.dgroup();
        const auto w = [&](uint16_t off) { return st.dsWord(off); };
        s.setSoundOn(w(0x63B4) != 0);
        s.setMusicOn(w(0x63B6) != 0);
        s.setSfxLoop(w(0x63B8) != 0);
        s.setMusicLoop(w(0x63BA) != 0);
        s.setMusicGate(w(0x37CE));
        oplLog.clear();
        const bool opl = r.restoreOpl(dg);
        s.restore(Sequencer::blockFromDgroup(dg.subspan(0x6448, Sequencer::kBlockBytes)),
                  Sequencer::blockFromDgroup(dg.subspan(0x64C0, Sequencer::kBlockBytes)), w(0x63BE), 0, opl);
    }
    size_t next = 0;
    r.setTickHook([&] {
        // ticks() is the number of ticks already run: the step for tick T runs before tick T.
        oplLog.setCommand(true);
        while (next < steps.size() && steps[next].tick <= r.ticks()) {
            const Step& st = steps[next++];
            if (st.kind == 's') s.sfxPlay(st.id);
            else if (st.kind == 'm') s.musicPlay(st.id, st.variant);
            else s.musicStop();
        }
        oplLog.setCommand(false);
    });
    const uint64_t end = std::min<uint64_t>(steps.back().tick + static_cast<uint64_t>(2.0 * mischief::dos::kPitTickHz),
                                            static_cast<uint64_t>(o.maxSeconds * mischief::dos::kPitTickHz));
    std::vector<double> samples;
    while (r.ticks() < end) {
        r.renderTick(samples);
        log.sample(r.ticks() - 1, r);
    }
    if (!writeWav(o.outPath, samples, o, rc.sampleRate)) return 1;
    out("soundtest ({}, {}): {} ticks, {:.2f} s, {} Hz -> {}\n", soundCardName(bank.device), o.raw ? "raw" : "smooth",
        r.ticks(), samples.size() / static_cast<double>(rc.sampleRate), rc.sampleRate, o.outPath);
    for (const Step& st : steps)
        out("  tick {:6}  {:8.3f} s  {}\n", st.tick, st.tick / mischief::dos::kPitTickHz,
            st.kind == 's' ? std::format("Sfx_Play({})", st.id)
            : st.kind == 'm' ? std::format("Music_Play({}), orchestration {}", st.id, st.variant)
                             : "Music_Stop");
    return 0;
}

std::optional<SoundCard> parseDevice(std::string_view s) {
    if (s == "speaker" || s == "ibm" || s == "pc") return SoundCard::Speaker;
    if (s == "tandy" || s == "psg") return SoundCard::Tandy;
    if (s == "adlib") return SoundCard::AdLib;
    return std::nullopt;
}

int usageError(const std::string& msg) {
    std::fprintf(stderr, "tmport: %s\n%s", msg.c_str(), dosSoundUsage().c_str());
    return 2;
}

}  // namespace

std::string dosSoundUsage() {
    return "DOS edition sound (offline, nothing is played):\n"
           "  --dos-sound <speaker|tandy|adlib> sfx <0..11> <out.wav>\n"
           "  --dos-sound <speaker|tandy|adlib> tune <0..2> <out.wav>\n"
           "  --dos-sound <speaker|tandy|adlib> soundtest <out.wav>   the DOSBox-X sound-test schedule\n"
           "  --dos-sound <speaker|tandy|adlib> all <out dir>         every effect and tune\n"
           "    --dos-data <dir>   the DOS game folder (SST.EXE, SFX.*, TUNES.*); default TM_DATA_DOS\n"
           "    --raw              exactly what tools/dos/dos_sound.py renders (44100 Hz, normalised)\n"
           "    --rate <hz>        output rate (default 48000; 44100 with --raw)\n"
           "    --loops <n>        play a tune n times (music loop flag DS:63ba)\n"
           "    --noise ti|ncr|sn  Tandy noise register (default ncr = NCR 8496; ti with --raw)\n"
           "    --seconds <s>      stop after s seconds\n"
           "    --lowpass <hz>     smooth-mode low-pass (default 7000, 0 = off); --dcblock <hz> (default 12)\n"
           "    --script <steps>   soundtest schedule, delay:kind:id,... (kind s effect, m tune, x stop;\n"
           "                       m:tune/v plays AdLib orchestration v = 0..3, else 0)\n"
           "    --variant <v>      tune: the AdLib orchestration (rand() & 3), default 0\n"
           "    --state-log <txt>  write the speaker/PSG state per changed PIT tick\n"
           "    --opl-log <txt>    AdLib: every OPL register write as 'tick reg value' (hex)\n"
           "    --from-state <sav> soundtest: start with the sound driver of a DOSBox-X state (its flags,\n"
           "                       blocks and AdLib driver), as --dos-load-state continues it\n"
           "  (AdLib: effects on the speaker; tunes by the original's AdLib driver on an emulated OPL2)\n";
}

std::optional<int> runDosSoundCommand(int argc, char** argv) {
    int at = -1;
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--dos-sound") at = i;
    if (at < 0) return std::nullopt;

    Options o;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const auto value = [&](std::string_view name) -> std::optional<std::string_view> {
            if (i + 1 >= argc) {
                usageError(std::string(name) + " needs a value");
                return std::nullopt;
            }
            return std::string_view(argv[++i]);
        };
        if (i == at) continue;
        if (a == "--dos-data" || a == "--data") {
            auto v = value(a);
            if (!v) return 2;
            o.dosData = *v;
        } else if (a == "--raw") {
            o.raw = true;
        } else if (a == "--rate") {
            auto v = value(a);
            if (!v) return 2;
            if (!parseInt(*v, o.rate) || o.rate < 8000 || o.rate > 192000) return usageError("bad --rate");
        } else if (a == "--loops") {
            auto v = value(a);
            if (!v) return 2;
            if (!parseInt(*v, o.loops) || o.loops < 1) return usageError("bad --loops");
        } else if (a == "--seconds") {
            auto v = value(a);
            if (!v) return 2;
            if (!parseDouble(*v, o.maxSeconds) || o.maxSeconds <= 0) return usageError("bad --seconds");
        } else if (a == "--lowpass" || a == "--dcblock") {
            auto v = value(a);
            if (!v) return 2;
            double& hz = a == "--lowpass" ? o.lowPassHz : o.dcBlockHz;
            if (!parseDouble(*v, hz) || hz < 0) return usageError(std::format("bad {}", a));
        } else if (a == "--noise") {
            auto v = value(a);
            if (!v) return 2;
            if (*v == "ti") o.noise = mischief::dos::NoiseType::Ti15;
            else if (*v == "ncr") o.noise = mischief::dos::NoiseType::Ncr8496;
            else if (*v == "sn") o.noise = mischief::dos::NoiseType::Sn76496;
            else return usageError("bad --noise (ti, ncr or sn)");
        } else if (a == "--script") {
            auto v = value(a);
            if (!v) return 2;
            o.script = *v;
        } else if (a == "--state-log") {
            auto v = value(a);
            if (!v) return 2;
            o.stateLog = *v;
        } else if (a == "--opl-log") {
            auto v = value(a);
            if (!v) return 2;
            o.oplLog = *v;
        } else if (a == "--from-state") {
            auto v = value(a);
            if (!v) return 2;
            o.fromState = *v;
        } else if (a == "--variant") {
            auto v = value(a);
            if (!v) return 2;
            if (!parseInt(*v, o.variant) || o.variant < 0 || o.variant > 3) return usageError("bad --variant (0..3)");
        } else if (a.starts_with("--")) {
            return usageError(std::format("unknown option {}", a));
        } else {
            pos.emplace_back(a);
        }
    }
    if (pos.size() < 3) return usageError("--dos-sound needs a device, what to render and an output");
    const auto dev = parseDevice(pos[0]);
    if (!dev) return usageError("device must be speaker, tandy or adlib");
    o.device = *dev;
    o.what = pos[1];
    if (o.what == "sfx" || o.what == "tune") {
        if (pos.size() != 4 || !parseInt(pos[2], o.id)) return usageError(o.what + " needs a number and an output file");
        const int limit = o.what == "sfx" ? SoundFile::kSfxCount : SoundFile::kTuneCount;
        if (o.id < 0 || o.id >= limit) return usageError(std::format("{} number must be 0..{}", o.what, limit - 1));
        o.outPath = pos[3];
    } else if (o.what == "soundtest" || o.what == "all") {
        if (pos.size() != 3) return usageError(o.what + " needs one output");
        o.outPath = pos[2];
    } else {
        return usageError("render sfx, tune, soundtest or all");
    }
    if (o.dosData.empty()) {
        if (auto env = mischief::getEnv("TM_DATA_DOS")) o.dosData = *env;
    }
    if (o.dosData.empty()) {
        std::fprintf(stderr, "tmport: the DOS game folder was not given. Pass --dos-data <dir> or set TM_DATA_DOS.\n");
        return 2;
    }

    try {
        const mischief::vfs::DirectorySource folder(mischief::pathFromUtf8(o.dosData));
        auto exeFile = folder.open("SST.EXE");
        if (!exeFile) throw mischief::MissingFileError("SST.EXE", "SST.EXE is missing from the DOS game folder");
        const DosExe exe = DosExe::load(exeFile->readAll());
        auto tables = std::make_shared<const SoundTables>(SoundTables::fromExe(exe));
        const SoundBank bank = SoundBank::load(folder, o.device, tables);
        StateLog log(!o.stateLog.empty());
        OplLog oplLog(!o.oplLog.empty());
        int rc = 0;
        if (o.what == "soundtest") {
            rc = runSoundTest(bank, o, log, oplLog);
        } else if (o.what == "all") {
            const auto dir = mischief::pathFromUtf8(o.outPath);
            std::filesystem::create_directories(dir);
            for (int tune = 0; tune < 2; ++tune) {
                const SoundFile& f = tune ? bank.music : bank.sfx;
                const int n = tune ? SoundFile::kTuneCount : SoundFile::kSfxCount;
                for (int i = 0; i < n; ++i) {
                    const auto r = renderOne(bank, o, tune != 0, i, log, oplLog);
                    const std::string path = mischief::pathToUtf8(dir / std::format("{}_{:02}.wav", fileTag(f), i));
                    if (!writeWav(path, r.samples, o, renderConfig(o).sampleRate)) return 1;
                    out("{:<10} {:2}  {:6} ticks  {:6.2f} s  {}\n", f.name(), i, r.ticks, r.ticks / mischief::dos::kPitTickHz, path);
                }
            }
        } else {
            const bool tune = o.what == "tune";
            const auto r = renderOne(bank, o, tune, o.id, log, oplLog);
            if (!writeWav(o.outPath, r.samples, o, renderConfig(o).sampleRate)) return 1;
            out("{} {} ({}, {}): {} ticks, {:.2f} s -> {}\n", (tune ? bank.music : bank.sfx).name(), o.id,
                soundCardName(o.device), o.raw ? "raw" : "smooth", r.ticks, r.ticks / mischief::dos::kPitTickHz, o.outPath);
        }
        if (rc == 0 && !log.write(o.stateLog)) {
            std::fprintf(stderr, "tmport: cannot write %s\n", o.stateLog.c_str());
            return 1;
        }
        if (rc == 0 && !oplLog.save(o.oplLog)) {
            std::fprintf(stderr, "tmport: cannot write %s\n", o.oplLog.c_str());
            return 1;
        }
        return rc;
    } catch (const mischief::MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
}

}  // namespace tmport::dos
