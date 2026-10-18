#include "app/engine_glue.h"

#include "audio/audio_system.h"
#include "audio/engine_input_port.h"
#include "game/game_state.h"
#include "platform/engine.h"
#include "ui/game_dialog.h"
#include "ui/menu_bar.h"

namespace tmport::app {

void mirrorAudioOptions(AppContext& ctx) {
    if (!ctx.audio || !ctx.state) return;
    ctx.state->options.soundOn = ctx.audio->settings().soundOn;
    ctx.state->options.voiceOn = ctx.audio->settings().voiceOn;
}

EngineGlue::EngineGlue(AppContext& ctx) : ctx_(ctx) {
    Engine& engine = ctx.engine;
    if (audio::AudioSystem* a = ctx.audio) {
        // Activation hook (App_Deactivate / App_Activate) and the Ctrl+S hook of the event fetch
        // (Audio_ToggleAll), as provided by the audio area.
        audio::connectAudioToEngine(engine, *a);
        // Ctrl+S: Audio_ToggleAll; the player's option bytes (users.dat +0x0e/+0x0f) follow it.
        engine.input().setSoundToggleHook([this, a] {
            a->toggleAll();
            mirrorAudioOptions(ctx_);
        });
        // The window-procedure duties of the audio: every pump, including frozen ones.
        audioHookId_ = engine.addPumpHook([this, a](PumpPhase) {
            ++serviceCalls_;
            a->service();
        });
    }
    ui::DialogHooks& h = ctx.dialogs.hooks();
    if (audio::AudioSystem* a = ctx.audio) {
        h.soundPauseAll = [a] { a->pauseAll(); };    // 1090:0536 Sound_PauseAll
        h.soundResumeAll = [a] { a->resumeAll(); };  // 1090:0540 Sound_ResumeAll
    }
    if (ui::MenuBar* m = ctx.menu) {
        h.menuIsEnabled = [m] { return m->isEnabled(); };       // 1078:01b1
        h.menuEnableAll = [m](bool on) { m->enableAll(on); };   // 1078:01b5
    }
}

EngineGlue::~EngineGlue() {
    Engine& engine = ctx_.engine;
    if (audioHookId_) engine.removePumpHook(audioHookId_);
    if (ctx_.audio) {
        engine.setActivationHook({});
        engine.input().setSoundToggleHook({});
    }
    ui::DialogHooks& h = ctx_.dialogs.hooks();
    h.soundPauseAll = {};
    h.soundResumeAll = {};
    h.menuIsEnabled = {};
    h.menuEnableAll = {};
}

}  // namespace tmport::app
