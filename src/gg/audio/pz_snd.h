#pragma once
// The puzzles' own sound helpers, overlay 19 segment 3924 (re/gg/docs/code_puzzles_a.md §2.4): one
// effect bank at a time (g_pzSndBank DS:b1b0, id + g_fmPresent), every member registered with the
// driver into g_pzSndSlots DS:b146, played with Snd_Play directly (section 0, the raw priority; no
// + 3Fh as in the bank layer). A speaker `C FF` member leaves an error code and stays silent.
//
// PzSnd_WaitEnd 3924:005b busy-waits while the slot is active, with no input polling; the game
// loop does that wait itself with isActive() (it must pump the host meanwhile).
// Not thread safe: GgAudio calls it under its render lock.

#include <array>
#include <cstdint>
#include <vector>

namespace tmport::gg {

class Archive;
class SndDriver;

class PzSnd {
public:
    static constexpr int kMaxEffects = 53;  // g_pzSndSlots: the largest bank (0x11d2) has 53

    PzSnd(SndDriver& driver, const Archive& archive) : driver_(driver), archive_(archive) {}

    void setFmPresent(int fm) { fmPresent_ = fm; }
    void loadBank(uint16_t id);       // PzSnd_LoadBank 3924:0000
    void freeBank();                  // PzSnd_FreeBank 3924:0074
    void play(int i, int priority);   // PzSnd_Play 3924:00ae (loop 0)
    void playLoop(int i, int priority);  // PzSnd_PlayLoop 3924:00cb
    void stop(int i);                 // PzSnd_Stop 3924:00e8
    void stopAll();                   // PzSnd_StopAll 3924:00fc
    void setTranspose(int i, int t);  // PzSnd_SetTranspose 3924:0124
    // Snd_IsActive of the effect's slot: PzSnd_WaitEnd waits while this is 1 (an invalid slot gives
    // -2 and ends the wait at once).
    int isActive(int i) const;
    int count() const { return count_; }  // g_pzSndCount DS:338c
    int slot(int i) const { return slots_.at(static_cast<size_t>(i)); }

private:
    SndDriver& driver_;
    const Archive& archive_;
    int fmPresent_ = 0;
    std::vector<uint8_t> bank_;
    std::array<int, kMaxEffects> slots_{};
    int count_ = 0;
};

}  // namespace tmport::gg
