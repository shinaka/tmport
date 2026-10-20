#pragma once
// G&G sound output: the BMF driver (snd_driver.h) and the bank layer (snd_res.h) on the core's
// DOS sound hardware (dos/audio/chip_renderer.h: the PC speaker on PIT channel 2, an emulated OPL2)
// and audio device (dos/audio/chip_audio_device.h: an SDL3 stream whose pull callback renders by
// sample count, or offline). It also makes the start-up of the original's sound
// (Init_CommandLine 303a:005c step 7: Snd_Init, g_fmPresent, Snd_Silence for -SN; Game_Init:
// SndRes_Init) and holds the music cue callbacks (music_cues.h) and the puzzles' helpers (pz_snd.h).
//
// Clock: one driver tick (Snd_Tick 1bb4:0db3) per sound tick of 2 PIT ticks at divisor 2000h =
// 72.826 Hz (code_runtime.md §1, code_sound.md §5). Two ways to run it:
//   * real time: open() starts the SDL stream; the audio thread ticks the driver by sample count,
//     whatever the frame rate is;
//   * on the caller's thread (no stream open): advanceTo() / renderTicks() run the ticks (and render
//     their samples, or not), e.g. from the runtime's VirtualPit on every even PIT tick for a
//     deterministic run, or from the host clock when no audio device could be opened.
// Game calls (every method below that is not marked otherwise) run synchronously with the renderer
// locked, so they take effect between two sound ticks, as the original's API calls do (they lock
// the driver with g_sndBusy); their results (handles, Snd_IsActive, the song tick) are immediate.
// The cue callbacks run inside the tick on the render side.
//
// Modes (PORTING_NOTES §7, code_sound.md §3.1): fm = -SA / no switch with a card (FM banks id + 1
// on the OPL2), speaker = -SI (speaker banks id, no OPL), off = -SN (the FM banks played silently:
// Snd_Silence stops every key, frequency, BD and speaker write; patch and volume writes go on).

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "dos/audio/chip_audio_device.h"
#include "dos/audio/opl_log.h"
#include "gg/audio/music_cues.h"
#include "gg/audio/pz_snd.h"
#include "gg/audio/snd_driver.h"
#include "gg/audio/snd_res.h"
#include "gg/audio/sound_mode.h"

namespace tmport::dos {
class OplChip;
}

namespace tmport::gg {

class Archive;
class GgExe;

// g_soundDevice for Snd_Init: off 0, speaker 1, fm -1 (detect; with the emulated card the same as 4).
int soundDevice(SoundMode m);

// The sound tick rate: PIT 1193181.67 / 2000h / 2 = 72.826 Hz.
double ggSoundTickHz();

struct GgAudioConfig {
    SoundMode mode = SoundMode::Fm;
    bool adlibPresent = true;  // the emulated card answers AdLib_Detect (off and fm)
    dos::ChipRenderConfig render;  // tickHz is set to ggSoundTickHz()
    // Time of one AdLib_Write on the reference machine (DOSBox-X, 486 at 26800 cycles): the writes
    // of a tick reach the chip this far apart (fmt_sound.md §5).
    double oplWriteSeconds = 0.0;  // 0: kOplWriteSeconds
    std::string sdlDriver;         // SDL audio driver to force ("dummy" in tests: never audible)
    float volume = 0.7f;
    bool logOpl = false;      // keep every OPL register write (oplLog)
    bool logSpeaker = false;  // keep every speaker port write (speakerLog)
};

// One speaker port write: OUT 42h (divisor) or OUT 61h (the gate bits).
struct SpeakerWrite {
    uint64_t tick = 0;     // the sound tick it belongs to (a game call: the tick it runs before)
    bool port61 = false;   // false: divisor
    uint16_t value = 0;
    bool command = false;  // made by a game call between two ticks
};

class GgAudio {
public:
    // One AdLib_Write (OUT, 6 status reads, OUT, 35 status reads) on the reference machine: 65 us,
    // fitted on the title captures (7 809 writes in ticks: 45 of their whole-ms stamps off; fmt_sound.md §5.2).
    static constexpr double kOplWriteSeconds = 65e-6;

    // Runs the start-up (Snd_Init, g_fmPresent, Snd_Silence for off, SndRes_Init with the global
    // effect bank). `exe` and `archive` must outlive the object. Throws tmport::FormatError for an
    // ssg.exe whose driver tables are not v1.0's.
    GgAudio(const GgExe& exe, const Archive& archive, const GgAudioConfig& cfg = {});
    ~GgAudio();
    GgAudio(const GgAudio&) = delete;
    GgAudio& operator=(const GgAudio&) = delete;

    SoundMode mode() const { return cfg_.mode; }
    uint16_t caps() const { return caps_; }     // Snd_Init's result
    int fmPresent() const { return fmPresent_; }  // g_fmPresent DS:a0d8

    // ---- real time
    bool open(uint32_t sdlDevice = 0);  // false (logged) without a device: then advance it yourself
    void close();
    bool isOpen() const;
    void setVolume(float v);
    void setOutputEnabled(bool on);  // engine mute (the driver keeps running)

    // ---- game calls (SndRes_* 1cdd; handles as the original's)
    int loadMusicBank(uint16_t id);
    int loadEffectBank(uint16_t id);
    int loadSong(uint16_t id);
    void freeMusicBank(int h);
    void freeEffectBank(int h);
    void freeSong(int h);
    void playMusic(int h, int section = 0, int loop = 1);
    void playEffect(int h, int loop, int priority);  // h = effect bank handle + k; priority + 3Fh
    void stopMusic(int h);
    void stopEffect(int h);
    void stopAllEffects();
    int isPlaying(int h);
    void selectArrangement(int h, int n);
    void muteChannel(int h, int channel);
    void unmuteChannel(int h, int channel);
    void setEffectBank(int h);  // g_effectBank DS:7a44
    int musicTick(int h);       // Snd_GetTick(SndRes_GetSlot(h)) (races read it)
    void toggleSound();         // Ctrl+S: Snd_ToggleOption (the game flips its g_soundOff itself)
    void setSoundMuted(int muted);  // DS:1ad4 from the player's option, then ApplySoundOption
    int soundMuted();
    void pause();     // Snd_Pause (dialogs, drop-downs)
    void resume();    // Snd_Resume
    void shutdown();  // Snd_Off (Game_Shutdown)

    // Any other call (the driver API, MusicCues, PzSnd), with the renderer locked; its OPL writes
    // are logged as a command's.
    template <class F>
    auto call(F&& fn) -> std::invoke_result_t<F&, SndRes&> {
        using R = std::invoke_result_t<F&, SndRes&>;
        if constexpr (std::is_void_v<R>) {
            locked([&] { fn(*res_); });
        } else {
            std::optional<R> r;
            locked([&] { r.emplace(fn(*res_)); });
            return std::move(*r);
        }
    }

    // ---- on the caller's thread (only while no stream is open)
    // Runs sound ticks until tickIndex() == tick (samples appended to out when given, else not
    // synthesised); returns the samples rendered. A runtime ticking the sound from its own PIT
    // calls step() on every even PIT tick: pit.onTick(2, [&](uint32_t) { audio.step(); }, 0).
    size_t advanceTo(uint64_t tick, std::vector<float>* out = nullptr);
    size_t step(std::vector<float>* out = nullptr) { return advanceTo(driver_->tickIndex() + 1, out); }
    // Renders the next n ticks (the rest of the current one first, if a render stopped inside it).
    size_t renderTicks(uint32_t n, std::vector<float>& out);
    // Sound ticks run so far (Snd_Tick calls, the lost ones included).
    uint64_t tickIndex() const;

    // ---- logs (read them while no stream is open)
    const dos::OplWriteLog& oplLog() const { return oplLog_; }
    const std::vector<SpeakerWrite>& speakerLog() const { return speakerLog_; }
    void clearLogs();

    // ---- parts (while a stream is open, use them inside call())
    SndDriver& driver() { return *driver_; }
    SndRes& res() { return *res_; }
    MusicCues& cues() { return *cues_; }
    PzSnd& puzzles() { return *pz_; }
    dos::ChipRenderer& renderer() { return *renderer_; }
    dos::ChipAudioDevice& device() { return *device_; }
    int sampleRate() const { return cfg_.render.sampleRate; }

private:
    class Ports;
    void locked(const std::function<void()>& fn);

    GgAudioConfig cfg_;
    dos::OplWriteLog oplLog_;
    std::vector<SpeakerWrite> speakerLog_;
    std::unique_ptr<dos::OplChip> chip_;
    std::unique_ptr<dos::ChipRenderer> renderer_;
    std::unique_ptr<Ports> ports_;
    std::unique_ptr<SndDriver> driver_;
    std::unique_ptr<dos::ChipAudioDevice> device_;
    std::unique_ptr<SndRes> res_;
    std::unique_ptr<MusicCues> cues_;
    std::unique_ptr<PzSnd> pz_;
    uint16_t caps_ = 0;
    int fmPresent_ = 0;
    bool command_ = false;
};

}  // namespace tmport::gg
