#pragma once
// Console attachment for the windowed (GUI-subsystem) Windows build of tmport.
//
// On Windows tmport.exe is linked as a GUI application so that a double-click in Explorer opens
// only the game window, without a console. When it is started from a console (cmd, PowerShell,
// Windows Terminal), the command-line tools and --headless runs must still print there:
// attachParentConsole() attaches to the parent's console (AttachConsole(ATTACH_PARENT_PROCESS))
// and rebinds stdin/stdout/stderr to it, but only those streams that are not already redirected
// to a file or a pipe (`tmport --info > out.txt`, MSYS/Git Bash pipes, CI logs keep working).
//
// Elsewhere (Linux, macOS, and console-subsystem Windows programs such as tmtests) it does
// nothing: those programs always have their terminal.

namespace tmport::platform {

enum class ConsoleState {
    NotNeeded,  // not Windows, a console program, or no parent console but every stream redirected
    Attached,   // attached to the parent's console; the streams that led nowhere now use it
    None,       // no parent console (started from Explorer) and no redirection: output goes nowhere
};

// Call once, first thing in main(), before anything prints or logs.
ConsoleState attachParentConsole();
// The result of the call above (NotNeeded before it).
ConsoleState consoleState();

}  // namespace tmport::platform
