#pragma once
// A tiny synthetic SoundFont 2 file, generated in code (no samples from anywhere), for checking the
// SoundFont MIDI path without a real General MIDI SoundFont: unit tests, and
// `tmport --make-test-soundfont <out.sf2>` for trying --midi-render or the game on a machine that
// has no SoundFont. It sounds like a test tone generator, not like music.
//
// Contents (SoundFont 2.01, 16-bit samples):
//   * sample "sine" and sample "square": 20 cycles of 100 samples each at 44000 Hz with a loop
//     over cycles 2..17, original pitch 69 (A4), so the root key plays at exactly 440 Hz;
//   * preset bank 0 program 0 "Test Sine" (sine), bank 0 program 80 "Test Square" (square),
//     both following the key (12 semitones per octave);
//   * preset bank 128 program 0 "Test Drums" (the drum kit of MIDI channel 10): the square
//     sample on every key at a fixed 440 Hz (scale tuning 0), so a drum hit is recognisable;
//   * no attack, full sustain, release 50 ms (releaseVolEnv -5186 timecents).

#include <cstdint>
#include <vector>

namespace tmport::audio {

namespace testsf2 {
inline constexpr int kSampleRate = 44000;
inline constexpr int kCycle = 100;           // samples per cycle: 440 Hz at the root key
inline constexpr int kRootKey = 69;
inline constexpr double kRootHz = 440.0;
inline constexpr double kReleaseSeconds = 0.05;
inline constexpr int kSineProgram = 0;
inline constexpr int kSquareProgram = 80;
inline constexpr int kDrumBank = 128;
}  // namespace testsf2

// The synthetic SoundFont as a complete .sf2 file image.
std::vector<uint8_t> makeTestSoundFont();

}  // namespace tmport::audio
