#pragma once
// The AdLib music driver of SST.EXE (segment 4ac9, Turbo C; fmt_sound.md §5.5): a melodic-mode
// derivative of the AdLib sample driver ("timbre" records of 13 + 13 operator parameters and two
// wave selects, a 25-steps-per-semitone F-number table), cut down and extended by the game:
//
//   AdLib_Init 00b7            slot volumes, F-number tables, register 0xBD, 0x08, every channel
//                              keyed off, wave select enabled (0x01 = 0x20, every 0xE0+ slot = 0)
//   AdLib_PlayTune 0002        orchestration k = 4 * tune + (rand() & 3): an instrument (or none)
//                              and a transposition for each of the 9 OPL channels
//   AdLib_NoteOn 029e          sequencer voice v (0..2) sounds the semitone on every channel of
//                              its group 3v..3v+2 that the orchestration uses (voice 3 is ignored):
//                              key off, then F-number / block of semitone + transposition, key on
//   AdLib_NoteOff 0432         per channel of the group: release rate 10 on both operators, then the
//                              same F-number with the key bit clear
//
// Every register write goes to an OplWriter (AdLib_Write 491b:0875, port DS:656C = 0x388) in the
// original's order. The driver's variables are the original's DGROUP variables (DS offsets below),
// so a DOSBox-X state's driver can be continued (restore()).
//
// Not ported (no caller in SST.EXE): AdLib_SetVoiceA 020f (SetVoiceVolume), AdLib_SetVoiceB 0251,
// AdLib_SetReg 0271, AdLib_PitchBend 071d, AdLib_SetPercInstr 089d. AdLib_Detect 0d0f (timer
// registers 2 and 4 during Hw_Detect) has no audible effect and is not modelled.

#include <array>
#include <cstdint>
#include <span>

namespace tmport::dos {

struct SoundTables;

// One write to the OPL2 (register, value), in the driver's order.
class OplWriter {
public:
    virtual ~OplWriter() = default;
    virtual void oplWrite(uint8_t reg, uint8_t value) = 0;
};

class AdLibDriver {
public:
    static constexpr int kChannels = 9;
    static constexpr int kSlots = 18;
    static constexpr int kVoiceEntries = 11;   // the sample driver's 11 voices (6 melodic + 5 percussive)
    static constexpr int kParams = 14;         // per slot: 13 timbre parameters + the wave select
    static constexpr int kPitchSteps = 25;     // F-number rows per semitone (NR_STEP_PITCH)
    static constexpr int kPitches = 96;        // semitones 0..95 (8 blocks)
    static constexpr uint8_t kNoteOffRelease = 10;  // release rate AdLib_NoteOffVoice gives both operators

    // Timbre parameter indices (the order of the instrument records and of DS:CA15's rows).
    enum Param : uint8_t {
        kKsl, kMulti, kFeedback, kAttack, kSustain, kSustaining, kDecay, kRelease, kLevel, kAm, kVib, kKsr, kFm, kWave
    };

    // The driver's variables in DGROUP (BSS of the original; restore() reads them at these offsets).
    struct State {
        std::array<uint8_t, kPitches> blockOf{};      // c6e4 noteDIV12: block of a semitone
        std::array<uint8_t, kPitches> noteOf{};       // c744 noteMOD12: semitone within the block
        uint8_t amDepth = 0;                          // c7a4
        std::array<uint8_t, kChannels> used{};        // c7a5 channel used by the orchestration
        uint8_t noteSel = 0;                          // c7ae
        uint8_t vibDepth = 0;                         // c7af
        std::array<uint8_t, kVoiceEntries> notePitch{};  // c7b0 last pitch per channel (byte, read signed)
        uint16_t pitchRange = 0;                      // c7bb
        std::array<std::array<uint16_t, 12>, kPitchSteps> fnum{};  // c7bd F-numbers, 25 rows of 12
        std::array<std::array<uint8_t, kParams>, kSlots> slot{};   // ca15 parameters per operator slot
        std::array<uint8_t, kVoiceEntries> keyOn{};   // cb11 key-on bit per channel
        uint16_t waveSelect = 0;                      // cb1c 0x20 when wave selects are enabled
        std::array<uint8_t, kVoiceEntries> fnumRow{};  // cb1e far pointer per channel into c7bd, as a row
        std::array<uint8_t, kSlots> slotVolume{};     // cb4a relative volume per slot (0x7F)
        std::array<int8_t, kChannels> transpose{};    // cb5c semitones per channel (orchestration)
        uint16_t pitchRangeStep = 0;                  // cb65
        std::array<int16_t, kVoiceEntries> halfToneOffset{};  // cb67
    };

    // DGROUP offsets of the variables above (fmt_sound.md §5.5).
    static constexpr uint16_t kDsBlockOf = 0xC6E4, kDsNoteOf = 0xC744, kDsAmDepth = 0xC7A4, kDsUsed = 0xC7A5,
                              kDsNoteSel = 0xC7AE, kDsVibDepth = 0xC7AF, kDsNotePitch = 0xC7B0, kDsPitchRange = 0xC7BB,
                              kDsFnum = 0xC7BD, kDsSlotParams = 0xCA15, kDsKeyOn = 0xCB11, kDsWaveSelect = 0xCB1C,
                              kDsFnumPtr = 0xCB1E, kDsSlotVolume = 0xCB4A, kDsTranspose = 0xCB5C,
                              kDsPitchRangeStep = 0xCB65, kDsHalfToneOffset = 0xCB67;
    static constexpr uint16_t kDsStateBegin = kDsBlockOf;
    static constexpr uint16_t kDsStateEnd = kDsHalfToneOffset + 2 * kVoiceEntries;  // one past
    // Read-only tables in the initialised DGROUP.
    static constexpr uint16_t kDsSlotMap = 0x8124;        // u8[9][2] modulator and carrier slot per channel
    static constexpr uint16_t kDsSlotOffset = 0x8136;     // u8[18] register offset per slot
    static constexpr uint16_t kDsSlotCarrier = 0x8148;    // u8[18] 1 = carrier
    static constexpr uint16_t kDsSlotChannel = 0x815A;    // u8[18] channel per slot
    static constexpr uint16_t kDsInstrumentPtrs = 0x88BC; // far[24] -> 0x4E-byte records (13 + 13 i16, 2 waves)
    static constexpr uint16_t kDsOrchInstrument = 0x891C; // u8[12][9] instrument per channel, 0xFF unused
    static constexpr uint16_t kDsOrchTranspose = 0x8988;  // i8[12][9] transposition per channel

    // `tables` (the DGROUP image) must outlive the driver; `out` receives the register writes.
    AdLibDriver(const SoundTables& tables, OplWriter& out);

    void init();                            // 4ac9:00b7 AdLib_Init
    void playTune(int tune, int variant);   // 4ac9:0002 AdLib_PlayTune (variant = rand() & 3, drawn by the caller)
    void noteOn(int voice, uint16_t pitch);  // 4ac9:029e AdLib_NoteOn (pitch = DS:63AC[voice], note - 1 + 12 * octave)
    void noteOff(int voice);                // 4ac9:0432 AdLib_NoteOff

    const State& state() const { return s_; }

    // A DOSBox-X state's driver: the variables from its DGROUP image (DS:0000..), then every register
    // the driver wrote is written again from them (instruments of the used channels, the channels'
    // F-number, block and key-on bit, 0x01, 0x08, 0xBD): the chip holds what the original's held, except
    // that the envelopes start again.
    void restore(std::span<const uint8_t> dgroup);
    // The variables into a DGROUP image (DS:0000.., at least kDsStateEnd bytes), as the original holds
    // them (the F-number pointers as DGROUP far pointers with segment `dsSegment`).
    void saveTo(std::span<uint8_t> dgroup, uint16_t dsSegment = 0x4D94) const;

    // F-number of a row (0..24) and semitone (0..11) (fmt_sound.md §5.5).
    uint16_t fnumber(int row, int semitone) const { return s_.fnum[static_cast<size_t>(row)][static_cast<size_t>(semitone)]; }

private:
    void rewriteRegisters();  // restore(): every register the driver wrote, from the variables
    void write(uint8_t reg, uint8_t value) { out_.oplWrite(reg, value); }
    uint8_t ds8(uint16_t off) const;
    int16_t ds16s(uint16_t off) const;
    uint8_t slotOf(int channel, int op) const;  // DS:8124
    uint8_t slotOffset(int slot) const;         // DS:8136
    bool slotIsCarrier(int slot) const;         // DS:8148
    uint8_t slotChannel(int slot) const;        // DS:815A

    void initSlotVolume();                       // 4ac9:054a AdLib_InitSlotVolume
    void buildFreqTable();                       // 4ac9:067e AdLib_InitFNums
    void setFNumRow(int row, int num, int den);  // 4ac9:0601 AdLib_SetFNumRow
    void setGlobalParams(uint8_t am, uint8_t vib, uint8_t noteSel);  // 4ac9:0179 AdLib_SetGlobalParams
    void setPitchRange(uint16_t range);          // 4ac9:0150 AdLib_SetPitchRange
    void setWaveSelect(bool on);                 // 4ac9:010d AdLib_SetWaveSel
    void setVoiceTimbre(int channel, uint16_t record);  // 4ac9:019a AdLib_SetVoiceTimbre
    void setSlotParams(int slot, uint16_t params, int16_t wave);  // 4ac9:0847 AdLib_SetSlotParams
    void setSlotParam(int slot, Param p, uint8_t value);          // 4ac9:081a AdLib_SetSlotParam
    void writeAllParams(int slot);               // 4ac9:095e AdLib_WriteAllParams
    void writeParam(int slot, int p);            // 4ac9:08db AdLib_WriteParam
    void noteOffChannel(int channel);            // 4ac9:04ef AdLib_NoteOffVoice
    void setFreq(int channel, int pitch, bool keyOn);  // 4ac9:0c44 AdLib_SetFreq
    void keyOff(int channel);                    // 4ac9:0ce8 AdLib_KeyOff

    // Register writers (each writes one register from the slot parameters or the globals).
    void writeAmVibRhythm();       // 4ac9:0bd6 AdLib_WriteAmVibRhythm   0xBD
    void writeNoteSel();           // 4ac9:0a0e AdLib_WriteNoteSel       0x08
    void writeKslLevel(int slot);  // 4ac9:099c AdLib_WriteKslLevel 0x40 + slot offset
    void writeFeedbackFm(int slot);  // 4ac9:0a29 AdLib_WriteFeedbackFm  0xC0 + channel (modulator slots)
    void writeAttackDecay(int slot);  // 4ac9:0a86 AdLib_SetAD      0x60 + slot offset
    void writeSustainRelease(int slot);  // 4ac9:0ad6 AdLib_SetSR   0x80 + slot offset
    void writeAmVibEgKsrMulti(int slot);  // 4ac9:0b26 AdLib_WriteAmVibEgKsrMulti 0x20 + slot offset
    void writeWave(int slot);      // 4ac9:0c03 AdLib_SetWave       0xE0 + slot offset

    const SoundTables& tables_;
    OplWriter& out_;
    State s_;
};

}  // namespace tmport::dos
