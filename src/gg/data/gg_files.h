#pragma once
// GgFiles: the user's Gizmos & Gadgets folder (ssg.exe, gizmo000..003.dat, optional PLAYERS.DAT),
// found, opened and checked once at start-up (re/gg/docs/PORTING_NOTES.md §1, fmt_data.md §1).
//
//  * File names are looked up without regard to case.
//  * ssg.exe must be the DOS v1.0 program (GgExe: size, MZ, TLINK, Borland signature, overlays) and
//    the four archives the v1.0 volumes (Archive::checkVolume: magic and digit, offset table, entry
//    counts, the 0xA55A integrity entries). A missing file throws tmport::MissingFileError naming
//    it; a broken one tmport::FormatError whose message starts with the file name.
//  * gizmo004.dat is the original's run-time swap file (a screen save-under, fmt_data.md §9): it is
//    ignored, never read or written. ssg.bat, Read_1st.txt and ssg.ico are ignored too.
//  * PLAYERS.DAT is optional: it is only imported into the engine's own copy (gg/save).
// The engine never writes into the game folder.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "gg/data/archive.h"
#include "gg/data/gg_exe.h"

namespace tmport::gg {

// Where detectGgFolder found the folder.
enum class GgFolderSource : uint8_t {
    Explicit,    // the --data argument (or the settings)
    Environment, // GG_DATA
    NearProgram, // the program's folder or one of its 3 parents, or a GadgetsAndGizmos child of them
};
std::string_view describe(GgFolderSource s);

struct GgFolder {
    std::filesystem::path path;
    GgFolderSource source = GgFolderSource::Explicit;
};

class GgFiles {
public:
    static constexpr std::string_view kExeName = "ssg.exe";
    static constexpr std::string_view kPlayersName = "PLAYERS.DAT";
    static constexpr std::string_view kSwapName = "gizmo004.dat";  // ignored
    static constexpr std::string_view kDefaultFolderName = "GadgetsAndGizmos";

    // Loads and checks ssg.exe and the four archives of `folder` (see above).
    static GgFiles open(const std::filesystem::path& folder);

    // True when `folder` holds ssg.exe and gizmo000.dat (nothing is read or validated).
    static bool looksLikeGgFolder(const std::filesystem::path& folder);

    const std::filesystem::path& folder() const { return folder_; }
    const GgExe& exe() const { return exe_; }
    const Archive& archive() const { return archive_; }

    // PLAYERS.DAT of the game folder with its real spelling; nullopt if absent.
    std::optional<std::filesystem::path> playersDatPath() const { return pathOf(kPlayersName); }
    // Any file of the folder by name (case-insensitive); nullopt if absent.
    std::optional<std::filesystem::path> pathOf(std::string_view name) const;

private:
    GgFiles(std::filesystem::path folder, GgExe exe, Archive archive);

    std::filesystem::path folder_;
    GgExe exe_;
    Archive archive_;
};

// Finds the game folder, in this order (the first that looks like a G&G folder wins, nothing is
// opened): `explicitPath` (returned even when it does not look right, so the caller reports the
// error against what the user asked for), the GG_DATA environment variable, then `programDir`
// and its 3 parents, each itself and its child folder named GadgetsAndGizmos (any case).
std::optional<GgFolder> detectGgFolder(const std::optional<std::filesystem::path>& explicitPath,
                                       const std::filesystem::path& programDir);

}  // namespace tmport::gg
