#pragma once
// The mod kit of the 1990 DOS edition (tmport --export-mod-kit <dir> --edition dos): the user's own
// SST.EXE and data files in the editable formats of docs/MODDING.md ("The 1990 DOS edition"), laid
// out as a DOS mod folder (dos/mods/dos_mod_folder.h). With verification on, the kit is applied as a
// mod over the original files through the same code as a real mod and every file and the DGROUP must
// come back byte for byte; the encoders are also run on every frame and picture without shortcuts
// (the PAK packer must reproduce each frame's bytes, the PAN packer each whole file).
//
// The kit is the user's private copy of game data: it is written only to the directory the user
// names and must never be distributed or committed anywhere.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace tmport::dos {

class DosFiles;

struct DosModKitOptions {
    bool verify = true;  // re-import the kit and compare with the original files
    bool force = false;  // allow a non-empty output directory (files are overwritten)
};

struct DosModKitReport {
    std::map<std::string, size_t> counts;  // files per folder ("PAK", "PAN", "SOUND", "TEXT", "EXE")
    size_t files = 0;
    uint64_t bytes = 0;
    size_t verified = 0;                   // data files (and SST.EXE's DGROUP) that came back byte for byte
    size_t framesReproduced = 0, frames = 0;      // PAK frames the packer reproduces
    size_t picturesReproduced = 0, pictureFiles = 0;  // PAN files the packer reproduces
    std::vector<std::string> failures;     // round-trip mismatches and errors
};

// `files` must hold the original files (DosFiles::open(folder, nullptr)). Throws mischief::Error if
// the directory cannot be used (exists and is not empty without force).
DosModKitReport exportDosModKit(const DosFiles& files, const std::filesystem::path& outDir, const DosModKitOptions& options = {});

}  // namespace tmport::dos
