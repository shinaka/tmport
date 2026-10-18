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
// Users_ReadFile (1158:0040) + recount. False (empty table) if the file is missing or short.
bool readUsersFile(const std::filesystem::path& path, UsersTable& out);
// Users_WriteFile (1158:00bb): creates the directories, writes header + table. False on failure
// (the original then shows its "Write Error" box; the caller decides what to show).
bool writeUsersFile(const std::filesystem::path& path, const UsersTable& table);

// Merge `player` into the table read from disk exactly like Users_SaveCurrent (1160:0f28) does,
// without file I/O: `table` must hold the freshly read file. Updates sel (DS:6171).
void mergePlayer(UsersTable& table, const PlayerRecord& player, int& sel);

struct SaveOutcome {
    bool attempted = false;  // false: nobody signed in (name starts with '_'), nothing done
    bool written = false;    // the file was written successfully
};
// Users_SaveCurrent: re-read path, merge, write. `table` (optional) receives the table as written.
SaveOutcome saveCurrentPlayer(const std::filesystem::path& path, const PlayerRecord& player, int& sel,
                              UsersTable* table = nullptr);

// The table part of SignIn_RemovePlayer (1160:089f) when the player confirmed.
void removeSlot(UsersTable& table, int idx);
// SignIn_RemovePlayer: removeSlot() if confirmed, then write the in-memory table in any case.
bool removePlayer(const std::filesystem::path& path, UsersTable& table, int idx, bool confirmed);

}  // namespace tmport::save
