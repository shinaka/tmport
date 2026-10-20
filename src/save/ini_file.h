#pragma once
// Minimal INI reader/writer for the engines' settings files (tmport.ini, save/settings.h): sections,
// key = value, ';' or '#' comments; section and key names are case-insensitive. Updating values
// keeps comments, unknown keys and the order of the user's file.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::save {

class IniFile {
public:
    static IniFile parse(std::string_view text);
    static std::optional<IniFile> load(const std::filesystem::path& path);
    // Atomic replace; the previous file is kept as "<path>.bak".
    bool save(const std::filesystem::path& path) const;
    std::string text() const;

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    // Updates the existing line (keeping its position) or appends the key to its section,
    // creating the section at the end when needed.
    void set(std::string_view section, std::string_view key, std::string_view value);

private:
    struct Line {
        std::string raw;      // the line as read (comments, blank lines, sections)
        std::string section;  // lower-case section this line belongs to
        std::string key;      // lower-case key; empty for non key lines
        std::string value;
        bool isSection = false;
    };
    std::vector<Line> lines_;
};

}  // namespace tmport::save
