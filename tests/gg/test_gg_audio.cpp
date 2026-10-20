// Tests of the G&G sound area (src/gg/audio): BMF songs, the driver (ssg.exe 1bb4), the bank layer
// (1cdd), the music cues (12e0), the puzzles' helpers (3924), the device glue, the soundtest
// schedules and the DOSBox-X capture comparison. Expected numbers come from re/gg/docs/fmt_sound.md,
// code_sound.md, the oracle tools/gg/bmf.py and the round 1/2 captures (REFERENCE_CAPTURES.md
// §3.3, §9.7, §10.6); only counts, register numbers and CRC-32s are stored, never game data.

#include <doctest/doctest.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <string>
#include <vector>

#include "gg/audio/audio_cli.h"
#include "gg/audio/bmf.h"
#include "gg/audio/dro_compare.h"
#include "gg/audio/gg_audio.h"
#include "gg/audio/soundtest.h"
#include "gg/data/resources.h"
#include "gg_test_data.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/png_writer.h"

using namespace tmport;
using namespace tmport::gg;

namespace {

GgAudioConfig offline(SoundMode mode) {
    GgAudioConfig cfg;
    cfg.mode = mode;
    cfg.logOpl = true;
    cfg.logSpeaker = true;
    cfg.sdlDriver = "dummy";  // never a real device
    return cfg;
}

uint32_t crcText(const std::string& t) { return png::crc32(std::span(reinterpret_cast<const uint8_t*>(t.data()), t.size())); }

std::string speakerText(const std::vector<SpeakerWrite>& log) {
    std::string t;
    for (const SpeakerWrite& w : log) t += std::format("{} {} {:x}{}\n", w.tick, w.port61 ? "p61" : "div", w.value, w.command ? " c" : "");
    return t;
}

uint32_t crcPcm16(const std::vector<float>& pcm) {
    std::vector<uint8_t> b;
    for (const float f : pcm) {
        const auto v = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(f, -1.0f, 1.0f) * 32767.0f));
        b.push_back(static_cast<uint8_t>(v));
        b.push_back(static_cast<uint8_t>(v >> 8));
    }
    return png::crc32(b);
}

// The writes of a log in [from, to) ticks, made inside ticks (not by calls).
std::vector<dos::OplWriteLog::Entry> tickWrites(const dos::OplWriteLog& log, uint64_t from, uint64_t to) {
    std::vector<dos::OplWriteLog::Entry> v;
    for (const auto& e : log.entries())
        if (!e.command && e.tick >= from && e.tick < to) v.push_back(e);
    return v;
}

bool isKeyWrite(const dos::OplWriteLog::Entry& e) { return (e.reg >= 0xA0 && e.reg <= 0xB8) || e.reg == 0xBD; }

}  // namespace

// --------------------------------------------------------------------------------- data-free

TEST_CASE("gg audio: sound modes and the -S switches (Init_CommandLine 303a:005c)") {
    CHECK(parseSoundMode("fm") == SoundMode::Fm);
    CHECK(parseSoundMode("speaker") == SoundMode::Speaker);
    CHECK(parseSoundMode("off") == SoundMode::Off);
    CHECK(parseSoundMode("-SN") == SoundMode::Off);
    CHECK(parseSoundMode("-s") == SoundMode::Off);
    CHECK(parseSoundMode("/S-") == SoundMode::Off);
    CHECK(parseSoundMode("-si") == SoundMode::Speaker);
    CHECK(parseSoundMode("-SA") == SoundMode::Fm);
    CHECK(parseSoundMode("-sBLASTER") == SoundMode::Fm);  // only three characters count
    CHECK(parseSoundMode("-sm") == SoundMode::Fm);
    CHECK_FALSE(parseSoundMode("-SX").has_value());
    CHECK_FALSE(parseSoundMode("-GV").has_value());
    CHECK(soundDevice(SoundMode::Off) == 0);
    CHECK(soundDevice(SoundMode::Speaker) == 1);
    CHECK(soundDevice(SoundMode::Fm) == -1);
    CHECK(ggSoundTickHz() == doctest::Approx(72.8260).epsilon(1e-5));
}

TEST_CASE("gg audio: soundtest scripts") {
    const auto s = SoundTestSchedule::parse("10:f:3:1:5,0:m:1020,4/2x3:g:1,20:end");
    REQUIRE(s.steps.size() == 6);
    CHECK(s.steps[0].tick == 0);
    CHECK(s.steps[0].cmd == "m");
    CHECK(s.steps[0].args == std::vector<int>{0x1020});  // ids are hex
    CHECK(s.steps[1].tick == 4);
    CHECK(s.steps[2].tick == 6);
    CHECK(s.steps[3].tick == 8);
    CHECK(s.steps[3].cmd == "g");
    CHECK(s.steps[4].args == std::vector<int>{3, 1, 5});
    CHECK(s.steps[5].cmd == "end");
    CHECK_THROWS_AS(SoundTestSchedule::parse("5:zz"), std::invalid_argument);
    CHECK_THROWS_AS(SoundTestSchedule::parse("x:m:1"), std::invalid_argument);
    CHECK_THROWS_AS(SoundTestSchedule::parse("5"), std::invalid_argument);
    const auto names = SoundTestSchedule::names();
    CHECK(names.size() >= 12);
    for (const auto& n : names) {
        const auto sch = SoundTestSchedule::named(n);
        REQUIRE(sch.has_value());
        CHECK(sch->capture.ends_with(".dro"));
        CHECK(sch->steps.back().cmd == "end");
    }
    CHECK_FALSE(SoundTestSchedule::named("nope").has_value());
}

TEST_CASE("gg audio: DOSBox-X capture rules on a synthetic log") {
    using E = dos::OplWriteLog::Entry;
    const std::vector<E> log = {
        {0, 0x01, 0x20, true}, {0, 0x04, 0x60, true},  // the timer register is never cached
        {1, 0x20, 0x01, false}, {1, 0xB0, 0x31, false}, {1, 0xBD, 0x30, false},  // before the request
        {2, 0xB1, 0x25, false},                        // key on: the capture opens
        {2, 0xB1, 0x25, false},                        // no change: not logged
        {3, 0x40, 0x3F, false}, {3, 0x02, 0xFF, false}, {3, 0xBD, 0x30, false}, {4, 0xBD, 0x20, false},
    };
    const SimulatedCapture cap = simulateCapture(log, 5);
    // the dump: non-zero registers ascending, the key bit of B0h..B8h and the drum bits of BDh masked
    REQUIRE(cap.dump.size() == 4);
    CHECK((cap.dump[0].reg == 0x01 && cap.dump[0].value == 0x20));
    CHECK(cap.dump[1].reg == 0x20);
    CHECK((cap.dump[2].reg == 0xB0 && cap.dump[2].value == 0x11));
    CHECK((cap.dump[3].reg == 0xBD && cap.dump[3].value == 0x20));
    REQUIRE(cap.writes.size() == 3);
    CHECK((cap.writes[0].reg == 0xB1 && cap.writes[0].value == 0x25 && cap.writes[0].tick == 2));
    CHECK((cap.writes[1].reg == 0x40 && cap.writes[1].inTick == 0));
    CHECK((cap.writes[2].reg == 0xBD && cap.writes[2].value == 0x20));
    CHECK(simulateCapture(log).writes.front().reg == 0xB0);  // requested earlier: the first key-on
    CHECK(isCaptureTrigger(0xBD, 0x21));
    CHECK_FALSE(isCaptureTrigger(0xBD, 0x20));
    CHECK_FALSE(isCaptureTrigger(0xB9, 0x20));

    // the same as a .dro v2.0 file: codemap {01, 20, 40, B0, BD, B1}, short delay code 0x7E, long 0x7F
    std::vector<uint8_t> f = {'D', 'B', 'R', 'A', 'W', 'O', 'P', 'L', 2, 0, 0, 0};
    const std::vector<uint8_t> pairs = {0, 0x20, 1, 0x01, 3, 0x11, 4, 0x20, 5, 0x25, 0x7E, 13, 2, 0x3F, 0x7F, 0, 4, 0x20};
    for (int k = 0; k < 4; ++k) f.push_back(static_cast<uint8_t>((pairs.size() / 2) >> (8 * k)));
    for (int k = 0; k < 4; ++k) f.push_back(static_cast<uint8_t>(270 >> (8 * k)));
    f.insert(f.end(), {0, 0, 0, 0x7E, 0x7F, 6, 0x01, 0x20, 0x40, 0xB0, 0xBD, 0xB1});
    f.insert(f.end(), pairs.begin(), pairs.end());
    const DroFile dro = parseDro(f);
    REQUIRE(dro.writes.size() == 7);
    CHECK(dro.dumpSize == 4);
    CHECK(dro.lengthMs == 270);
    CHECK((dro.writes[5].ms == 14 && dro.writes[5].reg == 0x40));
    CHECK(dro.writes[6].ms == 270);
    // the engine log continues it: trigger, 40h, BDh
    const DroComparison c = compareWithDro(dro, log, 5);
    CHECK(c.found);
    CHECK(c.triggerIndex == 5);
    CHECK(c.matched == 3);
    CHECK(c.refWrites == 3);
    CHECK(c.dumpDiffer == 0);
    CHECK_THROWS_AS(parseDro(std::vector<uint8_t>(10, 0)), FormatError);

    // the log's text form reads back
    dos::OplWriteLog l;
    for (const E& e : log) {
        l.setCommand(e.command);
        l.write(e.tick, e.reg, e.value);
    }
    CHECK(parseOplLogText(l.text()) == log);
    CHECK_THROWS_AS(parseOplLogText("1 xx 2\n"), FormatError);
}

// ------------------------------------------------------------------------------ with the game

TEST_CASE("gg audio: every BMF song parses like tools/gg/bmf.py") {
    GG_REQUIRE_DATA();
    const Archive& arc = ggFiles->archive();
    std::vector<uint16_t> ids = arc.ids();
    std::sort(ids.begin(), ids.end());
    std::string digest;
    std::map<std::string, size_t> n;
    size_t songs = 0, events = 0, afterEnd = 0;
    const auto add = [&](const std::string& path, std::span<const uint8_t> p) {
        const BmfSongInfo g = parseBmfSong(p);
        ++songs;
        ++n[std::format("class{}", g.songClass)];
        afterEnd += g.end != g.blockSize;
        for (const BmfEvent& e : g.events) {
            ++events;
            static constexpr const char* kKind[] = {"off", "on", "prog", "bend", "meta"};
            ++n[std::format("{}{}", kKind[static_cast<int>(e.kind)], g.songClass)];
            int a = 0, b = 0;
            switch (e.kind) {
                case BmfEventKind::NoteOff: break;
                case BmfEventKind::NoteOn: a = e.key, b = g.songClass == bmf::kClassOpl ? e.velocity : 0; break;
                case BmfEventKind::Program: a = e.program; break;
                case BmfEventKind::Bend: a = e.bend; break;
                case BmfEventKind::Meta: a = e.metaType, b = e.metaValue; break;
            }
            digest += std::format("{} {} {} {} {} {} {}\n", path, e.tick, e.offset, static_cast<int>(e.kind), e.channel, a, b);
        }
    };
    for (const uint16_t id : ids) {
        const Resource r = arc.load(id);
        if (isBmfSong(r.bytes)) add(std::format("{:04x}", id), r.bytes);
        else if (r.resType() == ResType::Bundle && looksLikeBundle(r.bytes))
            for (const BundleMember& m : parseBundle(r.bytes))
                if (isBmfSong(m.payload)) add(std::format("{:04x}/{}", id, m.index), m.payload);
    }
    // fmt_sound.md §3.3 and `bmf.py --check`
    CHECK(songs == 495);
    CHECK(n["class1"] == 237);
    CHECK(n["class3"] == 258);
    CHECK(events == 28847);
    CHECK(n["on1"] == 5711);
    CHECK(n["off1"] == 3438);
    CHECK(n["bend1"] == 118);
    CHECK(n["meta1"] == 237);
    CHECK(n["on3"] == 9041);
    CHECK(n["off3"] == 8774);
    CHECK(n["prog3"] == 642);
    CHECK(n["bend3"] == 628);
    CHECK(n["meta3"] == 258);
    CHECK(afterEnd == 1);  // 0x105b/0: 12 bytes after its end event
    // zlib.crc32 of the same lines written from bmf.parse (extracted/scratch/gg/wave2/audio/bmfdigest.py)
    CHECK(crcText(digest) == 0xf29c7f5bu);
}

TEST_CASE("gg audio: start-up per mode (Snd_Init 1bb4:1234, SndRes_Init 1cdd:0004)") {
    GG_REQUIRE_DATA();
    {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Fm));
        CHECK(a.caps() == 5);  // speaker + OPL
        CHECK(a.fmPresent() == 1);
        const auto& w = a.oplLog().entries();
        // AdLib_Detect (6 writes) and AdLib_InitChip (111): code_sound.md §3.3, §3.4
        REQUIRE(w.size() == 117);
        const std::array<uint8_t, 16> head = {4, 0x60, 4, 0x80, 2, 0xFF, 4, 0x21, 4, 0x60, 4, 0x80, 0xBD, 0x20, 8, 0};
        for (size_t i = 0; i < head.size() / 2; ++i) CHECK((w[i].reg == head[2 * i] && w[i].value == head[2 * i + 1] && w[i].command));
        CHECK((w.back().reg == 0x01 && w.back().value == 0x20));
        CHECK(a.driver().oplBdShadow() == 0x20);
        // the global effect bank 0x105C: entry 5 holds the count, effects in 6..12, handle 6
        CHECK(a.res().globalEffects() == 6);
        CHECK(a.res().entry(5) == 7);
        for (int k = 0; k < 7; ++k) CHECK(a.res().entry(6 + k) == k);
    }
    {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Speaker));
        CHECK(a.caps() == 1);
        CHECK(a.fmPresent() == 0);
        CHECK(a.oplLog().entries().empty());
        CHECK(a.res().entry(5) == 7);  // 0x105B: 7 speaker effects, all registered
        CHECK(a.res().entry(6) == 0);
        CHECK_FALSE(a.driver().hasOpl());
    }
    {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Off));
        CHECK(a.caps() == 5);  // -SN: the OPL forced on, the FM banks
        CHECK(a.fmPresent() == 1);
        CHECK_FALSE(a.driver().enabled());
        // Snd_Silence: Snd_HwSilenceAll's note-offs (voices 10..6 the BD register, 5..0 B5h..B0h)
        // are the last writes before g_sndEnabled = 0
        CHECK(a.oplLog().entries().size() == 117 + 11);
    }
}

TEST_CASE("gg audio: handles and slots as the bank layer numbers them (1cdd)") {
    GG_REQUIRE_DATA();
    GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Fm));
    const int fx = a.loadEffectBank(0x1022);  // 0x1023: 14 effects
    CHECK(fx == 14);
    CHECK(a.res().entry(13) == 14);
    CHECK(a.res().slot(fx) == 7);
    const int music = a.loadMusicBank(0x1020);
    CHECK(music == 0);
    CHECK(a.res().slot(music) == 21);
    const int song = a.loadSong(0x1056);  // the hint chime 0x1057
    CHECK(song == 28);
    CHECK(a.res().slot(song) == 22);
    CHECK(a.isPlaying(music) == 0);
    a.playMusic(music);
    CHECK(a.isPlaying(music) == 1);
    a.freeSong(song);
    a.freeMusicBank(music);
    a.freeEffectBank(fx);
    CHECK(a.res().entry(0) == -1);
    CHECK(a.res().entry(13) == -1);
    CHECK(a.res().entry(28) == -1);
    // the driver's results: bad slot, bad section, not BMF, class unavailable
    a.call([](SndRes& r) {
        SndDriver& d = r.driver();
        CHECK(d.play(-1, 0, 0, 0) == SndDriver::kBadSlot);
        CHECK(d.play(64, 0, 0, 0) == SndDriver::kBadSlot);
        CHECK(d.play(0, 1, 0, 0) == SndDriver::kBadSection);
        CHECK(d.isActive(63) == SndDriver::kBadSlot);
        std::vector<uint8_t> junk(64, 0);
        CHECK(d.registerSong(junk, 0) == SndDriver::kNotBmf);
    });
    GgAudio spk(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Speaker));
    std::vector<uint8_t> fmSong = ggFiles->archive().load(0x1057).bytes;
    CHECK(spk.call([&](SndRes& r) { return r.driver().registerSong(fmSong, 0); }) == SndDriver::kClassUnavailable);
}

TEST_CASE("gg audio: the title music's register and speaker streams") {
    GG_REQUIRE_DATA();
    const auto title = SoundTestSchedule::named("title");
    REQUIRE(title.has_value());
    {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Fm));
        SoundTest(a).run(*title, nullptr, 3000);
        CHECK(a.tickIndex() == 3000);
        CHECK(a.oplLog().entries().size() == 4525);
        CHECK(crcText(a.oplLog().text()) == 0xfe6b9305u);
    }
    {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Speaker));
        SoundTest(a).run(*title, nullptr, 3000);
        CHECK(a.speakerLog().size() == 751u);
        CHECK(crcText(speakerText(a.speakerLog())) == 0xc0921bd7u);
    }
}

TEST_CASE("gg audio: rendered samples (OPL2 and speaker)") {
    GG_REQUIRE_DATA();
    for (const SoundMode m : {SoundMode::Fm, SoundMode::Speaker}) {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(m));
        const int h = a.loadMusicBank(0x1020);
        a.playMusic(h);
        std::vector<float> pcm;
        a.renderTicks(300, pcm);
        // 300 ticks at 48000 / 72.826 samples each (the fractional accumulator)
        CHECK(pcm.size() == 197731u);
        float peak = 0;
        for (const float f : pcm) peak = std::max(peak, std::abs(f));
        CHECK(peak > 0.05f);
        CHECK(peak <= 1.0f);
        CHECK(crcPcm16(pcm) == (m == SoundMode::Fm ? 0xd744dd9du : 0xb4b6a8a6u));
    }
}

TEST_CASE("gg audio: channel ownership by priority (Snd_Play 1bb4:10c1, Snd_ReleaseChannels 1bb4:077c)") {
    GG_REQUIRE_DATA();
    GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Fm));
    const int music = a.loadMusicBank(0x1020);  // channels 1..8, 10
    const int fx = a.loadEffectBank(0x1039);    // 0x103A
    a.playMusic(music);
    a.advanceTo(10);
    const auto flags = [&](int slot, int ch) { return a.call([&](SndRes& r) { return r.driver().songBlock(slot)[bmf::kChannels + 4 * ch]; }); };
    const auto owner = [&](int ch) { return a.call([&](SndRes& r) { return r.driver().channelOwner(3, ch); }); };
    const int musicSlot = a.res().slot(music), s17 = a.res().slot(fx + 17), s15 = a.res().slot(fx + 15), s10 = a.res().slot(fx + 10);
    // an effect at 43h takes channel 0 (free); effect 15 at the same priority does not steal it
    a.playEffect(fx + 17, 0, 4);
    CHECK(owner(0) == (s17 | 0x4300));
    a.advanceTo(12);
    a.playEffect(fx + 15, 0, 4);
    CHECK((flags(s15, 0) & bmf::kChannelStopped) != 0);
    CHECK(owner(0) == (s17 | 0x4300));
    // effect 17 ends (7 ticks): the channel goes to effect 15, whose patch is loaded then
    a.clearLogs();
    a.advanceTo(30);
    CHECK(owner(0) == (s15 | 0x4300));
    CHECK((flags(s15, 0) & bmf::kChannelStopped) == 0);
    const auto w = a.oplLog().entries();
    CHECK(std::count_if(w.begin(), w.end(), [](const auto& e) { return e.reg == 0x20; }) >= 1);
    // effect 10 (channels 0, 1, 2) at 49h steals channels 1 and 2 from the music (and 0 from 15)
    a.playEffect(fx + 10, 0, 10);
    CHECK(owner(1) == (s10 | 0x4900));
    CHECK((flags(musicSlot, 1) & bmf::kChannelStopped) != 0);
    CHECK((flags(s15, 0) & bmf::kChannelStopped) != 0);
    a.stopEffect(fx + 10);
    // released: channel 0 back to effect 15 (if it still plays), 1 and 2 to the music (priority 0)
    CHECK((owner(1) & 0xFF) == musicSlot);
    CHECK((flags(musicSlot, 1) & bmf::kChannelStopped) == 0);
}

TEST_CASE("gg audio: Ctrl+S, the silence song (SndRes_ApplySoundOption 1cdd:0592)") {
    GG_REQUIRE_DATA();
    GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Fm));
    const int music = a.loadMusicBank(0x1020);
    a.playMusic(music);
    a.advanceTo(100);
    a.toggleSound();
    CHECK(a.soundMuted() == 1);
    const int silence = a.res().slot(a.res().globalEffects());
    for (int c = 0; c < 11; ++c) CHECK((a.call([&](SndRes& r) { return r.driver().channelOwner(3, c); }) & 0xFF) == silence);
    a.clearLogs();
    a.advanceTo(1100);
    // the music runs on silently: no frequency, key or drum write in 1000 ticks
    for (const auto& e : tickWrites(a.oplLog(), 101, 1100)) CHECK_FALSE(isKeyWrite(e));
    CHECK(a.call([&](SndRes& r) { return r.driver().getTick(r.slot(music)); }) > 1000);
    a.toggleSound();
    CHECK(a.soundMuted() == 0);
    CHECK((a.call([&](SndRes& r) { return r.driver().channelOwner(3, 1); }) & 0xFF) == a.res().slot(music));
    a.clearLogs();
    a.advanceTo(1400);
    const auto w = tickWrites(a.oplLog(), 1100, 1400);
    CHECK(std::count_if(w.begin(), w.end(), [](const auto& e) { return e.reg >= 0xB0 && e.reg <= 0xB5 && (e.value & 0x20); }) > 0);
}

TEST_CASE("gg audio: -SN keeps only the patch and volume writes (Snd_Silence 1bb4:0b97)") {
    GG_REQUIRE_DATA();
    GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Off));
    const size_t init = a.oplLog().entries().size();
    const int music = a.loadMusicBank(0x1020);
    a.playMusic(music);
    a.advanceTo(1000);
    const auto& w = a.oplLog().entries();
    size_t patch = 0, keys = 0;
    for (size_t i = init; i < w.size(); ++i) {
        keys += isKeyWrite(w[i]);
        patch += !isKeyWrite(w[i]);
    }
    CHECK(keys == 0);
    CHECK(patch > 100);  // Opl_SetPatch and Opl_SetVolume still write
    // the speaker: only Snd_HwSilenceAll's gate off (port 61h is not gated by g_sndEnabled)
    for (const SpeakerWrite& s : a.speakerLog()) CHECK((s.port61 && (s.value & 1) == 0));
}

TEST_CASE("gg audio: pause stands the sound time still (Snd_PauseLock 1bb4:0b7f)") {
    GG_REQUIRE_DATA();
    GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Fm));
    const int music = a.loadMusicBank(0x1020);
    a.playMusic(music);
    a.advanceTo(200);
    const int t0 = a.musicTick(music);
    a.clearLogs();
    a.pause();
    // Snd_HwSilenceAll: the drums' BD writes and the melodic voices' B0h without the key bit
    const auto& calls = a.oplLog().entries();
    CHECK(calls.size() == 11);
    for (const auto& e : calls) CHECK((e.reg == 0xBD || (e.reg >= 0xB0 && e.reg <= 0xB5 && !(e.value & 0x20))));
    a.pause();  // not nestable: nothing more
    CHECK(a.oplLog().entries().size() == 11);
    a.advanceTo(300);
    CHECK(a.musicTick(music) == t0);
    CHECK(a.oplLog().entries().size() == 11);
    a.resume();
    a.advanceTo(310);
    CHECK(a.musicTick(music) == t0 + 10);
}

TEST_CASE("gg audio: the workshop cue never fires (fmt_sound.md §2.1)") {
    GG_REQUIRE_DATA();
    for (const SoundMode m : {SoundMode::Fm, SoundMode::Speaker}) {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(m));
        SoundTest(a).run(SoundTestSchedule::parse("0:W"), nullptr, 4000);
        CHECK(a.res().bankCueTick() == (m == SoundMode::Fm ? 0x4905 : 0x4902));
        CHECK(a.cues().workshopCueCount() == 0);
        CHECK(a.cues().workshopArrangement() == 1);
    }
}

TEST_CASE("gg audio: the final movie's speaker list 4 (code_sound.md §13.4) and its cue (§14.3)") {
    GG_REQUIRE_DATA();
    {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Speaker));
        const int h = a.loadMusicBank(MusicCues::kMovieMusicBank);
        const int slot = a.res().slot(h);
        const auto block = [&](int off) { return a.call([&](SndRes& r) { return r.driver().songBlock(slot)[off]; }); };
        const uint8_t delayHi = block(0x02), rec77 = block(0x0E + 4 * 77);
        a.selectArrangement(h, 4);
        CHECK(block(0x02) == (delayHi | 1));  // "channel -3": +256 ticks of delay
        CHECK(block(0x0E + 4 * 77) == (rec77 | 1));
        for (int c = 0; c <= 4; ++c) CHECK((block(0x0E + 4 * c) & bmf::kChannelMuted) != 0);
        a.selectArrangement(h, 0);
        for (int c = 0; c <= 4; ++c) CHECK((block(0x0E + 4 * c) & bmf::kChannelMuted) == 0);
        CHECK(block(0x02) == (delayHi | 1));  // the unmute loop covers channels 0..15 only
    }
    {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Fm));
        SoundTest(a).run(SoundTestSchedule::parse("0:V"), nullptr, 1);
        const int h = a.cues().movieMusicStep() == 1 ? a.res().slot(0) : -1;
        REQUIRE(h >= 0);
        const auto block = [&](int off) { return a.call([&](SndRes& r) { return r.driver().songBlock(r.slot(0))[off]; }); };
        CHECK((block(0x0E + 4 * 8) & bmf::kChannelMuted) != 0);  // FM list 1 = 01 08 FF
        // Music_MovieCue at the end of each 592-tick pass: arrangements 2, 3, 4, all, all, all, 1
        const int expect[] = {2, 3, 4, 5, 6, 0, 1, 2};
        for (int pass = 0; pass < 8; ++pass) {
            a.advanceTo(static_cast<uint64_t>(592 * (pass + 1) + 1));
            CHECK(a.cues().movieMusicStep() == expect[pass]);
        }
    }
}

TEST_CASE("gg audio: room music transposes (Music_RoomCue 12e0:007f)") {
    GG_REQUIRE_DATA();
    const auto transpose = [](GgAudio& a) {
        return a.call([&](SndRes& r) { return static_cast<int8_t>(r.driver().songBlock(r.slot(a.cues().roomMusicBank()))[bmf::kTranspose]); });
    };
    {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Fm));
        SoundTest(a).run(SoundTestSchedule::parse("0:R:1:1"), nullptr, 10);
        CHECK(a.cues().roomCueTick(1) == 3555);
        CHECK(transpose(a) == 2);  // building 0's step 3: the stride-6 bug
        a.advanceTo(3560);         // the FM Aircraft music is 3555 ticks long: the cue fires
        CHECK(a.cues().roomMusicStep() == 1);
        CHECK(transpose(a) == static_cast<int8_t>(a.cues().transposeByte(12 + 2)));
    }
    {
        GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Speaker));
        SoundTest(a).run(SoundTestSchedule::parse("0:R:1:1"), nullptr, 10000);
        CHECK(a.cues().roomMusicStep() == 0);  // 2370 ticks on the speaker: never reaches 3555
        CHECK(transpose(a) == 2);
    }
}

TEST_CASE("gg audio: the DOSBox-X captures, write for write (REFERENCE_CAPTURES.md §9.7, §10.6)") {
    GG_REQUIRE_DATA();
    GG_REQUIRE_CAPTURES();
    const std::map<std::string, size_t> writes = {
        {"title", 4225},           {"title_replay", 3586},    {"hub", 1521},
        {"postrace", 549},         {"warehouse", 1186},       {"workshop", 669},
        {"puzzle_k4", 356},        {"puzzle_k5", 162},        {"race_automotive", 1421},
        {"race_altenergy", 2899},  {"race_aircraft_won", 2685}, {"race_aircraft_lost", 2598},
        {"movie", 8320},
    };
    // reconstructed up to the movie's end only (fmt_sound.md §5.4)
    const std::map<std::string, size_t> partial = {{"movie", 7050}};
    for (const auto& [name, n] : writes) {
        const auto sch = SoundTestSchedule::named(name);
        REQUIRE(sch.has_value());
        const auto file = testdata::ggCapture(std::filesystem::path("audio") / sch->capture);
        if (!file) {
            MESSAGE(sch->capture << " is not in GG_CAPTURES: skipped");
            continue;
        }
        const DroComparison c = checkSchedule(ggFiles->exe(), ggFiles->archive(), *sch, *file);
        INFO(name << ": " << c.summary());
        CHECK(c.refWrites == n);
        CHECK(c.matched == (partial.contains(name) ? partial.at(name) : n));
        // the writes of the ticks lie on the 13.731 ms grid (65 us per AdLib_Write)
        CHECK(c.within05 * 100 >= c.timed * 97);
        if (!sch->compareWithDump) CHECK(c.dumpDiffer == 0);
    }
}

TEST_CASE("gg audio: the device on the silent SDL dummy driver") {
    GG_REQUIRE_DATA();
    GgAudio a(ggFiles->exe(), ggFiles->archive(), offline(SoundMode::Fm));
    if (!a.open()) {
        MESSAGE("SDL dummy audio driver unavailable: test skipped");
        return;
    }
    CHECK(a.device().driverName() == "dummy");
    const int h = a.loadMusicBank(0x1020);
    a.playMusic(h);
    const uint64_t t0 = SDL_GetTicks();
    while (a.musicTick(h) < 20 && SDL_GetTicks() - t0 < 3000) SDL_Delay(5);
    CHECK(a.musicTick(h) >= 20);  // the callback pulls samples and the driver advances
    std::vector<float> out;
    CHECK(a.renderTicks(1, out) == 0);  // not while the stream is open
    a.close();
    CHECK_FALSE(a.isOpen());
}

TEST_CASE("gg audio: the sound tools (audioCli)") {
    GG_REQUIRE_DATA();
    const auto dir = testdata::scratchDir("gg_audio_cli");
    const std::string data = pathToUtf8(ggFiles->folder());
    int code = -1;
    CHECK_FALSE(audioCli({"--gg-info"}, code));
    const auto log = pathToUtf8(dir / "title.txt");
    REQUIRE(audioCli({"--gg-oplog", "soundtest", "title", log, "--data", data}, code));
    CHECK(code == 0);
    const auto bytes = readWholeFile(pathFromUtf8(log));
    REQUIRE(bytes.has_value());
    const auto entries = parseOplLogText(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    CHECK(entries.size() > 7000);
    const auto wav = pathToUtf8(dir / "chime.wav");
    REQUIRE(audioCli({"--gg-sound", "fm", "song", "1057", wav, "--data", data}, code));
    CHECK(code == 0);
    const auto w = readWholeFile(pathFromUtf8(wav));
    REQUIRE(w.has_value());
    CHECK(w->size() > 44 + 2 * 48000 / 10);
    REQUIRE(audioCli({"--gg-sound", "speaker", "song", "1057", wav, "--data", data}, code));
    CHECK(code == 1);  // a class 3 song does not play on the speaker
    if (const auto dro = testdata::ggCapture("audio/w0_title_attract.dro")) {
        REQUIRE(audioCli({"--gg-dro-compare", log, pathToUtf8(*dro)}, code));
        CHECK(code == 0);
    }
}
