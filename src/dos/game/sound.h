#pragma once
// The game's view of the sound driver (segment 491b, fmt_sound.md §5.2a, §7): the entry points the
// game calls and the driver flags it sets, behind an interface so that the real device
// (DosAudioDevice, dos/audio/dos_audio_device.h) and a recording double for tests are
// interchangeable. Game code calls the free functions at the end of this header, which keep the
// DGROUP copies of the flags (CoreState::soundOn ...) and the device in step, exactly where the
// original writes DS:63B4 / 63B6 / 63B8 / 63BA / 37CE.
//
// Owner: mainloop agent (the wiring in runDosEdition); the facade itself is plumbing, not logic.

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tmport::dos {

class DosAudioDevice;
class DosGame;

class SoundFacade {
public:
    virtual ~SoundFacade() = default;
    virtual void sfxPlay(int id) = 0;          // 491b:06d7 Sfx_Play
    virtual void sfxStop() = 0;                // 491b:074b Sfx_Stop
    virtual void musicPlay(int tune) = 0;      // 491b:0791 Music_Play (AdLib: one rand() when the music is on)
    virtual void musicStop() = 0;              // 491b:0811 Music_Stop
    virtual void silenceAll() = 0;             // 491b:0854 Sound_SilenceAll
    virtual void setSoundOn(bool on) = 0;      // DS:63B4 g_soundOn
    virtual void setMusicOn(bool on) = 0;      // DS:63B6 g_musicOn
    virtual void setSfxLoop(bool on) = 0;      // DS:63B8 g_sfxLoop
    virtual void setMusicLoop(bool on) = 0;    // DS:63BA g_musicLoop
    virtual void setMusicGate(uint16_t m) = 0; // DS:37CE g_musicGate (0 pauses the music, 0xFFFF runs it)
    // The effect block status DS:6448 != 0 as the game sees it (set at once by Sfx_Play; read by
    // Cell_P70_TreasureRoom 176a:0596).
    virtual bool sfxActive() const = 0;
    // One PIT tick of the timer interrupt (Timer_ISR step 4); a device that renders by sample count
    // in its audio callback ignores it, an offline renderer advances one tick.
    virtual void pitTick() {}
    // A DOSBox-X state's driver (wave 4, load_state.cpp): the effect block DS:6448 and the music block
    // DS:64C0 (0x78 bytes each, the DGROUP layout) and the voice count DS:63BE; the effect and the
    // tune go on where the state left them (Sequencer::restore). A wrapper facade forwards it.
    virtual void restoreDriver(std::span<const uint8_t> /*sfxBlock*/, std::span<const uint8_t> /*musicBlock*/,
                               int /*voices*/) {}
    // A DOSBox-X state's AdLib driver (wave 5): its variables (orchestration, instruments, the channels'
    // pitches and keys) from the state's whole DGROUP image; called right before restoreDriver, which
    // then sends no AdLib_PlayTune and no notes (DosAudioDevice::restoreOplDriver). A wrapper facade
    // forwards it.
    virtual void restoreOplDriver(std::span<const uint8_t> /*dgroup*/) {}
};

// A facade that only records the calls (tests, headless runs without sound).
class RecordingSound final : public SoundFacade {
public:
    enum class Op : uint8_t { SfxPlay, SfxStop, MusicPlay, MusicStop, SilenceAll, SoundOn, MusicOn, SfxLoop, MusicLoop, MusicGate,
                              Restore };
    struct Call {
        Op op = Op::SfxPlay;
        int arg = 0;
        bool operator==(const Call&) const = default;
    };

    void sfxPlay(int id) override;
    void sfxStop() override;
    void musicPlay(int tune) override;
    void musicStop() override;
    void silenceAll() override;
    void setSoundOn(bool on) override;
    void setMusicOn(bool on) override;
    void setSfxLoop(bool on) override;
    void setMusicLoop(bool on) override;
    void setMusicGate(uint16_t m) override;
    bool sfxActive() const override { return sfxActive_; }
    void pitTick() override { ++pitTicks_; }
    // Recorded as Restore with the music block's tune id (0xFFFF: none playing); the effect status is
    // the block's.
    void restoreDriver(std::span<const uint8_t> sfxBlock, std::span<const uint8_t> musicBlock, int voices) override;

    const std::vector<Call>& calls() const { return calls_; }
    void clear() {
        calls_.clear();
        delays_.clear();
    }
    // Tests of the frame timing: the delay each call was made with (DosAudioSound::setDelaySource),
    // parallel to calls(); empty while no source is set.
    void setDelaySource(std::function<uint32_t()> delay) { delay_ = std::move(delay); }
    const std::vector<uint32_t>& delays() const { return delays_; }
    // The model of the effect status: set by sfxPlay, cleared by sfxStop/silenceAll and here.
    void setSfxActive(bool a) { sfxActive_ = a; }
    uint64_t pitTicks() const { return pitTicks_; }
    // "sfx 4, music 1, ..." for test messages.
    std::string describe() const;

private:
    void record(Op op, int arg);

    std::vector<Call> calls_;
    std::vector<uint32_t> delays_;
    std::function<uint32_t()> delay_;
    bool sfxActive_ = false;
    uint64_t pitTicks_ = 0;
};

// The real driver. With `offline` the device has no SDL stream and pitTick() renders one PIT tick
// of samples (kept in samples() when keepSamples is on), as the headless runs need.
class DosAudioSound final : public SoundFacade {
public:
    DosAudioSound(DosAudioDevice& device, bool offline);

    // Where a call takes effect (wave 5): the PIT ticks after the next timer interrupt that each call
    // waits, asked when it is made (DosRuntime::soundDelay: a long frame's calls sound where the
    // original's frame made them). Unset: every call at the next interrupt.
    void setDelaySource(std::function<uint32_t()> delay) { delay_ = std::move(delay); }

    void sfxPlay(int id) override;
    void sfxStop() override;
    void musicPlay(int tune) override;
    void musicStop() override;
    void silenceAll() override;
    void setSoundOn(bool on) override;
    void setMusicOn(bool on) override;
    void setSfxLoop(bool on) override;
    void setMusicLoop(bool on) override;
    void setMusicGate(uint16_t m) override;
    bool sfxActive() const override;
    void pitTick() override;
    void restoreDriver(std::span<const uint8_t> sfxBlock, std::span<const uint8_t> musicBlock, int voices) override;
    void restoreOplDriver(std::span<const uint8_t> dgroup) override;

    // Offline: whether the samples are kept (samples()); when they are not, the OPL2 is not
    // synthesised either (DosAudioDevice::setOplSynthesis), as nobody hears it.
    void keepSamples(bool keep);
    const std::vector<float>& samples() const { return samples_; }

private:
    uint32_t delay() const { return delay_ ? delay_() : 0; }

    DosAudioDevice& device_;
    bool offline_;
    std::function<uint32_t()> delay_;
    bool keepSamples_ = false;
    bool soundOn_ = true;
    // The last Sfx_Play / Sfx_Stop and the device's tick count when it was made: until the device
    // has rendered a tick after it, its status does not show it yet.
    enum class Pending : uint8_t { None, Play, Stop };
    Pending pending_ = Pending::None;
    uint64_t pendingTicks_ = 0;
    std::vector<float> samples_;
    std::vector<float> scratch_;
};

// ------------------------------------------------------------------------- game-side calls
// What game code calls: the driver entry point plus the DGROUP copy of the flag it changes.

void sfxPlay(DosGame& g, int id);          // 491b:06d7 Sfx_Play
void sfxStop(DosGame& g);                  // 491b:074b Sfx_Stop
void musicPlay(DosGame& g, int tune);      // 491b:0791 Music_Play (tune < 0: nothing, see tuneForArea)
void musicStop(DosGame& g);                // 491b:0811 Music_Stop
void soundSilenceAll(DosGame& g);          // 491b:0854 Sound_SilenceAll
void setSoundOn(DosGame& g, bool on);      // g_soundOn DS:63B4 = on
void setMusicOn(DosGame& g, bool on);      // g_musicOn DS:63B6 = on
void setSfxLoop(DosGame& g, bool on);      // g_sfxLoop DS:63B8 = on (end sequence states 44/45/64/65)
void setMusicLoop(DosGame& g, bool on);    // g_musicLoop DS:63BA = on (Title_Run, Scene_Update)
void setMusicGate(DosGame& g, uint16_t m); // g_musicGate DS:37CE = m (Sound_SyncState)
bool sfxBlockActive(const DosGame& g);     // DS:6448 != 0
// Pushes every flag of CoreState to the facade (after loadFromDgroup or a state restore).
void syncSoundFlags(DosGame& g);
// A DOSBox-X state's sound driver from its DGROUP image (DS:0000..): the blocks DS:6448 / DS:64C0 and
// the voice count DS:63BE through SoundFacade::restoreDriver (after syncSoundFlags), and before them
// the AdLib driver's variables (SoundFacade::restoreOplDriver: the orchestration and instruments of the
// playing tune, the channels' pitches and keys).
void soundRestoreDriver(DosGame& g, std::span<const uint8_t> dgroup);
// The tune of a music area (code_ui.md §6.2): 0 below area 4, 1 below 7, 2 below 15, else -1.
int tuneForArea(uint16_t area);

}  // namespace tmport::dos
