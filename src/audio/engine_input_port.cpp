#include "audio/engine_input_port.h"

#include "audio/audio_system.h"
#include "platform/engine.h"

namespace tmport::audio {

uint16_t EngineInputPort::peekCode() { return engine_.peekInputCode(); }

uint16_t EngineInputPort::getCode() { return engine_.getInputCode(); }

void EngineInputPort::idle() {
    engine_.pump();
    if (audio_) audio_->service();
    engine_.clock().sleepMs(1);
}

void connectAudioToEngine(Engine& engine, AudioSystem& audio) {
    engine.setActivationHook([&audio](bool active) {
        if (active)
            audio.appActivate();
        else
            audio.appDeactivate();
    });
    engine.input().setSoundToggleHook([&audio] { audio.toggleAll(); });
}

}  // namespace tmport::audio
