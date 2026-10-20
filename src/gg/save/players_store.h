#pragma once
// The engine's PLAYERS.DAT: where it lives, when it is written, import from the game folder and
// export back (re/gg/docs/PORTING_NOTES.md §9, code_ui.md §11.3, §12, fmt_data.md §8).
//
//  * The file keeps the original format byte for byte (gg/save/players_dat.h) and lives in the
//    engine's preference directory as "players.dat" (the caller passes that directory: the G&G
//    app's own, separate from Treasure Mountain's files). The game folder is never written.
//  * The store keeps the file image in memory and changes exactly the bytes the original writes,
//    at the original's moments, then writes the image (atomically, the previous file kept as .bak):
//      - Players_SaveCurrent (2b5a:01ea): the current record to its slot and its first 22 bytes to
//        index entry g_rosterIndex (saveCurrent);
//      - Roster_Save (2caa:0145): the sign-in's roster copy, the first 0x8A0 bytes (saveRoster);
//      - Roster_LoadOrCreate (2caa:004f): the whole file when it is missing or its version is not
//        12 (load).
//    So the bytes on disk equal what the original's in-place writes would leave.
//  * The callers (wave 3) keep the game state in the record (g_player) and the roster copy
//    (g_roster) as the original does, and pack the building block before a save while a building
//    is in progress (Players_SaveCurrent's g_inBuilding branch: PlayerRecord::blocks).
//
// Deliberate deviations (the format and the moments are unchanged): writes are atomic; the original
// deletes players.dat and exits when a write fails (2b5a:010a): the store reports the failure and
// keeps the file; a file of the wrong size, magic or version is re-created as the original does,
// but the old one is kept as "<file>.bak"; a missing or broken file at a load of a record is an
// error the caller reports instead of exit(1).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "gg/save/players_dat.h"

namespace tmport::gg {

inline constexpr std::string_view kPlayersFileName = "players.dat";
// <prefDir>/players.dat.
std::filesystem::path playersPathIn(const std::filesystem::path& prefDir);

// The original's write moments (code_ui.md §12.5, §11.3, §11.5).
enum class GgSaveMoment : uint8_t {
    Create,          // Roster_LoadOrCreate 2caa:004f: the file is created (99 blank records)
    NewPlayer,       // State_02_SignIn 2caa:1351: Players_SaveCurrent, then Roster_Save
    RemovePlayer,    // Ctrl+R in the sign-in (2caa:082e): Roster_Save only
    SaveAndExit,     // Cmd_SaveAndExit 4106:00c5 (menu or Esc, Yes), also from the post-race menu
    LeaveRoom,       // State_08_LeaveRoom 12e0:09aa: the vehicle is done, before the race
    SaveAndLeave,    // State_16_SaveAndLeave 12e0:012f: Start New Player / Return to Technology Center
    PostRaceMenu,    // State_13_PostRaceMenu 2fcb:0617: leaving the post-race menu after a win
    FinalMovie,      // State_15_FinalMovie 2fcb:06e0: after the movie
};
std::string_view describe(GgSaveMoment m);

enum class PlayersStatus : uint8_t {
    Ok,         // a valid file was read
    Created,    // there was no file: a new one was created and written
    Recreated,  // the file had the wrong size, magic or version: re-created (the old one kept as .bak)
    WriteFailed,// the file had to be created but could not be written (the image is in memory)
};
std::string_view describe(PlayersStatus s);

enum class PlayersImport : uint8_t {
    Imported,       // copied byte for byte
    DestExists,     // the engine already has a file and overwrite was not asked: nothing done
    SourceMissing,  // no PLAYERS.DAT in the game folder
    SourceInvalid,  // wrong size, magic or version
    Failed,         // the engine's file could not be written
};
std::string_view describe(PlayersImport r);

class PlayersStore {
public:
    explicit PlayersStore(std::filesystem::path file) : path_(std::move(file)) {}
    const std::filesystem::path& path() const { return path_; }

    // Roster_LoadOrCreate (2caa:004f) on the engine's file: reads it, or creates it (see above).
    PlayersStatus load();
    // load(), but first imports the game folder's PLAYERS.DAT (gameFolderPlayersDat, e.g.
    // GgFiles::playersDatPath()) when the engine has no file yet.
    PlayersStatus loadWithImport(const std::optional<std::filesystem::path>& gameFolderPlayersDat,
                                 PlayersImport* imported = nullptr);
    PlayersStatus status() const { return status_; }

    // The file image as it is on disk after the last write.
    const PlayersDat& file() const { return file_; }
    // Roster_LoadOrCreate's read: the sign-in's private copy g_roster.
    Roster roster() const { return file_.roster; }
    // Players_LoadRecord (2b5a:0198): the record of a slot (0..98). Throws std::out_of_range.
    PlayerRecord loadRecord(int slot) const;

    // Players_SaveCurrent (2b5a:01ea): `rec` to slot rec.slot and its 22-byte entry to index entry
    // `rosterIndex`, then the file is written. Does nothing and returns true while `saveDisabled`
    // (g_saveDisabled DS:1f7a, the sign-in's Esc path). False when the write failed.
    bool saveCurrent(GgSaveMoment moment, const PlayerRecord& rec, int rosterIndex, bool saveDisabled = false);
    // Roster_Save (2caa:0145): the header and index from the sign-in's copy, then the file is written.
    bool saveRoster(GgSaveMoment moment, const Roster& roster);

    // Moments written so far (diagnostics and tests).
    int writes() const { return writes_; }
    std::optional<GgSaveMoment> lastMoment() const { return lastMoment_; }

private:
    bool writeImage(GgSaveMoment moment);

    std::filesystem::path path_;
    PlayersDat file_;
    PlayersStatus status_ = PlayersStatus::Created;
    int writes_ = 0;
    std::optional<GgSaveMoment> lastMoment_;
};

// The checks of a PLAYERS.DAT before it is imported or used: 578 487 bytes, USR 0x1A, version 12.
bool isValidPlayersDat(std::span<const uint8_t> bytes);
// Copies a PLAYERS.DAT (e.g. from the game folder) to dest, byte for byte (atomic; a replaced dest
// is kept as .bak). Without overwrite nothing happens when dest exists.
PlayersImport importPlayers(const std::filesystem::path& source, const std::filesystem::path& dest, bool overwrite);
// Writes the engine's file back as a PLAYERS.DAT at dest (atomic, a replaced file is kept as .bak).
// False if the engine's file is not valid or the write fails.
bool exportPlayers(const std::filesystem::path& engineFile, const std::filesystem::path& dest);

}  // namespace tmport::gg
