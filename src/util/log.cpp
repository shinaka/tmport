#include "util/log.h"

#include <SDL3/SDL_log.h>

#include <string>

namespace tmport::log {

namespace {
bool g_verbose = false;
bool g_prioritiesSet = false;

void ensurePriorities() {
    if (g_prioritiesSet) return;
    g_prioritiesSet = true;
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION,
                       g_verbose ? SDL_LOG_PRIORITY_DEBUG : SDL_LOG_PRIORITY_INFO);
}
}  // namespace

void setVerbose(bool v) {
    g_verbose = v;
    g_prioritiesSet = true;
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, v ? SDL_LOG_PRIORITY_DEBUG : SDL_LOG_PRIORITY_INFO);
}

bool verbose() { return g_verbose; }

void write(Level level, std::string_view message) {
    ensurePriorities();
    SDL_LogPriority prio = SDL_LOG_PRIORITY_INFO;
    switch (level) {
        case Level::Debug: prio = SDL_LOG_PRIORITY_DEBUG; break;
        case Level::Info: prio = SDL_LOG_PRIORITY_INFO; break;
        case Level::Warn: prio = SDL_LOG_PRIORITY_WARN; break;
        case Level::Error: prio = SDL_LOG_PRIORITY_ERROR; break;
    }
    const std::string text(message);
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, prio, "%s", text.c_str());
}

}  // namespace tmport::log
