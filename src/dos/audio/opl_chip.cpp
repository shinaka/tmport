#include "dos/audio/opl_chip.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>

#include "ymfm_opl.h"

namespace tmport::dos {

namespace {

// The latency of the interpolation: output time t is rendered from the chip samples around t - 2,
// so an output sample never needs a chip sample after the current time.
constexpr double kLatency = 2.0;

}  // namespace

struct OplChip::Impl {
    Impl(double outputRate, double writeSeconds, double gain)
        : chip(intf), step(kChipRate / outputRate), writeSamples(writeSeconds * kChipRate), gain(gain) {
        chip.reset();
    }

    struct Write {
        double time;  // chip samples
        uint8_t reg, value;
    };

    ymfm::ymfm_interface intf;
    ymfm::ym3812 chip;
    double step;          // chip samples per output sample
    double writeSamples;  // chip samples per register write
    double gain;

    double now = 0.0;     // chip time of the next output sample (before the latency)
    double busy = 0.0;    // chip time when the last queued write is done
    uint64_t generated = 0;  // chip samples generated so far (the next one is sample `generated`)
    std::array<double, 4> hist{};  // the last four chip samples: hist[3] = sample generated - 1
    std::deque<Write> pending;

    void apply(const Write& w) {
        chip.write_address(w.reg);
        chip.write_data(w.value);
    }
    // Applies every write due by chip sample k.
    void applyUntil(double k) {
        while (!pending.empty() && pending.front().time <= k) {
            apply(pending.front());
            pending.pop_front();
        }
    }
    void generateOne() {
        applyUntil(static_cast<double>(generated));
        ymfm::ym3812::output_data o;
        chip.generate(&o, 1);
        hist = {hist[1], hist[2], hist[3], static_cast<double>(o.data[0])};
        ++generated;
    }
};

OplChip::OplChip(double outputRate, double writeSeconds, double gain)
    : impl_(std::make_unique<Impl>(outputRate, writeSeconds, gain)) {}

OplChip::~OplChip() = default;

void OplChip::oplWrite(uint8_t reg, uint8_t value) {
    Impl& m = *impl_;
    // Not before the chip sample that is generated next (an earlier time would be in the past).
    const double t = std::max({m.now, m.busy, static_cast<double>(m.generated)});
    m.pending.push_back({t, reg, value});
    m.busy = t + m.writeSamples;
}

void OplChip::render(double* out, size_t n) {
    Impl& m = *impl_;
    for (size_t i = 0; i < n; ++i) {
        const double t = m.now - kLatency;
        const double fl = std::floor(t);
        // samples fl-1 .. fl+2 are needed: generate up to sample fl+2 (= the last one in hist)
        while (static_cast<double>(m.generated) < fl + 3.0) m.generateOne();
        if (t >= 1.0) {
            // hist[3] = sample generated-1 = fl+2 (or later if a skip ran ahead): index from the end
            const double base = static_cast<double>(m.generated) - 4.0;  // time of hist[0]
            const double x = t - base - 1.0;                               // position between hist[1] and hist[2]
            const double y0 = m.hist[0], y1 = m.hist[1], y2 = m.hist[2], y3 = m.hist[3];
            // Catmull-Rom spline
            const double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
            const double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
            const double c = -0.5 * y0 + 0.5 * y2;
            const double v = ((a * x + b) * x + c) * x + y1;
            out[i] += v * m.gain;
        }
        m.now += m.step;
    }
}

void OplChip::skip(size_t n) {
    Impl& m = *impl_;
    m.now += m.step * static_cast<double>(n);
    const double t = std::floor(m.now - kLatency) + 3.0;
    if (static_cast<double>(m.generated) < t) {
        m.applyUntil(t);
        m.generated = static_cast<uint64_t>(t);
        m.hist.fill(0.0);
    }
}

}  // namespace tmport::dos
