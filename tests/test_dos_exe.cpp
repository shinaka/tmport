#include <doctest/doctest.h>

#include <stdexcept>
#include <vector>

#include "dos/data/dos_exe.h"
#include "dos_test_data.h"

using tmport::dos::DosExe;

TEST_CASE("DosExe: rejects files that are not the DOS SST.EXE") {
    const std::vector<uint8_t> junk(64, 0x41);
    CHECK_THROWS_AS(DosExe::load(junk), std::runtime_error);
    std::vector<uint8_t> mz(64, 0);
    mz[0] = 'M';
    mz[1] = 'Z';
    mz[4] = 1;  // one page, last page 0 = 512 bytes: longer than the file
    CHECK_THROWS_AS(DosExe::load(mz), std::runtime_error);
}

TEST_CASE("DosExe: unpacks the user's SST.EXE and resolves Ghidra addresses") {
    TM_REQUIRE_DOS_DATA();
    CHECK(dosExe->image().size() == DosExe::kImageSize);
    CHECK(dosExe->dsString(0x0004).starts_with("Turbo-C"));
    // The cell handler table DS:08A6 holds far pointers into code segment 1568 (code_world.md).
    const auto p = dosExe->readFarPtr(0x08A6);
    CHECK(p.seg == 0x1568);
    CHECK(dosExe->farPtrAtDs(0x08A6).data() == dosExe->at(0x1568, p.off).data());
    CHECK(dosExe->at(0x0fff, 0).empty());
}
