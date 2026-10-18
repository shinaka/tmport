#include "util/file_util.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <fstream>
#include <system_error>

#include "util/log.h"

namespace tmport {

namespace {

void createParentDirectories(const std::filesystem::path& p) {
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
}

// Creates/truncates `p`, writes `data`, optionally flushes it to the disk (SDL_FlushIO calls
// FlushFileBuffers / fdatasync), and closes it. Every step is checked (REVIEW_1 H-1: the old
// std::ofstream version reported success before its destructor wrote the last buffer).
bool writeChecked(const std::filesystem::path& p, std::span<const uint8_t> data, bool syncToDisk) {
    SDL_IOStream* io = SDL_IOFromFile(pathToUtf8(p).c_str(), "wb");
    if (!io) {
        log::debug("cannot create {}: {}", pathToUtf8(p), SDL_GetError());
        return false;
    }
    bool ok = data.empty() || SDL_WriteIO(io, data.data(), data.size()) == data.size();
    if (ok && syncToDisk) ok = SDL_FlushIO(io);
    if (!ok) log::debug("writing {} failed: {}", pathToUtf8(p), SDL_GetError());
    if (!SDL_CloseIO(io)) {
        log::debug("closing {} failed: {}", pathToUtf8(p), SDL_GetError());
        ok = false;
    }
    return ok;
}

}  // namespace

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    std::u8string u(utf8.size(), u8'\0');
    for (size_t i = 0; i < utf8.size(); ++i) u[i] = static_cast<char8_t>(utf8[i]);
    return std::filesystem::path(u);
}

std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::optional<std::vector<uint8_t>> readWholeFile(const std::filesystem::path& p) {
    // Regular files only: on POSIX a directory can be opened as a stream and report a bogus size.
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) return std::nullopt;
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return std::nullopt;
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(n));
    if (n > 0 && !f.read(reinterpret_cast<char*>(data.data()), n)) return std::nullopt;
    return data;
}

bool writeWholeFile(const std::filesystem::path& p, std::span<const uint8_t> data) {
    createParentDirectories(p);
    return writeChecked(p, data, false);
}

bool writeFileAtomically(const std::filesystem::path& p, std::span<const uint8_t> data) {
    createParentDirectories(p);
    std::filesystem::path tmp = p;
    tmp += ".tmp";
    // Removes a partly written temporary file (never anything else that has its name).
    const auto discardTmp = [&tmp] {
        std::error_code ec;
        if (std::filesystem::is_regular_file(tmp, ec)) std::filesystem::remove(tmp, ec);
    };
    if (!writeChecked(tmp, data, true)) {
        discardTmp();
        return false;
    }
    // SDL_RenamePath replaces an existing target: rename() on POSIX, MoveFileExW with
    // MOVEFILE_REPLACE_EXISTING on Windows.
    if (!SDL_RenamePath(pathToUtf8(tmp).c_str(), pathToUtf8(p).c_str())) {
        log::debug("cannot replace {}: {}", pathToUtf8(p), SDL_GetError());
        discardTmp();
        return false;
    }
    return true;
}

std::filesystem::path backupPathFor(const std::filesystem::path& p) {
    std::filesystem::path b = p;
    b += ".bak";
    return b;
}

bool replaceFileKeepingBackup(const std::filesystem::path& p, std::span<const uint8_t> data,
                              const std::function<bool(std::span<const uint8_t>)>& keep) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) {
        const auto old = readWholeFile(p);
        if (old && !std::ranges::equal(*old, data) && (!keep || keep(*old)) &&
            !writeFileAtomically(backupPathFor(p), *old))
            log::warn("cannot keep the previous {} as {}", pathToUtf8(p), pathToUtf8(backupPathFor(p)));
    }
    return writeFileAtomically(p, data);
}

std::string toUpperAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool iequalsAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
        if (x != y) return false;
    }
    return true;
}

std::optional<std::string> getEnv(const char* name) {
    // SDL_getenv_unsafe reads the process environment (UTF-8 on Windows).
    const char* v = SDL_getenv_unsafe(name);
    if (!v || !*v) return std::nullopt;
    return std::string(v);
}

}  // namespace tmport
