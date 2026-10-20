#include "gg/app/gg_clock.h"

#include <algorithm>

namespace tmport::gg {

GgClock::GgClock() : pit_(kDivisor), tickClock_(pit_) {
    // 1454:00c5: g_ticks16 counts with g_ticks (registered first, so it runs before the sound tick).
    pit_.onTick(1, [this](uint32_t) { ++ticks16_; });
}

void GgClock::setSoundTick(std::function<void(uint32_t)> fn) {
    if (soundId_ != 0) pit_.remove(soundId_);
    soundId_ = 0;
    sound_ = std::move(fn);
    if (!sound_) return;
    // 1454:00c9: TEST g_ticks, 1 after the increment; Snd_Tick on even values.
    soundId_ = pit_.onTick(2, [this](uint32_t t) {
        ++soundTicks_;
        if (sound_) sound_(t);
    }, 0);
}

void GgClock::tick() {
    // 1454:00a5: the BIOS chain counter is 0 in the file and only ever reset to 0, so the old INT 8
    // runs on every interrupt (8 times the BIOS rate).
    biosCounter_ = static_cast<int16_t>(biosCounter_ + 1);
    if (biosCounter_ >= 0) {
        biosCounter_ = 0;
        ++biosTicks_;
    }
    // 1454:00bb: g_ticks += 1 (and the callbacks: g_ticks16, the sound tick on even values).
    pit_.step(1);
    nowNs_ = timeOfTick(pit_.interrupts());
}

void GgClock::setNowNs(uint64_t t) {
    const uint64_t last = timeOfTick(pit_.interrupts());
    const uint64_t next = timeOfTick(pit_.interrupts() + 1);
    nowNs_ = std::clamp<uint64_t>(t, last, next - 1);
}

uint64_t GgClock::retraceTimeNs(uint64_t k) const {
    // ceil(k * N / D) without overflow: k = q * D + r.
    const uint64_t q = k / kRetraceDen, r = k % kRetraceDen;
    return retracePhase_ + q * kRetraceNum + (r * kRetraceNum + kRetraceDen - 1) / kRetraceDen;
}

uint64_t GgClock::nextRetraceAfter(uint64_t t) const {
    if (t < retracePhase_) return retracePhase_;
    const uint64_t d = t - retracePhase_;
    const uint64_t q = d / kRetraceNum, r = d % kRetraceNum;
    uint64_t k = q * kRetraceDen + r * kRetraceDen / kRetraceNum;  // about the retraces so far
    while (k > 0 && retraceTimeNs(k) > t) --k;
    while (retraceTimeNs(k) <= t) ++k;
    return retraceTimeNs(k);
}

}  // namespace tmport::gg
