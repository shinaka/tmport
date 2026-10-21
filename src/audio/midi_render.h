#pragma once
// Offline MIDI rendering (nothing is played, no device is opened): a song runs through our
// MidiSequencer on a simulated clock into the SoundFont back end in offline mode. The clock steps
// to each event's own time, so every message lands on its exact sample; the result is what the
// game's music sounds like on that SoundFont, as PCM. Used by `tmport --midi-render` and the tests.

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "audio/sf2_synth.h"
#include "audio/smf.h"

namespace tmport::audio {

struct MidiRenderOptions {
    double seconds = 0.0;       // output length; 0 = the song once plus tailSeconds
    double tailSeconds = 2.0;   // silence-or-release after the song when seconds == 0
    int64_t loopFromUs = -1;    // >= 0: when the song ends, restart it from there (looping tunes)
    float volume = 1.0f;        // music volume factor
    int blockFrames = 256;      // render granularity (does not affect timing)
};

struct MidiRenderResult {
    std::vector<float> pcm;     // interleaved stereo floats
    int sampleRate = 0;
    uint64_t frames = 0;
    uint64_t messages = 0;      // channel messages the sequencer sent to the synthesizer
    uint64_t songEndUs = 0;     // when the first play-through ended (0: not within the output)
    int plays = 0;              // completed play-throughs
    int maxVoices = 0;          // most synthesizer voices sounding at a block boundary
};

// Renders `song` on `synth` (which is reset first). Throws mischief::Error if the synth is missing.
MidiRenderResult renderMidiSong(const std::shared_ptr<const MidiSong>& song, const std::shared_ptr<Sf2Synth>& synth,
                                const MidiRenderOptions& options);

struct PcmStats {
    uint64_t frames = 0;
    float peak = 0.0f;           // largest |sample|, 1.0 = full scale
    double rms = 0.0;            // over both channels and the whole output
    uint64_t clipped = 0;        // samples beyond full scale (clamped in the WAV)
    int64_t firstSound = -1;     // first / last frame louder than `silence` (-1: all silent)
    int64_t lastSound = -1;
};
PcmStats analysePcm(std::span<const float> interleavedStereo, float silence = 1e-4f);

// A 16-bit PCM stereo WAV file of interleaved float samples (clamped to full scale).
std::vector<uint8_t> makeWavStereo16(std::span<const float> interleavedStereo, int sampleRate);

}  // namespace tmport::audio
