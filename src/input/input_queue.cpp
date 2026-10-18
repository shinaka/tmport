#include "input/input_queue.h"

#include <SDL3/SDL_events.h>

namespace tmport {

namespace {

// Up, Down, Left, Right -> 0..3; -1 for other virtual keys.
int arrowIndex(uint16_t vk) {
    switch (vk) {
        case input::vk::kUp: return 0;
        case input::vk::kDown: return 1;
        case input::vk::kLeft: return 2;
        case input::vk::kRight: return 3;
        default: return -1;
    }
}

}  // namespace

bool InputQueue::handleEvent(const SDL_Event& e) {
    scratch_.clear();
    if (translator_.translate(e, scratch_) == 0) return false;
    for (const WinMsg& m : scratch_) post(m);
    return true;
}

void InputQueue::post(const WinMsg& m) {
    if (!messages_.empty()) {
        WinMsg& last = messages_.back();
        if (m.type == WinMsg::Type::MouseMove && last.type == WinMsg::Type::MouseMove) {
            last = m;  // Windows keeps a single pending WM_MOUSEMOVE with the latest position
            return;
        }
        if (m.type == WinMsg::Type::Close && last.type == WinMsg::Type::Close) return;
    }
    messages_.push_back(m);
}

void InputQueue::setEvent(int16_t what, int16_t p1, int16_t p2) {
    evType_ = what;
    evP1_ = p1;
    evP2_ = p2;
    produced_ = true;
}

// MainWndProc (1010:0196), input messages only.
void InputQueue::dispatch(const WinMsg& m) {
    switch (m.type) {
        case WinMsg::Type::Char:     // 1010:0520 WM_CHAR -> Evt_PostKey(wParam)
        case WinMsg::Type::PostKey:  // 1028:0000
            setEvent(GameEvent::kKey, static_cast<int16_t>(m.param), evP2_);
            break;
        case WinMsg::Type::KeyDown: {  // 1010:0471
            const uint16_t vk = m.param;
            const bool mapped = (vk >= input::vk::kPrior && vk <= input::vk::kDown) || vk == input::vk::kInsert ||
                                vk == input::vk::kDelete;
            // VK 0x29..0x2C hit an unresolved jump-table chain in the original (§12); ignored.
            if (!mapped) break;
            setEvent(GameEvent::kKey, static_cast<int16_t>(input::kVkFlag | vk), evP2_);
            const int a = arrowIndex(vk);
            if (a >= 0) {
                bool* flags[4] = {&upHeld, &downHeld, &leftHeld, &rightHeld};
                *flags[a] = true;
                downDrain_[a] = drain_;
                deferredUp_[a] = false;
            }
            break;
        }
        case WinMsg::Type::KeyUp: {  // 1010:04ef: arrows clear their held flag
            const int a = arrowIndex(m.param);
            if (a < 0) break;
            if (tapLatch_ && downDrain_[a] == drain_) {
                deferredUp_[a] = true;  // pressed and released within this drain: keep it for one poll
                break;
            }
            bool* flags[4] = {&upHeld, &downHeld, &leftHeld, &rightHeld};
            *flags[a] = false;
            break;
        }
        case WinMsg::Type::MouseMove:  // 1010:0430 (the stale-coordinate quirk is dropped)
            mouseX_ = m.x;
            mouseY_ = m.y;
            callMouseMoveHook();
            break;
        case WinMsg::Type::LButtonDown:  // 1010:03a4
            mouseX_ = m.x;
            mouseY_ = m.y;
            setEvent(GameEvent::kLeftDown, m.x, m.y);
            buttonDown_ = true;
            break;
        case WinMsg::Type::LButtonUp:  // 1010:03d2
            mouseX_ = m.x;
            mouseY_ = m.y;
            setEvent(GameEvent::kLeftUp, m.x, m.y);
            buttonDown_ = false;
            break;
        case WinMsg::Type::RButtonDown:  // 1010:0400
            mouseX_ = m.x;
            mouseY_ = m.y;
            setEvent(GameEvent::kRightDown, m.x, m.y);
            break;
        case WinMsg::Type::RButtonUp:  // 1010:0408
            mouseX_ = m.x;
            mouseY_ = m.y;
            setEvent(GameEvent::kRightUp, m.x, m.y);
            break;
        case WinMsg::Type::Close:  // 1010:0387 WM_CLOSE / 0344 WM_QUERYENDSESSION
            quitRequested_ = true;
            quitSeen_ = true;
            break;
    }
}

void InputQueue::beginDrain() {
    ++drain_;
    bool* flags[4] = {&upHeld, &downHeld, &leftHeld, &rightHeld};
    for (int a = 0; a < 4; ++a)
        if (deferredUp_[a]) {
            *flags[a] = false;
            deferredUp_[a] = false;
        }
}

void InputQueue::deliver(GameEvent& ev) {
    if (ev.what == GameEvent::kQuit && quitPresent_) quitPresent_();
    if (ev.what == GameEvent::kKey && ev.p1 == input::kCtrlS && soundToggle_) soundToggle_();
}

int InputQueue::getNextEvent(GameEvent& ev) {
    beginDrain();
    evType_ = 0;
    ev = GameEvent{};
    while (quitRequested_ || !messages_.empty()) {
        produced_ = false;
        if (!quitRequested_) {
            const WinMsg m = messages_.front();
            messages_.pop_front();
            dispatch(m);
        }
        if (mode_ == InputMode::FirstEventPerDrain) {
            // Only the first event of a drain is kept; a quit request overrides it.
            if (quitRequested_ || (evType_ != 0 && ev.what == 0)) {
                ev = GameEvent{evType_, evP1_, evP2_};
                if (quitRequested_) {
                    ev.what = GameEvent::kQuit;
                    quitRequested_ = false;
                }
                deliver(ev);
            }
        } else {
            if (quitRequested_) {
                events_.push_back(GameEvent{GameEvent::kQuit, evP1_, evP2_});
                quitRequested_ = false;
            } else if (produced_) {
                const GameEvent e{evType_, evP1_, evP2_};
                // A held arrow's key repeats queue the same code faster than the room loop takes
                // one per tick; keep one pending copy so a later Enter/Space is not delayed
                // (AUTOPLAY_FINDINGS A-1; the original's one-event drain never built a backlog).
                const bool arrow = e.what == GameEvent::kKey && e.p1 >= input::kLeft && e.p1 <= input::kDown;
                if (!(arrow && !events_.empty() && events_.back() == e))
                    events_.push_back(e);
            }
        }
    }
    if (mode_ == InputMode::Lossless && !events_.empty()) {
        ev = events_.front();
        events_.pop_front();
        deliver(ev);
    }
    return ev.what;
}

uint16_t InputQueue::codeForEvent(const GameEvent& ev) const {
    switch (ev.what) {
        case GameEvent::kQuit: return input::kEsc;
        case GameEvent::kKey: return static_cast<uint16_t>(ev.p1);
        case GameEvent::kLeftDown: {
            const uint16_t id = hotspots_.hitTest(ev.p1, ev.p2);
            return id ? static_cast<uint16_t>(id | input::kLeftDownFlag) : 0;
        }
        case GameEvent::kLeftUp: return hotspots_.hitTest(ev.p1, ev.p2);
        case GameEvent::kRightDown: {
            const uint16_t id = hotspots_.hitTest(ev.p1, ev.p2);
            return id ? static_cast<uint16_t>(id | input::kRightDownFlag) : 0;
        }
        case GameEvent::kRightUp: {
            const uint16_t id = hotspots_.hitTest(ev.p1, ev.p2);
            return id ? static_cast<uint16_t>(id | input::kRightUpFlag) : 0;
        }
        default: return 0;
    }
}

uint16_t InputQueue::getCode() {
    if (pendingCode_ != 0) {
        const uint16_t c = pendingCode_;
        pendingCode_ = 0;
        return c;
    }
    GameEvent ev;
    getNextEvent(ev);
    return codeForEvent(ev);
}

uint16_t InputQueue::peekCode() {
    if (pendingCode_ == 0) pendingCode_ = getCode();
    return pendingCode_;
}

void InputQueue::flushMessages() {
    beginDrain();
    while (!messages_.empty()) {
        const WinMsg m = messages_.front();
        messages_.pop_front();
        produced_ = false;
        dispatch(m);
    }
    evType_ = 0;
    events_.clear();
}

void InputQueue::flushInput() {
    flushMessages();
    pendingCode_ = 0;
}

bool InputQueue::empty() const {
    return messages_.empty() && events_.empty() && pendingCode_ == 0 && !quitRequested_;
}

void InputQueue::getMousePos(int& x, int& y) {
    int lx = 0, ly = 0;
    if (mouseSource_ && mouseSource_(lx, ly)) {
        mouseX_ = static_cast<int16_t>(lx);
        mouseY_ = static_cast<int16_t>(ly);
    }
    x = mouseX_;
    y = mouseY_;
}

uint16_t InputQueue::pollMouseDown() {
    if (!buttonDown_) return 0;
    int x = 0, y = 0;
    getMousePos(x, y);
    return static_cast<uint16_t>(hotspots_.hitTest(x, y) | input::kLeftDownFlag);
}

InputQueue::Hook InputQueue::setMouseMoveHook(Hook hook) {
    Hook old = std::move(moveHook_);
    moveHook_ = std::move(hook);
    return old;
}

void InputQueue::callMouseMoveHook() {
    if (moveHook_) moveHook_();
}

void InputQueue::reset() {
    messages_.clear();
    events_.clear();
    evType_ = evP1_ = evP2_ = 0;
    quitRequested_ = quitSeen_ = false;
    pendingCode_ = 0;
    mouseX_ = mouseY_ = 0;
    buttonDown_ = false;
    upHeld = downHeld = leftHeld = rightHeld = false;
    for (int a = 0; a < 4; ++a) deferredUp_[a] = false;
}

}  // namespace tmport
