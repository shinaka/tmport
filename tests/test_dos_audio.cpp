// DOS edition sound (src/dos/audio): the sound files and DGROUP tables, the 491b driver rules
// (fmt_sound.md §5, §7), the speaker and SN76496 models, the renderer clock and the output device.
// Synthetic streams and tables test the rules without game data; with TM_DATA_DOS the user's files
// are checked by lengths, event counts and CRC-32s of rendered PCM only (the renders were compared
// sample for sample with tools/dos/dos_sound.py and with the DOSBox-X recordings).
// No test opens an audible device: the SDL test forces the silent "dummy" driver.

#include <doctest/doctest.h>

#include <SDL3/SDL_timer.h>

#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "dos/audio/dos_audio_device.h"
#include "dos/audio/dos_sound_renderer.h"
#include "dos/audio/psg_synth.h"
#include "dos/audio/sequencer.h"
#include "dos/audio/sound_data.h"
#include "dos/audio/speaker_synth.h"
#include "dos_test_data.h"
#include "util/error.h"
#include "vfs/directory_source.h"

using namespace tmport;
using namespace tmport::dos;

namespace {

// ---------------------------------------------------------------------- synthetic driver data

// A 64 KB DGROUP with made-up tables at the driver's offsets: periods that only need to descend,
// envelope 0 = {0, 2, 4} then 0xFF, envelope i > 0 = {i & 15}.
std::shared_ptr<const SoundTables> syntheticTables() {
    std::vector<uint8_t> ds(0x10000, 0);
    const auto put16 = [&](uint16_t at, uint16_t v) {
        ds[at] = static_cast<uint8_t>(v);
        ds[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    put16(SoundTables::kDsPeriodTables, SoundTables::kDsSpeakerPeriods);
    put16(SoundTables::kDsPeriodTables + 2, SoundTables::kDsPsgPeriods);
    for (int i = 0; i < 12; ++i) {
        put16(static_cast<uint16_t>(SoundTables::kDsSpeakerPeriods + 2 * i), static_cast<uint16_t>(36000 - 1500 * i));
        put16(static_cast<uint16_t>(SoundTables::kDsPsgPeriods + 2 * i), static_cast<uint16_t>(1700 - 70 * i));
    }
    uint16_t at = 0x6600;
    for (int e = 0; e < SoundTables::kEnvelopeCount; ++e) {
        put16(static_cast<uint16_t>(SoundTables::kDsEnvelopes + 2 * e), at);
        if (e == 0) {
            for (int v : {0, 2, 4}) ds[at++] = static_cast<uint8_t>(v);
        } else {
            ds[at++] = static_cast<uint8_t>(e & 15);
        }
        ds[at++] = 0xFF;
    }
    return std::make_shared<SoundTables>(SoundTables::fromDgroup(std::move(ds)));
}

using Stream = std::vector<uint8_t>;
constexpr uint8_t kEnd[] = {0xFF, 0xFF};

// A sound file: header of `headerBytes` with entries[e][v] -> stream offsets; streams appended.
std::vector<uint8_t> makeFile(int voices, const std::vector<std::vector<Stream>>& entries) {
    const size_t header = voices == 1 ? 0x96 : 0x230;
    std::vector<uint8_t> f(header, 0);
    const auto put16 = [&](size_t at, size_t v) {
        f[at] = static_cast<uint8_t>(v);
        f[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    put16(0, header);  // first slot = header length (it is also entry 0's first offset)
    for (size_t e = 0; e < entries.size(); ++e)
        for (int v = 0; v < voices; ++v) {
            put16(2 * (e * voices + v), f.size());
            const Stream& s = entries[e][static_cast<size_t>(v)];
            f.insert(f.end(), s.begin(), s.end());
            f.insert(f.end(), std::begin(kEnd), std::end(kEnd));
        }
    return f;
}

// Commands and notes (fmt_sound.md §4.2).
Stream tempo(uint8_t v) { return {0x4D, v}; }
Stream release(uint8_t v) { return {0x0D, v}; }
Stream envelope(uint8_t v) { return {0x5D, v}; }
Stream sustain(uint8_t v) { return {0x6D, v}; }
Stream note(uint8_t oct, uint8_t semitone, uint8_t code) { return {static_cast<uint8_t>(oct << 4 | semitone), code}; }
Stream rest(uint8_t code) { return {0x00, code}; }
Stream cat(std::initializer_list<Stream> parts) {
    Stream s;
    for (const Stream& p : parts) s.insert(s.end(), p.begin(), p.end());
    return s;
}

// 12 effects and 3 tunes with one voice (speaker files) or four (Tandy files: voice 0 carries the
// stream, the others are unused unless given).
struct Synthetic {
    std::shared_ptr<const SoundTables> tables = syntheticTables();
    std::vector<Stream> sfx = std::vector<Stream>(12, cat({tempo(25), note(3, 1, 2)}));  // 25 ticks each
    std::vector<Stream> tunes = std::vector<Stream>(3, cat({tempo(25), note(2, 5, 0), note(2, 8, 0)}));  // 2 x 100 ticks

    SoundBank bank(SoundCard card) const {
        const auto entries = [&](const std::vector<Stream>& list, int voices) {
            std::vector<std::vector<Stream>> e;
            for (const Stream& s : list) {
                std::vector<Stream> vs(static_cast<size_t>(voices));
                vs[0] = s;
                for (int v = 1; v < voices; ++v) vs[static_cast<size_t>(v)] = {};  // only the end marker: unused
                e.push_back(vs);
            }
            return e;
        };
        return SoundBank::fromBytes(card, makeFile(sfxVoices(card), entries(sfx, sfxVoices(card))),
                                    makeFile(musicVoices(card), entries(tunes, musicVoices(card))), tables);
    }
};

// Records what the driver writes.
struct RecordingPorts final : SoundPorts {
    struct Event {
        uint64_t tick;
        char kind;  // 'd' divisor, 'e' speaker enable, 'p' PSG byte, 't' OPL tune, 'n' OPL note on, 'f' OPL note off
        int a = 0, b = 0;
    };
    std::vector<Event> ev;
    uint64_t tick = 0;
    bool speakerOn = false;
    uint16_t divisor = 0;
    void speakerDivisor(uint16_t d) override {
        divisor = d;
        ev.push_back({tick, 'd', d});
    }
    void speakerEnable(bool on) override {
        speakerOn = on;
        ev.push_back({tick, 'e', on ? 1 : 0});
    }
    void psgWrite(uint8_t v) override { ev.push_back({tick, 'p', v}); }
    void oplPlayTune(int t, int variant) override { ev.push_back({tick, 't', t, variant}); }
    void oplNoteOn(int v, uint8_t s) override { ev.push_back({tick, 'n', v, s}); }
    void oplNoteOff(int v) override { ev.push_back({tick, 'f', v}); }
    size_t count(char k) const {
        size_t n = 0;
        for (const Event& e : ev) n += e.kind == k;
        return n;
    }
};

void run(Sequencer& s, RecordingPorts& p, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        s.timerTick();
        ++p.tick;
    }
}

}  // namespace

// ----------------------------------------------------------------------------------- files

TEST_CASE("DOS audio: sound file headers and entries") {
    Synthetic syn;
    const SoundBank b = syn.bank(SoundCard::Tandy);
    CHECK(b.sfx.voicesPerEntry() == 4);
    CHECK(b.sfx.headerBytes() == 0x230);
    CHECK(b.sfx.entryCount() == 12);
    CHECK(b.music.entryCount() == 3);
    CHECK(b.sfx.byteAt(100000) == 0xFF);  // past the static buffer: end of stream

    auto bad = makeFile(1, std::vector<std::vector<Stream>>(12, std::vector<Stream>(1, Stream{})));
    bad[0] = 0x98;  // header length
    CHECK_THROWS_AS(SoundFile::parse(bad, 1, 12, SoundFile::kSfxBufferSize, "X"), FormatError);
    const auto few = makeFile(1, std::vector<std::vector<Stream>>(5, std::vector<Stream>(1, Stream{})));
    CHECK_THROWS_AS(SoundFile::parse(few, 1, 12, SoundFile::kSfxBufferSize, "X"), FormatError);
    CHECK_THROWS_AS(SoundFile::parse(std::vector<uint8_t>(2000, 0), 1, 12, SoundFile::kSfxBufferSize, "X"), FormatError);

    std::vector<uint8_t> ds(0x10000, 0);
    CHECK_THROWS_AS(SoundTables::fromDgroup(ds), FormatError);  // no period table pointers
}

// ------------------------------------------------------------------------------ driver rules

TEST_CASE("DOS audio: note length, release gap and end of an effect (speaker)") {
    Synthetic syn;
    const SoundBank bank = syn.bank(SoundCard::Speaker);
    RecordingPorts p;
    Sequencer s(bank, p);
    s.sfxPlay(0);  // tempo 25 -> 100 ticks per whole note; C (octave nibble 3), quarter = 25 ticks
    CHECK(s.sfxBlock().status == 1);
    s.timerTick();
    CHECK(p.speakerOn);
    CHECK(p.divisor == (36000 >> 3));
    CHECK(s.sfxBlock().ticksLeft[0] == 24);
    // On for 25 - release (6) = 19 ticks: off when ticks left == release - 1.
    for (int t = 1; t < 19; ++t) {
        s.timerTick();
        CHECK(p.speakerOn);
    }
    s.timerTick();  // tick 19
    CHECK_FALSE(p.speakerOn);
    for (int t = 20; t < 25; ++t) s.timerTick();
    CHECK(s.sfxBlock().status == 2);
    s.timerTick();  // tick 25: the end event
    CHECK(s.sfxBlock().status == 0);
    CHECK(s.sfxBlock().id == Sequencer::kIdle);
}

TEST_CASE("DOS audio: effect priority by id, lost on the Tandy") {
    Synthetic syn;
    {
        const SoundBank bank = syn.bank(SoundCard::Speaker);
        RecordingPorts p;
        Sequencer s(bank, p);
        s.sfxPlay(5);
        run(s, p, 3);
        s.sfxPlay(7);  // higher id: ignored while 5 plays
        CHECK(s.sfxBlock().id == 5);
        CHECK(s.sfxBlock().status == 2);
        s.sfxPlay(5);  // equal id restarts
        CHECK(s.sfxBlock().status == 1);
        s.sfxPlay(3);
        CHECK(s.sfxBlock().id == 3);
        s.setSoundOn(false);
        s.sfxPlay(0);  // sound off: nothing happens
        CHECK(s.sfxBlock().id == 3);
    }
    {
        const SoundBank bank = syn.bank(SoundCard::Tandy);
        RecordingPorts p;
        Sequencer s(bank, p);
        s.sfxPlay(3);
        // Sfx_Play called Sfx_Stop after storing the id: the id is idle again (491b:0714).
        CHECK(s.sfxBlock().id == Sequencer::kIdle);
        CHECK(s.sfxBlock().status == 1);
        CHECK(p.count('p') == 4 * 3);  // Sfx_Stop: attenuation 15 + period 0 on all four voices
        run(s, p, 3);
        s.sfxPlay(7);  // replaces effect 3
        CHECK(s.sfxBlock().status == 1);
        CHECK(s.sfxBlock().ptr[0] == bank.sfx.headerSlot(4 * 7));
    }
}

TEST_CASE("DOS audio: music frozen under an effect (speaker, Tandy), not on AdLib") {
    Synthetic syn;
    for (SoundCard card : {SoundCard::Speaker, SoundCard::Tandy, SoundCard::AdLib}) {
        CAPTURE(soundCardName(card));
        const SoundBank bank = syn.bank(card);
        RecordingPorts p;
        Sequencer s(bank, p);
        s.musicPlay(0, 2);
        run(s, p, 5);
        const uint16_t before = s.musicBlock().ticksLeft[0];
        s.sfxPlay(1);
        run(s, p, 10);
        if (card == SoundCard::AdLib)
            CHECK(s.musicBlock().ticksLeft[0] == before - 10);
        else
            CHECK(s.musicBlock().ticksLeft[0] == before);
        run(s, p, 20);  // the effect (26 ticks) ends at its 26th tick
        CHECK(s.sfxBlock().status == 0);
        const uint16_t after = s.musicBlock().ticksLeft[0];
        run(s, p, 3);
        CHECK(s.musicBlock().ticksLeft[0] == after - 3);  // the music goes on
        if (card == SoundCard::AdLib) {
            REQUIRE(p.count('t') == 1);
            CHECK(p.ev[0].kind == 't');
            CHECK(p.ev[0].b == 2);  // the orchestration variant passed through
            CHECK(p.count('n') >= 1);
            CHECK(p.count('e') >= 1);  // the effect on the speaker
        }
    }
}

TEST_CASE("DOS audio: AdLib note semitones and the voice groups") {
    Synthetic syn;
    syn.tunes[1] = cat({tempo(25), note(3, 1, 2), note(4, 12, 2)});
    const SoundBank bank = syn.bank(SoundCard::AdLib);
    RecordingPorts p;
    Sequencer s(bank, p);
    s.musicPlay(1, 0);
    run(s, p, 30);
    std::vector<int> semis;
    for (const auto& e : p.ev)
        if (e.kind == 'n') semis.push_back(e.b);
    REQUIRE(semis.size() == 2);
    CHECK(semis[0] == 0 + 12 * 3);   // note - 1 + 12 * octave
    CHECK(semis[1] == 11 + 12 * 4);
    CHECK(p.count('f') >= 1);        // release gap: AdLib_NoteOff
}

TEST_CASE("DOS audio: music pause gate and Music_Play with the music off") {
    Synthetic syn;
    const SoundBank bank = syn.bank(SoundCard::Speaker);
    RecordingPorts p;
    Sequencer s(bank, p);
    s.musicPlay(0, 0);
    run(s, p, 4);
    const uint16_t left = s.musicBlock().ticksLeft[0];
    s.setMusicPaused(true);  // DS:37ce = 0: the ISR skips Sound_Tick for the music
    run(s, p, 10);
    CHECK(s.musicBlock().ticksLeft[0] == left);
    s.sfxPlay(2);  // an effect still plays while paused
    run(s, p, 2);
    CHECK(s.sfxBlock().status == 2);
    s.setMusicPaused(false);
    run(s, p, 30);  // effect ends, music resumes
    CHECK(s.musicBlock().ticksLeft[0] < left);

    s.setMusicOn(false);
    s.musicPlay(2, 0);  // the block is cleared and stays stopped
    CHECK(s.musicBlock().status == 0);
    CHECK(s.musicBlock().id == 2);
    CHECK(s.musicBlock().tempo == 0);
}

TEST_CASE("DOS audio: music loop flag restarts in the same tick") {
    Synthetic syn;
    const SoundBank bank = syn.bank(SoundCard::Speaker);
    RecordingPorts p;
    Sequencer s(bank, p);
    s.musicPlay(0, 0);
    run(s, p, 200);  // two whole notes
    CHECK(s.musicBlock().status == 2);
    s.timerTick();  // tick 200: end
    CHECK(s.musicBlock().status == 0);

    Sequencer l(bank, p);
    l.setMusicLoop(true);
    l.musicPlay(0, 0);
    run(l, p, 201);
    CHECK(l.musicBlock().status == 2);
    CHECK(l.musicLoops() == 1);
    CHECK(l.musicBlock().ticksLeft[0] == 99);  // the first note again, already one tick in
}

TEST_CASE("DOS audio: Tandy attenuation pass (envelope, 0xFF tick, sustain, release)") {
    Synthetic syn;
    syn.tunes[0] = cat({tempo(40), envelope(0), sustain(6), release(3), note(3, 1, 4)});  // 160 >> 4 = 10 ticks
    const SoundBank bank = syn.bank(SoundCard::Tandy);
    RecordingPorts p;
    Sequencer s(bank, p);
    s.musicPlay(0, 0);
    std::vector<int> att;  // voice 0 attenuation written per tick (-1: none)
    for (int t = 0; t < 11; ++t) {
        p.ev.clear();
        s.timerTick();
        int a = -1;
        for (const auto& e : p.ev)
            if ((e.a & 0xF0) == 0x90) a = e.a & 0x0F;
        att.push_back(a);
    }
    // 0, 2, 4 (envelope), 0xFF (no write), sustain 6 while ticks left > release, then 15; the end
    // event silences the voice (VoiceOff).
    CHECK(att == std::vector<int>{0, 2, 4, -1, 6, 6, 15, 15, 15, 15, 15});
}

TEST_CASE("DOS audio: Tandy periods (octave folding, 10-bit limit) and the noise voice") {
    Synthetic syn;
    syn.tunes[0] = cat({tempo(25), note(0, 1, 2), note(1, 1, 2), note(2, 1, 2)});
    const SoundBank bank = syn.bank(SoundCard::Tandy);
    RecordingPorts p;
    Sequencer s(bank, p);
    s.musicPlay(0, 0);
    const auto periodAt = [&](uint64_t tick) {
        int lo = -1, period = -1;
        for (const auto& e : p.ev) {
            if (e.tick != tick || e.kind != 'p') continue;
            if ((e.a & 0xF0) == 0x80) lo = e.a & 0x0F;
            else if (!(e.a & 0x80) && lo >= 0) period = lo | (e.a << 4);
        }
        return period;
    };
    run(s, p, 60);
    CHECK(periodAt(0) == 1700 / 2);  // octave 0: 1700 > 0x3FF, halved
    CHECK(periodAt(25) == 1700 / 2); // octave 1 = octave 0
    CHECK(periodAt(50) == 1700 / 2); // octave 2: 1700 >> 1

    // Voice 3: noise control 0xE0 + note - 1.
    std::vector<std::vector<Stream>> e(12, std::vector<Stream>(4));
    e[0][0] = tempo(25);
    e[0][3] = cat({envelope(5), note(0, 8, 2)});
    const SoundBank nb = SoundBank::fromBytes(SoundCard::Tandy, makeFile(4, e),
                                              makeFile(4, std::vector<std::vector<Stream>>(3, std::vector<Stream>(4))), syn.tables);
    RecordingPorts q;
    Sequencer n(nb, q);
    n.sfxPlay(0);
    q.ev.clear();
    n.timerTick();
    bool sawNoise = false;
    for (const auto& ev : q.ev) sawNoise |= ev.a == 0xE7;
    CHECK(sawNoise);
}

TEST_CASE("DOS audio: zero-length streams end instead of hanging") {
    Synthetic syn;
    syn.sfx[4] = cat({tempo(0), note(3, 1, 2), note(3, 2, 2)});  // tempo 0: every duration is 0
    const SoundBank bank = syn.bank(SoundCard::Speaker);
    RecordingPorts p;
    Sequencer s(bank, p);
    s.sfxPlay(4);
    s.timerTick();
    CHECK(s.sfxBlock().status == 0);
    CHECK_FALSE(s.streamGuardTripped());
}

// ---------------------------------------------------------------------------------- synths

TEST_CASE("DOS audio: SN76496 register writes") {
    PsgSynth psg(SynthMode::Raw, 44100, NoiseType::Ti15);
    psg.write(0x8A);  // voice 0 period low = 0xA
    psg.write(0x05);  // data: high 6 bits
    CHECK(psg.period(0) == 0x5A);
    psg.write(0xB3);  // voice 1 attenuation 3
    CHECK(psg.attenuation(1) == 3);
    psg.write(0x07);  // data byte after an attenuation latch rewrites it
    CHECK(psg.attenuation(1) == 7);
    psg.write(0xE6);
    CHECK(psg.noiseControl() == 6);
    CHECK(psg.toneFrequency(2) == doctest::Approx(3579545.0 / (32 * 1024)));  // period 0 = 1024
}

TEST_CASE("DOS audio: noise register restarts (TI every write, NCR 8496 on a mode change)") {
    const auto noiseRun = [](NoiseType type, bool rewrite, uint8_t second) {
        PsgSynth psg(SynthMode::Raw, 48000, type);
        psg.write(0xF0);  // noise attenuation 0
        psg.write(0xE4);  // white, clock / 512
        std::vector<double> a(500, 0.0), b(500, 0.0);
        psg.render(a.data(), a.size());
        if (rewrite) psg.write(second);
        psg.render(b.data(), b.size());
        return std::make_pair(a, b);
    };
    // TI: rewriting the same value restarts the sequence (the phase of the shift clock continues,
    // so compare the bit pattern over many samples).
    const auto ti = noiseRun(NoiseType::Ti15, true, 0xE4);
    const auto tiFree = noiseRun(NoiseType::Ti15, false, 0);
    CHECK(ti.second != tiFree.second);
    // NCR: the same value does not restart; flipping the white/periodic bit does.
    const auto ncrSame = noiseRun(NoiseType::Ncr8496, true, 0xE4);
    const auto ncrFree = noiseRun(NoiseType::Ncr8496, false, 0);
    CHECK(ncrSame.second == ncrFree.second);
    const auto ncrFlip = noiseRun(NoiseType::Ncr8496, true, 0xE0);
    CHECK(ncrFlip.second != ncrFree.second);
}

TEST_CASE("DOS audio: counter reload at the half period (Smooth mode)") {
    BoxSquare osc;
    std::vector<double> out(48, 0.0);
    osc.renderCounter(1000.0, 1.0, 10, out.data(), 48000.0);  // 48 samples per period
    osc.renderCounter(2000.0, 1.0, 38, out.data() + 10, 48000.0);
    for (int i = 0; i < 23; ++i) CHECK(out[static_cast<size_t>(i)] > 0.99);  // first half of the old period
    for (int i = 24; i < 36; ++i) CHECK(out[static_cast<size_t>(i)] < -0.99); // new period's second half: 12 samples
    CHECK(out[37] > 0.99);

    SpeakerSynth spk(SynthMode::Smooth, 48000);
    spk.setDivisor(1193);  // ~1 kHz
    std::vector<double> s(10, 0.0);
    spk.render(s.data(), s.size());
    CHECK(s[5] == 0.0);  // gate low: silent
    spk.setEnabled(true);
    spk.render(s.data(), s.size());
    CHECK(s[0] > 0.49);  // the counter restarts with a new period (output high first)
}

TEST_CASE("DOS audio: renderer clock (PIT ticks by sample count)") {
    Synthetic syn;
    const SoundBank bank = syn.bank(SoundCard::Tandy);
    for (int rate : {48000, 44100, 49716}) {
        CAPTURE(rate);
        DosRenderConfig cfg;
        cfg.sampleRate = rate;
        DosSoundRenderer r(bank, cfg);
        std::vector<double> buf(static_cast<size_t>(rate));
        r.render(buf.data(), buf.size());
        CHECK(r.ticks() == 237);  // 236.6974 ticks per second, the first at sample 0
        CHECK(r.samplesPerTick() == doctest::Approx(rate * 5041.0 / 1193182.0));
    }
    // Chunk sizes do not change the output (Raw mode).
    DosRenderConfig raw;
    raw.mode = SynthMode::Raw;
    raw.sampleRate = 44100;
    DosSoundRenderer a(bank, raw), b(bank, raw);
    a.sequencer().musicPlay(0, 0);
    b.sequencer().musicPlay(0, 0);
    std::vector<double> whole(20000), parts(20000);
    a.render(whole.data(), whole.size());
    for (size_t at = 0, k = 1; at < parts.size(); at += k, k = k * 3 % 997 + 1)
        b.render(parts.data() + at, std::min(k, parts.size() - at));
    double maxDiff = 0;
    for (size_t i = 0; i < whole.size(); ++i) maxDiff = std::max(maxDiff, std::abs(whole[i] - parts[i]));
    CHECK(maxDiff < 1e-9);
}

// ------------------------------------------------------------------------- output device

TEST_CASE("DOS audio: device command queue and the AdLib rand() draw (offline)") {
    Synthetic syn;
    auto bank = std::make_shared<const SoundBank>(syn.bank(SoundCard::AdLib));
    int draws = 0;
    DosAudioConfig cfg;
    DosAudioDevice dev(bank, cfg, [&] { return ++draws * 5; });
    dev.musicPlay(1);
    CHECK(draws == 1);  // drawn on the caller's thread
    dev.setMusicOn(false);
    dev.musicPlay(1);  // music off: Music_Play returns before AdLib_PlayTune
    CHECK(draws == 1);
    dev.setMusicOn(true);
    dev.musicPlay(2);
    CHECK(draws == 2);
    std::vector<float> out(4800);
    CHECK(dev.renderOffline(out.data(), out.size()) == out.size());
    const auto st = dev.status();
    CHECK(st.musicStatus == 2);
    CHECK(st.musicId == 2);
    CHECK(st.ticks == 24);  // 4800 samples at 48 kHz
    dev.sfxPlay(0);
    dev.renderOffline(out.data(), out.size());
    float peak = 0;
    for (float v : out) peak = std::max(peak, std::abs(v));
    CHECK(peak > 0.05f);
    dev.setOutputEnabled(false);
    dev.renderOffline(out.data(), out.size());
    peak = 0;
    for (float v : out) peak = std::max(peak, std::abs(v));
    CHECK(peak == 0.0f);
    CHECK(dev.status().ticks == 72);  // the driver kept running

    // Lockstep capture: whole PIT ticks; a queued command starts at the first of them.
    std::vector<float> ticks;
    dev.setOutputEnabled(true);
    dev.sfxStop();
    const size_t n5 = dev.renderOfflineTicks(5, ticks);
    CHECK(ticks.size() == n5);
    CHECK(n5 > 1013);  // the rest of tick 71, then 5 x 202.79 samples
    CHECK(n5 < 1217);
    CHECK(dev.status().ticks == 77);
    const size_t m5 = dev.renderOfflineTicks(5, ticks);  // on a tick boundary now: whole ticks only
    CHECK((m5 == 1013 || m5 == 1014));
    CHECK(dev.status().ticks == 82);
    CHECK(dev.status().sfxStatus == 0);

    // No rand() on the speaker.
    auto spk = std::make_shared<const SoundBank>(syn.bank(SoundCard::Speaker));
    DosAudioDevice d2(spk, cfg, [&] { return ++draws; });
    d2.musicPlay(0);
    CHECK(draws == 2);
}

TEST_CASE("DOS audio: device on the silent SDL dummy driver") {
    Synthetic syn;
    auto bank = std::make_shared<const SoundBank>(syn.bank(SoundCard::Speaker));
    DosAudioConfig cfg;
    cfg.sdlDriver = "dummy";  // never a real device
    DosAudioDevice dev(bank, cfg);
    if (!dev.open()) {
        MESSAGE("SDL dummy audio driver unavailable: test skipped");
        return;
    }
    CHECK(dev.driverName() == "dummy");
    dev.musicPlay(0);
    const uint64_t t0 = SDL_GetTicks();
    while (dev.status().ticks < 30 && SDL_GetTicks() - t0 < 3000) SDL_Delay(5);
    CHECK(dev.status().ticks >= 30);  // the callback pulls samples and the sequencer advances
    CHECK(dev.status().musicStatus == 2);
    float buf[16];
    CHECK(dev.renderOffline(buf, 16) == 0);  // not while the stream is open
    dev.close();
    CHECK_FALSE(dev.isOpen());
}

// --------------------------------------------------------------------- the user's files

namespace {

uint32_t crc32(std::span<const int16_t> pcm) {  // zlib's CRC-32 of the little-endian samples
    uint32_t c = 0xFFFFFFFFu;
    for (int16_t s : pcm)
        for (int k = 0; k < 2; ++k) {
            c ^= static_cast<uint8_t>(static_cast<uint16_t>(s) >> (8 * k));
            for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    return ~c;
}

SoundBank userBank(const DosExe& exe, SoundCard card) {
    const vfs::DirectorySource folder(*testdata::dosDataPath());
    return SoundBank::load(folder, card, std::make_shared<const SoundTables>(SoundTables::fromExe(exe)));
}

}  // namespace

TEST_CASE("DOS audio: the user's sound files and driver tables") {
    TM_REQUIRE_DOS_DATA();
    const auto tables = SoundTables::fromExe(*dosExe);
    CHECK(tables.speakerPeriod(0) == 36488);  // 1193182 / 32.70 Hz (C1)
    CHECK(tables.psgPeriod(0) == 1720);
    const SoundBank spk = userBank(*dosExe, SoundCard::Speaker);
    const SoundBank tan = userBank(*dosExe, SoundCard::Tandy);
    const SoundBank adl = userBank(*dosExe, SoundCard::AdLib);
    CHECK(spk.sfx.fileSize() == 1680);
    CHECK(spk.music.fileSize() == 3152);
    CHECK(tan.sfx.fileSize() == 1622);
    CHECK(tan.music.fileSize() == 5000);
    CHECK(adl.sfx.name() == "SFX.IBM");
    CHECK(adl.music.name() == "TUNES.TAN");
    CHECK(spk.sfx.entryCount() == 12);
    CHECK(spk.music.entryCount() == 3);
    CHECK(tan.sfx.entryCount() == 12);
    CHECK(tan.music.entryCount() == 3);
}

TEST_CASE("DOS audio: lengths of every effect and tune (ticks, as dos_sound.py lists them)") {
    TM_REQUIRE_DOS_DATA();
    const std::array<uint64_t, 15> speaker = {193, 501, 121, 22, 49, 49, 200, 145, 75, 477, 137, 167, 12545, 16997, 5761};
    const std::array<uint64_t, 15> tandy = {193, 155, 139, 22, 49, 49, 326, 145, 76, 477, 176, 167, 12993, 17032, 5761};
    for (SoundCard card : {SoundCard::Speaker, SoundCard::Tandy}) {
        const SoundBank bank = userBank(*dosExe, card);
        for (int i = 0; i < 15; ++i) {
            CAPTURE(soundCardName(card));
            CAPTURE(i);
            DosRenderConfig cfg;
            cfg.mode = SynthMode::Raw;
            cfg.sampleRate = 8000;  // only the length matters here
            DosSoundRenderer r(bank, cfg);
            const auto res = renderEntry(r, i >= 12, i >= 12 ? i - 12 : i);
            CHECK(res.ticks == (card == SoundCard::Speaker ? speaker : tandy)[static_cast<size_t>(i)]);
        }
    }
}

TEST_CASE("DOS audio: raw renders equal tools/dos/dos_sound.py (CRC-32 of the 16-bit PCM)") {
    TM_REQUIRE_DOS_DATA();
    struct Case {
        SoundCard card;
        bool tune;
        int id;
        size_t samples;
        uint32_t crc;  // zlib.crc32 of dos_sound.py's WAV data (44100 Hz, normalised)
    };
    const Case cases[] = {
        {SoundCard::Speaker, false, 0, 38163, 0xb6b8fec8}, {SoundCard::Speaker, false, 1, 95548, 0x99e0876a},
        {SoundCard::Speaker, false, 9, 91077, 0x3c800999}, {SoundCard::Speaker, true, 2, 1075567, 0x2e0f4aa1},
        {SoundCard::Tandy, false, 1, 31083, 0xa1210183},   {SoundCard::Tandy, false, 6, 62943, 0xcafaf6b0},
        {SoundCard::Tandy, false, 10, 34996, 0xfeda0549},  {SoundCard::Tandy, true, 2, 1075567, 0xa0c4e8aa},
    };
    const SoundBank spk = userBank(*dosExe, SoundCard::Speaker);
    const SoundBank tan = userBank(*dosExe, SoundCard::Tandy);
    for (const Case& c : cases) {
        CAPTURE(soundCardName(c.card));
        CAPTURE(c.tune);
        CAPTURE(c.id);
        DosRenderConfig cfg;
        cfg.mode = SynthMode::Raw;
        cfg.sampleRate = 44100;
        cfg.noise = NoiseType::Ti15;
        DosSoundRenderer r(c.card == SoundCard::Speaker ? spk : tan, cfg);
        const auto res = renderEntry(r, c.tune, c.id);
        const auto pcm = toPcm16Normalized(res.samples);
        CHECK(pcm.size() == c.samples);
        CHECK(crc32(pcm) == c.crc);
    }
}

TEST_CASE("DOS audio: the sound-test schedule (event counts checked against the DOSBox-X recordings)") {
    TM_REQUIRE_DOS_DATA();
    // REFERENCE_CAPTURES.md R2.3: Music_Stop at +100 ticks, effects 0..11 every 600 ticks, tunes 0, 1,
    // 2 at +700, +12900, +17300, stop at +6000; the music loop flag was set.
    struct Step {
        uint64_t tick;
        char kind;
        int id;
    };
    std::vector<Step> steps = {{100, 'x', 0}};
    for (int i = 0; i < 12; ++i) steps.push_back({700 + 600u * static_cast<uint64_t>(i), 's', i});
    steps.push_back({8000, 'm', 0});
    steps.push_back({20900, 'm', 1});
    steps.push_back({38200, 'm', 2});
    steps.push_back({44200, 'x', 0});
    for (SoundCard card : {SoundCard::Speaker, SoundCard::AdLib}) {
        CAPTURE(soundCardName(card));
        const SoundBank bank = userBank(*dosExe, card);
        RecordingPorts p;
        Sequencer s(bank, p);
        s.setMusicLoop(true);
        size_t next = 0;
        for (uint64_t t = 0; t < 44200 + 474; ++t) {
            while (next < steps.size() && steps[next].tick == t) {
                const Step& st = steps[next++];
                if (st.kind == 's') s.sfxPlay(st.id);
                else if (st.kind == 'm') s.musicPlay(st.id, 0);
                else s.musicStop();
            }
            p.tick = t;
            s.timerTick();
        }
        size_t onsets = 0, noteOns = 0;
        bool on = false;
        for (const auto& e : p.ev) {
            if (e.kind == 'e') {
                onsets += e.a && !on;
                on = e.a != 0;
            }
            if (e.kind == 'n' && e.a < 3) ++noteOns;
        }
        if (card == SoundCard::Speaker) {
            // Speaker off->on writes (897 at tick resolution; the 896 measured in the DOSBox-X
            // recording start within 0.12 tick of the engine's ticks, and no recorded note is missing).
            CHECK(onsets == 898);
            CHECK(s.musicLoops() == 3);  // each tune looped once before the next command
        } else {
            CHECK(noteOns == 1465);  // AdLib_NoteOn calls of voices 0-2; each has a key-on in the .dro
        }
    }
}
