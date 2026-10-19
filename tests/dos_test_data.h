#pragma once
// Access to the user's DOS edition files for data-dependent tests.
//   TM_DATA_DOS     = folder holding SST.EXE, the PAN/PAK files, ALL_PUZS, ... (the TM-DOS folder);
//                     tests that need it skip without it.
//   TM_DOS_CAPTURES = the DOSBox-X reference material of the original (extracted\captures\dos: ega\,
//                     tandy\, savestates\, USERS_*.DAT, re/dos/docs/REFERENCE_CAPTURES.md); tests
//                     that compare with it skip without it.

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
// TM_DOS_CAPTURES when it names a folder; nullopt otherwise (the capture-dependent tests skip).
std::optional<std::filesystem::path> dosCapturesPath();
// A file of the captures (e.g. "savestates/r2/fl_walk2.sav"); nullopt without TM_DOS_CAPTURES or when
// the file is missing.
std::optional<std::filesystem::path> dosCapture(const std::filesystem::path& relative);

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

// Skips the rest of a test case when TM_DOS_CAPTURES is not set; defines `captures` (the folder).
#define TM_REQUIRE_DOS_CAPTURES()                                      \
    const auto captures = testdata::dosCapturesPath();                 \
    if (!captures) {                                                   \
        MESSAGE("TM_DOS_CAPTURES is not set: test skipped");           \
        return;                                                        \
    }                                                                  \
    (void)captures
