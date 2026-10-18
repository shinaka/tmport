#pragma once
// Modal animation playback (10d8:0082 Anim_PlayModal; re/docs/code_1098-10d8.md §9.2,
// fmt_othr_tables.md §4.4, PORTING_NOTES.md §4.6). Used by the level cut-scenes (7 fps) and the
// celebration (4 fps).
//
// The first tick runs on the first pass (timer slot 7 is armed with 0), later ticks every
// 1000/fps ms (integer division). Input is polled on every pass of the loop: Enter (1) or
// Space (2) stops the animation when it is skippable; Esc calls onEsc and the animation
// continues if that returns false. The loop ends when the channel no longer runs that script.

#include <cstdint>
#include <functional>

#include "anim/anim_engine.h"
#include "anim/dam_script.h"

namespace tmport {
class Engine;
}

namespace tmport::anim {

// What the modal player needs from the engine (Engine provides it through EngineModalHost;
// tests use a fake clock and a scripted input queue).
class ModalHost {
public:
    virtual ~ModalHost() = default;
    virtual void timerSet(int slot, int32_t ms) = 0;  // 10a8:002a Timer_Set
    virtual bool timerExpired(int slot) = 0;          // 10a8:0066 Timer_Expired (may pump / yield)
    virtual void flushInput() = 0;                    // 1028:0126 Input_Flush
    virtual uint16_t getInputCode() = 0;              // 1028:0131 Input_GetCode (0 = none)
    virtual void flushDirtyRects() = 0;               // 1058:0731 Gfx_FlushDirtyRects
};

class EngineModalHost final : public ModalHost {
public:
    explicit EngineModalHost(Engine& engine) : engine_(engine) {}
    void timerSet(int slot, int32_t ms) override;
    bool timerExpired(int slot) override;
    void flushInput() override;
    uint16_t getInputCode() override;
    void flushDirtyRects() override;

private:
    Engine& engine_;
};

// Gfx_BlitToWindow through an Engine's presenter (for AnimEngine's present callback).
PresentFn enginePresent(Engine& engine);

enum ModalResult : int {
    kModalFinished = 0,  // the script reached its end
    kModalEnter = 1,     // skipped with Enter
    kModalSpace = 2,     // skipped with Space
    kModalQuit = 3,      // onEsc returned true (not in the original, whose Esc handler exits
                         // the program when the player confirms; see playModal)
};

struct ModalParams {
    ScriptPtr script;
    int frameBase = 0;
    int user0 = 0, user1 = 0;  // stored in the channel, never read (kept for fidelity)
    bool skippable = true;
    // Esc handler (1140:0098 MenuCmd_Exit: the "Exit" Yes/No dialog). Return false when the
    // player answered No: the animation continues. Game code normally does not return true but
    // ends the program (QuitGame) as the original does; if it returns true the channel is freed
    // and playModal returns kModalQuit. nullptr: Esc is ignored.
    // TODO(integration): the celebration (10f0) passes the "Exit" dialog of the text_ui area
    // (GameDlg_RunModal on descriptor DS:1533), which saves and throws QuitGame on Yes.
    std::function<bool()> onEsc;
    int x = 0, y = 0;
    int fps = 4;  // must be non-zero (the original divides by it); 0 is treated as 1
};

// 10d8:0082 Anim_PlayModal: starts the script on layer 0 with channel flags 1 and plays it to
// the end. Saves g_dirtyEnabled, enables it while playing and restores it. What stays on screen
// follows the script's ending (FE FF erases the last frame; FD..FF, only 20300, leaves it and
// its erase rects stay queued for the next eraseSprites caller).
int playModal(AnimEngine& anim, ModalHost& host, const ModalParams& params);
int playModal(AnimEngine& anim, Engine& engine, const ModalParams& params);

// The timer period of one tick: (int)(1000 / fps), with fps 0 treated as 1.
int32_t modalTickMs(int fps);

}  // namespace tmport::anim
