#pragma once
// Scripted input for automated runs (--keys): a list of steps fed to the engine as SDL events, one
// step per input fetch, so the game, its dialogs and the menu bar see exactly what a player's
// keyboard and mouse would produce (Engine::postEvent). Works headless.
//
// Script: comma-separated steps.
//   enter esc space tab backspace up down left right home end pgup pgdn insert delete f1..f12
//   alt (a lone Alt press) a..z 0..9          key press (down + up); a..z also type the letter
//   alt+<key> ctrl+<key> shift+<key>           with modifiers (combinable: ctrl+alt+x)
//   click:<x>:<y>   move:<x>:<y>               left click / pointer move at canvas coordinates
//   wait:<ms>                                  no step until the time has passed
//   shot:<file.png>                            write the 640x480 canvas
//   quit                                       throw QuitGame (end the game)

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tmport {
class Engine;
enum class PumpPhase : uint8_t;
}  // namespace tmport

namespace tmport::app {

struct ScriptStep {
    enum class Kind { Key, Click, Move, Wait, Shot, Quit };
    Kind kind = Kind::Key;
    uint32_t keycode = 0;   // SDL_Keycode
    uint32_t scancode = 0;  // SDL_Scancode
    uint16_t mod = 0;       // SDL_Keymod
    std::string text;       // text input sent with the key (printable keys without Alt/Ctrl)
    int x = 0, y = 0;       // Click / Move (canvas)
    uint32_t ms = 0;        // Wait
    std::filesystem::path path;  // Shot
};

std::optional<std::vector<ScriptStep>> parseScript(std::string_view text, std::string& error);

class ScriptInput {
public:
    // Installs a pump hook on the engine. quitAtEnd: throw QuitGame from the first fetch after
    // the last step (headless runs end that way).
    ScriptInput(Engine& engine, std::vector<ScriptStep> steps, bool quitAtEnd);
    ~ScriptInput();
    ScriptInput(const ScriptInput&) = delete;
    ScriptInput& operator=(const ScriptInput&) = delete;

    bool finished() const { return next_ >= steps_.size(); }
    size_t shotsWritten() const { return shots_; }

private:
    void onPump(PumpPhase phase);
    void post(const ScriptStep& s);

    Engine& engine_;
    std::vector<ScriptStep> steps_;
    size_t next_ = 0;
    bool quitAtEnd_;
    int hookId_ = 0;
    uint64_t waitUntil_ = 0;
    size_t shots_ = 0;
    int endFetches_ = 0;
};

}  // namespace tmport::app
