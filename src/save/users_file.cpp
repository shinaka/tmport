#include "save/users_file.h"

#include <cstring>
#include <string>
#include <system_error>

#include "mischief/util/file_util.h"
#include "mischief/util/log.h"

namespace tmport::save {

void UsersTable::fromTableBytes(std::span<const uint8_t> table) {
    for (size_t i = 0; i < static_cast<size_t>(kSlotCount); ++i) {
        auto& s = slots[i].bytes;
        const size_t o = i * kSlotSize;
        if (o + kSlotSize <= table.size())
            std::memcpy(s.data(), table.data() + o, kSlotSize);
        else
            s.fill(0);
    }
    recount();
}

std::vector<uint8_t> UsersTable::tableBytes() const {
    std::vector<uint8_t> out(kTableSize);
    for (size_t i = 0; i < static_cast<size_t>(kSlotCount); ++i)
        std::memcpy(out.data() + i * kSlotSize, slots[i].bytes.data(), kSlotSize);
    return out;
}

std::vector<uint8_t> UsersTable::fileBytes() const {
    std::vector<uint8_t> out(kMagic.begin(), kMagic.end());
    const auto t = tableBytes();
    out.insert(out.end(), t.begin(), t.end());
    return out;
}

void UsersTable::recount() {
    count = 0;
    while (count < kSlotCount && !slots[static_cast<size_t>(count)].empty()) ++count;
}

void UsersTable::clear() {
    for (auto& s : slots) s.bytes.fill(0);
    count = 0;
}

namespace {

// FarStrCmp (1160:009f) == 0: unsigned byte compare up to the first NUL of either string.
bool nameEquals(const UserSlot& s, std::string_view name) {
    for (size_t k = 0;; ++k) {
        const uint8_t a = k < kSlotSize ? s.bytes[k] : 0;
        const uint8_t c = k < name.size() ? static_cast<uint8_t>(name[k]) : 0;
        if (a != c) return false;
        if (a == 0) return true;
    }
}

}  // namespace

int UsersTable::findByName(std::string_view name) const {
    for (int i = 0; i < count; ++i)
        if (nameEquals(slots[static_cast<size_t>(i)], name)) return i;
    return count;
}

void UsersTable::sortByTotal() {
    for (int i = 1; i < count; ++i) {
        const UserSlot key = slots[static_cast<size_t>(i)];
        int j = i;
        // An element moves up past entries whose total is strictly smaller (unsigned).
        while (j - 1 >= 0 && slots[static_cast<size_t>(j - 1)].totalTreasures() < key.totalTreasures()) {
            slots[static_cast<size_t>(j)] = slots[static_cast<size_t>(j - 1)];
            --j;
        }
        slots[static_cast<size_t>(j)] = key;
    }
}

bool parseUsersFile(std::span<const uint8_t> file, UsersTable& out) {
    if (file.size() < kFileSize) {
        out.clear();
        return false;
    }
    out.fromTableBytes(file.subspan(kHeaderSize, kTableSize));
    return true;
}

std::string_view describe(UsersFileStatus s) {
    switch (s) {
        case UsersFileStatus::Ok: return "valid";
        case UsersFileStatus::Missing: return "missing";
        case UsersFileStatus::Short: return "too short";
        default: return "unreadable";
    }
}

UsersFileStatus readUsersFileStatus(const std::filesystem::path& path, UsersTable& out) {
    out.clear();
    std::error_code ec;
    if (std::filesystem::status(path, ec).type() == std::filesystem::file_type::not_found)
        return UsersFileStatus::Missing;
    const auto data = mischief::readWholeFile(path);
    if (!data) return UsersFileStatus::Unreadable;  // exists (or cannot even be examined) but no read
    return parseUsersFile(*data, out) ? UsersFileStatus::Ok : UsersFileStatus::Short;
}

bool readUsersFile(const std::filesystem::path& path, UsersTable& out) {
    return readUsersFileStatus(path, out) == UsersFileStatus::Ok;
}

std::filesystem::path usersBackupPath(const std::filesystem::path& path) { return mischief::backupPathFor(path); }

RosterLoad loadRoster(const std::filesystem::path& path, UsersTable& out) {
    RosterLoad r;
    r.file = readUsersFileStatus(path, out);
    if (r.file == UsersFileStatus::Ok || r.file == UsersFileStatus::Missing) return r;
    const auto bak = usersBackupPath(path);
    UsersTable b;
    const UsersFileStatus bs = readUsersFileStatus(bak, b);
    if (bs == UsersFileStatus::Ok) {
        mischief::log::warn("users.dat: {} is {}; using the roster saved in {} ({} players)", mischief::pathToUtf8(path), describe(r.file),
                  mischief::pathToUtf8(bak), b.count);
        out = b;
        r.fromBackup = true;
    } else {
        mischief::log::error("users.dat: {} is {} and {} is {}: the roster cannot be read, and the file is not overwritten",
                   mischief::pathToUtf8(path), describe(r.file), mischief::pathToUtf8(bak), describe(bs));
    }
    return r;
}

bool writeUsersFile(const std::filesystem::path& path, const UsersTable& table) {
    const auto bytes = table.fileBytes();
    // The replaced file becomes users.dat.bak if it is a valid roster with at least one player.
    const auto keep = [](std::span<const uint8_t> old) {
        UsersTable t;
        return parseUsersFile(old, t) && t.count > 0;
    };
    if (!mischief::replaceFileKeepingBackup(path, bytes, keep)) {
        mischief::log::error("users.dat: writing {} failed (the file is unchanged)", mischief::pathToUtf8(path));
        return false;
    }
    return true;
}

bool mergePlayer(UsersTable& table, const PlayerRecord& player, int& sel) {
    const std::string name = player.name.substr(0, kMaxNameLength);
    int at = sel;
    if (at < 0 || at >= table.count || !nameEquals(table.slots[static_cast<size_t>(at)], name)) {
        // The original writes slot[sel] unconditionally. sel == count is a new player; anything else
        // means the file changed since sel was set (another instance, a deleted file, a failed write).
        const int found = table.findByName(name);  // count when absent
        if (at != table.count)
            mischief::log::warn("users.dat: slot {} does not hold {}; saving to slot {}", at, name, found);
        at = found;
    }
    if (at >= kSlotCount) {
        mischief::log::error("users.dat: the roster is full; {} cannot be saved", name);
        return false;
    }
    player.writeTo(table.slots[static_cast<size_t>(at)]);
    if (table.count <= at) ++table.count;  // appended at count
    sel = at;
    if (table.count > 1) {
        table.sortByTotal();
        sel = table.findByName(name);
    }
    return true;
}

SaveOutcome saveCurrentPlayer(const std::filesystem::path& path, const PlayerRecord& player, int& sel,
                              UsersTable* tableOut) {
    SaveOutcome r;
    if (!player.name.empty() && player.name[0] == kNoPlayerChar) return r;  // nobody signed in
    r.attempted = true;
    UsersTable table;
    const RosterLoad load = loadRoster(path, table);  // a missing file is an empty roster
    r.fromBackup = load.fromBackup;
    const bool placed = mergePlayer(table, player, sel);
    if (tableOut) *tableOut = table;
    if (!load.usable() || !placed) {
        r.refused = true;  // never replace a roster that could not be read with one that lacks it
        return r;
    }
    r.written = writeUsersFile(path, table);
    return r;
}

void removeSlot(UsersTable& table, int idx) {
    if (idx < 0) idx = 0;
    for (int k = idx + 1; k <= kSlotCount - 1; ++k)
        table.slots[static_cast<size_t>(k - 1)] = table.slots[static_cast<size_t>(k)];
    table.slots[static_cast<size_t>(kSlotCount - 1)].bytes[0] = 0;  // table offset 0x251A
    if (table.count != 0) --table.count;
}

bool removePlayer(const std::filesystem::path& path, UsersTable& table, int idx, bool confirmed) {
    if (confirmed) removeSlot(table, idx);
    return writeUsersFile(path, table);
}

}  // namespace tmport::save
