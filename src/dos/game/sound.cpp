// The game's sound facade (sound.h): the recording double, the DosAudioDevice adapter and the
// game-side calls that keep the DGROUP flags in step.
#include "dos/game/sound.h"

#include <format>

#include "dos/audio/dos_audio_device.h"
#include "dos/game/game.h"

namespace tmport::dos {

// ---- RecordingSound -------------------------------------------------------------------------

void RecordingSound::sfxPlay(int id) {
    calls_.push_back({Op::SfxPlay, id});
    sfxActive_ = true;
}
void RecordingSound::sfxStop() {
    calls_.push_back({Op::SfxStop, 0});
    sfxActive_ = false;
}
void RecordingSound::musicPlay(int tune) { calls_.push_back({Op::MusicPlay, tune}); }
void RecordingSound::musicStop() { calls_.push_back({Op::MusicStop, 0}); }
void RecordingSound::silenceAll() {
    calls_.push_back({Op::SilenceAll, 0});
    sfxActive_ = false;
}
void RecordingSound::setSoundOn(bool on) { calls_.push_back({Op::SoundOn, on ? 1 : 0}); }
void RecordingSound::setMusicOn(bool on) { calls_.push_back({Op::MusicOn, on ? 1 : 0}); }
void RecordingSound::setSfxLoop(bool on) { calls_.push_back({Op::SfxLoop, on ? 1 : 0}); }
void RecordingSound::setMusicLoop(bool on) { calls_.push_back({Op::MusicLoop, on ? 1 : 0}); }
void RecordingSound::setMusicGate(uint16_t m) { calls_.push_back({Op::MusicGate, m}); }
void RecordingSound::restoreDriver(std::span<const uint8_t> sfxBlock, std::span<const uint8_t> musicBlock, int /*voices*/) {
    const auto word = [](std::span<const uint8_t> b, size_t i) { return b.size() > 2 * i + 1 ? b[2 * i] | (b[2 * i + 1] << 8) : 0; };
    calls_.push_back({Op::Restore, word(musicBlock, 0) != 0 ? word(musicBlock, 1) : 0xFFFF});
    sfxActive_ = word(sfxBlock, 0) != 0;
}

std::string RecordingSound::describe() const {
    static constexpr const char* kNames[] = {"sfx", "sfxstop", "music", "musicstop", "silence",
                                             "soundon", "musicon", "sfxloop", "musicloop", "gate", "restore"};
    std::string s;
    for (const Call& c : calls_) {
        if (!s.empty()) s += ", ";
        s += std::format("{} {}", kNames[static_cast<size_t>(c.op)], c.arg);
    }
    return s;
}

// ---- DosAudioSound --------------------------------------------------------------------------

DosAudioSound::DosAudioSound(DosAudioDevice& device, bool offline) : device_(device), offline_(offline) {}

// Sfx_Play and Sfx_Stop set the status DS:6448 at once; the device shows a request after the tick
// that applies it (the next one it renders, offline or on the audio thread), so until then the
// facade answers for it.
void DosAudioSound::sfxPlay(int id) {
    device_.sfxPlay(id);
    if (!soundOn_) return;  // 491b:06d7 returns at once with g_soundOn 0
    pending_ = Pending::Play;
    pendingTicks_ = device_.status().ticks;
}
void DosAudioSound::sfxStop() {
    device_.sfxStop();
    pending_ = Pending::Stop;
    pendingTicks_ = device_.status().ticks;
}
void DosAudioSound::musicPlay(int tune) { device_.musicPlay(tune); }
void DosAudioSound::musicStop() { device_.musicStop(); }
void DosAudioSound::silenceAll() { device_.silenceAll(); }  // voices off; the effect's status stays
void DosAudioSound::setSoundOn(bool on) {
    soundOn_ = on;
    device_.setSoundOn(on);
}
void DosAudioSound::setMusicOn(bool on) { device_.setMusicOn(on); }
// DS:63B8: the end sequence's looping effect 2 (player states 44/45/64/65, Cell_P64_EndScreen).
void DosAudioSound::setSfxLoop(bool on) { device_.setSfxLoop(on); }
void DosAudioSound::setMusicLoop(bool on) { device_.setMusicLoop(on); }
void DosAudioSound::setMusicGate(uint16_t m) { device_.setMusicPaused(m == 0); }

bool DosAudioSound::sfxActive() const {
    const DosAudioDevice::Status st = device_.status();
    if (pending_ != Pending::None && st.ticks <= pendingTicks_) return pending_ == Pending::Play;
    return st.sfxStatus != 0;
}

void DosAudioSound::restoreDriver(std::span<const uint8_t> sfxBlock, std::span<const uint8_t> musicBlock, int voices) {
    device_.restoreBlocks(sfxBlock, musicBlock, voices);
    const bool active = sfxBlock.size() >= 2 && (sfxBlock[0] | sfxBlock[1]) != 0;
    pending_ = active ? Pending::Play : Pending::Stop;  // the status shows it after the next tick
    pendingTicks_ = device_.status().ticks;
}

void DosAudioSound::pitTick() {
    if (!offline_) return;  // the audio thread renders by sample count
    scratch_.clear();
    device_.renderOfflineTicks(1, scratch_);
    if (keepSamples_) samples_.insert(samples_.end(), scratch_.begin(), scratch_.end());
}

// ---- game-side calls ------------------------------------------------------------------------

void sfxPlay(DosGame& g, int id) { g.sound.sfxPlay(id); }
void sfxStop(DosGame& g) { g.sound.sfxStop(); }
void musicPlay(DosGame& g, int tune) {
    if (tune < 0) return;
    g.sound.musicPlay(tune);
    // A tune that starts (the driver's set-up runs only with the music on, 491b:07b9): the frame
    // timing of a dissolve the same tick latched (main_loop.cpp setDissolveStart).
    if (g.core.musicOn != 0) ++g.musicPlays;
}
void musicStop(DosGame& g) { g.sound.musicStop(); }
void soundSilenceAll(DosGame& g) { g.sound.silenceAll(); }

void setSoundOn(DosGame& g, bool on) {
    g.core.soundOn = on ? 1 : 0;
    g.sound.setSoundOn(on);
}
void setMusicOn(DosGame& g, bool on) {
    g.core.musicOn = on ? 1 : 0;
    g.sound.setMusicOn(on);
}
void setSfxLoop(DosGame& g, bool on) {
    g.core.sfxLoop = on ? 1 : 0;
    g.sound.setSfxLoop(on);
}
void setMusicLoop(DosGame& g, bool on) {
    g.core.musicLoop = on ? 1 : 0;
    g.sound.setMusicLoop(on);
}
void setMusicGate(DosGame& g, uint16_t m) {
    g.core.musicGate = m;
    g.sound.setMusicGate(m);
}
bool sfxBlockActive(const DosGame& g) { return g.sound.sfxActive(); }

void syncSoundFlags(DosGame& g) {
    g.sound.setSoundOn(g.core.soundOn != 0);
    g.sound.setMusicOn(g.core.musicOn != 0);
    g.sound.setSfxLoop(g.core.sfxLoop != 0);
    g.sound.setMusicLoop(g.core.musicLoop != 0);
    g.sound.setMusicGate(g.core.musicGate);
}

void soundRestoreDriver(DosGame& g, std::span<const uint8_t> dgroup) {
    constexpr size_t kSfxBlock = 0x6448, kMusicBlock = 0x64C0, kVoices = 0x63BE, kBlockBytes = 0x78;
    if (dgroup.size() < kMusicBlock + kBlockBytes) return;
    g.sound.restoreDriver(dgroup.subspan(kSfxBlock, kBlockBytes), dgroup.subspan(kMusicBlock, kBlockBytes),
                          dgroup[kVoices] | (dgroup[kVoices + 1] << 8));
}

// 43cb:03cb / 43cb:046e / 47c7:08b5: the tune of an area.
int tuneForArea(uint16_t area) {
    if (area < 4) return 0;
    if (area < 7) return 1;
    if (area < 15) return 2;
    return -1;
}

}  // namespace tmport::dos
