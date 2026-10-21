#include "dos/save/users_store.h"

#include <system_error>

#include "mischief/util/file_util.h"
#include "mischief/util/log.h"
#include "save/settings.h"

namespace tmport::dos {

namespace {

namespace fs = std::filesystem;

bool wholeFile(std::span<const uint8_t> data) { return data.size() >= kUsersFileSize; }

bool fileExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

}  // namespace

fs::path dosUsersPath(const save::Paths& paths) { return paths.prefDir / fs::path(kDosUsersFileName); }

std::optional<fs::path> findDosUsersDat(const fs::path& dosFolder) {
    std::error_code ec;
    for (fs::directory_iterator it(dosFolder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (mischief::iequalsAscii(mischief::pathToUtf8(it->path().filename()), "USERS.DAT")) return it->path();
    }
    return std::nullopt;
}

std::string_view describe(DosSaveMoment m) {
    switch (m) {
        case DosSaveMoment::NewPlayer: return "new player";
        case DosSaveMoment::RemovePlayer: return "remove player";
        case DosSaveMoment::StartOver: return "start over";
        case DosSaveMoment::StartNewPlayer: return "start new player";
        case DosSaveMoment::ExitAndSave: return "exit and save";
        case DosSaveMoment::GameEnd: return "game end";
    }
    return "?";
}

std::string_view describe(DosUsersStatus s) {
    switch (s) {
        case DosUsersStatus::Ok: return "ok";
        case DosUsersStatus::Missing: return "missing";
        case DosUsersStatus::Short: return "shorter than 3243 bytes";
        case DosUsersStatus::Unreadable: return "unreadable";
    }
    return "?";
}

std::string_view describe(DosImportResult r) {
    switch (r) {
        case DosImportResult::Imported: return "imported";
        case DosImportResult::DestExists: return "the engine already has a DOS roster";
        case DosImportResult::SourceMissing: return "no USERS.DAT in the DOS folder";
        case DosImportResult::SourceInvalid: return "USERS.DAT is shorter than 3243 bytes or unreadable";
        case DosImportResult::Failed: return "write failed";
    }
    return "?";
}

// ---------------------------------------------------------------------------------- DosUsersStore

// 47c7:0452 Users_EnsureLoaded -> 47c7:0514 Users_ReadFile.
DosUsersStatus DosUsersStore::load() {
    users_ = UsersDat{};
    fromBackup_ = false;
    if (!fileExists(path_)) return status_ = DosUsersStatus::Missing;
    const auto data = mischief::readWholeFile(path_);
    if (data && wholeFile(*data)) {
        users_ = *UsersDat::parse(*data);
        return status_ = DosUsersStatus::Ok;
    }
    status_ = data ? DosUsersStatus::Short : DosUsersStatus::Unreadable;
    // Never start over from an empty roster on top of a damaged file: use the last good version.
    if (const auto bak = mischief::readWholeFile(mischief::backupPathFor(path_)); bak && wholeFile(*bak)) {
        users_ = *UsersDat::parse(*bak);
        fromBackup_ = true;
        mischief::log::warn("{}: {}; using the backup {}", mischief::pathToUtf8(path_), describe(status_), mischief::pathToUtf8(mischief::backupPathFor(path_)));
    } else {
        mischief::log::warn("{}: {}; the DOS roster will not be saved", mischief::pathToUtf8(path_), describe(status_));
    }
    return status_;
}

DosUsersStatus DosUsersStore::loadWithImport(const fs::path& dosFolder, DosImportOutcome* outcome) {
    DosImportOutcome o;
    if (fileExists(path_)) {
        o.result = DosImportResult::DestExists;
    } else if (const auto src = findDosUsersDat(dosFolder)) {
        o.source = *src;
        o.result = importDosUsers(*src, path_, false);
        if (o.result == DosImportResult::Imported)
            mischief::log::info("imported the DOS roster {} into {}", mischief::pathToUtf8(*src), mischief::pathToUtf8(path_));
    } else {
        o.result = DosImportResult::SourceMissing;
    }
    if (outcome) *outcome = o;
    return load();
}

// 47c7:055a Users_WriteFile (whole image; the original retries after a critical-error box).
bool DosUsersStore::write(DosSaveMoment moment) {
    if (!writable()) {
        mischief::log::error("{}: not written at '{}' ({}, no whole backup)", mischief::pathToUtf8(path_), describe(moment), describe(status_));
        return false;
    }
    const std::vector<uint8_t> data = users_.bytes();
    if (!mischief::replaceFileKeepingBackup(path_, data, wholeFile)) {
        mischief::log::error("{}: write failed at '{}'", mischief::pathToUtf8(path_), describe(moment));
        return false;
    }
    ++writes_;
    lastMoment_ = moment;
    // The file on disk is whole now, whatever it was before.
    status_ = DosUsersStatus::Ok;
    fromBackup_ = false;
    return true;
}

// 47c7:07b9 Users_AddPlayer: ... copy 9 bytes ba0a -> name; Users_WriteFile.
bool DosUsersStore::saveNewPlayer(int rec, std::string_view name) {
    record(rec).setName(name);
    return write(DosSaveMoment::NewPlayer);
}

// 47c7:13fa SignInDlg_RemoveYes -> 47c7:075d Users_ClearRecord.
bool DosUsersStore::removePlayer(int rec) {
    record(rec).clear();
    return write(DosSaveMoment::RemovePlayer);
}

// 43cb:028b MenuCmd_StartOver: record +9 = 0; Users_Pack; Users_WriteFile.
bool DosUsersStore::saveStartOver(int rec) {
    record(rec).savedGame = 0;
    return write(DosSaveMoment::StartOver);
}

// 43cb:02f8 MenuCmd_StartNewPlayer: record +9 = g_gameInProgress; Users_Pack; Users_WriteFile.
bool DosUsersStore::saveStartNewPlayer(int rec, bool gameInProgress) {
    record(rec).savedGame = gameInProgress ? 1 : 0;
    return write(DosSaveMoment::StartNewPlayer);
}

// 43cb:032c MenuCmd_ExitAndSave: record +9 = g_gameInProgress; Users_Pack; Users_WriteFile.
bool DosUsersStore::saveExitAndSave(int rec, bool gameInProgress) {
    record(rec).savedGame = gameInProgress ? 1 : 0;
    return write(DosSaveMoment::ExitAndSave);
}

// 47c7:09a2 Game_End: record +9 = 0; +0x15A = g_nets; +0x15C = g_coins; Users_Pack; Users_WriteFile.
bool DosUsersStore::saveGameEnd(int rec, uint16_t nets, uint16_t coins) {
    UserRecord& r = record(rec);
    r.savedGame = 0;
    r.bankedNets = nets;
    r.bankedCoins = coins;
    return write(DosSaveMoment::GameEnd);
}

// ------------------------------------------------------------------------------ import / export

DosImportResult importDosUsers(const fs::path& dosUsersDat, const fs::path& dest, bool overwrite) {
    if (!overwrite && fileExists(dest)) return DosImportResult::DestExists;
    if (!fileExists(dosUsersDat)) return DosImportResult::SourceMissing;
    const auto data = mischief::readWholeFile(dosUsersDat);
    if (!data || !wholeFile(*data)) return DosImportResult::SourceInvalid;
    const std::span<const uint8_t> whole(data->data(), kUsersFileSize);
    if (!mischief::replaceFileKeepingBackup(dest, whole, wholeFile)) return DosImportResult::Failed;
    return DosImportResult::Imported;
}

bool exportDosUsers(const fs::path& engineFile, const fs::path& dest) {
    const auto data = mischief::readWholeFile(engineFile);
    if (!data || !wholeFile(*data)) return false;
    return mischief::replaceFileKeepingBackup(dest, std::span<const uint8_t>(data->data(), kUsersFileSize), wholeFile);
}

}  // namespace tmport::dos
