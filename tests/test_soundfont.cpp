// SoundFont MIDI back end: TinySoundFont synthesizer, the back end's sample-accurate scheduling,
// offline rendering and the --midi-render tool. Everything runs on a synthetic SoundFont generated
// in code (audio/sf2_writer.h: 440 Hz sine / square / fixed-pitch drum tones); nothing is played:
// the PCM is analysed (zero crossings, levels). The only device ever opened is SDL's silent
// "dummy" driver.

#include <SDL3/SDL_timer.h>

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_cli.h"
#include "audio/audio_system.h"
#include "audio/midi_backend_soundfont.h"
#include "audio/midi_render.h"
#include "audio/sdl_wave_output.h"
#include "audio/sf2_synth.h"
#include "audio/sf2_writer.h"
#include "audio_test_util.h"
#include "mischief/util/file_util.h"
#include "save/settings.h"
#include "test_data.h"

using namespace tmport;
using namespace tmport::audio;
using audiotest::FakeClock;
using audiotest::TrackWriter;

namespace {

constexpr int kRate = 44100;

std::shared_ptr<Sf2Synth> testSynth(int rate = kRate) {
    std::string error;
    std::shared_ptr<Sf2Synth> s = Sf2Synth::loadMemory(makeTestSoundFont(), rate, error);
    REQUIRE_MESSAGE(s != nullptr, error);
    return s;
}

std::filesystem::path tempDir(const std::string& name) {
    const auto d = std::filesystem::temp_directory_path() / "tmport_tests" / "soundfont" / name;
    std::filesystem::create_directories(d);
    return d;
}

// --- PCM analysis (interleaved stereo floats) ----------------------------------------------------

size_t frameAt(double seconds, int rate = kRate) { return static_cast<size_t>(std::llround(seconds * rate)); }

std::vector<float> renderSeconds(Sf2Synth& s, double seconds) {
    std::vector<float> pcm(frameAt(seconds, s.sampleRate()) * 2);
    s.render(pcm.data(), static_cast<int>(pcm.size() / 2));
    return pcm;
}

// Frequency of channel `ch` between two times, from the upward zero crossings (interpolated).
double frequency(const std::vector<float>& pcm, double t0, double t1, int ch = 0, int rate = kRate) {
    const size_t a = frameAt(t0, rate), b = std::min(frameAt(t1, rate), pcm.size() / 2);
    double first = -1, last = -1;
    int cycles = 0;
    for (size_t i = a + 1; i < b; ++i) {
        const float x0 = pcm[(i - 1) * 2 + ch], x1 = pcm[i * 2 + ch];
        if (x0 < 0.0f && x1 >= 0.0f) {
            const double pos = static_cast<double>(i - 1) + (-x0) / (x1 - x0);
            if (first < 0)
                first = pos;
            else
                ++cycles;
            last = pos;
        }
    }
    return cycles > 0 ? cycles * static_cast<double>(rate) / (last - first) : 0.0;
}

double rms(const std::vector<float>& pcm, double t0, double t1, int ch = -1, int rate = kRate) {
    const size_t a = frameAt(t0, rate), b = std::min(frameAt(t1, rate), pcm.size() / 2);
    double sum = 0;
    size_t n = 0;
    for (size_t i = a; i < b; ++i)
        for (int c = 0; c < 2; ++c)
            if (ch < 0 || ch == c) {
                sum += static_cast<double>(pcm[i * 2 + c]) * pcm[i * 2 + c];
                ++n;
            }
    return n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

float peak(const std::vector<float>& pcm, double t0, double t1, int rate = kRate) {
    const size_t a = frameAt(t0, rate) * 2, b = std::min(frameAt(t1, rate) * 2, pcm.size());
    float p = 0;
    for (size_t i = a; i < b; ++i) p = std::max(p, std::fabs(pcm[i]));
    return p;
}

// Magnitude of the left channel at frequency hz (Hann-windowed DFT bin).
double magnitudeAt(const std::vector<float>& pcm, double t0, double t1, double hz, int rate = kRate) {
    const size_t a = frameAt(t0, rate), b = std::min(frameAt(t1, rate), pcm.size() / 2);
    const double n = static_cast<double>(b - a), w = 2.0 * 3.14159265358979323846 * hz / rate;
    double re = 0, im = 0;
    for (size_t i = a; i < b; ++i) {
        const double k = static_cast<double>(i - a);
        const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * k / n);
        re += pcm[i * 2] * hann * std::cos(w * k);
        im += pcm[i * 2] * hann * std::sin(w * k);
    }
    return std::sqrt(re * re + im * im);
}

// Third harmonic relative to the fundamental: ~0 for the sine, ~1/3 for the square.
double harmonic3(const std::vector<float>& pcm, double t0, double t1, double hz) {
    return magnitudeAt(pcm, t0, t1, 3 * hz) / magnitudeAt(pcm, t0, t1, hz);
}
bool isSine(const std::vector<float>& pcm, double t0, double t1, double hz) { return harmonic3(pcm, t0, t1, hz) < 0.03; }
bool isSquare(const std::vector<float>& pcm, double t0, double t1, double hz) {
    const double h = harmonic3(pcm, t0, t1, hz);
    return h > 0.25 && h < 0.4;
}

// Times (s) where the sound starts after at least `gapMs` of silence.
std::vector<double> onsets(const std::vector<float>& pcm, int rate = kRate, float threshold = 1e-3f, double gapMs = 20) {
    std::vector<double> v;
    const size_t gap = static_cast<size_t>(gapMs * rate / 1000);
    size_t quiet = gap;  // the start counts as silence
    for (size_t i = 0; i < pcm.size() / 2; ++i) {
        const bool loud = std::fabs(pcm[i * 2]) > threshold || std::fabs(pcm[i * 2 + 1]) > threshold;
        if (loud) {
            if (quiet >= gap) v.push_back(static_cast<double>(i) / rate);
            quiet = 0;
        } else {
            ++quiet;
        }
    }
    return v;
}

double semitones(double hz, int semis) { return hz * std::pow(2.0, semis / 12.0); }

}  // namespace

// ------------------------------------------------------------------------------------ the SF2

TEST_CASE("SoundFont: the synthetic test SoundFont loads (presets, banks, files, errors)") {
    const auto sf2 = makeTestSoundFont();
    REQUIRE(sf2.size() > 8000);
    CHECK(std::string(sf2.begin(), sf2.begin() + 4) == "RIFF");
    CHECK(std::string(sf2.begin() + 8, sf2.begin() + 12) == "sfbk");

    auto s = testSynth();
    CHECK(s->sampleRate() == kRate);
    CHECK(s->presetCount() == 3);
    CHECK(s->presetName(0, testsf2::kSineProgram) == "Test Sine");
    CHECK(s->presetName(0, testsf2::kSquareProgram) == "Test Square");
    CHECK(s->presetName(testsf2::kDrumBank, 0) == "Test Drums");
    CHECK(s->presetName(0, 1).empty());
    // GM defaults after loading: program 0 everywhere, channel 10 on the drum kit.
    CHECK(s->channel(0).bank == 0);
    CHECK(s->channel(0).program == 0);
    CHECK(s->channel(0).volume == 100);
    CHECK(s->channel(0).expression == 127);
    CHECK(s->channel(9).bank == 128);
    CHECK(s->activeVoices() == 0);
    // Default headroom: one note (sample at half scale, velocity 100, GM volume 100 = (100/127)^2,
    // pan centre) at -10 dB.
    const double oneNote = 0.5 * (100 / 127.0) * std::pow(100 / 127.0, 2) * std::sqrt(0.5);
    s->send(0x90, 69, 100);
    auto one = renderSeconds(*s, 0.1);
    CHECK(peak(one, 0, 0.1) == doctest::Approx(oneNote * std::pow(10.0, -0.5)).epsilon(0.03));
    s->setGainDb(0.0f);
    s->send(0x90, 69, 0);
    s->send(0x90, 69, 100);  // the gain applies to notes started afterwards
    renderSeconds(*s, 0.1);  // (the old note's release)
    one = renderSeconds(*s, 0.1);
    CHECK(peak(one, 0, 0.1) == doctest::Approx(oneNote).epsilon(0.03));
    s->reset();

    // From a file (a non-ASCII name: the loader must not go through narrow fopen paths).
    const auto file = tempDir("load") / std::filesystem::path(u8"test_t\u00f6ne.sf2");
    REQUIRE(mischief::writeWholeFile(file, sf2));
    std::string error;
    CHECK(Sf2Synth::loadFile(file, 22050, error) != nullptr);

    CHECK(Sf2Synth::loadFile(tempDir("load") / "missing.sf2", kRate, error) == nullptr);
    CHECK(error.find("open") != std::string::npos);
    const std::vector<uint8_t> junk(100, 0x41);
    REQUIRE(mischief::writeWholeFile(tempDir("load") / "junk.sf2", junk));
    CHECK(Sf2Synth::loadFile(tempDir("load") / "junk.sf2", kRate, error) == nullptr);
    CHECK(error.find("SoundFont") != std::string::npos);
    CHECK(Sf2Synth::loadMemory(junk, kRate, error) == nullptr);
    auto truncated = sf2;
    truncated.resize(sf2.size() / 2);
    CHECK(Sf2Synth::loadMemory(truncated, kRate, error) == nullptr);
}

// ---------------------------------------------------------------------------- MIDI handling

TEST_CASE("SoundFont synth: note on plays the note's frequency; note off and all notes off silence it") {
    auto s = testSynth();
    auto silent = renderSeconds(*s, 0.05);
    CHECK(peak(silent, 0, 0.05) == 0.0f);

    s->send(0x90, 69, 100);  // A4
    auto pcm = renderSeconds(*s, 0.5);
    CHECK(rms(pcm, 0.05, 0.5) > 0.03);
    CHECK(frequency(pcm, 0.05, 0.5) == doctest::Approx(440.0).epsilon(0.003));
    CHECK(frequency(pcm, 0.05, 0.5, 1) == doctest::Approx(440.0).epsilon(0.003));  // both channels (pan centre)
    CHECK(rms(pcm, 0.05, 0.5, 0) == doctest::Approx(rms(pcm, 0.05, 0.5, 1)).epsilon(0.01));
    CHECK(s->activeVoices() == 1);

    s->send(0x80, 69, 0);
    pcm = renderSeconds(*s, 0.2);
    CHECK(rms(pcm, 0.0, 0.01) > 0.01);           // the release (50 ms) is still audible
    CHECK(peak(pcm, 0.07, 0.2) < 1e-5f);         // then silence
    CHECK(s->activeVoices() == 0);

    // Other keys follow 12-tone equal temperament from the root key.
    s->send(0x90, 60, 100);
    s->send(0x90, 60, 0);  // note on with velocity 0 is a note off
    pcm = renderSeconds(*s, 0.1);
    CHECK(peak(pcm, 0.07, 0.1) < 1e-5f);
    s->send(0x90, 60, 100);
    pcm = renderSeconds(*s, 0.4);
    CHECK(frequency(pcm, 0.05, 0.4) == doctest::Approx(semitones(440.0, -9)).epsilon(0.003));  // C4 261.6 Hz
    s->send(0x91, 81, 100);  // channel 2, A5, together with the C4
    pcm = renderSeconds(*s, 0.3);
    CHECK(s->activeVoices() == 2);
    // All notes off (CC 123) on each channel: notes end with their release.
    s->send(0xB0, 123, 0);
    s->send(0xB1, 123, 0);
    pcm = renderSeconds(*s, 0.2);
    CHECK(rms(pcm, 0.0, 0.01) > 0.01);
    CHECK(peak(pcm, 0.07, 0.2) < 1e-5f);
    CHECK(s->activeVoices() == 0);

    // All sound off (CC 120) ends the note at once (fast release), sooner than its own release.
    s->send(0x90, 69, 100);
    renderSeconds(*s, 0.1);
    s->send(0xB0, 120, 0);
    pcm = renderSeconds(*s, 0.1);
    CHECK(peak(pcm, 0.03, 0.1) < 1e-5f);
    // The base class's allNotesOff (stop / pause of the sequencer) is sustain off + CC 123.
    s->send(0x90, 69, 100);
    s->send(0xB0, 64, 127);
    renderSeconds(*s, 0.05);
    for (uint8_t ch = 0; ch < 16; ++ch) {
        s->send(static_cast<uint8_t>(0xB0 | ch), 64, 0);
        s->send(static_cast<uint8_t>(0xB0 | ch), 123, 0);
    }
    pcm = renderSeconds(*s, 0.2);
    CHECK(peak(pcm, 0.07, 0.2) < 1e-5f);
}

TEST_CASE("SoundFont synth: program change, percussion channel 10, pitch bend and its range") {
    auto s = testSynth();
    s->send(0x90, 69, 100);
    auto pcm = renderSeconds(*s, 0.3);
    CHECK(isSine(pcm, 0.05, 0.3, 440.0));
    s->send(0x80, 69, 0);
    renderSeconds(*s, 0.1);

    s->send(0xC0, testsf2::kSquareProgram, 0);
    CHECK(s->channel(0).program == testsf2::kSquareProgram);
    s->send(0x90, 69, 100);
    pcm = renderSeconds(*s, 0.3);
    CHECK(isSquare(pcm, 0.05, 0.3, 440.0));
    CHECK(frequency(pcm, 0.05, 0.3) == doctest::Approx(440.0).epsilon(0.003));
    s->send(0x80, 69, 0);
    renderSeconds(*s, 0.1);

    // A program the SoundFont lacks plays program 0 (not the channel's previous sound).
    s->send(0xC0, 5, 0);
    CHECK(s->channel(0).program == 0);

    // Channel 10: the drum kit (bank 128). Its tone has a fixed pitch (scale tuning 0) and is a
    // square: key 36 sounds at 440 Hz there, but at 65.4 Hz as a sine on a melodic channel.
    CHECK(s->channel(9).bank == testsf2::kDrumBank);
    s->send(0x99, 36, 100);
    pcm = renderSeconds(*s, 0.3);
    CHECK(frequency(pcm, 0.05, 0.3) == doctest::Approx(440.0).epsilon(0.003));
    CHECK(isSquare(pcm, 0.05, 0.3, 440.0));
    s->send(0x89, 36, 0);
    s->send(0xC9, 0, 0);  // a program change on channel 10 selects another kit; kit 0 stays drums
    CHECK(s->channel(9).bank == testsf2::kDrumBank);
    renderSeconds(*s, 0.1);
    s->send(0x90, 36, 100);
    pcm = renderSeconds(*s, 0.5);
    CHECK(frequency(pcm, 0.05, 0.5) == doctest::Approx(semitones(440.0, 36 - 69)).epsilon(0.005));
    s->send(0x80, 36, 0);
    renderSeconds(*s, 0.1);

    // Pitch bend: full up = +2 semitones (default range), centre = none; applies to a sounding note.
    s->send(0x90, 69, 100);
    s->send(0xE0, 0x7F, 0x7F);
    CHECK(s->channel(0).pitchWheel == 16383);
    pcm = renderSeconds(*s, 0.3);
    CHECK(frequency(pcm, 0.05, 0.3) == doctest::Approx(semitones(440.0, 2)).epsilon(0.003));
    s->send(0xE0, 0x00, 0x40);
    pcm = renderSeconds(*s, 0.3);
    CHECK(frequency(pcm, 0.05, 0.3) == doctest::Approx(440.0).epsilon(0.003));
    s->send(0xE0, 0x00, 0x00);  // full down
    pcm = renderSeconds(*s, 0.3);
    CHECK(frequency(pcm, 0.05, 0.3) == doctest::Approx(semitones(440.0, -2)).epsilon(0.003));
    // RPN 0 (pitch-bend sensitivity) = 12 semitones via data entry.
    s->send(0xB0, 101, 0);
    s->send(0xB0, 100, 0);
    s->send(0xB0, 6, 12);
    s->send(0xB0, 38, 0);
    s->send(0xE0, 0x7F, 0x7F);
    pcm = renderSeconds(*s, 0.3);
    CHECK(frequency(pcm, 0.05, 0.3) == doctest::Approx(880.0).epsilon(0.005));
    // Reset all controllers: bend centre (the range stays as set, like volume and program).
    s->send(0xB0, 121, 0);
    CHECK(s->channel(0).pitchWheel == 8192);
    pcm = renderSeconds(*s, 0.3);
    CHECK(frequency(pcm, 0.05, 0.3) == doctest::Approx(440.0).epsilon(0.003));
}

TEST_CASE("SoundFont synth: volume and expression (GM curve), pan, sustain, reset all controllers") {
    auto s = testSynth();
    auto level = [&](double t = 0.2) {
        auto pcm = renderSeconds(*s, t);
        return rms(pcm, 0.02, t);
    };
    s->send(0xB0, 7, 127);
    s->send(0x90, 69, 100);
    const double full = level();
    REQUIRE(full > 0.03);
    s->send(0xB0, 7, 64);  // applies to the sounding note
    CHECK(level() / full == doctest::Approx(std::pow(64 / 127.0, 2)).epsilon(0.03));
    s->send(0xB0, 7, 100);  // GM default
    CHECK(level() / full == doctest::Approx(std::pow(100 / 127.0, 2)).epsilon(0.03));
    s->send(0xB0, 7, 127);
    s->send(0xB0, 11, 64);  // expression multiplies
    CHECK(level() / full == doctest::Approx(std::pow(64 / 127.0, 2)).epsilon(0.03));
    s->send(0xB0, 7, 64);
    CHECK(level() / full == doctest::Approx(std::pow(64 / 127.0, 4)).epsilon(0.05));
    s->send(0xB0, 7, 0);
    CHECK(level() < 1e-4);
    // Velocity scales the note's level linearly.
    s->send(0xB0, 7, 127);
    s->send(0xB0, 11, 127);
    s->send(0x80, 69, 0);
    renderSeconds(*s, 0.1);
    s->send(0x90, 69, 50);
    CHECK(level() / full == doctest::Approx(50 / 100.0).epsilon(0.03));
    s->send(0x80, 69, 0);
    renderSeconds(*s, 0.1);

    // Pan: hard left / right.
    s->send(0xB0, 10, 0);
    s->send(0x90, 69, 100);
    auto pcm = renderSeconds(*s, 0.2);
    CHECK(rms(pcm, 0.02, 0.2, 0) > 0.03);
    CHECK(rms(pcm, 0.02, 0.2, 1) < 1e-6);
    s->send(0xB0, 10, 127);
    pcm = renderSeconds(*s, 0.2);
    CHECK(rms(pcm, 0.02, 0.2, 0) < 0.15 * rms(pcm, 0.02, 0.2, 1));
    s->send(0xB0, 10, 64);
    pcm = renderSeconds(*s, 0.2);
    CHECK(rms(pcm, 0.02, 0.2, 0) == doctest::Approx(rms(pcm, 0.02, 0.2, 1)).epsilon(0.02));
    s->send(0x80, 69, 0);
    renderSeconds(*s, 0.1);

    // Sustain pedal: a released key keeps sounding until the pedal is lifted.
    s->send(0xB0, 64, 127);
    s->send(0x90, 69, 100);
    renderSeconds(*s, 0.05);
    s->send(0x80, 69, 0);
    pcm = renderSeconds(*s, 0.3);
    CHECK(rms(pcm, 0.2, 0.3) > 0.03);
    s->send(0xB0, 64, 0);
    pcm = renderSeconds(*s, 0.2);
    CHECK(peak(pcm, 0.07, 0.2) < 1e-5f);

    // Reset all controllers (RP-015): expression and sustain reset, volume stays.
    s->send(0xB0, 7, 64);
    s->send(0xB0, 11, 30);
    s->send(0xB0, 64, 127);
    s->send(0xB0, 121, 0);
    CHECK(s->channel(0).volume == 64);
    CHECK(s->channel(0).expression == 127);
    s->send(0x90, 69, 100);
    renderSeconds(*s, 0.05);
    s->send(0x80, 69, 0);  // sustain was reset: the note ends
    pcm = renderSeconds(*s, 0.2);
    CHECK(peak(pcm, 0.07, 0.2) < 1e-5f);

    // GM reset.
    s->send(0xC0, testsf2::kSquareProgram, 0);
    s->reset();
    CHECK(s->channel(0).program == 0);
    CHECK(s->channel(0).volume == 100);
}

// ------------------------------------------------------------------------- back end timing

TEST_CASE("SoundFont back end: messages land on their own sample; later ones wait in the queue") {
    FakeClock clock;
    SoundFontMidiBackend::Config c;
    c.synth = testSynth();
    c.output = SoundFontMidiBackend::Output::Offline;
    c.clock = clock.fn();
    SoundFontMidiBackend b(std::move(c));
    CHECK(b.name() == "soundfont");
    CHECK_FALSE(b.isOpen());
    b.send(0x90, 69, 100);  // before open(): ignored
    REQUIRE(b.open());
    CHECK(b.isOpen());
    CHECK(b.queuedMessages() == 0);

    clock.us = 10'000;
    b.send(0x90, 69, 100);  // at 10 ms
    clock.us = 150'000;
    b.send(0x80, 69, 0);    // at 150 ms: beyond the first block
    std::vector<float> pcm(frameAt(0.3) * 2);
    b.renderAt(pcm.data(), static_cast<int>(frameAt(0.1)), 0);
    CHECK(b.queuedMessages() == 1);
    b.renderAt(pcm.data() + frameAt(0.1) * 2, static_cast<int>(frameAt(0.2)), 100'000);
    CHECK(b.queuedMessages() == 0);
    const auto on = onsets(pcm);
    REQUIRE(on.size() == 1);
    CHECK(std::fabs(on[0] - 0.010) < 3.0 / kRate);  // on its sample (+1 for the sine's zero start)
    CHECK(frequency(pcm, 0.02, 0.14) == doctest::Approx(440.0).epsilon(0.003));
    CHECK(peak(pcm, 0.15 + 0.06, 0.3) < 1e-5f);  // note off at 150 ms + 50 ms release
    CHECK(b.framesRendered() == frameAt(0.3));
    CHECK(b.peakSinceOpen() > 0.05f);

    // The music volume scales the output.
    clock.us = 400'000;
    b.send(0x90, 69, 100);
    std::vector<float> loud(frameAt(0.2) * 2), soft(frameAt(0.2) * 2);
    b.renderAt(loud.data(), static_cast<int>(frameAt(0.2)), 400'000);
    b.setVolume(0.25f);
    CHECK(b.volume() == 0.25f);
    b.renderAt(soft.data(), static_cast<int>(frameAt(0.2)), 600'000);
    CHECK(rms(soft, 0, 0.2) / rms(loud, 0.05, 0.2) == doctest::Approx(0.25).epsilon(0.02));

    // close() silences at once and drops what is queued; send() after close is ignored.
    b.send(0x90, 72, 100);
    b.close();
    CHECK_FALSE(b.isOpen());
    CHECK(b.queuedMessages() == 0);
    CHECK(b.synth()->activeVoices() == 0);
}

TEST_CASE("SoundFont back end: the real-time stream keeps a constant latency, not block jitter") {
    FakeClock clock;
    SoundFontMidiBackend::Config c;
    c.synth = testSynth();
    c.output = SoundFontMidiBackend::Output::Offline;  // renderRealtime() is driven by hand here
    c.clock = clock.fn();
    c.latencyUs = 50'000;
    SoundFontMidiBackend b(std::move(c));
    REQUIRE(b.open());

    // Device callbacks for 1024 frames (~23 ms) at irregular moments; notes sent in between.
    const int block = 1024;
    const uint64_t blockUs = block * 1'000'000ull / kRate;
    std::vector<float> pcm;
    std::vector<float> buf(block * 2);
    const int64_t jitter[] = {0, 3000, -2500, 1200, -3000, 2000, 0, -1000};
    auto callbackAt = [&](int i) {
        return 1'000'000 + static_cast<uint64_t>(i) * blockUs + static_cast<uint64_t>(jitter[i % 8] + 4000);
    };
    const uint64_t streamStartUs = callbackAt(0) - 50'000;  // the first block starts at now - latency
    const uint64_t noteUs[] = {1'013'000, 1'131'000, 1'247'000};
    size_t next = 0;
    for (int i = 0; i < 24; ++i) {
        const uint64_t callbackUs = callbackAt(i);
        // The sequencer sends notes (and their note-offs 30 ms later) up to the callback time.
        while (next < 3 && noteUs[next] <= callbackUs) {
            clock.us = noteUs[next];
            b.send(0x90, 69, 100);
            clock.us = noteUs[next] + 30'000;
            b.send(0x80, 69, 0);
            ++next;
        }
        clock.us = callbackUs;
        b.renderRealtime(buf.data(), block);
        pcm.insert(pcm.end(), buf.begin(), buf.end());
    }
    const auto on = onsets(pcm);
    REQUIRE(on.size() == 3);
    for (size_t k = 0; k < 3; ++k) {
        const double expected = static_cast<double>(noteUs[k] - streamStartUs) / 1e6;
        CHECK(std::fabs(on[k] - expected) < 3.0 / kRate);  // sample accurate, whatever the block timing
    }
}

TEST_CASE("SoundFont: tempo changes on a simulated clock (offline render of a synthetic song)") {
    // 480 PPQN. 120 BPM: notes at 0 and 0.5 s (250 ms each); at tick 960 (1.0 s) the tempo halves
    // to 60 BPM: notes at 1.0 s and 2.0 s (500 ms each); the song ends at tick 1920 = 3.0 s.
    TrackWriter t;
    t.tempo(0, 500000).ev(0, {0xC0, 0}).ev(0, {0xB0, 7, 127});
    t.ev(0, {0x90, 60, 100}).ev(240, {0x80, 60, 0});
    t.ev(240, {0x90, 64, 100}).ev(240, {0x80, 64, 0});
    t.tempo(240, 1000000);
    t.ev(0, {0x90, 67, 100}).ev(240, {0x80, 67, 0});
    t.ev(240, {0x90, 69, 100}).ev(240, {0x80, 69, 0});
    t.end(240);
    auto song = std::make_shared<const MidiSong>(parseSmf(audiotest::smfFile(0, 480, {t})));
    REQUIRE(song->lengthUs == 3'000'000);

    MidiRenderOptions o;
    o.tailSeconds = 0.5;
    const MidiRenderResult r = renderMidiSong(song, testSynth(), o);
    CHECK(r.sampleRate == kRate);
    CHECK(r.frames == frameAt(3.5));
    CHECK(r.pcm.size() == r.frames * 2);
    CHECK(r.plays == 1);
    CHECK(r.songEndUs == 3'000'000);
    CHECK(r.messages == 10);  // program, volume, 4 note-ons, 4 note-offs (meta events are not sent)
    const auto on = onsets(r.pcm);
    REQUIRE(on.size() == 4);
    const double expected[] = {0.0, 0.5, 1.0, 2.0};
    for (size_t k = 0; k < 4; ++k) CHECK(std::fabs(on[k] - expected[k]) < 0.0005);
    // Each note at its pitch, each ending at its note-off (+ the 50 ms release).
    CHECK(frequency(r.pcm, 0.02, 0.24) == doctest::Approx(semitones(440, 60 - 69)).epsilon(0.004));
    CHECK(frequency(r.pcm, 0.52, 0.74) == doctest::Approx(semitones(440, 64 - 69)).epsilon(0.004));
    CHECK(frequency(r.pcm, 1.02, 1.49) == doctest::Approx(semitones(440, 67 - 69)).epsilon(0.004));
    CHECK(frequency(r.pcm, 2.02, 2.49) == doctest::Approx(440.0).epsilon(0.004));
    CHECK(peak(r.pcm, 0.25 + 0.07, 0.5) < 1e-5f);
    CHECK(peak(r.pcm, 1.5 + 0.07, 2.0) < 1e-5f);
    CHECK(peak(r.pcm, 2.5 + 0.07, 3.5) < 1e-5f);
    const PcmStats st = analysePcm(r.pcm);
    CHECK(st.frames == r.frames);
    CHECK(st.clipped == 0);
    CHECK(st.firstSound <= 1);  // the sine starts at 0
    CHECK(static_cast<double>(st.lastSound) / kRate == doctest::Approx(2.55).epsilon(0.01));

    // --seconds: exactly that long, here cutting the song; looping restarts it at its end.
    o.seconds = 1.25;
    CHECK(renderMidiSong(song, testSynth(), o).frames == frameAt(1.25));
    o.seconds = 7.0;
    o.loopFromUs = 0;
    const MidiRenderResult looped = renderMidiSong(song, testSynth(), o);
    CHECK(looped.plays == 2);
    const auto lon = onsets(looped.pcm);
    REQUIRE(lon.size() >= 6);
    CHECK(std::fabs(lon[4] - 3.0) < 0.0005);
    CHECK(std::fabs(lon[5] - 3.5) < 0.0005);

    // The WAV writer: 16-bit stereo PCM.
    const auto wav = makeWavStereo16(r.pcm, r.sampleRate);
    REQUIRE(wav.size() == 44 + r.frames * 4);
    CHECK(std::string(wav.begin(), wav.begin() + 4) == "RIFF");
    CHECK((wav[22] | wav[23] << 8) == 2);
    CHECK((wav[24] | wav[25] << 8 | wav[26] << 16 | wav[27] << 24) == kRate);
    CHECK((wav[34] | wav[35] << 8) == 16);
}

TEST_CASE("SoundFont: stop, pause and resume through the 1090 driver rules (chase restores the program)") {
    // Tune 9000 (loops from the start): program 0, then A4 every 250 ms for 150 ms; 2 s long.
    TrackWriter t;
    t.tempo(0, 500000).ev(0, {0xC0, 0}).ev(0, {0xB0, 7, 127});
    uint32_t last = 0;
    for (uint32_t n = 0; n < 8; ++n) {
        const uint32_t on = n * 240, off = on + 144;
        t.ev(on - last, {0x90, 69, 100}).ev(off - on, {0x80, 69, 0});
        last = off;
    }
    t.end(1920 - last);
    auto song = std::make_shared<const MidiSong>(parseSmf(audiotest::smfFile(0, 480, {t})));

    FakeClock clock;
    SoundFontMidiBackend::Config c;
    c.synth = testSynth();
    c.output = SoundFontMidiBackend::Output::Offline;
    c.clock = clock.fn();
    auto backend = std::make_unique<SoundFontMidiBackend>(std::move(c));
    SoundFontMidiBackend* sf = backend.get();
    MidiPlayer player(std::move(backend), clock.fn(), false);
    REQUIRE(player.ok());
    CHECK(player.backendName() == "soundfont");
    const AudioTables tables = audiotest::makeTables();
    audiotest::MapTrackLoader loader;
    loader.songs["MUSIC\\Et0.mid"] = song;
    MidiDriver driver(tables, player, loader);

    // Advances the clock in 5 ms blocks, servicing the driver like the engine pump, rendering.
    std::vector<float> pcm;
    auto run = [&](double seconds) {
        const int n = static_cast<int>(frameAt(0.005));
        std::vector<float> buf(static_cast<size_t>(n) * 2);
        for (int i = 0; i < static_cast<int>(std::lround(seconds / 0.005)); ++i) {
            const uint64_t start = static_cast<uint64_t>(pcm.size() / 2) * 1'000'000 / kRate;
            clock.us = start;
            player.service();
            driver.service();
            player.service();
            sf->renderAt(buf.data(), n, start);
            pcm.insert(pcm.end(), buf.begin(), buf.end());
        }
    };

    driver.playTrack(9000);
    CHECK(driver.isPlaying());
    run(0.6);
    CHECK(frequency(pcm, 0.01, 0.14) == doctest::Approx(440.0).epsilon(0.004));
    CHECK(isSine(pcm, 0.01, 0.14, 440.0));
    CHECK(onsets(pcm).size() == 3);  // 0, 0.25, 0.5 s

    // Pause mid-note (0.6 s): silence after the release; the position is kept.
    driver.pause();
    CHECK(driver.paused());
    CHECK(driver.savedPosMs() == doctest::Approx(600).epsilon(0.02));
    run(0.4);
    CHECK(peak(pcm, 0.6 + 0.07, 1.0) < 1e-5f);
    // Meanwhile something else changes the channel's program (another tune would).
    sf->synth()->send(0xC0, testsf2::kSquareProgram, 0);
    // Resume at the saved position: the chased program change restores the sine; the next
    // note (song time 0.75 s) comes 150 ms after the resume.
    driver.resume();
    CHECK_FALSE(driver.paused());
    const double resumedAt = static_cast<double>(pcm.size() / 2) / kRate;
    run(0.5);
    std::vector<float> tail(pcm.begin() + static_cast<std::ptrdiff_t>(frameAt(resumedAt) * 2), pcm.end());
    const auto on = onsets(tail);
    REQUIRE(on.size() >= 1);
    CHECK(on[0] == doctest::Approx(0.15).epsilon(0.05));
    CHECK(isSine(tail, on[0] + 0.01, on[0] + 0.14, 440.0));  // the chased program 0, not the square
    CHECK(frequency(tail, on[0] + 0.01, on[0] + 0.14) == doctest::Approx(440.0).epsilon(0.004));

    // Stop: silence; then a full play to the end loops from the start (loopPos 0).
    driver.stop();
    CHECK_FALSE(driver.isPlaying());
    run(0.3);
    CHECK(peak(pcm, static_cast<double>(pcm.size() / 2) / kRate - 0.2, static_cast<double>(pcm.size() / 2) / kRate) < 1e-5f);
    driver.playTrack(9000);
    const double startedAt = static_cast<double>(pcm.size() / 2) / kRate;
    run(2.6);
    std::vector<float> loop(pcm.begin() + static_cast<std::ptrdiff_t>(frameAt(startedAt) * 2), pcm.end());
    const auto lon = onsets(loop);
    REQUIRE(lon.size() >= 10);  // 8 notes, then the loop restarts at 2.0 s
    CHECK(lon[8] == doctest::Approx(2.0).epsilon(0.01));
    CHECK(driver.isPlaying());
}

// ---------------------------------------------------------------------- configuration / CLI

TEST_CASE("SoundFont: back end selection and tmport.ini keys") {
#if defined(_WIN32)
    CHECK(resolveMidiBackendKind(MidiBackendKind::Auto, true) == MidiBackendKind::WinMM);
    CHECK(resolveMidiBackendKind(MidiBackendKind::Auto, false) == MidiBackendKind::WinMM);
#else
    CHECK(resolveMidiBackendKind(MidiBackendKind::Auto, true) == MidiBackendKind::SoundFont);
    CHECK(resolveMidiBackendKind(MidiBackendKind::Auto, false) == MidiBackendKind::Null);
#endif
    CHECK(resolveMidiBackendKind(MidiBackendKind::SoundFont, false) == MidiBackendKind::SoundFont);
    CHECK(resolveMidiBackendKind(MidiBackendKind::Null, true) == MidiBackendKind::Null);

    // A SoundFont back end without a SoundFont cannot open (music silent, midiOk = 0); no device.
    MidiBackendConfig mc;
    mc.kind = MidiBackendKind::SoundFont;
    auto b = createMidiBackend(mc);
    REQUIRE(b != nullptr);
    CHECK(b->name() == "soundfont");
    CHECK_FALSE(b->open());
    mc.kind = MidiBackendKind::Null;
    CHECK(createMidiBackend(mc)->name() == "null");

    const save::EngineSettings d = save::EngineSettings::fromIni(mischief::save::IniFile{});
    CHECK(d.soundFont.empty());
    CHECK(d.musicVolume == 100);
    const save::EngineSettings s = save::EngineSettings::fromIni(mischief::save::IniFile::parse(
        "[audio]\nmidi_backend = winmm\nmidi = SoundFont\nsoundfont = C:\\sf2\\GeneralUser GS.sf2\nmusic_volume = 40\n"));
    CHECK(s.midiBackend == save::MidiBackend::SoundFont);  // "midi" wins over "midi_backend"
    CHECK(s.soundFont == "C:\\sf2\\GeneralUser GS.sf2");
    CHECK(s.musicVolume == 40);
    CHECK(save::EngineSettings::fromIni(mischief::save::IniFile::parse("[audio]\nmidi_backend = sf2\n")).midiBackend == save::MidiBackend::SoundFont);
    CHECK(save::EngineSettings::fromIni(mischief::save::IniFile::parse("[audio]\nmusic_volume = 101\n")).musicVolume == 100);  // bad: default
    mischief::save::IniFile ini = mischief::save::IniFile::parse("[audio]\nmidi = soundfont\n");
    save::EngineSettings w = s;
    w.midiBackend = save::MidiBackend::Null;
    w.toIni(ini);
    CHECK(ini.get("audio", "midi") == "null");  // the alias is kept in step
    CHECK(ini.get("audio", "midi_backend") == "null");
    CHECK(save::EngineSettings::fromIni(mischief::save::IniFile::parse(ini.text())) == w);
}

TEST_CASE("SoundFont: the back end on SDL's silent dummy driver, sharing the wave voice's device") {
    SdlWaveOutput out("dummy");  // never a real device
    if (!out.open()) {
        MESSAGE("SDL dummy audio driver unavailable: test skipped");
        return;
    }
    REQUIRE(out.deviceId() != 0);
    SoundFontMidiBackend::Config c;
    c.synth = testSynth();
    c.output = SoundFontMidiBackend::Output::Device;
    c.device = out.deviceId();
    c.volume = 0.5f;
    SoundFontMidiBackend b(std::move(c));
    REQUIRE(b.open());
    CHECK(b.device() == out.deviceId());
    b.send(0xC0, testsf2::kSquareProgram, 0);
    b.send(0x90, 69, 100);
    const uint64_t t0 = SDL_GetTicks();
    while (b.framesRendered() < frameAt(0.3) && SDL_GetTicks() - t0 < 5000) SDL_Delay(5);
    CHECK(b.framesRendered() >= frameAt(0.3));  // SDL pulls the music stream in real time
    CHECK(b.queuedMessages() == 0);
    CHECK(b.peakSinceOpen() > 0.02f);
    CHECK(b.synth()->activeVoices() == 1);
    CHECK(b.synth()->channel(0).program == testsf2::kSquareProgram);

    // The wave voice still plays on the same device, mixed with the music.
    WaveVoice voice(out);
    auto snd = std::make_shared<Sound>();
    snd->id = 1;
    snd->sampleRate = 22050;
    snd->pcm.assign(22050 / 5, 0x80);
    voice.play(snd);
    const uint64_t t1 = SDL_GetTicks();
    while (voice.isPlaying() && SDL_GetTicks() - t1 < 3000) SDL_Delay(2);
    CHECK_FALSE(voice.isPlaying());
    CHECK(SDL_GetTicks() - t1 >= 100);

    b.send(0x80, 69, 0);
    b.close();
    CHECK_FALSE(b.isOpen());
    CHECK(b.device() == 0);
    CHECK(out.available());  // the wave voice's device is still open
    out.close();
}

TEST_CASE("SoundFont: AudioSystem::create with the SoundFont back end (dummy SDL driver, user's tunes)") {
    TM_REQUIRE_GAME_DATA();
    const auto sf2 = tempDir("system") / "test.sf2";
    REQUIRE(mischief::writeWholeFile(sf2, makeTestSoundFont()));
    AudioConfig cfg;
    cfg.sdlAudioDriver = "dummy";
    cfg.midiBackend = MidiBackendKind::SoundFont;
    cfg.soundFontPath = mischief::pathToUtf8(sf2);
    cfg.musicVolume = 0.8f;
    auto a = AudioSystem::create(*g, cfg);
    REQUIRE(a != nullptr);
    if (!a->voice().available()) {
        MESSAGE("SDL dummy audio driver unavailable: test skipped");
        return;
    }
    CHECK(a->midiPlayer().backendName() == "soundfont");
    REQUIRE(a->midi().midiOk());
    a->music().playZone(zone::kLevel0);
    CHECK(a->music().isZonePlaying(zone::kLevel0));
    uint64_t frames = 0;
    float vol = 0;
    const uint64_t t0 = SDL_GetTicks();
    while (frames < 44100 / 4 && SDL_GetTicks() - t0 < 5000) {
        SDL_Delay(10);
        a->service();
        a->midiPlayer().withBackend([&](MidiBackend& m) {
            auto* sf = dynamic_cast<SoundFontMidiBackend*>(&m);
            REQUIRE(sf != nullptr);
            frames = sf->framesRendered();
            vol = sf->volume();
        });
    }
    CHECK(frames >= 44100 / 4);
    CHECK(vol == doctest::Approx(0.8f));
    CHECK(a->midiPlayer().positionUs() > 0);
    a->pauseNested();
    CHECK_FALSE(a->midiPlayer().playing());
    a->resumeNested();
    CHECK(a->midiPlayer().playing());
    a->music().stopScene();

    // A missing SoundFont: the back end does not open, music reports not playing, nothing crashes.
    cfg.soundFontPath = mischief::pathToUtf8(tempDir("system") / "nope.sf2");
    auto b = AudioSystem::create(*g, cfg);
    CHECK_FALSE(b->midi().midiOk());
    b->music().playZone(zone::kLevel0);
    CHECK_FALSE(b->music().isZonePlaying(zone::kLevel0));
}

TEST_CASE("SoundFont: --midi-render of SOUNDS\\EOPEN1.MID with the synthetic SoundFont") {
    TM_REQUIRE_GAME_DATA();
    const auto data = testdata::dataPath();
    const auto dir = tempDir("render");
    const std::string sf2 = mischief::pathToUtf8(dir / "test.sf2");
    const std::string wav = mischief::pathToUtf8(dir / "eopen1.wav");
    {
        std::vector<std::string> args = {"tmport", "--make-test-soundfont", sf2};
        std::vector<char*> argv;
        for (auto& s : args) argv.push_back(s.data());
        int rc = -1;
        REQUIRE(runAudioCli(static_cast<int>(argv.size()), argv.data(), rc));
        REQUIRE(rc == 0);
    }
    auto runRender = [&](std::vector<std::string> extra) {
        std::vector<std::string> args = {"tmport", "--midi-render", "SOUNDS\\EOPEN1.MID", wav, "--soundfont", sf2, "--data", *data};
        args.insert(args.end(), extra.begin(), extra.end());
        std::vector<char*> argv;
        for (auto& s : args) argv.push_back(s.data());
        int rc = -1;
        REQUIRE(runAudioCli(static_cast<int>(argv.size()), argv.data(), rc));
        return rc;
    };
    REQUIRE(runRender({}) == 0);

    auto f = g->source->open("SOUNDS\\EOPEN1.MID");
    REQUIRE(f);
    const MidiSong song = parseSmf(f->readAll());
    REQUIRE(song.lengthUs > 5'000'000);  // the intro tune lasts about 8 s
    const uint64_t expectedFrames = (song.lengthUs * 44100 + 999'999) / 1'000'000 + 2 * 44100;  // + 2 s tail

    const auto bytes = mischief::readWholeFile(mischief::pathFromUtf8(wav));
    REQUIRE(bytes);
    REQUIRE(bytes->size() > 44);
    auto u16 = [&](size_t o) { return static_cast<uint32_t>((*bytes)[o] | (*bytes)[o + 1] << 8); };
    auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
    CHECK(u16(22) == 2);
    CHECK(u32(24) == 44100);
    CHECK(u16(34) == 16);
    const uint32_t dataBytes = u32(40);
    CHECK(dataBytes == bytes->size() - 44);
    CHECK(dataBytes / 4 == expectedFrames);
    // Not silent, not clipped: decode the samples and look at the levels.
    std::vector<float> pcm(dataBytes / 2);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(u16(44 + i * 2)) / 32768.0f;
    const PcmStats st = analysePcm(pcm);
    CHECK(st.peak > 0.05f);
    CHECK(st.peak < 1.0f);  // the synthesizer's headroom: no clipping
    CHECK(st.clipped == 0);
    CHECK(st.rms > 0.005);
    CHECK(st.firstSound >= 0);
    CHECK(static_cast<double>(st.firstSound) / 44100 < 1.0);
    // The last sound is near the song's last note, and the 2 s tail ends in silence.
    CHECK(static_cast<double>(st.lastSound) / 44100 <= song.lengthUs / 1e6 + 0.5);
    CHECK(static_cast<double>(st.lastSound) / 44100 >= song.tickToUs(song.lastNoteOnTick()) / 1e6);

    // --seconds sets the exact length.
    REQUIRE(runRender({"--seconds", "5"}) == 0);
    const auto shortWav = mischief::readWholeFile(mischief::pathFromUtf8(wav));
    REQUIRE(shortWav);
    CHECK(shortWav->size() == 44 + 5 * 44100 * 4);
    // Bad arguments.
    CHECK(runRender({"--seconds", "-1"}) == 2);
}
