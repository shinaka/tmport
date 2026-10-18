#include "audio/music_manager.h"

namespace tmport::audio {

std::optional<int> MusicManager::tuneForZone(int code) {
    switch (code) {
        case 0: return 9001;
        case 1: case 2: return 9003;
        case 3: case 4: return 9004;
        case 5: case 6: return 9005;
        case 7: return 9000;
        case -98: return 9002;
        case -97: return 9009;
        case -96: return 9008;
        case -95: return 9006;
        case -94: return 9007;
        default: return std::nullopt;
    }
}

// 1128:0000 Music_PlayZone (+ 1128:007b): the tune is recorded even with sound off.
void MusicManager::playZone(int code) {
    const auto tune = tuneForZone(code);
    if (!tune) return;
    tuneId_ = static_cast<int16_t>(*tune);
    zone_ = static_cast<int16_t>(code);
    sceneActive_ = true;
    if (settings_.soundOn) driver_.playTrack(tuneId_);
}

// 1128:00d6 Music_Halt
void MusicManager::halt() { driver_.stop(); }

// 1128:00dc Music_Resume
void MusicManager::resume() {
    if (settings_.soundOn) driver_.playTrack(tuneId_);
}

// 1128:00ef Music_StopScene
void MusicManager::stopScene() {
    if (!sceneActive_) return;
    driver_.stop();
    sceneActive_ = false;
    zone_ = 0;
}

// 1128:010c Music_IsZonePlaying
bool MusicManager::isZonePlaying(int code) const { return driver_.isPlaying() && zone_ == code; }

bool MusicManager::onRoomChange(int leftCode, int enteredCode, bool deferStart) {
    if (leftCode == enteredCode) return false;
    stopScene();
    if (!deferStart) playZone(enteredCode);
    return true;
}

}  // namespace tmport::audio
