#pragma once
// File and string helpers. Paths given as std::string are UTF-8.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmport {

std::filesystem::path pathFromUtf8(std::string_view utf8);
std::string pathToUtf8(const std::filesystem::path& p);

// The whole file; nullopt if `p` is not a regular file or cannot be read.
std::optional<std::vector<uint8_t>> readWholeFile(const std::filesystem::path& p);
// Creates parent directories as needed, then creates or truncates `p` and writes `data`. True only
// if the write AND the final flush and close succeeded. Not atomic: a failure or a crash can leave
// `p` partly written (use writeFileAtomically for files that must survive that).
bool writeWholeFile(const std::filesystem::path& p, std::span<const uint8_t> data);

// Replaces `p` with `data` so that `p` holds either its old or its new contents, never a mix: the
// data goes to "<p>.tmp" next to it, which is flushed to the disk and closed (every step checked),
// then renamed over `p`. On failure `p` is unchanged, the temporary file is removed and false is
// returned. Creates parent directories as needed.
bool writeFileAtomically(const std::filesystem::path& p, std::span<const uint8_t> data);
// "<p>.bak", where replaceFileKeepingBackup() keeps the previous contents of `p`.
std::filesystem::path backupPathFor(const std::filesystem::path& p);
// writeFileAtomically(p, data), after saving the current contents of `p` to backupPathFor(p) (also
// atomically) when `p` exists, can be read, differs from `data` and `keep` (if given) accepts it,
// e.g. "is a valid file of its kind": the backup then always holds the last good version. A
// failure to write the backup is logged and does not stop the write.
bool replaceFileKeepingBackup(const std::filesystem::path& p, std::span<const uint8_t> data,
                              const std::function<bool(std::span<const uint8_t>)>& keep = {});

std::string toUpperAscii(std::string_view s);
std::string toLowerAscii(std::string_view s);
bool iequalsAscii(std::string_view a, std::string_view b);

// Value of an environment variable, if set and non-empty.
std::optional<std::string> getEnv(const char* name);

}  // namespace tmport
