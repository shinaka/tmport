#include "dos/audio/adlib_driver.h"

#include "dos/audio/sound_data.h"

namespace tmport::dos {

namespace {

constexpr int kRecordWaveOffset = 0x34;  // two i16 wave selects after the 13 + 13 parameters
constexpr int kRecordCarrierOffset = 0x1A;
constexpr uint8_t kMaxVolume = 0x7F;

}  // namespace

AdLibDriver::AdLibDriver(const SoundTables& tables, OplWriter& out) : tables_(tables), out_(out) {}

uint8_t AdLibDriver::ds8(uint16_t off) const { return tables_.ds8(off); }
int16_t AdLibDriver::ds16s(uint16_t off) const { return static_cast<int16_t>(tables_.ds16(off)); }
uint8_t AdLibDriver::slotOf(int channel, int op) const { return ds8(static_cast<uint16_t>(kDsSlotMap + 2 * channel + op)); }
uint8_t AdLibDriver::slotOffset(int slot) const { return ds8(static_cast<uint16_t>(kDsSlotOffset + slot)); }
bool AdLibDriver::slotIsCarrier(int slot) const { return ds8(static_cast<uint16_t>(kDsSlotCarrier + slot)) != 0; }
uint8_t AdLibDriver::slotChannel(int slot) const { return ds8(static_cast<uint16_t>(kDsSlotChannel + slot)); }

// ------------------------------------------------------------------------------ entry points

// 4ac9:00b7 AdLib_Init (called by Sound_Init 491b:06a9)
void AdLibDriver::init() {
    initSlotVolume();
    buildFreqTable();
    writeAmVibRhythm();  // 4ac9:0103 AdLib_SetMode: the sample driver's SetMode(0), melodic only
    setGlobalParams(0, 0, 0);
    for (int ch = 0; ch < kChannels; ++ch) keyOff(ch);
    setPitchRange(1);
    setWaveSelect(true);
}

// 4ac9:0002 AdLib_PlayTune: arrangement k = 4 * tune + (rand() & 3); three 9-byte rows per k
// (instrument, transposition, and a third row DS:89F4 that is addressed but never read).
void AdLibDriver::playTune(int tune, int variant) {
    const auto k = static_cast<uint16_t>(static_cast<uint16_t>(tune << 2) + (variant & 3));
    s_.used.fill(0);  // memset(DS:C7A5, 0, 9)
    for (int ch = 0; ch < kChannels; ++ch) {
        const uint8_t inst = ds8(static_cast<uint16_t>(kDsOrchInstrument + 9 * k + ch));
        if (inst == 0xFF) continue;
        s_.transpose[static_cast<size_t>(ch)] = static_cast<int8_t>(ds8(static_cast<uint16_t>(kDsOrchTranspose + 9 * k + ch)));
        // far pointer DS:88BC[inst] (index sign-extended); the records lie in DGROUP
        const auto ptr = static_cast<uint16_t>(kDsInstrumentPtrs + 4 * static_cast<int8_t>(inst));
        setVoiceTimbre(ch, tables_.ds16(ptr));
        s_.used[static_cast<size_t>(ch)] = 1;
    }
}

// 4ac9:029e AdLib_NoteOn: voices 0, 1, 2 drive channel groups 0-2, 3-5, 6-8 (unsigned compare: 3
// and above do nothing).
void AdLibDriver::noteOn(int voice, uint16_t pitch) {
    if (static_cast<unsigned>(voice) > 2) return;
    for (int ch = 3 * voice; ch < 3 * voice + 3; ++ch) {
        if (!s_.used[static_cast<size_t>(ch)]) continue;
        keyOff(ch);
        setFreq(ch, s_.transpose[static_cast<size_t>(ch)] + static_cast<int16_t>(pitch), true);
    }
}

// 4ac9:0432 AdLib_NoteOff (signed compare: negative voices would fall through to nothing as well)
void AdLibDriver::noteOff(int voice) {
    if (voice > 2 || voice < 0) return;
    for (int ch = 3 * voice; ch < 3 * voice + 3; ++ch)
        if (s_.used[static_cast<size_t>(ch)]) noteOffChannel(ch);
}

// ---------------------------------------------------------------------------- initialisation

// 4ac9:054a AdLib_InitSlotVolume (the sample driver's InitSlotVolume)
void AdLibDriver::initSlotVolume() { s_.slotVolume.fill(kMaxVolume); }

// 4ac9:067e AdLib_InitFNums (InitFNums): 25 rows of 12 F-numbers, row r detuned by 4r/100 of a
// semitone; every channel points at row 0 with no half-tone offset (AdLib_PitchBend, which would
// change both, is never called); the block / semitone of the pitches 0..95.
void AdLibDriver::buildFreqTable() {
    for (int row = 0, num = 0; row < kPitchSteps; ++row, num += 100 / kPitchSteps) setFNumRow(row, num, 100);
    s_.fnumRow.fill(0);
    s_.halfToneOffset.fill(0);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 12; ++j) {
            s_.blockOf[static_cast<size_t>(12 * i + j)] = static_cast<uint8_t>(i);
            s_.noteOf[static_cast<size_t>(12 * i + j)] = static_cast<uint8_t>(j);
        }
}

// 4ac9:0601 AdLib_SetFNumRow (SetFNum) with 4ac9:055c AdLib_CalcFNum (CalcPremFNum): the F-number of C
// detuned by num/den semitone, in 32-bit signed arithmetic (Turbo C's long multiply and divide),
// then each next semitone as * 106 / 100; stored as (low 16 bits + 4) >> 3.
void AdLibDriver::setFNumRow(int row, int num, int den) {
    // MUL then CWD: the 16-bit products are sign-extended
    const int32_t d100 = static_cast<int16_t>(static_cast<uint16_t>(den * 100));
    const int32_t n6 = static_cast<int16_t>(static_cast<uint16_t>(num * 6));
    int32_t f8 = static_cast<int32_t>(static_cast<uint32_t>(d100 + n6) * 52088u);  // 260.44 Hz * 2 * 100
    f8 /= d100 * 25;
    int32_t val = static_cast<int32_t>(static_cast<uint32_t>(f8) << 14);
    val = static_cast<int32_t>(static_cast<uint32_t>(val) * 9u);
    val /= 179 * 625;
    auto& out = s_.fnum[static_cast<size_t>(row)];
    out[0] = static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint16_t>(val) + 4) >> 3);
    for (int i = 1; i < 12; ++i) {
        val = static_cast<int32_t>(static_cast<uint32_t>(val) * 106u);
        val /= 100;
        out[static_cast<size_t>(i)] = static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint16_t>(val) + 4) >> 3);
    }
}

// 4ac9:0179 AdLib_SetGlobalParams (SetGParam): AM depth, vibrato depth, note select.
void AdLibDriver::setGlobalParams(uint8_t am, uint8_t vib, uint8_t noteSel) {
    s_.amDepth = am;
    s_.vibDepth = vib;
    s_.noteSel = noteSel;
    writeAmVibRhythm();
    writeNoteSel();
}

// 4ac9:0150 AdLib_SetPitchRange (SetPitchRange): 1..12 semitones, kept for AdLib_PitchBend.
void AdLibDriver::setPitchRange(uint16_t range) {
    if (range > 12) range = 12;
    if (range < 1) range = 1;
    s_.pitchRange = range;
    s_.pitchRangeStep = static_cast<uint16_t>(range * kPitchSteps);
}

// 4ac9:010d AdLib_SetWaveSel (SetWaveSel): every slot's wave select 0, then 0x01 = 0x20 / 0.
void AdLibDriver::setWaveSelect(bool on) {
    s_.waveSelect = on ? 0x20 : 0;
    for (int slot = 0; slot < kSlots; ++slot) write(static_cast<uint8_t>(0xE0 + slotOffset(slot)), 0);
    write(0x01, static_cast<uint8_t>(s_.waveSelect));
}

// ------------------------------------------------------------------------------- instruments

// 4ac9:019a AdLib_SetVoiceTimbre (SetVoiceTimbre): the record's modulator half on the channel's
// first slot, the carrier half on its second, each with its wave select.
void AdLibDriver::setVoiceTimbre(int channel, uint16_t record) {
    const int16_t wave0 = ds16s(static_cast<uint16_t>(record + kRecordWaveOffset));
    const int16_t wave1 = ds16s(static_cast<uint16_t>(record + kRecordWaveOffset + 2));
    setSlotParams(slotOf(channel, 0), record, wave0);
    setSlotParams(slotOf(channel, 1), static_cast<uint16_t>(record + kRecordCarrierOffset), wave1);
}

// 4ac9:0847 AdLib_SetSlotParams (SetSlotParam): the low bytes of 13 i16 parameters, the wave
// select & 3, then every register of the slot.
void AdLibDriver::setSlotParams(int slot, uint16_t params, int16_t wave) {
    auto& p = s_.slot[static_cast<size_t>(slot)];
    for (int i = 0; i < kParams - 1; ++i) p[static_cast<size_t>(i)] = ds8(static_cast<uint16_t>(params + 2 * i));
    p[kWave] = static_cast<uint8_t>(wave & 3);
    writeAllParams(slot);
}

// 4ac9:081a AdLib_SetSlotParam (SetASlotParam): one parameter, then the register holding it.
void AdLibDriver::setSlotParam(int slot, Param param, uint8_t value) {
    s_.slot[static_cast<size_t>(slot)][param] = value;
    writeParam(slot, param);
}

// 4ac9:095e AdLib_WriteAllParams (SndSetAllPrm)
void AdLibDriver::writeAllParams(int slot) {
    writeAmVibRhythm();
    writeNoteSel();
    writeKslLevel(slot);
    writeFeedbackFm(slot);
    writeAttackDecay(slot);
    writeSustainRelease(slot);
    writeAmVibEgKsrMulti(slot);
    writeWave(slot);
}

// 4ac9:08db AdLib_WriteParam (SndSetPrm): the jump table at 4ac9:08f3 by parameter.
void AdLibDriver::writeParam(int slot, int param) {
    switch (param) {
        case kKsl:
        case kLevel: writeKslLevel(slot); break;
        case kMulti:
        case kSustaining:
        case kAm:
        case kVib:
        case kKsr: writeAmVibEgKsrMulti(slot); break;
        case kFeedback:
        case kFm: writeFeedbackFm(slot); break;
        case kAttack:
        case kDecay: writeAttackDecay(slot); break;
        case kSustain:
        case kRelease: writeSustainRelease(slot); break;
        case kWave: writeWave(slot); break;
        case 14:
        case 15:
        case 17: writeAmVibRhythm(); break;  // AM depth, vibrato depth, percussion mode
        case 16: writeNoteSel(); break;
        default: break;  // above 17: nothing
    }
}

// ------------------------------------------------------------------------------------- notes

// 4ac9:04ef AdLib_NoteOffVoice: the game's own note off. Both operators get release rate 10 (it stays
// until the next AdLib_PlayTune loads the instrument again), then the channel's last pitch is written
// again with the key bit clear (the pitch byte is read signed).
void AdLibDriver::noteOffChannel(int channel) {
    setSlotParam(slotOf(channel, 0), kRelease, kNoteOffRelease);
    setSlotParam(slotOf(channel, 1), kRelease, kNoteOffRelease);
    setFreq(channel, static_cast<int8_t>(s_.notePitch[static_cast<size_t>(channel)]), false);
}

// 4ac9:0c44 AdLib_SetFreq: keeps the key bit and the pitch (low byte), adds the half-tone offset,
// clamps to 0..95, then 0xA0 + channel = F-number low byte, 0xB0 + channel = key << 5 | block << 2 |
// F-number bits 8-9.
void AdLibDriver::setFreq(int channel, int pitch, bool keyOn) {
    const auto c = static_cast<size_t>(channel);
    s_.keyOn[c] = keyOn ? 1 : 0;
    s_.notePitch[c] = static_cast<uint8_t>(pitch);
    int p = static_cast<int16_t>(static_cast<uint16_t>(pitch + s_.halfToneOffset[c]));
    if (p > 95) p = 95;
    if (p < 0) p = 0;
    const uint16_t fnum = s_.fnum[s_.fnumRow[c]][s_.noteOf[static_cast<size_t>(p)]];
    write(static_cast<uint8_t>(0xA0 + channel), static_cast<uint8_t>(fnum));
    const int b = (keyOn ? 0x20 : 0) + (s_.blockOf[static_cast<size_t>(p)] << 2) + ((fnum >> 8) & 3);
    write(static_cast<uint8_t>(0xB0 + channel), static_cast<uint8_t>(b));
}

// 4ac9:0ce8 AdLib_KeyOff (SoundChut): 0xB0 + channel = 0, then 0xA0 + channel = 0.
void AdLibDriver::keyOff(int channel) {
    write(static_cast<uint8_t>(0xB0 + channel), 0);
    write(static_cast<uint8_t>(0xA0 + channel), 0);
}

// ----------------------------------------------------------------------------- the registers

// 4ac9:0bd6 AdLib_WriteAmVibRhythm (SndSAmVibRhythm): melodic mode only (no percussion bits).
void AdLibDriver::writeAmVibRhythm() {
    write(0xBD, static_cast<uint8_t>((s_.amDepth ? 0x80 : 0) | (s_.vibDepth ? 0x40 : 0)));
}

// 4ac9:0a0e AdLib_WriteNoteSel (SndSNoteSel)
void AdLibDriver::writeNoteSel() { write(0x08, s_.noteSel ? 0x40 : 0); }

// 4ac9:099c AdLib_WriteKslLevel (SndSKslLevel): the output level scaled by the slot volume
// (0x7F leaves it unchanged), KSL in bits 6-7.
void AdLibDriver::writeKslLevel(int slot) {
    const auto& p = s_.slot[static_cast<size_t>(slot)];
    unsigned t = 63u - (static_cast<unsigned>(static_cast<int8_t>(p[kLevel])) & 0x3F);
    t = static_cast<uint16_t>(static_cast<int8_t>(s_.slotVolume[static_cast<size_t>(slot)]) * t);
    t = static_cast<uint16_t>(t + t + kMaxVolume);
    t = 63u - t / (2 * kMaxVolume);
    t |= static_cast<unsigned>(static_cast<int8_t>(p[kKsl]) << 6);
    write(static_cast<uint8_t>(0x40 + slotOffset(slot)), static_cast<uint8_t>(t));
}

// 4ac9:0a29 AdLib_WriteFeedbackFm (SndSFeedFm): modulator slots only: feedback << 1, bit 0 = additive.
void AdLibDriver::writeFeedbackFm(int slot) {
    if (slotIsCarrier(slot)) return;
    const auto& p = s_.slot[static_cast<size_t>(slot)];
    const int t = (static_cast<int8_t>(p[kFeedback]) << 1) | (p[kFm] ? 0 : 1);
    write(static_cast<uint8_t>(0xC0 + slotChannel(slot)), static_cast<uint8_t>(t));
}

// 4ac9:0a86 AdLib_SetAD (SndSAttDecay)
void AdLibDriver::writeAttackDecay(int slot) {
    const auto& p = s_.slot[static_cast<size_t>(slot)];
    const int t = (static_cast<int8_t>(p[kAttack]) << 4) | (static_cast<int8_t>(p[kDecay]) & 0x0F);
    write(static_cast<uint8_t>(0x60 + slotOffset(slot)), static_cast<uint8_t>(t));
}

// 4ac9:0ad6 AdLib_SetSR (SndSSusRelease)
void AdLibDriver::writeSustainRelease(int slot) {
    const auto& p = s_.slot[static_cast<size_t>(slot)];
    const int t = (static_cast<int8_t>(p[kSustain]) << 4) | (static_cast<int8_t>(p[kRelease]) & 0x0F);
    write(static_cast<uint8_t>(0x80 + slotOffset(slot)), static_cast<uint8_t>(t));
}

// 4ac9:0b26 AdLib_WriteAmVibEgKsrMulti (SndSAVEK): tremolo, vibrato, sustaining, KSR, multiplier.
void AdLibDriver::writeAmVibEgKsrMulti(int slot) {
    const auto& p = s_.slot[static_cast<size_t>(slot)];
    const int t = (p[kAm] ? 0x80 : 0) + (p[kVib] ? 0x40 : 0) + (p[kSustaining] ? 0x20 : 0) + (p[kKsr] ? 0x10 : 0) +
                  (static_cast<int8_t>(p[kMulti]) & 0x0F);
    write(static_cast<uint8_t>(0x20 + slotOffset(slot)), static_cast<uint8_t>(t));
}

// 4ac9:0c03 AdLib_SetWave (SndWaveSelect): the slot's wave while wave selects are enabled, else 0.
void AdLibDriver::writeWave(int slot) {
    const int t = s_.waveSelect ? (static_cast<int8_t>(s_.slot[static_cast<size_t>(slot)][kWave]) & 3) : 0;
    write(static_cast<uint8_t>(0xE0 + slotOffset(slot)), static_cast<uint8_t>(t));
}

// --------------------------------------------------------------------------------- restore

void AdLibDriver::restore(std::span<const uint8_t> dg) {
    if (dg.size() < kDsStateEnd) return;
    const auto b = [&](uint16_t off) { return dg[off]; };
    const auto w = [&](uint16_t off) { return static_cast<uint16_t>(dg[off] | (dg[static_cast<size_t>(off) + 1] << 8)); };
    for (int i = 0; i < kPitches; ++i) {
        s_.blockOf[static_cast<size_t>(i)] = b(static_cast<uint16_t>(kDsBlockOf + i));
        s_.noteOf[static_cast<size_t>(i)] = b(static_cast<uint16_t>(kDsNoteOf + i));
    }
    s_.amDepth = b(kDsAmDepth);
    for (int i = 0; i < kChannels; ++i) s_.used[static_cast<size_t>(i)] = b(static_cast<uint16_t>(kDsUsed + i));
    s_.noteSel = b(kDsNoteSel);
    s_.vibDepth = b(kDsVibDepth);
    for (int i = 0; i < kVoiceEntries; ++i) {
        const auto c = static_cast<size_t>(i);
        s_.notePitch[c] = b(static_cast<uint16_t>(kDsNotePitch + i));
        s_.keyOn[c] = b(static_cast<uint16_t>(kDsKeyOn + i));
        // a far pointer into the F-number rows (offset word first); anything else: row 0
        const uint16_t ptr = w(static_cast<uint16_t>(kDsFnumPtr + 4 * i));
        const int row = ptr >= kDsFnum ? (ptr - kDsFnum) / 24 : 0;
        s_.fnumRow[c] = static_cast<uint8_t>(row < kPitchSteps && (ptr - kDsFnum) % 24 == 0 ? row : 0);
        s_.halfToneOffset[c] = static_cast<int16_t>(w(static_cast<uint16_t>(kDsHalfToneOffset + 2 * i)));
    }
    s_.pitchRange = w(kDsPitchRange);
    for (int r = 0; r < kPitchSteps; ++r)
        for (int j = 0; j < 12; ++j)
            s_.fnum[static_cast<size_t>(r)][static_cast<size_t>(j)] = w(static_cast<uint16_t>(kDsFnum + 24 * r + 2 * j));
    for (int sl = 0; sl < kSlots; ++sl) {
        for (int p = 0; p < kParams; ++p)
            s_.slot[static_cast<size_t>(sl)][static_cast<size_t>(p)] = b(static_cast<uint16_t>(kDsSlotParams + kParams * sl + p));
        s_.slotVolume[static_cast<size_t>(sl)] = b(static_cast<uint16_t>(kDsSlotVolume + sl));
    }
    s_.waveSelect = w(kDsWaveSelect);
    for (int i = 0; i < kChannels; ++i) s_.transpose[static_cast<size_t>(i)] = static_cast<int8_t>(b(static_cast<uint16_t>(kDsTranspose + i)));
    s_.pitchRangeStep = w(kDsPitchRangeStep);
    rewriteRegisters();
}

void AdLibDriver::saveTo(std::span<uint8_t> dg, uint16_t dsSegment) const {
    if (dg.size() < kDsStateEnd) return;
    const auto b = [&](uint16_t off, uint8_t v) { dg[off] = v; };
    const auto w = [&](uint16_t off, uint16_t v) {
        dg[off] = static_cast<uint8_t>(v);
        dg[static_cast<size_t>(off) + 1] = static_cast<uint8_t>(v >> 8);
    };
    for (int i = 0; i < kPitches; ++i) {
        b(static_cast<uint16_t>(kDsBlockOf + i), s_.blockOf[static_cast<size_t>(i)]);
        b(static_cast<uint16_t>(kDsNoteOf + i), s_.noteOf[static_cast<size_t>(i)]);
    }
    b(kDsAmDepth, s_.amDepth);
    for (int i = 0; i < kChannels; ++i) b(static_cast<uint16_t>(kDsUsed + i), s_.used[static_cast<size_t>(i)]);
    b(kDsNoteSel, s_.noteSel);
    b(kDsVibDepth, s_.vibDepth);
    for (int i = 0; i < kVoiceEntries; ++i) {
        const auto c = static_cast<size_t>(i);
        b(static_cast<uint16_t>(kDsNotePitch + i), s_.notePitch[c]);
        b(static_cast<uint16_t>(kDsKeyOn + i), s_.keyOn[c]);
        w(static_cast<uint16_t>(kDsFnumPtr + 4 * i), static_cast<uint16_t>(kDsFnum + 24 * s_.fnumRow[c]));
        w(static_cast<uint16_t>(kDsFnumPtr + 4 * i + 2), dsSegment);
        w(static_cast<uint16_t>(kDsHalfToneOffset + 2 * i), static_cast<uint16_t>(s_.halfToneOffset[c]));
    }
    w(kDsPitchRange, s_.pitchRange);
    for (int r = 0; r < kPitchSteps; ++r)
        for (int j = 0; j < 12; ++j)
            w(static_cast<uint16_t>(kDsFnum + 24 * r + 2 * j), s_.fnum[static_cast<size_t>(r)][static_cast<size_t>(j)]);
    for (int sl = 0; sl < kSlots; ++sl) {
        for (int p = 0; p < kParams; ++p)
            b(static_cast<uint16_t>(kDsSlotParams + kParams * sl + p), s_.slot[static_cast<size_t>(sl)][static_cast<size_t>(p)]);
        b(static_cast<uint16_t>(kDsSlotVolume + sl), s_.slotVolume[static_cast<size_t>(sl)]);
    }
    w(kDsWaveSelect, s_.waveSelect);
    for (int i = 0; i < kChannels; ++i) b(static_cast<uint16_t>(kDsTranspose + i), static_cast<uint8_t>(s_.transpose[static_cast<size_t>(i)]));
    w(kDsPitchRangeStep, s_.pitchRangeStep);
}

void AdLibDriver::rewriteRegisters() {
    write(0x01, static_cast<uint8_t>(s_.waveSelect));
    writeAmVibRhythm();
    writeNoteSel();
    for (int ch = 0; ch < kChannels; ++ch) {
        const auto c = static_cast<size_t>(ch);
        if (s_.used[c]) {
            writeAllParams(slotOf(ch, 0));
            writeAllParams(slotOf(ch, 1));
        }
        // A channel's 0xA0 / 0xB0 were last written by AdLib_SetFreq (its pitch and key bit are
        // kept), or never after AdLib_Init's key off (pitch 0 and no key: both registers 0; no tune
        // plays pitch 0).
        if (s_.notePitch[c] == 0 && s_.keyOn[c] == 0) {
            write(static_cast<uint8_t>(0xB0 + ch), 0);
            write(static_cast<uint8_t>(0xA0 + ch), 0);
        } else {
            setFreq(ch, static_cast<int8_t>(s_.notePitch[c]), s_.keyOn[c] != 0);
        }
    }
}

}  // namespace tmport::dos
