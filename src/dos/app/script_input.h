#pragma once
// Scripted input for the DOS games, expressed in game ticks (re/dos/docs/PORTING_NOTES.md §4:
// "--keys should express key down/up in game ticks"). The runtime applies the events of tick n right
// before game tick n (TM DOS: DosRuntime, whose game tick is 5 PIT ticks; G&G: its own tick unit), so
// a script reproduces the per-tick key facts of the reference logs exactly (typematic repeats are
// synthesised by the keyboard, dos/input/pc_keyboard.h, as for a player's keyboard). Shared core.
//
// Text form: items separated by commas and/or white space, each TICK:ACTION[:ARGS]
//   TICK            a game tick (decimal): the event happens at the time of game tick TICK and is
//                   applied right before it; or N.F, a time inside a game tick (fraction of a tick,
//                   up to 6 decimals): N.F lies between game ticks N and N + 1, so the event is
//                   applied right before game tick N + 1 and stamped with its own time, as a key
//                   press in DOSBox-X lands between two PIT interrupts (one PIT tick = 0.2 game
//                   ticks). The time matters for the typematic repeats that follow a key down
//                   (500 ms, then every 33 ms); "806.4923:down:down" presses Down 10.4 ms after tick 806.
//                   +N or +N.F = that many ticks after the previous item's time
//   down:KEY        key down              up:KEY    key up
//   tap:KEY[:N]     key down at TICK, up at TICK + N (default 1)
//   move:X:Y        pointer at logical pixel (X, Y) of the 320x200 screen
//   buttons:MASK    mouse buttons (1 left, 2 right, 4 middle)
//   click:X:Y       move there and press the left button at TICK, release at TICK + 1
// KEY names: dos::pcKeyByName (up, kp8, pgup, enter, a, f1, lshift, scan48, escan48, ...).
// Example: "55:down:right, 103:up:right, 195:down:pgup, +95:up:pgup, 806.4923:down:down".

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dos/input/pc_keyboard.h"

namespace tmport::dos {

struct ScriptEvent {
    enum class Kind : uint8_t { KeyDown, KeyUp, MouseMove, MouseButtons };
    static constexpr uint32_t kOnTick = 1'000'000;  // phasePpm of an event at the tick's own time

    uint64_t tick = 0;     // applied right before game tick `tick`
    Kind kind = Kind::KeyDown;
    PcKey key{};
    int x = 0, y = 0;      // MouseMove, logical 320x200 pixels
    uint16_t buttons = 0;  // MouseButtons
    // When the event happens, in millionths of a game tick after game tick `tick - 1`: 1..1000000
    // (kOnTick: at game tick `tick` itself, the whole-tick form).
    uint32_t phasePpm = kOnTick;
    bool operator==(const ScriptEvent&) const = default;
};

class ScriptedInput {
public:
    static std::optional<ScriptedInput> parse(std::string_view text, std::string& error);

    // Adds an event; events keep the order of their time (tick, then phase), and events of the same
    // time the order they were added in.
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
    std::vector<ScriptEvent> events_;  // sorted by (tick, phasePpm), stable
    size_t next_ = 0;
};

}  // namespace tmport::dos
