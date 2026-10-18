#include "util/file_util.h"

#include <SDL3/SDL_stdinc.h>

#include <fstream>
#include <system_error>

namespace tmport {

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
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
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
