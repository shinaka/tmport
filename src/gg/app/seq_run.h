#pragma once
// The state machine and the exit paths of Gizmos & Gadgets (ssg.exe 12e0:000f Seq_Run, 1000:031e
// exit, 12e0:0b19 Game_Shutdown; re/gg/docs/code_runtime.md §3, §4).
//
// Seq_Run(table, &state) runs table[state] until a state returns -1 or the table entry is NULL:
//
//   r == -2   state = prev (prev is not updated: two -2 in a row stay on prev)
//   r == -1   stop, return -1
//   r ==  0   prev = state, state += 1
//   other     prev = state, state = r (also other negative values)
//
// main runs the 17-state table DS:00c2 (index 0 unused, entry 18 NULL); the attract loop DS:20a8
// and the workshop DS:219a are tables of their own. The far pointers of the tables are mapped to
// engine functions by the states' module (PORTING_NOTES.md §6).
//
// exit(code) unwinds to main as GgExit (the engine has no process exit inside the game), where the
// atexit functions run in reverse order of registration (only Game_Shutdown in the original).

#include <cstdint>
#include <exception>
#include <functional>
#include <span>
#include <vector>

namespace tmport::gg {

// A state function: returns 0 (next), -1 (stop), -2 (back) or the next state.
using StateFn = std::function<int16_t()>;

// 12e0:000f Seq_Run. An empty function, or a state outside the table (the original would read past
// it; no shipped state does), ends the run with 0 (logged when outside).
int16_t seqRun(std::span<const StateFn> table, int16_t& state);

// The process exit codes of ssg.exe (code_runtime.md §4).
namespace exit_code {
inline constexpr int kNormal = 0;    // the final movie, Save & Exit, Esc in the attract loop
inline constexpr int kStartup = 1;   // start-up refusals (usage, display, mouse, memory), Mem_Alloc
inline constexpr int kHeap = 2;      // corrupted far heap (Mem_Alloc, Mem_Free, Mem_CheckHeap)
inline constexpr int kAbort = 3;     // abort (also assert) and the heap checks
inline constexpr int kOverlay = 4;   // overlay error (_c0_OvrRuntimeError 1000:027b)
inline constexpr int kNo286 = 5;     // CPU below 286 (ssg.bat reports it); dropped by the engine
}  // namespace exit_code

// exit(code) 1000:031e as an exception: caught where the program ends (the app), which then runs the
// atexit functions.
struct GgExit : std::exception {
    int code = 0;
    explicit GgExit(int c) : code(c) {}
    const char* what() const noexcept override { return "exit"; }
};

[[noreturn]] void gameExit(int code);

// The atexit list of the Borland RTL (__exit 1000:02c7 runs it in reverse order of registration).
class ExitHandlers {
public:
    void atExit(std::function<void()> fn) { fns_.push_back(std::move(fn)); }
    // Runs and clears them, newest first; an exception of one is logged and the others still run.
    void run();
    size_t size() const { return fns_.size(); }

private:
    std::vector<std::function<void()>> fns_;
};

}  // namespace tmport::gg
