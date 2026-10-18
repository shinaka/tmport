#include "audio/midi_render.h"

#include <algorithm>
#include <cmath>

#include "audio/midi_backend_soundfont.h"
#include "audio/midi_sequencer.h"
#include "util/error.h"

namespace tmport::audio {

MidiRenderResult renderMidiSong(const std::shared_ptr<const MidiSong>& song, const std::shared_ptr<Sf2Synth>& synth,
                                const MidiRenderOptions& o) {
    if (!synth) throw Error("renderMidiSong: no synthesizer");
    MidiRenderResult r;
    r.sampleRate = synth->sampleRate();
    const uint64_t rate = static_cast<uint64_t>(r.sampleRate);

    uint64_t now = 0;  // the simulated clock (us)
    SoundFontMidiBackend::Config c;
    c.synth = synth;
    c.output = SoundFontMidiBackend::Output::Offline;
    c.clock = [&now] { return now; };
    c.volume = o.volume;
    SoundFontMidiBackend backend(std::move(c));
    if (!backend.open()) throw Error("renderMidiSong: the synthesizer could not be opened");
    MidiSequencer seq(backend);
    seq.setSong(song);
    if (song) seq.start(0, 0);

    const uint64_t songFrames = song ? (song->lengthUs * rate + 999'999) / 1'000'000 : 0;
    r.frames = o.seconds > 0 ? static_cast<uint64_t>(std::llround(o.seconds * static_cast<double>(rate)))
                             : songFrames + static_cast<uint64_t>(std::llround(std::max(o.tailSeconds, 0.0) * static_cast<double>(rate)));
    r.pcm.assign(static_cast<size_t>(r.frames) * 2, 0.0f);
    const bool loop = o.loopFromUs >= 0 && song && static_cast<uint64_t>(o.loopFromUs) < song->lengthUs;
    const int block = std::max(o.blockFrames, 1);

    for (uint64_t f = 0; f < r.frames;) {
        const int n = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(block), r.frames - f));
        const uint64_t startUs = f * 1'000'000 / rate;
        const uint64_t endUs = (f + static_cast<uint64_t>(n)) * 1'000'000 / rate;
        // Step the clock to every event due before the end of this block, at its own time.
        while (seq.playing()) {
            const uint64_t wake = seq.nextWakeUs();
            if (wake >= endUs) break;
            now = std::max(now, wake);
            if (seq.advance(now)) {
                ++r.plays;
                if (r.songEndUs == 0) r.songEndUs = now;
                if (loop) seq.start(now, static_cast<uint64_t>(o.loopFromUs));
            }
        }
        backend.renderAt(r.pcm.data() + static_cast<size_t>(f) * 2, n, startUs);
        r.maxVoices = std::max(r.maxVoices, synth->activeVoices());
        f += static_cast<uint64_t>(n);
    }
    r.messages = backend.messageCount();
    return r;
}

PcmStats analysePcm(std::span<const float> pcm, float silence) {
    PcmStats s;
    s.frames = pcm.size() / 2;
    double sum = 0.0;
    for (size_t i = 0; i < pcm.size(); ++i) {
        const float a = std::fabs(pcm[i]);
        s.peak = std::max(s.peak, a);
        sum += static_cast<double>(pcm[i]) * pcm[i];
        if (a > 1.0f) ++s.clipped;
        if (a > silence) {
            const int64_t frame = static_cast<int64_t>(i / 2);
            if (s.firstSound < 0) s.firstSound = frame;
            s.lastSound = frame;
        }
    }
    s.rms = pcm.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(pcm.size()));
    return s;
}

std::vector<uint8_t> makeWavStereo16(std::span<const float> pcm, int sampleRate) {
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2);
    std::vector<uint8_t> o;
    o.reserve(44 + dataBytes);
    auto id = [&](const char* s) { o.insert(o.end(), s, s + 4); };
    auto u16 = [&](uint16_t v) {
        o.push_back(static_cast<uint8_t>(v));
        o.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto u32 = [&](uint32_t v) {
        u16(static_cast<uint16_t>(v));
        u16(static_cast<uint16_t>(v >> 16));
    };
    const uint32_t rate = static_cast<uint32_t>(sampleRate);
    id("RIFF");
    u32(36 + dataBytes);
    id("WAVE");
    id("fmt ");
    u32(16);
    u16(1);             // PCM
    u16(2);             // stereo
    u32(rate);
    u32(rate * 4);      // bytes per second
    u16(4);             // block align
    u16(16);            // bits per sample
    id("data");
    u32(dataBytes);
    for (float v : pcm) {
        const float c = std::clamp(v, -1.0f, 1.0f);
        u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(c * 32767.0f))));
    }
    return o;
}

}  // namespace tmport::audio
