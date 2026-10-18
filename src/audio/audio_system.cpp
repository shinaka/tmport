#include "audio/audio_system.h"

#include "audio/sdl_wave_output.h"
#include "res/game_files.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::audio {

namespace {

// Ctrl+S as an input code (WM_CHAR 0x13): always interrupts speech (1088:01a9).
constexpr uint16_t kCodeCtrlS = 0x13;

template <class T>
std::unique_ptr<T> require(std::unique_ptr<T> p, const char* what) {
    if (!p) throw Error(std::string("AudioSystem: missing ") + what);
    return p;
}

}  // namespace

std::unique_ptr<AudioSystem> AudioSystem::create(GameFiles& files, const AudioConfig& config) {
    AudioParts parts;
    MidiBackendConfig mc;
    mc.kind = config.midiBackend;
    mc.soundFont = config.soundFontPath;
    mc.musicVolume = config.musicVolume;
    if (config.waveDevice) {
        auto sdl = std::make_unique<SdlWaveOutput>(config.sdlAudioDriver);
        if (sdl->open()) {
            mc.sdlDevice = sdl->deviceId();  // the music stream is mixed on the same device
            parts.wave = std::move(sdl);
        }
    }
    parts.sounds = std::make_unique<ResourceSoundLoader>(*files.res);
    // (parts.midi is destroyed before parts.wave, so the music stream leaves the device first.)
    parts.midi = std::make_unique<MidiPlayer>(createMidiBackend(mc), MicroClock(realClockUs), true);
    parts.tracks = std::make_unique<DataSourceTrackLoader>(*files.source);
    auto a = std::make_unique<AudioSystem>(AudioTables::fromExe(*files.exe), std::move(parts), config.musicSet);
    log::debug("audio: wave voice {}, MIDI back end '{}' ({})", a->voice().available() ? "available" : "unavailable",
               a->midiPlayer().backendName(), a->midi().midiOk() ? "open" : "failed");
    return a;
}

AudioSystem::AudioSystem(AudioTables tables, AudioParts parts, MusicSet set)
    : tables_(std::move(tables)),
      wave_(parts.wave ? std::move(parts.wave) : std::make_unique<NoWaveOutput>()),
      sounds_(require(std::move(parts.sounds), "sound loader")),
      midi_(parts.midi ? std::move(parts.midi)
                       : std::make_unique<MidiPlayer>(std::make_unique<NullMidiBackend>(), MicroClock(realClockUs), true)),
      tracks_(require(std::move(parts.tracks), "track loader")),
      voice_(*wave_),
      driver_(tables_, *midi_, *tracks_, set),
      music_(driver_, settings_, tables_.initialTuneId) {
    settings_.soundOn = tables_.soundOn;
    settings_.voiceOn = tables_.voiceOn;
}

AudioSystem::~AudioSystem() {
    voice_.stop();
    driver_.stop();
}

void AudioSystem::service() {
    voice_.isPlaying();  // MM_WOM_DONE -> Wave_Stop (1010:039c)
    midi_->service();    // manual-mode sequencer step (no-op when threaded)
    driver_.service();   // MM_MCINOTIFY -> Midi_OnPlayDone (1010:0529)
    midi_->service();    // manual mode: a loop restart sends its first events at once
}

// ----------------------------------------------------------------------------------- settings

// 1128:012f Audio_ToggleSound
void AudioSystem::toggleSound() {
    if (!settings_.soundOn) {
        settings_.soundOn = 1;
        music_.resume();
    } else {
        settings_.soundOn = 0;
        voice_.stop();
        driver_.stop();
    }
}

// 1128:0151 Audio_ToggleAll
void AudioSystem::toggleAll() {
    if (!settings_.soundOn && !settings_.voiceOn) {
        settings_.voiceOn = 1;
        settings_.soundOn = 1;
        music_.resume();
    } else {
        settings_.voiceOn = 0;
        settings_.soundOn = 0;
        sfxStop();
        driver_.stop();
    }
}

void AudioSystem::setSoundFromOptions(bool on) {
    if ((settings_.soundOn != 0) != on) toggleSound();
}

void AudioSystem::applySignInSettings(uint8_t recordSound, uint8_t recordVoice) {
    if (recordSound != settings_.soundOn && settings_.soundOn != 0) {
        toggleSound();
        settings_.soundOn = recordSound;
    }
    settings_.voiceOn = recordVoice;
}

void AudioSystem::onNewPlayer() {
    if (settings_.soundOn == 0) toggleSound();
}

// --------------------------------------------------------------------------------- wave voice

// 1170:0000 Sfx_Play
void AudioSystem::sfxPlay(int n) {
    if (sfxHeld_) {
        voice_.stop();
        sfxHeld_ = false;
    }
    if (settings_.soundOn) {
        auto s = sounds_->load(7000 + n);
        sfxHeld_ = s != nullptr;
        voice_.play(std::move(s));
    }
}

// 1170:005b Sfx_Stop
void AudioSystem::sfxStop() {
    voice_.stop();
    sfxHeld_ = false;
}

// 1088:01a9 Wave_PlayResUntilEvent
bool AudioSystem::playUntilEvent(int soundId, std::span<const uint16_t> stopCodes, InputPort& in) {
    voice_.play(sounds_->load(soundId));
    bool interrupted = false;
    for (;;) {
        const uint16_t c = in.peekCode();
        if (c != 0) {
            for (uint16_t k : stopCodes) {
                if (k == 0) break;
                if ((c & 0x0FFF) == k) {
                    interrupted = true;
                    break;
                }
            }
            if (c == kCodeCtrlS) interrupted = true;
            if (interrupted)
                voice_.stop();  // the code stays queued for the caller
            else
                in.getCode();   // any other input is discarded
        }
        if (!voice_.isPlaying() || interrupted) return interrupted;
        in.idle();
    }
}

bool AudioSystem::speak(int soundId, StopList list, InputPort& in) {
    if (!settings_.voiceOn) return false;
    return playUntilEvent(soundId, tables_.stopCodes(list), in);
}

bool AudioSystem::speakSequence(std::span<const int> soundIds, StopList list, InputPort& in) {
    for (int id : soundIds)
        if (speak(id, list, in)) return true;
    return false;
}

void AudioSystem::waitWhilePlaying(InputPort& in) {
    // Bounded: on the real device SdlWaveOutput::done() reports the sound finished at the latest
    // its length + SdlWaveOutput::kDoneMarginMs after the start, even if the device stopped
    // consuming it (REVIEW_1 L-3).
    while (voice_.isPlaying()) in.idle();
}

bool AudioSystem::jingleThenSpeak(int sfx, std::span<const int> soundIds, StopList list, InputPort& in) {
    sfxPlay(sfx);
    waitWhilePlaying(in);
    return speakSequence(soundIds, list, in);
}

void AudioSystem::startSpeech(int soundId) { voice_.play(sounds_->load(soundId)); }

// ------------------------------------------------------------------------------ pause / resume

// 1090:0536 Sound_PauseAll
void AudioSystem::pauseAll() {
    voice_.stop();
    driver_.pause();
}

// 1090:0540 Sound_ResumeAll
void AudioSystem::resumeAll() { driver_.resume(); }

// 10d0:0000 Music_PauseNested
void AudioSystem::pauseNested() {
    pauseAll();
    pauseDepth_ = static_cast<int8_t>(pauseDepth_ + 1);
}

// 10d0:000e Music_ResumeNested
void AudioSystem::resumeNested() {
    pauseDepth_ = static_cast<int8_t>(pauseDepth_ - 1);
    if (pauseDepth_ < 1) {
        pauseDepth_ = 0;
        resumeAll();
    }
}

// 10d0:0025 Music_ResumeForce
void AudioSystem::resumeForce() {
    pauseDepth_ = 0;
    resumeAll();
}

// 1010:05c8 App_Deactivate
void AudioSystem::appDeactivate() {
    if (!driver_.appActive()) return;
    driver_.setAppActive(false);
    pauseAll();
}

// 1010:05db App_Activate (the caller checks that the window is not minimised)
void AudioSystem::appActivate() {
    if (driver_.appActive()) return;
    driver_.setAppActive(true);
    resumeAll();
}

}  // namespace tmport::audio
