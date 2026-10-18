#pragma once
// AudioSystem: everything the game hears, with the original's rules (re/docs/fmt_sound.md,
// PORTING_NOTES.md §4.9):
//   * one wave voice shared by sound effects and speech (a new sound cuts the current one);
//   * Sfx_Play (SSND 7000+n, fire-and-forget) and the blocking, interruptible speech wait
//     Wave_PlayResUntilEvent (stop-code lists read from the EXE);
//   * MIDI music: own SMF sequencer -> MidiBackend, the 1090 driver and the 1128 zone manager;
//   * the two settings (Music and Sound / Voice), Ctrl+S, options and sign-in rules;
//   * pause/resume around dialogs, menus and focus loss (10d0, 1010:05c8/05db).
//
// Integration with the engine: connectAudioToEngine() (audio/engine_input_port.h) installs the
// Ctrl+S hook of the event fetch (toggleAll) and the focus hook (appDeactivate/appActivate);
// EngineInputPort runs the speech waits on the engine. Wired by the app (app/engine_glue.h):
// service() runs from an Engine pump hook (Engine::addPumpHook, every pump including frozen ones:
// MM_WOM_DONE / MM_MCINOTIFY, looping, OPEN1 -> OPEN2); the in-engine menu loop runs in
// Engine::holdWhile, which calls the activation hook (appDeactivate() / appActivate()); game
// dialogs call pauseAll() / resumeAll() through the DialogSystem hooks (their 10d0 nesting lives in
// DialogSystem). Still to be wired by their owners (TODO(integration)):
//   * users.dat load/save -> settings(), applySignInSettings(), onNewPlayer().

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "audio/audio_tables.h"
#include "audio/input_port.h"
#include "audio/midi_backend.h"
#include "audio/midi_driver.h"
#include "audio/midi_player.h"
#include "audio/music_manager.h"
#include "audio/sound.h"
#include "audio/wave_output.h"
#include "audio/wave_voice.h"

namespace tmport {
struct GameFiles;
}

namespace tmport::audio {

struct AudioConfig {
    // SDL audio driver for the wave voice: "" = SDL's default; "dummy" = silent, real-time
    // (tests, headless). The SDL_AUDIO_DRIVER environment variable applies when empty.
    std::string sdlAudioDriver;
    bool waveDevice = true;  // false: no wave device at all (sounds finish at once)
    MidiBackendKind midiBackend = MidiBackendKind::Auto;
    MusicSet musicSet = MusicSet::Extended;
    // SoundFont back end: the General MIDI .sf2 file (UTF-8 path; empty = none configured) and the
    // music volume (0..1). The music stream joins the wave voice's SDL device.
    std::string soundFontPath;
    float musicVolume = 1.0f;
};

// Injected parts (tests build these by hand; create() builds the real ones).
struct AudioParts {
    std::unique_ptr<WaveOutput> wave;     // default: NoWaveOutput
    std::unique_ptr<SoundLoader> sounds;  // required
    std::unique_ptr<MidiPlayer> midi;     // default: Null back end, threaded
    std::unique_ptr<TrackLoader> tracks;  // required
};

class AudioSystem {
public:
    // Real devices for the user's game files (which must outlive the AudioSystem). The wave
    // voice uses SDL (opened here); MIDI uses the configured back end on a sequencer thread.
    static std::unique_ptr<AudioSystem> create(GameFiles& files, const AudioConfig& config = {});

    AudioSystem(AudioTables tables, AudioParts parts, MusicSet set = MusicSet::Extended);
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Window-procedure duties, called from every pump: closes the wave device when a sound has
    // ended (MM_WOM_DONE -> Wave_Stop) and delivers the end-of-tune notification (MM_MCINOTIFY
    // -> Midi_OnPlayDone: loop or stop).
    void service();

    // --- Settings ----------------------------------------------------------------------------
    AudioSettings& settings() { return settings_; }
    bool soundOn() const { return settings_.soundOn != 0; }
    bool voiceOn() const { return settings_.voiceOn != 0; }
    void toggleSound();                    // 1128:012f Audio_ToggleSound
    void toggleAll();                      // 1128:0151 Audio_ToggleAll (Ctrl+S): both on only if both were off
    void setSoundFromOptions(bool on);     // 1140:00cc options dialog: toggles if the answer differs
    void setVoiceOn(bool on) { settings_.voiceOn = on ? 1 : 0; }  // 1140:00f7 (a playing sentence continues)
    // 1160:13ce..1415 sign-in: can switch Music and Sound off but never on; Voice is copied.
    void applySignInSettings(uint8_t recordSound, uint8_t recordVoice);
    void onNewPlayer();                    // 1160:1632: switches Music and Sound on if it is off

    // --- Wave voice: effects and speech ------------------------------------------------------
    WaveVoice& voice() { return voice_; }
    void sfxPlay(int n);                   // 1170:0000 Sfx_Play: SSND 7000+n if soundOn
    void sfxStop();                        // 1170:005b Sfx_Stop (immediate)
    bool sfxIsPlaying() { return voice_.isPlaying(); }  // 1170:0082
    // 1088:01a9 Wave_PlayResUntilEvent: plays the sound and waits until it ends or an input code
    // c arrives with (c & 0x0FFF) in stopCodes, or c == 0x13 (Ctrl+S). Non-matching codes are
    // consumed and discarded; the interrupting code stays queued for the caller. Returns true if
    // interrupted. Not gated by voiceOn (the callers check it); returns at once without a device.
    bool playUntilEvent(int soundId, std::span<const uint16_t> stopCodes, InputPort& in);
    // voiceOn ? playUntilEvent(id, the list) : false -- the pattern of every speech call site.
    bool speak(int soundId, StopList list, InputPort& in);
    // Speaks ids in order; an interruption skips the rest (fmt_sound.md §6.5 rule 6). voiceOn-gated.
    bool speakSequence(std::span<const int> soundIds, StopList list, InputPort& in);
    // Waits until the wave voice is idle without consuming input (1150:0602 / 080d: the jingle
    // before praise / wrong-answer speech is allowed to finish).
    void waitWhilePlaying(InputPort& in);
    // The jingle-then-speech rule as one call: Sfx_Play(sfx) (12 praise, 8 wrong answer), wait for
    // it to finish, then speakSequence. (The original draws the screen text between the first two
    // steps; callers needing that use the three primitives.)
    bool jingleThenSpeak(int sfx, std::span<const int> soundIds, StopList list, InputPort& in);
    // Starts a sound without waiting (intro narration, 1138:0283; the caller checks voiceOn and
    // polls voice().isPlaying()).
    void startSpeech(int soundId);
    const std::vector<uint16_t>& stopCodes(StopList l) const { return tables_.stopCodes(l); }

    // --- Music -------------------------------------------------------------------------------
    MusicManager& music() { return music_; }
    MidiDriver& midi() { return driver_; }
    MidiPlayer& midiPlayer() { return *midi_; }

    // --- Pause / resume ----------------------------------------------------------------------
    void pauseAll();       // 1090:0536 Sound_PauseAll: cut the wave voice, pause the music
    void resumeAll();      // 1090:0540 Sound_ResumeAll: resume the music at the saved position
    void pauseNested();    // 10d0:0000 (game message box / dialog entry)
    void resumeNested();   // 10d0:000e (resumes when the nesting returns to 0)
    void resumeForce();    // 10d0:0025
    void appDeactivate();  // 1010:05c8 App_Deactivate (focus lost, menu loop entered)
    void appActivate();    // 1010:05db App_Activate (focus regained, menu loop left)
    bool appActive() const { return driver_.appActive(); }
    int pauseDepth() const { return pauseDepth_; }

    const AudioTables& tables() const { return tables_; }

private:
    AudioTables tables_;
    AudioSettings settings_;
    std::unique_ptr<WaveOutput> wave_;
    std::unique_ptr<SoundLoader> sounds_;
    std::unique_ptr<MidiPlayer> midi_;
    std::unique_ptr<TrackLoader> tracks_;
    WaveVoice voice_;
    MidiDriver driver_;
    MusicManager music_;
    bool sfxHeld_ = false;   // DS:2ab2: an effect copy is held (until the next Sfx_Play / Sfx_Stop)
    int8_t pauseDepth_ = 0;  // DS:5cc8 (signed byte)
};

}  // namespace tmport::audio
