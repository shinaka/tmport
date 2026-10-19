#include "dos/audio/sequencer.h"

namespace tmport::dos {

namespace {

// A stream whose events never yield a duration would keep Sound_NextEvent looping inside the
// timer interrupt (the original hangs). The engine cuts such a voice after this many events.
constexpr int kMaxEventsPerFetch = 4096;

}  // namespace

// 491b:067b Sound_Init (after Sound_LoadData has filled the buffers)
Sequencer::Sequencer(const SoundBank& bank, SoundPorts& ports)
    : bank_(bank), tables_(*bank.tables), ports_(ports), device_(bank.device) {
    switch (device_) {
        case SoundCard::Speaker: voices_ = 1; break;
        case SoundCard::Tandy:
            voices_ = 4;  // port DS:656c = 0xC0; Speaker_On sets port 0x61 bits 5-6 (PSG to the speaker)
            break;
        case SoundCard::AdLib:
            voices_ = 4;
            ports_.oplInit();  // 4ac9:00b7 AdLib_Init
            break;
    }
}

// --------------------------------------------------------------------------- game entry points

// 491b:06d7 Sfx_Play
void Sequencer::sfxPlay(int idArg) {
    if (!soundOn_) return;
    if (device_ == SoundCard::AdLib) voices_ = 1;  // effects play on the speaker
    const auto id = static_cast<uint16_t>(idArg);
    if (id > sfx_.id) return;  // a lower (or equal) id is playing: it keeps priority (unsigned; idle = 0xFFFF)
    sfx_.id = id;
    int shift = 1;  // header bytes per entry: 2 (.IBM) or 8 (.TAN)
    if (device_ == SoundCard::Tandy) {
        sfxStop();  // also sets the id back to 0xFFFF: on the Tandy any later effect replaces this one
        shift = 3;
    }
    // The block is not cleared: tempo, envelopes and sustain of the previous effect remain until
    // the new streams set them.
    const auto entry = static_cast<uint16_t>(id << shift);
    for (int v = 0; v < voices_; ++v) {
        const uint16_t off = bank_.sfx.headerSlot(entry / 2 + v);
        sfx_.ptr[v] = sfx_.start[v] = off;
    }
    sfx_.status = 1;
}

// 491b:074b Sfx_Stop
void Sequencer::sfxStop() {
    sfx_.status = 0;
    sfx_.id = kIdle;
    if (device_ != SoundCard::Tandy) {
        ports_.speakerEnable(false);  // port 0x61 &= 0xFC, directly (also on AdLib: effects use the speaker)
        sfx_.active[0] = 0;
        return;
    }
    for (int v = 0; v < voices_; ++v) {
        sfx_.active[v] = 0;
        owned_[v] = 0;
        voiceOff(v);
    }
}

// 491b:0791 Music_Play
void Sequencer::musicPlay(int tune, int adlibVariant) {
    music_ = Block{};  // the whole block is zeroed (REP STOSW over 0x78 bytes)
    music_.id = static_cast<uint16_t>(tune);
    if (!musicOn_) return;  // music off: the block stays cleared, i.e. stopped
    if (device_ == SoundCard::AdLib) {
        ports_.oplPlayTune(tune, adlibVariant);  // 4ac9:0002: orchestration 4 * tune + (rand() & 3)
        voices_ = 4;
    }
    const int shift = device_ == SoundCard::Speaker ? 1 : 3;
    const auto entry = static_cast<uint16_t>(static_cast<uint16_t>(tune) << shift);
    for (int v = 0; v < voices_; ++v) {
        const uint16_t off = bank_.music.headerSlot(entry / 2 + v);
        music_.ptr[v] = music_.start[v] = off;
    }
    music_.status = 1;
}

// 491b:0811 Music_Stop
void Sequencer::musicStop() {
    music_.status = 0;
    music_.id = kIdle;
    const int saved = voices_;
    if (device_ == SoundCard::AdLib) voices_ = 4;
    for (int v = 0; v < voices_; ++v) {
        music_.active[v] = 0;
        owned_[v] = 0;
        voiceOff(v);  // on the speaker this also silences a playing effect's current note
    }
    voices_ = saved;
}

// 491b:0854 Sound_SilenceAll
void Sequencer::silenceAll() {
    const int n = device_ == SoundCard::AdLib ? 4 : voices_;
    for (int v = 0; v < n; ++v) voiceOff(v);
}

// ------------------------------------------------------------------------------------ the tick

// 1000:0428 Timer_ISR, step 4
void Sequencer::timerTick() {
    if (((music_.status & musicGate_) | sfx_.status) != 0) soundTick();
}

// 491b:0004 Sound_Tick
void Sequencer::soundTick() {
    owned_.fill(0);
    if (sfx_.status != 0) {
        if (device_ == SoundCard::AdLib) {
            // 491b:002a: the effect runs on the speaker with one voice; the music goes on unless paused.
            device_ = SoundCard::Speaker;
            voices_ = 1;
            runBlock(sfx_, false);
            device_ = SoundCard::AdLib;
            if (musicGate_ == 0) return;
        } else {
            runBlock(sfx_, false);  // 491b:0058: the music is frozen while an effect plays
            return;
        }
    }
    if (music_.status == 0) return;
    if (device_ == SoundCard::AdLib)
        voices_ = 4;
    else
        owned_ = sfx_.active;  // 491b:008a: voices an effect still holds are left alone
    runBlock(music_, true);
}

// 491b:00be Sound_RunChannel
void Sequencer::runBlock(Block& b, bool music) {
    file_ = &fileOf(music);
    if (b.status != 2) restartVoices(b);
    stepVoices(b);
    // 491b:00e7: a block that stopped restarts in the same tick when its loop flag is set. The
    // original jumps back as long as the block keeps stopping; a stream that ends at once would
    // hang it, so the engine restarts once per tick.
    if (b.status == 0 && (music ? musicLoop_ : sfxLoop_)) {
        b.ptr = b.start;
        restartVoices(b);
        stepVoices(b);
        if (music) ++musicLoopCount_;
    }
    file_ = nullptr;
}

// 491b:0136 Sound_RestartVoices
void Sequencer::restartVoices(Block& b) {
    b.status = 2;
    b.tempoAdj = 0;
    for (int v = 0; v < voices_; ++v) {
        b.active[v] = 0;
        if (file_->byteAt(b.ptr[v]) == 0xFF) continue;  // voice unused in this entry
        b.active[v] = 2;
        b.ticksLeft[v] = 0;
        b.rawDur[v] = 1;
        b.legato[v] = 0;
        b.rest[v] = 0;
        b.tie[v] = 0;
        b.release[v] = 6;
    }
}

// 491b:0181 Sound_StepVoices
void Sequencer::stepVoices(Block& b) {
    for (int v = 0; v < voices_; ++v) {
        if (!b.active[v]) continue;
        if (b.ticksLeft[v] == 0) {
            nextEvent(b, v);
            if (!b.active[v]) continue;
        }
        --b.ticksLeft[v];
        // Release gap: the note goes off `release` ticks before its end (not on the Tandy, whose
        // volume pass does it, and not when legato).
        if (device_ != SoundCard::Tandy && b.legato[v] != 1 &&
            b.ticksLeft[v] == static_cast<uint16_t>(b.release[v] - 1))
            voiceOff(v);
    }
    if ((b.active[0] | b.active[1] | b.active[2] | b.active[3]) == 0)
        allVoicesDone(b);
    else if (device_ == SoundCard::Tandy)
        updateVolumes(b);
}

// 491b:01ee Sound_NextEvent: events until one takes time or the voice ends.
void Sequencer::nextEvent(Block& b, int v) {
    for (int n = 0;; ++n) {
        if (n == kMaxEventsPerFetch) {
            guardTripped_ = true;
            b.active[v] = 0;
            voiceOff(v);
            return;
        }
        const uint16_t p = b.ptr[v];
        const uint8_t b0 = file_->byteAt(p);
        const uint8_t b1 = file_->byteAt(static_cast<uint16_t>(p + 1));
        const auto oct = static_cast<uint8_t>(b0 >> 4);
        const auto note = static_cast<uint8_t>(b0 & 0x0F);
        switch (note) {
            case 0x0:  // rest
                voiceOff(v);
                noteLength(b, v, b1);
                b.rest[v] = 1;
                break;
            case 0xD: control(b, v, oct, b1); break;  // command (no time)
            case 0xE: glide(b, v, oct, b1); break;    // raw pitch for rawdur ticks
            case 0xF:                                 // end of voice: the pointer stays on it
                b.active[v] = 0;
                voiceOff(v);
                return;
            default:  // semitone 1..12
                noteOn(b, v, oct, note);
                noteLength(b, v, b1);
                b.rest[v] = 0;
                break;
        }
        b.ptr[v] = static_cast<uint16_t>(p + 2);
        if (b.ticksLeft[v] != 0) return;
    }
}

// 491b:025d Sound_NoteOn
void Sequencer::noteOn(Block& /*b*/, int v, uint8_t oct, uint8_t note) {
    switch (device_) {
        case SoundCard::Tandy: {
            if (v == 3) {  // noise voice: noise control 0..7 (+8 ignored by the chip)
                psgOut(static_cast<uint8_t>(note - 1 + 0xE0));
                return;
            }
            // Octaves 0 and 1 give the same period; a period above the chip's 10 bits is halved.
            uint16_t n = static_cast<uint16_t>(tables_.psgPeriod(note - 1) >> (oct ? oct - 1 : 0));
            if (static_cast<int16_t>(n) > 0x3FF) n = static_cast<uint16_t>(n >> 1);
            setFrequency(v, n);
            return;
        }
        case SoundCard::AdLib:
            setFrequency(v, static_cast<uint8_t>(note - 1 + 12 * oct));  // semitone, 8 bits
            speakerOn(v);
            return;
        case SoundCard::Speaker:
            setFrequency(v, static_cast<uint16_t>(tables_.speakerPeriod(note - 1) >> oct));
            speakerOn(v);
            return;
    }
}

// 491b:02d8 Sound_NoteLength: duration code -> ticks (tempo = ticks per whole note).
void Sequencer::noteLength(Block& b, int v, uint8_t code) {
    auto d = static_cast<uint16_t>(b.tempo + b.tempoAdj);
    if (!(code & 0x80)) {  // bit 7: the whole tempo, no tie bookkeeping
        d = static_cast<uint16_t>(d >> (code & 7));
        if (code & 0x08) d = static_cast<uint16_t>(d + (d >> 1));  // dotted
        // Tie state: bit 4 set 0->1, 1->3, 2->3, 3 stays; clear: 0 stays, else +1.
        if (code & 0x10) {
            if (b.tie[v] != 3) b.tie[v] = b.tie[v] == 1 ? uint16_t{3} : static_cast<uint16_t>(b.tie[v] + 1);
        } else if (b.tie[v] != 0) {
            ++b.tie[v];
        }
    }
    b.ticksLeft[v] = d;
    b.newNote[v] = 1;
}

// 491b:033d Tandy_UpdateVolumes: one attenuation write per voice and tick.
void Sequencer::updateVolumes(Block& b) {
    for (int v = 0; v < kVoices; ++v) {
        const auto latch = static_cast<uint8_t>(0x90 + 0x20 * v);
        if (owned_[v] || !b.active[v]) continue;
        if (b.newNote[v] == 1) {
            b.newNote[v] = 0;
            if (static_cast<int16_t>(b.tie[v]) < 2) {  // restart the envelope
                b.envDone[v] = 0;
                b.envPos[v] = b.envStart[v];
            } else if (b.tie[v] != 3) {  // a tied-to note keeps the running envelope
                b.tie[v] = 0;
            }
        } else if (b.rest[v] == 1) {
            psgOut(static_cast<uint8_t>(latch + 0x0F));
            continue;
        } else if (!(b.tie[v] == 1 || b.tie[v] == 3 || b.legato[v] == 1) &&
                   static_cast<int16_t>(b.ticksLeft[v]) <= static_cast<int16_t>(b.release[v])) {
            psgOut(static_cast<uint8_t>(latch + 0x0F));  // release
            continue;
        }
        // 491b:03b8 envelope step (the value is added to the latch without masking)
        if (b.envDone[v] == 0) {
            const uint8_t a = tables_.ds8(b.envPos[v]);
            b.envPos[v] = static_cast<uint16_t>(b.envPos[v] + 1);
            if (a == 0xFF) {
                b.envDone[v] = 1;  // this tick writes nothing
                continue;
            }
            psgOut(static_cast<uint8_t>(a + latch));
        } else if (static_cast<int16_t>(b.envDone[v]) <= 1) {
            psgOut(static_cast<uint8_t>(b.sustain[v] + latch));
        } else {
            psgOut(static_cast<uint8_t>(latch + 0x0F));
        }
    }
}

// 491b:03fe Sound_Control (491b:0446..04c3)
void Sequencer::control(Block& b, int v, uint8_t cmd, uint8_t value) {
    switch (cmd) {
        case 0:  // release gap in ticks; 0 = legato
            if (value == 0) {
                b.legato[v] = 1;
            } else {
                b.legato[v] = 0;
                b.release[v] = value;
            }
            break;
        case 1:
        case 2: {  // tempo +/- value %, rounded up when the remainder is above 49
            if (value == 0) {
                b.tempoAdj = 0;
                break;
            }
            const uint32_t prod = static_cast<uint32_t>(b.tempo) * value;
            auto q = static_cast<uint16_t>(prod / 100);
            if (static_cast<int16_t>(prod % 100) > 0x31) ++q;
            b.tempoAdj = cmd == 1 ? q : static_cast<uint16_t>(-q);
            break;
        }
        case 3: b.rawDur[v] = value; break;
        case 4: b.tempo = static_cast<uint16_t>(value * 4); break;
        case 5: b.envStart[v] = tables_.ds16(static_cast<uint16_t>(SoundTables::kDsEnvelopes + 2 * value)); break;
        case 6: b.sustain[v] = value; break;
        default: break;  // 7..15 ignored
    }
}

// 491b:04ce Sound_Glide: raw-pitch event, rawdur ticks long (sweeps and noise bursts in effects).
void Sequencer::glide(Block& b, int v, uint8_t oct, uint8_t value) {
    if (value == 0) {
        voiceOff(v);
        b.rest[v] = 1;
    } else if (device_ == SoundCard::Tandy) {
        auto k = static_cast<uint16_t>(((value * 3) >> 2) + 1);
        const uint8_t c = oct ? oct : 1;
        auto n = static_cast<uint16_t>(0xD70 >> c);
        if (c < 4)
            k = static_cast<uint16_t>(k << (4 - c));
        else if (c > 4)
            k = static_cast<uint16_t>(k >> (c - 4));
        n = static_cast<uint16_t>(n - k);
        if (static_cast<int16_t>(n) > 0x400) n = static_cast<uint16_t>(n >> 1);
        setFrequency(v, n);
        b.newNote[v] = 1;
        b.rest[v] = 0;
    } else if (device_ == SoundCard::AdLib) {
        b.newNote[v] = 1;  // no sound on the OPL
    } else {
        // divisor = (first word of the device's period table - 128 * value) >> octave, 16-bit
        const uint16_t table = tables_.ds16(static_cast<uint16_t>(SoundTables::kDsPeriodTables + 2 * static_cast<int>(device_)));
        const auto base = static_cast<uint16_t>(tables_.ds16(table) - (value << 7));
        setFrequency(v, static_cast<uint16_t>(base >> oct));
        speakerOn(v);
    }
    b.ticksLeft[v] = b.rawDur[v];
}

// 491b:0575 Speaker_On
void Sequencer::speakerOn(int v) {
    switch (device_) {
        case SoundCard::Tandy: return;  // port 0x61 |= 0x60: routes the PSG to the speaker, nothing to model
        case SoundCard::AdLib: ports_.oplNoteOn(v, static_cast<uint8_t>(adlibPitch_[v])); return;
        case SoundCard::Speaker:
            if (owned_[v] == 0) ports_.speakerEnable(true);  // port 0x61 |= 3
            return;
    }
}

// 491b:05b8 Sound_VoiceOff
void Sequencer::voiceOff(int v) {
    if (owned_[v]) return;
    switch (device_) {
        case SoundCard::Tandy:
            psgOut(static_cast<uint8_t>((0x90 + 0x20 * v) | 0x0F));  // attenuation 15
            setFrequency(v, 0);                                     // and period 0
            return;
        case SoundCard::AdLib: ports_.oplNoteOff(v); return;
        case SoundCard::Speaker: ports_.speakerEnable(false); return;  // port 0x61 &= 0xFC
    }
}

// 491b:0607 Sound_SetFrequency
void Sequencer::setFrequency(int v, uint16_t value) {
    if (owned_[v]) return;
    switch (device_) {
        case SoundCard::Tandy:  // latch with the low 4 bits, then (value >> 4) unmasked
            psgOut(static_cast<uint8_t>((0x80 + 0x20 * v) | (value & 0x0F)));
            psgOut(static_cast<uint8_t>(value >> 4));
            return;
        case SoundCard::AdLib: adlibPitch_[v] = value; return;
        case SoundCard::Speaker: ports_.speakerDivisor(value); return;
    }
}

// 491b:0656 Sound_AllVoicesDone
void Sequencer::allVoicesDone(Block& b) {
    b.status = 0;
    b.id = kIdle;
    for (int v = 0; v < voices_; ++v) {
        b.active[v] = 0;
        voiceOff(v);
    }
}

}  // namespace tmport::dos
