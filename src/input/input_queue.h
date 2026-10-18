#pragma once
// The original's input model (segments 1010 WndProc, 1020 mouse, 1028 event queue;
// re/docs/code_1008-1030.md §5-6, ARCHITECTURE.md §3.3, PORTING_NOTES.md §4.7).
//
//  SDL events --EventTranslator--> WinMsg "application message queue" (post/handleEvent)
//      --drain (Evt_GetNextEvent / Evt_FlushMessages)--> WndProc side effects (held arrows, mouse
//      position, button state, pending event) --> GameEvent --> Input_GetCode -> 16-bit code.
//
// As in the original, messages are only *dispatched* inside a drain: posting does not change the
// held flags or the mouse position. The platform pump (Engine::pump) only posts; busy waits
// (Engine::delayMs) therefore do not deliver input, it is seen afterwards.
//
// Drain modes:
//  * FirstEventPerDrain: exactly the original: a drain dispatches every queued message but keeps
//    only the first event; later events of the same drain are lost (their side effects stay).
//    REFERENCE_CAPTURES.md §4.5 observed this for keys < ~5 ms apart.
//  * Lossless (engine default): every event is kept in order and returned by later fetches; an
//    arrow key repeat is merged into an identical copy still at the end of the queue.
//
// This header keeps the milestone-1 names (push/pop/flush/empty and the public held flags).

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "input/event_translator.h"
#include "input/hotspot_table.h"
#include "input/input_codes.h"
#include "input/win_msg.h"

union SDL_Event;
struct SDL_Renderer;

namespace tmport {

enum class InputMode : uint8_t {
    Lossless,            // keep every event (default)
    FirstEventPerDrain,  // the original's behaviour (1028:004f)
};

// GameEvent (6 bytes in the original, filled by Evt_GetNextEvent 1028:004f).
struct GameEvent {
    enum : int16_t { kNone = 0, kQuit = 1, kKey = 2, kLeftDown = 3, kLeftUp = 4, kRightDown = 5, kRightUp = 6 };
    int16_t what = kNone;
    int16_t p1 = 0;  // key code (kKey) or game x
    int16_t p2 = 0;  // game y
    bool operator==(const GameEvent&) const = default;
};

class InputQueue {
public:
    using Hook = std::function<void()>;
    // Live pointer position in game coordinates (GetCursorPos - viewport origin). Returns false
    // when unavailable (headless), in which case the last dispatched position is used.
    using MouseSource = std::function<bool(int& x, int& y)>;

    // --- configuration ---------------------------------------------------------------------------
    void setMode(InputMode m) { mode_ = m; }
    InputMode mode() const { return mode_; }
    // Optional comfort setting (default off): an arrow pressed and released within one drain
    // stays "held" until the next drain, so a tap shorter than a 100 ms tick is not missed
    // (REFERENCE_CAPTURES.md §4.5 observed such taps being lost in the original).
    void setTapLatch(bool on) { tapLatch_ = on; }
    bool tapLatch() const { return tapLatch_; }
    void setRenderer(SDL_Renderer* r) { translator_.setRenderer(r); }
    const EventTranslator& translator() const { return translator_; }

    // --- feeding (platform side) -------------------------------------------------------------
    // Translates one SDL event and posts the resulting messages. True if anything was posted.
    bool handleEvent(const SDL_Event& e);
    // Posts one message to the application queue. Consecutive mouse moves are coalesced (Windows
    // keeps one pending WM_MOUSEMOVE); a close request right after another one is dropped.
    void post(const WinMsg& m);
    // Evt_PostKey (1028:0000) from engine code: queues a key event with this code.
    void push(uint16_t code) { post(WinMsg::postKey(code)); }
    size_t pendingMessages() const { return messages_.size(); }

    // --- the original API ------------------------------------------------------------------------
    // 1028:004f Evt_GetNextEvent: drains the message queue (dispatching every message) and
    // returns the event type (0 = nothing). Ctrl+S (key 0x13) calls the sound-toggle hook when it
    // is the event returned. Does not pump the OS (Engine::getInputCode pumps first).
    int getNextEvent(GameEvent& ev);
    // 1028:0131 Input_GetCode: the look-ahead code if one is pending, else the next event
    // converted to an input code (0 if none).
    uint16_t getCode();
    uint16_t pop() { return getCode(); }
    // 1028:01f6 Input_PeekCode: fetches into the look-ahead slot without consuming.
    uint16_t peekCode();
    // 1028:0013 Evt_FlushMessages: dispatches every queued message (side effects stay) and
    // discards the pending event (and, in lossless mode, every queued event).
    void flushMessages();
    // 1028:0126 Input_Flush: flushMessages() and clears the look-ahead code.
    void flushInput();
    void flush() { flushInput(); }
    // True when no message, event, look-ahead code or quit request is pending.
    bool empty() const;

    // Held arrow keys (DS:06ee up, 06f0 down, 06f2 left, 06f4 right), written by the WndProc part
    // during drains and polled by the player controllers.
    bool upHeld = false, downHeld = false, leftHeld = false, rightHeld = false;

    // --- mouse (segment 1020) --------------------------------------------------------------------
    int mouseX() const { return mouseX_; }  // DS:06f6, game coordinates, last dispatched
    int mouseY() const { return mouseY_; }  // DS:06f8
    // 1020:0106 Mouse_IsButtonDown: left button state as of the last drain (DS:06e3).
    bool mouseButtonDown() const { return buttonDown_; }
    // 1020:010a Mouse_GetPos: live pointer position (also stored as mouseX/mouseY).
    void getMousePos(int& x, int& y);
    void setMouseSource(MouseSource src) { mouseSource_ = std::move(src); }
    // 1098:017e Hotspot_PollMouseDown: hitTest(live position) | 0x8000 while the left button is
    // down (0x8000 alone over no hot spot), else 0.
    uint16_t pollMouseDown();
    // 1020:00be Mouse_SetMoveHook: installs the hook called after each dispatched WM_MOUSEMOVE;
    // returns the previous one. 1020:00b1 Mouse_CallMoveHook calls it.
    Hook setMouseMoveHook(Hook hook);
    void callMouseMoveHook();

    // Ctrl+S inside the event fetch: Audio_ToggleAll (1128:0151).
    // TODO(integration): the audio area installs its toggle here.
    void setSoundToggleHook(Hook hook) { soundToggle_ = std::move(hook); }
    // Quit event inside the event fetch: Gfx_BlitToWindow(0, 0, 512, 384). Installed by Engine.
    void setQuitPresentHook(Hook hook) { quitPresent_ = std::move(hook); }
    // Sticky: a close request has been dispatched since the last clearQuitSeen() (for tools).
    bool quitSeen() const { return quitSeen_; }
    void clearQuitSeen() { quitSeen_ = false; }

    HotspotTable& hotspots() { return hotspots_; }
    const HotspotTable& hotspots() const { return hotspots_; }

    // Forgets every queued message/event and resets keys, mouse and the look-ahead slot.
    void reset();

    // Converts a GameEvent to an input code exactly like the switch of 1028:0131.
    uint16_t codeForEvent(const GameEvent& ev) const;

private:
    void beginDrain();
    void dispatch(const WinMsg& m);  // the input part of MainWndProc
    void setEvent(int16_t what, int16_t p1, int16_t p2);
    void deliver(GameEvent& ev);     // per-event side effects of the fetch (quit present, Ctrl+S)

    EventTranslator translator_;
    HotspotTable hotspots_;
    InputMode mode_ = InputMode::Lossless;
    bool tapLatch_ = false;

    std::deque<WinMsg> messages_;   // application message queue
    std::deque<GameEvent> events_;  // lossless mode: dispatched events not fetched yet
    std::vector<WinMsg> scratch_;

    int16_t evType_ = 0, evP1_ = 0, evP2_ = 0;  // DS:3d82 / 3d84 / 3d86
    bool produced_ = false;                     // the current message set an event
    bool quitRequested_ = false;                // DS:049c
    bool quitSeen_ = false;
    uint16_t pendingCode_ = 0;                  // DS:06ec
    int16_t mouseX_ = 0, mouseY_ = 0;           // DS:06f6 / 06f8
    bool buttonDown_ = false;                   // DS:06e3
    uint32_t drain_ = 0;                        // drain counter (tap latch)
    uint32_t downDrain_[4] = {};                // drain in which each arrow went down
    bool deferredUp_[4] = {};                   // tap latch: key-up to apply at the next drain
    Hook moveHook_;                             // DS:06e5
    Hook soundToggle_;
    Hook quitPresent_;
    MouseSource mouseSource_;
};

}  // namespace tmport
