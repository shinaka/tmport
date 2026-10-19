#pragma once
// DOS edition sound output: the driver (DosSoundRenderer) run inside an SDL3 audio stream's pull
// callback, so the sequencer advances by sample count at the PIT rate whatever the frame rate is
// (fmt_sound.md §9, PORTING_NOTES.md §2.3). The game calls the driver's entry points from its
// own thread; they are queued and take effect right before the next PIT tick of the audio thread,
// as the original's calls from the main loop take effect at the next timer interrupt.
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
#include <string>
#include <vector>

#include "dos/audio/dos_sound_renderer.h"

struct SDL_AudioStream;

namespace tmport::dos {

struct DosAudioConfig {
    DosRenderConfig render;
    // SDL audio driver to force ("dummy" in tests: never audible); empty = SDL's choice.
    std::string sdlDriver;
    float volume = 0.7f;  // master gain on the synth levels (speaker square +-0.5)
};

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
    void close();
    bool isOpen() const { return stream_ != nullptr; }
    std::string driverName() const;

    // ---- the driver's entry points (thread safe; applied before the next PIT tick)
    void sfxPlay(int id);       // 491b:06d7 Sfx_Play
    void sfxStop();             // 491b:074b Sfx_Stop
    void musicPlay(int tune);   // 491b:0791 Music_Play (AdLib: draws rand() here when the music is on)
    void musicStop();           // 491b:0811 Music_Stop
    void silenceAll();          // 491b:0854 Sound_SilenceAll
    void setSoundOn(bool on);   // DS:63b4 g_soundOn
    void setMusicOn(bool on);   // DS:63b6 g_musicOn
    void setMusicPaused(bool paused);  // DS:37ce (0 while g_uiMode != 0, Sound_SyncState 43cb:046e)
    void setMusicLoop(bool on);        // DS:63ba
    void setSfxLoop(bool on);          // DS:63b8 (the end sequence loops effect 2, player states 44/45/64/65)

    // ---- engine-side controls (no effect on the driver state)
    void setVolume(float v) { volume_.store(v); }
    float volume() const { return volume_.load(); }
    // Off: the output is silent but the sequencer keeps running (timing is unchanged).
    void setOutputEnabled(bool on) { outputEnabled_.store(on); }

    // ---- headless (only while no SDL stream is open)
    // Renders n float samples.
    size_t renderOffline(float* out, size_t n);
    // Appends the rest of the current PIT tick's samples (if renderOffline stopped inside one) and
    // those of the next `ticks` ticks (about 202.8 each at 48 kHz); returns their number. Lockstep
    // capture for a runtime that drives its virtual PIT itself. Commands
    // queued before the call take effect at the first of these ticks, as a call made in a game
    // tick reaches the sound tick of the next interrupt in the original (code_runtime.md §2.3).
    size_t renderOfflineTicks(uint32_t ticks, std::vector<float>& out);

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
                                    SfxLoop };
        Kind kind;
        int a = 0;
        int b = 0;
    };
    void push(Command c);
    void applyPending();  // audio side, before a tick
    void renderFloat(float* out, size_t n);
    void publishStatus();
    static void callback(void* user, SDL_AudioStream* stream, int additional, int total);

    std::shared_ptr<const SoundBank> bank_;
    DosAudioConfig cfg_;
    std::function<int()> rand_;
    bool musicOnMirror_ = true;  // the caller's copy of g_musicOn (for the AdLib rand() draw)

    std::mutex renderMutex_;  // the renderer: audio thread, or the offline caller
    std::unique_ptr<DosSoundRenderer> renderer_;
    std::vector<double> scratch_;
    std::vector<float> floatBuf_;  // the callback's output block

    std::mutex queueMutex_;
    std::vector<Command> queue_;
    std::atomic<bool> pending_{false};

    std::atomic<float> volume_;
    std::atomic<bool> outputEnabled_{true};
    std::atomic<uint64_t> statusWord_{0};  // sfx status/id, music status/id packed
    std::atomic<uint64_t> statusTicks_{0};

    SDL_AudioStream* stream_ = nullptr;
    bool ownsDevice_ = false;
    bool sdlInit_ = false;
};

}  // namespace tmport::dos
