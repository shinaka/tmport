// DOS edition sound (src/dos/audio): the sound files and DGROUP tables, the 491b driver rules
// (fmt_sound.md §5, §7), the speaker and SN76496 models, the renderer clock and the output device.
// Synthetic streams and tables test the rules without game data; with TM_DATA_DOS the user's files
// are checked by lengths, event counts and CRC-32s of rendered PCM only (the renders were compared
// sample for sample with tools/dos/dos_sound.py and with the DOSBox-X recordings). The AdLib driver
// (4ac9) is checked register by register on synthetic tables, on the user's tunes (counts and
// CRC-32s) and, with TM_DOS_CAPTURES, against DOSBox-X's raw OPL capture of every orchestration.
// No test opens an audible device: the SDL test forces the silent "dummy" driver.

#include <doctest/doctest.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "dos/audio/adlib_driver.h"
#include "dos/audio/dos_audio_device.h"
#include "dos/audio/dos_sound_renderer.h"
#include "dos/audio/opl_backend.h"
#include "dos/audio/sequencer.h"
#include "dos/audio/sound_data.h"
#include "dos/save/dosbox_state.h"
#include "dos_test_data.h"
#include "mischief/dos/audio/opl_chip.h"
#include "mischief/dos/audio/psg_synth.h"
#include "mischief/dos/audio/speaker_synth.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/vfs/directory_source.h"

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
    CHECK_THROWS_AS(SoundFile::parse(bad, 1, 12, SoundFile::kSfxBufferSize, "X"), mischief::FormatError);
    const auto few = makeFile(1, std::vector<std::vector<Stream>>(5, std::vector<Stream>(1, Stream{})));
    CHECK_THROWS_AS(SoundFile::parse(few, 1, 12, SoundFile::kSfxBufferSize, "X"), mischief::FormatError);
    CHECK_THROWS_AS(SoundFile::parse(std::vector<uint8_t>(2000, 0), 1, 12, SoundFile::kSfxBufferSize, "X"), mischief::FormatError);

    std::vector<uint8_t> ds(0x10000, 0);
    CHECK_THROWS_AS(SoundTables::fromDgroup(ds), mischief::FormatError);  // no period table pointers
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
    raw.mode = mischief::dos::SynthMode::Raw;
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
    const mischief::vfs::DirectorySource folder(*testdata::dosDataPath());
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

TEST_CASE("DOS audio: a driver restored from its DGROUP blocks continues the tune and the effect") {
    TM_REQUIRE_DOS_DATA();
    // The DGROUP layout of a block round-trips.
    Sequencer::Block blk;
    blk.status = 2;
    blk.id = 1;
    blk.tempo = 448;
    blk.ptr = {600, 1700, 2500, 2950};
    blk.envPos = {0x66A3, 0x68EE, 0x68EE, 0};
    blk.tie = {0, 3, 1, 0};
    const auto bytes = Sequencer::blockToDgroup(blk);
    CHECK(bytes.size() == 0x78u);
    CHECK(bytes[0x08] == (600 & 0xFF));  // stream pointer 0 at +0x08
    const Sequencer::Block back = Sequencer::blockFromDgroup(bytes);
    CHECK(Sequencer::blockToDgroup(back) == bytes);
    CHECK(back.tie == blk.tie);

    for (SoundCard card : {SoundCard::Tandy, SoundCard::Speaker}) {
        CAPTURE(soundCardName(card));
        const SoundBank bank = userBank(*dosExe, card);
        const int voices = card == SoundCard::Tandy ? 4 : 1;
        // A tune for 1000 ticks, then effect 6 (which freezes the tune) at 1020: driver b continues
        // from the blocks of tick 1000 (in the middle of notes), driver c from those of tick 1030
        // (in the middle of the effect). From their first tick on they write what driver a writes.
        RecordingPorts pa, pb, pc;
        Sequencer a(bank, pa), b(bank, pb), c(bank, pc);
        for (Sequencer* s : {&a, &b, &c}) s->setMusicLoop(true);
        a.musicPlay(1, 0);
        run(a, pa, 1000);
        auto copy = [](const Sequencer::Block& blk) { return Sequencer::blockFromDgroup(Sequencer::blockToDgroup(blk)); };
        b.restore(copy(a.sfxBlock()), copy(a.musicBlock()), voices);
        // The device got the sounding notes again: a PSG period (or a speaker divisor).
        CHECK(pb.count(card == SoundCard::Tandy ? 'p' : 'd') >= 1);
        auto same = [](const RecordingPorts& x, size_t nx, const RecordingPorts& y, size_t ny) {
            if (x.ev.size() - nx != y.ev.size() - ny) return false;
            for (size_t i = 0; i < x.ev.size() - nx; ++i)
                if (x.ev[nx + i].kind != y.ev[ny + i].kind || x.ev[nx + i].a != y.ev[ny + i].a) return false;
            return true;
        };
        int differB = 0, differC = 0;
        for (int t = 0; t < 400; ++t) {
            if (t == 20) {
                a.sfxPlay(6);
                b.sfxPlay(6);
            }
            if (t == 30) {
                c.restore(copy(a.sfxBlock()), copy(a.musicBlock()), voices);
                CHECK(c.sfxActive());
            }
            const size_t na = pa.ev.size(), nb = pb.ev.size(), nc = pc.ev.size();
            run(a, pa, 1);
            run(b, pb, 1);
            if (t >= 30) run(c, pc, 1);
            differB += !same(pa, na, pb, nb);
            if (t >= 30) differC += !same(pa, na, pc, nc);
        }
        CHECK(differB == 0);
        CHECK(differC == 0);
        CHECK(Sequencer::blockToDgroup(a.musicBlock()) == Sequencer::blockToDgroup(b.musicBlock()));
        CHECK(Sequencer::blockToDgroup(a.musicBlock()) == Sequencer::blockToDgroup(c.musicBlock()));
    }
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
            cfg.mode = mischief::dos::SynthMode::Raw;
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
        cfg.mode = mischief::dos::SynthMode::Raw;
        cfg.sampleRate = 44100;
        cfg.noise = mischief::dos::NoiseType::Ti15;
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

// ------------------------------------------------------------------------ the AdLib driver (4ac9)

namespace {

// Records the OPL register writes with the PIT tick they belong to, and the register image.
struct OplRecorder final : mischief::dos::OplWriter {
    struct W {
        uint64_t tick;
        uint8_t reg, value;
        bool command = false;  // made by a call between two interrupts (Music_Play, Music_Stop)
        bool operator==(const W&) const = default;
    };
    std::vector<W> writes;
    std::array<uint8_t, 256> regs{};
    uint64_t tick = 0;
    bool command = false;
    void oplWrite(uint8_t reg, uint8_t value) override {
        writes.push_back({tick, reg, value, command});
        regs[reg] = value;
    }
    std::vector<std::pair<int, int>> pairs(size_t from = 0) const {
        std::vector<std::pair<int, int>> p;
        for (size_t i = from; i < writes.size(); ++i) p.emplace_back(writes[i].reg, writes[i].value);
        return p;
    }
};

// The sequencer's AdLib calls on a driver that writes to a recorder (no synthesis), as the renderer
// wires them; `hold` = the ticks AdLib_PlayTune keeps the tune back.
struct DriverPorts final : SoundPorts {
    DriverPorts(const SoundTables& t, bool hold = true) : driver(t, rec), hold(hold) {}
    OplRecorder rec;
    AdLibDriver driver;
    bool hold;  // AdLibOpl's set-up time
    int lastHold = 0;
    void speakerDivisor(uint16_t) override {}
    void speakerEnable(bool) override {}
    void psgWrite(uint8_t) override {}
    void oplInit() override { driver.init(); }
    void oplPlayTune(int t, int v) override {
        const size_t before = rec.writes.size();
        driver.playTune(t, v);
        lastHold = hold ? AdLibOpl::playTuneHold(rec.writes.size() - before) : 0;
    }
    void oplNoteOn(int v, uint8_t s) override { driver.noteOn(v, s); }
    void oplNoteOff(int v) override { driver.noteOff(v); }
    int oplPlayTuneTicks() const override { return lastHold; }
};

void tickOn(Sequencer& s, OplRecorder& r, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) {
        s.timerTick();
        ++r.tick;
    }
}

// The standard OPL2 operator layout of the AdLib driver (DS:8124..816b), two synthetic instruments
// and one orchestration (k = 6: tune 1, variant 2) in a synthetic DGROUP.
std::shared_ptr<const SoundTables> syntheticAdLibTables() {
    std::vector<uint8_t> ds = syntheticTables()->dgroup;
    const auto put16 = [&](uint16_t at, uint16_t v) {
        ds[at] = static_cast<uint8_t>(v);
        ds[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    const uint8_t slotMap[18] = {0, 3, 1, 4, 2, 5, 6, 9, 7, 10, 8, 11, 12, 15, 13, 16, 14, 17};
    const uint8_t slotOffset[18] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21};
    for (int i = 0; i < 18; ++i) {
        ds[AdLibDriver::kDsSlotMap + i] = slotMap[i];
        ds[AdLibDriver::kDsSlotOffset + i] = slotOffset[i];
        ds[AdLibDriver::kDsSlotCarrier + i] = (i % 6) >= 3 ? 1 : 0;
        ds[AdLibDriver::kDsSlotChannel + i] = static_cast<uint8_t>(3 * (i / 6) + i % 3);
    }
    // instrument records: 13 modulator and 13 carrier i16 parameters, two wave selects
    const int16_t inst0[28] = {1, 2, 5, 15, 3, 1, 4, 6, 20, 0, 1, 0, 1, /*carrier*/ 0, 1, 0, 14, 2, 0, 5, 7, 0, 1, 0, 1, 1, 1, 2};
    const int16_t inst1[28] = {0, 1, 3, 12, 4, 0, 6, 2, 30, 1, 0, 1, 0, /*carrier*/ 2, 3, 0, 9, 5, 1, 2, 8, 4, 0, 1, 0, 1, 3, 0};
    for (int i = 0; i < 28; ++i) {
        put16(static_cast<uint16_t>(0x7000 + 2 * i), static_cast<uint16_t>(inst0[i]));
        put16(static_cast<uint16_t>(0x7100 + 2 * i), static_cast<uint16_t>(inst1[i]));
    }
    put16(AdLibDriver::kDsInstrumentPtrs, 0x7000);
    put16(AdLibDriver::kDsInstrumentPtrs + 2, 0x4D94);
    put16(AdLibDriver::kDsInstrumentPtrs + 4, 0x7100);
    put16(AdLibDriver::kDsInstrumentPtrs + 6, 0x4D94);
    for (int k = 0; k < 12; ++k)
        for (int ch = 0; ch < 9; ++ch) ds[AdLibDriver::kDsOrchInstrument + 9 * k + ch] = 0xFF;
    ds[AdLibDriver::kDsOrchInstrument + 9 * 6 + 0] = 0;  // channel 0: instrument 0, +12
    ds[AdLibDriver::kDsOrchTranspose + 9 * 6 + 0] = 12;
    ds[AdLibDriver::kDsOrchInstrument + 9 * 6 + 2] = 1;  // channel 2: instrument 1, -7
    ds[AdLibDriver::kDsOrchTranspose + 9 * 6 + 2] = static_cast<uint8_t>(-7);
    return std::make_shared<SoundTables>(SoundTables::fromDgroup(std::move(ds)));
}

using RegList = std::vector<std::pair<int, int>>;

}  // namespace

TEST_CASE("DOS AdLib: AdLib_Init writes and the F-number table") {
    const auto tables = syntheticAdLibTables();
    OplRecorder rec;
    AdLibDriver d(*tables, rec);
    d.init();
    RegList want = {{0xBD, 0}, {0xBD, 0}, {0x08, 0}};
    for (int ch = 0; ch < 9; ++ch) {
        want.emplace_back(0xB0 + ch, 0);  // AdLib_KeyOff: 0xB0 first, then 0xA0
        want.emplace_back(0xA0 + ch, 0);
    }
    for (int off : {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21}) want.emplace_back(0xE0 + off, 0);
    want.emplace_back(0x01, 0x20);  // wave selects enabled
    CHECK(rec.pairs() == want);
    // Row 0 (C .. B), as the .dro captures show them; row 1 is 4/100 semitone higher, row 24 96/100.
    const std::array<uint16_t, 12> row0 = {343, 364, 385, 408, 433, 459, 486, 515, 546, 579, 614, 650};
    const std::array<uint16_t, 12> row1 = {344, 365, 387, 410, 434, 460, 488, 517, 548, 581, 615, 652};
    const std::array<uint16_t, 12> row24 = {363, 385, 408, 432, 458, 485, 514, 545, 578, 612, 649, 688};
    for (int i = 0; i < 12; ++i) {
        CAPTURE(i);
        CHECK(d.fnumber(0, i) == row0[static_cast<size_t>(i)]);
        CHECK(d.fnumber(1, i) == row1[static_cast<size_t>(i)]);
        CHECK(d.fnumber(24, i) == row24[static_cast<size_t>(i)]);
    }
    CHECK(d.state().slotVolume[17] == 0x7F);
    CHECK(d.state().blockOf[95] == 7);
    CHECK(d.state().noteOf[95] == 11);
    CHECK(d.state().pitchRangeStep == 25);
}

TEST_CASE("DOS AdLib: AdLib_PlayTune, note on and note off write the registers in the original's order") {
    const auto tables = syntheticAdLibTables();
    OplRecorder rec;
    AdLibDriver d(*tables, rec);
    d.init();
    size_t at = rec.writes.size();
    d.playTune(1, 2);  // k = 6: channel 0 instrument 0 (+12), channel 2 instrument 1 (-7)
    // SndSetAllPrm per slot: 0xBD, 0x08, KSL/level, feedback/connection (modulator only), attack/decay,
    // sustain/release, AM/vib/EG/KSR/multiplier, wave select
    const RegList ch0 = {{0xBD, 0}, {0x08, 0}, {0x40, 0x54}, {0xC0, 0x0A}, {0x60, 0xF4}, {0x80, 0x36}, {0x20, 0x62}, {0xE0, 1},
                         {0xBD, 0}, {0x08, 0}, {0x43, 0x00}, {0x63, 0xE5}, {0x83, 0x27}, {0x23, 0x91}, {0xE3, 2}};
    RegList got = rec.pairs(at);
    REQUIRE(got.size() == 30);  // 15 writes per used channel
    CHECK(RegList(got.begin(), got.begin() + 15) == ch0);
    CHECK(got[15] == std::pair{0xBD, 0});
    CHECK(got[17] == std::pair{0x42, 0x1E});  // instrument 1's modulator: KSL 0, level 30
    CHECK(got[18] == std::pair{0xC2, 0x07});  // feedback 3, additive (connection 0)
    CHECK(got[25] == std::pair{0x45, 0x84});  // carrier: KSL 2, level 4
    CHECK(d.state().used == std::array<uint8_t, 9>{1, 0, 1, 0, 0, 0, 0, 0, 0});
    CHECK(d.state().transpose[2] == -7);

    // Note on, voice 0 = channels 0..2: key off (0xB0, 0xA0), then F-number and block with the key bit.
    at = rec.writes.size();
    d.noteOn(0, 36);  // octave nibble 3, C
    CHECK(rec.pairs(at) == RegList{{0xB0, 0}, {0xA0, 0}, {0xA0, 0x57}, {0xB0, 0x31},     // 48: block 4, C (343)
                                   {0xB2, 0}, {0xA2, 0}, {0xA2, 0xCB}, {0xB2, 0x29}});  // 29: block 2, F (459)
    at = rec.writes.size();
    d.noteOn(3, 36);  // the noise voice: no OPL channel
    d.noteOn(1, 36);  // channels 3..5: none used
    CHECK(rec.writes.size() == at);
    // Note off: release rate 10 on both operators, the same pitch without the key.
    d.noteOff(0);
    CHECK(rec.pairs(at) == RegList{{0x80, 0x3A}, {0x83, 0x2A}, {0xA0, 0x57}, {0xB0, 0x11},
                                   {0x82, 0x4A}, {0x85, 0x5A}, {0xA2, 0xCB}, {0xB2, 0x09}});
    CHECK(d.state().slot[0][AdLibDriver::kRelease] == 10);  // kept until the next AdLib_PlayTune

    // Pitches clamp to 0..95; the stored pitch byte is read signed by the note off.
    at = rec.writes.size();
    d.noteOn(0, 200);  // channel 0: 212 -> 95 (block 7, B = 650)
    CHECK(rec.writes[at + 2].value == 0x8A);
    CHECK(rec.writes[at + 3].value == 0x3E);
    at = rec.writes.size();
    d.noteOff(0);  // byte 212 = -44 -> 0: block 0, C
    CHECK(rec.writes[at + 2] == OplRecorder::W{0, 0xA0, 0x57});
    CHECK(rec.writes[at + 3] == OplRecorder::W{0, 0xB0, 0x01});
}

TEST_CASE("DOS AdLib: Music_Play holds the tune for AdLib_PlayTune's set-up time") {
    Synthetic syn;
    syn.tables = syntheticAdLibTables();
    const SoundBank bank = syn.bank(SoundCard::AdLib);
    // The reference machine's set-up time: 76.3 us per write, the call 0.2 tick after its interrupt.
    CHECK(AdLibOpl::playTuneHold(30) == 0);   // 2 channels: 2.3 ms
    CHECK(AdLibOpl::playTuneHold(90) == 1);   // 6 channels: 6.9 ms = 1.62 PIT ticks
    CHECK(AdLibOpl::playTuneHold(105) == 2);  // 7 channels: 1.90 PIT ticks + 0.2
    CHECK(AdLibOpl::playTuneHold(135) == 2);  // 9 channels: 2.44 PIT ticks
    DriverPorts p(*bank.tables);
    Sequencer s(bank, p);
    CHECK(p.rec.writes.size() == 40);  // AdLib_Init
    s.musicPlay(1, 2);
    CHECK(p.rec.writes.size() == 70);  // the set-up of 2 channels, written at once
    CHECK(s.musicBlock().status == 1);  // under one tick: status 1 before the next interrupt
    // A set-up of two ticks (as for 7 to 9 channels).
    struct Fixed final : SoundPorts {
        void speakerDivisor(uint16_t) override {}
        void speakerEnable(bool) override {}
        void psgWrite(uint8_t) override {}
        void oplNoteOn(int, uint8_t) override { ++notes; }
        int oplPlayTuneTicks() const override { return 2; }
        int notes = 0;
    } f;
    Sequencer s2(bank, f);
    s2.musicPlay(1, 2);
    CHECK(s2.musicActive());
    CHECK(s2.musicBlock().status == 0);  // Music_Play sets status 1 only after AdLib_PlayTune
    CHECK(s2.musicHoldTicks() == 2);
    s2.timerTick();
    s2.timerTick();
    CHECK(f.notes == 0);  // the two interrupts during the set-up play nothing
    CHECK(s2.musicBlock().status == 1);
    s2.timerTick();
    CHECK(f.notes == 1);  // the first note on the third
    // A Music_Stop during the set-up: the tune never starts.
    s2.musicPlay(1, 2);
    s2.timerTick();
    s2.musicStop();
    for (int i = 0; i < 10; ++i) s2.timerTick();
    CHECK_FALSE(s2.musicActive());
    CHECK(s2.musicBlock().status == 0);
    CHECK(f.notes == 1);
}

TEST_CASE("DOS AdLib: a driver restored from its DGROUP image holds the same registers and goes on alike") {
    const auto tables = syntheticAdLibTables();
    Synthetic syn;
    syn.tables = tables;
    syn.tunes[1] = cat({tempo(25), note(3, 1, 2), note(4, 5, 3), rest(3), note(2, 9, 1)});
    const SoundBank bank = syn.bank(SoundCard::AdLib);
    DriverPorts pa(*tables), pb(*tables, false);
    Sequencer a(bank, pa), b(bank, pb);
    a.setMusicLoop(true);
    b.setMusicLoop(true);
    a.musicPlay(1, 2);
    tickOn(a, pa.rec, 40);  // inside the second note
    std::vector<uint8_t> dg(0x10000, 0);
    pa.driver.saveTo(dg);
    pb.rec.regs = {};
    pb.driver.restore(dg);  // b's chip: only what restore() writes
    b.restore(a.sfxBlock(), a.musicBlock(), 4, 0, true);
    for (int r = 0x20; r < 0x100; ++r) {
        CAPTURE(r);
        CHECK(pb.rec.regs[static_cast<size_t>(r)] == pa.rec.regs[static_cast<size_t>(r)]);
    }
    std::vector<uint8_t> dg2(0x10000, 0);
    pb.driver.saveTo(dg2);
    CHECK(dg2 == dg);
    const size_t na = pa.rec.writes.size(), nb = pb.rec.writes.size();
    tickOn(a, pa.rec, 300);
    tickOn(b, pb.rec, 300);
    CHECK(pa.rec.pairs(na) == pb.rec.pairs(nb));
    CHECK(pa.rec.pairs(na).size() > 20);
}

namespace {

// A DOSBox raw OPL capture (v2): (ms, register, value); `dump` = the register dump at its start.
struct Dro {
    std::vector<std::array<uint32_t, 3>> writes;
    size_t dump = 0;
};

std::optional<Dro> readDro(const std::filesystem::path& path) {
    const auto file = mischief::readWholeFile(path);
    if (!file) return std::nullopt;
    const std::vector<uint8_t>& d = *file;
    if (d.size() < 26 || std::memcmp(d.data(), "DBRAWOPL", 8) != 0 || d[8] != 2) return std::nullopt;
    const uint32_t pairs = d[12] | (d[13] << 8) | (d[14] << 16) | (static_cast<uint32_t>(d[15]) << 24);
    const uint8_t shortDelay = d[23], longDelay = d[24], mapLen = d[25];
    size_t p = 26u + mapLen;
    uint32_t t = 0;
    Dro out;
    for (uint32_t i = 0; i < pairs && p + 1 < d.size(); ++i, p += 2) {
        const uint8_t c = d[p], v = d[p + 1];
        if (c == shortDelay)
            t += v + 1u;
        else if (c == longDelay)
            t += (v + 1u) << 8;
        else
            out.writes.push_back({t, static_cast<uint32_t>((c & 0x80 ? 0x100 : 0) | d[26 + (c & 0x7F)]), v});
    }
    int prev = -1;
    while (out.dump < out.writes.size() && out.writes[out.dump][0] == 0 && static_cast<int>(out.writes[out.dump][1] & 0xFF) > prev)
        prev = static_cast<int>(out.writes[out.dump++][1] & 0xFF);
    return out;
}

}  // namespace

TEST_CASE("DOS AdLib: the user's tunes: every orchestration's register stream (counts and CRC-32)") {
    TM_REQUIRE_DOS_DATA();
    const SoundBank bank = userBank(*dosExe, SoundCard::AdLib);
    struct Want {
        size_t setup, writes;
        uint32_t crc;
    };
    // tune t, variant v -> AdLib_PlayTune's writes (15 per used channel), the whole tune's writes
    // (from Music_Play to the last note off) and the CRC-32 of its (tick, register, value) bytes.
    // Tune 1's orchestrations 0 and 3 are the same rows in SST.EXE (DS:8940 = DS:895B, DS:89AC = DS:89C7).
    // Every stream here equals the capture's (the test below).
    const Want want[12] = {
        {135, 16239, 0x75841034}, {135, 16239, 0xa54fdf56}, {135, 16239, 0xb27c70e5}, {120, 13864, 0x0607965a},
        {135, 14235, 0xcbeb5cb2}, {105, 9973, 0x6a70e34e},  {135, 14235, 0x9dc8f1f4}, {135, 14235, 0xcbeb5cb2},
        {90, 3666, 0x27b12a5b},   {135, 3723, 0x3fadc789},  {135, 3723, 0x59c84a86},  {135, 3723, 0xa39d6e06},
    };
    for (int t = 0; t < 3; ++t)
        for (int v = 0; v < 4; ++v) {
            CAPTURE(t);
            CAPTURE(v);
            DriverPorts p(*bank.tables);
            Sequencer s(bank, p);
            const size_t init = p.rec.writes.size();
            s.musicPlay(t, v);
            const size_t setup = p.rec.writes.size() - init;
            while (s.musicActive() && p.rec.tick < 30000) tickOn(s, p.rec, 1);
            uint32_t c = 0xFFFFFFFFu;
            for (size_t i = init; i < p.rec.writes.size(); ++i) {
                const auto& w = p.rec.writes[i];
                const uint8_t bytes[4] = {static_cast<uint8_t>(w.tick), static_cast<uint8_t>(w.tick >> 8), w.reg, w.value};
                for (uint8_t b : bytes) {
                    c ^= b;
                    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
                }
            }
            const Want& w = want[4 * t + v];
            CHECK(setup == w.setup);
            CHECK(p.rec.writes.size() - init == w.writes);
            CHECK(~c == w.crc);
        }
}

TEST_CASE("DOS AdLib: the driver against DOSBox-X's OPL capture of all 12 orchestrations (.dro)") {
    TM_REQUIRE_DOS_DATA();
    TM_REQUIRE_DOS_CAPTURES();
    // The capture (fmt_sound.md §11.7): the sound-test harness state w5/snd_adlib_variants plays every
    // tune in every orchestration (rand seed set before each Music_Play), an effect under the first
    // tune, and a tune change without Music_Stop; DOSBox-X's .dro logs the register writes that change
    // a register, in ms.
    const auto state = testdata::dosCapture("savestates/w5/snd_adlib_variants.sav");
    const auto dro = testdata::dosCapture("audio/adlib_w5_variants.dro");
    if (!state || !dro) {
        MESSAGE("the wave-5 AdLib capture is not in TM_DOS_CAPTURES: test skipped");
        return;
    }
    const auto ref = readDro(*dro);
    REQUIRE(ref);
    // The schedule of the harness: delay in PIT ticks after the previous step, kind, tune/effect, variant.
    struct Step {
        uint64_t delay;
        char kind;
        int id, variant;
    };
    std::vector<Step> steps = {{2000, 'x', 0, 0}};
    const uint64_t len[3] = {12993, 17032, 5761};
    for (int v = 0; v < 4; ++v)
        for (int t = 0; t < 3; ++t) {
            steps.push_back({200, 'm', t, v});
            if (v == 0 && t == 0) {
                steps.push_back({3000, 's', 6, 0});
                steps.push_back({len[0] - 3000 + 200, 'x', 0, 0});
            } else {
                steps.push_back({len[t] + 200, 'x', 0, 0});
            }
        }
    steps.push_back({200, 'm', 0, 0});
    steps.push_back({3000, 'm', 2, 1});
    steps.push_back({2000, 'x', 0, 0});

    const SoundBank bank = userBank(*dosExe, SoundCard::AdLib);
    DriverPorts p(*bank.tables);
    Sequencer s(bank, p);
    // the driver as the state left it (what --dos-load-state does)
    const DosboxState st = DosboxState::load(*state);
    const auto dg = st.dgroup();
    s.setSoundOn(st.dsWord(0x63B4) != 0);
    s.setMusicOn(st.dsWord(0x63B6) != 0);
    s.setMusicLoop(st.dsWord(0x63BA) != 0);
    p.rec.writes.clear();
    p.driver.restore(dg);
    s.restore(Sequencer::blockFromDgroup(dg.subspan(0x6448, 0x78)), Sequencer::blockFromDgroup(dg.subspan(0x64C0, 0x78)),
              st.dsWord(0x63BE), 0, true);
    for (const Step& stp : steps) {
        tickOn(s, p.rec, stp.delay);
        p.rec.command = true;
        if (stp.kind == 's') s.sfxPlay(stp.id);
        else if (stp.kind == 'm') s.musicPlay(stp.id, stp.variant);
        else s.musicStop();
        p.rec.command = false;
    }
    tickOn(s, p.rec, 500);

    // The capture started inside the group of writes of a tick: its registers are the dump (key bits
    // of 0xB0-0xB8 masked) and the writes of its first millisecond; it continues with engine tick 180
    // (tools/dos/oplcmp.py finds the tick). There the engine's registers equal the capture's.
    constexpr uint64_t kFirstTick = 180;
    size_t d0 = ref->dump;
    while (d0 < ref->writes.size() && ref->writes[d0][0] <= 1) ++d0;
    std::array<int, 512> cache;
    cache.fill(0);
    for (size_t i = 0; i < d0; ++i) cache[ref->writes[i][1]] = static_cast<int>(ref->writes[i][2]);
    std::array<int, 256> image{};
    size_t e0 = 0;
    for (; e0 < p.rec.writes.size() && p.rec.writes[e0].tick < kFirstTick; ++e0) image[p.rec.writes[e0].reg] = p.rec.writes[e0].value;
    int differ = 0;
    for (int r = 0x20; r < 0xF6; ++r) {
        const bool key = r >= 0xB0 && r <= 0xB8;
        differ += key ? (image[static_cast<size_t>(r)] & ~0x20) != (cache[static_cast<size_t>(r)] & ~0x20)
                      : image[static_cast<size_t>(r)] != cache[static_cast<size_t>(r)];
        if (key) cache[static_cast<size_t>(r)] = image[static_cast<size_t>(r)];
    }
    CHECK(differ == 0);

    // DOSBox-X's filter: a write of the value the register holds is not logged. k = the writes before
    // it in its tick (of its kind), for the timing.
    std::vector<OplRecorder::W> logged;
    std::vector<int> pos;
    int k = 0;
    for (size_t i = e0; i < p.rec.writes.size(); ++i) {
        const auto& w = p.rec.writes[i];
        k = i > e0 && p.rec.writes[i - 1].tick == w.tick && p.rec.writes[i - 1].command == w.command ? k + 1 : 0;
        if (cache[w.reg] == w.value) continue;
        cache[w.reg] = w.value;
        logged.push_back(w);
        pos.push_back(k);
    }
    const size_t n = ref->writes.size() - d0;
    CHECK(logged.size() == n);
    size_t same = 0;
    while (same < std::min(n, logged.size()) && logged[same].reg == ref->writes[d0 + same][1] &&
           logged[same].value == ref->writes[d0 + same][2])
        ++same;
    CHECK(same == n);
    CHECK(n == 85767);
    MESSAGE(std::format("{} writes identical of {} logged by DOSBox-X", same, n));

    // Timing (capture: whole ms, 4.2248 ms per PIT tick): every write of a timer interrupt at its tick
    // plus 70 us per write before it in the tick, the time base fitted per segment between two tune
    // starts (the capture's ms clock drifted 2.2 ms against the PIT once, in turbo mode).
    const double tickMs = 1000.0 * mischief::dos::kTimerDivisor / mischief::dos::kPitHz;
    std::vector<std::vector<double>> segments(1);
    for (size_t i = 0; i < same; ++i) {
        if (logged[i].command) {
            if ((logged[i].reg >= 0x40 && logged[i].reg <= 0x55) && !segments.back().empty()) segments.emplace_back();
            continue;
        }
        segments.back().push_back(ref->writes[d0 + i][0] - logged[i].tick * tickMs - pos[i] * mischief::dos::OplChip::kWriteSeconds * 1000.0);
    }
    size_t total = 0, within = 0, within60 = 0;
    for (auto& seg : segments) {
        if (seg.empty()) continue;
        std::vector<double> sorted = seg;
        std::sort(sorted.begin(), sorted.end());
        const double base = sorted[sorted.size() / 2];
        for (double x : seg) {
            ++total;
            within += std::abs(x - base) <= tickMs / 4;
            within60 += std::abs(x - base) <= tickMs * 0.6;
        }
    }
    MESSAGE(std::format("{} of {} timer-interrupt writes within a quarter tick of their segment's time base", within, total));
    CHECK(segments.size() == 15);  // the restored tune and the 14 tune starts
    CHECK(within60 == total);
    CHECK(within >= 81700);
}
