#pragma once
// The sound bank layer of ssg.exe, resident segment 1cdd (re/gg/docs/code_sound.md §13): sound
// handles over the driver's slots, music banks (channel lists, an optional cue-tick record, one
// song), effect banks (songs only) and single songs, read from the archive with the speaker / FM
// pairing (every loader reads id + g_fmPresent: id holds the speaker version, id + 1 the FM one),
// music arrangements (SndRes_SelectArrangement), the sound switch and pause.
//
// Handles are the original's (game code adds effect numbers to them): g_sndTable DS:9d48[60], −1
// = free; music banks take the first free entry from 0 (the handle is that entry), effect banks
// the first free entry e from 5 and return e + 1 (table[e] = the member count n, table[e + 1 + k] =
// the slot of effect k, or an error code for a `C FF` hole), single songs the first free entry
// from 5. Every loaded bank is a private copy (Res_LoadCopy), kept here as the original's
// g_sndBankMem DS:9c58; the driver works inside it.
//
// Hazards kept as the original has them (§13.4): the arrangement mute loop runs on past the end of
// a list until it meets an FFh byte (the final movie's speaker list 4), and the music bank loader
// reads the cue tick at the wrong offset (the workshop cue never fires, fmt_sound.md §2.1).
// Deviations: a table entry past 60 (a full table, an effect bank that does not fit) throws instead
// of writing over the next variables; reads past a bank copy's end stop the mute loop.
//
// Not thread safe: GgAudio calls it under its render lock.

#include <array>
#include <cstdint>
#include <vector>

#include "gg/audio/snd_driver.h"

namespace tmport::gg {

class Archive;

class SndRes {
public:
    static constexpr int kTableSize = 60;
    static constexpr int kFirstEffectHandle = 5;  // effect banks and single songs search from here
    static constexpr int kEffectPriority = 0x3F;  // SndRes_PlayEffect adds it to the caller's priority
    static constexpr uint16_t kGlobalEffectBank = 0x105B;  // loaded by SndRes_Init (g_sndGlobalEffects)

    SndRes(SndDriver& driver, const Archive& archive);

    // g_fmPresent DS:a0d8: 1 when Snd_Init reported an OPL (caps bit 2); added to every bank id.
    void setFmPresent(int fm) { fmPresent_ = fm; }
    int fmPresent() const { return fmPresent_; }

    // SndRes_Init 1cdd:0004: all handles free, then the global effect bank 0x105B (+ g_fmPresent).
    void init();

    // ---- loading (§13.1); each returns the handle. Throws std::out_of_range for a missing
    // resource and tmport::FormatError for a broken bank.
    int loadMusicBank(uint16_t id);   // 1cdd:0024
    int loadEffectBank(uint16_t id);  // 1cdd:012d (returns entry + 1)
    int loadSong(uint16_t id);        // 1cdd:01e0
    void freeMusicBank(int h);        // 1cdd:0237
    void freeEffectBank(int h);       // 1cdd:027d
    void freeSong(int h);             // 1cdd:02e3

    // ---- playing (§13.2)
    // 1cdd:031a: priority from a member whose payload starts with 3 (none: 0); an already playing
    // music is not restarted.
    void playMusic(int h, int section, int loop);
    // 1cdd:03c9: section 0, priority p + 3Fh; restarts the effect if it plays. h = bank handle + k.
    void playEffect(int h, int loop, int p);
    void stopMusic(int h);   // 1cdd:03ef
    void stopEffect(int h);  // 1cdd:0409
    // 1cdd:0521: every effect of the screen's effect bank g_effectBank (DS:7a44, setEffectBank).
    void stopAllEffects();
    int isPlaying(int h) const;  // 1cdd:056d: 0 for a handle whose entry is negative
    int slot(int h) const;       // SndRes_GetSlot 1cdd:055f (and its twin 1cdd:0551)
    // 1cdd:0423: unmute channels 0..15, then mute the channels of the n-th `01 c c .. FF` list.
    void selectArrangement(int h, int n);
    void muteChannel(int h, int channel);    // 1cdd:04ef
    void unmuteChannel(int h, int channel);  // 1cdd:0508

    // ---- the sound switch, pause (§12)
    // SndRes_ApplySoundOption 1cdd:0592: g_soundMuted != 0 plays the global bank's effect 0 (the
    // silence song) at priority 7Fh, looping; 0 stops it.
    void applySoundOption();
    // Snd_ToggleOption 1cdd:05c8 (Ctrl+S): flips g_soundMuted and applies it. The original also
    // flips the player's option g_soundOff DS:45cc, which belongs to the game state.
    void toggleOption();
    void setSoundMuted(int muted) { soundMuted_ = muted; }  // DS:1ad4 (the sign-in and the Options dialog)
    int soundMuted() const { return soundMuted_; }
    void pause() { driver_.pauseLock(); }      // Snd_Pause 1cdd:05db
    void resume() { driver_.resumeUnlock(); }  // Snd_Resume 1cdd:05e5

    // ---- game variables the layer reads or writes
    void setEffectBank(int h) { effectBank_ = h; }  // g_effectBank DS:7a44
    int effectBank() const { return effectBank_; }
    int globalEffects() const { return globalEffects_; }  // g_sndGlobalEffects DS:9dc0
    uint16_t bankCueTick() const { return bankCueTick_; } // g_sndBankCueTick DS:9dc2
    int lastEffectSlot() const { return lastEffectSlot_; }  // DS:1ad6 (no reader in the game)
    int lastMusicSlot() const { return lastMusicSlot_; }    // DS:1ad8

    // Table entry (g_sndTable[i]) and the bank copy of an entry (for tools and tests).
    int16_t entry(int i) const { return table_.at(static_cast<size_t>(i)); }
    const std::vector<uint8_t>& bankMemory(int i) const { return mem_.at(static_cast<size_t>(i)); }

    SndDriver& driver() { return driver_; }

private:
    int16_t& at(int i);
    int firstFree(int from) const;
    std::vector<uint8_t> loadCopy(uint16_t id) const;  // Res_LoadCopy(id + g_fmPresent)
    // Offset of member i's payload in a bank copy (bank + off[i] + 2), as the walks compute it.
    static size_t memberPayload(const std::vector<uint8_t>& bank, int i);

    SndDriver& driver_;
    const Archive& archive_;
    int fmPresent_ = 0;
    std::array<int16_t, kTableSize> table_{};
    std::array<std::vector<uint8_t>, kTableSize> mem_{};
    int globalEffects_ = -1;
    uint16_t bankCueTick_ = 0;
    int effectBank_ = -1;
    int soundMuted_ = 0;
    int lastEffectSlot_ = -1;
    int lastMusicSlot_ = -1;
};

}  // namespace tmport::gg
