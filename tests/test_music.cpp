// Audio area: MIDI driver (1090) and music manager (1128) state machine on a fake clock, plus the
// pause/resume, focus and toggle paths through AudioSystem. Synthetic tunes: tune 900k lasts
// k+2 seconds with one note per second (see audio_test_util.h).

#include <doctest/doctest.h>

#include "audio/audio_system.h"
#include "audio/music_manager.h"
#include "audio_test_util.h"
#include "test_data.h"

using namespace tmport;
using namespace tmport::audio;
using namespace audiotest;

TEST_CASE("Music: zone -> tune table matches the switch in the user's EXE (1128:001a)") {
    TM_REQUIRE_GAME_DATA();
    // Music_PlayZone: 13 case values at CS:00a2, jump targets at CS:00bc; each target starts
    // with MOV word [13ca], imm16 (c7 06 ca 13 lo hi).
    const auto& exe = g->exe->exe();
    const int seg = (0x1128 - 0x1000) / 8 + 1;
    const auto code = exe.segmentData(seg);
    REQUIRE(code.size() > 0x100);
    auto u16 = [&](size_t o) { return static_cast<uint16_t>(code[o] | (code[o + 1] << 8)); };
    int checked = 0;
    for (int i = 0; i < 13; ++i) {
        const int value = static_cast<int16_t>(u16(0xA2 + 2 * i));
        const uint16_t target = u16(0xBC + 2 * i);
        INFO("case " << value << " -> " << target);
        REQUIRE(target + 6u <= code.size());
        CHECK(code[target] == 0xC7);
        CHECK(code[target + 1] == 0x06);
        CHECK(u16(target + 2) == 0x13CA);
        const auto tune = MusicManager::tuneForZone(value);
        REQUIRE(tune.has_value());
        CHECK(*tune == u16(target + 4));
        ++checked;
    }
    CHECK(checked == 13);
    CHECK_FALSE(MusicManager::tuneForZone(8).has_value());
    CHECK_FALSE(MusicManager::tuneForZone(-99).has_value());
}

TEST_CASE("Music: tune files resolve through the EXE tables in both sets") {
    TM_REQUIRE_GAME_DATA();
    const AudioTables t = AudioTables::fromExe(*g->exe);
    DataSourceTrackLoader loader(*g->source);
    MidiPlayer player(std::make_unique<NullMidiBackend>(), MicroClock{}, false);
    for (MusicSet set : {MusicSet::Extended, MusicSet::Base}) {
        MidiDriver drv(t, player, loader, set);
        for (int id = 9000; id < 9010; ++id) {
            const std::string path = drv.trackPath(id);
            INFO(path);
            REQUIRE(!path.empty());
            CHECK(path.substr(t.soundsDir.size(), 1) == (set == MusicSet::Extended ? "E" : "B"));
            auto song = loader.load(path);
            REQUIRE(song != nullptr);
            CHECK(song->lengthUs > 7'000'000);
        }
        CHECK(drv.trackPath(1234).empty());
    }
    // OPEN1: the base file carries ~54 s of trailing silence, the extended one does not.
    MidiDriver e(t, player, loader, MusicSet::Extended), b(t, player, loader, MusicSet::Base);
    CHECK(loader.load(b.trackPath(9006))->lengthUs == 61'920'000);
    CHECK(loader.load(e.trackPath(9006))->lengthUs == 8'160'000);
}

TEST_CASE("Music: zone play, loop from the start, zone-change rule") {
    Rig r;
    auto& a = *r.audio;
    auto& m = a.music();
    CHECK(m.tuneId() == -1);
    m.playZone(zone::kLevel0);
    CHECK(r.tracks->requests.back() == "MUSIC\\Et3.mid");
    CHECK(a.midi().curMusic() == 9003);
    CHECK(m.zone() == zone::kLevel0);
    CHECK(m.sceneActive());
    CHECK(m.isZonePlaying(zone::kLevel0));
    CHECK_FALSE(m.isZonePlaying(zone::kLevel1));

    // Tune 9003 lasts 5 s with notes at 0..4 s. After the end it restarts from 0 (loopPos 0).
    r.run(5000);
    const auto ons = r.noteOnsSince(0);
    REQUIRE(ons.size() == 6);  // 0,1,2,3,4 s and the restart at 5 s
    CHECK(ons[5].timeUs == 5'000'000);
    CHECK(m.isZonePlaying(zone::kLevel0));
    r.run(1500);
    CHECK(r.noteOnsSince(5'000'001).size() == 1);  // 6 s

    // Same music code: nothing happens, the tune keeps its position.
    const uint64_t pos = a.midiPlayer().positionUs();
    CHECK_FALSE(m.onRoomChange(zone::kLevel0, zone::kLevel0, false));
    CHECK(a.midiPlayer().positionUs() == pos);
    // Different code: stop the scene and start the new zone from its beginning.
    const int silencesBefore = r.midi->silences;
    CHECK(m.onRoomChange(zone::kLevel0, zone::kLevel1, false));
    CHECK(r.midi->silences > silencesBefore);
    CHECK(a.midi().curMusic() == 9004);
    CHECK(a.midiPlayer().positionUs() == 0);
    CHECK(m.isZonePlaying(zone::kLevel1));
    // Deferred start (rooms 0 and 9): stopped now, started by the caller after the transition.
    CHECK(m.onRoomChange(zone::kLevel1, zone::kHall, true));
    CHECK_FALSE(a.midi().isPlaying());
    CHECK(m.zone() == 0);
    CHECK_FALSE(m.sceneActive());
    m.playZone(zone::kHall);
    CHECK(a.midi().curMusic() == 9001);
    // Unknown codes do nothing.
    m.playZone(42);
    CHECK(a.midi().curMusic() == 9001);
}

TEST_CASE("Music: OPEN1 plays once, then the intro switches to OPEN2, which loops") {
    Rig r;
    auto& a = *r.audio;
    auto& m = a.music();
    m.playZone(zone::kOpen1);  // tune 9006: 8 s, loopPos -1
    CHECK(a.midi().curMusic() == 9006);
    // The intro loop polls Music_IsZonePlaying(-95) each tick (1138:031d..0331).
    uint64_t switchedAt = 0;
    for (int ms = 0; ms < 20000 && !switchedAt; ++ms) {
        r.run(1);
        if (!m.isZonePlaying(zone::kOpen1)) {
            m.playZone(zone::kOpen2);
            switchedAt = r.clock.us;
        }
    }
    CHECK(switchedAt == 8'000'000);
    CHECK(a.midi().curMusic() == 9007);
    r.run(9000);  // OPEN2 = 9 s: loops
    CHECK(m.isZonePlaying(zone::kOpen2));
    CHECK(r.noteOnsSince(switchedAt + 9'000'000).size() == 1);
}

TEST_CASE("Music: pause and resume at the saved position (dialogs, menus, focus)") {
    Rig r;
    auto& a = *r.audio;
    a.music().playZone(zone::kLevel2);  // tune 9005: 7 s
    r.run(1500);
    CHECK(r.noteOnsSince(0).size() == 2);  // 0 s and 1 s

    a.pauseNested();  // message box
    CHECK(a.pauseDepth() == 1);
    CHECK(a.midi().paused());
    CHECK(a.midi().savedPosMs() == 1500);
    a.pauseNested();  // nested dialog: no second pause
    CHECK(a.pauseDepth() == 2);
    r.run(5000);
    CHECK(r.noteOnsSince(1'500'001).empty());  // silent while paused
    a.resumeNested();
    CHECK(a.midi().paused());  // still nested
    const uint64_t resumeAt = r.clock.us;
    a.resumeNested();
    CHECK(a.pauseDepth() == 0);
    CHECK_FALSE(a.midi().paused());
    r.run(600);
    const auto after = r.noteOnsSince(resumeAt);
    REQUIRE(after.size() == 1);
    CHECK(after[0].timeUs == resumeAt + 500'000);  // the 2 s note, 0.5 s after resuming at 1.5 s
    a.resumeNested();  // extra resume clamps at 0
    CHECK(a.pauseDepth() == 0);

    // Focus loss: the wave voice is cut and the music pauses; focus regained resumes it.
    r.sounds->lengthsMs = {{7001, 2000}};
    a.sfxPlay(1);
    a.appDeactivate();
    CHECK_FALSE(a.appActive());
    CHECK_FALSE(a.voice().isPlaying());
    CHECK(a.midi().paused());
    const uint32_t saved = a.midi().savedPosMs();
    r.run(3000);
    a.appDeactivate();  // repeated deactivation does nothing
    a.appActivate();
    CHECK(a.appActive());
    CHECK_FALSE(a.midi().paused());
    CHECK(a.midiPlayer().positionUs() == saved * 1000ull);
    CHECK_FALSE(a.voice().isPlaying());  // the effect is not resumed

    a.pauseNested();
    a.resumeForce();
    CHECK(a.pauseDepth() == 0);
    CHECK_FALSE(a.midi().paused());
}

TEST_CASE("Music: halt + resume restarts the tune from its beginning (hints, elf questions)") {
    Rig r;
    auto& a = *r.audio;
    auto& m = a.music();
    m.playZone(zone::kCastle);  // tune 9000: 2 s
    r.run(1500);
    m.halt();  // Hint_Show -> Music_Halt
    CHECK(a.midi().curMusic() == -1);
    CHECK_FALSE(a.midi().isPlaying());
    CHECK(m.zone() == zone::kCastle);  // zone and tune kept
    r.run(4000);
    const uint64_t t = r.clock.us;
    m.resume();  // Music_Resume
    CHECK(a.midi().curMusic() == 9000);
    CHECK(a.midiPlayer().positionUs() == 0);
    r.run(1);
    const auto ons = r.noteOnsSince(t);
    REQUIRE(ons.size() == 1);
    CHECK(ons[0].timeUs == t + 1000);  // the first note again (sent on the first service)
}

TEST_CASE("Music: a one-shot tune is abandoned by a pause") {
    Rig r;
    auto& a = *r.audio;
    a.music().playZone(zone::kOpen1);
    r.run(1000);
    a.pauseNested();
    CHECK(a.midi().curMusic() == -1);
    CHECK_FALSE(a.midi().trackOpen());
    CHECK_FALSE(a.midi().paused());
    a.resumeNested();
    CHECK_FALSE(a.midi().isPlaying());
    CHECK_FALSE(a.music().isZonePlaying(zone::kOpen1));
}

TEST_CASE("Music: sound off records the zone; Ctrl+S and options restart it from the start") {
    Rig r;
    auto& a = *r.audio;
    auto& m = a.music();
    a.settings().soundOn = 0;
    m.playZone(zone::kLevel1);
    CHECK(m.tuneId() == 9004);
    CHECK(m.zone() == zone::kLevel1);
    CHECK_FALSE(a.midi().isPlaying());
    a.toggleSound();  // on: Music_Resume
    CHECK(a.midi().curMusic() == 9004);
    r.run(2500);
    a.toggleAll();  // Ctrl+S: both off, music stopped
    CHECK_FALSE(a.midi().isPlaying());
    CHECK_FALSE(a.soundOn());
    a.toggleAll();  // both on: the scene tune restarts from the beginning
    CHECK(a.midi().curMusic() == 9004);
    CHECK(a.midiPlayer().positionUs() == 0);
    a.setSoundFromOptions(false);
    CHECK_FALSE(a.midi().isPlaying());
    a.setSoundFromOptions(true);
    CHECK(a.midi().isPlaying());
    // Music_StopScene forgets the zone: Music_Resume still knows the tune id.
    m.stopScene();
    CHECK(m.zone() == 0);
    m.resume();
    CHECK(a.midi().curMusic() == 9004);
}

TEST_CASE("Music: the inactive-app and paused-flag rules of the 1090 driver") {
    Rig r;
    auto& a = *r.audio;
    auto& m = a.music();
    m.playZone(zone::kLevel0);
    r.run(1200);
    a.appDeactivate();  // paused at 1.2 s
    CHECK(a.midi().paused());
    m.playZone(zone::kThrone);  // inactive: the track is opened but not started
    CHECK(a.midi().trackOpen());
    CHECK_FALSE(a.midiPlayer().playing());
    CHECK(a.midi().curMusic() == 9003);  // stale: set only when Midi_PlayTrack starts a tune
    CHECK(a.midi().isPlaying());         // ...so Midi_IsPlaying says yes although nothing plays
    CHECK(a.midi().paused());            // Midi_PlayTrack does not clear it
    CHECK(a.midi().savedPosMs() == 0);   // Midi_OpenTrack resets it
    const uint64_t t = r.clock.us;
    a.appActivate();  // Sound_ResumeAll -> Midi_Resume plays the new track from 0
    CHECK(a.midiPlayer().playing());
    CHECK(a.midiPlayer().positionUs() == 0);
    CHECK(a.music().isZonePlaying(zone::kThrone));
    r.run(1);
    REQUIRE(r.noteOnsSince(t).size() == 1);
    CHECK(r.noteOnsSince(t)[0].d1 == 60 + 8);  // tune 9008's note
}

TEST_CASE("Music: without MIDI output (midiOk = 0) nothing ever reports playing") {
    Rig r(true, false);
    auto& a = *r.audio;
    CHECK_FALSE(a.midi().midiOk());
    a.music().playZone(zone::kOpen1);
    CHECK(a.midi().curMusic() == 9006);
    CHECK_FALSE(a.midi().isPlaying());
    CHECK_FALSE(a.music().isZonePlaying(zone::kOpen1));  // the intro moves on to OPEN2 at once
    r.run(100);
    CHECK(r.failing->messageCount() == 0);
}

TEST_CASE("Music: B/E selection picks the file set") {
    Rig r;
    auto& a = *r.audio;
    a.midi().setMusicSet(MusicSet::Base);
    a.music().playZone(zone::kThrone);
    CHECK(r.tracks->requests.back() == "MUSIC\\Bt8.mid");
    a.midi().setMusicSet(MusicSet::Extended);
    a.music().resume();
    CHECK(r.tracks->requests.back() == "MUSIC\\Et8.mid");
}
