#pragma once
// The OPL output stage of the DOS sound renderer (chip_renderer.h): an emulated OPL2 (opl_chip.h) or
// a driver that owns one (TM DOS's AdLib driver, opl_backend.h) adds its samples at the renderer's
// rate.

#include <cstddef>

namespace tmport::dos {

class OplOutput {
public:
    virtual ~OplOutput() = default;
    // Adds n samples at the renderer's rate.
    virtual void render(double* out, size_t n) = 0;
    // Advances n samples without synthesis (a run whose sound is not kept).
    virtual void skip(size_t n) { (void)n; }
};

}  // namespace tmport::dos
