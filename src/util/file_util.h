#pragma once
// File and string helpers. Paths given as std::string are UTF-8.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmport {

std::filesystem::path pathFromUtf8(std::string_view utf8);
std::string pathToUtf8(const std::filesystem::path& p);

std::optional<std::vector<uint8_t>> readWholeFile(const std::filesystem::path& p);
// Creates parent directories as needed.
bool writeWholeFile(const std::filesystem::path& p, std::span<const uint8_t> data);

std::string toUpperAscii(std::string_view s);
std::string toLowerAscii(std::string_view s);
bool iequalsAscii(std::string_view a, std::string_view b);

// Value of an environment variable, if set and non-empty.
std::optional<std::string> getEnv(const char* name);

}  // namespace tmport
