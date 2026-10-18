// Audio area: SSND decoding and WAV export, the single wave voice, sound effects, the speech wait
// (Wave_PlayResUntilEvent 1088:01a9), the jingle rule and the settings toggles. Everything runs
// on a fake clock except the SDL test, which uses SDL's silent "dummy" audio driver.

#include <doctest/doctest.h>

#include <SDL3/SDL_timer.h>

#include <array>
#include <cmath>
#include <cstring>

#include "audio/audio_system.h"
#include "audio/audio_tables.h"
#include "audio/engine_input_port.h"
#include "audio/sdl_wave_output.h"
#include "platform/engine.h"
#include "timing/clock.h"
#include "audio/sound.h"
#include "audio/wave_voice.h"
#include "audio_test_util.h"
#include "res/resource_manager.h"
#include "test_data.h"
#include "util/hash.h"

using namespace tmport;
using namespace tmport::audio;
using namespace audiotest;

// ------------------------------------------------------------------------------------ SSND / WAV

TEST_CASE("Audio: WAV layout equals Python's wave module (synthetic data)") {
    // Bytes Python 3's wave module writes for 3 samples (80 81 82) at 22050 Hz, mono, 8 bit:
    // the odd data length gets a zero pad byte that the RIFF size counts.
    const uint8_t expect[] = {'R', 'I', 'F', 'F', 0x28, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                              1,   0,   1,   0,   0x22, 0x56, 0, 0, 0x22, 0x56, 0, 0, 1, 0, 8, 0, 'd', 'a', 't', 'a',
                              3,   0,   0,   0,   0x80, 0x81, 0x82, 0};
    Sound s;
    s.sampleRate = 22050;
    s.pcm = {0x80, 0x81, 0x82};
    const auto w = makeWav(s);
    REQUIRE(w.size() == sizeof(expect));
    CHECK(std::memcmp(w.data(), expect, sizeof(expect)) == 0);
    s.pcm.push_back(0x83);
    CHECK(makeWav(s).size() == 48);  // even: no pad

    // SSND header decode.
    std::vector<uint8_t> raw = {0, 0, 0, 0, 0x22, 0x56, 0, 0, 4, 0, 0, 0, 1, 2, 3, 4, 0, 0};
    auto d = soundFromSsnd(raw, 7);
    REQUIRE(d.has_value());
    CHECK(d->id == 7);
    CHECK(d->sampleRate == 22050u);
    CHECK(d->pcm == std::vector<uint8_t>{1, 2, 3, 4});
    CHECK(d->durationUs() == 181u);
    CHECK_FALSE(soundFromSsnd(std::vector<uint8_t>(11, 0)).has_value());
}

namespace {

struct DllExpect {
    DllId dll;
    size_t count;
    uint32_t minSamples, maxSamples;
    uint64_t totalSamples;
    int minCs, maxCs;   // re/docs/fmt_sound.md §2.1, shortest / longest in 1/100 s
    int totalDs;        // total in 1/10 s
};
// Counts and sample totals from tools/assets/ssnd.py's manifest (extracted/scratch/sound/manifest.json),
// rounded durations from the fmt_sound.md §2.1 table.
const DllExpect kDlls[] = {
    {DllId::TM0SOUND, 42, 3068, 154404, 1081580, 14, 700, 491},
    {DllId::TM1SOUND, 203, 9748, 232692, 10649962, 44, 1055, 4830},
    {DllId::TM2SOUND, 400, 9114, 97294, 12180674, 41, 441, 5524},
    {DllId::TM3SOUND, 400, 9684, 120954, 12855640, 44, 549, 5830},
    {DllId::TM4SOUND, 400, 9748, 98834, 14096704, 44, 448, 6393},
    {DllId::TM5SOUND, 400, 10216, 98358, 14128368, 46, 446, 6407},
    {DllId::TM6SOUND, 400, 9538, 107154, 14869992, 43, 486, 6744},
    {DllId::TM7SOUND, 400, 9240, 101730, 14867526, 42, 461, 6743},
    {DllId::TM8SOUND, 216, 9368, 122390, 8387062, 42, 555, 3804},
};

struct WavExpect {
    int id;
    size_t size;
    uint64_t hash;  // FNV-1a 64 of the whole WAV file written by tools/assets/ssnd.py
};
// extracted/scratch/audio/wav_ref.py
const WavExpect kWavs[] = {
    {7000, 34816u, 0x8d4ffaa415069344ull}, {7012, 34492u, 0x4797fc7478e0decdull}, {7101, 44736u, 0x01f53fe06b694401ull},
    {4000, 71490u, 0x50d969827455be24ull}, {6615, 14046u, 0x01cf99dabb20ccabull}, {7552, 17088u, 0x28f0e7d609067659ull},
};

}  // namespace

TEST_CASE("Audio: all 2861 SSND sounds decode (counts and durations vs fmt_sound.md §2.1)") {
    TM_REQUIRE_GAME_DATA();
    ResourceSoundLoader loader(*g->res);
    size_t total = 0;
    uint64_t totalSamples = 0;
    for (const auto& e : kDlls) {
        INFO(dllFileName(e.dll));
        const auto ids = g->res->ids(ResType::SSND, e.dll);
        CHECK(ids.size() == e.count);
        uint32_t mn = UINT32_MAX, mx = 0;
        uint64_t sum = 0;
        for (int id : ids) {
            CHECK(dllFor(ResType::SSND, id, gfx::ColorMode::Colour256) == e.dll);  // routing agrees
            auto s = loader.load(id);
            REQUIRE(s != nullptr);
            CHECK(s->sampleRate == 22050u);
            CHECK(s->pcm.front() == 0x80);
            const uint32_t n = static_cast<uint32_t>(s->pcm.size());
            mn = std::min(mn, n);
            mx = std::max(mx, n);
            sum += n;
        }
        CHECK(mn == e.minSamples);
        CHECK(mx == e.maxSamples);
        CHECK(sum == e.totalSamples);
        CHECK(std::lround(mn / 22050.0 * 100) == e.minCs);
        CHECK(std::lround(mx / 22050.0 * 100) == e.maxCs);
        CHECK(std::lround(sum / 22050.0 * 10) == e.totalDs);
        total += ids.size();
        totalSamples += sum;
    }
    CHECK(total == 2861);
    // fmt_sound.md §2.1: "4676.6 s" is the sum of the rounded per-DLL totals; 77.9 minutes.
    int sumDs = 0;
    for (const auto& e : kDlls) sumDs += e.totalDs;
    CHECK(sumDs == 46766);
    CHECK(totalSamples == 103117508u);
    CHECK(std::lround(totalSamples / 22050.0 / 60 * 10) == 779);
    // Unmapped ids yield no sound (the original never asks for them).
    CHECK(loader.load(6800) == nullptr);
    CHECK(loader.load(8000) == nullptr);
}

TEST_CASE("Audio: WAV export matches tools/assets/ssnd.py byte for byte") {
    TM_REQUIRE_GAME_DATA();
    ResourceSoundLoader loader(*g->res);
    for (const auto& e : kWavs) {
        INFO("SSND " << e.id);
        auto s = loader.load(e.id);
        REQUIRE(s != nullptr);
        const auto w = makeWav(*s);
        CHECK(w.size() == e.size);
        CHECK(fnv1a64(w) == e.hash);
    }
}

TEST_CASE("Audio: stop-code lists and initial settings read from the EXE") {
    TM_REQUIRE_GAME_DATA();
    const AudioTables t = AudioTables::fromExe(*g->exe);
    CHECK(t.stopCodes(StopList::ClueReplay) == std::vector<uint16_t>{0x0D, 0x1B});
    CHECK(t.stopCodes(StopList::Hint) == std::vector<uint16_t>{0x0D, 0x1B, 0x4B0});
    CHECK(t.stopCodes(StopList::Praise) == std::vector<uint16_t>{0x1B, 0x0D});
    CHECK(t.stopCodes(StopList::WrongAnswer) == std::vector<uint16_t>{0x1B, 0x0D});
    const std::vector<uint16_t> quiz = {0x126, 0x128, 0x125, 0x127, 0x0D,  0x409, 0x1B,  0xA80, 0xA81,
                                        0xA82, 0xA70, 0xA71, 0xA72, 0xA73, 0xA74, 0xA75, 0xA76, 0xA88};
    CHECK(t.stopCodes(StopList::QuizLine) == quiz);
    CHECK(t.stopCodes(StopList::QuizAnswer) == quiz);
    CHECK(t.soundOn == 1);
    CHECK(t.voiceOn == 1);
    CHECK(t.initialTuneId == -1);
    for (size_t i = 0; i < AudioTables::kTunes; ++i) {
        CHECK(t.tuneIds[i] == static_cast<int16_t>(9000 + i));
        CHECK(t.loopPos[i] == (i == 6 ? -1 : 0));
        CHECK(t.tuneFiles[i].size() > 5);
        CHECK(t.tuneFiles[i][0] == ' ');  // placeholder byte replaced by 'B' / 'E'
    }
    CHECK_FALSE(t.soundsDir.empty());
}

// ------------------------------------------------------------------------------- wave voice

TEST_CASE("Audio: one voice -- every new sound cuts the current one") {
    Rig r;
    r.sounds->lengthsMs = {{7001, 500}, {7002, 800}, {7100, 1000}, {7003, 300}, {7004, 300}};
    auto& a = *r.audio;
    auto& log = r.wave->log;

    a.sfxPlay(1);
    REQUIRE(a.voice().current() != nullptr);
    CHECK(a.voice().current()->id == 7001);
    r.run(100);
    a.sfxPlay(2);  // effect cuts effect
    REQUIRE(log.size() == 3);
    CHECK((log[0].what == 'S' && log[0].id == 7001));
    CHECK((log[1].what == 'X' && log[1].id == 7001 && log[1].at == 100'000));
    CHECK((log[2].what == 'S' && log[2].id == 7002));

    r.run(100);
    a.startSpeech(7100);  // speech cuts the effect
    CHECK(a.voice().current()->id == 7100);
    CHECK(log[log.size() - 2].what == 'X');
    r.run(100);
    a.sfxPlay(3);  // an effect cuts speech
    CHECK(a.voice().current()->id == 7003);
    CHECK((log[log.size() - 2].what == 'X' && log[log.size() - 2].id == 7100));

    // Natural end: the voice reports idle and the device is closed (MM_WOM_DONE -> Wave_Stop).
    r.run(299);
    CHECK(a.voice().isPlaying());
    r.run(1);
    CHECK_FALSE(a.voice().isPlaying());
    CHECK(log.back().what == 'X');

    // Sfx_Stop stops at once.
    a.sfxPlay(4);
    CHECK(a.sfxIsPlaying());
    a.sfxStop();
    CHECK_FALSE(a.sfxIsPlaying());

    // A missing sound neither plays nor cuts anything (Wave_Play returns for NULL).
    a.startSpeech(7100);
    const size_t n = log.size();
    a.startSpeech(4242);
    CHECK(log.size() == n);
    CHECK(a.voice().current()->id == 7100);
}

TEST_CASE("Audio: Sfx_Play gating by Music and Sound, and the held-effect quirk") {
    Rig r;
    r.sounds->lengthsMs = {{7003, 300}, {7004, 300}, {7100, 1000}};
    auto& a = *r.audio;
    a.sfxPlay(3);          // holds an effect copy (DS:2ab2) until the next Sfx_Play / Sfx_Stop
    a.startSpeech(7100);   // speech cuts the effect; the copy stays held
    a.settings().soundOn = 0;
    a.sfxPlay(4);          // sound off: nothing new plays, but the held copy is released with Wave_Stop
    CHECK_FALSE(a.voice().isPlaying());
    a.startSpeech(7100);
    a.sfxPlay(4);          // nothing held now: the speech continues
    CHECK(a.voice().isPlaying());
    CHECK(a.voice().current()->id == 7100);
}

TEST_CASE("Audio: no wave device -- sounds finish at once, speech costs no time") {
    Rig r(false);
    r.sounds->lengthsMs = {{7100, 1000}};
    FakeInputPort in(r.clock);
    r.audio->sfxPlay(1);
    CHECK_FALSE(r.audio->voice().isPlaying());
    CHECK_FALSE(r.audio->playUntilEvent(7100, r.audio->stopCodes(StopList::Hint), in));
    CHECK(in.idles == 0);
    CHECK(r.clock.us == 0);
}

// ------------------------------------------------------------------------------- speech wait

TEST_CASE("Audio: speech wait -- natural end, discarded input, interruption") {
    Rig r;
    r.sounds->lengthsMs = {{7100, 1000}, {7101, 1000}, {4000, 1000}, {4001, 1000}};
    auto& a = *r.audio;
    const auto& hint = a.stopCodes(StopList::Hint);

    SUBCASE("plays to the end; non-matching input is consumed and discarded") {
        FakeInputPort in(r.clock);
        in.at(100, 'x');
        in.at(200, 0x20);
        in.at(300, 0x125);
        CHECK_FALSE(a.playUntilEvent(7100, hint, in));
        CHECK(r.clock.us >= 1'000'000);
        CHECK(r.clock.us <= 1'002'000);
        CHECK(in.queue.empty());
        CHECK(in.consumed == std::vector<uint16_t>{'x', 0x20, 0x125});
        CHECK_FALSE(a.voice().isPlaying());
    }
    SUBCASE("a stop code interrupts at once and stays queued for the caller") {
        FakeInputPort in(r.clock);
        in.at(100, 'q');
        in.at(300, 0x0D);
        CHECK(a.playUntilEvent(7100, hint, in));
        CHECK(r.clock.us == 300'000);
        CHECK_FALSE(a.voice().isPlaying());
        REQUIRE(in.queue.size() == 1);
        CHECK(in.getCode() == 0x0D);
        CHECK(in.consumed == std::vector<uint16_t>{'q', 0x0D});
    }
    SUBCASE("Ctrl+S always interrupts") {
        FakeInputPort in(r.clock);
        in.at(50, 0x13);
        CHECK(a.playUntilEvent(7100, hint, in));
        CHECK(in.queue.front() == 0x13);
    }
    SUBCASE("codes are compared with the mouse flags masked off (c & 0x0FFF)") {
        const auto& quiz = a.stopCodes(StopList::QuizLine);
        FakeInputPort in1(r.clock);
        in1.at(10, 0x2409);  // right-up on hot spot 0x409
        CHECK(a.playUntilEvent(4000, quiz, in1));
        CHECK(in1.queue.front() == 0x2409);
        FakeInputPort in2(r.clock);
        in2.at(10, 0x8A70);  // left-down on line hot spot 0xA70
        CHECK(a.playUntilEvent(4000, quiz, in2));
        FakeInputPort in3(r.clock);
        in3.at(10, 0x84B0);  // left-down on the full-screen hint spot
        CHECK(a.playUntilEvent(7100, hint, in3));
        FakeInputPort in4(r.clock);
        in4.at(10, 0x4B0);   // not in the clue-replay list
        const uint64_t t0 = r.clock.us;
        CHECK_FALSE(a.playUntilEvent(7100, a.stopCodes(StopList::ClueReplay), in4));
        CHECK(r.clock.us - t0 >= 1'000'000);
    }
    SUBCASE("speak() is gated by Voice; sequences stop at the first interruption") {
        FakeInputPort in(r.clock);
        a.setVoiceOn(false);
        CHECK_FALSE(a.speak(7100, StopList::Hint, in));
        CHECK(r.wave->log.empty());
        a.setVoiceOn(true);
        in.at(500, 0x1B);
        const std::array<int, 2> ids = {7100, 7101};
        CHECK(a.speakSequence(ids, StopList::Praise, in));
        for (const auto& e : r.wave->log) CHECK(e.id != 7101);  // the second part never started
        FakeInputPort in2(r.clock);
        CHECK_FALSE(a.speakSequence(ids, StopList::Praise, in2));
        CHECK(r.wave->log.back().id == 7101);
    }
}

TEST_CASE("Audio: jingle then speech -- the jingle finishes first and input waits for the speech") {
    Rig r;
    r.sounds->lengthsMs = {{7012, 1560}, {7300, 800}, {7308, 600}};
    auto& a = *r.audio;
    FakeInputPort in(r.clock);

    // No input: sfx 12 plays to its end, then the praise and the clue word.
    const std::array<int, 2> ids = {7300, 7308};
    CHECK_FALSE(a.jingleThenSpeak(12, ids, StopList::Praise, in));
    const auto& log = r.wave->log;
    REQUIRE(log.size() >= 3);
    CHECK((log[0].what == 'S' && log[0].id == 7012 && log[0].at == 0));
    uint64_t speechStart = 0;
    for (const auto& e : log)
        if (e.what == 'S' && e.id == 7300) speechStart = e.at;
    CHECK(speechStart >= 1'560'000);
    CHECK(r.clock.us >= 1'560'000 + 800'000 + 600'000);

    // Enter pressed during the jingle is not consumed by the wait; it then cuts the speech at once.
    Rig r2;
    r2.sounds->lengthsMs = {{7012, 1560}, {7300, 800}, {7308, 600}};
    FakeInputPort in2(r2.clock);
    in2.at(200, 0x0D);
    CHECK(r2.audio->jingleThenSpeak(12, ids, StopList::Praise, in2));
    CHECK(r2.clock.us >= 1'560'000);
    CHECK(r2.clock.us < 1'570'000);
    CHECK(in2.queue.front() == 0x0D);
}

// ------------------------------------------------------------------------------------ settings

TEST_CASE("Audio: Ctrl+S, options and sign-in rules for the two settings") {
    Rig r;
    r.sounds->lengthsMs = {{7100, 1000}};
    auto& a = *r.audio;
    auto set = [&](int s, int v) {
        a.settings().soundOn = static_cast<uint8_t>(s);
        a.settings().voiceOn = static_cast<uint8_t>(v);
    };
    // Ctrl+S: both on only if both were off.
    set(1, 1); a.toggleAll(); CHECK((!a.soundOn() && !a.voiceOn()));
    set(0, 0); a.toggleAll(); CHECK((a.soundOn() && a.voiceOn()));
    set(1, 0); a.toggleAll(); CHECK((!a.soundOn() && !a.voiceOn()));
    set(0, 1); a.toggleAll(); CHECK((!a.soundOn() && !a.voiceOn()));
    // Ctrl+S cuts a sentence (Sfx_Stop -> Wave_Stop).
    set(1, 1);
    a.startSpeech(7100);
    a.toggleAll();
    CHECK_FALSE(a.voice().isPlaying());

    // Options dialog: toggles only when the answer differs.
    set(1, 1);
    a.setSoundFromOptions(true);
    CHECK(a.soundOn());
    a.setSoundFromOptions(false);
    CHECK_FALSE(a.soundOn());
    a.setSoundFromOptions(true);
    CHECK(a.soundOn());

    // Sign-in can switch Music and Sound off but never on; Voice is copied.
    set(0, 1);
    a.applySignInSettings(1, 0);
    CHECK_FALSE(a.soundOn());
    CHECK_FALSE(a.voiceOn());
    set(1, 0);
    a.applySignInSettings(0, 1);
    CHECK_FALSE(a.soundOn());
    CHECK(a.voiceOn());
    set(1, 1);
    a.applySignInSettings(1, 1);
    CHECK(a.soundOn());
    // A new player switches Music and Sound on.
    set(0, 0);
    a.onNewPlayer();
    CHECK(a.soundOn());
}

// ------------------------------------------------------------------ engine integration

TEST_CASE("Audio: speech wait on the headless Engine (peek look-ahead, Ctrl+S hook)") {
    TM_REQUIRE_GAME_DATA();
    Rig r;
    r.sounds->lengthsMs = {{7100, 1000}};
    ManualClock clock(10000);
    clock.onSleep = [&](uint64_t) { r.clock.us += 1000; };  // the wave voice runs on the rig clock
    EngineConfig cfg;
    cfg.clock = &clock;
    Engine engine(*g, gfx::ColorMode::Colour256, cfg);
    connectAudioToEngine(engine, *r.audio);
    EngineInputPort port(engine, r.audio.get());
    auto& a = *r.audio;

    // A non-matching key is consumed and lost; Enter interrupts and stays for the next fetch.
    engine.postKey('x');
    engine.postKey(0x0D);
    CHECK(a.playUntilEvent(7100, a.stopCodes(StopList::Hint), port));
    CHECK(engine.getInputCode() == 0x0D);
    CHECK(engine.getInputCode() == 0);

    // Plays to the end without input (1 s on the clock).
    const uint64_t t0 = r.clock.us;
    CHECK_FALSE(a.playUntilEvent(7100, a.stopCodes(StopList::Hint), port));
    CHECK(r.clock.us - t0 >= 1'000'000);

    // Ctrl+S: the event fetch toggles sound and voice off (hook), and the code interrupts.
    clock.onSleep = [&](uint64_t now) {
        r.clock.us += 1000;
        if (now == 10100) engine.postKey(0x13);
    };
    clock.set(10000);
    CHECK(a.soundOn());
    CHECK(a.playUntilEvent(7100, a.stopCodes(StopList::Hint), port));
    CHECK_FALSE(a.soundOn());
    CHECK_FALSE(a.voiceOn());
    CHECK_FALSE(a.voice().isPlaying());
    CHECK(engine.getInputCode() == 0x13);  // still delivered to the game
}

// ----------------------------------------------------------------------------- SDL (dummy)

TEST_CASE("Audio: SDL wave voice on the silent dummy driver") {
    SdlWaveOutput out("dummy");  // never a real device
    if (!out.open()) {
        MESSAGE("SDL dummy audio driver unavailable: test skipped");
        return;
    }
    CHECK(out.driverName() == "dummy");
    WaveVoice voice(out);
    REQUIRE(voice.available());

    auto s = std::make_shared<Sound>();
    s->id = 1;
    s->sampleRate = 22050;
    s->pcm.assign(22050 / 5, 0x80);  // 200 ms
    const uint64_t t0 = SDL_GetTicks();
    voice.play(s);
    CHECK(voice.isPlaying());
    while (voice.isPlaying() && SDL_GetTicks() - t0 < 3000) SDL_Delay(2);
    const uint64_t dt = SDL_GetTicks() - t0;
    CHECK_FALSE(voice.isPlaying());
    CHECK(dt >= 100);   // consumed at about real time
    CHECK(dt < 2000);

    // A long sound at another rate is cut at once by stop() and by a new sound.
    auto longer = std::make_shared<Sound>();
    longer->id = 2;
    longer->sampleRate = 11025;
    longer->pcm.assign(11025 * 5, 0x80);
    voice.play(longer);
    SDL_Delay(20);
    CHECK(voice.isPlaying());
    voice.play(s);
    CHECK(voice.current()->id == 1);
    voice.stop();
    CHECK_FALSE(voice.isPlaying());
    CHECK(out.done());
    out.close();
    CHECK_FALSE(out.available());
}

TEST_CASE("Audio: AudioSystem::create on the user's files (dummy SDL driver, null MIDI)") {
    TM_REQUIRE_GAME_DATA();
    AudioConfig cfg;
    cfg.sdlAudioDriver = "dummy";
    cfg.midiBackend = MidiBackendKind::Null;  // never WinMM in tests
    auto a = AudioSystem::create(*g, cfg);
    REQUIRE(a != nullptr);
    CHECK(a->midiPlayer().backendName() == "null");
    if (!a->voice().available()) {
        MESSAGE("SDL dummy audio driver unavailable: wave part skipped");
    } else {
        a->sfxPlay(25);  // SSND 7025, 0.14 s
        REQUIRE(a->voice().current() != nullptr);
        CHECK(a->voice().current()->id == 7025);
        const uint64_t t0 = SDL_GetTicks();
        while (a->voice().isPlaying() && SDL_GetTicks() - t0 < 3000) SDL_Delay(2);
        CHECK(SDL_GetTicks() - t0 >= 80);
        CHECK_FALSE(a->voice().isPlaying());
    }
    // Music on the threaded null back end: a zone starts its tune from the user's SOUNDS folder.
    a->music().playZone(zone::kLevel0);
    CHECK(a->music().isZonePlaying(zone::kLevel0));
    CHECK(a->midi().curMusic() == 9003);
    SDL_Delay(30);
    CHECK(a->midiPlayer().positionUs() > 0);
    a->pauseNested();
    CHECK(a->midi().paused());
    CHECK_FALSE(a->midiPlayer().playing());
    a->resumeNested();
    CHECK(a->midiPlayer().playing());
    a->music().stopScene();
    CHECK_FALSE(a->midi().isPlaying());
}
