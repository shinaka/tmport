#pragma once
// A mod folder of the 1990 DOS edition (docs/MODDING.md, "The 1990 DOS edition"):
//
//   <dir>/mod.json                  optional: {"name": "...", "resizable": ["SPRITES.PAK:72", "BEGIN.PAK:31-35"]}
//   <dir>/PAK/<STEM>/<k>.png        frame k of <STEM>.PAK (SPRITES, BEGIN, CLOSE, SHACK)
//   <dir>/PAK/<STEM>.json           frame hotspots and header words: {"frames": {"72": {"x": -3, "y": -20}}}
//   <dir>/PAN/<STEM>/<k>.png        picture k of <STEM>.PAN (OPENING: 0; the scenery files: blocks)
//   <dir>/SOUND/<NAME>.txt          TUNES.IBM, SFX.IBM, TUNES.TAN, SFX.TAN as note text
//   <dir>/TEXT/<NAME>.txt           ALL_PUZS, PRG_DICT, HELP.DAT as text
//   <dir>/EXE/strings.json          SST.EXE strings by DS offset
//   <dir>/EXE/palettes.json         the looks' colour tables in SST.EXE
//   <dir>/FILES/<NAME>              a whole data file in its original binary form
//
// Folder and file names are matched case-insensitively; the targets are resolved against the
// EXE's file table when the folder is applied (dos/mods/dos_mods.h).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "save/settings.h"

namespace tmport::dos {

enum class DosModFileKind { RawFile, PakFrame, PakHeaders, PanPicture, SoundText, TextFile, Strings, Palettes };

struct DosModFile {
    DosModFileKind kind = DosModFileKind::RawFile;
    std::string target;  // the data file it changes, upper case ("SPRITES.PAK"); "SST.EXE" for EXE/*
    int index = -1;      // frame or picture number
    std::filesystem::path path;
};

// mod.json "resizable": "SPRITES.PAK:72", "BEGIN.PAK:31-35" or "SHACK.PAK:*".
struct DosResizeRule {
    std::string file;  // upper case
    int lo = 0, hi = 0;
};

struct DosModManifest {
    std::string name;
    std::vector<DosResizeRule> resizable;
    bool frameResizable(std::string_view file, int frame) const;
};

struct DosModFolder {
    std::filesystem::path dir;
    DosModManifest manifest;
    std::vector<DosModFile> files;      // sorted by kind, target, index
    std::vector<std::string> warnings;  // unrecognised names
    std::vector<std::string> errors;    // a broken mod.json
};

// Scans a folder; a missing folder gives an empty DosModFolder.
DosModFolder scanDosModFolder(const std::filesystem::path& dir);
// Parses mod.json. Throws mischief::FormatError.
DosModManifest parseDosManifest(std::string_view json);

// The DOS mod folders of an ini file (nullptr = none): [mods] enabled = false gives none; [mods]
// dos_paths = a ';'-separated list (relative to the ini's folder); else <pref>/dos_mods and
// <executable>/dos_mods. Later folders override earlier ones.
std::vector<std::filesystem::path> dosModDirsFromIni(const mischief::save::IniFile* ini, const std::filesystem::path& iniDir,
                                                     const std::filesystem::path& prefDir, const std::filesystem::path& exeDir);
// The same for the paths' tmport.ini, then `extra` (--mods) in order.
std::vector<std::filesystem::path> configuredDosModDirs(const save::Paths& paths, const std::vector<std::filesystem::path>& extra = {});

}  // namespace tmport::dos
