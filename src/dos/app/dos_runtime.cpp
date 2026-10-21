#include "dos/app/dos_runtime.h"

#include <algorithm>

namespace tmport::dos {

namespace {

// The PIT input clock in units of 1/3 Hz, for the frame clock: one PIT tick = 15123 / 3579545 s.
constexpr uint64_t kPitClockTimes3 = 3'579'545ull;
constexpr uint64_t kPitTickUnits = 3ull * kPitDivisor;  // 15123

}  // namespace

// ------------------------------------------------------------------------------------- PitClock

const mischief::PitTimeBase& PitClock::timeBase() {
    // One PIT tick = 3 * 5041 / 3579545 s = 3024600000000 / 715909 ns (reduced by 5).
    static const mischief::PitTimeBase base(kPitDivisor);
    return base;
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
    const auto k = mischief::dos::pcKeyForSdlScancode(sdlScancode);
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

void DosRuntime::resumeAt(uint32_t gTicks, uint32_t pitToNextGameTick) {
    if (pit_ != 0 || frames_ != 0) return;  // only before the first tick
    const uint32_t wait = std::clamp<uint32_t>(pitToNextGameTick, 1, kPitTicksPerGameTick);
    runPitTicksTo(kPitTicksPerGameTick - wait);  // sound ticks only: the next game tick is on PIT tick 5
    phaseNs_ = 0;
    ticksOffset_ = gTicks - static_cast<uint32_t>(pit_);
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
    for (const mischief::dos::ScriptEvent& e : script_.take(n)) {
        // A sub-tick event carries its own time between game ticks e.tick - 1 and e.tick (an event
        // of an earlier tick that is applied late is taken as happening now: the keyboard clamps).
        uint64_t t = timeNs;
        if (e.phasePpm != mischief::dos::ScriptEvent::kOnTick && e.tick > 0) {
            const uint64_t t0 = PitClock::timeOfGameTick(e.tick - 1), t1 = PitClock::timeOfGameTick(e.tick);
            t = std::min(timeNs, t0 + (t1 - t0) * e.phasePpm / mischief::dos::ScriptEvent::kOnTick);
        }
        switch (e.kind) {
            case mischief::dos::ScriptEvent::Kind::KeyDown: keyboard_.keyDown(e.key, t); break;
            case mischief::dos::ScriptEvent::Kind::KeyUp: keyboard_.keyUp(e.key, t); break;
            case mischief::dos::ScriptEvent::Kind::MouseMove: mouse_.setLogicalPosition(e.x, e.y); break;
            case mischief::dos::ScriptEvent::Kind::MouseButtons: mouse_.setButtons(e.buttons); break;
        }
    }
}

// The frame's timing as the game reported it, placed at `base` (FrameTiming units since PIT tick 0).
void DosRuntime::placeFrameTiming(uint64_t base) {
    frameEnd_ = base + frameTiming_.length();
    frozen_.clear();
    for (const FrameTiming::Span& s : frameTiming_.quitSections()) frozen_.push_back({base + s.from, base + s.to});
}

bool DosRuntime::frozenAt(uint64_t units) const {
    return std::any_of(frozen_.begin(), frozen_.end(), [units](const AbsSpan& s) { return units >= s.from && units < s.to; });
}

// A call at `units` is played by the interrupt after it, PIT tick units / 1000 + 1; the device plays a
// command at the next interrupt, pit_ + 1, and the given number of PIT ticks after it.
uint32_t DosRuntime::soundDelay() const {
    if (!longFrames() || !frameTiming_.isOpen()) return 0;
    const uint64_t pit = frameTiming_.callTime() / FrameTiming::kMilli;
    return pit > pit_ ? static_cast<uint32_t>(pit - pit_) : 0;
}

// One original frame at the reference timing: g_stepDone cleared (unless the frame continues a
// dissolve or a Button_Flash wait: no Frame_Begin then), the frame clock (the frame that runs after a
// game tick takes that tick's share; one begun before its tick, when a long frame ended, none),
// Frame_Begin, the body, and its reported cost placed at its start.
void DosRuntime::runFrame(RuntimeClient& c, uint64_t start, uint64_t nextTick, bool continued) {
    if (!continued) stepGate_.clear();
    const uint64_t tick = kPitTicksPerGameTick * FrameTiming::kMilli;
    frameClock_.beginFrame(nextTick - start >= tick ? kPitTicksPerGameTick : 0);
    frameTiming_.clear();
    frameTiming_.setStart(start, nextTick);
    frameTiming_.open();
    c.frameBegin();
    c.frameBody();
    ++frames_;
    placeFrameTiming(frameTiming_.startUnits());
    continuing_ = frameTiming_.continuesFrame();
    frameTiming_.clear();
    if (c.quitRequested()) running_ = false;  // main: until g_quit != 0
}

// Game_Tick with its two counters, unless the tick falls into the frame's I/O (g_quit DS:912E = 1:
// 130f:0103 returns at once).
void DosRuntime::runGameTick(RuntimeClient& c, uint64_t n, uint64_t timeNs, bool inFrame) {
    const bool frozen = longFrames() && frozenAt(unitsNow());
    if (frozen) ++ticksFrozen_;
    if (frozen || c.quitRequested()) return;
    TickInfo info;
    info.gameTick = n;
    info.ticks = ticks();
    info.gated = stepGate_.done();
    info.timeNs = timeNs;
    info.inLongFrame = inFrame;
    c.gameTick(info);
    counters_.advance(c.uiMode());
}

// One game tick (Timer_ISR step 5) with the original frames around it.
void DosRuntime::gameStep(uint64_t n) {
    RuntimeClient& c = client_ ? *client_ : nullClient_;
    const uint64_t t = PitClock::timeOfPitTick(pit_);
    applyScript(n, t);
    keyboard_.deliverUntil(t);  // Kbd_ISR ran for every key byte before this tick
    const uint64_t now = unitsNow();
    if (frames_ == 0 && !frameTiming_.empty()) {
        // A cost reported before the first frame (main's start-up I/O) belongs to a frame that
        // began at PIT tick 0.
        if (longFrames()) placeFrameTiming(0);
        frameTiming_.clear();
    }
    if (longFrames()) {
        gameStepReference(n, c, t, now);
        return;
    }
    // Period mode and the slow-machine mode: Frame_Begin, the game ticks of the frame, its body (the
    // frame stands from right after the previous tick to the tick after its body; no cost is placed).
    const uint32_t perFrame = ticksPerFrame();
    if ((n - 1) % perFrame == 0) {
        const uint64_t tick = kPitTicksPerGameTick * FrameTiming::kMilli;
        stepGate_.clear();
        frameClock_.beginFrame(perFrame * kPitTicksPerGameTick);
        frameTiming_.clear();
        frameTiming_.setStart(now - tick + 1, now + perFrame * tick);
        c.frameBegin();
    }
    runGameTick(c, n, t, false);
    if (n % perFrame == 0) {
        c.frameBody();
        ++frames_;
        frameTiming_.clear();
        if (c.quitRequested()) running_ = false;  // main: until g_quit != 0
    }
}

// The reference timing (lockstep at the reference frame speed: long frames, PORTING_NOTES.md §2.5).
// The original's loop runs its frames back to back; idle ones last a fraction of a PIT tick, so the
// frame that does a tick's work begins right after that tick. A frame that ran past a tick (a scroll
// frame, a search frame, a bubble, a scene load) has the ticks inside it run Game_Tick without a frame
// of their own (or not at all inside its I/O); the loop's next frame begins when it ends: before the
// next game tick when it ended after the previous one.
void DosRuntime::gameStepReference(uint64_t n, RuntimeClient& c, uint64_t timeNs, uint64_t now) {
    const uint64_t tick = kPitTicksPerGameTick * FrameTiming::kMilli;
    if (!continuing_ && frames_ > 0 && frameEnd_ > now - tick && frameEnd_ <= now) {
        // The frame that began when the last one ended, between the previous tick and this one: it
        // latches what the ticks before it did and may last past this tick (which then falls inside it).
        runFrame(c, frameEnd_, now, false);
        if (!running_) return;
    }
    // The tick falls inside the last frame (it lasted past this tick's time): no frame after it.
    const bool inFrame = now < frameEnd_;
    if (inFrame) ++ticksInLongFrames_;
    runGameTick(c, n, timeNs, inFrame);
    if (!inFrame && running_) {
        // The frame after this tick: a new one (Frame_Begin latches this tick), or the next part of a
        // dissolve or a Button_Flash wait (no Frame_Begin: g_stepDone stays as the ticks set it).
        runFrame(c, now, now + tick, continuing_);
    }
}

}  // namespace tmport::dos
