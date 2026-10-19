#include "dos/app/dos_runtime.h"

#include <algorithm>

namespace tmport::dos {

namespace {

// One PIT tick = 3 * 5041 / 3579545 s = kPeriodNum / kPeriodDen ns (reduced: gcd = 1).
constexpr uint64_t kPeriodNum = 3'024'600'000'000ull;  // 15123e9 / 5
constexpr uint64_t kPeriodDen = 715'909ull;            // 3579545 / 5
// The PIT input clock in units of 1/3 Hz, for the frame clock: one PIT tick = 15123 / 3579545 s.
constexpr uint64_t kPitClockTimes3 = 3'579'545ull;
constexpr uint64_t kPitTickUnits = 3ull * kPitDivisor;  // 15123

}  // namespace

// ------------------------------------------------------------------------------------- PitClock

uint64_t PitClock::timeOfPitTick(uint64_t k) {
    // ceil(k * N / D) without overflow (k = q * D + r): the first whole nanosecond at or after the
    // exact tick time, so "tick k is due at t" is exactly k * period <= t.
    const uint64_t q = k / kPeriodDen, r = k % kPeriodDen;
    return q * kPeriodNum + (r * kPeriodNum + kPeriodDen - 1) / kPeriodDen;
}

uint64_t PitClock::pitTicksAt(uint64_t tNs) {
    // floor(t * D / N), exact: k * N / D <= t  <=>  k <= t * D / N.
    const uint64_t q = tNs / kPeriodNum, r = tNs % kPeriodNum;
    return q * kPeriodDen + (r * kPeriodDen) / kPeriodNum;
}

// ---------------------------------------------------------------------------------- TickCounters

// 130f:0186: the 13-tick cycle and the 60-tick "second".
void TickCounters::advance(uint8_t uiMode) {
    if (--cycle13 < 0) cycle13 = 12;
    --secondCountdown;
    if (secondCountdown == 0 && uiMode == 0) {
        secondCountdown = 60;
        ++seconds;
    }
}

// ---------------------------------------------------------------------------- OriginalFrameClock

void OriginalFrameClock::beginFrame(uint32_t pitTicks) {
    for (size_t i = 0; i < static_cast<size_t>(FrameFeature::Count); ++i) {
        const auto f = static_cast<FrameFeature>(i);
        const uint64_t rate = referenceRateMilli(f);
        if (speed_ == FrameSpeed::Period || rate == 0) {
            steps_[i] = f == FrameFeature::Dissolve && speed_ == FrameSpeed::Period ? kDissolveCells : 1;
            continue;
        }
        // steps += rate/1000 per second * pitTicks * 15123 / 3579545 s, kept exactly in integers.
        acc_[i] += rate * pitTicks * kPitTickUnits;
        const uint64_t unit = 1000ull * kPitClockTimes3;
        steps_[i] = static_cast<int>(acc_[i] / unit);
        acc_[i] %= unit;
    }
}

// ------------------------------------------------------------------------------------ DosRuntime

DosRuntime::DosRuntime(RuntimeOptions options)
    : options_(options), frameClock_(options.frameSpeed), keyboard_(options.keyboard) {
    keyboard_.isr().install();  // Kbd_Install 43a0:0002
}

bool DosRuntime::hostKey(uint32_t sdlScancode, bool down, bool hostRepeat) {
    const auto k = pcKeyForSdlScancode(sdlScancode);
    if (!k) return false;
    if (hostRepeat) return true;
    if (down) keyDownNow(*k);
    else keyUpNow(*k);
    return true;
}

uint32_t DosRuntime::ticksPerFrame() const {
    return options_.frameMode == FrameMode::SlowMachine ? std::max<uint32_t>(1, options_.slowMachineTicksPerFrame) : 1;
}

uint32_t DosRuntime::advance(uint64_t hostDeltaNs) {
    if (!running_) return 0;
    hostDeltaNs = std::min<uint64_t>(hostDeltaNs, 3600ull * 1'000'000'000ull);  // a stall of over an hour is an hour
    const uint64_t before = gameTicks();
    const uint64_t target = nowNs() + hostDeltaNs;
    uint64_t due = PitClock::pitTicksAt(target);
    uint64_t phaseAfter = target - PitClock::timeOfPitTick(due);
    if (options_.maxCatchUpGameTicks > 0) {
        // The PIT tick of the first game tick still to run, and of the last one allowed.
        const uint64_t firstGame = (pit_ / kPitTicksPerGameTick + 1) * kPitTicksPerGameTick;
        const uint64_t lastAllowed = firstGame + (options_.maxCatchUpGameTicks - 1) * kPitTicksPerGameTick;
        if (due > lastAllowed + kPitTicksPerGameTick - 1) {
            // The host fell behind: drop the time in between (keeping the sub-tick phase).
            const uint64_t keep = lastAllowed + kPitTicksPerGameTick - 1;
            droppedPit_ += due - keep;
            due = keep;
        }
    }
    runPitTicksTo(due);
    if (running_) phaseNs_ = phaseAfter;
    return static_cast<uint32_t>(gameTicks() - before);
}

void DosRuntime::stepGameTicks(uint64_t n) {
    for (uint64_t i = 0; i < n && running_; ++i) {
        runPitTicksTo((pit_ / kPitTicksPerGameTick + 1) * kPitTicksPerGameTick);
        phaseNs_ = 0;
    }
}

void DosRuntime::stepPitTicks(uint64_t n) {
    if (!running_) return;
    runPitTicksTo(pit_ + n);
    phaseNs_ = 0;
}

// Timer_ISR (1000:0428) for each PIT tick up to lastPit.
void DosRuntime::runPitTicksTo(uint64_t lastPit) {
    RuntimeClient& c = client_ ? *client_ : nullClient_;
    while (pit_ < lastPit && running_) {
        ++pit_;           // g_ticks DS:3D2B += 1
        c.soundTick();    // Sound_Tick 491b:0004 (never skipped)
        if (pit_ % kPitTicksPerGameTick == 0) gameStep(pit_ / kPitTicksPerGameTick);
    }
}

void DosRuntime::applyScript(uint64_t n, uint64_t timeNs) {
    for (const ScriptEvent& e : script_.take(n)) {
        switch (e.kind) {
            case ScriptEvent::Kind::KeyDown: keyboard_.keyDown(e.key, timeNs); break;
            case ScriptEvent::Kind::KeyUp: keyboard_.keyUp(e.key, timeNs); break;
            case ScriptEvent::Kind::MouseMove: mouse_.setLogicalPosition(e.x, e.y); break;
            case ScriptEvent::Kind::MouseButtons: mouse_.setButtons(e.buttons); break;
        }
    }
}

// One game tick (Timer_ISR step 5) with the original frame it belongs to.
void DosRuntime::gameStep(uint64_t n) {
    RuntimeClient& c = client_ ? *client_ : nullClient_;
    const uint64_t t = PitClock::timeOfPitTick(pit_);
    applyScript(n, t);
    keyboard_.deliverUntil(t);  // Kbd_ISR ran for every key byte before this tick
    const uint32_t perFrame = ticksPerFrame();
    if ((n - 1) % perFrame == 0) {
        stepGate_.clear();
        frameClock_.beginFrame(perFrame * kPitTicksPerGameTick);
        c.frameBegin();
    }
    if (!c.quitRequested()) {  // 130f:0103: if g_quit != 0 return
        TickInfo info;
        info.gameTick = n;
        info.ticks = static_cast<uint32_t>(pit_);
        info.gated = stepGate_.done();
        info.timeNs = t;
        c.gameTick(info);
        counters_.advance(c.uiMode());
    }
    if (n % perFrame == 0) {
        c.frameBody();
        ++frames_;
        if (c.quitRequested()) running_ = false;  // main: until g_quit != 0
    }
}

}  // namespace tmport::dos
