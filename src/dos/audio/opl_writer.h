#pragma once
// The register interface of an OPL2 (YM3812): one write of a value to a register, in the driver's
// order. Drivers write to it (TM DOS: the AdLib driver of SST.EXE 4ac9, adlib_driver.h; G&G: the BMF
// driver's OPL class, ssg.exe 1bb4), the emulated chip (opl_chip.h) and the write log (opl_log.h)
// take it.

#include <cstdint>

namespace tmport::dos {

class OplWriter {
public:
    virtual ~OplWriter() = default;
    virtual void oplWrite(uint8_t reg, uint8_t value) = 0;
};

}  // namespace tmport::dos
