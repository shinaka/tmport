#include "save/users_file.h"

#include <cstring>

#include "util/file_util.h"
#include "util/log.h"

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

int UsersTable::findByName(std::string_view name) const {
    for (int i = 0; i < count; ++i) {
        // FarStrCmp: unsigned byte compare up to the first NUL of either string.
        const auto& b = slots[static_cast<size_t>(i)].bytes;
        size_t k = 0;
        for (;; ++k) {
            const uint8_t a = k < kSlotSize ? b[k] : 0;
            const uint8_t c = k < name.size() ? static_cast<uint8_t>(name[k]) : 0;
            if (a != c || a == 0) {
                if (a == c) return i;
                break;
            }
        }
    }
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

bool readUsersFile(const std::filesystem::path& path, UsersTable& out) {
    auto data = readWholeFile(path);
    if (!data) {
        out.clear();
        return false;
    }
    return parseUsersFile(*data, out);
}

bool writeUsersFile(const std::filesystem::path& path, const UsersTable& table) {
    const auto bytes = table.fileBytes();
    if (!writeWholeFile(path, bytes)) {
        log::error("users.dat: writing {} failed", pathToUtf8(path));
        return false;
    }
    return true;
}

void mergePlayer(UsersTable& table, const PlayerRecord& player, int& sel) {
    if (sel < 0 || sel >= kSlotCount) {
        // Cannot happen in the original (the list is full at 7 players); keep the table intact.
        log::warn("users.dat: player index {} out of range", sel);
        return;
    }
    player.writeTo(table.slots[static_cast<size_t>(sel)]);
    if (table.count <= sel) ++table.count;
    if (table.count > 1) {
        table.sortByTotal();
        sel = table.findByName(player.name.substr(0, kMaxNameLength));
    }
}

SaveOutcome saveCurrentPlayer(const std::filesystem::path& path, const PlayerRecord& player, int& sel,
                              UsersTable* tableOut) {
    SaveOutcome r;
    if (!player.name.empty() && player.name[0] == kNoPlayerChar) return r;  // nobody signed in
    r.attempted = true;
    UsersTable table;
    readUsersFile(path, table);  // a missing/short file is an empty roster
    mergePlayer(table, player, sel);
    r.written = writeUsersFile(path, table);
    if (tableOut) *tableOut = table;
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
