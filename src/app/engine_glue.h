#pragma once
// Wiring between the engine and the services that the original's window procedure and modal
// loops called (re/docs/code_1008-1030.md §5, fmt_sound.md §7/§10, code_1098-10d8.md §10):
//
//   Engine pump hook      -> audio.service()                 (MM_WOM_DONE / MM_MCINOTIFY, every pump)
//   activation hook       -> audio.appDeactivate()/appActivate()  (1010:05c8 / 05db; focus loss,
//                            minimise, and the menu loop: WM_ENTERMENULOOP / WM_EXITMENULOOP)
//   Ctrl+S in the fetch   -> audio.toggleAll()               (1128:0151, inside 1028:004f; the
//                            0x13 code that follows interrupts speech waits, fmt_sound.md §5.2)
//   DialogSystem hooks    -> Sound_PauseAll / Sound_ResumeAll (1090:0536 / 0540) and
//                            Menu_IsEnabled / Menu_EnableAll  (1078:01b1 / 01b5)
//
// The music pause nesting of 10d0 (counter DS:5cc8) is DialogSystem::musicPauseNested /
// musicResumeNested / musicResumeForce; its hooks therefore call the non-nesting AudioSystem::
// pauseAll / resumeAll. (AudioSystem::pauseNested keeps its own copy of the counter and must not be
// mixed with the dialogs' one; in the original only 10c0 and GameMain's reset use 10d0.)

#include "app/app_context.h"

namespace tmport::app {

// Copies the audio settings (Music and Sound, Voice) into ctx.state->options (DS:0b96 / 0b97 live
// in AudioSystem::settings(); GameState keeps the copy that users.dat saves). No-op without both.
void mirrorAudioOptions(AppContext& ctx);

class EngineGlue {
public:
    // Installs every hook that has its parts in ctx (audio and menu may be null). Removed again by
    // the destructor; ctx must outlive the glue.
    explicit EngineGlue(AppContext& ctx);
    ~EngineGlue();
    EngineGlue(const EngineGlue&) = delete;
    EngineGlue& operator=(const EngineGlue&) = delete;

    // Number of audio.service() calls made by the pump hook (tests).
    long serviceCalls() const { return serviceCalls_; }

private:
    AppContext& ctx_;
    int audioHookId_ = 0;
    long serviceCalls_ = 0;
};

}  // namespace tmport::app
