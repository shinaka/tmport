#pragma once
// A data source that serves some files of the game folder from mod folders (the MIDI tunes,
// PORTING_NOTES.md §7.4) and everything else from the user's game data. Installed in place of
// GameFiles::source by mods::applyMods, so every MIDI track loader picks the overrides up.

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "vfs/data_source.h"

namespace tmport::mods {

class ModDataSource final : public vfs::DataSource {
public:
    // files: game-relative path (any case, '/' or '\') -> file on disk.
    ModDataSource(std::unique_ptr<vfs::DataSource> base, const std::map<std::string, std::filesystem::path>& files);

    std::string describe() const override;
    std::shared_ptr<vfs::File> open(std::string_view relPath) const override;
    std::vector<vfs::DirEntry> list(std::string_view relDir) const override { return base_->list(relDir); }

    const vfs::DataSource& base() const { return *base_; }
    // True if relPath is served from a mod file.
    bool overrides(std::string_view relPath) const { return files_.contains(key(relPath)); }
    // The normalised key of a game-relative path ("SOUNDS\\Eopen1.mid" -> "SOUNDS/EOPEN1.MID").
    static std::string key(std::string_view relPath);

private:
    std::unique_ptr<vfs::DataSource> base_;
    std::map<std::string, std::filesystem::path> files_;
};

}  // namespace tmport::mods
