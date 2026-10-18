#pragma once
// Fakes for the audio tests: a manual microsecond clock, a wave output that "plays" on it, a
// scripted input port, an in-memory sound/track loader and a tiny SMF writer. No device is ever
// opened by these.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_system.h"
#include "audio/input_port.h"
#include "audio/midi_driver.h"
#include "audio/smf.h"
#include "audio/sound.h"
#include "audio/wave_output.h"

namespace audiotest {

using namespace tmport::audio;

struct FakeClock {
    uint64_t us = 0;
    MicroClock fn() {
        return [this] { return us; };
    }
};

// Plays a sound for exactly its duration on the fake clock; logs starts and stops.
class FakeWaveOutput final : public WaveOutput {
public:
    struct Entry {
        char what;  // 'S' start, 'X' stop
        uint64_t at;
        int id;
    };
    FakeWaveOutput(FakeClock& c, bool available = true) : clock_(c), available_(available) {}
    bool available() const override { return available_; }
    void start(const Sound& s) override {
        startAt_ = clock_.us;
        durUs_ = s.durationUs();
        id_ = s.id;
        log.push_back({'S', clock_.us, s.id});
    }
    void stop() override { log.push_back({'X', clock_.us, id_}); }
    bool done() override { return clock_.us >= startAt_ + durUs_; }

    std::vector<Entry> log;

private:
    FakeClock& clock_;
    bool available_;
    uint64_t startAt_ = 0, durUs_ = 0;
    int id_ = -1;
};

// Synthetic sounds: id -> length in ms (22050 Hz, silence).
class MapSoundLoader final : public SoundLoader {
public:
    std::map<int, uint32_t> lengthsMs;
    std::shared_ptr<const Sound> load(int id) override {
        auto it = lengthsMs.find(id);
        if (it == lengthsMs.end()) return nullptr;
        auto s = std::make_shared<Sound>();
        s->id = id;
        s->sampleRate = 22050;
        s->pcm.assign(static_cast<size_t>(22050ull * it->second / 1000), 0x80);
        return s;
    }
};

// Input codes that "arrive" at given fake times; idle() advances the clock by 1 ms.
class FakeInputPort final : public InputPort {
public:
    explicit FakeInputPort(FakeClock& c) : clock_(c) {}
    void at(uint64_t ms, uint16_t code) { script_.push_back({ms * 1000, code}); }
    uint16_t peekCode() override {
        deliver();
        ++peeks;
        return queue.empty() ? 0 : queue.front();
    }
    uint16_t getCode() override {
        deliver();
        if (queue.empty()) return 0;
        const uint16_t c = queue.front();
        queue.pop_front();
        consumed.push_back(c);
        return c;
    }
    void idle() override {
        ++idles;
        clock_.us += 1000;
        if (onIdle) onIdle();
    }

    std::deque<uint16_t> queue;
    std::vector<uint16_t> consumed;
    int idles = 0, peeks = 0;
    std::function<void()> onIdle;

private:
    void deliver() {
        while (!script_.empty() && script_.front().first <= clock_.us) {
            queue.push_back(script_.front().second);
            script_.pop_front();
        }
    }
    FakeClock& clock_;
    std::deque<std::pair<uint64_t, uint16_t>> script_;
};

class MapTrackLoader final : public TrackLoader {
public:
    std::map<std::string, std::shared_ptr<const MidiSong>> songs;
    std::vector<std::string> requests;
    std::shared_ptr<const MidiSong> load(const std::string& relPath) override {
        requests.push_back(relPath);
        auto it = songs.find(relPath);
        return it == songs.end() ? nullptr : it->second;
    }
};

// A MIDI output that cannot be opened (no device).
class FailingMidiBackend final : public MidiBackend {
public:
    std::string name() const override { return "failing"; }
    bool open() override { return false; }
    void close() override {}
    bool isOpen() const override { return false; }
    void send(uint8_t, uint8_t, uint8_t) override { ++messages_; }
};

// --- SMF writer ------------------------------------------------------------------------------

inline void putVlq(std::vector<uint8_t>& o, uint32_t v) {
    uint8_t tmp[5];
    int n = 0;
    tmp[n++] = static_cast<uint8_t>(v & 0x7F);
    while (v >>= 7) tmp[n++] = static_cast<uint8_t>(0x80 | (v & 0x7F));
    while (n) o.push_back(tmp[--n]);
}

struct TrackWriter {
    std::vector<uint8_t> bytes;
    TrackWriter& ev(uint32_t delta, std::initializer_list<uint8_t> data) {
        putVlq(bytes, delta);
        bytes.insert(bytes.end(), data.begin(), data.end());
        return *this;
    }
    TrackWriter& tempo(uint32_t delta, uint32_t usPerQuarter) {
        return ev(delta, {0xFF, 0x51, 0x03, static_cast<uint8_t>(usPerQuarter >> 16), static_cast<uint8_t>(usPerQuarter >> 8),
                          static_cast<uint8_t>(usPerQuarter)});
    }
    TrackWriter& end(uint32_t delta) { return ev(delta, {0xFF, 0x2F, 0x00}); }
};

inline std::vector<uint8_t> smfFile(uint16_t format, uint16_t division, const std::vector<TrackWriter>& tracks) {
    std::vector<uint8_t> o = {'M', 'T', 'h', 'd', 0, 0, 0, 6};
    auto be16 = [&](uint16_t v) {
        o.push_back(static_cast<uint8_t>(v >> 8));
        o.push_back(static_cast<uint8_t>(v));
    };
    be16(format);
    be16(static_cast<uint16_t>(tracks.size()));
    be16(division);
    for (const auto& t : tracks) {
        o.insert(o.end(), {'M', 'T', 'r', 'k'});
        const uint32_t n = static_cast<uint32_t>(t.bytes.size());
        o.push_back(static_cast<uint8_t>(n >> 24));
        o.push_back(static_cast<uint8_t>(n >> 16));
        o.push_back(static_cast<uint8_t>(n >> 8));
        o.push_back(static_cast<uint8_t>(n));
        o.insert(o.end(), t.bytes.begin(), t.bytes.end());
    }
    return o;
}

// A test tune: program `prog` on channel 0, then one note per second (on at n s, off at n+0.5 s)
// for `seconds` seconds, 480 PPQN at 120 BPM (one quarter = 500 ms). Length = seconds.
inline std::shared_ptr<const MidiSong> makeTune(uint8_t prog, uint32_t seconds) {
    TrackWriter t;
    t.tempo(0, 500000).ev(0, {0xC0, prog}).ev(0, {0xB0, 7, 100});
    uint32_t last = 0;
    for (uint32_t n = 0; n < seconds; ++n) {
        const uint32_t on = n * 960, off = on + 480;
        t.ev(on - last, {0x90, static_cast<uint8_t>(60 + prog % 12), 100});
        t.ev(off - on, {0x80, static_cast<uint8_t>(60 + prog % 12), 0});
        last = off;
    }
    t.end(seconds * 960 - last);
    return std::make_shared<const MidiSong>(parseSmf(smfFile(0, 480, {t})));
}

// Tables shaped like the game's (ids 9000..9009, index 6 plays once) with synthetic file names.
inline AudioTables makeTables() {
    AudioTables t;
    for (size_t i = 0; i < AudioTables::kTunes; ++i) {
        t.tuneIds[i] = static_cast<int16_t>(9000 + i);
        t.loopPos[i] = i == 6 ? -1 : 0;
        t.tuneFiles[i] = " t" + std::to_string(i) + ".mid";
    }
    t.soundsDir = "MUSIC\\";
    t.soundOn = 1;
    t.voiceOn = 1;
    t.initialTuneId = -1;
    t.stopLists[static_cast<size_t>(StopList::ClueReplay)] = {0x0D, 0x1B};
    t.stopLists[static_cast<size_t>(StopList::Hint)] = {0x0D, 0x1B, 0x4B0};
    t.stopLists[static_cast<size_t>(StopList::Praise)] = {0x1B, 0x0D};
    t.stopLists[static_cast<size_t>(StopList::WrongAnswer)] = {0x1B, 0x0D};
    const std::vector<uint16_t> quiz = {0x126, 0x128, 0x125, 0x127, 0x0D,  0x409, 0x1B,  0xA80, 0xA81,
                                        0xA82, 0xA70, 0xA71, 0xA72, 0xA73, 0xA74, 0xA75, 0xA76, 0xA88};
    t.stopLists[static_cast<size_t>(StopList::QuizLine)] = quiz;
    t.stopLists[static_cast<size_t>(StopList::QuizAnswer)] = quiz;
    return t;
}

// An AudioSystem wired to fakes on one fake clock. Tune k (file "MUSIC\Etk.mid" / "MUSIC\Btk.mid")
// lasts k+2 seconds and uses program k.
struct Rig {
    FakeClock clock;
    FakeWaveOutput* wave = nullptr;
    MapSoundLoader* sounds = nullptr;
    MapTrackLoader* tracks = nullptr;
    RecordingMidiBackend* midi = nullptr;      // when the MIDI output works
    FailingMidiBackend* failing = nullptr;     // when it does not (midiOk = 0)
    std::unique_ptr<AudioSystem> audio;

    explicit Rig(bool waveAvailable = true, bool midiOk = true) {
        AudioParts p;
        auto w = std::make_unique<FakeWaveOutput>(clock, waveAvailable);
        wave = w.get();
        p.wave = std::move(w);
        auto s = std::make_unique<MapSoundLoader>();
        sounds = s.get();
        p.sounds = std::move(s);
        auto t = std::make_unique<MapTrackLoader>();
        tracks = t.get();
        for (int k = 0; k < 10; ++k) {
            auto song = makeTune(static_cast<uint8_t>(k), static_cast<uint32_t>(k + 2));
            t->songs["MUSIC\\Et" + std::to_string(k) + ".mid"] = song;
            t->songs["MUSIC\\Bt" + std::to_string(k) + ".mid"] = song;
        }
        p.tracks = std::move(t);
        if (midiOk) {
            auto b = std::make_unique<RecordingMidiBackend>(clock.fn());
            midi = b.get();
            p.midi = std::make_unique<MidiPlayer>(std::move(b), clock.fn(), false);
        } else {
            auto b = std::make_unique<FailingMidiBackend>();
            failing = b.get();
            p.midi = std::make_unique<MidiPlayer>(std::move(b), clock.fn(), false);
        }
        audio = std::make_unique<AudioSystem>(makeTables(), std::move(p));
    }

    // Advances the fake clock in 1 ms steps, servicing the audio system like Engine::pump.
    void run(uint64_t ms) {
        for (uint64_t i = 0; i < ms; ++i) {
            clock.us += 1000;
            audio->service();
        }
    }

    // Note-on messages recorded at or after t (us).
    std::vector<RecordingMidiBackend::Message> noteOnsSince(uint64_t t) const {
        std::vector<RecordingMidiBackend::Message> v;
        if (!midi) return v;
        for (const auto& m : midi->log)
            if (m.timeUs >= t && (m.status & 0xF0) == 0x90 && m.d2 > 0) v.push_back(m);
        return v;
    }
};

}  // namespace audiotest
