#include "dos/audio/speaker_synth.h"

namespace tmport::dos {

void SpeakerSynth::setEnabled(bool on) {
    if (on && !on_ && mode_ == SynthMode::Smooth) osc_.restart();
    on_ = on;
}

void SpeakerSynth::render(double* out, size_t n) {
    if (mode_ == SynthMode::Raw)
        osc_.render(frequency(), on_ ? kAmplitude : 0.0, n, out, rate_);
    else if (on_)  // the counter stands still while GATE2 is low; it restarts on the rising edge
        osc_.renderCounter(frequency(), kAmplitude, n, out, rate_);
}

}  // namespace tmport::dos
