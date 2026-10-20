// The sound facade over GgAudio (sound.h GgAudioSound; re/gg/docs/code_sound.md §12-§15). Owner: flow agent.
//
// Every call goes to GgAudio, which runs it with the renderer locked (between two sound ticks, as the
// original's API calls run with the driver locked): the bank layer's calls through GgAudio's own
// methods, the music cue set-ups (MusicCues) and the puzzles' helpers (PzSnd) through GgAudio::call.
// Handles are the bank layer's own (SndRes), so the numbers the game keeps in GgState::sound are the
// original's. With --verbose every call is logged with the sound tick it runs before.
#include "gg/game/sound.h"

#include <format>

#include "gg/audio/gg_audio.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// "sound: <tick> playEffect(12,0,4)" in the debug log (the sound tick the call runs before).
template <class... A>
void note(GgAudio& a, std::string_view op, A... args) {
    if (!log::verbose()) return;
    std::string s;
    ((s += std::format("{}{}", s.empty() ? "" : ",", args)), ...);
    log::debug("sound: {} {}({})", a.tickIndex(), op, s);
}

}  // namespace

// 1cdd:0024 SndRes_LoadMusicBank (id + DS:a0d8)
int GgAudioSound::loadMusicBank(uint16_t id) {
    const int h = audio_.loadMusicBank(id);
    note(audio_, "loadMusicBank", id, h);
    return h;
}

// 1cdd:012d SndRes_LoadEffectBank
int GgAudioSound::loadEffectBank(uint16_t id) {
    const int h = audio_.loadEffectBank(id);
    note(audio_, "loadEffectBank", id, h);
    return h;
}

// 1cdd:01e0 SndRes_LoadSong (the hint chime 0x1056)
int GgAudioSound::loadSong(uint16_t id) {
    const int h = audio_.loadSong(id);
    note(audio_, "loadSong", id, h);
    return h;
}

// 1cdd:0237 SndRes_FreeMusicBank
void GgAudioSound::freeMusicBank(int h) {
    note(audio_, "freeMusicBank", h);
    audio_.freeMusicBank(h);
}

// 1cdd:027d SndRes_FreeEffectBank
void GgAudioSound::freeEffectBank(int h) {
    note(audio_, "freeEffectBank", h);
    audio_.freeEffectBank(h);
}

// 1cdd:02e3 SndRes_FreeSong
void GgAudioSound::freeSong(int h) {
    note(audio_, "freeSong", h);
    audio_.freeSong(h);
}

// 1cdd:031a SndRes_PlayMusic (not restarted while it plays)
void GgAudioSound::playMusic(int h, int section, int loop) {
    note(audio_, "playMusic", h, section, loop);
    audio_.playMusic(h, section, loop);
}

// 1cdd:03c9 SndRes_PlayEffect (priority + 3Fh)
void GgAudioSound::playEffect(int h, int loop, int priority) {
    note(audio_, "playEffect", h, loop, priority);
    audio_.playEffect(h, loop, priority);
}

// 1cdd:03ef SndRes_StopMusic
void GgAudioSound::stopMusic(int h) {
    note(audio_, "stopMusic", h);
    audio_.stopMusic(h);
}

// 1cdd:0409 SndRes_StopEffect
void GgAudioSound::stopEffect(int h) {
    note(audio_, "stopEffect", h);
    audio_.stopEffect(h);
}

// 1cdd:0521 SndRes_StopAllEffects (of the effect bank)
void GgAudioSound::stopAllEffects() {
    note(audio_, "stopAllEffects");
    audio_.stopAllEffects();
}

// 1cdd:056d SndRes_IsPlaying (1 playing)
int GgAudioSound::isPlaying(int h) { return audio_.isPlaying(h); }

// 1cdd:0423 SndRes_SelectArrangement
void GgAudioSound::selectArrangement(int h, int n) {
    note(audio_, "selectArrangement", h, n);
    audio_.selectArrangement(h, n);
}

// 1cdd:04ef SndRes_MuteChannel
void GgAudioSound::muteChannel(int h, int channel) {
    note(audio_, "muteChannel", h, channel);
    audio_.muteChannel(h, channel);
}

// 1cdd:0508 SndRes_UnmuteChannel
void GgAudioSound::unmuteChannel(int h, int channel) {
    note(audio_, "unmuteChannel", h, channel);
    audio_.unmuteChannel(h, channel);
}

// g_effectBank DS:7a44 as the bank layer sees it (SndRes_StopAllEffects, the screen animations)
void GgAudioSound::setEffectBank(int h) { audio_.setEffectBank(h); }

// DS:9dc0 g_sndGlobalEffects (bank 0x105B)
int GgAudioSound::globalEffects() {
    return audio_.call([](SndRes& r) { return r.globalEffects(); });
}

// Snd_GetTick(SndRes_GetSlot(h)) 1bb4:0c0c (races)
int GgAudioSound::musicTick(int h) { return audio_.musicTick(h); }

// 1cdd:05c8 Snd_ToggleOption (Ctrl+S): DS:1ad4 ^= 1; the caller flips g_soundOff
void GgAudioSound::toggleSound() {
    note(audio_, "toggleSound");
    audio_.toggleSound();
}

// DS:1ad4 = muted, 1cdd:0592 SndRes_ApplySoundOption
void GgAudioSound::setSoundMuted(int muted) {
    note(audio_, "setSoundMuted", muted);
    audio_.setSoundMuted(muted);
}

// DS:1ad4 g_soundMuted
int GgAudioSound::soundMuted() { return audio_.soundMuted(); }

// 1cdd:05db Snd_Pause (not nestable)
void GgAudioSound::pause() {
    note(audio_, "pause");
    audio_.pause();
}

// 1cdd:05e5 Snd_Resume
void GgAudioSound::resume() {
    note(audio_, "resume");
    audio_.resume();
}

// 1bb4:0e15 Snd_Off (Game_Shutdown)
void GgAudioSound::shutdown() {
    note(audio_, "shutdown");
    audio_.shutdown();
}

// DS:a0d8 g_fmPresent
int GgAudioSound::fmPresent() { return audio_.fmPresent(); }

// DS:09e6 and g_building for Music_RoomCue
void GgAudioSound::setRoomMusic(int bank, int building) {
    note(audio_, "setRoomMusic", bank, building);
    audio_.call([&](SndRes&) {
        audio_.cues().setRoomMusicBank(bank);
        audio_.cues().setBuilding(building);
    });
}

// 1622:03b6 RoomMusic_SelectLayers
void GgAudioSound::roomMusicSelectLayers(int n, bool roomHasChimps, int level) {
    note(audio_, "roomMusicSelectLayers", n, roomHasChimps ? 1 : 0, level);
    audio_.call([&](SndRes&) { audio_.cues().roomMusicSelectLayers(n, roomHasChimps, level); });
}

// 1622:0346 RoomMusic_Start
void GgAudioSound::roomMusicStart() {
    note(audio_, "roomMusicStart");
    audio_.call([&](SndRes&) { audio_.cues().roomMusicStart(); });
}

// DS:08be g_roomMusicStep
int GgAudioSound::roomMusicStep() {
    return audio_.call([&](SndRes&) { return audio_.cues().roomMusicStep(); });
}

// 2dee:1a90.. (arrangement 1, the cue)
void GgAudioSound::workshopMusicStart(int musicBank) {
    note(audio_, "workshopMusicStart", musicBank);
    audio_.call([&](SndRes&) { audio_.cues().workshopMusicStart(musicBank); });
}

// 2dee:17eb / 185d (arrangement 3 / back)
void GgAudioSound::workshopBlueprint(bool open) {
    note(audio_, "workshopBlueprint", open ? 1 : 0);
    audio_.call([&](SndRes&) { audio_.cues().workshopBlueprint(open); });
}

// 2fcb:0640 (step 1, the cue, arrangement 1)
void GgAudioSound::movieMusicStart(int musicBank) {
    note(audio_, "movieMusicStart", musicBank);
    audio_.call([&](SndRes&) { audio_.cues().movieMusicStart(musicBank); });
}

// 3924:0000 PzSnd_LoadBank
void GgAudioSound::pzLoadBank(uint16_t id) {
    note(audio_, "pzLoadBank", id);
    audio_.call([&](SndRes&) { audio_.puzzles().loadBank(id); });
}

// 3924:0074 PzSnd_FreeBank
void GgAudioSound::pzFreeBank() {
    note(audio_, "pzFreeBank");
    audio_.call([&](SndRes&) { audio_.puzzles().freeBank(); });
}

// 3924:00ae PzSnd_Play
void GgAudioSound::pzPlay(int i, int priority) {
    note(audio_, "pzPlay", i, priority);
    audio_.call([&](SndRes&) { audio_.puzzles().play(i, priority); });
}

// 3924:00cb PzSnd_PlayLoop
void GgAudioSound::pzPlayLoop(int i, int priority) {
    note(audio_, "pzPlayLoop", i, priority);
    audio_.call([&](SndRes&) { audio_.puzzles().playLoop(i, priority); });
}

// 3924:00e8 PzSnd_Stop
void GgAudioSound::pzStop(int i) {
    note(audio_, "pzStop", i);
    audio_.call([&](SndRes&) { audio_.puzzles().stop(i); });
}

// 3924:00fc PzSnd_StopAll
void GgAudioSound::pzStopAll() {
    note(audio_, "pzStopAll");
    audio_.call([&](SndRes&) { audio_.puzzles().stopAll(); });
}

// 3924:0124 PzSnd_SetTranspose
void GgAudioSound::pzSetTranspose(int i, int t) {
    note(audio_, "pzSetTranspose", i, t);
    audio_.call([&](SndRes&) { audio_.puzzles().setTranspose(i, t); });
}

// 3924:005b PzSnd_WaitEnd: its test, Snd_IsActive of the effect's slot (the caller busy-waits while it
// is 1, pumping with GgRuntime::idle()).
int GgAudioSound::pzIsActive(int i) {
    return audio_.call([&](SndRes&) { return audio_.puzzles().isActive(i); });
}

// DS:338c g_pzSndCount
int GgAudioSound::pzCount() {
    return audio_.call([&](SndRes&) { return audio_.puzzles().count(); });
}

}  // namespace tmport::gg
