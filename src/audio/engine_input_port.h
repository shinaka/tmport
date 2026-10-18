#pragma once
// Glue between the audio system and the running Engine:
//   * EngineInputPort: InputPort on Engine::peekInputCode / getInputCode / pump, for the speech
//     wait and the jingle wait in the real game;
//   * connectAudioToEngine(): installs the audio duties the engine exposes as hooks.

#include "audio/input_port.h"

namespace tmport {
class Engine;
}

namespace tmport::audio {

class AudioSystem;

class EngineInputPort final : public InputPort {
public:
    // audio (optional): serviced on every idle step (see connectAudioToEngine).
    explicit EngineInputPort(Engine& engine, AudioSystem* audio = nullptr) : engine_(engine), audio_(audio) {}

    uint16_t peekCode() override;  // Engine::peekInputCode (1028:01f6)
    uint16_t getCode() override;   // Engine::getInputCode (1028:0131)
    void idle() override;          // Engine::pump (posts input, delivers none) + 1 ms sleep

private:
    Engine& engine_;
    AudioSystem* audio_;
};

// Installs on the engine:
//   * the activation hook: App_Deactivate / App_Activate (1010:05c8 / 05db) -> cut the wave
//     voice and pause the music / resume it at the saved position;
//   * the Ctrl+S hook of the event fetch: Audio_ToggleAll (1128:0151), run inside the fetch
//     before the 0x13 code is returned (1028:004f), so Ctrl+S also interrupts speech.
// Both objects must outlive the connection. The rest is wired by the app (app/engine_glue.h):
//   * audio.service() (MM_WOM_DONE / MM_MCINOTIFY: tune looping and the OPEN1 -> OPEN2 switch
//     depend on it) runs from an Engine pump hook (Engine::addPumpHook), in every pump;
//   * the in-engine menu bar (WM_ENTERMENULOOP / WM_EXITMENULOOP) runs inside Engine::holdWhile,
//     which calls this activation hook; game message boxes call audio.pauseAll() / resumeAll()
//     through the DialogSystem hooks.
void connectAudioToEngine(Engine& engine, AudioSystem& audio);

}  // namespace tmport::audio
