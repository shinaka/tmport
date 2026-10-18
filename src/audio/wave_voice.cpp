#include "audio/wave_voice.h"

namespace tmport::audio {

// 1088:0014 Wave_Play
void WaveVoice::play(std::shared_ptr<const Sound> s) {
    if (!out_.available() || !s) return;
    stop();
    current_ = std::move(s);
    out_.start(*current_);
    open_ = true;
    playing_ = true;
    ++starts_;
}

// 1088:0114 Wave_Stop
void WaveVoice::stop() {
    if (!out_.available()) return;
    if (open_) {
        out_.stop();
        open_ = false;
    }
    playing_ = false;
    current_.reset();
}

// 1088:0175 Wave_IsPlaying
bool WaveVoice::isPlaying() {
    if (open_ && out_.done()) stop();
    return playing_;
}

}  // namespace tmport::audio
