#pragma once
// The OPL2 (YM3812) of an AdLib card, emulated by ymfm (BSD-3-Clause, pinned in
// cmake/sources_dos_audio.cmake): register writes go in at the time the driver makes them, the chip
// runs at its own rate (3579545 Hz / 72 = 49715.9 samples per second) and its output is resampled to
// the renderer's rate (4-point cubic interpolation, two chip samples of latency).
//
// Timing of the writes: the chip time advances with the rendered samples; a write is applied at the
// current time, or when the previous write has finished. Every AdLib_Write (491b:0875: OUT, 6 status
// reads, OUT, 35 status reads) takes `writeSeconds` (70 us on the reference machine: DOSBox-X at
// cycles=fixed 6000, from the .dro captures), so the writes of one PIT tick follow each other the way
// they reach the chip in the original. It matters for the key: AdLib_NoteOn keys a channel off and on
// again four writes later, and the chip must see the key off for the note to start anew (ymfm, like
// the real chip, samples the key state once per output sample).

#include <cstddef>
#include <cstdint>
#include <memory>

#include "dos/audio/adlib_driver.h"

namespace tmport::dos {

class OplChip final : public OplWriter {
public:
    static constexpr double kClockHz = 3579545.0;            // 14.31818 MHz / 4 on the AdLib card
    static constexpr double kChipRate = kClockHz / 72.0;    // samples per second
    static constexpr double kWriteSeconds = 70e-6;           // one AdLib_Write on the reference machine
    // Output level: ymfm's 16-bit output (a full-scale sine of one channel at total level 0 is about
    // +-4084) times this gives the renderer's level (the speaker square is +-0.5): the balance of the
    // DOSBox-X AdLib recordings, which mix the OPL and the speaker (the tunes' RMS against the
    // effects' peaks, fmt_sound.md §11.7).
    static constexpr double kDefaultGain = 1.0 / 9486.0;

    explicit OplChip(double outputRate, double writeSeconds = kWriteSeconds, double gain = kDefaultGain);
    ~OplChip() override;
    OplChip(const OplChip&) = delete;
    OplChip& operator=(const OplChip&) = delete;

    // OplWriter: queued at the current time (after the previous write has finished).
    void oplWrite(uint8_t reg, uint8_t value) override;

    // Adds n samples at the output rate.
    void render(double* out, size_t n);
    // Advances n output samples without synthesis (the queued writes are applied): for runs whose
    // sound is not kept. The chip's envelopes stand still meanwhile.
    void skip(size_t n);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tmport::dos
