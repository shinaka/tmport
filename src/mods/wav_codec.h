#pragma once
// WAV <-> SSND (re/docs/fmt_sound.md §3.2: u32 0, u32 sample rate, u32 sample count, 8-bit
// unsigned mono PCM).
//
// Import accepts RIFF/WAVE PCM with 8, 16, 24 or 32-bit integer samples or 32-bit float, any
// number of channels (mixed down to mono) and any rate (resampled to the target rate: linear
// interpolation up, area averaging down). 8-bit mono at the target rate is copied unchanged, so
// export -> import is byte-exact.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tmport::mods {

struct WavFormat {
    int formatTag = 1;     // 1 PCM, 3 IEEE float (0xFFFE extensible is reported as its subformat)
    int channels = 1;
    uint32_t sampleRate = 0;
    int bitsPerSample = 8;
    size_t frames = 0;
};

// Parses the header. Throws FormatError with a clear message for unsupported files.
WavFormat parseWavFormat(std::span<const uint8_t> wav);

// A WAV file -> SSND body (header + PCM, zero-padded to the NE size). note (optional) receives a
// short description of the conversion done ("44100 Hz 16-bit stereo -> 22050 Hz 8-bit mono").
std::vector<uint8_t> wavToSsnd(std::span<const uint8_t> wav, uint32_t targetRate, std::string* note = nullptr);

// SSND body -> WAV file (8-bit mono at the SSND's rate). Throws FormatError for a short body.
std::vector<uint8_t> ssndToWav(std::span<const uint8_t> ssnd);

// A PCM WAV file from interleaved samples (8-bit unsigned or 16-bit signed little-endian bytes).
std::vector<uint8_t> makeWav(uint32_t rate, int channels, int bitsPerSample, std::span<const uint8_t> sampleBytes);

}  // namespace tmport::mods
