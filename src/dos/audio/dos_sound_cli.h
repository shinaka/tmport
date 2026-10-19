#pragma once
// Offline DOS sound tool (no device is opened, nothing is played):
//   --dos-sound <speaker|tandy|adlib> sfx <n> <out.wav>     one effect (0..11)
//   --dos-sound <speaker|tandy|adlib> tune <n> <out.wav>    one tune (0..2)
//   --dos-sound <speaker|tandy|adlib> soundtest <out.wav>   the DOSBox-X recording schedule
//   --dos-sound <speaker|tandy|adlib> all <out dir>         every effect and tune
// options: --dos-data <dir> (default: TM_DATA_DOS), --raw (sample for sample like
// tools/dos/dos_sound.py: 44100 Hz, TI noise, peak-normalised, 50 ms tail), --rate <hz>,
// --loops <n>, --noise ti|ncr|sn, --seconds <max>, --lowpass <hz>, --dcblock <hz>,
// --script <steps> (soundtest), --state-log <txt> (speaker/PSG state and OPL calls per tick).
// AdLib: the effects play on the speaker and the tunes on OplFallbackSynth (opl_backend.h).

#include <optional>
#include <string>

namespace tmport::dos {

// Runs the tool when argv contains --dos-sound; nullopt otherwise.
std::optional<int> runDosSoundCommand(int argc, char** argv);
std::string dosSoundUsage();

}  // namespace tmport::dos
