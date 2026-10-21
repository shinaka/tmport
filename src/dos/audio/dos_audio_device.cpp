#include "dos/audio/dos_audio_device.h"

#include <algorithm>

namespace tmport::dos {

DosAudioDevice::DosAudioDevice(std::shared_ptr<const SoundBank> bank, DosAudioConfig cfg, std::function<int()> rand)
    : bank_(std::move(bank)), cfg_(std::move(cfg)), rand_(std::move(rand)) {
    renderer_ = std::make_unique<DosSoundRenderer>(*bank_, cfg_.render);
    output_ = std::make_unique<mischief::dos::ChipAudioDevice>(renderer_->chips(), cfg_, "dos audio");
    output_->setTickHook([this] { applyPending(); });
    output_->setAfterRender([this] { publishStatus(); });
    publishStatus();
}

DosAudioDevice::~DosAudioDevice() { output_.reset(); }

bool DosAudioDevice::open(uint32_t sdlDevice) { return output_->open(sdlDevice, std::string(soundCardName(bank_->device))); }

// ---------------------------------------------------------------------------- command queue

void DosAudioDevice::push(Command c) {
    std::lock_guard lock(queueMutex_);
    queue_.push_back(c);
    pending_.store(true);
}

void DosAudioDevice::applyPending() {
    if (!pending_.load()) return;
    std::vector<Command> cmds;
    std::vector<Restore> restores;
    std::vector<std::vector<uint8_t>> oplRestores;
    {
        std::lock_guard lock(queueMutex_);
        cmds.swap(queue_);
        restores.swap(restores_);
        oplRestores.swap(oplRestores_);
        pending_.store(false);
    }
    Sequencer& s = renderer_->sequencer();
    size_t nextRestore = 0, nextOplRestore = 0;
    std::vector<Command> later;  // delayed commands, one tick closer now (kept in their order)
    for (Command c : cmds) {
        if (c.delay > 0) {
            --c.delay;
            later.push_back(c);
            continue;
        }
        switch (c.kind) {
            case Command::Kind::SfxPlay: s.sfxPlay(c.a); break;
            case Command::Kind::SfxStop: s.sfxStop(); break;
            case Command::Kind::MusicPlay: s.musicPlay(c.a, c.b); break;
            case Command::Kind::MusicStop: s.musicStop(); break;
            case Command::Kind::SilenceAll: s.silenceAll(); break;
            case Command::Kind::SoundOn: s.setSoundOn(c.a != 0); break;
            case Command::Kind::MusicOn: s.setMusicOn(c.a != 0); break;
            case Command::Kind::MusicPaused: s.setMusicPaused(c.a != 0); break;
            case Command::Kind::MusicLoop: s.setMusicLoop(c.a != 0); break;
            case Command::Kind::SfxLoop: s.setSfxLoop(c.a != 0); break;
            case Command::Kind::Restore:
                if (nextRestore < restores.size()) {
                    const Restore& r = restores[nextRestore++];
                    s.restore(r.sfx, r.music, r.voices, r.variant, oplRestored_);
                    oplRestored_ = false;
                }
                break;
            case Command::Kind::OplRestore:
                if (nextOplRestore < oplRestores.size()) oplRestored_ = renderer_->restoreOpl(oplRestores[nextOplRestore++]);
                break;
        }
    }
    if (later.empty()) return;
    // Back in front of whatever the game queued meanwhile (pushed after them).
    std::lock_guard lock(queueMutex_);
    queue_.insert(queue_.begin(), later.begin(), later.end());
    pending_.store(true);
}

void DosAudioDevice::sfxPlay(int id, uint32_t delay) { push({Command::Kind::SfxPlay, id, 0, delay}); }
void DosAudioDevice::sfxStop(uint32_t delay) { push({Command::Kind::SfxStop, 0, 0, delay}); }
void DosAudioDevice::musicStop(uint32_t delay) { push({Command::Kind::MusicStop, 0, 0, delay}); }
void DosAudioDevice::silenceAll(uint32_t delay) { push({Command::Kind::SilenceAll, 0, 0, delay}); }
void DosAudioDevice::setSoundOn(bool on, uint32_t delay) { push({Command::Kind::SoundOn, on ? 1 : 0, 0, delay}); }
void DosAudioDevice::setMusicPaused(bool paused, uint32_t delay) { push({Command::Kind::MusicPaused, paused ? 1 : 0, 0, delay}); }
void DosAudioDevice::setMusicLoop(bool on, uint32_t delay) { push({Command::Kind::MusicLoop, on ? 1 : 0, 0, delay}); }
void DosAudioDevice::setSfxLoop(bool on, uint32_t delay) { push({Command::Kind::SfxLoop, on ? 1 : 0, 0, delay}); }

void DosAudioDevice::restoreBlocks(std::span<const uint8_t> sfxBlock, std::span<const uint8_t> musicBlock, int voices,
                                   int adlibVariant) {
    std::lock_guard lock(queueMutex_);
    restores_.push_back({Sequencer::blockFromDgroup(sfxBlock), Sequencer::blockFromDgroup(musicBlock), voices, adlibVariant});
    queue_.push_back({Command::Kind::Restore});
    pending_.store(true);
}

void DosAudioDevice::restoreOplDriver(std::span<const uint8_t> dgroup) {
    if (bank_->device != SoundCard::AdLib) return;
    std::lock_guard lock(queueMutex_);
    oplRestores_.emplace_back(dgroup.begin(), dgroup.end());
    queue_.push_back({Command::Kind::OplRestore});
    pending_.store(true);
}

void DosAudioDevice::setOplSynthesis(bool on) {
    output_->withRenderer([on](mischief::dos::ChipRenderer& r) { r.setOplSynthesis(on); });
}

void DosAudioDevice::setMusicOn(bool on, uint32_t delay) {
    musicOnMirror_ = on;
    push({Command::Kind::MusicOn, on ? 1 : 0, 0, delay});
}

void DosAudioDevice::musicPlay(int tune, uint32_t delay) {
    int variant = 0;
    // 4ac9:0002 AdLib_PlayTune: rand() & 3 picks one of 4 orchestrations (only reached when the
    // music is on, 491b:07b9); drawn now, in the game's order of rand() calls, whatever the delay.
    if (bank_->device == SoundCard::AdLib && musicOnMirror_ && rand_) variant = rand_() & 3;
    push({Command::Kind::MusicPlay, tune, variant, delay});
}

// ----------------------------------------------------------------------------------- status

void DosAudioDevice::publishStatus() {
    const Sequencer& s = renderer_->sequencer();
    const uint64_t w = static_cast<uint64_t>(s.sfxBlock().status) | (static_cast<uint64_t>(s.sfxBlock().id) << 16) |
                       (static_cast<uint64_t>(s.musicBlock().status) << 32) | (static_cast<uint64_t>(s.musicBlock().id) << 48);
    statusWord_.store(w);
    statusTicks_.store(renderer_->ticks());
}

DosAudioDevice::Status DosAudioDevice::status() const {
    const uint64_t w = statusWord_.load();
    Status st;
    st.sfxStatus = static_cast<uint16_t>(w);
    st.sfxId = static_cast<uint16_t>(w >> 16);
    st.musicStatus = static_cast<uint16_t>(w >> 32);
    st.musicId = static_cast<uint16_t>(w >> 48);
    st.ticks = statusTicks_.load();
    return st;
}

}  // namespace tmport::dos
