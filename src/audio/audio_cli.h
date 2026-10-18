#pragma once
// Offline audio tools of tmport (no sound is played, no device is opened):
//   --dump-ssnd <id> <out.wav>          write one SSND resource as a WAV file
//   --midi-dump <file|tune id> [--midi-set B|E] [--brief]
//                                       parse a MIDI file and print its events, tempo map and
//                                       timing, plus a simulated sequencer run (null back end)
//   --midi-render <file|tune id> <out.wav> --soundfont <sf2> [--seconds N] [--loop] [--rate R]
//                                       render a tune through the SoundFont synthesizer on a
//                                       simulated clock into a WAV file and print level statistics
//   --make-test-soundfont <out.sf2>     write the synthetic test SoundFont (audio/sf2_writer.h)

#include <string>

namespace tmport::audio {

// Runs an audio tool if argv contains one of its commands. Returns true when it handled the
// command line (exitCode is then set), false to let the normal CLI run.
bool runAudioCli(int argc, char** argv, int& exitCode);
// Usage lines for --help.
std::string audioCliUsage();

}  // namespace tmport::audio
