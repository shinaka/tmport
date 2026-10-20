// The sound facade's recording double (sound.h).
#include "gg/game/sound.h"

#include <format>

namespace tmport::gg {

void RecordingSound::record(std::string op, std::vector<int> args) { calls_.push_back(Call{std::move(op), std::move(args)}); }

std::vector<RecordingSound::Call> RecordingSound::takeCalls() {
    std::vector<Call> out(calls_.begin() + static_cast<std::ptrdiff_t>(taken_), calls_.end());
    taken_ = calls_.size();
    return out;
}

std::string RecordingSound::describe(const std::vector<Call>& calls) {
    std::string out;
    for (const Call& c : calls) {
        if (!out.empty()) out += "; ";
        out += c.op + "(";
        for (size_t i = 0; i < c.args.size(); ++i) out += std::format("{}{}", i ? "," : "", c.args[i]);
        out += ")";
    }
    return out;
}

int RecordingSound::loadMusicBank(uint16_t id) {
    const int h = nextHandle_++;
    record("loadMusicBank", {id, h});
    return h;
}

int RecordingSound::loadEffectBank(uint16_t id) {
    const int h = nextHandle_;
    nextHandle_ += kEffectSpan;
    record("loadEffectBank", {id, h});
    return h;
}

int RecordingSound::loadSong(uint16_t id) {
    const int h = nextHandle_++;
    record("loadSong", {id, h});
    return h;
}

void RecordingSound::freeMusicBank(int h) { record("freeMusicBank", {h}); }
void RecordingSound::freeEffectBank(int h) { record("freeEffectBank", {h}); }
void RecordingSound::freeSong(int h) { record("freeSong", {h}); }
void RecordingSound::playMusic(int h, int section, int loop) { record("playMusic", {h, section, loop}); }
void RecordingSound::playEffect(int h, int loop, int priority) { record("playEffect", {h, loop, priority}); }
void RecordingSound::stopMusic(int h) { record("stopMusic", {h}); }
void RecordingSound::stopEffect(int h) { record("stopEffect", {h}); }
void RecordingSound::stopAllEffects() { record("stopAllEffects", {}); }
int RecordingSound::isPlaying(int h) { return playing_ ? playing_(h) : 0; }
void RecordingSound::selectArrangement(int h, int n) { record("selectArrangement", {h, n}); }
void RecordingSound::muteChannel(int h, int channel) { record("muteChannel", {h, channel}); }
void RecordingSound::unmuteChannel(int h, int channel) { record("unmuteChannel", {h, channel}); }

void RecordingSound::setEffectBank(int h) {
    effectBank_ = h;
    record("setEffectBank", {h});
}

int RecordingSound::musicTick(int h) { return musicTick_ ? musicTick_(h) : 0x7FFF; }

void RecordingSound::toggleSound() {
    muted_ ^= 1;
    record("toggleSound", {muted_});
}

void RecordingSound::setSoundMuted(int muted) {
    muted_ = muted;
    record("setSoundMuted", {muted});
}

void RecordingSound::pause() { record("pause", {}); }
void RecordingSound::resume() { record("resume", {}); }
void RecordingSound::shutdown() { record("shutdown", {}); }
void RecordingSound::setRoomMusic(int bank, int building) { record("setRoomMusic", {bank, building}); }
void RecordingSound::roomMusicSelectLayers(int n, bool roomHasChimps, int level) {
    record("roomMusicSelectLayers", {n, roomHasChimps ? 1 : 0, level});
}
void RecordingSound::roomMusicStart() { record("roomMusicStart", {}); }
void RecordingSound::workshopMusicStart(int musicBank) { record("workshopMusicStart", {musicBank}); }
void RecordingSound::workshopBlueprint(bool open) { record("workshopBlueprint", {open ? 1 : 0}); }
void RecordingSound::movieMusicStart(int musicBank) { record("movieMusicStart", {musicBank}); }
void RecordingSound::pzLoadBank(uint16_t id) { record("pzLoadBank", {id}); }
void RecordingSound::pzFreeBank() { record("pzFreeBank", {}); }
void RecordingSound::pzPlay(int i, int priority) { record("pzPlay", {i, priority}); }
void RecordingSound::pzPlayLoop(int i, int priority) { record("pzPlayLoop", {i, priority}); }
void RecordingSound::pzStop(int i) { record("pzStop", {i}); }
void RecordingSound::pzStopAll() { record("pzStopAll", {}); }
void RecordingSound::pzSetTranspose(int i, int t) { record("pzSetTranspose", {i, t}); }
int RecordingSound::pzIsActive(int i) { return pzActive_ ? pzActive_(i) : 0; }

}  // namespace tmport::gg
