#include "save/ini_file.h"

#include <cstdint>
#include <span>

#include "util/file_util.h"

namespace tmport::save {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

}  // namespace

// ------------------------------------------------------------------------------------- IniFile

IniFile IniFile::parse(std::string_view text) {
    IniFile ini;
    std::string section;
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF && static_cast<uint8_t>(text[1]) == 0xBB &&
        static_cast<uint8_t>(text[2]) == 0xBF)
        text.remove_prefix(3);  // UTF-8 BOM
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view raw = text.substr(pos, end - pos);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        {
            Line line;
            line.raw = std::string(raw);
            const std::string_view t = trim(raw);
            if (!t.empty() && t.front() == '[' && t.find(']') != std::string_view::npos) {
                section = toLowerAscii(trim(t.substr(1, t.find(']') - 1)));
                line.isSection = true;
                line.section = section;
            } else {
                line.section = section;
                if (!t.empty() && t.front() != ';' && t.front() != '#') {
                    const size_t eq = t.find('=');
                    if (eq != std::string_view::npos) {
                        line.key = toLowerAscii(trim(t.substr(0, eq)));
                        line.value = std::string(trim(t.substr(eq + 1)));
                    }
                }
            }
            ini.lines_.push_back(std::move(line));
        }
        pos = end + 1;
    }
    return ini;
}

std::optional<IniFile> IniFile::load(const std::filesystem::path& path) {
    auto data = readWholeFile(path);
    if (!data) return std::nullopt;
    return parse(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
}

std::string IniFile::text() const {
    std::string out;
    for (const auto& l : lines_) {
        out += l.raw;
        out += '\n';
    }
    return out;
}

bool IniFile::save(const std::filesystem::path& path) const {
    const std::string t = text();
    // Atomic, keeping the replaced file as <name>.bak (REVIEW_1 H-1).
    return replaceFileKeepingBackup(path, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(t.data()), t.size()));
}

std::optional<std::string> IniFile::get(std::string_view section, std::string_view key) const {
    const std::string s = toLowerAscii(section), k = toLowerAscii(key);
    for (const auto& l : lines_)
        if (!l.isSection && l.section == s && l.key == k) return l.value;
    return std::nullopt;
}

void IniFile::set(std::string_view section, std::string_view key, std::string_view value) {
    const std::string s = toLowerAscii(section), k = toLowerAscii(key);
    Line line;
    line.section = s;
    line.key = k;
    line.value = std::string(value);
    line.raw = std::string(key) + " = " + std::string(value);
    for (auto& l : lines_)
        if (!l.isSection && l.section == s && l.key == k) {
            l = line;
            return;
        }
    // Insert after the last non-blank line of the section.
    int header = -1, lastLine = -1;
    for (size_t i = 0; i < lines_.size(); ++i) {
        const Line& l = lines_[i];
        if (l.isSection && l.section == s && header < 0) header = static_cast<int>(i);
        if (l.section == s && (header >= 0 || s.empty()) && !trim(l.raw).empty()) lastLine = static_cast<int>(i);
    }
    if (header < 0 && !s.empty()) {
        if (!lines_.empty() && !trim(lines_.back().raw).empty()) lines_.push_back(Line{});
        Line h;
        h.raw = "[" + std::string(section) + "]";
        h.section = s;
        h.isSection = true;
        lines_.push_back(h);
        lines_.push_back(line);
        return;
    }
    const size_t at = lastLine < 0 ? (header < 0 ? 0 : static_cast<size_t>(header) + 1) : static_cast<size_t>(lastLine) + 1;
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(at), line);
}

}  // namespace tmport::save
