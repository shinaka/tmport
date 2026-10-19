#pragma once
// The engine's copy of the DOS USERS.DAT: where it lives, when it is written, import from the DOS
// folder and export back (PORTING_NOTES.md §5, code_ui.md §9.7-9.9, §5.4).
//
//  * The file keeps the DOS format byte for byte (dos/save/users_dat.h) and lives in the engine's
//    preference directory as "dos_users.dat", separate from the Windows edition's users.dat.
//  * It is written at the original's moments only (Users_WriteFile 47c7:055a): new player, remove
//    player, File > Start Over, File > Start New Player, File > Exit and Save, and Game_End after each
//    game; never at level changes or while playing. The callers (wave 3 sign-in and menus) pack the
//    game state into the selected record first, as the original does with Users_Pack (47c7:0e0e),
//    which also stores DS:929F into UsersDat::spriteHighWater.
//  * Import copies the DOS folder's USERS.DAT into the engine's file (automatically when the engine
//    has none yet); export writes the engine's file back as a USERS.DAT for DOSBox.
//
// Deliberate robustness deviations (the file format and the write moments are unchanged): every
// write is atomic ("<file>.tmp" renamed over the file) and keeps the replaced file as "<file>.bak"
// when it was a whole file; a file that exists but is short or unreadable is never overwritten from
// an empty roster (the roster comes from the .bak when that is whole, otherwise writes are refused).
// The original reads a short file into a zeroed image and overwrites it at the next write.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "dos/save/users_dat.h"

namespace tmport::save {
struct Paths;
}

namespace tmport::dos {

inline constexpr std::string_view kDosUsersFileName = "dos_users.dat";
// <prefDir>/dos_users.dat (save::Paths::prefDir, SDL_GetPrefPath("TMPort", "TMPort")).
std::filesystem::path dosUsersPath(const save::Paths& paths);
// USERS.DAT in the DOS game folder, found without regard to case; nullopt if absent.
std::optional<std::filesystem::path> findDosUsersDat(const std::filesystem::path& dosFolder);

// The original's write moments (PORTING_NOTES.md §5; code_ui.md §5.4, §9.6, §9.7, §9.9).
enum class DosSaveMoment : uint8_t {
    NewPlayer,       // Users_AddPlayer 47c7:07b9
    RemovePlayer,    // SignInDlg_RemoveYes 47c7:13fa -> Users_ClearRecord 47c7:075d
    StartOver,       // MenuCmd_StartOver 43cb:028b (the saved game is dropped)
    StartNewPlayer,  // MenuCmd_StartNewPlayer 43cb:02f8
    ExitAndSave,     // MenuCmd_ExitAndSave 43cb:032c
    GameEnd,         // Game_End 47c7:09a2
};
std::string_view describe(DosSaveMoment m);

enum class DosUsersStatus : uint8_t {
    Ok,          // a whole file was read
    Missing,     // no file: an empty roster (the original's zeroed image)
    Short,       // exists but holds fewer than 3243 bytes
    Unreadable,  // exists but cannot be read
};
std::string_view describe(DosUsersStatus s);

enum class DosImportResult : uint8_t {
    Imported,       // copied (the first 3243 bytes, unchanged)
    DestExists,     // the engine already has a file and overwrite was not asked: nothing done
    SourceMissing,  // no USERS.DAT in the DOS folder
    SourceInvalid,  // shorter than 3243 bytes or unreadable
    Failed,         // the engine's file could not be written
};
std::string_view describe(DosImportResult r);
struct DosImportOutcome {
    DosImportResult result = DosImportResult::SourceMissing;
    std::filesystem::path source;
};

class DosUsersStore {
public:
    explicit DosUsersStore(std::filesystem::path file) : path_(std::move(file)) {}
    const std::filesystem::path& path() const { return path_; }

    // Users_EnsureLoaded / Users_ReadFile (47c7:0452 / 0514) on the engine's file.
    DosUsersStatus load();
    // load(), but first imports the DOS folder's USERS.DAT when the engine has no file yet.
    DosUsersStatus loadWithImport(const std::filesystem::path& dosFolder, DosImportOutcome* outcome = nullptr);
    DosUsersStatus status() const { return status_; }
    bool fromBackup() const { return fromBackup_; }
    // False when the file on disk is damaged and no whole backup exists: write() refuses then.
    bool writable() const { return status_ == DosUsersStatus::Ok || status_ == DosUsersStatus::Missing || fromBackup_; }

    UsersDat& users() { return users_; }
    const UsersDat& users() const { return users_; }
    UserRecord& record(int i) { return users_.records.at(static_cast<size_t>(i)); }

    // Users_WriteFile (47c7:055a): the whole image, at one of the original's write moments.
    bool write(DosSaveMoment moment);

    // The record part of each moment, then write(). `rec` is the selected record (g_userRec DS:9228).
    // NewPlayer: the caller picked users().firstFreeRecord(), ran Users_InitNewPlayer + Users_Pack
    // into it; this copies the 9-byte name (47c7:07b9) and writes.
    bool saveNewPlayer(int rec, std::string_view name);
    // RemovePlayer: Users_ClearRecord (360 zero bytes, answer masks 7), write. The original then
    // re-reads the file (DS:6169 = 0); the in-memory image already equals it.
    bool removePlayer(int rec);
    // StartOver: record +9 = 0 (after Users_Pack by the caller), write.
    bool saveStartOver(int rec);
    // StartNewPlayer / ExitAndSave: record +9 = g_gameInProgress (DS:9205) (after Users_Pack), write.
    bool saveStartNewPlayer(int rec, bool gameInProgress);
    bool saveExitAndSave(int rec, bool gameInProgress);
    // GameEnd: record +9 = 0, +0x15A = g_nets, +0x15C = g_coins (banked for the next game), then
    // Users_Pack by the caller (it does not touch these fields, so the order is free), write.
    bool saveGameEnd(int rec, uint16_t nets, uint16_t coins);

    // Moments written so far (diagnostics and tests).
    int writes() const { return writes_; }
    std::optional<DosSaveMoment> lastMoment() const { return lastMoment_; }

private:
    std::filesystem::path path_;
    UsersDat users_{};
    DosUsersStatus status_ = DosUsersStatus::Missing;
    bool fromBackup_ = false;
    int writes_ = 0;
    std::optional<DosSaveMoment> lastMoment_;
};

// Copies a DOS USERS.DAT (e.g. from the game folder) to dest, byte for byte (atomic; a replaced
// dest is kept as .bak). Without overwrite nothing happens when dest exists.
DosImportResult importDosUsers(const std::filesystem::path& dosUsersDat, const std::filesystem::path& dest,
                               bool overwrite);
// Writes the engine's file back as a DOS USERS.DAT at dest (3243 bytes, atomic, a replaced file is
// kept as .bak). False if the engine's file is not whole or the write fails.
bool exportDosUsers(const std::filesystem::path& engineFile, const std::filesystem::path& dest);

}  // namespace tmport::dos
