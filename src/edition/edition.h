#pragma once
// Edition detection (re/dos/docs/SCOPING.md §5.1): the engine plays two editions of the game from
// the user's files, and a data path tells which one it holds.
//
//   WinCd    the Windows CD edition (v1.01): a folder with TMTWINCD.EXE, a folder with a TMTWINCD\
//            subfolder, or the CD image (.cue/.bin/.iso), as before.
//   Dos1990  the 1990 DOS edition (V 1.0): a folder with SST.EXE, SPRITES.PAK and ALL_PUZS whose
//            SST.EXE passes dos::DosExe::loadFile (EXEPACK, image size, Turbo C copyright, palette
//            sanity); anything else is refused with DosExe's message.
//
// A path may hold both (a parent folder with TM-DOS\ and TMTWINCD\, or with a CD image): then
// --edition dos|win or [engine] edition in tmport.ini chooses, otherwise detectEdition throws an
// error that names both.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "util/error.h"

namespace tmport {

enum class Edition : uint8_t { WinCd, Dos1990 };

// "the Windows CD edition" / "the 1990 DOS edition" (messages).
std::string_view editionName(Edition e);
// "win" / "dos" (--edition, [engine] edition).
std::string_view editionKey(Edition e);
// win | wincd | windows -> WinCd, dos | dos1990 -> Dos1990 (case-insensitive); nullopt otherwise
// (callers treat "auto" and "" as no preference).
std::optional<Edition> parseEditionKey(std::string_view text);

// The files that identify the DOS edition (SCOPING §5.1).
inline constexpr std::string_view kDosExeName = "SST.EXE";
inline constexpr std::string_view kDosMarkerFiles[] = {"SST.EXE", "SPRITES.PAK", "ALL_PUZS"};

struct EditionCandidate {
    Edition edition = Edition::WinCd;
    std::filesystem::path path;  // DOS: the folder holding SST.EXE; Windows: the game folder or the CD image
    std::string problem;         // why it cannot be played (empty: usable)
    std::string missingFile;     // the file a problem is about, if one is missing
    bool usable() const { return problem.empty(); }
};

// What a data path holds, looking at the path itself and its immediate subfolders:
//   win: <path> is a CD image; <path> holds TMTWINCD.EXE; <path>\TMTWINCD holds it; or <path>
//        holds exactly one .cue / .iso file.
//   dos: <path> is SST.EXE itself, or <path> or one of its subfolders holds SST.EXE (the first
//        usable one by name); checked with checkDosFolder.
struct EditionScan {
    std::optional<EditionCandidate> win;
    std::optional<EditionCandidate> dos;
};
EditionScan scanEditions(const std::filesystem::path& path);

// Checks a DOS folder: SST.EXE, SPRITES.PAK and ALL_PUZS present (case-insensitive), SST.EXE
// accepted by dos::DosExe::loadFile.
EditionCandidate checkDosFolder(const std::filesystem::path& dir);

// A data path that cannot be played; what() is the message for the player.
class EditionError : public Error {
public:
    explicit EditionError(const std::string& message, std::string file = {}) : Error(message), file_(std::move(file)) {}
    const std::string& file() const { return file_; }

private:
    std::string file_;
};

// Chooses the edition of a data path; preferred = --edition / [engine] edition (nullopt: auto).
//   preferred Dos1990: the DOS candidate (throws when there is none or it is not usable).
//   preferred WinCd:   the Windows candidate's path (or the path as given when none was found;
//                      the Windows start-up then names the missing file).
//   auto:              both usable -> throws, naming both; only a DOS candidate -> it (throws
//                      with its problem when not usable); otherwise WinCd with the path as given
//                      (exactly the Windows start-up of before).
EditionCandidate detectEdition(const std::filesystem::path& path, std::optional<Edition> preferred = std::nullopt);

// Auto-detection of a DOS folder (the Windows one is vfs::autoDetectDataPath): the current
// directory and its 2 parents, then the executable's directory and its 3 parents, each as the
// folder itself or one of its subfolders holding SST.EXE.
std::optional<std::filesystem::path> autoDetectDosPath(const std::filesystem::path& exeDir);

}  // namespace tmport
