#pragma once
// Access to the user's DOS edition files for data-dependent tests.
//   TM_DATA_DOS = folder holding SST.EXE, the PAN/PAK files, ALL_PUZS, ... (the TM-DOS folder);
//   tests that need it skip without it.

#include <filesystem>
#include <optional>

#include "dos/data/dos_exe.h"
#include "dos/data/dos_files.h"

namespace testdata {

std::optional<std::filesystem::path> dosDataPath();
// Shared unpacked SST.EXE from TM_DATA_DOS (nullptr when TM_DATA_DOS is unset).
const tmport::dos::DosExe* dosExe();
// Shared DosFiles of TM_DATA_DOS: every data file, checked (nullptr when TM_DATA_DOS is unset).
const tmport::dos::DosFiles* dosFiles();

}  // namespace testdata

// Skips the rest of a test case (with a message) when TM_DATA_DOS is not set.
#define TM_REQUIRE_DOS_DATA()                                          \
    const auto* dosExe = testdata::dosExe();                           \
    if (!dosExe) {                                                     \
        MESSAGE("TM_DATA_DOS is not set: test skipped");               \
        return;                                                        \
    }                                                                  \
    (void)dosExe

// The same, defining a local 'dosFiles' pointer (the opened DOS folder).
#define TM_REQUIRE_DOS_FILES()                                         \
    const auto* dosFiles = testdata::dosFiles();                       \
    if (!dosFiles) {                                                   \
        MESSAGE("TM_DATA_DOS is not set: test skipped");               \
        return;                                                        \
    }                                                                  \
    (void)dosFiles
