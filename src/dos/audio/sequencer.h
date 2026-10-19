#pragma once
// The sound driver of SST.EXE (segment 491b, hand-written assembly): a fixed-tick sequencer for
// one effect block and one music block, run from the timer interrupt at 1193182 / 5041 =
// 236.70 Hz (re/dos/docs/fmt_sound.md §3-§5, code_runtime.md §2.3).
//
// The sequencer is device independent: it writes to SoundPorts (PIT channel 2 and port 0x61 for
// the speaker, the SN76496 port, the AdLib driver 4ac9), and something else turns those writes
// into samples (DosSoundRenderer). It only advances when timerTick() is called, so it can run
// inside an audio callback by sample count (fmt_sound.md §9) or from a virtual PIT.
//
// Quirks kept because they change what is heard [O, disassembly 491b]:
//  * an effect freezes the music on the speaker and the Tandy (Sound_Tick 491b:0058); on AdLib the
//    effect plays on the speaker and the music goes on (491b:002a);
//  * effect priority by id: Sfx_Play(id) is ignored while an effect with a lower id plays
//    (491b:06fa); on the Tandy Sfx_Play first calls Sfx_Stop, which resets the current id to
//    0xFFFF, so there any effect replaces any other;
//  * Sfx_Play does not clear the effect block (stale tempo/envelope carry over), Music_Play does;
//    Music_Play with the music off leaves the music block cleared, i.e. stopped;
//  * speaker release gap (note off at ticks left == release - 1), Tandy release one tick earlier,
//    ties continuing the envelope, the 0xFF envelope tick without a write, PSG octave folding;
//  * a voice turned off on the PSG gets period 0 (491b:0600), so music resuming after an effect
//    sounds at period 0 until each voice's next note.

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#include "dos/audio/sound_data.h"

namespace tmport::dos {

// What the driver writes to.
class SoundPorts {
public:
    virtual ~SoundPorts() = default;
    // PIT channel 2 divisor (OUT 0x42 low, high; mode 3 was set once by Timer_Install).
    virtual void speakerDivisor(uint16_t divisor) = 0;
    // Port 0x61 bits 0-1 (timer 2 gate + speaker data) set (true) or cleared (false).
    virtual void speakerEnable(bool on) = 0;
    // One byte to the SN76496 port (0xC0 on the Tandy 1000, 0x205 on the PS/1).
    virtual void psgWrite(uint8_t value) = 0;
    // The AdLib music driver (segment 4ac9, wave 5): AdLib_Init 4ac9:00b7, AdLib_PlayTune
    // 4ac9:0002 (variant = rand() & 3, drawn by the caller), AdLib_NoteOn 4ac9:029e with the semitone
    // `note - 1 + 12 * octave`, AdLib_NoteOff 4ac9:0432.
    virtual void oplInit() {}
    virtual void oplPlayTune(int /*tune*/, int /*variant*/) {}
    virtual void oplNoteOn(int /*voice*/, uint8_t /*semitone*/) {}
    virtual void oplNoteOff(int /*voice*/) {}
    // PIT ticks the last oplPlayTune() takes on the reference machine: Music_Play sets the music
    // block's status only after AdLib_PlayTune returns, so the tune waits that long (opl_backend.h).
    virtual int oplPlayTuneTicks() const { return 0; }
};

class Sequencer {
public:
    static constexpr int kVoices = 4;
    static constexpr uint16_t kIdle = 0xFFFF;

    // One 0x78-byte state block: effects at DS:6448, music at DS:64c0. The comments give the DS
    // offset of each field in the work copy at DS:63ce that the driver processes.
    struct Block {
        uint16_t status = 0;      // 63ce: 0 idle, 1 start requested, 2 playing
        uint16_t id = kIdle;      // 63d0: effect / tune number
        uint16_t tempo = 0;       // 63d2: ticks per whole note
        uint16_t tempoAdj = 0;    // 63d4: signed adjustment (commands 1, 2)
        std::array<uint16_t, kVoices> ptr{};        // 63d6: stream position (buffer offset)
        std::array<uint16_t, kVoices> start{};      // 63de: stream start (loop)
        std::array<uint16_t, kVoices> ticksLeft{};  // 63e6
        std::array<uint16_t, kVoices> rawDur{};     // 63ee: ticks per raw-pitch event
        std::array<uint16_t, kVoices> active{};     // 63f6: 0 or 2
        std::array<uint16_t, kVoices> legato{};     // 63fe
        std::array<uint16_t, kVoices> release{};    // 6406
        std::array<uint16_t, kVoices> envStart{};   // 640e: near pointer into DGROUP
        std::array<uint16_t, kVoices> envPos{};     // 6416
        std::array<uint16_t, kVoices> envDone{};    // 641e: 0 running, 1 sustain
        std::array<uint16_t, kVoices> sustain{};    // 6426
        std::array<uint16_t, kVoices> newNote{};    // 642e
        std::array<uint16_t, kVoices> rest{};       // 6436
        std::array<uint16_t, kVoices> tie{};        // 643e
    };

    // The bank must outlive the sequencer. Calls Sound_Init (491b:067b).
    Sequencer(const SoundBank& bank, SoundPorts& ports);

    SoundCard device() const { return bank_.device; }

    // ---- the driver's entry points (called by the game between interrupts)
    void sfxPlay(int id);                       // 491b:06d7 Sfx_Play
    void sfxStop();                             // 491b:074b Sfx_Stop
    void musicPlay(int tune, int adlibVariant);  // 491b:0791 Music_Play (variant: AdLib only, rand() & 3)
    void musicStop();                           // 491b:0811 Music_Stop
    void silenceAll();                          // 491b:0854 Sound_SilenceAll

    // ---- driver flags set by the game
    void setSoundOn(bool on) { soundOn_ = on; }      // DS:63b4 g_soundOn (Sfx_Play only)
    void setMusicOn(bool on) { musicOn_ = on; }      // DS:63b6 g_musicOn (Music_Play only)
    bool soundOn() const { return soundOn_; }
    bool musicOn() const { return musicOn_; }
    void setMusicLoop(bool on) { musicLoop_ = on; }  // DS:63ba (set by the title and the scene loader)
    void setSfxLoop(bool on) { sfxLoop_ = on; }      // DS:63b8 (set by the end sequence, player states 44/45/64/65)
    // DS:37ce: 0xFFFF while g_uiMode == 0, 0 while a menu or box is open (Sound_SyncState 43cb:046e).
    void setMusicGate(uint16_t mask) { musicGate_ = mask; }
    void setMusicPaused(bool paused) { musicGate_ = paused ? 0 : 0xFFFF; }

    // ---- one PIT interrupt: Timer_ISR 1000:0428 step 4 calls Sound_Tick when
    // (music.status & DS:37ce) | sfx.status != 0.
    void timerTick();

    const Block& sfxBlock() const { return sfx_; }
    const Block& musicBlock() const { return music_; }
    bool sfxActive() const { return sfx_.status != 0; }
    bool musicActive() const { return music_.status != 0 || musicHold_ > 0; }  // (a tune held back by AdLib_PlayTune counts)
    // ---- a DOSBox-X state's driver (wave 4): the blocks as the original holds them in DGROUP
    static constexpr size_t kBlockBytes = 0x78;
    // The 0x78-byte block at DS:6448 (effects) or DS:64c0 (music) in the field order above (stream
    // pointers are offsets into the effect / music file, envelope pointers DS offsets).
    static Block blockFromDgroup(std::span<const uint8_t> bytes);
    static std::array<uint8_t, kBlockBytes> blockToDgroup(const Block& b);
    // Continues a driver saved between two timer interrupts: both blocks and the voice count DS:63be
    // as they were; then the device gets what the current notes had written to it (each sounding
    // voice's pitch from the time-taking event before its stream pointer, the speaker gate off past
    // the release point, a rest as voice off; the Tandy volumes follow at the next tick's volume
    // pass). `adlibVariant` = the orchestration of the playing tune (AdLib). With `oplRestored` the
    // AdLib driver was restored from the state as well (its registers hold the sounding notes), so
    // no AdLib_PlayTune and no note is sent again.
    void restore(const Block& sfx, const Block& music, int voices, int adlibVariant = 0, bool oplRestored = false);
    // PIT ticks the tune of the last Music_Play still waits for AdLib_PlayTune (0 = none).
    int musicHoldTicks() const { return musicHold_; }

    // Times a looping block restarted (DS:63ba / 63b8), for tools and tests.
    uint32_t musicLoops() const { return musicLoopCount_; }
    // A stream that never yields a duration (it would hang the original's interrupt) was cut.
    bool streamGuardTripped() const { return guardTripped_; }

private:
    void soundTick();                 // 491b:0004 Sound_Tick
    void runBlock(Block& b, bool music);  // 491b:00be Sound_RunChannel
    void restartVoices(Block& b);     // 491b:0136 Sound_RestartVoices
    void stepVoices(Block& b);        // 491b:0181 Sound_StepVoices
    void nextEvent(Block& b, int v);  // 491b:01ee Sound_NextEvent
    void noteOn(Block& b, int v, uint8_t oct, uint8_t note);  // 491b:025d Sound_NoteOn
    void noteLength(Block& b, int v, uint8_t code);           // 491b:02d8 Sound_NoteLength
    void updateVolumes(Block& b);     // 491b:033d Tandy_UpdateVolumes
    void control(Block& b, int v, uint8_t cmd, uint8_t value);  // 491b:03fe Sound_Control
    void glide(Block& b, int v, uint8_t oct, uint8_t value);    // 491b:04ce Sound_Glide
    void glidePitch(int v, uint8_t oct, uint8_t value);         // its pitch part
    void resumeVoice(Block& b, int v, bool music);              // restore(): the note in progress
    void speakerOn(int v);            // 491b:0575 Speaker_On
    void voiceOff(int v);             // 491b:05b8 Sound_VoiceOff
    void setFrequency(int v, uint16_t value);  // 491b:0607 Sound_SetFrequency
    void allVoicesDone(Block& b);     // 491b:0656 Sound_AllVoicesDone

    const SoundFile& fileOf(bool music) const { return music ? bank_.music : bank_.sfx; }
    void psgOut(uint8_t v) { ports_.psgWrite(v); }  // 49a6:0001 SoundPort_Out

    const SoundBank& bank_;
    const SoundTables& tables_;
    SoundPorts& ports_;

    Block sfx_;    // DS:6448
    Block music_;  // DS:64c0
    // Sound_RunChannel works on a copy of the block at DS:63ce; this is the block being run (and
    // whose file the events come from), set for the duration of runBlock.
    const SoundFile* file_ = nullptr;

    SoundCard device_;  // DS:63bc (the AdLib tick switches it to the speaker for the effect)
    int voices_ = 1;      // DS:63be
    std::array<uint16_t, kVoices> owned_{};  // DS:63c6: voices held by an effect (music run only)
    std::array<uint16_t, kVoices> adlibPitch_{};  // DS:63ac
    bool soundOn_ = true;     // DS:63b4 (1 in the EXE)
    bool musicOn_ = true;     // DS:63b6 (1 in the EXE)
    bool sfxLoop_ = false;    // DS:63b8
    bool musicLoop_ = false;  // DS:63ba
    uint16_t musicGate_ = 0xFFFF;  // DS:37ce
    int musicHold_ = 0;  // AdLib: ticks until Music_Play's status 1 (AdLib_PlayTune's set-up time)
    uint32_t musicLoopCount_ = 0;
    bool guardTripped_ = false;
};

}  // namespace tmport::dos
