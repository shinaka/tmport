#include "dos/audio/opl_log.h"

#include <format>
#include <span>

#include "util/file_util.h"

namespace tmport::dos {

std::string OplWriteLog::text() const {
    std::string t;
    for (const Entry& e : entries_) t += std::format("{} {:02x} {:02x}{}\n", e.tick, e.reg, e.value, e.command ? " c" : "");
    return t;
}

bool OplWriteLog::save(const std::string& path) const {
    if (!on_) return true;
    const std::string t = text();
    return writeWholeFile(pathFromUtf8(path), std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(t.data()), t.size()));
}

}  // namespace tmport::dos
