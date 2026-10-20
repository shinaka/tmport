#pragma once
// The game's view of the sound (re/gg/docs/code_sound.md §12-§15): every call the game logic makes to
// the bank layer (SndRes 1cdd), the driver (1bb4: the music tick, pause), the music cue set-ups (12e0's
// callbacks: MusicCues) and the puzzles' helpers (PzSnd 3924), behind an interface so that the real
// sound (GgAudioSound over GgAudio, gg/audio/gg_audio.h) and a recording double for tests
// (RecordingSound) are interchangeable. Game code holds the handles in GgState::sound (the original's
// DGROUP words) and passes them, as the original passes them to SndRes_*.
//
// Handles are the bank layer's: an effect bank's handle h names effect k as h + k (code_sound.md §13.1).
// Priorities are the callers' arguments (the bank layer adds 3Fh for effects; PzSnd does not).
//
// Owners: the interface and RecordingSound are the skeleton's; GgAudioSound (sound_audio.cpp) is the
// flow agent's (the sound facade implementation over GgAudio).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tmport::gg {

class GgAudio;

class SoundFacade {
public:
    virtual ~SoundFacade() = default;

    // ---- banks (1cdd)
    virtual int loadMusicBank(uint16_t id) = 0;   // 1cdd:0024 SndRes_LoadMusicBank (id + DS:a0d8)
    virtual int loadEffectBank(uint16_t id) = 0;  // 1cdd:012d SndRes_LoadEffectBank
    virtual int loadSong(uint16_t id) = 0;        // 1cdd:01e0 SndRes_LoadSong (the hint chime 0x1056)
    virtual void freeMusicBank(int h) = 0;        // 1cdd:0237 SndRes_FreeMusicBank
    virtual void freeEffectBank(int h) = 0;       // 1cdd:027d SndRes_FreeEffectBank
    virtual void freeSong(int h) = 0;             // 1cdd:02e3 SndRes_FreeSong
    // ---- playing (1cdd)
    virtual void playMusic(int h, int section, int loop) = 0;    // 1cdd:031a SndRes_PlayMusic (not restarted while it plays)
    virtual void playEffect(int h, int loop, int priority) = 0;  // 1cdd:03c9 SndRes_PlayEffect (priority + 3Fh)
    virtual void stopMusic(int h) = 0;                           // 1cdd:03ef SndRes_StopMusic
    virtual void stopEffect(int h) = 0;                          // 1cdd:0409 SndRes_StopEffect
    virtual void stopAllEffects() = 0;                           // 1cdd:0521 SndRes_StopAllEffects (of the effect bank)
    virtual int isPlaying(int h) = 0;                            // 1cdd:056d SndRes_IsPlaying (1 playing)
    virtual void selectArrangement(int h, int n) = 0;            // 1cdd:0423 SndRes_SelectArrangement
    virtual void muteChannel(int h, int channel) = 0;            // 1cdd:04ef SndRes_MuteChannel
    virtual void unmuteChannel(int h, int channel) = 0;          // 1cdd:0508 SndRes_UnmuteChannel
    // g_effectBank DS:7a44 as the bank layer sees it (SndRes_StopAllEffects, the screen animations'
    // effect ops): set it whenever GgState::sound.effectBank changes.
    virtual void setEffectBank(int h) = 0;
    virtual int globalEffects() = 0;                             // DS:9dc0 g_sndGlobalEffects (bank 0x105B)
    virtual int musicTick(int h) = 0;                            // Snd_GetTick(SndRes_GetSlot(h)) 1bb4:0c0c (races)
    // ---- switches
    virtual void toggleSound() = 0;           // 1cdd:05c8 Snd_ToggleOption (Ctrl+S): DS:1ad4 ^= 1; the caller flips g_soundOff
    virtual void setSoundMuted(int muted) = 0;// DS:1ad4 = muted, 1cdd:0592 SndRes_ApplySoundOption
    virtual int soundMuted() = 0;             // DS:1ad4 g_soundMuted
    virtual void pause() = 0;                 // 1cdd:05db Snd_Pause (not nestable)
    virtual void resume() = 0;                // 1cdd:05e5 Snd_Resume
    virtual void shutdown() = 0;              // 1bb4:0e15 Snd_Off (Game_Shutdown)
    virtual int fmPresent() = 0;              // DS:a0d8 g_fmPresent
    // ---- the music cue set-ups (12e0 callbacks, MusicCues)
    virtual void setRoomMusic(int bank, int building) = 0;           // DS:09e6 and g_building for Music_RoomCue
    virtual void roomMusicSelectLayers(int n, bool roomHasChimps, int level) = 0;  // 1622:03b6 RoomMusic_SelectLayers
    virtual void roomMusicStart() = 0;                               // 1622:0346 RoomMusic_Start
    virtual int roomMusicStep() = 0;                                 // DS:08be g_roomMusicStep
    virtual void workshopMusicStart(int musicBank) = 0;              // 2dee:1a90.. (arrangement 1, the cue)
    virtual void workshopBlueprint(bool open) = 0;                   // 2dee:17eb / 185d (arrangement 3 / back)
    virtual void movieMusicStart(int musicBank) = 0;                 // 2fcb:0640 (step 1, the cue, arrangement 1)
    // ---- the puzzles' bank (3924 PzSnd)
    virtual void pzLoadBank(uint16_t id) = 0;          // 3924:0000 PzSnd_LoadBank
    virtual void pzFreeBank() = 0;                     // 3924:0074 PzSnd_FreeBank
    virtual void pzPlay(int i, int priority) = 0;      // 3924:00ae PzSnd_Play
    virtual void pzPlayLoop(int i, int priority) = 0;  // 3924:00cb PzSnd_PlayLoop
    virtual void pzStop(int i) = 0;                    // 3924:00e8 PzSnd_Stop
    virtual void pzStopAll() = 0;                      // 3924:00fc PzSnd_StopAll
    virtual void pzSetTranspose(int i, int t) = 0;     // 3924:0124 PzSnd_SetTranspose
    // Snd_IsActive of the effect's slot (PzSnd_WaitEnd 3924:005b busy-waits while it is 1; the caller
    // pumps with GgRuntime::idle()).
    virtual int pzIsActive(int i) = 0;
    virtual int pzCount() = 0;                         // DS:338c g_pzSndCount
};

// A facade that records the calls (tests, the per-frame traces) and answers as a silent sound would:
// handles are numbered as the bank layer numbers them (a bank takes the next free handles: one for a
// music bank or a song, kEffectSpan for an effect bank), nothing ever plays (isPlaying and pzIsActive
// 0: busy-waits on sound end at once), and the music tick runs ahead (musicTick: waits on it end at
// once) unless a tick source is set.
class RecordingSound final : public SoundFacade {
public:
    static constexpr int kEffectSpan = 64;  // handles an effect bank reserves (its effects are h + k)

    struct Call {
        std::string op;      // the method's name, e.g. "playEffect"
        std::vector<int> args;
        bool operator==(const Call&) const = default;
    };

    int loadMusicBank(uint16_t id) override;
    int loadEffectBank(uint16_t id) override;
    int loadSong(uint16_t id) override;
    void freeMusicBank(int h) override;
    void freeEffectBank(int h) override;
    void freeSong(int h) override;
    void playMusic(int h, int section, int loop) override;
    void playEffect(int h, int loop, int priority) override;
    void stopMusic(int h) override;
    void stopEffect(int h) override;
    void stopAllEffects() override;
    int isPlaying(int h) override;
    void selectArrangement(int h, int n) override;
    void muteChannel(int h, int channel) override;
    void unmuteChannel(int h, int channel) override;
    void setEffectBank(int h) override;
    int globalEffects() override { return globalEffects_; }
    int musicTick(int h) override;
    void toggleSound() override;
    void setSoundMuted(int muted) override;
    int soundMuted() override { return muted_; }
    void pause() override;
    void resume() override;
    void shutdown() override;
    int fmPresent() override { return fmPresent_; }
    void setRoomMusic(int bank, int building) override;
    void roomMusicSelectLayers(int n, bool roomHasChimps, int level) override;
    void roomMusicStart() override;
    int roomMusicStep() override { return 0; }
    void workshopMusicStart(int musicBank) override;
    void workshopBlueprint(bool open) override;
    void movieMusicStart(int musicBank) override;
    void pzLoadBank(uint16_t id) override;
    void pzFreeBank() override;
    void pzPlay(int i, int priority) override;
    void pzPlayLoop(int i, int priority) override;
    void pzStop(int i) override;
    void pzStopAll() override;
    void pzSetTranspose(int i, int t) override;
    int pzIsActive(int i) override;
    int pzCount() override { return pzCount_; }

    const std::vector<Call>& calls() const { return calls_; }
    // The calls since the last take (the per-frame trace).
    std::vector<Call> takeCalls();
    void clear() { calls_.clear(); taken_ = 0; }
    // "playEffect(12,0,4); stopAllEffects()" for test messages and traces.
    static std::string describe(const std::vector<Call>& calls);

    // Answers of the double: the music tick of a handle (default: 0x7FFF, so a wait on it ends), and
    // whether a handle or a puzzle effect plays (default: never).
    void setMusicTickSource(std::function<int(int h)> f) { musicTick_ = std::move(f); }
    void setPlayingSource(std::function<int(int h)> f) { playing_ = std::move(f); }
    void setPzActiveSource(std::function<int(int i)> f) { pzActive_ = std::move(f); }
    void setFmPresent(int fm) { fmPresent_ = fm; }
    void setPzCount(int n) { pzCount_ = n; }
    int effectBank() const { return effectBank_; }

private:
    void record(std::string op, std::vector<int> args);

    std::vector<Call> calls_;
    size_t taken_ = 0;
    int nextHandle_ = 1;  // handle 0 is the global effect bank of SndRes_Init
    int globalEffects_ = 0;
    int effectBank_ = -1;
    int muted_ = 0;
    int fmPresent_ = 1;
    int pzCount_ = 0;
    std::function<int(int)> musicTick_;
    std::function<int(int)> playing_;
    std::function<int(int)> pzActive_;
};

// The real sound: GgAudio's bank layer, driver, music cues and PzSnd (gg/audio/gg_audio.h), each call
// under its render lock. Owner: flow agent.
class GgAudioSound final : public SoundFacade {
public:
    explicit GgAudioSound(GgAudio& audio) : audio_(audio) {}

    int loadMusicBank(uint16_t id) override;
    int loadEffectBank(uint16_t id) override;
    int loadSong(uint16_t id) override;
    void freeMusicBank(int h) override;
    void freeEffectBank(int h) override;
    void freeSong(int h) override;
    void playMusic(int h, int section, int loop) override;
    void playEffect(int h, int loop, int priority) override;
    void stopMusic(int h) override;
    void stopEffect(int h) override;
    void stopAllEffects() override;
    int isPlaying(int h) override;
    void selectArrangement(int h, int n) override;
    void muteChannel(int h, int channel) override;
    void unmuteChannel(int h, int channel) override;
    void setEffectBank(int h) override;
    int globalEffects() override;
    int musicTick(int h) override;
    void toggleSound() override;
    void setSoundMuted(int muted) override;
    int soundMuted() override;
    void pause() override;
    void resume() override;
    void shutdown() override;
    int fmPresent() override;
    void setRoomMusic(int bank, int building) override;
    void roomMusicSelectLayers(int n, bool roomHasChimps, int level) override;
    void roomMusicStart() override;
    int roomMusicStep() override;
    void workshopMusicStart(int musicBank) override;
    void workshopBlueprint(bool open) override;
    void movieMusicStart(int musicBank) override;
    void pzLoadBank(uint16_t id) override;
    void pzFreeBank() override;
    void pzPlay(int i, int priority) override;
    void pzPlayLoop(int i, int priority) override;
    void pzStop(int i) override;
    void pzStopAll() override;
    void pzSetTranspose(int i, int t) override;
    int pzIsActive(int i) override;
    int pzCount() override;

    GgAudio& audio() { return audio_; }

private:
    GgAudio& audio_;
};

}  // namespace tmport::gg
