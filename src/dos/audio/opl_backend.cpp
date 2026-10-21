#include "dos/audio/opl_backend.h"

#include <algorithm>
#include <cmath>

#include "mischief/dos/audio/synth_common.h"

namespace tmport::dos {

int AdLibOpl::playTuneHold(size_t writes) {
    const double ticks = static_cast<double>(writes) * kPlayTuneWriteSeconds * mischief::dos::kPitTickHz;
    return std::max(0, static_cast<int>(std::ceil(ticks + kCallPhase)) - 1);
}

void AdLibOpl::playTune(int tune, int variant) {
    const size_t before = writes_;
    driver_.playTune(tune, variant);
    hold_ = playTuneHold(writes_ - before);
}

AdLibOpl::AdLibOpl(const SoundTables& tables, double sampleRate) : chip_(sampleRate), driver_(tables, *this) {}

void AdLibOpl::oplWrite(uint8_t reg, uint8_t value) {
    ++writes_;
    if (observer_) observer_(reg, value);
    chip_.oplWrite(reg, value);
}

bool AdLibOpl::restore(std::span<const uint8_t> dgroup) {
    if (dgroup.size() < AdLibDriver::kDsStateEnd) return false;
    driver_.restore(dgroup);
    return true;
}

}  // namespace tmport::dos
