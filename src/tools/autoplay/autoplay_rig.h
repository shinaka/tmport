#pragma once
// The headless application the autoplayer runs the real game in: the same parts the bootstrap
// builds (app/bootstrap.cpp: Engine, Font, TextRenderer, DialogSystem, AnimEngine, MenuBar,
// AudioSystem, GameTables / GameState, AppContext, menu commands, EngineGlue), but never with a
// window, never with an audible device, and optionally on a manual clock (the game then runs as
// fast as the CPU allows: every wait advances the clock instead of sleeping).
//
// The rig owns one AppContext; game::gameMain(rig.ctx(), options) runs the whole game in it.
// A second Rig on the same pref directory is "the program started again" (users.dat persists).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

#include "app/app_context.h"
#include "timing/clock.h"

namespace tmport {
struct GameFiles;
class Engine;
namespace text { class Font; class TextRenderer; }
namespace ui { class DialogSystem; class MenuBar; }
namespace audio { class AudioSystem; }
namespace anim { class AnimEngine; }
namespace save { struct Paths; }
namespace game { struct GameState; struct GameTables; }
namespace app { class EngineGlue; }
}  // namespace tmport

namespace tmport::autoplay {

struct RigOptions {
    std::filesystem::path prefDir;  // users.dat lives here (created when missing)
    bool manualClock = true;        // false: the SDL clock (real time)
    uint64_t clockStart = 1000;     // manual clock: the first nowMs() (the game seeds srand from it)
    bool audio = true;              // a silent AudioSystem (no wave device, null MIDI); false: none
};

class Rig {
public:
    Rig(GameFiles& files, const RigOptions& options);
    ~Rig();
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    AppContext& ctx() { return *ctx_; }
    Engine& engine() { return *engine_; }
    game::GameState& state() { return *state_; }
    const game::GameTables& tables() const { return *tables_; }
    ui::DialogSystem& dialogs() { return *dialogs_; }
    ui::MenuBar* menu() { return menu_.get(); }
    audio::AudioSystem* audio() { return audio_.get(); }
    anim::AnimEngine& anim() { return *anim_; }
    const save::Paths& paths() const { return *paths_; }
    Clock& clock();
    ManualClock* manualClock() { return manual_.get(); }

    // True while a game dialog (DialogSystem::runModal / pager) polls for input: the dialogs'
    // Input_GetCode hook is wrapped so that the autoplayer knows a Yes/No box is up.
    bool inDialogFetch() const { return dialogDepth_ > 0; }
    // Number of dialog input polls so far (diagnostics).
    uint64_t dialogPolls() const { return dialogPolls_; }

private:
    std::unique_ptr<ManualClock> manual_;
    std::unique_ptr<save::Paths> paths_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<audio::AudioSystem> audio_;
    std::unique_ptr<text::Font> font_;
    std::unique_ptr<text::TextRenderer> text_;
    std::unique_ptr<ui::DialogSystem> dialogs_;
    std::unique_ptr<anim::AnimEngine> anim_;
    std::unique_ptr<ui::MenuBar> menu_;
    std::unique_ptr<game::GameTables> tables_;
    std::unique_ptr<game::GameState> state_;
    std::unique_ptr<AppContext> ctx_;
    std::unique_ptr<app::EngineGlue> glue_;
    int dialogDepth_ = 0;
    uint64_t dialogPolls_ = 0;
};

}  // namespace tmport::autoplay
