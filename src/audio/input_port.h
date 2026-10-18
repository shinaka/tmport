#pragma once
// InputPort: the input primitives the speech wait needs (re/docs/fmt_sound.md §5.2), so the
// audio code can run on the engine (EngineInputPort) or on a scripted fake clock in tests.

#include <cstdint>

namespace tmport::audio {

class InputPort {
public:
    virtual ~InputPort() = default;
    // 1028:01f6 Input_PeekCode: pumps events and returns the next input code (0 = none) without
    // consuming it; the next getCode() returns the same code.
    virtual uint16_t peekCode() = 0;
    // 1028:0131 Input_GetCode: returns and consumes the next input code (0 = none).
    virtual uint16_t getCode() = 0;
    // Called between polls of a wait loop: keeps the window alive (pump, present) without
    // consuming input, and yields the CPU briefly.
    virtual void idle() = 0;
};

}  // namespace tmport::audio
