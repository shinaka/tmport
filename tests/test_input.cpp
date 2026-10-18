// Input model tests (input/event_translator, input/input_queue, input/hotspot_table,
// input/cursor_controller): SDL events are synthesised, nothing opens a window.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "input/cursor_controller.h"
#include "input/event_translator.h"
#include "input/hotspot_table.h"
#include "input/input_queue.h"

using namespace tmport;

namespace {

SDL_Event keyEvent(bool down, SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE, SDL_Scancode sc = SDL_SCANCODE_UNKNOWN,
                   bool repeat = false) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.key = key;
    e.key.mod = mod;
    e.key.scancode = sc;
    e.key.down = down;
    e.key.repeat = repeat;
    return e;
}
SDL_Event keyDown(SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE, SDL_Scancode sc = SDL_SCANCODE_UNKNOWN) {
    return keyEvent(true, key, mod, sc);
}
SDL_Event keyUp(SDL_Keycode key, SDL_Keymod mod = SDL_KMOD_NONE, SDL_Scancode sc = SDL_SCANCODE_UNKNOWN) {
    return keyEvent(false, key, mod, sc);
}
SDL_Event textInput(const char* utf8) {
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = utf8;
    return e;
}
// Mouse events in canvas coordinates (no renderer attached to the translator).
SDL_Event mouseButton(bool down, Uint8 button, float cx, float cy) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = button;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = cx;
    e.button.y = cy;
    return e;
}
SDL_Event mouseMove(float cx, float cy) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = cx;
    e.motion.y = cy;
    return e;
}
SDL_Event simple(SDL_EventType t) {
    SDL_Event e{};
    e.type = t;
    return e;
}
// Game (x, y) -> canvas coordinates at the pixel centre.
float cx(int gx) { return static_cast<float>(gx + 64) + 0.5f; }
float cy(int gy) { return static_cast<float>(gy + 48) + 0.5f; }

std::vector<uint16_t> drainCodes(InputQueue& q, int n) {
    std::vector<uint16_t> out;
    for (int i = 0; i < n; ++i) out.push_back(q.getCode());
    return out;
}

}  // namespace

TEST_CASE("translator: control keys become WM_CHAR bytes") {
    std::vector<WinMsg> m;
    EventTranslator t;
    t.translate(keyDown(SDLK_RETURN), m);
    t.translate(keyDown(SDLK_KP_ENTER), m);
    t.translate(keyDown(SDLK_ESCAPE), m);
    t.translate(keyDown(SDLK_BACKSPACE), m);
    t.translate(keyDown(SDLK_TAB), m);
    t.translate(keyDown(SDLK_R, SDL_KMOD_LCTRL), m);
    t.translate(keyDown(SDLK_S, SDL_KMOD_RCTRL), m);
    t.translate(keyDown(SDLK_A, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT)), m);
    t.translate(keyDown(SDLK_LEFTBRACKET, SDL_KMOD_LCTRL), m);
    t.translate(keyDown(SDLK_RETURN, SDL_KMOD_LCTRL), m);
    t.translate(keyDown(SDLK_BACKSPACE, SDL_KMOD_LCTRL), m);
    const std::vector<WinMsg> want = {WinMsg::chr(0x0D), WinMsg::chr(0x0D), WinMsg::chr(0x1B), WinMsg::chr(0x08),
                                      WinMsg::chr(0x09), WinMsg::chr(0x12), WinMsg::chr(0x13), WinMsg::chr(0x01),
                                      WinMsg::chr(0x1B), WinMsg::chr(0x0A), WinMsg::chr(0x7F)};
    CHECK(m == want);

    // Letters, digits and space produce nothing on key down (they arrive as text input); AltGr
    // (Ctrl+Alt) and Alt+key produce no control character.
    m.clear();
    t.translate(keyDown(SDLK_A), m);
    t.translate(keyDown(SDLK_SPACE), m);
    t.translate(keyDown(SDLK_1), m);
    t.translate(keyDown(SDLK_E, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_RALT)), m);
    t.translate(keyDown(SDLK_X, SDL_KMOD_LALT), m);
    t.translate(keyDown(SDLK_F1), m);
    t.translate(keyUp(SDLK_RETURN), m);
    CHECK(m.empty());
}

TEST_CASE("translator: text input becomes Windows ANSI bytes") {
    std::vector<WinMsg> m;
    EventTranslator t;
    t.translate(textInput("aZ 9_"), m);
    t.translate(textInput("\xC3\xA9"), m);          // e acute -> 0xE9
    t.translate(textInput("\xE2\x82\xAC"), m);      // euro sign -> 0x80
    t.translate(textInput("\xE6\x97\xA5"), m);      // CJK: no ANSI byte, dropped
    t.translate(textInput("\x7F\x01"), m);          // control characters dropped
    const std::vector<WinMsg> want = {WinMsg::chr('a'), WinMsg::chr('Z'), WinMsg::chr(' '), WinMsg::chr('9'),
                                      WinMsg::chr('_'), WinMsg::chr(0xE9), WinMsg::chr(0x80)};
    CHECK(m == want);
    CHECK(EventTranslator::utf8ToAnsi("\xE2\x80\x9C" "x" "\xE2\x80\x9D") == std::vector<uint8_t>{0x93, 'x', 0x94});
}

TEST_CASE("translator: navigation keys become WM_KEYDOWN/WM_KEYUP virtual keys") {
    std::vector<WinMsg> m;
    EventTranslator t;
    const SDL_Keycode keys[] = {SDLK_PAGEUP, SDLK_PAGEDOWN, SDLK_END, SDLK_HOME, SDLK_LEFT,
                                SDLK_UP,     SDLK_RIGHT,    SDLK_DOWN, SDLK_INSERT, SDLK_DELETE};
    const uint16_t vks[] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E};
    for (size_t i = 0; i < 10; ++i) {
        m.clear();
        t.translate(keyDown(keys[i]), m);
        t.translate(keyUp(keys[i]), m);
        REQUIRE(m.size() == 2);
        CHECK(m[0] == WinMsg::keyDown(vks[i]));
        CHECK(m[1] == WinMsg::keyUp(vks[i]));
    }
    // Keypad with NumLock off = navigation keys; with NumLock on nothing on key down (digit text).
    m.clear();
    t.translate(keyDown(SDLK_KP_8, SDL_KMOD_NONE, SDL_SCANCODE_KP_8), m);
    t.translate(keyDown(SDLK_KP_4, SDL_KMOD_NONE, SDL_SCANCODE_KP_4), m);
    t.translate(keyDown(SDLK_KP_PERIOD, SDL_KMOD_NONE, SDL_SCANCODE_KP_PERIOD), m);
    t.translate(keyDown(SDLK_KP_8, SDL_KMOD_NUM, SDL_SCANCODE_KP_8), m);
    t.translate(keyDown(SDLK_KP_5, SDL_KMOD_NONE, SDL_SCANCODE_KP_5), m);
    const std::vector<WinMsg> want = {WinMsg::keyDown(0x26), WinMsg::keyDown(0x25), WinMsg::keyDown(0x2E)};
    CHECK(m == want);
    // Alt+arrow is WM_SYSKEYDOWN (ignored); its key-up still clears the held flag; Ctrl/Shift pass.
    m.clear();
    t.translate(keyDown(SDLK_LEFT, SDL_KMOD_LALT), m);
    t.translate(keyUp(SDLK_LEFT, SDL_KMOD_LALT), m);
    t.translate(keyDown(SDLK_RIGHT, SDL_KMOD_LCTRL), m);
    t.translate(keyDown(SDLK_UP, SDL_KMOD_LSHIFT), m);
    const std::vector<WinMsg> want2 = {WinMsg::keyUp(0x25), WinMsg::keyDown(0x27), WinMsg::keyDown(0x26)};
    CHECK(m == want2);
}

TEST_CASE("translator: mouse and close events") {
    std::vector<WinMsg> m;
    EventTranslator t;
    t.translate(mouseMove(64.0f, 48.0f), m);
    t.translate(mouseButton(true, SDL_BUTTON_LEFT, 575.9f, 431.2f), m);
    t.translate(mouseButton(false, SDL_BUTTON_LEFT, 10.0f, 20.0f), m);
    t.translate(mouseButton(true, SDL_BUTTON_RIGHT, 100.0f, 100.0f), m);
    t.translate(mouseButton(false, SDL_BUTTON_RIGHT, 100.0f, 100.0f), m);
    t.translate(mouseButton(true, SDL_BUTTON_MIDDLE, 100.0f, 100.0f), m);  // ignored
    t.translate(simple(SDL_EVENT_WINDOW_CLOSE_REQUESTED), m);
    t.translate(simple(SDL_EVENT_QUIT), m);
    using T = WinMsg::Type;
    const std::vector<WinMsg> want = {WinMsg::mouse(T::MouseMove, 0, 0), WinMsg::mouse(T::LButtonDown, 511, 383),
                                      WinMsg::mouse(T::LButtonUp, -54, -28), WinMsg::mouse(T::RButtonDown, 36, 52),
                                      WinMsg::mouse(T::RButtonUp, 36, 52), WinMsg::close(), WinMsg::close()};
    CHECK(m == want);
}

TEST_CASE("translator: window -> canvas -> game mapping through a letterboxed renderer") {
    // A software renderer on a 1000x600 surface stands in for the window: 640x480 letterboxed at
    // scale 1.25 -> canvas drawn at x 100..900, y 0..600.
    SDL_Surface* surface = SDL_CreateSurface(1000, 600, SDL_PIXELFORMAT_RGBA32);
    REQUIRE(surface);
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(r);
    REQUIRE(SDL_SetRenderLogicalPresentation(r, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX));
    EventTranslator t;
    t.setRenderer(r);
    std::vector<WinMsg> m;
    using T = WinMsg::Type;
    t.translate(mouseButton(true, SDL_BUTTON_LEFT, 500.5f, 300.5f), m);  // centre -> canvas (320.4,240.4)
    t.translate(mouseButton(true, SDL_BUTTON_LEFT, 180.5f, 60.5f), m);   // canvas (64.4,48.4) = game (0,0)
    t.translate(mouseButton(true, SDL_BUTTON_LEFT, 100.5f, 0.5f), m);    // canvas (0.4,0.4)
    t.translate(mouseMove(819.9f, 539.9f), m);                           // canvas (575.9, 431.9) -> (511,383)
    const std::vector<WinMsg> want = {WinMsg::mouse(T::LButtonDown, 256, 192), WinMsg::mouse(T::LButtonDown, 0, 0),
                                      WinMsg::mouse(T::LButtonDown, -64, -48), WinMsg::mouse(T::MouseMove, 511, 383)};
    CHECK(m == want);
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(surface);
}

TEST_CASE("queue: key codes like Input_GetCode") {
    InputQueue q;
    q.handleEvent(keyDown(SDLK_RETURN));
    q.handleEvent(keyDown(SDLK_ESCAPE));
    q.handleEvent(textInput(" Ab"));
    q.handleEvent(keyDown(SDLK_BACKSPACE));
    q.handleEvent(keyDown(SDLK_R, SDL_KMOD_LCTRL));
    q.handleEvent(keyDown(SDLK_PAGEUP));
    q.handleEvent(keyDown(SDLK_DELETE));
    q.handleEvent(keyDown(SDLK_LEFT));
    CHECK(drainCodes(q, 11) == std::vector<uint16_t>{0x0D, 0x1B, 0x20, 'A', 'b', 0x08, 0x12, 0x121, 0x12E, 0x125, 0});
    CHECK(q.empty());
}

TEST_CASE("queue: held arrow flags change only when messages are dispatched") {
    InputQueue q;
    q.handleEvent(keyDown(SDLK_UP));
    q.handleEvent(keyDown(SDLK_RIGHT));
    CHECK_FALSE(q.upHeld);  // posted, not yet dispatched
    CHECK(q.getCode() == input::kUp);
    CHECK(q.upHeld);  // the drain dispatched both key downs
    CHECK(q.rightHeld);
    CHECK(q.getCode() == input::kRight);  // lossless: second event still delivered
    q.handleEvent(keyUp(SDLK_UP));
    CHECK(q.upHeld);
    CHECK(q.getCode() == 0);  // key up produces no code
    CHECK_FALSE(q.upHeld);
    CHECK(q.rightHeld);
    // Auto-repeat repeats the code.
    q.handleEvent(keyEvent(true, SDLK_DOWN, SDL_KMOD_NONE, SDL_SCANCODE_UNKNOWN, true));
    q.handleEvent(keyEvent(true, SDLK_DOWN, SDL_KMOD_NONE, SDL_SCANCODE_UNKNOWN, true));
    CHECK(drainCodes(q, 3) == std::vector<uint16_t>{0x128, 0x128, 0});
    CHECK(q.downHeld);
    // Non-arrow navigation keys do not touch the flags; PgUp key-up is harmless.
    q.handleEvent(keyDown(SDLK_HOME));
    q.handleEvent(keyUp(SDLK_HOME));
    CHECK(drainCodes(q, 2) == std::vector<uint16_t>{0x124, 0});
    CHECK_FALSE(q.leftHeld);
}

TEST_CASE("queue: tap latch keeps a short arrow tap for one poll") {
    for (bool latch : {false, true}) {
        InputQueue q;
        q.setTapLatch(latch);
        q.handleEvent(keyDown(SDLK_UP));
        q.handleEvent(keyUp(SDLK_UP));
        CHECK(q.getCode() == input::kUp);
        CHECK(q.upHeld == latch);  // original: the up in the same drain clears it at once
        CHECK(q.getCode() == 0);
        CHECK_FALSE(q.upHeld);  // the next drain applies the deferred key up
    }
    // A key held across drains behaves the same with or without the latch.
    InputQueue q;
    q.setTapLatch(true);
    q.handleEvent(keyDown(SDLK_LEFT));
    q.getCode();
    q.handleEvent(keyUp(SDLK_LEFT));
    q.getCode();
    CHECK_FALSE(q.leftHeld);
}

TEST_CASE("queue: mouse buttons become hot-spot codes with the transition flags") {
    InputQueue q;
    auto& hs = q.hotspots();
    hs.set(0x0D, 0, 0, 512, 384);     // slot 0, full screen (click = Enter)
    hs.set(0x63, 10, 296, 190, 76);   // slot 1, overlaps slot 0 and wins
    q.handleEvent(mouseButton(true, SDL_BUTTON_LEFT, cx(20), cy(300)));
    q.handleEvent(mouseButton(false, SDL_BUTTON_LEFT, cx(20), cy(300)));
    q.handleEvent(mouseButton(true, SDL_BUTTON_RIGHT, cx(20), cy(300)));
    q.handleEvent(mouseButton(false, SDL_BUTTON_RIGHT, cx(20), cy(300)));
    q.handleEvent(mouseButton(true, SDL_BUTTON_LEFT, cx(300), cy(10)));
    CHECK(drainCodes(q, 6) == std::vector<uint16_t>{0x8063, 0x63, 0x4063, 0x2063, 0x800D, 0});
    CHECK(q.mouseButtonDown());  // the last left down was not released
    CHECK(q.mouseX() == 300);
    CHECK(q.mouseY() == 10);

    // Over no hot spot every transition gives 0 (the black border outside the game area too).
    hs.clear();
    q.handleEvent(mouseButton(false, SDL_BUTTON_LEFT, cx(300), cy(10)));
    q.handleEvent(mouseButton(true, SDL_BUTTON_RIGHT, 5.0f, 5.0f));
    CHECK(drainCodes(q, 2) == std::vector<uint16_t>{0, 0});
    CHECK_FALSE(q.mouseButtonDown());
    CHECK(q.mouseX() == -59);
    // The hit test runs when the code is fetched (as in 1028:0131), against the current table.
    q.handleEvent(mouseButton(true, SDL_BUTTON_LEFT, cx(5), cy(5)));
    hs.set(0x4B0, 0, 0, 10, 10);
    CHECK(q.getCode() == 0x84B0);
}

TEST_CASE("queue: mouse moves are coalesced, dispatched in the drain and call the move hook") {
    InputQueue q;
    int calls = 0;
    std::vector<std::pair<int, int>> seen;
    q.setMouseMoveHook([&] {
        ++calls;
        seen.emplace_back(q.mouseX(), q.mouseY());
    });
    q.handleEvent(mouseMove(cx(1), cy(1)));
    q.handleEvent(mouseMove(cx(2), cy(2)));
    q.handleEvent(mouseMove(cx(3), cy(4)));
    CHECK(calls == 0);  // nothing dispatched yet
    CHECK(q.pendingMessages() == 1);
    CHECK(q.getCode() == 0);  // a move is not an event
    CHECK(calls == 1);
    CHECK(seen.back() == std::pair<int, int>{3, 4});
    // A move between two clicks is kept in order.
    q.handleEvent(mouseButton(true, SDL_BUTTON_LEFT, cx(3), cy(4)));
    q.handleEvent(mouseMove(cx(9), cy(9)));
    q.handleEvent(mouseButton(false, SDL_BUTTON_LEFT, cx(9), cy(9)));
    CHECK(q.pendingMessages() == 3);
    q.getCode();
    CHECK(calls == 2);
    auto old = q.setMouseMoveHook(nullptr);
    CHECK(static_cast<bool>(old));
}

TEST_CASE("queue: window close and quit become 0x1B once, with a full re-present") {
    InputQueue q;
    int presents = 0;
    q.setQuitPresentHook([&] { ++presents; });
    q.handleEvent(simple(SDL_EVENT_WINDOW_CLOSE_REQUESTED));
    q.handleEvent(simple(SDL_EVENT_QUIT));  // the duplicate request is dropped
    CHECK(drainCodes(q, 2) == std::vector<uint16_t>{0x1B, 0});
    CHECK(presents == 1);
    CHECK(q.quitSeen());
    // A quit request survives Input_Flush (DS:049c is not cleared by Evt_FlushMessages).
    q.handleEvent(simple(SDL_EVENT_WINDOW_CLOSE_REQUESTED));
    q.flushInput();
    CHECK(q.getCode() == 0x1B);
}

TEST_CASE("queue: first-event-per-drain mode loses later events of a drain") {
    InputQueue q;
    q.setMode(InputMode::FirstEventPerDrain);
    int toggles = 0;
    q.setSoundToggleHook([&] { ++toggles; });
    q.handleEvent(textInput("ABC"));
    CHECK(drainCodes(q, 2) == std::vector<uint16_t>{'A', 0});  // B and C lost
    // Side effects of lost events still happen: the button and the position.
    q.hotspots().set(7, 0, 0, 100, 100);
    q.handleEvent(textInput("x"));
    q.handleEvent(mouseButton(true, SDL_BUTTON_LEFT, cx(50), cy(60)));
    q.handleEvent(keyDown(SDLK_UP));
    CHECK(q.getCode() == 'x');
    CHECK(q.mouseButtonDown());
    CHECK(q.mouseX() == 50);
    CHECK(q.upHeld);
    CHECK(q.getCode() == 0);
    // Ctrl+S toggles only when it is the event the drain keeps.
    q.handleEvent(textInput("a"));
    q.handleEvent(keyDown(SDLK_S, SDL_KMOD_LCTRL));
    CHECK(q.getCode() == 'a');
    CHECK(toggles == 0);
    q.handleEvent(keyDown(SDLK_S, SDL_KMOD_LCTRL));
    q.handleEvent(textInput("a"));
    CHECK(q.getCode() == 0x13);  // handled inside the fetch and also returned
    CHECK(toggles == 1);
    // A quit request in the same drain overrides the kept event.
    q.handleEvent(textInput("q"));
    q.handleEvent(simple(SDL_EVENT_WINDOW_CLOSE_REQUESTED));
    CHECK(q.getCode() == 0x1B);
    CHECK(q.getCode() == 0);
    // Separate drains lose nothing.
    q.handleEvent(textInput("1"));
    CHECK(q.getCode() == '1');
    q.handleEvent(textInput("2"));
    CHECK(q.getCode() == '2');
}

TEST_CASE("queue: lossless mode keeps every event in order; Ctrl+S toggles once per delivery") {
    InputQueue q;
    int toggles = 0;
    q.setSoundToggleHook([&] { ++toggles; });
    q.hotspots().set(0x600, 0, 20, 512, 267);
    q.handleEvent(textInput("a"));
    q.handleEvent(keyDown(SDLK_S, SDL_KMOD_LCTRL));
    q.handleEvent(mouseButton(true, SDL_BUTTON_LEFT, cx(100), cy(100)));
    q.handleEvent(keyDown(SDLK_S, SDL_KMOD_LCTRL));
    q.handleEvent(simple(SDL_EVENT_WINDOW_CLOSE_REQUESTED));
    CHECK(q.getCode() == 'a');
    CHECK(toggles == 0);
    CHECK(q.getCode() == 0x13);
    CHECK(toggles == 1);
    CHECK(drainCodes(q, 4) == std::vector<uint16_t>{0x8600, 0x13, 0x1B, 0});
    CHECK(toggles == 2);
}

TEST_CASE("queue: peek and flush") {
    InputQueue q;
    q.handleEvent(textInput("ab"));
    CHECK(q.peekCode() == 'a');
    CHECK(q.peekCode() == 'a');
    CHECK(q.getCode() == 'a');
    CHECK(q.getCode() == 'b');
    CHECK(q.peekCode() == 0);

    // Input_Flush drops queued input and the look-ahead, keeps the side effects.
    q.handleEvent(textInput("xyz"));
    CHECK(q.peekCode() == 'x');
    q.handleEvent(keyDown(SDLK_LEFT));
    q.handleEvent(mouseMove(cx(40), cy(41)));
    q.flushInput();
    CHECK(q.leftHeld);
    CHECK(q.mouseX() == 40);
    CHECK(q.getCode() == 0);
    CHECK(q.empty());
    // Evt_PostKey from engine code.
    q.push(0x4AE);
    CHECK(q.getCode() == 0x4AE);
}

TEST_CASE("queue: Hotspot_PollMouseDown uses the live pointer position") {
    InputQueue q;
    q.hotspots().set(0x600, 0, 20, 512, 267);
    int liveX = 100, liveY = 100;
    q.setMouseSource([&](int& x, int& y) {
        x = liveX;
        y = liveY;
        return true;
    });
    CHECK(q.pollMouseDown() == 0);  // button up
    q.handleEvent(mouseButton(true, SDL_BUTTON_LEFT, cx(5), cy(5)));
    q.getCode();
    CHECK(q.mouseButtonDown());
    CHECK(q.pollMouseDown() == 0x8600);
    liveY = 5;  // pointer moved above the play-field while held
    CHECK(q.pollMouseDown() == 0x8000);
    int x = 0, y = 0;
    q.getMousePos(x, y);
    CHECK(x == 100);
    CHECK(y == 5);
    CHECK(q.mouseY() == 5);  // Mouse_GetPos also stores the position
}

TEST_CASE("mouse-directed walking latch") {
    InputQueue q;
    q.hotspots().set(0x600, 0, 20, 512, 267);
    q.setMouseSource([&](int& x, int& y) {
        x = 200;
        y = 5;  // outside the play-field
        return true;
    });
    MouseWalkLatch latch;
    CHECK(latch.resolve(0, q) == 0);  // button up
    q.handleEvent(mouseButton(true, SDL_BUTTON_LEFT, cx(200), cy(100)));
    const uint16_t first = q.getCode();
    CHECK(first == 0x8600);
    CHECK(latch.resolve(first, q) == 0x8600);
    latch.set();  // the controller steered toward the mouse
    CHECK(latch.resolve(0, q) == MouseWalkLatch::kPlayField);  // held and latched: still 0x600
    CHECK(latch.resolve(input::kSpace, q) == input::kSpace);   // any input clears the latch
    CHECK_FALSE(latch.latched());
    CHECK(latch.resolve(0, q) == 0x8000);  // not latched: plain poll (pointer outside every spot)
}

TEST_CASE("hot-spot table: allocator, priority, half-open rectangles") {
    HotspotTable t;
    t.set(0x600, 0, 20, 512, 267);
    t.set(0x601, 0, 20, 0, 267);   // zero width: never hit
    t.set(0x602, 512, 20, 0, 267);
    t.set(0x0D, 0, 228, 512, 59);
    CHECK(t.find(0x600) == 0);
    CHECK(t.find(0x601) == 1);
    CHECK(t.find(0x0D) == 3);
    CHECK(t.hitTest(0, 20) == 0x600);
    CHECK(t.hitTest(0, 250) == 0x0D);       // slot 3 beats slot 0
    CHECK(t.hitTest(511, 286) == 0x0D);
    CHECK(t.hitTest(512, 100) == 0);        // x + w is outside
    CHECK(t.hitTest(100, 287) == 0);        // y + h is outside
    CHECK(t.hitTest(100, 19) == 0);
    CHECK(t.hitTest(-1, 100) == 0);
    // Update in place keeps the slot (and so the priority).
    t.set(0x600, 0, 0, 512, 384);
    CHECK(t.find(0x600) == 0);
    CHECK(t.hitTest(10, 300) == 0x600);
    CHECK(t.hitTest(10, 250) == 0x0D);
    // Remove frees the slot; the next new id takes the LOWEST free slot.
    t.remove(0x601);
    t.set(0x409, 0, 0, 512, 384);
    CHECK(t.find(0x409) == 1);
    CHECK(t.hitTest(10, 300) == 0x409);  // slot 1 beats slot 0, slot 3 still wins in its band
    CHECK(t.hitTest(10, 250) == 0x0D);
    CHECK(t.count() == 4);
    // Fill the table; a 51st id overflows into the save area and is never hit.
    for (uint16_t id = 0x1000; t.count() < HotspotTable::kSlots; ++id) t.set(id, 600, 600, 1, 1);
    t.set(0x7777, 0, 0, 512, 384);
    CHECK_FALSE(t.contains(0x7777));
    CHECK(t.saved()[0].id == 0x7777);
    CHECK(t.hitTest(10, 300) == 0x409);
    // Remove clears only the id; ClearAll frees every slot (rectangles stay).
    t.clear();
    CHECK(t.count() == 0);
    CHECK(t.hitTest(10, 300) == 0);
    CHECK(t.slots()[0].w == 512);
    // Save/restore helpers (1098:0130/0157, dead code in the original).
    t.set(1, 0, 0, 5, 5);
    t.saveAll();
    t.clear();
    t.restoreAll();
    CHECK(t.hitTest(2, 2) == 1);
}

TEST_CASE("cursor controller: walk cursors over the play-field, arrow elsewhere or when forced") {
    InputQueue q;
    CursorSet cursors;  // headless: selection is tracked without SDL
    CursorController ctl(q, cursors);
    ctl.installAsMoveHook();
    auto moveTo = [&](int x, int y) {
        q.handleEvent(mouseMove(cx(x), cy(y)));
        q.getCode();
    };
    moveTo(100, 100);
    CHECK(cursors.shown() == CursorSet::Shown::Custom);
    CHECK(cursors.shownIndex() == CursorController::kWalkRightCursor);  // 100 >= last 0
    moveTo(90, 100);
    CHECK(cursors.shownIndex() == CursorController::kWalkLeftCursor);
    moveTo(90, 101);  // same x: not "left of" -> right cursor
    CHECK(cursors.shownIndex() == CursorController::kWalkRightCursor);
    moveTo(100, 300);  // below the play-field (y >= 287)
    CHECK(cursors.shown() == CursorSet::Shown::Arrow);
    moveTo(512, 20);  // x == 512 still counts as play-field
    CHECK(cursors.shown() == CursorSet::Shown::Custom);
    ctl.setForced(true);
    CHECK(cursors.shown() == CursorSet::Shown::Arrow);
    moveTo(200, 100);
    CHECK(cursors.shown() == CursorSet::Shown::Arrow);
    ctl.setForced(false);  // last position reset to (-1,-1): moving right
    CHECK(cursors.shownIndex() == CursorController::kWalkRightCursor);
    CHECK(ctl.lastX() == 200);
    // Cursor_Select returns the previous index.
    CHECK(cursors.select(0) == CursorController::kWalkRightCursor);
}
