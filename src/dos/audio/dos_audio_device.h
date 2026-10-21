#pragma once
// DOS edition sound output: the driver (DosSoundRenderer) run inside an SDL3 audio stream's pull
// callback (the engine's ChipAudioDevice, mischief/dos/audio/chip_audio_device.h), so the sequencer
// advances by sample count at the PIT rate whatever the frame rate is (fmt_sound.md §9,
// PORTING_NOTES.md §2.3). The game calls the driver's entry points from its
// own thread; they are queued and take effect right before the next PIT tick of the audio thread,
// as the original's calls from the main loop take effect at the next timer interrupt. A call can
// carry a delay in PIT ticks after that one (wave 5): the game makes a long frame's calls when the
// frame starts, the original partway through it (DosRuntime::soundDelay); a delayed command waits in
// the queue, and commands take effect in the order of their PIT tick (pushed order within one).
//
// Offline (headless) use opens no device: the caller pulls samples with renderOffline(), or with
// renderOfflineTicks() in whole PIT ticks (5 per game tick) for deterministic captures.
//
// AdLib and rand(): Music_Play on the AdLib device draws one rand() for the orchestration
// (4ac9:0002) whenever the music is on, which shifts the game's random sequence. musicPlay()
// draws it on the caller's thread through the `rand` function given to the constructor (the
// game's Turbo C generator), so the game sees the same sequence as on a real AdLib machine.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "dos/audio/dos_sound_renderer.h"
#include "mischief/dos/audio/chip_audio_device.h"

namespace tmport::dos {

// render (DosRenderConfig), sdlDriver ("dummy" in tests: never audible; empty = SDL's choice) and
// volume (master gain on the synth levels, speaker square +-0.5; 0.7).
using DosAudioConfig = mischief::dos::ChipAudioConfig;

class DosAudioDevice {
public:
    // `bank` is shared with the renderer. `rand` = the game's rand() (0..0x7FFF); empty = no draw.
    DosAudioDevice(std::shared_ptr<const SoundBank> bank, DosAudioConfig cfg, std::function<int()> rand = {});
    ~DosAudioDevice();
    DosAudioDevice(const DosAudioDevice&) = delete;
    DosAudioDevice& operator=(const DosAudioDevice&) = delete;

    // Opens an SDL playback stream (float mono at the configured rate) with the pull callback, on
    // the default device or, with `sdlDevice` != 0, bound to that already open logical device.
    // Returns false (and logs) when there is no device; the commands still work and renderOffline()
    // can be used.
    bool open(uint32_t sdlDevice = 0);
    void close() { output_->close(); }
    bool isOpen() const { return output_->isOpen(); }
    std::string driverName() const { return output_->driverName(); }

    // ---- the driver's entry points (thread safe; applied before the next PIT tick, or `delay` PIT
    // ticks after it)
    void sfxPlay(int id, uint32_t delay = 0);       // 491b:06d7 Sfx_Play
    void sfxStop(uint32_t delay = 0);               // 491b:074b Sfx_Stop
    void musicPlay(int tune, uint32_t delay = 0);   // 491b:0791 Music_Play (AdLib: draws rand() here when the music is on)
    void musicStop(uint32_t delay = 0);             // 491b:0811 Music_Stop
    void silenceAll(uint32_t delay = 0);            // 491b:0854 Sound_SilenceAll
    void setSoundOn(bool on, uint32_t delay = 0);   // DS:63b4 g_soundOn
    void setMusicOn(bool on, uint32_t delay = 0);   // DS:63b6 g_musicOn
    void setMusicPaused(bool paused, uint32_t delay = 0);  // DS:37ce (0 while g_uiMode != 0, Sound_SyncState 43cb:046e)
    void setMusicLoop(bool on, uint32_t delay = 0);        // DS:63ba
    void setSfxLoop(bool on, uint32_t delay = 0);          // DS:63b8 (the end sequence loops effect 2, player states 44/45/64/65)
    // A DOSBox-X state's driver (wave 4): the effect block DS:6448 and the music block DS:64c0 (0x78
    // bytes each, as in DGROUP) and the voice count DS:63be, applied before the next PIT tick like the
    // other commands; the effect and the tune continue where the state left them (Sequencer::restore).
    // The flags (DS:63b4..63ba, DS:37ce) are set with the calls above.
    void restoreBlocks(std::span<const uint8_t> sfxBlock, std::span<const uint8_t> musicBlock, int voices,
                       int adlibVariant = 0);
    // A DOSBox-X state's AdLib driver (wave 5): its variables from the state's DGROUP image (DS:0000..,
    // the range of AdLibDriver::State), applied before the next PIT tick; the OPL registers are written
    // again from them, and the next restoreBlocks() then sends no AdLib_PlayTune and no notes. Nothing
    // on the speaker and the Tandy.
    void restoreOplDriver(std::span<const uint8_t> dgroup);

    // ---- engine-side controls (no effect on the driver state)
    void setVolume(float v) { output_->setVolume(v); }
    float volume() const { return output_->volume(); }
    // Off: the output is silent but the sequencer keeps running (timing is unchanged).
    void setOutputEnabled(bool on) { output_->setOutputEnabled(on); }
    // Off: the OPL2 is not synthesised (the AdLib driver still writes its registers): for headless
    // runs whose samples are thrown away (the emulated chip is the costly part). Speaker and Tandy
    // output are not affected.
    void setOplSynthesis(bool on);

    // ---- headless (only while no SDL stream is open)
    // Renders n float samples.
    size_t renderOffline(float* out, size_t n) { return output_->renderOffline(out, n); }
    // Appends the rest of the current PIT tick's samples (if renderOffline stopped inside one) and
    // those of the next `ticks` ticks (about 202.8 each at 48 kHz); returns their number. Lockstep
    // capture for a runtime that drives its virtual PIT itself. Commands
    // queued before the call take effect at the first of these ticks (a delayed one that many ticks
    // later), as a call made in a game tick reaches the sound tick of the next interrupt in the
    // original (code_runtime.md §2.3).
    size_t renderOfflineTicks(uint32_t ticks, std::vector<float>& out) { return output_->renderOfflineTicks(ticks, out); }

    struct Status {
        uint16_t sfxStatus = 0, sfxId = Sequencer::kIdle;
        uint16_t musicStatus = 0, musicId = Sequencer::kIdle;
        uint64_t ticks = 0;
    };
    // Driver state after the last rendered tick (approximate while commands are pending).
    Status status() const;
    SoundCard device() const { return bank_->device; }
    int sampleRate() const { return cfg_.render.sampleRate; }

private:
    struct Command {
        enum class Kind : uint8_t { SfxPlay, SfxStop, MusicPlay, MusicStop, SilenceAll, SoundOn, MusicOn, MusicPaused, MusicLoop,
                                    SfxLoop, Restore, OplRestore };
        Kind kind;
        int a = 0;
        int b = 0;
        uint32_t delay = 0;  // PIT ticks still to wait (Restore and OplRestore: always 0)
    };
    void push(Command c);
    void applyPending();  // audio side, before a tick: the commands due, the others one tick closer
    void publishStatus();

    std::shared_ptr<const SoundBank> bank_;
    DosAudioConfig cfg_;
    std::function<int()> rand_;
    bool musicOnMirror_ = true;  // the caller's copy of g_musicOn (for the AdLib rand() draw)

    std::unique_ptr<DosSoundRenderer> renderer_;
    std::unique_ptr<mischief::dos::ChipAudioDevice> output_;  // after the renderer it plays (destroyed first)

    std::mutex queueMutex_;
    std::vector<Command> queue_;
    struct Restore {
        Sequencer::Block sfx, music;
        int voices = 4, variant = 0;
    };
    std::vector<Restore> restores_;  // the payloads of queued Restore commands (in order)
    std::vector<std::vector<uint8_t>> oplRestores_;  // the payloads of queued OplRestore commands
    bool oplRestored_ = false;  // audio side: an OplRestore was applied, the next Restore sends no notes
    std::atomic<bool> pending_{false};

    std::atomic<uint64_t> statusWord_{0};  // sfx status/id, music status/id packed
    std::atomic<uint64_t> statusTicks_{0};
};

}  // namespace tmport::dos
