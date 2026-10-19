#include "edition/edition.h"

#include <algorithm>
#include <format>
#include <system_error>
#include <vector>

#include "dos/data/dos_exe.h"
#include "util/file_util.h"
#include "util/log.h"
#include "vfs/data_source.h"

namespace tmport {

namespace {

// Case-insensitive lookup of <dir>/<name> (a file, or a folder with wantDir).
std::optional<std::filesystem::path> findEntry(const std::filesystem::path& dir, std::string_view name, bool wantDir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return std::nullopt;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!iequalsAscii(pathToUtf8(it->path().filename()), name)) continue;
        std::error_code ec2;
        if (it->is_directory(ec2) == wantDir) return it->path();
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> subfolders(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (it->is_directory(ec2)) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string extensionOf(const std::filesystem::path& p) { return toLowerAscii(pathToUtf8(p.extension())); }

bool isImageFile(const std::filesystem::path& p) {
    const std::string ext = extensionOf(p);
    return ext == ".cue" || ext == ".bin" || ext == ".iso" || ext == ".img";
}

std::optional<EditionCandidate> scanWindows(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        if (isImageFile(path)) return EditionCandidate{Edition::WinCd, path, {}, {}};
        return std::nullopt;
    }
    if (findEntry(path, vfs::kGameExeName, false)) return EditionCandidate{Edition::WinCd, path, {}, {}};
    if (auto sub = findEntry(path, vfs::kGameFolderName, true); sub && findEntry(*sub, vfs::kGameExeName, false))
        return EditionCandidate{Edition::WinCd, *sub, {}, {}};
    std::vector<std::filesystem::path> images;
    for (std::filesystem::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        const std::string ext = extensionOf(it->path());
        if (ext == ".cue" || ext == ".iso") images.push_back(it->path());
    }
    if (images.size() == 1) return EditionCandidate{Edition::WinCd, images[0], {}, {}};
    return std::nullopt;
}

std::optional<EditionCandidate> scanDos(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        if (iequalsAscii(pathToUtf8(path.filename()), kDosExeName)) return checkDosFolder(path.parent_path());
        return std::nullopt;
    }
    if (findEntry(path, kDosExeName, false)) return checkDosFolder(path);
    std::optional<EditionCandidate> first;
    for (const auto& sub : subfolders(path)) {
        if (!findEntry(sub, kDosExeName, false)) continue;
        EditionCandidate c = checkDosFolder(sub);
        if (c.usable()) return c;
        if (!first) first = std::move(c);
    }
    return first;
}

std::string howToChoose() {
    return "Choose one with  --edition win  or  --edition dos,  or write  edition = win  (or dos) in the "
           "[engine] section of tmport.ini.";
}

}  // namespace

std::string_view editionName(Edition e) { return e == Edition::Dos1990 ? "the 1990 DOS edition" : "the Windows CD edition"; }

std::string_view editionKey(Edition e) { return e == Edition::Dos1990 ? "dos" : "win"; }

std::optional<Edition> parseEditionKey(std::string_view text) {
    const std::string t = toLowerAscii(text);
    if (t == "win" || t == "wincd" || t == "windows") return Edition::WinCd;
    if (t == "dos" || t == "dos1990") return Edition::Dos1990;
    return std::nullopt;
}

EditionCandidate checkDosFolder(const std::filesystem::path& dir) {
    EditionCandidate c{Edition::Dos1990, dir, {}, {}};
    for (std::string_view f : kDosMarkerFiles) {
        if (findEntry(dir, f, false)) continue;
        c.missingFile = std::string(f);
        c.problem = std::format("A file of the 1990 DOS edition is missing: {}\n\nDOS game folder: {}", f, pathToUtf8(dir));
        return c;
    }
    const auto exe = findEntry(dir, kDosExeName, false);
    try {
        (void)dos::DosExe::loadFile(*exe);
    } catch (const std::exception& e) {
        c.missingFile = std::string(kDosExeName);
        c.problem = std::format("{} cannot be used: {}.\n\nOnly the 1990 DOS edition, version 1.0, is supported.\n\nDOS game folder: {}",
                                kDosExeName, e.what(), pathToUtf8(dir));
    }
    return c;
}

EditionScan scanEditions(const std::filesystem::path& path) {
    EditionScan s;
    s.win = scanWindows(path);
    s.dos = scanDos(path);
    return s;
}

EditionCandidate detectEdition(const std::filesystem::path& path, std::optional<Edition> preferred) {
    const EditionScan s = scanEditions(path);
    if (preferred == Edition::Dos1990) {
        if (!s.dos)
            throw EditionError(std::format("The 1990 DOS edition was not found in {}: there is no {} in that folder "
                                           "or in one of its subfolders.",
                                           pathToUtf8(path), kDosExeName),
                               std::string(kDosExeName));
        if (!s.dos->usable()) throw EditionError(s.dos->problem, s.dos->missingFile);
        return *s.dos;
    }
    if (preferred == Edition::WinCd) return s.win ? *s.win : EditionCandidate{Edition::WinCd, path, {}, {}};

    if (s.dos && s.dos->usable() && s.win)
        throw EditionError(std::format("{} holds two editions of Treasure Mountain!:\n"
                                       "  - the Windows CD edition: {}\n"
                                       "  - the 1990 DOS edition: {}\n\n{}",
                                       pathToUtf8(path), pathToUtf8(s.win->path), pathToUtf8(s.dos->path), howToChoose()));
    if (s.dos && !s.win) {
        if (!s.dos->usable()) throw EditionError(s.dos->problem, s.dos->missingFile);
        return *s.dos;
    }
    if (s.dos) log::warn("{} (ignored: playing the Windows CD edition in {})", s.dos->problem, pathToUtf8(path));
    // The Windows start-up of before: the path as given (GameFiles names what is missing).
    return EditionCandidate{Edition::WinCd, path, {}, {}};
}

std::optional<std::filesystem::path> autoDetectDosPath(const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> bases;
    std::error_code ec;
    auto addWithParents = [&](std::filesystem::path p, int levels) {
        for (int i = 0; i <= levels && !p.empty(); ++i) {
            bases.push_back(p);
            const auto parent = p.parent_path();
            if (parent == p) break;
            p = parent;
        }
    };
    addWithParents(std::filesystem::current_path(ec), 2);
    if (!exeDir.empty()) addWithParents(exeDir, 3);
    for (const auto& b : bases) {
        if (findEntry(b, kDosExeName, false)) return b;
        for (const auto& sub : subfolders(b))
            if (findEntry(sub, kDosExeName, false)) return sub;
    }
    return std::nullopt;
}

}  // namespace tmport
