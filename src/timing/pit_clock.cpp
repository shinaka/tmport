#include "timing/pit_clock.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace tmport {

// ----------------------------------------------------------------------------------- PitTimeBase

PitTimeBase::PitTimeBase(uint32_t divisor) : divisor_(divisor) {
    if (divisor == 0) throw std::invalid_argument("PIT divisor 0");
    // One period = 3 * divisor / 3579545 s = 3e9 * divisor / 3579545 ns, reduced.
    const uint64_t n = 3'000'000'000ull * divisor, d = 3'579'545ull;
    const uint64_t g = std::gcd(n, d);
    num_ = n / g;
    den_ = d / g;
    // ticksAt multiplies a remainder below num_ by den_.
    if (num_ > std::numeric_limits<uint64_t>::max() / den_) throw std::invalid_argument("PIT divisor too large");
    numQ_ = num_ / den_;
    numR_ = num_ % den_;
}

uint64_t PitTimeBase::timeOfTick(uint64_t k) const {
    // ceil(k * N / D) without overflow (k = q * D + r, N = nq * D + nr): the first whole nanosecond
    // at or after the exact tick time, so "tick k is due at t" is exactly k * period <= t.
    const uint64_t q = k / den_, r = k % den_;
    return q * num_ + r * numQ_ + (r * numR_ + den_ - 1) / den_;
}

uint64_t PitTimeBase::ticksAt(uint64_t tNs) const {
    // floor(t * D / N), exact: k * N / D <= t  <=>  k <= t * D / N.
    const uint64_t q = tNs / num_, r = tNs % num_;
    return q * den_ + (r * den_) / num_;
}

// ------------------------------------------------------------------------------------ VirtualPit

int VirtualPit::onTick(uint32_t every, std::function<void(uint32_t)> fn, uint32_t phase) {
    const int id = nextId_++;
    callbacks_.push_back({id, std::max<uint32_t>(every, 1), phase, std::move(fn)});
    return id;
}

void VirtualPit::remove(int id) {
    std::erase_if(callbacks_, [id](const Callback& c) { return c.id == id; });
}

void VirtualPit::runInterrupt() {
    ++run_;
    const uint32_t c = ticks();
    for (size_t i = 0; i < callbacks_.size(); ++i) {  // by index: a callback may add another
        const Callback& cb = callbacks_[i];
        if (c % cb.every == cb.phase % cb.every) {
            const auto fn = cb.fn;
            fn(c);
        }
    }
}

uint64_t VirtualPit::advanceNs(uint64_t hostDeltaNs, uint64_t maxTicks) {
    const uint64_t target = nowNs() + hostDeltaNs;
    uint64_t due = base_.ticksAt(target);
    const uint64_t phaseAfter = target - base_.timeOfTick(due);
    if (maxTicks > 0 && due > run_ + maxTicks) {
        dropped_ += due - (run_ + maxTicks);
        due = run_ + maxTicks;
    }
    const uint64_t before = run_;
    while (run_ < due) runInterrupt();
    phaseNs_ = phaseAfter;
    return run_ - before;
}

void VirtualPit::step(uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) runInterrupt();
    phaseNs_ = 0;
}

void VirtualPit::runTo(uint64_t k) {
    while (run_ < k) runInterrupt();
    phaseNs_ = 0;
}

}  // namespace tmport
