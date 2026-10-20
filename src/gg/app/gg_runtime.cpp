#include "gg/app/gg_runtime.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>

#include "platform/host.h"

namespace tmport::gg {

const char* yieldPointName(YieldPoint p) {
    switch (p) {
        case YieldPoint::TimerWait: return "timerWait";
        case YieldPoint::TimerExpired: return "timerExpired";
        case YieldPoint::Delay: return "delay";
        case YieldPoint::GetInputCode: return "getInputCode";
        case YieldPoint::WaitInputCode: return "waitInputCode";
        case YieldPoint::Idle: return "idle";
        case YieldPoint::WaitRetrace: return "waitRetrace";
        case YieldPoint::WaitTicks: return "waitTicks";
    }
    return "?";
}

GgRuntime::GgRuntime(RuntimeConfig config, platform::Host* host)
    : config_(config),
      host_(host),
      timers_(clock_),
      keyboard_(config.keyboard),
      mouse_(config.mouse),
      input_(keyboard_, mouse_, zones_, [this] { return clock_.ticks(); }) {
    config_.pollStepNs = std::max<uint64_t>(config_.pollStepNs, 1000);
    clock_.setRetracePhaseNs(config_.retracePhaseNs);
    keyboard_.setTickSource([this] { return clock_.ticks(); });
    mouse_.setTickSource([this] { return clock_.ticks(); });
    if (host_) installHostHandlers();
}

GgRuntime::~GgRuntime() {
    if (host_) host_->setHandlers({});
}

// ------------------------------------------------------------------------------- start-up, shutdown

void GgRuntime::installHandlers() {
    // 1454:0000 Input_InstallHandlers: INT 33h AX=0Ch first, then INT 9, INT 8 and the PIT.
    mouse_.installHandler();
}

void GgRuntime::removeHandlers() {
    // 1454:0058 Input_RemoveHandlers
    mouse_.removeHandler();
}

// ------------------------------------------------------------------------------------- host input

void GgRuntime::installHostHandlers() {
    platform::HostHandlers h;
    auto pointerAt = [this](float wx, float wy) {
        // A window maps its coordinates to the 320x200 canvas; without one (headless, events posted
        // by tests or tools) the coordinates are canvas pixels already.
        float cx = wx, cy = wy;
        if (host_->hasWindow() && !host_->windowToCanvas(wx, wy, cx, cy)) return;
        hostPointer(cx, cy);
    };
    h.input = [this, pointerAt](const SDL_Event& e) {
        switch (e.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                hostKey(static_cast<uint32_t>(e.key.scancode), e.type == SDL_EVENT_KEY_DOWN, e.key.repeat);
                break;
            case SDL_EVENT_MOUSE_MOTION: pointerAt(e.motion.x, e.motion.y); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                pointerAt(e.button.x, e.button.y);
                const uint16_t b = e.button.button == SDL_BUTTON_LEFT     ? dos::Int33Mouse::kLeft
                                   : e.button.button == SDL_BUTTON_RIGHT  ? dos::Int33Mouse::kRight
                                   : e.button.button == SDL_BUTTON_MIDDLE ? dos::Int33Mouse::kMiddle
                                                                          : uint16_t{0};
                if (b) hostButton(b, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                break;
            }
            default: break;
        }
    };
    h.window = [this](const SDL_Event& e) {
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) releaseAllKeys();
    };
    h.activation = [this](bool active) {
        // Frozen (focus loss, minimised): keys go up; the time spent frozen is skipped.
        if (!active) releaseAllKeys();
        resync_ = true;
    };
    host_->setHandlers(std::move(h));
}

bool GgRuntime::hostKey(uint32_t sdlScancode, bool down, bool repeat) {
    const auto key = dos::pcKeyForSdlScancode(sdlScancode);
    if (!key) return false;
    if (repeat) return true;  // the keyboard makes its own typematic repeats
    const uint64_t t = clock_.nowNs();
    if (down) keyboard_.keyDown(*key, t);
    else keyboard_.keyUp(*key, t);
    // INT 9 runs at once: Kbd_ISR sees the byte with the g_ticks of this moment.
    keyboard_.deliverUntil(t);
    return true;
}

void GgRuntime::hostPointer(float canvasX, float canvasY) {
    mouse_.driver().setLogicalPosition(static_cast<int>(std::floor(canvasX)), static_cast<int>(std::floor(canvasY)));
}

void GgRuntime::hostButton(uint16_t button, bool down) { mouse_.driver().setButton(button, down); }

void GgRuntime::releaseAllKeys() {
    const uint64_t t = clock_.nowNs();
    keyboard_.releaseAll(t);
    keyboard_.deliverUntil(t);
}

// ------------------------------------------------------------------------------------------- time

uint64_t GgRuntime::hostNowNs() const {
    if (host_) return host_->nowNs();
    if (config_.clock) return config_.clock->nowMs() * 1'000'000ull;
    return SDL_GetTicksNS();
}

void GgRuntime::sleepHost() {
    Clock& c = host_ ? host_->clock() : (config_.clock ? *config_.clock : Clock::system());
    c.sleepMs(1);
}

uint64_t GgRuntime::scriptTimeNs(const dos::ScriptEvent& e) const {
    if (e.phasePpm >= dos::ScriptEvent::kOnTick || e.tick == 0) return clock_.timeOfTick(e.tick);
    const uint64_t base = clock_.timeOfTick(e.tick - 1);
    const uint64_t span = clock_.timeOfTick(e.tick) - base;
    return base + span * e.phasePpm / dos::ScriptEvent::kOnTick;
}

std::optional<uint64_t> GgRuntime::nextScriptTimeNs() const {
    if (scriptNext_ >= script_.events().size()) return std::nullopt;
    return scriptTimeNs(script_.events()[scriptNext_]);
}

uint64_t GgRuntime::scriptEndTick() const {
    const auto& ev = script_.events();
    return ev.empty() ? 0 : ev.back().tick;
}

void GgRuntime::setScript(dos::ScriptedInput script) {
    script_ = std::move(script);
    scriptNext_ = 0;
}

void GgRuntime::addScript(const dos::ScriptedInput& more, uint64_t offset) {
    dos::ScriptedInput merged;
    for (size_t i = scriptNext_; i < script_.events().size(); ++i) merged.add(script_.events()[i]);
    for (dos::ScriptEvent e : more.events()) {
        e.tick += offset;
        merged.add(e);
    }
    script_ = std::move(merged);
    scriptNext_ = 0;
}

void GgRuntime::applyScriptEvent(const dos::ScriptEvent& e) {
    const uint64_t when = std::max(scriptTimeNs(e), clock_.nowNs());
    keyboard_.deliverUntil(when);  // what the keyboard sent before it
    switch (e.kind) {
        case dos::ScriptEvent::Kind::KeyDown:
            keyboard_.keyDown(e.key, when);
            keyboard_.deliverUntil(when);
            break;
        case dos::ScriptEvent::Kind::KeyUp:
            keyboard_.keyUp(e.key, when);
            keyboard_.deliverUntil(when);
            break;
        case dos::ScriptEvent::Kind::MouseMove: mouse_.driver().setLogicalPosition(e.x, e.y); break;
        case dos::ScriptEvent::Kind::MouseButtons: mouse_.driver().setButtons(e.buttons); break;
    }
}

void GgRuntime::runInterrupt() {
    const uint64_t k = clock_.interrupts() + 1;
    if (limit_ && k > *limit_) throw GgRunEnd(GgRunEnd::Reason::Limit);
    // The keyboard bytes up to the interrupt's time come first (Kbd_ISR stamps them with the g_ticks
    // before it), then Timer_ISR.
    keyboard_.deliverUntil(clock_.timeOfTick(k));
    clock_.tick();
}

void GgRuntime::advanceTo(uint64_t t) {
    while (true) {
        const uint64_t tick = clock_.nextTickNs();
        // A script event due by the target and by the next interrupt (one at an interrupt's own time
        // comes right before it).
        if (const auto ev = nextScriptTimeNs(); ev && *ev <= t && *ev <= tick) {
            const dos::ScriptEvent& e = script_.events()[scriptNext_++];
            if (*ev < tick) clock_.setNowNs(std::max(*ev, clock_.nowNs()));
            applyScriptEvent(e);
            continue;
        }
        if (tick > t) break;
        runInterrupt();
    }
    if (t > clock_.nowNs()) clock_.setNowNs(t);
    keyboard_.deliverUntil(clock_.nowNs());
}

void GgRuntime::advanceTicks(uint64_t n) { advanceTo(clock_.timeOfTick(clock_.interrupts() + n)); }

void GgRuntime::pump() {
    if (host_) {
        host_->dispatchPending();
        if (!config_.deterministic) host_->freezeWhilePaused();
        if (host_->quitRequested()) throw GgRunEnd(GgRunEnd::Reason::HostClosed);
    }
    if (config_.deterministic) return;
    const uint64_t h = hostNowNs();
    if (resync_) {
        resync_ = false;
        lastHostNs_ = h;
    }
    uint64_t delta = h > lastHostNs_ ? h - lastHostNs_ : 0;
    lastHostNs_ = h;
    const uint64_t cap = clock_.tickPeriodNs() * config_.maxCatchUpTicks;
    if (config_.maxCatchUpTicks != 0 && delta > cap) {
        droppedNs_ += delta - cap;
        delta = cap;
    }
    advanceTo(clock_.nowNs() + delta);
    if (host_) host_->renderIfDue();
}

// ------------------------------------------------------------------------------------ yield points

void GgRuntime::poll() {
    ++polls_;
    const bool first = pollsSinceWait_++ == 0;
    if (config_.deterministic) {
        pump();
        // Busy polls let a little time pass, never across an interrupt in one step.
        if (!first) advanceTo(std::min(clock_.nowNs() + config_.pollStepNs, clock_.nextTickNs()));
    } else {
        if (!first && lastPollIdle_) sleepHost();
        pump();
    }
}

void GgRuntime::endWait(uint64_t startNs) {
    // A wait that let time pass makes the next poll free again; one that ended at once (its deadline
    // was already reached) counts as a poll, so a loop of them still moves on.
    if (clock_.nowNs() > startNs) {
        pollsSinceWait_ = 0;
    } else if (config_.deterministic && pollsSinceWait_++ > 0) {
        advanceTo(std::min(clock_.nowNs() + config_.pollStepNs, clock_.nextTickNs()));
    }
}

void GgRuntime::timerWait(int slot) {
    // 152c:0029 Timer_Wait: no polling, no flush (the mouse handler still runs).
    ++waits_;
    const uint64_t start = clock_.nowNs();
    pump();
    while (!timers_.reached(slot)) {
        if (config_.deterministic) {
            advanceTo(clock_.nextTickNs());
        } else {
            sleepHost();
            pump();
        }
    }
    endWait(start);
    yielded(YieldPoint::TimerWait, slot);
}

bool GgRuntime::timerExpired(int slot) {
    poll();
    const bool r = timers_.expired(slot);
    lastPollIdle_ = !r;
    yielded(YieldPoint::TimerExpired, slot);
    return r;
}

void GgRuntime::delay(uint16_t n) {
    // 152c:0072 Delay: the test comes first (Delay(0) returns at once); the 32-bit end wraps.
    ++waits_;
    const uint64_t start = clock_.nowNs();
    if (!config_.deterministic) pump();  // real time: g_ticks as of now
    const uint32_t end = clock_.ticks() + n;
    while (clock_.ticks() < end) (void)getInputCode();
    endWait(start);
    yielded(YieldPoint::Delay);
}

uint16_t GgRuntime::getInputCode() {
    poll();
    const uint16_t c = input_.getCode();
    lastPollIdle_ = c == 0;
    yielded(YieldPoint::GetInputCode);
    return c;
}

uint16_t GgRuntime::waitInputCode() {
    // 198d:02a5 Input_WaitCode: no pacing, no flush.
    uint16_t c = 0;
    while ((c = getInputCode()) == 0) {
    }
    yielded(YieldPoint::WaitInputCode);
    return c;
}

void GgRuntime::idle() {
    poll();
    lastPollIdle_ = true;
    yielded(YieldPoint::Idle);
}

void GgRuntime::waitRetrace() {
    // 1399:05d6: wait while 3DAh bit 3 is set, then while it is clear: the start of the next retrace.
    ++waits_;
    const uint64_t start = clock_.nowNs();
    pump();  // real time: the clock catches up first
    const uint64_t target = clock_.nextRetraceAfter(clock_.nowNs());
    if (config_.deterministic) {
        advanceTo(target);
    } else {
        while (clock_.nowNs() < target) {
            sleepHost();
            pump();
        }
    }
    endWait(start);
    yielded(YieldPoint::WaitRetrace);
}

void GgRuntime::waitTicks(uint64_t n) {
    ++waits_;
    const uint64_t start = clock_.nowNs();
    pump();
    const uint64_t target = clock_.interrupts() + n;
    if (config_.deterministic) {
        advanceTo(clock_.timeOfTick(target));
    } else {
        while (clock_.interrupts() < target) {
            sleepHost();
            pump();
        }
    }
    endWait(start);
    yielded(YieldPoint::WaitTicks);
}

// ------------------------------------------------------------------------------------------ frames

void GgRuntime::showFrame(std::span<const uint32_t> rgba) {
    if (rgba.size() != static_cast<size_t>(kWidth) * kHeight) return;
    frame_.assign(rgba.begin(), rgba.end());
    ++framesShown_;
    if (host_) host_->presenter().presentFrame(rgba);
    if (frameSink_) frameSink_(clock_.interrupts(), rgba);
}

}  // namespace tmport::gg
