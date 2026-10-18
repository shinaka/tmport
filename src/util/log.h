#pragma once
// Logging on top of SDL_Log (SDL_LogMessage, application category).

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace tmport::log {

enum class Level { Debug, Info, Warn, Error };

// Writes one message. Safe to call before SDL_Init.
void write(Level level, std::string_view message);

// Enables debug-level messages.
void setVerbose(bool verbose);
bool verbose();

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (verbose()) write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace tmport::log
