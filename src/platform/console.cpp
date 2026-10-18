#include "platform/console.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <io.h>
#endif

#include <cstdint>
#include <cstdio>

namespace tmport::platform {

namespace {
ConsoleState g_state = ConsoleState::NotNeeded;

#if defined(_WIN32)
// A standard handle that already leads somewhere: a console, a file, a pipe or a device (NUL).
bool usable(DWORD which) {
    HANDLE h = GetStdHandle(which);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return false;
    return GetFileType(h) != FILE_TYPE_UNKNOWN;
}

// Reopens one CRT stream on the console device and makes it the process's standard handle too
// (SDL_Log writes to GetStdHandle(STD_ERROR_HANDLE) directly).
bool rebind(FILE* stream, DWORD which, const char* device, const char* mode) {
    FILE* f = nullptr;
    if (freopen_s(&f, device, mode, stream) != 0 || !f) return false;
    // Unbuffered output keeps printf and SDL_Log lines in order (the CRT has no line buffering).
    if (stream != stdin) std::setvbuf(stream, nullptr, _IONBF, 0);
    const intptr_t h = _get_osfhandle(_fileno(stream));
    if (h != -1) SetStdHandle(which, reinterpret_cast<HANDLE>(h));
    return true;
}
#endif
}  // namespace

ConsoleState attachParentConsole() {
#if defined(_WIN32)
    // A console program (tmtests, tmport-cli) has its console already, with or without a window.
    if (GetConsoleWindow() != nullptr || GetConsoleCP() != 0) return g_state = ConsoleState::NotNeeded;
    const bool in = usable(STD_INPUT_HANDLE), out = usable(STD_OUTPUT_HANDLE), err = usable(STD_ERROR_HANDLE);
    // Attach even when every stream is redirected (SDL_Log would attach on its first message anyway),
    // so Ctrl+C in the console reaches the program as it does for console programs.
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return g_state = (in && out && err) ? ConsoleState::NotNeeded : ConsoleState::None;
    bool outToConsole = false;
    if (!out) outToConsole = rebind(stdout, STD_OUTPUT_HANDLE, "CONOUT$", "w");
    if (!err) rebind(stderr, STD_ERROR_HANDLE, "CONOUT$", "w");
    if (!in) rebind(stdin, STD_INPUT_HANDLE, "CONIN$", "r");
    // cmd and PowerShell do not wait for GUI programs and have already printed their next prompt:
    // start the output on a fresh line.
    if (outToConsole) std::fputs("\n", stdout);
    return g_state = ConsoleState::Attached;
#else
    return g_state = ConsoleState::NotNeeded;
#endif
}

ConsoleState consoleState() { return g_state; }

}  // namespace tmport::platform
