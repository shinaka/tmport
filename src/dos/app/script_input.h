#pragma once
// Scripted input for the DOS edition, expressed in game ticks (PORTING_NOTES.md §4: "--keys should
// express key down/up in game ticks"). DosRuntime applies the events of tick n right before game
// tick n, so a script reproduces the per-tick key facts of the reference logs exactly (typematic
// repeats are synthesised by the PcKeyboard as for a player's keyboard).
//
// Text form: items separated by commas and/or white space, each TICK:ACTION[:ARGS]
//   TICK            a game tick (decimal), or +N = N ticks after the previous item's tick
//   down:KEY        key down              up:KEY    key up
//   tap:KEY[:N]     key down at TICK, up at TICK + N (default 1)
//   move:X:Y        pointer at logical pixel (X, Y) of the 320x200 screen
//   buttons:MASK    mouse buttons (1 left, 2 right, 4 middle)
//   click:X:Y       move there and press the left button at TICK, release at TICK + 1
// KEY names: dos::pcKeyByName (up, kp8, pgup, enter, a, f1, lshift, scan48, escan48, ...).
// Example: "55:down:right, 103:up:right, 195:down:pgup, +95:up:pgup".

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dos/input/keyboard.h"

namespace tmport::dos {

struct ScriptEvent {
    enum class Kind : uint8_t { KeyDown, KeyUp, MouseMove, MouseButtons };
    uint64_t tick = 0;
    Kind kind = Kind::KeyDown;
    PcKey key{};
    int x = 0, y = 0;      // MouseMove, logical 320x200 pixels
    uint16_t buttons = 0;  // MouseButtons
    bool operator==(const ScriptEvent&) const = default;
};

class ScriptedInput {
public:
    static std::optional<ScriptedInput> parse(std::string_view text, std::string& error);

    // Adds an event; events of the same tick keep the order they were added in.
    void add(const ScriptEvent& e);
    void keyDown(uint64_t tick, PcKey k) { add({tick, ScriptEvent::Kind::KeyDown, k}); }
    void keyUp(uint64_t tick, PcKey k) { add({tick, ScriptEvent::Kind::KeyUp, k}); }
    void tap(uint64_t tick, PcKey k, uint64_t holdTicks = 1) {
        keyDown(tick, k);
        keyUp(tick + holdTicks, k);
    }

    // The events of game tick n not yet taken (in order); the cursor moves past them. Ticks must be
    // asked in increasing order; events of earlier ticks that were skipped come with the next call.
    std::span<const ScriptEvent> take(uint64_t n);
    const std::vector<ScriptEvent>& events() const { return events_; }
    bool finished() const { return next_ >= events_.size(); }
    uint64_t lastTick() const { return events_.empty() ? 0 : events_.back().tick; }
    void rewind() { next_ = 0; }

private:
    std::vector<ScriptEvent> events_;  // sorted by tick, stable
    size_t next_ = 0;
};

}  // namespace tmport::dos
