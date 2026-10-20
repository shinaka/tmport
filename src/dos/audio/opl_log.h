#pragma once
// A log of OPL2 register writes for comparisons with DOSBox-X raw OPL captures (.dro), write for
// write (tools/dos/oplcmp.py; both DOS games). One line per write: 'tick reg value' (hex reg and
// value), tick = the driver tick the write belongs to (TM DOS: the PIT tick; G&G: whatever unit its
// comparison tool expects, e.g. the sound tick); writes made by a call from the game between two
// ticks carry a fourth column 'c' and the tick they run before (setCommand).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "dos/audio/opl_writer.h"

namespace tmport::dos {

class OplWriteLog {
public:
    struct Entry {
        uint64_t tick = 0;
        uint8_t reg = 0, value = 0;
        bool command = false;
        bool operator==(const Entry&) const = default;
    };

    explicit OplWriteLog(bool enabled = true) : on_(enabled) {}

    // Records one write (nothing when disabled).
    void write(uint64_t tick, uint8_t reg, uint8_t value) {
        if (on_) entries_.push_back({tick, reg, value, command_});
    }
    // The writes that follow belong to a command (until setCommand(false)).
    void setCommand(bool on) { command_ = on; }
    bool enabled() const { return on_; }
    void clear() { entries_.clear(); }
    const std::vector<Entry>& entries() const { return entries_; }

    // The log as text, one 'tick reg value[ c]' line per write.
    std::string text() const;
    // Writes text() to path (UTF-8 path); a disabled log writes nothing and succeeds.
    bool save(const std::string& path) const;

private:
    bool on_;
    bool command_ = false;
    std::vector<Entry> entries_;
};

// An OplWriter in front of another (the chip, or a game's own writer) that logs every write with the
// tick `tick()` returns.
class OplLogTap final : public OplWriter {
public:
    OplLogTap(OplWriter& out, OplWriteLog& log, std::function<uint64_t()> tick)
        : out_(out), log_(log), tick_(std::move(tick)) {}
    void oplWrite(uint8_t reg, uint8_t value) override {
        log_.write(tick_ ? tick_() : 0, reg, value);
        out_.oplWrite(reg, value);
    }

private:
    OplWriter& out_;
    OplWriteLog& log_;
    std::function<uint64_t()> tick_;
};

}  // namespace tmport::dos
