#pragma once
// WaveVoice: the original's single waveOut voice (segment 1088, re/docs/fmt_sound.md §5.2).
// Sound effects and speech share it; starting any sound cuts the current one; nothing queues or
// mixes. Completion is polled (isPlaying), as the original polls MM_WOM_DONE.

#include <memory>

#include "audio/sound.h"
#include "audio/wave_output.h"

namespace tmport::audio {

class WaveVoice {
public:
    explicit WaveVoice(WaveOutput& out) : out_(out) {}

    // waveAvail (DS:09f2).
    bool available() const { return out_.available(); }

    // 1088:0014 Wave_Play: does nothing without a device or a sound; otherwise stops the current
    // sound and starts s from its beginning.
    void play(std::shared_ptr<const Sound> s);
    // 1088:0114 Wave_Stop: closes the device if open and marks the voice idle.
    void stop();
    // 1088:0175 Wave_IsPlaying: if the sound has ended, stop (close) first; then report whether a
    // sound is playing. Always false without a device.
    bool isPlaying();

    // The sound last started, while it is playing (nullptr when idle).
    const Sound* current() const { return playing_ ? current_.get() : nullptr; }
    // Number of sounds started (for tests and diagnostics).
    uint64_t startCount() const { return starts_; }

private:
    WaveOutput& out_;
    bool open_ = false;     // waveFmtPtr != NULL (DS:09f4)
    bool playing_ = false;  // waveState == 1 (DS:09f8; -1 = idle)
    std::shared_ptr<const Sound> current_;
    uint64_t starts_ = 0;
};

}  // namespace tmport::audio
