#pragma once
// WAV <-> SSND (re/docs/fmt_sound.md §3.2: u32 0, u32 sample rate, u32 sample count, 8-bit
// unsigned mono PCM).
//
// Import accepts what mods/wav_file.h reads (any PCM or float WAV, mixed down to mono and resampled
// to the target rate). 8-bit mono at the target rate is copied unchanged, so export -> import is
// byte-exact.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mods/wav_file.h"

namespace tmport::mods {

// A WAV file -> SSND body (header + PCM, zero-padded to the NE size). note (optional) receives a
// short description of the conversion done ("44100 Hz 16-bit stereo -> 22050 Hz 8-bit mono").
std::vector<uint8_t> wavToSsnd(std::span<const uint8_t> wav, uint32_t targetRate, std::string* note = nullptr);

// SSND body -> WAV file (8-bit mono at the SSND's rate). Throws FormatError for a short body.
std::vector<uint8_t> ssndToWav(std::span<const uint8_t> ssnd);

}  // namespace tmport::mods
