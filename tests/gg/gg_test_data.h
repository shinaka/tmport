#pragma once
// Access to the user's Gizmos & Gadgets files for data-dependent tests (model: tests/dos_test_data.h).
//   GG_DATA     = the game folder (ssg.exe, gizmo000..003.dat, PLAYERS.DAT); default: the repository's
//                 GadgetsAndGizmos folder when it holds the game. Tests that need it skip without it.
//   GG_CAPTURES = the DOSBox-X reference material (extracted\captures\gg: original\, savestates\,
//                 r2\, ...; re/gg/docs/REFERENCE_CAPTURES.md). Tests that compare with it skip
//                 without it.
// Tests store sizes, counts, CRCs and coordinates only, never game text or pixels.

#include <filesystem>
#include <optional>

#include "gg/data/gg_files.h"

namespace testdata {

// GG_DATA, else <repo>/GadgetsAndGizmos when it looks like the game; nullopt otherwise.
std::optional<std::filesystem::path> ggDataPath();
// Shared GgFiles of the game folder (opened and checked once; nullptr without the data).
const tmport::gg::GgFiles* ggFiles();
// GG_CAPTURES when it names a folder; nullopt otherwise.
std::optional<std::filesystem::path> ggCapturesPath();
// A file of the captures (e.g. "savestates/w0/06_hub.players"); nullopt without GG_CAPTURES or when
// the file is missing.
std::optional<std::filesystem::path> ggCapture(const std::filesystem::path& relative);
// The repository root (for the checked-in docs such as re/gg/docs/overlay_map.tsv).
std::filesystem::path repoRoot();
// A new empty scratch folder for one test (under the system temp folder; removed by the caller or
// at the next run).
std::filesystem::path scratchDir(const char* name);

}  // namespace testdata

// Skips the rest of a test case (with a message) when the game folder is not available; defines
// `ggFiles` (the opened folder).
#define GG_REQUIRE_DATA()                                                  \
    const auto* ggFiles = testdata::ggFiles();                             \
    if (!ggFiles) {                                                        \
        MESSAGE("GG_DATA is not set and no GadgetsAndGizmos folder: test skipped"); \
        return;                                                            \
    }                                                                      \
    (void)ggFiles

// Skips the rest of a test case when GG_CAPTURES is not set; defines `captures` (the folder).
#define GG_REQUIRE_CAPTURES()                                              \
    const auto captures = testdata::ggCapturesPath();                      \
    if (!captures) {                                                       \
        MESSAGE("GG_CAPTURES is not set: test skipped");                   \
        return;                                                            \
    }                                                                      \
    (void)captures
