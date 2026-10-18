#pragma once
// users.dat: the player roster and saved games, byte-compatible with the original
// (re/docs/sys_resources_saves.md §6, ARCHITECTURE.md §2.11/§4.3, PORTING_NOTES.md §4.10).
//
// File = "USR\x1A" (written, never checked) + 7 slots of 0x62F bytes = 11 085 bytes.
//  * Load (1158:0040 + 1160:0dd6): read the 4-byte header and exactly 11 081 bytes; a missing or
//    short file is an empty (zeroed) roster; extra bytes are ignored. The player count is the
//    number of leading slots with a non-empty name (at most 7).
//  * Save of the signed-in player (1160:0f28): re-read the file, copy the player's fields into
//    slot[sel] (bytes the game never writes keep the file's values), count++ if sel >= count,
//    stable insertion sort of slots 0..count-1 by total treasures (descending, unsigned), re-find
//    sel by byte-exact name, write header + table.
//  * Remove Player (1160:089f): on Yes shift slots idx+1..6 down, clear slot 6's first byte,
//    count--; the file is written even after No.
//
// Deliberate robustness deviations (REVIEW_1 H-1, M-1; the file format and the save order are
// unchanged, and so is the result whenever the original's assumptions hold):
//  * Every write is atomic (users.dat.tmp, flushed and closed, then renamed over users.dat), and the
//    file being replaced is kept as users.dat.bak when it was a valid roster.
//  * A missing users.dat is an empty roster, as in the original. A users.dat that exists but cannot
//    be read or is short is never overwritten from an empty roster: the roster comes from
//    users.dat.bak if that is valid, otherwise the save is refused (the caller shows the "Write
//    Error" box, DS:2084).
//  * The merge writes the player at sel only if that slot still holds the player's name; otherwise
//    it re-finds the name, or appends at count (never behind empty slots).

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "save/user_record.h"

namespace tmport::save {

struct UsersTable {
    std::array<UserSlot, kSlotCount> slots{};
    int count = 0;  // DS:6170

    // The 0x2B49-byte table (no header).
    void fromTableBytes(std::span<const uint8_t> table);
    std::vector<uint8_t> tableBytes() const;
    // Header + table, as written by the original.
    std::vector<uint8_t> fileBytes() const;
    // 1160:0dd6: count = leading slots with name[0] != 0, at most 7.
    void recount();
    void clear();
    // Index of the first slot in 0..count-1 whose name equals `name` byte for byte (FarStrCmp
    // 1160:009f); count when there is none.
    int findByName(std::string_view name) const;
    // Stable insertion sort of slots 0..count-1 by total treasures, descending (1160:0f28).
    void sortByTotal();

    bool operator==(const UsersTable&) const = default;
};

// Users_ReadFile semantics on bytes already read: false (and an empty table) unless at least
// 4 + 11 081 bytes are present.
bool parseUsersFile(std::span<const uint8_t> file, UsersTable& out);

enum class UsersFileStatus {
    Ok,          // read and at least 11 085 bytes long
    Missing,     // no such file (the original's "empty roster")
    Short,       // exists but is shorter than 11 085 bytes (e.g. an interrupted write)
    Unreadable,  // exists but cannot be opened or read
};
std::string_view describe(UsersFileStatus s);
// Users_ReadFile (1158:0040) + recount, telling apart why a file did not load. `out` is the
// (recounted) table for Ok and an empty table otherwise.
UsersFileStatus readUsersFileStatus(const std::filesystem::path& path, UsersTable& out);
// Users_ReadFile (1158:0040) + recount. False (empty table) if the file is missing or short.
bool readUsersFile(const std::filesystem::path& path, UsersTable& out);
// users.dat.bak next to `path`.
std::filesystem::path usersBackupPath(const std::filesystem::path& path);

// The roster as the game should see it: users.dat; if it exists but cannot be read or is short,
// users.dat.bak; else an empty table.
struct RosterLoad {
    UsersFileStatus file = UsersFileStatus::Missing;  // status of users.dat itself
    bool fromBackup = false;                          // the table came from users.dat.bak
    // The table may replace users.dat: it was read from it, or from the backup, or users.dat does
    // not exist (empty roster, as the original).
    bool usable() const { return file == UsersFileStatus::Ok || file == UsersFileStatus::Missing || fromBackup; }
};
RosterLoad loadRoster(const std::filesystem::path& path, UsersTable& out);

// Users_WriteFile (1158:00bb): creates the directories, writes header + table atomically, keeping
// the replaced file as users.dat.bak if it was a valid roster. False on failure (the file is then
// unchanged; the original shows its "Write Error" box, the caller decides what to show).
bool writeUsersFile(const std::filesystem::path& path, const UsersTable& table);

// Merge `player` into the table read from disk like Users_SaveCurrent (1160:0f28) does, without file
// I/O: `table` must hold the freshly read file. Updates sel (DS:6171). The player goes to slot sel
// if it holds the player's name, else to the slot with that name, else it is appended at count.
// False (table unchanged) only when the player is not in a full roster (7 others).
bool mergePlayer(UsersTable& table, const PlayerRecord& player, int& sel);

struct SaveOutcome {
    bool attempted = false;   // false: nobody signed in (name starts with '_'), nothing done
    bool written = false;     // the file was written successfully
    bool fromBackup = false;  // users.dat was damaged; the roster was rebuilt from users.dat.bak
    bool refused = false;     // not written: users.dat is damaged and there is no valid backup, or
                              // the roster is full of other players
};
// Users_SaveCurrent: re-read path (loadRoster), merge, write. `table` (optional) receives the table
// as merged (and written, when written is true).
SaveOutcome saveCurrentPlayer(const std::filesystem::path& path, const PlayerRecord& player, int& sel,
                              UsersTable* table = nullptr);

// The table part of SignIn_RemovePlayer (1160:089f) when the player confirmed.
void removeSlot(UsersTable& table, int idx);
// SignIn_RemovePlayer: removeSlot() if confirmed, then write the in-memory table in any case.
bool removePlayer(const std::filesystem::path& path, UsersTable& table, int idx, bool confirmed);

}  // namespace tmport::save
