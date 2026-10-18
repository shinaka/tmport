#pragma once

// Shared contract between the application shell (src/app), the screens (title, sign-in, ...) and
// the game modules: everything a scene needs, owned by the bootstrap and passed by reference.
// Owned by the "app" area; other areas may read it. Members that are still being built by other
// areas are pointers that may be null until wired up.

#include <functional>
#include <stdexcept>

namespace tmport {

class Engine;
class ExeTables;
class ResourceManager;
struct GameFiles;

namespace text { class Font; class TextRenderer; }
namespace ui { class DialogSystem; class MenuBar; }
namespace audio { class AudioSystem; }
namespace anim { class AnimEngine; }
namespace save { struct Paths; struct EngineSettings; }
namespace game { struct GameState; }

// Services of the game area that the app layer (menu commands, shutdown) calls. Every member may be
// empty. TODO(integration): GameMain (game area) fills these when it starts.
struct GameServices {
    std::function<void()> seeRank;            // 10f0:0487 Menu_SeeRank (File > See Rank)
    std::function<void()> saveCurrentPlayer;  // 1160:0f28 Users_SaveCurrent (Exit saves before quitting)
};

struct AppContext {
    Engine& engine;
    GameFiles& files;          // files.exe (ExeTables), files.res (ResourceManager)
    const text::Font& font;    // FFNT 999
    text::TextRenderer& text;  // Text_Render port, drawing into engine.frame()
    ui::DialogSystem& dialogs; // DlgDesc boxes, Yes/No, pager

    audio::AudioSystem* audio = nullptr;  // null when audio failed to initialise
    anim::AnimEngine* anim = nullptr;     // DAM engine
    ui::MenuBar* menu = nullptr;          // in-engine Win16-style menu bar
    game::GameState* state = nullptr;     // persistent player/climb state
    const save::Paths* paths = nullptr;   // pref path, users.dat location, ini
    save::EngineSettings* settings = nullptr;
    GameServices services{};              // filled by the game area
};

// Replacements for the original's longjmp(DS:5eda, n) (caught in GameMain at the place of the
// setjmp at 1118:004c, see re/docs/ARCHITECTURE.md §2.5 and PORTING_NOTES.md §3).
struct RestartToSignIn : std::exception {   // longjmp value 1: "Start Another Game"
    const char* what() const noexcept override { return "restart to sign-in"; }
};
struct QuitGame : std::exception {          // longjmp value 2: "Exit"
    const char* what() const noexcept override { return "quit game"; }
};

}  // namespace tmport
