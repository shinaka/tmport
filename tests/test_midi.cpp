// Audio area: SMF parser, real-time sequencer (fake clock) and the threaded player (null back end).

#include <doctest/doctest.h>

#include <SDL3/SDL_timer.h>

#include <map>
#include <string>

#include "audio/midi_backend.h"
#include "audio/midi_player.h"
#include "audio/midi_sequencer.h"
#include "audio/smf.h"
#include "audio_test_util.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/util/hash.h"
#include "test_data.h"

using namespace tmport;
using namespace tmport::audio;
using namespace audiotest;

namespace {

// Format 1, 96 PPQN: track 0 = tempo map (120 BPM, then 240 BPM from tick 192), track 1 = notes
// with running status across a meta event, a SysEx, and a status byte after it.
std::vector<uint8_t> syntheticFormat1() {
    TrackWriter t0;
    t0.tempo(0, 500000).ev(0, {0xFF, 0x58, 0x04, 4, 2, 24, 8}).tempo(192, 250000).end(192);
    TrackWriter t1;
    t1.ev(0, {0xC1, 0x05})
        .ev(0, {0x91, 0x3C, 0x64})
        .ev(0, {0xFF, 0x01, 0x02, 'h', 'i'})   // meta: running status survives
        .ev(96, {0x3E, 0x50})                  // running status note-on
        .ev(0, {0xF0, 0x03, 0x7E, 0x7F, 0xF7}) // SysEx: skipped by playback, cancels running status
        .ev(96, {0x81, 0x3C, 0x00})
        .ev(96, {0x3E, 0x00})                  // running status note-off
        .end(0);
    return smfFile(1, 96, {t0, t1});
}

std::uint64_t channelHash(const MidiSong& s) {
    uint64_t h = mischief::kFnv64Offset;
    for (const auto& e : s.events) {
        if (!e.isChannel()) continue;
        uint8_t b[11];
        for (int i = 0; i < 8; ++i) b[i] = static_cast<uint8_t>(e.us >> (8 * i));
        b[8] = e.status;
        b[9] = e.data1;
        b[10] = e.data2;
        h = mischief::fnv1a64(b, h);
    }
    return h;
}

struct SmfExpect {
    const char* file;
    int format, tracks, division, channelEvents, noteOns, noteOffs, metas, sysex;
    uint32_t endTick, lastNoteOnTick;
    uint64_t lengthUs;
    int tempos;
    uint32_t tempo0;
    uint64_t hash;  // FNV-1a 64 over the merged channel events (u64 LE us, status, d1, d2)
};
// From an independent Python parser: extracted/scratch/audio/smf_ref.py (smf_ref.txt). Lengths
// agree with re/docs/fmt_sound.md §8.1 (BOPEN1 61.9 s, EOPEN1 8.2 s, ...).
const SmfExpect kSmf[] = {
    {"BCASTLE.MID", 0, 1, 480, 2541, 1267, 1267, 6, 0, 57600u, 57480u, 67924440ull, 1, 566037u, 0xbfcff9048972937dull},
    {"BCLUBHSE.MID", 0, 1, 480, 1326, 659, 659, 8, 0, 44640u, 44163u, 43936920ull, 1, 472440u, 0x8e1c0b92402d4654ull},
    {"BFINALE.MID", 0, 1, 480, 264, 128, 128, 7, 0, 7680u, 7561u, 7680000ull, 1, 480000u, 0xf6a6f209a66f8167ull},
    {"BLEVEL1.MID", 0, 1, 480, 3387, 1689, 1689, 6, 0, 40320u, 40200u, 51958704ull, 1, 618556u, 0xe6ec7a3c72c8c9c3ull},
    {"BLEVEL2.MID", 0, 1, 480, 1601, 797, 797, 6, 0, 50622u, 49860u, 45198169ull, 1, 428571u, 0x17ab65f176f4291cull},
    {"BLEVEL3.MID", 0, 1, 480, 2576, 1285, 1285, 6, 0, 69120u, 68880u, 55031760ull, 1, 382165u, 0xd960da60e8946b9eull},
    {"BOPEN1.MID", 0, 1, 480, 161, 72, 72, 7, 0, 61920u, 7920u, 61920000ull, 1, 480000u, 0x6e865eb55f7b33acull},
    {"BOPEN2.MID", 0, 1, 480, 633, 299, 299, 7, 0, 23046u, 22800u, 23046000ull, 1, 480000u, 0x57c64c3c1acc9a17ull},
    {"BTHRONE.MID", 0, 1, 480, 1418, 682, 682, 6, 0, 30720u, 30480u, 27428544ull, 1, 428571u, 0x1e15ea8b3b566f26ull},
    {"BTITLE.MID", 0, 1, 480, 419, 208, 208, 6, 0, 15360u, 14880u, 16695648ull, 1, 521739u, 0x8467b08eaa165fd2ull},
    {"ECASTLE.MID", 0, 1, 480, 2853, 1422, 1422, 6, 0, 57600u, 57480u, 67924440ull, 1, 566037u, 0x299e3b852b823098ull},
    {"ECLUBHSE.MID", 0, 1, 480, 1326, 659, 659, 8, 0, 44640u, 44163u, 43936920ull, 1, 472440u, 0x5fac4621047a4306ull},
    {"EFINALE.MID", 0, 1, 480, 378, 184, 184, 7, 0, 7680u, 7561u, 7680000ull, 1, 480000u, 0x4276327f6bdb3c60ull},
    {"ELEVEL1.MID", 0, 1, 480, 3575, 1782, 1782, 6, 0, 40320u, 40200u, 51958704ull, 1, 618556u, 0x5614bfc21628e47dull},
    {"ELEVEL2.MID", 0, 1, 480, 1601, 797, 797, 6, 0, 50622u, 49860u, 45198169ull, 1, 428571u, 0x299f7561092b5508ull},
    {"ELEVEL3.MID", 0, 1, 480, 2576, 1285, 1285, 6, 0, 69120u, 68880u, 55031760ull, 1, 382165u, 0x97d7a754bc17ba46ull},
    {"EOPEN1.MID", 0, 1, 480, 168, 75, 75, 7, 0, 8160u, 7920u, 8160000ull, 1, 480000u, 0x04298f386fec26e8ull},
    {"EOPEN2.MID", 0, 1, 480, 633, 299, 299, 7, 0, 23046u, 22800u, 23046000ull, 1, 480000u, 0x446e5f09f0c15679ull},
    {"ETHRONE.MID", 0, 1, 480, 1496, 720, 720, 6, 0, 30720u, 30480u, 27428544ull, 1, 428571u, 0x9ca5ee018b0e1ef4ull},
    {"ETITLE.MID", 0, 1, 480, 420, 208, 208, 6, 0, 15360u, 14880u, 16695648ull, 1, 521739u, 0x3fe38006f1966446ull},
};

// Runs the sequencer from 0 on a fake clock in 1 ms steps; returns the end time.
uint64_t runToEnd(MidiSequencer& seq, FakeClock& clock, uint64_t limitUs) {
    seq.start(clock.us, 0);
    while (clock.us <= limitUs) {
        if (seq.advance(clock.us)) return clock.us;
        clock.us += 1000;
    }
    return UINT64_MAX;
}

}  // namespace

TEST_CASE("SMF: format 1 merge, running status, SysEx skip, tempo change") {
    const MidiSong s = parseSmf(syntheticFormat1());
    CHECK(s.format == 1);
    CHECK(s.tracks.size() == 2);
    CHECK(s.ppqn == 96);
    CHECK(s.warnings.empty());
    REQUIRE(s.tempoMap.size() == 2);
    CHECK(s.tempoMap[1].tick == 192);
    CHECK(s.tempoMap[1].us == 1'000'000);
    CHECK(s.tempoMap[1].usPerQuarter == 250000u);
    CHECK(s.endTick == 384);
    CHECK(s.lengthUs == 1'500'000);
    CHECK(s.tickToUs(96) == 500'000);
    CHECK(s.tickToUs(288) == 1'250'000);

    std::vector<const MidiEvent*> ch;
    size_t sysex = 0, metas = 0;
    for (const auto& e : s.events) {
        if (e.isChannel()) ch.push_back(&e);
        if (e.isSysEx()) ++sysex;
        if (e.isMeta()) ++metas;
    }
    CHECK(sysex == 1);
    CHECK(metas == 6);  // 2 tempo, time signature, text, 2 end-of-track
    REQUIRE(ch.size() == 5);
    CHECK((ch[0]->status == 0xC1 && ch[0]->data1 == 5 && ch[0]->us == 0));
    CHECK((ch[1]->status == 0x91 && ch[1]->data1 == 0x3C && ch[1]->data2 == 0x64));
    CHECK((ch[2]->status == 0x91 && ch[2]->data1 == 0x3E && ch[2]->data2 == 0x50 && ch[2]->us == 500'000));
    CHECK((ch[3]->status == 0x81 && ch[3]->us == 1'000'000));
    CHECK((ch[4]->status == 0x81 && ch[4]->data1 == 0x3E && ch[4]->us == 1'250'000));
    CHECK(ch[2]->isNoteOn());
    CHECK(ch[4]->isNoteOff());
    // Track 0's events come before track 1's at the same tick.
    CHECK(s.events[0].track == 0);
    CHECK(s.noteOnCount() == 2);
    CHECK(s.lastNoteOnTick() == 96);
    CHECK(describeEvent(s, *ch[2]).find("note-on") != std::string::npos);
    CHECK(describeEvent(s, s.events[0]).find("tempo 500000") != std::string::npos);
}

TEST_CASE("SMF: SMPTE division, missing end of track, malformed files") {
    // 25 fps x 40 ticks per frame = 1000 ticks per second.
    TrackWriter t;
    t.ev(0, {0x90, 60, 100}).ev(500, {0x80, 60, 0});  // no end-of-track
    const MidiSong s = parseSmf(smfFile(0, 0xE728, {t}));
    CHECK(s.ppqn == 0);
    CHECK(s.smpteFps == 25);
    CHECK(s.smpteTicksPerFrame == 40);
    CHECK(s.events.back().us == 500'000);
    CHECK(s.lengthUs == 500'000);
    CHECK(s.warnings.size() == 1);

    CHECK_THROWS_AS(parseSmf({'R', 'I', 'F', 'F', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), mischief::FormatError);
    TrackWriter bad;
    bad.ev(0, {0x3C, 0x40}).end(0);  // data byte with no running status
    CHECK_THROWS_AS(parseSmf(smfFile(0, 96, {bad})), mischief::FormatError);
    TrackWriter longVlq;
    longVlq.bytes = {0x81, 0x81, 0x81, 0x81, 0x01, 0x90, 60, 1};
    CHECK_THROWS_AS(parseSmf(smfFile(0, 96, {longVlq})), mischief::FormatError);
    TrackWriter ok;
    ok.end(0);
    CHECK_THROWS_AS(parseSmf(smfFile(0, 0, {ok})), mischief::FormatError);   // zero PPQN
    CHECK_THROWS_AS(parseSmf(smfFile(3, 96, {ok})), mischief::FormatError);  // unknown format
    auto noTracks = smfFile(0, 96, {});
    CHECK_THROWS_AS(parseSmf(noTracks), mischief::FormatError);
}

TEST_CASE("SMF: all 20 SOUNDS\\*.MID match the independent reference parser") {
    TM_REQUIRE_GAME_DATA();
    size_t seen = 0;
    for (const auto& e : kSmf) {
        INFO(e.file);
        auto f = g->source->open(std::string("SOUNDS/") + e.file);
        REQUIRE(f != nullptr);
        const MidiSong s = parseSmf(f->readAll());
        CHECK(s.format == e.format);
        CHECK(static_cast<int>(s.tracks.size()) == e.tracks);
        CHECK(s.division == e.division);
        CHECK(s.warnings.empty());
        size_t ch = 0, on = 0, off = 0, metas = 0, sysex = 0;
        for (const auto& ev : s.events) {
            if (ev.isChannel()) ++ch;
            if (ev.isNoteOn()) ++on;
            if (ev.isNoteOff()) ++off;
            if (ev.isMeta()) ++metas;
            if (ev.isSysEx()) ++sysex;
        }
        CHECK(static_cast<int>(ch) == e.channelEvents);
        CHECK(static_cast<int>(on) == e.noteOns);
        CHECK(static_cast<int>(off) == e.noteOffs);
        CHECK(static_cast<int>(metas) == e.metas);
        CHECK(static_cast<int>(sysex) == e.sysex);
        CHECK(s.endTick == e.endTick);
        CHECK(s.lastNoteOnTick() == e.lastNoteOnTick);
        CHECK(s.lengthUs == e.lengthUs);
        CHECK(static_cast<int>(s.tempoMap.size()) == e.tempos);
        CHECK(s.tempoMap.front().usPerQuarter == e.tempo0);
        CHECK(channelHash(s) == e.hash);
        ++seen;
    }
    CHECK(seen == 20);
    // Every MIDI file in the folder is covered by the table.
    size_t mids = 0;
    for (const auto& d : g->source->list("SOUNDS"))
        if (mischief::toUpperAscii(d.name).ends_with(".MID")) ++mids;
    CHECK(mids == 20);
}

TEST_CASE("Sequencer: events are sent at their time on a fake clock") {
    auto song = std::make_shared<const MidiSong>(parseSmf(syntheticFormat1()));
    FakeClock clock;
    RecordingMidiBackend rec(clock.fn());
    rec.open();
    MidiSequencer seq(rec);
    seq.setSong(song);
    clock.us = 7'000'000;  // any start time
    const uint64_t end = runToEnd(seq, clock, 20'000'000);
    CHECK(end == 7'000'000 + 1'500'000);
    CHECK_FALSE(seq.playing());
    CHECK(seq.positionUs(clock.us) == 1'500'000);
    REQUIRE(rec.log.size() == 5);  // the SysEx is not sent
    const uint64_t expectUs[] = {0, 0, 500'000, 1'000'000, 1'250'000};
    for (size_t i = 0; i < 5; ++i) CHECK(rec.log[i].timeUs == 7'000'000 + expectUs[i]);
    CHECK(rec.silences == 0);  // nothing was left sounding at the end
    CHECK(seq.advance(clock.us + 5000) == false);  // the end is reported once
}

TEST_CASE("Sequencer: stop silences and keeps the position; a mid-song start chases the state") {
    auto song = makeTune(5, 4);  // program 5, CC7, notes at 0,1,2,3 s (0.5 s long)
    FakeClock clock;
    RecordingMidiBackend rec(clock.fn());
    rec.open();
    MidiSequencer seq(rec);
    seq.setSong(song);
    seq.start(clock.us, 0);
    while (clock.us < 1'200'000) {
        seq.advance(clock.us);
        clock.us += 1000;
    }
    const size_t before = rec.log.size();  // program, CC7, on, off, on
    CHECK(before == 5);
    seq.stop(clock.us);
    CHECK(rec.silences == 1);
    // The sounding note gets a note-off before the all-notes-off messages.
    REQUIRE(rec.log.size() > before);
    CHECK(((rec.log[before].status & 0xF0) == 0x80 && rec.log[before].d1 == 65));
    CHECK(seq.positionUs(clock.us + 3'000'000) == 1'200'000);
    for (int i = 0; i < 100; ++i) CHECK_FALSE(seq.advance(clock.us + i * 1000));  // stopped: nothing sent
    const size_t afterStop = rec.log.size();

    clock.us = 5'000'000;
    seq.start(clock.us, 1'200'000);
    REQUIRE(rec.log.size() == afterStop + 2);  // chase: program 5 and CC7 = 100, in order
    CHECK((rec.log[afterStop].status == 0xC0 && rec.log[afterStop].d1 == 5));
    CHECK((rec.log[afterStop + 1].status == 0xB0 && rec.log[afterStop + 1].d1 == 7 && rec.log[afterStop + 1].d2 == 100));
    while (rec.log.size() == afterStop + 2 && clock.us < 6'000'000) {
        seq.advance(clock.us);
        clock.us += 1000;
    }
    // The next song event (note-off at 1.5 s) arrives 0.3 s after the resume.
    CHECK(rec.log.back().timeUs == 5'300'000);
    CHECK((rec.log.back().status & 0xF0) == 0x80);
}

TEST_CASE("Sequencer: all 20 game tunes play on a fake clock to their end") {
    TM_REQUIRE_GAME_DATA();
    for (const auto& e : kSmf) {
        INFO(e.file);
        auto f = g->source->open(std::string("SOUNDS/") + e.file);
        REQUIRE(f != nullptr);
        auto song = std::make_shared<const MidiSong>(parseSmf(f->readAll()));
        FakeClock clock;
        RecordingMidiBackend rec(clock.fn());
        rec.open();
        MidiSequencer seq(rec);
        seq.setSong(song);
        const uint64_t end = runToEnd(seq, clock, song->lengthUs + 2'000'000);
        CHECK(end >= song->lengthUs);
        CHECK(end < song->lengthUs + 1000);
        REQUIRE(static_cast<int>(rec.log.size()) == e.channelEvents);
        size_t i = 0;
        uint64_t maxLate = 0;
        bool sameOrder = true;
        for (const auto& ev : song->events) {
            if (!ev.isChannel()) continue;
            const auto& m = rec.log[i++];
            sameOrder = sameOrder && m.status == ev.status && m.d1 == ev.data1 && m.d2 == ev.data2;
            CHECK(m.timeUs >= ev.us);
            maxLate = std::max(maxLate, m.timeUs - ev.us);
        }
        CHECK(sameOrder);
        CHECK(maxLate < 1000);
    }
}

TEST_CASE("MidiPlayer: threaded playback on the real clock (recording back end, no device)") {
    TrackWriter t;  // 480 PPQN, 120 BPM: 48 ticks = 50 ms
    t.tempo(0, 500000).ev(0, {0x90, 60, 100}).ev(24, {0x80, 60, 0}).end(24);
    auto song = std::make_shared<const MidiSong>(parseSmf(smfFile(0, 480, {t})));
    CHECK(song->lengthUs == 50'000);
    auto rec = std::make_unique<RecordingMidiBackend>(MicroClock(realClockUs));
    RecordingMidiBackend* r = rec.get();
    MidiPlayer player(std::move(rec), MicroClock(realClockUs), true);
    REQUIRE(player.ok());
    player.load(song);
    const uint64_t t0 = SDL_GetTicks();
    player.play(0);
    bool finished = false;
    while (!finished && SDL_GetTicks() - t0 < 2000) {
        finished = player.takeFinished();
        if (!finished) SDL_Delay(2);
    }
    CHECK(finished);
    CHECK(SDL_GetTicks() - t0 >= 40);
    CHECK_FALSE(player.playing());
    size_t n = 0;
    player.withBackend([&](MidiBackend&) { n = r->log.size(); });
    CHECK(n == 2);
    // A stop discards the notification; play again and stop at once.
    player.play(0);
    player.stop();
    SDL_Delay(80);
    CHECK_FALSE(player.takeFinished());
}
