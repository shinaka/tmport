#include "gg/save/players_store.h"

#include <system_error>

#include "util/file_util.h"
#include "util/log.h"

namespace tmport::gg {

std::filesystem::path playersPathIn(const std::filesystem::path& prefDir) { return prefDir / kPlayersFileName; }

std::string_view describe(GgSaveMoment m) {
    switch (m) {
    case GgSaveMoment::Create: return "create";
    case GgSaveMoment::NewPlayer: return "new player";
    case GgSaveMoment::RemovePlayer: return "remove player";
    case GgSaveMoment::SaveAndExit: return "save and exit";
    case GgSaveMoment::LeaveRoom: return "vehicle done";
    case GgSaveMoment::SaveAndLeave: return "save and leave";
    case GgSaveMoment::PostRaceMenu: return "post-race menu";
    case GgSaveMoment::FinalMovie: return "final movie";
    }
    return "?";
}

std::string_view describe(PlayersStatus s) {
    switch (s) {
    case PlayersStatus::Ok: return "ok";
    case PlayersStatus::Created: return "created";
    case PlayersStatus::Recreated: return "re-created (the old file was kept as .bak)";
    case PlayersStatus::WriteFailed: return "could not be written";
    }
    return "?";
}

std::string_view describe(PlayersImport r) {
    switch (r) {
    case PlayersImport::Imported: return "imported";
    case PlayersImport::DestExists: return "the engine already has a players file";
    case PlayersImport::SourceMissing: return "no PLAYERS.DAT in the game folder";
    case PlayersImport::SourceInvalid: return "PLAYERS.DAT of the game folder is not valid";
    case PlayersImport::Failed: return "the players file could not be written";
    }
    return "?";
}

bool isValidPlayersDat(std::span<const uint8_t> bytes) {
    const auto d = PlayersDat::parse(bytes);
    return d && d->versionOk();
}

PlayersStatus PlayersStore::load() {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path_, ec);
    if (exists) {
        if (const auto bytes = readWholeFile(path_)) {
            if (auto d = PlayersDat::parse(*bytes); d && d->versionOk()) {
                file_ = std::move(*d);
                return status_ = PlayersStatus::Ok;
            }
        }
        log::warn("{}: not a valid players file (size, magic or version): re-created, the old one kept as .bak",
                  pathToUtf8(path_));
    }
    // Roster_LoadOrCreate 2caa:004f: open mode 1 failed (missing, or version != 12): create.
    file_ = PlayersDat::create();
    if (!writeImage(GgSaveMoment::Create)) return status_ = PlayersStatus::WriteFailed;
    return status_ = exists ? PlayersStatus::Recreated : PlayersStatus::Created;
}

PlayersStatus PlayersStore::loadWithImport(const std::optional<std::filesystem::path>& gameFolderPlayersDat,
                                           PlayersImport* imported) {
    PlayersImport r = PlayersImport::SourceMissing;
    std::error_code ec;
    if (gameFolderPlayersDat && !std::filesystem::exists(path_, ec)) r = importPlayers(*gameFolderPlayersDat, path_, false);
    else if (gameFolderPlayersDat) r = PlayersImport::DestExists;
    if (imported) *imported = r;
    return load();
}

PlayerRecord PlayersStore::loadRecord(int slot) const { return file_.records.at(static_cast<size_t>(slot)); }

bool PlayersStore::saveCurrent(GgSaveMoment moment, const PlayerRecord& rec, int rosterIndex, bool saveDisabled) {
    if (saveDisabled) return true;  // 2b5a:01ea: g_saveDisabled
    file_.records.at(static_cast<size_t>(rec.slot)) = rec;
    file_.roster.entries.at(static_cast<size_t>(rosterIndex)) = RosterEntry::ofRecord(rec);
    return writeImage(moment);
}

bool PlayersStore::saveRoster(GgSaveMoment moment, const Roster& roster) {
    file_.roster = roster;
    return writeImage(moment);
}

bool PlayersStore::writeImage(GgSaveMoment moment) {
    const auto bytes = file_.bytes();
    // The previous file is kept as .bak (whatever it was: a re-created file keeps the bad one).
    const bool ok = replaceFileKeepingBackup(path_, bytes);
    if (!ok) {
        log::error("{}: the players file could not be written ({})", pathToUtf8(path_), describe(moment));
        return false;
    }
    ++writes_;
    lastMoment_ = moment;
    return true;
}

PlayersImport importPlayers(const std::filesystem::path& source, const std::filesystem::path& dest, bool overwrite) {
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) return PlayersImport::SourceMissing;
    const auto bytes = readWholeFile(source);
    if (!bytes || !isValidPlayersDat(*bytes)) return PlayersImport::SourceInvalid;
    if (!overwrite && std::filesystem::exists(dest, ec)) return PlayersImport::DestExists;
    return replaceFileKeepingBackup(dest, *bytes) ? PlayersImport::Imported : PlayersImport::Failed;
}

bool exportPlayers(const std::filesystem::path& engineFile, const std::filesystem::path& dest) {
    const auto bytes = readWholeFile(engineFile);
    if (!bytes || !isValidPlayersDat(*bytes)) return false;
    return replaceFileKeepingBackup(dest, *bytes);
}

}  // namespace tmport::gg
