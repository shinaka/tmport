#pragma once
// Wave 2 preview, kept behind the hidden option --dos-preview (dos_entry.cpp); the game itself is
// dos/game/ (wave 3).
//
// What the 1990 DOS edition shows until its game logic exists: the screens that the composer of
// dos/gfx/dos_screens.h rebuilds pixel for pixel from the user's files, one after the other, with
// the sound driver of dos/audio playing what the original plays at that point. It runs as the
// DosRuntime's client (virtual PIT, one original frame per game tick) and reads the keyboard the
// way the game does: PcKeyboard -> Kbd_ISR -> BIOS queue -> Input_Poll (one key word per game tick).
//
//   screen     composed from                                 sound calls, as the original makes them there
//   title      defaultScreenSpec(Title, look)                Title_Run 4ba4:0434: loop on, Music_Play(1)
//   sign-in    the roster of the engine's DOS save           Title_Run end: loop off; g_uiMode 8, so
//                                                            Sound_SyncState pauses the music and the effect
//   clubhouse  a new game (47c7:0b19)                        Game_Start 47c7:08b5: area 5, Music_Stop, Sfx_Stop,
//                                                            Music_Play(tune(5) = 1), again via DS:5BC5
//   quiz       the catch position of ega/029, puzzle 61      out of the clubhouse (Cell_P12_StartRoom): area 0,
//                                                            Music_Stop, Music_Play(0); g_uiMode 2 pauses it
//   path       level 1 at the locked tree trunk              g_uiMode 0: the music runs again
//   castle     the ladder shaft                              Cell_P42_GateL2 176a:03aa: area 0x0D, Music_Play(2)
//
// tune(area) = 0 below area 4, 1 below 7, 2 below 15 (code_ui.md §6.2). The game screens use the
// saved game of record 1 for the hiding places and castle layout (dos_screens.h), so with a roster
// that has none they show the level-1 layout of SST.EXE.
//
// Keys: Enter or Space (or a left click) shows the next screen, after the castle the title again;
// 0-9, '-' and '=' play effects 0..11 (Sfx_Play); Ctrl+S is Game_HandleHotkeys 43cb:03cb (music
// and effects on or off; like the original only on the title or with no UI mode open); Esc quits.
// On the title the mouse pointer follows the mouse once it moves.

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "dos/app/dos_runtime.h"
#include "dos/audio/dos_audio_device.h"
#include "dos/gfx/dos_screens.h"
#include "dos/input/input_poll.h"
#include "edition/dos_options.h"

namespace tmport::dos {

// The sound driver for the chosen device on the files of `files` (SFX/TUNES per fmt_sound.md §2),
// with the game's rand() for the AdLib orchestration draw; nullptr for SoundDevice::Off. Nothing is
// opened: call open() for a real device, or pull samples with the offline functions.
std::unique_ptr<DosAudioDevice> makeDosAudioDevice(const DosFiles& files, SoundDevice device,
                                                   std::function<int()> rand = {});

// The screens of the preview, in the order Enter shows them.
inline constexpr DosScreen kPreviewScreens[] = {DosScreen::Title,  DosScreen::SignIn, DosScreen::Clubhouse,
                                                DosScreen::Quiz,   DosScreen::Path,   DosScreen::Castle};

class DosPreview final : public RuntimeClient {
public:
    // A finished 320x200 frame (RGBA32, the gfx::Presenter convention) of screen `s`.
    using FrameSink = std::function<void(DosScreen s, std::span<const uint32_t> rgba)>;

    // `roster`: a 3243-byte USERS.DAT image (empty: the folder's USERS.DAT). `audio` may be null
    // (no sound). With offlineAudio the preview renders the sound itself, one PIT tick per
    // soundTick() (headless runs; nothing is opened), otherwise the device's SDL stream pulls it.
    DosPreview(const DosGfxFiles& files, const DosOptions& options, DosRuntime& runtime, std::vector<uint8_t> roster,
               DosAudioDevice* audio, bool offlineAudio);

    void setFrameSink(FrameSink sink) { sink_ = std::move(sink); }
    // Shows the first screen (title) with its sound calls and hands its frame to the sink.
    void start();

    // RuntimeClient
    void soundTick() override;
    void gameTick(const TickInfo& info) override;
    void frameBody() override;
    uint8_t uiMode() const override { return uiMode_; }
    bool quitRequested() const override { return quit_; }

    DosScreen screen() const { return kPreviewScreens[index_]; }
    size_t screensShown() const { return shown_; }
    // The offline sound since the start (offlineAudio only; float samples of the device's rate).
    const std::vector<float>& offlineSamples() const { return samples_; }
    void keepOfflineSamples(bool keep) { keepSamples_ = keep; }
    bool musicOn() const { return musicOn_; }
    uint16_t area() const { return area_; }

private:
    void show(size_t index);
    void enterScreen(DosScreen s);
    void compose();
    void handleKey(uint16_t keyWord, int16_t command);
    // 43cb:03cb Game_HandleHotkeys, command 14 (Ctrl+S).
    void toggleSound();
    // 43cb:046e Sound_SyncState.
    void syncSound();
    void musicPlay(int tune);
    void musicStop();
    void sfxStop();
    void silenceAll();
    static int tuneForArea(uint16_t area);

    const DosGfxFiles& files_;
    DosOptions options_;
    DosRuntime& runtime_;
    std::vector<uint8_t> roster_;
    DosAudioDevice* audio_;
    bool offlineAudio_;
    bool keepSamples_ = false;
    std::vector<float> samples_;
    std::vector<float> scratch_;
    LookPalette palette_;
    InputTables inputTables_;
    InputPoll input_;
    FrameSink sink_;

    size_t index_ = 0;
    size_t shown_ = 0;
    bool dirty_ = true;
    bool quit_ = false;
    // The original's globals that the preview's sound calls use.
    uint8_t uiMode_ = 0x80;      // g_uiMode DS:9119
    uint8_t lastUiMode_ = 0;     // g_lastUiMode DS:9300 (BSS)
    uint16_t area_ = 6;          // g_area DS:910B
    bool musicOn_ = true;        // g_musicOn DS:63B6
    bool soundOn_ = true;        // g_soundOn DS:63B4
    bool soundChanged_ = false;  // g_soundChanged DS:5BC5
    // Input of the current frame (Input_Poll in the game tick, used by the frame body).
    std::vector<std::pair<uint16_t, int16_t>> keys_;
    uint16_t prevButtons_ = 0;
    bool clicked_ = false;
    // The title's mouse pointer: the capture's position until the mouse moves.
    uint16_t lastMouseX_ = 0, lastMouseY_ = 0;
    bool pointerFollows_ = false;
};

}  // namespace tmport::dos
