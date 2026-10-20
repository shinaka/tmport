#include "gg/audio/gg_audio.h"

#include <algorithm>

#include "dos/audio/opl_chip.h"
#include "gg/data/archive.h"
#include "gg/data/gg_exe.h"
#include "timing/pit_clock.h"

namespace tmport::gg {

int soundDevice(SoundMode m) {
    switch (m) {
        case SoundMode::Off: return SndDriver::kDeviceOff;
        case SoundMode::Speaker: return SndDriver::kDeviceSpeaker;
        case SoundMode::Fm: return SndDriver::kDeviceDetect;
    }
    return SndDriver::kDeviceDetect;
}

double ggSoundTickHz() { return PitTimeBase(kPitDivisorGg).hz() / 2.0; }

// The driver's ports: the OPL writes to the emulated chip (none for the speaker mode), the speaker
// to the renderer's PIT channel 2 model; both logged on request.
class GgAudio::Ports final : public SndPorts {
public:
    Ports(GgAudio& a, dos::OplWriter* opl, dos::SpeakerSynth& spk) : a_(a), opl_(opl), spk_(spk) {}
    void oplWrite(uint8_t reg, uint8_t value) override {
        a_.oplLog_.write(a_.driver_->tickIndex(), reg, value);
        if (opl_) opl_->oplWrite(reg, value);
    }
    void speakerDivisor(uint16_t divisor) override {
        if (a_.cfg_.logSpeaker) a_.speakerLog_.push_back({a_.driver_->tickIndex(), false, divisor, a_.command_});
        spk_.setDivisor(divisor);
    }
    void speakerPort61(uint8_t value) override {
        if (a_.cfg_.logSpeaker) a_.speakerLog_.push_back({a_.driver_->tickIndex(), true, value, a_.command_});
        spk_.setEnabled((value & 3) == 3);
    }

private:
    GgAudio& a_;
    dos::OplWriter* opl_;
    dos::SpeakerSynth& spk_;
};

GgAudio::GgAudio(const GgExe& exe, const Archive& archive, const GgAudioConfig& cfg) : cfg_(cfg), oplLog_(cfg.logOpl) {
    cfg_.render.tickHz = ggSoundTickHz();
    auto tables = std::make_shared<const SndTables>(SndTables::fromExe(exe));
    if (cfg_.mode == SoundMode::Fm)
        chip_ = std::make_unique<dos::OplChip>(cfg_.render.sampleRate, cfg_.oplWriteSeconds > 0 ? cfg_.oplWriteSeconds : kOplWriteSeconds);
    renderer_ = std::make_unique<dos::ChipRenderer>(cfg_.render, chip_.get());
    ports_ = std::make_unique<Ports>(*this, chip_.get(), renderer_->speaker());
    driver_ = std::make_unique<SndDriver>(std::move(tables), *ports_);
    dos::ChipAudioConfig ac;
    ac.render = cfg_.render;
    ac.sdlDriver = cfg_.sdlDriver;
    ac.volume = cfg_.volume;
    device_ = std::make_unique<dos::ChipAudioDevice>(*renderer_, ac, "gg audio");
    renderer_->setTickSource(driver_.get());

    // Init_CommandLine 303a:005c step 7, then SndRes_Init in Game_Init 12e0:0afb.
    res_ = std::make_unique<SndRes>(*driver_, archive);
    cues_ = std::make_unique<MusicCues>(*res_, exe);
    pz_ = std::make_unique<PzSnd>(*driver_, archive);
    locked([&] {
        const int device = soundDevice(cfg_.mode);
        caps_ = driver_->init(device, cfg_.mode != SoundMode::Speaker && cfg_.adlibPresent);
        fmPresent_ = (caps_ >> 2) & 1;
        if (device == SndDriver::kDeviceOff) driver_->silence();
        res_->setFmPresent(fmPresent_);
        pz_->setFmPresent(fmPresent_);
        res_->init();
    });
}

GgAudio::~GgAudio() {
    if (device_) device_->close();
}

void GgAudio::locked(const std::function<void()>& fn) {
    device_->withRenderer([&](dos::ChipRenderer&) {
        command_ = true;
        oplLog_.setCommand(true);
        fn();
        oplLog_.setCommand(false);
        command_ = false;
    });
}

bool GgAudio::open(uint32_t sdlDevice) { return device_->open(sdlDevice, std::string(soundModeName(cfg_.mode))); }
void GgAudio::close() { device_->close(); }
bool GgAudio::isOpen() const { return device_->isOpen(); }
void GgAudio::setVolume(float v) { device_->setVolume(v); }
void GgAudio::setOutputEnabled(bool on) { device_->setOutputEnabled(on); }

int GgAudio::loadMusicBank(uint16_t id) { return call([&](SndRes& r) { return r.loadMusicBank(id); }); }
int GgAudio::loadEffectBank(uint16_t id) { return call([&](SndRes& r) { return r.loadEffectBank(id); }); }
int GgAudio::loadSong(uint16_t id) { return call([&](SndRes& r) { return r.loadSong(id); }); }
void GgAudio::freeMusicBank(int h) { call([&](SndRes& r) { r.freeMusicBank(h); }); }
void GgAudio::freeEffectBank(int h) { call([&](SndRes& r) { r.freeEffectBank(h); }); }
void GgAudio::freeSong(int h) { call([&](SndRes& r) { r.freeSong(h); }); }
void GgAudio::playMusic(int h, int section, int loop) { call([&](SndRes& r) { r.playMusic(h, section, loop); }); }
void GgAudio::playEffect(int h, int loop, int priority) { call([&](SndRes& r) { r.playEffect(h, loop, priority); }); }
void GgAudio::stopMusic(int h) { call([&](SndRes& r) { r.stopMusic(h); }); }
void GgAudio::stopEffect(int h) { call([&](SndRes& r) { r.stopEffect(h); }); }
void GgAudio::stopAllEffects() { call([&](SndRes& r) { r.stopAllEffects(); }); }
int GgAudio::isPlaying(int h) { return call([&](SndRes& r) { return r.isPlaying(h); }); }
void GgAudio::selectArrangement(int h, int n) { call([&](SndRes& r) { r.selectArrangement(h, n); }); }
void GgAudio::muteChannel(int h, int channel) { call([&](SndRes& r) { r.muteChannel(h, channel); }); }
void GgAudio::unmuteChannel(int h, int channel) { call([&](SndRes& r) { r.unmuteChannel(h, channel); }); }
void GgAudio::setEffectBank(int h) { call([&](SndRes& r) { r.setEffectBank(h); }); }
int GgAudio::musicTick(int h) { return call([&](SndRes& r) { return r.driver().getTick(r.slot(h)); }); }
void GgAudio::toggleSound() { call([&](SndRes& r) { r.toggleOption(); }); }
void GgAudio::setSoundMuted(int muted) {
    call([&](SndRes& r) {
        r.setSoundMuted(muted);
        r.applySoundOption();
    });
}
int GgAudio::soundMuted() { return call([&](SndRes& r) { return r.soundMuted(); }); }
void GgAudio::pause() { call([&](SndRes& r) { r.pause(); }); }
void GgAudio::resume() { call([&](SndRes& r) { r.resume(); }); }
void GgAudio::shutdown() { call([&](SndRes& r) { r.driver().off(); }); }

size_t GgAudio::advanceTo(uint64_t tick, std::vector<float>* out) {
    if (device_->isOpen()) return 0;
    size_t n = 0;
    std::vector<float> scratch;
    const bool synth = out != nullptr;
    renderer_->setOplSynthesis(synth);
    while (driver_->tickIndex() < tick) {
        n += device_->renderOfflineTicks(1, synth ? *out : scratch);
        scratch.clear();
    }
    renderer_->setOplSynthesis(true);
    return n;
}

size_t GgAudio::renderTicks(uint32_t n, std::vector<float>& out) { return device_->renderOfflineTicks(n, out); }

uint64_t GgAudio::tickIndex() const {
    if (!device_->isOpen()) return driver_->tickIndex();
    uint64_t t = 0;
    device_->withRenderer([&](dos::ChipRenderer&) { t = driver_->tickIndex(); });
    return t;
}

void GgAudio::clearLogs() {
    oplLog_.clear();
    speakerLog_.clear();
}

}  // namespace tmport::gg
