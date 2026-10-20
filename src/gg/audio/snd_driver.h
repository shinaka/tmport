#pragma once
// The BMF sound driver of ssg.exe, resident segment 1bb4 (re/gg/docs/code_sound.md §2-§12): 64
// song slots, the sound tick (Snd_Tick 1bb4:0db3, every 2nd PIT tick = 72.826 Hz), sections,
// events, delays, meta events and cue callbacks, channel ownership by priority, the PC speaker
// interpreter (class 1: PIT channel 2 divisors, port 61h) and the OPL2 interpreter in rhythm mode
// (class 3: 11-byte patches, Opl_SetPatch / SetVolume / NoteOn / NoteOff, pitch bend), pause and
// silence. A port of the hand-written assembly, routine by routine; the original addresses are in
// the comments of snd_driver.cpp.
//
// Device independent: the driver writes the hardware through SndPorts (the OPL register writes,
// the speaker divisor, port 61h) and is a dos::SoundTickSource, so the core's ChipRenderer ticks
// it inside the audio callback by sample count (gg_audio.h), or a virtual PIT ticks it.
//
// Song memory: as in the original, each registered song is a block inside a loaded bank copy that
// the bank layer owns (snd_res.h); the driver keeps its run-time state in that block (fmt_sound.md
// §3.1) and reaches it by offsets from s = BMF + 0x10, also the out-of-range offsets of the
// original's data hazards (a mute of "channel -3" writes s+02h, code_sound.md §13.4). Accesses
// outside the bank copy are dropped (reads give 0); no shipped data makes one.
//
// Dead paths not ported: the PSG (class 2) and MPU-401 / MT-32 (class 4) devices, which nothing
// enables (Snd_AllocSlot refuses their songs), the tempo scaler 1bb4:0f70/0fb2 and the volume
// setters 1bb4:11a2/11df (no callers). Class 0 (the PIT divisor list 1bb4:087e) is ported.
//
// Not thread safe: the owner (GgAudio) calls it under its render lock.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "dos/audio/chip_renderer.h"

namespace tmport::gg {

class GgExe;

// The driver's tables in DGROUP (code_sound.md §2), read from the user's ssg.exe: a copy of the
// data segment (DS:0000..FFFF, zero past the initialised data) read at the driver's offsets, so an
// index the original does not check reads what it would read there.
class SndTables {
public:
    // Throws tmport::FormatError when the tables are not where v1.0 has them.
    static SndTables fromExe(const GgExe& exe);
    // For tests: a DGROUP image (at least up to DS:1AD4).
    static SndTables fromDgroup(std::vector<uint8_t> dgroup);

    uint8_t ds8(uint16_t off) const { return ds_[off]; }
    uint16_t ds16(uint16_t off) const { return static_cast<uint16_t>(ds_[off] | (ds_[static_cast<uint16_t>(off + 1)] << 8)); }

    uint8_t classChannels(uint8_t cls) const { return ds8(static_cast<uint16_t>(0x1658 + cls)); }  // DS:1658
    uint8_t noteOctave(uint8_t n) const { return ds8(static_cast<uint16_t>(0x1712 + n)); }         // DS:1712 (XLAT)
    uint8_t noteInOctave(uint8_t n) const { return ds8(static_cast<uint16_t>(0x1792 + n)); }       // DS:1792
    // OPL F-number of semitone i in row r (DS:1852 + 24 r + 2 i; the row is not range-checked).
    uint16_t fnumber(uint8_t row, uint8_t i) const { return ds16(static_cast<uint16_t>(0x1852 + 24 * row + 2 * i)); }

private:
    explicit SndTables(std::vector<uint8_t> ds) : ds_(std::move(ds)) {}
    std::vector<uint8_t> ds_;  // 64 KB
};

// The ports the driver writes (a renderer's chips, a log, or both).
class SndPorts {
public:
    virtual ~SndPorts() = default;
    // AdLib_Write 1bb4:00a9: OUT 388h reg, 6 status reads, OUT 389h value, 35 status reads.
    virtual void oplWrite(uint8_t reg, uint8_t value) = 0;
    // OUT 42h low byte, then high byte (PIT channel 2, mode 3 set by Input_InstallHandlers).
    virtual void speakerDivisor(uint16_t divisor) = 0;
    // OUT 61h: bit 0 = timer-2 gate, bit 1 = speaker data; the speaker sounds when both are set.
    virtual void speakerPort61(uint8_t value) = 0;
};

class SndDriver final : public dos::SoundTickSource {
public:
    static constexpr int kSlots = 64;
    // Results (AX of the API functions).
    static constexpr int kOk = 0;
    static constexpr int kNoFreeSlot = -1;        // Snd_AllocSlot
    static constexpr int kBadSlot = -2;           // Snd_GetSlotSong 1bb4:000e
    static constexpr int kBadSection = -3;        // Snd_SectionEntry 1bb4:002b
    static constexpr int kClassUnavailable = -4;  // Snd_AllocSlot
    static constexpr int kNotBmf = -6;            // Snd_RegisterSong
    // Snd_Init devices (g_soundDevice DS:2452, code_runtime.md §2.2).
    static constexpr int kDeviceDetect = -1;  // no -S switch
    static constexpr int kDeviceOff = 0;      // -SN, -S, -S-
    static constexpr int kDeviceSpeaker = 1;  // -SI
    static constexpr int kDeviceFm = 4;       // -SA, -SB, -SM
    // Owner words (g_sndChannelOwner DS:15d8).
    static constexpr uint16_t kOwnerFree = 0x00FF;
    static constexpr uint16_t kOwnerReleased = 0x00FE;

    using CueId = uint32_t;  // a registered cue callback (the far pointer stored at s+09h); 0 = none

    SndDriver(std::shared_ptr<const SndTables> tables, SndPorts& ports);

    // ---- initialisation (§3)
    // Snd_Init 1bb4:1234: clears the slots; AdLib_Detect for device -1 (the chip answers only when
    // `adlibPresent`), none for 1 and 8, detection then OPL forced on for any other device. Returns
    // the caps: 1 | hasPsg << 1 | hasOpl << 2 | hasMpu << 3 (bit 2 = g_fmPresent).
    uint16_t init(int device, bool adlibPresent);

    // ---- songs (§4). `memory` is the loaded copy that holds the song (the bank layer's); it must
    // stay valid and unmoved until the slot is freed.
    // Snd_RegisterSong 1bb4:0ebc: `bmfOffset` = the BMF header in memory; a slot, or -1/-4/-6.
    int registerSong(std::span<uint8_t> memory, size_t bmfOffset);
    // Snd_AllocSlot 1bb4:0e71: the song block at `blockOffset`.
    int allocSlot(std::span<uint8_t> memory, size_t blockOffset);
    int freeSlot(int slot);  // Snd_Free 1bb4:0d8f

    // ---- playing (§10, §11). Arguments are the original's 16-bit words; sections, priorities,
    // loop flags and transposes are used as their low byte, as the driver does.
    int play(int slot, int section, int priority, int loop);  // Snd_Play 1bb4:10c1
    int stop(int slot);                                        // Snd_Stop 1bb4:084a
    int setPriority(int slot, int priority);                   // Snd_SetPriority 1bb4:102c
    int getPriority(int slot) const;                           // 1bb4:0c34
    int isActive(int slot) const;                              // Snd_IsActive 1bb4:0c1e (-2 for a bad slot)
    // Snd_SetCue 1bb4:0be5: when the song's tick counter reaches `tick`, the cue runs inside the
    // sound tick (interrupt time in the original; the render side here).
    int setCue(int slot, uint16_t tick, CueId cue);
    int getTick(int slot) const;                               // Snd_GetTick 1bb4:0c0c
    int setLoop(int slot, int loop);                           // 1bb4:0bba
    int getLoop(int slot) const;                               // 1bb4:0bd1
    int setSection(int slot, int section);                     // 1bb4:0c72 (no restart)
    int getSection(int slot) const;                            // 1bb4:0c4a
    int setLoopSection(int slot, int section);                 // 1bb4:0c91
    int getLoopSection(int slot) const;                        // 1bb4:0c5e
    int getSectionTranspose(int slot, int section) const;      // 1bb4:0cb0
    int getSectionNext(int slot, int section) const;           // 1bb4:0cd2
    int setSectionNext(int slot, int next, int section);       // 1bb4:0cf4
    int setTranspose(int slot, int section, int transpose);    // Snd_SetTranspose 1bb4:0d26
    int muteChannel(int slot, int channel);                    // Snd_MuteChannel 1bb4:0f01 (channel not checked)
    int unmuteChannel(int slot, int channel);                  // Snd_UnmuteChannel 1bb4:0f50

    // ---- pause and silence (§12)
    void pauseLock();     // Snd_PauseLock 1bb4:0b7f (not nestable)
    void resumeUnlock();  // Snd_ResumeUnlock 1bb4:0b90
    void silence();       // Snd_Silence 1bb4:0b97 (-SN): no key, frequency, BD or speaker writes after it
    void off();           // Snd_Off 1bb4:0e15 (Game_Shutdown)

    // ---- the sound tick
    // Snd_Tick 1bb4:0db3 (dos::SoundTickSource): a tick in which the driver is busy (paused) is lost.
    void soundTick() override;

    // Registers a cue callback (a far function the game passes to Snd_SetCue); ids start at 1.
    CueId registerCue(std::function<void()> fn);

    // ---- state (for the bank layer, tools and tests)
    // Sound ticks run so far (soundTick calls); inside a tick, the tick being run.
    uint64_t tickIndex() const { return inTick_ ? ticks_ - 1 : ticks_; }
    bool enabled() const { return enabled_ != 0; }
    bool hasOpl() const { return classAvailable_[3] == 1; }
    bool paused() const { return busy_ != 0; }
    // The song block of a slot (s), nullptr for a free or bad slot.
    const uint8_t* songBlock(int slot) const;
    uint16_t channelOwner(int songClass, int channel) const;
    uint8_t port61() const { return port61_; }
    uint8_t oplBdShadow() const { return bdShadow_; }
    uint8_t oplB0Shadow(int voice) const { return b0Shadow_.at(static_cast<size_t>(voice)); }
    // g_sndBusy DS:165d, the lock every API function holds: a sound tick that falls inside one is
    // lost (no catch-up). The engine's calls never overlap a tick; tools reproduce a capture's lost
    // tick with it.
    void setBusy(bool on) { busy_ = on ? 1 : 0; }
    // Tools: a run that continues a capture made after screens it does not reproduce takes the
    // last B0h values of voices 0..5 (g_oplB0Shadow DS:16de, which Opl_NoteOff writes back) from it.
    void setOplB0Shadow(int voice, uint8_t value) { b0Shadow_.at(static_cast<size_t>(voice)) = value; }

private:
    // A song block: s = memory + offset; accesses relative to s, bounds-checked against memory.
    struct Song {
        uint8_t* mem = nullptr;
        size_t size = 0;
        size_t s = 0;
        explicit operator bool() const { return mem != nullptr; }
        uint8_t rd8(int off) const;
        uint16_t rd16(int off) const;
        uint32_t rd32(int off) const;
        void wr8(int off, uint8_t v);
        void wr16(int off, uint16_t v);
        void wr32(int off, uint32_t v);
        void or8(int off, uint8_t v) { wr8(off, static_cast<uint8_t>(rd8(off) | v)); }
        void and8(int off, uint8_t v) { wr8(off, static_cast<uint8_t>(rd8(off) & v)); }
        uint8_t cls() const { return rd8(0); }
    };
    // Snd_ReadEvent 1bb4:093f's results.
    struct Event {
        uint8_t status = 0;  // AL
        uint8_t data = 0;    // AH
        int rec = 0;         // DI: the channel record offset
        uint8_t channel = 0; // DL
    };
    enum class MetaResult { Delay, Continue, Stopped };
    // Pushes g_sndBusy, sets it to 1, restores it at the end of the scope (the API functions' lock).
    class BusyGuard {
    public:
        explicit BusyGuard(SndDriver& d) : d_(d), saved_(d.busy_) { d_.busy_ = static_cast<uint16_t>((d_.busy_ & 0xFF00) | 1); }
        ~BusyGuard() { d_.busy_ = saved_; }
        BusyGuard(const BusyGuard&) = delete;
        BusyGuard& operator=(const BusyGuard&) = delete;

    private:
        SndDriver& d_;
        uint16_t saved_;
    };

    Song* slotSong(int slot);  // Snd_GetSlotSong 1bb4:000e
    const Song* slotSong(int slot) const;
    static bool sectionValid(const Song& s, int section);  // Snd_SectionEntry 1bb4:002b
    uint16_t& owner(uint8_t cls, int channel);

    uint16_t startSection(Song& s, uint8_t section);                // 1bb4:0045
    void adlibWrite(uint8_t reg, uint8_t value) { ports_.oplWrite(reg, value); }
    void adlibInitChip();                                          // 1bb4:00de
    void adlibDetect(bool present);                                // 1bb4:0142
    void oplSetVolume(uint8_t voice, uint8_t velocity);            // 1bb4:0218
    void oplSetPatch(Song& s, uint8_t program, uint8_t voice);     // 1bb4:0270
    void oplNoteOff(uint8_t voice);                                // 1bb4:0302
    void oplNoteOn(Song& s, int rec, uint8_t key, uint8_t voice);  // 1bb4:0343
    uint16_t spkPitchBend(uint8_t note, uint16_t bend) const;      // 1bb4:0541
    void hwChannelOff(uint8_t channel, uint8_t cls);               // 1bb4:0641
    void hwSilenceAll();                                           // 1bb4:0674
    void applyChannelProgram(Song& s, int channel, uint8_t program);  // 1bb4:06d9
    void releaseChannels(Song& s);                                 // 1bb4:077c
    void stopOwnerChannel(uint8_t slot, int rec);                  // 1bb4:0eec
    void takeChannels(Song& s, int slot, uint8_t priority, bool resetProgram);  // the loops of 10c1 / 102c
    void stepSweep(Song& s);                                       // class 0, 1bb4:087e
    MetaResult metaEvent(Song& s, uint16_t& si);                   // 1bb4:08ef
    Event readEvent(Song& s, uint16_t& si);                        // 1bb4:093f
    bool readDelay(Song& s, uint16_t& si);                         // the delay after an event (false: same tick)
    void stepSpeaker(Song& s);                                     // class 1 (and 4), 1bb4:0966
    void stepOpl(Song& s);                                         // class 3, 1bb4:0ad6
    void port61And(uint8_t mask);
    void port61Or(uint8_t bits);

    std::shared_ptr<const SndTables> tables_;
    SndPorts& ports_;

    std::array<Song, kSlots> slots_{};               // DS:1458
    std::array<uint8_t, kSlots> active_{};           // DS:1558
    std::array<uint8_t, kSlots> priority_{};         // DS:1598
    std::array<std::array<uint16_t, 16>, 4> owner_;  // DS:15d8, rows for classes 1..4 (class 0 uses row 0)
    uint16_t busy_ = 0;                              // DS:165d
    std::array<uint8_t, 16> spkChannelNote_;         // DS:165f
    std::array<uint8_t, 16> oplVoiceKey_;            // DS:1673 (indexed by voice & 0Fh)
    uint16_t currentSlot_ = 0;                       // DS:167e
    uint8_t enabled_ = 1;                            // DS:1680
    std::array<uint8_t, 5> classAvailable_{};        // DS:1681 ([2] hasPsg, [3] hasOpl, [4] hasMpu)
    uint16_t masterVolume_ = 256;                    // DS:1686
    std::array<uint16_t, 16> channelVolume_{};       // DS:1688
    std::array<uint8_t, 6> b0Shadow_{};              // DS:16de
    uint8_t bdShadow_ = 0;                           // DS:16e4
    uint8_t port61_ = 0;                             // the speaker bits of port 61h

    std::vector<std::function<void()>> cues_;
    uint64_t ticks_ = 0;
    bool inTick_ = false;
};

}  // namespace tmport::gg
