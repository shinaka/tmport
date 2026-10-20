#include "gg/audio/snd_driver.h"

#include <algorithm>
#include <format>

#include "gg/audio/bmf.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kDsVoiceOperators = dg::kOplVoiceOperators;  // u8[11][2] operator indices (FFh = none)
constexpr uint16_t kDsOperatorOffsets = dg::kOplOperatorOffsets; // u8[18] register offsets
constexpr uint16_t kDsPatchRegs = dg::kOplPatchRegs;            // 20h 40h 60h 80h E0h
constexpr uint16_t kDsSpeakerBase = dg::kSpkDivisors;           // DS:1816 (entries -2..13 from DS:1812)
constexpr int kRec0 = static_cast<int>(bmf::kChannels);

int channelRecord(int channel) { return kRec0 + 4 * channel; }

// An 8-bit SHL AH,CL on a 286 or later: the count is masked to 5 bits; 8 or more clears the byte.
uint8_t bit8(uint8_t count) {
    const unsigned c = count & 0x1F;
    return c < 8 ? static_cast<uint8_t>(1u << c) : 0;
}

}  // namespace

// ------------------------------------------------------------------------------------- tables

SndTables SndTables::fromDgroup(std::vector<uint8_t> dgroup) {
    if (dgroup.size() < 0x1AD4) throw FormatError("SSG.EXE: DGROUP too short for the sound driver's tables");
    dgroup.resize(0x10000, 0);
    return SndTables(std::move(dgroup));
}

SndTables SndTables::fromExe(const GgExe& exe) {
    const auto ds = exe.dgroup();
    SndTables t = fromDgroup(std::vector<uint8_t>(ds.begin(), ds.end()));
    // v1.0's tables (code_sound.md §2): channels per class, the voice and operator tables, the
    // patch registers, the note tables, C1's speaker divisor and the first F-number.
    const bool ok = t.classChannels(1) == 16 && t.classChannels(3) == 11 && t.ds8(kDsVoiceOperators + 1) == 3 &&
                    t.ds8(kDsOperatorOffsets + 17) == 0x15 && t.ds8(kDsPatchRegs) == 0x20 && t.ds8(kDsPatchRegs + 4) == 0xE0 &&
                    t.noteOctave(127) == 10 && t.noteInOctave(127) == 7 && t.ds16(kDsSpeakerBase) == 0x8E88 && t.fnumber(0, 0) == 343;
    if (!ok) throw FormatError("SSG.EXE: the sound driver's tables are not where Gizmos & Gadgets v1.0 has them");
    return t;
}

// -------------------------------------------------------------------------------- song access

uint8_t SndDriver::Song::rd8(int off) const {
    const auto p = static_cast<ptrdiff_t>(s) + off;
    if (p < 0 || static_cast<size_t>(p) >= size) {
        log::debug("gg sound: read outside the song memory (s{:+#x})", off);
        return 0;
    }
    return mem[p];
}

uint16_t SndDriver::Song::rd16(int off) const { return static_cast<uint16_t>(rd8(off) | (rd8(off + 1) << 8)); }

uint32_t SndDriver::Song::rd32(int off) const { return rd16(off) | (static_cast<uint32_t>(rd16(off + 2)) << 16); }

void SndDriver::Song::wr8(int off, uint8_t v) {
    const auto p = static_cast<ptrdiff_t>(s) + off;
    if (p < 0 || static_cast<size_t>(p) >= size) {
        log::debug("gg sound: write outside the song memory dropped (s{:+#x})", off);
        return;
    }
    mem[p] = v;
}

void SndDriver::Song::wr16(int off, uint16_t v) {
    wr8(off, static_cast<uint8_t>(v));
    wr8(off + 1, static_cast<uint8_t>(v >> 8));
}

void SndDriver::Song::wr32(int off, uint32_t v) {
    wr16(off, static_cast<uint16_t>(v));
    wr16(off + 2, static_cast<uint16_t>(v >> 16));
}

// ------------------------------------------------------------------------------------- driver

SndDriver::SndDriver(std::shared_ptr<const SndTables> tables, SndPorts& ports) : tables_(std::move(tables)), ports_(ports) {
    // The initial DGROUP values of the driver's state.
    const SndTables& t = *tables_;
    for (size_t r = 0; r < owner_.size(); ++r)
        for (size_t c = 0; c < owner_[r].size(); ++c) owner_[r][c] = t.ds16(static_cast<uint16_t>(dg::kSndChannelOwner + 32 * r + 2 * c));
    busy_ = t.ds16(dg::kSndBusy);
    for (size_t c = 0; c < 16; ++c) {
        spkChannelNote_[c] = t.ds8(static_cast<uint16_t>(dg::kSpkChannelNote + c));
        oplVoiceKey_[c] = t.ds8(static_cast<uint16_t>(dg::kOplVoiceKey + c));
        channelVolume_[c] = t.ds16(static_cast<uint16_t>(dg::kOplChannelVolume + 2 * c));
    }
    currentSlot_ = t.ds16(dg::kSndCurrentSlot);
    enabled_ = t.ds8(dg::kSndEnabled);
    for (size_t k = 0; k < classAvailable_.size(); ++k) classAvailable_[k] = t.ds8(static_cast<uint16_t>(dg::kSndClassAvailable + k));
    masterVolume_ = t.ds16(dg::kOplMasterVolume);
    for (size_t v = 0; v < b0Shadow_.size(); ++v) b0Shadow_[v] = t.ds8(static_cast<uint16_t>(dg::kOplB0Shadow + v));
    bdShadow_ = t.ds8(dg::kOplBdShadow);
}

SndDriver::CueId SndDriver::registerCue(std::function<void()> fn) {
    cues_.push_back(std::move(fn));
    return static_cast<CueId>(cues_.size());
}

void SndDriver::port61And(uint8_t mask) {
    port61_ &= mask;
    ports_.speakerPort61(port61_);
}

void SndDriver::port61Or(uint8_t bits) {
    port61_ |= bits;
    ports_.speakerPort61(port61_);
}

// 1bb4:000e Snd_GetSlotSong: CF for a slot >= 64 (unsigned) or a NULL segment.
SndDriver::Song* SndDriver::slotSong(int slot) {
    const auto u = static_cast<uint16_t>(slot);
    if (u >= kSlots || !slots_[u]) return nullptr;
    return &slots_[u];
}

const SndDriver::Song* SndDriver::slotSong(int slot) const { return const_cast<SndDriver*>(this)->slotSong(slot); }

const uint8_t* SndDriver::songBlock(int slot) const {
    const Song* s = slotSong(slot);
    return s ? s->mem + s->s : nullptr;
}

// 1bb4:002b Snd_SectionEntry: not for class 0; the section (AL) below the count.
bool SndDriver::sectionValid(const Song& s, int section) {
    return s.cls() != 0 && static_cast<uint8_t>(section) < s.rd8(bmf::kSectionCount);
}

// 1bb4:06bb Snd_ClassChannelInfo: the owner row of a class (class 0 shares class 1's).
uint16_t& SndDriver::owner(uint8_t cls, int channel) {
    const size_t row = cls == 0 ? 0 : static_cast<size_t>(cls - 1) & 3;
    return owner_[row][static_cast<size_t>(channel) & 15];
}

uint16_t SndDriver::channelOwner(int songClass, int channel) const {
    return const_cast<SndDriver*>(this)->owner(static_cast<uint8_t>(songClass), channel);
}

// 1bb4:0045 Snd_StartSection: delay 1, tick 0; the bends cleared (class 4: 4000h), the transpose
// and the start from the section entry; one delay byte skipped (three when it is FFh).
uint16_t SndDriver::startSection(Song& s, uint8_t section) {
    s.wr16(bmf::kDelay, 1);
    s.wr16(bmf::kTick, 0);
    if (s.cls() == 0) {
        s.wr16(bmf::kPosition, 0x11);
        return 0x11;
    }
    s.wr8(bmf::kSection, section);
    const uint16_t bend = s.cls() == bmf::kClassMpu ? 0x4000 : 0;
    for (int c = 0; c < bmf::kChannelCount; ++c) s.wr16(channelRecord(c) + 2, bend);
    const int entry = static_cast<int>(bmf::kSections) + 4 * section;
    s.wr8(bmf::kTranspose, s.rd8(entry + 3));
    auto si = s.rd16(entry);
    const uint8_t first = s.rd8(si++);
    if (first == 0xFF) si = static_cast<uint16_t>(si + 2);
    s.wr16(bmf::kPosition, si);
    return si;
}

// -------------------------------------------------------------------------------------- AdLib

// 1bb4:00de AdLib_InitChip (111 writes): rhythm mode with the drums off, all channels and
// operators cleared, then the wave-select enable.
void SndDriver::adlibInitChip() {
    bdShadow_ = 0x20;
    adlibWrite(0xBD, 0x20);
    adlibWrite(0x08, 0x00);
    for (uint8_t c = 0; c < 9; ++c) {
        adlibWrite(static_cast<uint8_t>(0xA0 + c), 0);
        adlibWrite(static_cast<uint8_t>(0xB0 + c), 0);
    }
    for (uint16_t i = 0; i < 18; ++i) {
        const uint8_t o = tables_->ds8(static_cast<uint16_t>(kDsOperatorOffsets + i));
        for (const int reg : {0xE0, 0x20, 0x40, 0x60, 0x80}) adlibWrite(static_cast<uint8_t>(reg + o), 0);
    }
    adlibWrite(0x01, 0x20);
}

// 1bb4:0142 AdLib_Detect: the timer test (status & E0h = 0 after the reset, C0h after timer 1
// ran out); `present` stands for the chip's answers.
void SndDriver::adlibDetect(bool present) {
    adlibWrite(0x04, 0x60);
    adlibWrite(0x04, 0x80);
    if (present) {
        adlibWrite(0x02, 0xFF);
        adlibWrite(0x04, 0x21);
        classAvailable_[3] = 1;
        adlibWrite(0x04, 0x60);
        adlibWrite(0x04, 0x80);
        adlibInitChip();
        return;
    }
    // A missing card reads FFh: the first test already fails.
    classAvailable_[3] = 0;
    adlibWrite(0x04, 0x60);
    adlibWrite(0x04, 0x80);
}

// 1bb4:1234 Snd_Init
uint16_t SndDriver::init(int device, bool adlibPresent) {
    for (int k = 0; k < kSlots; ++k) {
        active_[static_cast<size_t>(k)] = 0;
        slots_[static_cast<size_t>(k)] = {};
    }
    busy_ &= 0xFF00;
    if (device == kDeviceDetect) {
        adlibDetect(adlibPresent);
    } else if (device != kDeviceSpeaker && device != 8) {
        adlibDetect(adlibPresent);
        classAvailable_[3] = 1;  // forced: -SN and -SA play the FM banks whatever the detection said
    }
    return static_cast<uint16_t>(1 | (classAvailable_[2] << 1) | (classAvailable_[3] << 2) | (classAvailable_[4] << 3));
}

// 1bb4:0218 Opl_SetVolume: the carrier's 40h register from the velocity byte, scaled by the master
// and channel volumes (256 each: the value written is the velocity). Not gated by g_sndEnabled.
void SndDriver::oplSetVolume(uint8_t voice, uint8_t velocity) {
    voice &= 0x0F;
    uint16_t idx = static_cast<uint16_t>(2 * voice + 1);
    if (tables_->ds8(static_cast<uint16_t>(kDsVoiceOperators + idx)) == 0xFF) --idx;
    const uint8_t op = tables_->ds8(static_cast<uint16_t>(kDsVoiceOperators + idx));
    const auto reg = static_cast<uint8_t>(0x40 + tables_->ds8(static_cast<uint16_t>(kDsOperatorOffsets + op)));
    const auto a = static_cast<uint8_t>(0x3F - (velocity & 0x3F));
    const auto a1 = static_cast<uint8_t>(static_cast<uint16_t>(a * masterVolume_ + 0x40) >> 8);
    const auto a2 = static_cast<uint8_t>(static_cast<uint16_t>(a1 * channelVolume_[voice] + 0x40) >> 8);
    adlibWrite(reg, static_cast<uint8_t>((0x3F - a2) | (velocity & 0xC0)));
}

// 1bb4:0270 Opl_SetPatch: patch p of the song (offset table at s + 54h + 4n), bytes 0..4 to the
// first operator's 20h/40h/60h/80h/E0h, byte 5 to C0h + channel (voices 0..6, 8 -> C8h, 10 -> C7h;
// none for 7 and 9), bytes 6..10 to the second operator (voices 0..6). Not gated by g_sndEnabled.
void SndDriver::oplSetPatch(Song& s, uint8_t program, uint8_t voice) {
    const int table = static_cast<uint16_t>(s.rd8(bmf::kSectionCount) * 4 + 0x54 + 2 * program);
    int p = s.rd16(table);
    voice &= 0x0F;
    const uint8_t op1 = tables_->ds8(static_cast<uint16_t>(kDsVoiceOperators + 2 * voice));
    const uint8_t op2 = tables_->ds8(static_cast<uint16_t>(kDsVoiceOperators + 2 * voice + 1));
    const uint8_t off1 = tables_->ds8(static_cast<uint16_t>(kDsOperatorOffsets + op1));
    for (uint16_t k = 0; k < 5; ++k)
        adlibWrite(static_cast<uint8_t>(tables_->ds8(static_cast<uint16_t>(kDsPatchRegs + k)) + off1), s.rd8(p++));
    if (voice != 7 && voice != 9) {
        const uint8_t ch = voice < 7 ? voice : voice > 9 ? 7 : 8;
        adlibWrite(static_cast<uint8_t>(0xC0 + ch), s.rd8(p++));
    }
    if (op2 == 0xFF) return;
    const uint8_t off2 = tables_->ds8(static_cast<uint16_t>(kDsOperatorOffsets + op2));
    for (uint16_t k = 0; k < 5; ++k)
        adlibWrite(static_cast<uint8_t>(tables_->ds8(static_cast<uint16_t>(kDsPatchRegs + k)) + off2), s.rd8(p++));
}

// 1bb4:0302 Opl_NoteOff: melodic voices B0h without the key bit (from the shadow), drums their BD
// bit cleared (the shadow even while disabled).
void SndDriver::oplNoteOff(uint8_t voice) {
    if (voice < 6) {
        if (enabled_) adlibWrite(static_cast<uint8_t>(0xB0 + voice), static_cast<uint8_t>(b0Shadow_[voice] & 0xDF));
        return;
    }
    bdShadow_ = static_cast<uint8_t>(bdShadow_ & ~bit8(static_cast<uint8_t>(10 - voice)));
    if (enabled_) adlibWrite(0xBD, bdShadow_);
}

// 1bb4:0343 Opl_NoteOn: n = key + transpose + bend.hi - 12 (bit 7 -> 0, above 95 -> 95); F-number
// row = bend.lo; melodic voices keyed through B0h (no key-off first), drums through their BD bit.
void SndDriver::oplNoteOn(Song& s, int rec, uint8_t key, uint8_t voice) {
    const uint16_t bend = s.rd16(rec + 2);
    voice &= 0x0F;
    oplVoiceKey_[voice] = key;
    if (key >= 0x80) return;
    auto n = static_cast<uint8_t>(key + s.rd8(bmf::kTranspose) + (bend >> 8) - 12);
    if (n & 0x80) n = 0;
    else if (n > 0x5F) n = 0x5F;
    const uint8_t block = tables_->noteOctave(n);
    const uint16_t f = tables_->fnumber(static_cast<uint8_t>(bend), tables_->noteInOctave(n));
    const auto a0 = static_cast<uint8_t>(f);
    auto b0 = static_cast<uint8_t>((f >> 8) | static_cast<uint8_t>(block << 2));
    if (voice < 6) {
        b0 |= 0x20;
        if (enabled_) adlibWrite(static_cast<uint8_t>(0xA0 + voice), a0);
        b0Shadow_[voice] = b0;
        if (enabled_) adlibWrite(static_cast<uint8_t>(0xB0 + voice), b0);
        return;
    }
    const uint8_t ch = voice < 9 ? voice : voice == 9 ? 8 : 7;
    if (enabled_) adlibWrite(static_cast<uint8_t>(0xA0 + ch), a0);
    if (enabled_) adlibWrite(static_cast<uint8_t>(0xB0 + ch), b0);
    bdShadow_ |= bit8(static_cast<uint8_t>(10 - voice));
    if (enabled_) adlibWrite(0xBD, bdShadow_);
}

// 1bb4:0541 Spk_PitchBend (base table DS:1816): d = T[n % 12] >> (n / 12); a bend (2000h = two
// semitones) moves it towards T[i +- 2] by (difference * bend) >> 13.
uint16_t SndDriver::spkPitchBend(uint8_t note, uint16_t bend) const {
    const uint8_t oct = tables_->noteOctave(note);
    const auto at = static_cast<uint16_t>(kDsSpeakerBase + 2 * tables_->noteInOctave(note));
    const auto shift = static_cast<unsigned>(oct & 0x1F);
    const auto d = static_cast<uint16_t>(tables_->ds16(at) >> shift);
    if (bend == 0) return d;
    if (!(bend & 0x8000)) {
        const auto up = static_cast<uint16_t>(tables_->ds16(static_cast<uint16_t>(at + 4)) >> shift);
        const uint32_t prod = static_cast<uint32_t>(static_cast<uint16_t>(d - up)) * bend;
        return static_cast<uint16_t>(d - static_cast<uint16_t>(prod >> 13));
    }
    const auto down = static_cast<uint16_t>(tables_->ds16(static_cast<uint16_t>(at - 4)) >> shift);
    const uint32_t prod = static_cast<uint32_t>(static_cast<uint16_t>(down - d)) * (bend & 0x7FFF);
    return static_cast<uint16_t>(d + static_cast<uint16_t>(prod >> 13));
}

// 1bb4:0641 Snd_HwChannelOff: classes 0/1 the speaker gate (not gated by g_sndEnabled), class 3
// the voice's note-off. Classes 2 and 4 (PSG, MIDI) are dead.
void SndDriver::hwChannelOff(uint8_t channel, uint8_t cls) {
    if (cls < 2) port61And(0xFE);
    else if (cls == bmf::kClassOpl) oplNoteOff(channel);
}

// 1bb4:0674 Snd_HwSilenceAll: the speaker gate, then OPL voices 10..0 when an OPL is present.
void SndDriver::hwSilenceAll() {
    port61And(0xFE);
    if (classAvailable_[3] == 1)
        for (int v = 10; v >= 0; --v) oplNoteOff(static_cast<uint8_t>(v));
}

// 1bb4:06d9 Snd_ApplyChannelProgram: class 3 reloads the channel's patch (none for program FFh).
void SndDriver::applyChannelProgram(Song& s, int channel, uint8_t program) {
    if (s.cls() == bmf::kClassOpl && program != bmf::kNoProgram) oplSetPatch(s, program, static_cast<uint8_t>(channel));
}

// 1bb4:0eec Snd_StopOwnerChannel: bit 1 of the same channel record in the owner's song.
void SndDriver::stopOwnerChannel(uint8_t slot, int rec) {
    if (Song* o = slotSong(slot)) o->or8(rec, bmf::kChannelStopped);
}

// The take loop of Snd_Play 1bb4:1145 and Snd_SetPriority 1bb4:1068: every channel the song uses is
// taken when free or when its owner's priority is lower (unsigned); else it stays stopped.
void SndDriver::takeChannels(Song& s, int slot, uint8_t priority, bool resetProgram) {
    const uint8_t cls = s.cls();
    const int n = tables_->classChannels(cls);
    const auto mine = static_cast<uint16_t>((slot & 0xFF) | (priority << 8));
    for (int c = 0; c < n; ++c) {
        const int rec = channelRecord(c);
        if (s.rd8(rec) < 0x80) continue;
        uint16_t& o = owner(cls, c);
        if (o != kOwnerFree) {
            if (priority <= (o >> 8)) {
                s.or8(rec, bmf::kChannelStopped);  // equal priority does not steal
                continue;
            }
            stopOwnerChannel(static_cast<uint8_t>(o), rec);
            hwChannelOff(static_cast<uint8_t>(c), cls);
        }
        s.and8(rec, static_cast<uint8_t>(~bmf::kChannelStopped));
        o = mine;
        if (resetProgram) s.wr8(rec + 1, bmf::kNoProgram);
        applyChannelProgram(s, c, s.rd8(rec + 1));
    }
}

// 1bb4:077c Snd_ReleaseChannels: the song gives up its channels; each goes to the active song of
// the same class (0 and 1 count as one) with the highest priority (signed, >= ; slots 63..0, so
// the lowest slot wins a tie), which gets its patch back and plays on from its next note.
void SndDriver::releaseChannels(Song& s) {
    const uint8_t cls = s.cls();
    const int n = tables_->classChannels(cls);
    for (int c = 0; c < n; ++c) {
        const int rec = channelRecord(c);
        const uint8_t f = s.rd8(rec);
        if (!(f & bmf::kChannelUsed) || (f & bmf::kChannelStopped)) continue;
        owner(cls, c) = kOwnerReleased;
        s.or8(rec, bmf::kChannelStopped);
        hwChannelOff(static_cast<uint8_t>(c), cls);
    }
    for (int c = 0; c < n; ++c) {
        uint16_t& o = owner(cls, c);
        if (o != kOwnerReleased) continue;
        const int rec = channelRecord(c);
        for (int k = kSlots - 1; k >= 0; --k) {
            if (!active_[static_cast<size_t>(k)]) continue;
            const uint8_t p = priority_[static_cast<size_t>(k)];
            if (static_cast<int8_t>(p) < static_cast<int8_t>(o >> 8)) continue;
            const Song& t = slots_[static_cast<size_t>(k)];
            const uint8_t tc = t.cls();
            if (tc != cls && static_cast<uint8_t>(tc + cls - 1) != 0) continue;
            if (t.rd8(rec) < 0x80) continue;
            o = static_cast<uint16_t>((k | 0x80) | (p << 8));
        }
    }
    for (int c = 0; c < n; ++c) {
        uint16_t& o = owner(cls, c);
        const auto lo = static_cast<uint8_t>(o);
        if (lo == 0xFE) {
            o = kOwnerFree;
        } else if (lo != 0xFF && (lo & 0x80)) {
            const auto winner = static_cast<uint8_t>(lo & 0x7F);
            o = static_cast<uint16_t>((o & 0xFF00) | winner);
            Song& t = slots_[winner];
            const int rec = channelRecord(c);
            applyChannelProgram(t, c, t.rd8(rec + 1));
            t.and8(rec, static_cast<uint8_t>(~bmf::kChannelStopped));
        }
    }
}

// ---------------------------------------------------------------------------------- songs (§4)

// 1bb4:0e71 Snd_AllocSlot: the first free slot, if the song's class is available.
int SndDriver::allocSlot(std::span<uint8_t> memory, size_t blockOffset) {
    int slot = -1;
    for (int k = 0; k < kSlots && slot < 0; ++k)
        if (!slots_[static_cast<size_t>(k)]) slot = k;
    if (slot < 0) return kNoFreeSlot;
    const uint8_t cls = blockOffset < memory.size() ? memory[blockOffset] : 0xFF;
    // Classes above 4 would index past DS:1685 in the original; refused here.
    if (cls >= classAvailable_.size() || classAvailable_[cls] != 1) return kClassUnavailable;
    slots_[static_cast<size_t>(slot)] = {memory.data(), memory.size(), blockOffset};
    return slot;
}

// 1bb4:0ebc Snd_RegisterSong: 'BM' 'F' 1Ah, then the block at BMF + 10h.
int SndDriver::registerSong(std::span<uint8_t> memory, size_t bmfOffset) {
    if (bmfOffset + bmf::kHeaderSize > memory.size() ||
        !std::equal(bmf::kMagic.begin(), bmf::kMagic.end(), memory.begin() + static_cast<ptrdiff_t>(bmfOffset)))
        return kNotBmf;
    return allocSlot(memory, bmfOffset + bmf::kHeaderSize);
}

// 1bb4:0d8f Snd_Free
int SndDriver::freeSlot(int slot) {
    if (!slotSong(slot)) return kBadSlot;
    stop(slot);
    slots_[static_cast<uint16_t>(slot)] = {};
    return kOk;
}

// --------------------------------------------------------------------------------- the API

// 1bb4:10c1 Snd_Play: a playing song restarts its section (its sounding channels cut) and keeps
// its channels; a new one takes them by priority (§10.1).
int SndDriver::play(int slot, int section, int priority, int loop) {
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    s->wr8(bmf::kLoop, static_cast<uint8_t>(loop));
    if (s->cls() != 0) {
        if (!sectionValid(*s, section)) return kBadSection;
        s->wr8(bmf::kLoopSection, static_cast<uint8_t>(section));
    }
    const auto u = static_cast<uint16_t>(slot);
    if (active_[u]) {
        BusyGuard lock(*this);
        startSection(*s, static_cast<uint8_t>(section));
        const uint8_t cls = s->cls();
        const int n = tables_->classChannels(cls);
        for (int c = 0; c < n; ++c) {
            const uint8_t f = s->rd8(channelRecord(c));
            if ((f & bmf::kChannelUsed) && !(f & bmf::kChannelStopped)) hwChannelOff(static_cast<uint8_t>(c), cls);
        }
        return setPriority(slot, priority);
    }
    startSection(*s, static_cast<uint8_t>(section));
    takeChannels(*s, slot, static_cast<uint8_t>(priority), true);
    priority_[u] = static_cast<uint8_t>(priority);
    active_[u] = 1;
    return kOk;
}

// 1bb4:084a Snd_Stop
int SndDriver::stop(int slot) {
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    const auto u = static_cast<uint16_t>(slot);
    const bool was = active_[u] == 1;
    active_[u] = 0;
    if (was) {
        BusyGuard lock(*this);
        releaseChannels(*s);
    }
    return kOk;
}

// 1bb4:102c Snd_SetPriority: a higher priority takes channels (and re-takes its own, reloading
// their patches), a lower one releases them.
int SndDriver::setPriority(int slot, int priority) {
    BusyGuard lock(*this);
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    const auto u = static_cast<uint16_t>(slot);
    const uint8_t old = priority_[u];
    const auto p = static_cast<uint8_t>(priority);
    priority_[u] = p;
    if (!active_[u] || old == p) return kOk;
    if (old > p) releaseChannels(*s);
    else takeChannels(*s, slot, p, false);
    return kOk;
}

int SndDriver::getPriority(int slot) const {
    if (!slotSong(slot)) return kBadSlot;
    return priority_[static_cast<uint16_t>(slot)];
}

int SndDriver::isActive(int slot) const {
    if (!slotSong(slot)) return kBadSlot;
    return active_[static_cast<uint16_t>(slot)];
}

int SndDriver::setCue(int slot, uint16_t tick, CueId cue) {
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    s->wr16(bmf::kCueTick, tick);
    s->wr32(bmf::kCueCallback, cue);
    return kOk;
}

int SndDriver::getTick(int slot) const {
    const Song* s = slotSong(slot);
    return s ? s->rd16(bmf::kTick) : kBadSlot;
}

int SndDriver::setLoop(int slot, int loop) {
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    s->wr8(bmf::kLoop, static_cast<uint8_t>(loop));
    return kOk;
}

int SndDriver::getLoop(int slot) const {
    const Song* s = slotSong(slot);
    return s ? s->rd8(bmf::kLoop) : kBadSlot;
}

int SndDriver::setSection(int slot, int section) {
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    if (!sectionValid(*s, section)) return kBadSection;
    s->wr8(bmf::kSection, static_cast<uint8_t>(section));
    return kOk;
}

int SndDriver::getSection(int slot) const {
    const Song* s = slotSong(slot);
    return s ? s->rd8(bmf::kSection) : kBadSlot;
}

int SndDriver::setLoopSection(int slot, int section) {
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    if (!sectionValid(*s, section)) return kBadSection;
    s->wr8(bmf::kLoopSection, static_cast<uint8_t>(section));
    return kOk;
}

int SndDriver::getLoopSection(int slot) const {
    const Song* s = slotSong(slot);
    return s ? s->rd8(bmf::kLoopSection) : kBadSlot;
}

int SndDriver::getSectionTranspose(int slot, int section) const {
    const Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    if (!sectionValid(*s, section)) return kBadSection;
    return s->rd8(static_cast<int>(bmf::kSections) + 4 * static_cast<uint8_t>(section) + 3);
}

int SndDriver::getSectionNext(int slot, int section) const {
    const Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    if (!sectionValid(*s, section)) return kBadSection;
    return s->rd8(static_cast<int>(bmf::kSections) + 4 * static_cast<uint8_t>(section) + 2);
}

// 1bb4:0cf4: next = FFh (none) is accepted without the section check.
int SndDriver::setSectionNext(int slot, int next, int section) {
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    if (static_cast<uint16_t>(next) != 0xFF && !sectionValid(*s, next)) return kBadSection;
    if (!sectionValid(*s, section)) return kBadSection;
    s->wr8(static_cast<int>(bmf::kSections) + 4 * static_cast<uint8_t>(section) + 2, static_cast<uint8_t>(next));
    return kOk;
}

// 1bb4:0d26 Snd_SetTranspose: the entry's transpose, and the live one if the section is the
// current one (sounding notes keep their pitch). Class 4's all-notes-off is dead.
int SndDriver::setTranspose(int slot, int section, int transpose) {
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    if (!sectionValid(*s, section)) return kBadSection;
    const auto t = static_cast<uint8_t>(transpose);
    s->wr8(static_cast<int>(bmf::kSections) + 4 * static_cast<uint8_t>(section) + 3, t);
    if (static_cast<uint8_t>(section) == s->rd8(bmf::kSection)) s->wr8(bmf::kTranspose, t);
    return kOk;
}

// 1bb4:0f01 Snd_MuteChannel: flags |= 1, and the channel cut when the song plays it. The channel
// is not range-checked: the record offset 0Eh + 4c is taken as a 16-bit offset from s.
int SndDriver::muteChannel(int slot, int channel) {
    BusyGuard lock(*this);
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    const int rec = static_cast<int16_t>(static_cast<uint16_t>(channel * 4 + kRec0));
    const uint8_t f = s->rd8(rec);
    s->or8(rec, bmf::kChannelMuted);
    if (f >= 0x80 && !(f & (bmf::kChannelMuted | bmf::kChannelStopped))) hwChannelOff(static_cast<uint8_t>(channel), s->cls());
    return kOk;
}

// 1bb4:0f50 Snd_UnmuteChannel: nothing is re-keyed.
int SndDriver::unmuteChannel(int slot, int channel) {
    Song* s = slotSong(slot);
    if (!s) return kBadSlot;
    s->and8(static_cast<int16_t>(static_cast<uint16_t>(channel * 4 + kRec0)), static_cast<uint8_t>(~bmf::kChannelMuted));
    return kOk;
}

// ------------------------------------------------------------------------- pause, silence (§12)

void SndDriver::pauseLock() {
    if (busy_ != 0) return;
    busy_ = 1;
    hwSilenceAll();
}

void SndDriver::resumeUnlock() { busy_ = 0; }

void SndDriver::silence() {
    if (!enabled_) return;
    BusyGuard lock(*this);
    hwSilenceAll();
    enabled_ = 0;
}

void SndDriver::off() {
    BusyGuard lock(*this);
    for (int k = 0; k < kSlots; ++k) {
        if (!active_[static_cast<size_t>(k)]) continue;
        active_[static_cast<size_t>(k)] = 0;
        Song& s = slots_[static_cast<size_t>(k)];
        const int n = tables_->classChannels(s.cls());
        for (int c = 0; c < n; ++c) s.or8(channelRecord(c), bmf::kChannelStopped);
    }
    hwSilenceAll();
    for (auto& row : owner_) row.fill(kOwnerFree);
}

// ------------------------------------------------------------------------------ the sound tick

// 1bb4:0db3 Snd_Tick: every active slot in ascending order: tick counter + 1 (the cue runs when it
// equals the cue tick), delay - 1, the class interpreter when it reaches 0.
void SndDriver::soundTick() {
    ++ticks_;
    if ((busy_ & 0xFF) == 1) return;
    inTick_ = true;
    for (int k = 0; k < kSlots; ++k) {
        if (!active_[static_cast<size_t>(k)]) continue;
        currentSlot_ = static_cast<uint16_t>(k);
        Song& s = slots_[static_cast<size_t>(k)];
        const auto tick = static_cast<uint16_t>(s.rd16(bmf::kTick) + 1);
        s.wr16(bmf::kTick, tick);
        if (tick == s.rd16(bmf::kCueTick)) {
            const uint32_t cue = s.rd32(bmf::kCueCallback);
            if (cue >= 1 && cue <= cues_.size() && cues_[cue - 1]) cues_[cue - 1]();
            else if (cue != 0) log::warn("gg sound: song in slot {} has an unknown cue callback {:#x}", k, cue);
            // cue 0 (NULL) would be called by the original; only a song that never restarts its
            // section in 65536 ticks gets here.
        }
        const auto delay = static_cast<uint16_t>(s.rd16(bmf::kDelay) - 1);
        s.wr16(bmf::kDelay, delay);
        if (delay != 0) continue;
        switch (s.cls()) {
            case 0: stepSweep(s); break;
            case bmf::kClassSpeaker:
            case bmf::kClassMpu: stepSpeaker(s); break;
            case bmf::kClassOpl: stepOpl(s); break;
            default: break;  // class 2 (PSG) is never registered
        }
    }
    inTick_ = false;
}

// 1bb4:093f Snd_ReadEvent
SndDriver::Event SndDriver::readEvent(Song& s, uint16_t& si) {
    Event e;
    const uint16_t ax = s.rd16(si);
    si = static_cast<uint16_t>(si + 2);
    e.status = static_cast<uint8_t>(ax);
    e.data = static_cast<uint8_t>(ax >> 8);
    if (e.status < 0x80) {
        e.data = e.status;
        --si;
        e.status = s.rd8(bmf::kRunningStatus);
    }
    s.wr8(bmf::kRunningStatus, e.status);
    e.channel = static_cast<uint8_t>(e.status & 0x0F);
    e.rec = channelRecord(e.channel);
    return e;
}

// The delay byte after an event (09ce / 0b2f): 0 = the next event in this tick (false), FFh = a
// u16 delay; the delay and the position are stored.
bool SndDriver::readDelay(Song& s, uint16_t& si) {
    uint16_t d = s.rd8(si);
    si = static_cast<uint16_t>(si + 1);
    if (d == 0xFF) {
        d = s.rd16(si);
        si = static_cast<uint16_t>(si + 2);
    } else if (d == 0) {
        return false;
    }
    s.wr16(bmf::kDelay, d);
    s.wr16(bmf::kPosition, si);
    return true;
}

// 1bb4:08ef Snd_MetaEvent (FF len16 t v ...): the end of the current section starts the next
// section, or the loop section, in the same tick; else the song stops.
SndDriver::MetaResult SndDriver::metaEvent(Song& s, uint16_t& si) {
    --si;
    const uint16_t len = s.rd16(si);
    si = static_cast<uint16_t>(si + 2);
    const uint8_t t = s.rd8(si), v = s.rd8(si + 1);
    si = static_cast<uint16_t>(si + len);
    if (t != 1 || v != s.rd8(bmf::kSection)) return MetaResult::Delay;
    uint8_t next = s.rd8(static_cast<int>(bmf::kSections) + 4 * v + 2);
    if (next == bmf::kNoSection) {
        if (!s.rd8(bmf::kLoop)) {
            stop(currentSlot_);
            return MetaResult::Stopped;
        }
        next = s.rd8(bmf::kLoopSection);
    }
    si = startSection(s, next);
    return MetaResult::Continue;
}

// 1bb4:0966 Snd_StepSpeaker (class 1; DS:1aaa sends class 4 here too). One voice for every song:
// the last note on sets the pitch, any note off gates the speaker off.
void SndDriver::stepSpeaker(Song& s) {
    uint16_t si = s.rd16(bmf::kPosition);
    while (true) {
        const Event e = readEvent(s, si);
        const auto hi = static_cast<uint8_t>(e.status & 0xF0);
        const uint8_t flags = s.rd8(e.rec);
        const bool playing = !(flags & (bmf::kChannelMuted | bmf::kChannelStopped));
        if (hi == 0xE0) {  // bend: stored also when muted or stopped
            const auto bend = static_cast<uint16_t>(e.data | (s.rd8(si) << 8));
            si = static_cast<uint16_t>(si + 1);
            if (bend != s.rd16(e.rec + 2)) {
                s.wr16(e.rec + 2, bend);
                const uint8_t key = spkChannelNote_[e.channel];
                if ((s.rd8(e.rec) & 3) == 0 && key < 0x80) {
                    const uint16_t d = spkPitchBend(static_cast<uint8_t>(key + s.rd8(bmf::kTranspose)), bend);
                    if (enabled_) {
                        ports_.speakerDivisor(d);
                        port61Or(0x03);
                    }
                }
            }
        } else if (hi > 0xE0) {
            const MetaResult r = metaEvent(s, si);
            if (r == MetaResult::Stopped) return;
            if (r == MetaResult::Continue) continue;
        } else if (hi < 0x90) {  // note off (no data byte)
            --si;
            if (playing) {
                spkChannelNote_[e.channel] = hi;
                if (enabled_) port61And(0xFE);
            }
        } else if (playing) {  // 90h..DFh: note on
            spkChannelNote_[e.channel] = e.data;
            const uint16_t d = spkPitchBend(static_cast<uint8_t>(e.data + s.rd8(bmf::kTranspose)), s.rd16(e.rec + 2));
            if (enabled_) {
                ports_.speakerDivisor(d);
                port61Or(0x03);
            }
        }
        if (readDelay(s, si)) return;
    }
}

// 1bb4:0ad6 Snd_StepOpl (class 3): the channel is the OPL voice.
void SndDriver::stepOpl(Song& s) {
    uint16_t si = s.rd16(bmf::kPosition);
    while (true) {
        const Event e = readEvent(s, si);
        const auto playing = [&] { return (s.rd8(e.rec) & (bmf::kChannelMuted | bmf::kChannelStopped)) == 0; };
        if (e.status >= 0xF0) {
            const MetaResult r = metaEvent(s, si);
            if (r == MetaResult::Stopped) return;
            if (r == MetaResult::Continue) continue;
        } else if (e.status >= 0xE0) {  // bend: stored also when muted or stopped; re-keys the voice
            const auto bend = static_cast<uint16_t>(e.data | (s.rd8(si) << 8));
            si = static_cast<uint16_t>(si + 1);
            if (bend != s.rd16(e.rec + 2)) {
                s.wr16(e.rec + 2, bend);
                if (playing()) oplNoteOn(s, e.rec, oplVoiceKey_[e.channel], e.channel);
            }
        } else if (e.status >= 0xC0) {  // program: stored always, the patch loaded unless stopped
            if (e.data != s.rd8(e.rec + 1)) {
                s.wr8(e.rec + 1, e.data);
                if (!(s.rd8(e.rec) & bmf::kChannelStopped)) oplSetPatch(s, e.data, e.channel);
            }
        } else if (e.status >= 0x90) {  // note on: key, velocity
            const uint8_t velocity = s.rd8(si);
            si = static_cast<uint16_t>(si + 1);
            if (playing()) {
                oplSetVolume(e.channel, velocity);
                oplNoteOn(s, e.rec, e.data, e.channel);
            }
        } else {  // note off (no data byte)
            --si;
            if (playing()) {
                oplVoiceKey_[e.channel] = e.status;
                oplNoteOff(e.channel);
            }
        }
        if (readDelay(s, si)) return;
    }
}

// 1bb4:087e Snd_StepSpeakerSweep (class 0, no song uses it): words from s+11h: a divisor held for
// the delay at s+0Fh, 0 = gate off, 8000h..FFFEh = delay & 0Fh, FFFFh = the end.
void SndDriver::stepSweep(Song& s) {
    uint16_t si = s.rd16(bmf::kPosition);
    for (int guard = 0; guard < 0x10000; ++guard) {
        const uint16_t ax = s.rd16(si);
        si = static_cast<uint16_t>(si + 2);
        s.wr16(bmf::kPosition, si);
        if (ax & 0x8000) {
            if (ax != 0xFFFF) {
                s.wr16(0x0F, static_cast<uint16_t>(ax & 0x0F));
                continue;
            }
            if (!s.rd8(bmf::kLoop)) {
                stop(currentSlot_);
                return;
            }
            si = startSection(s, 0);
            continue;
        }
        const bool playing = (s.rd8(bmf::kChannels) & 3) == 0 && enabled_;
        if (ax == 0) {
            if (playing) port61And(0xFE);
        } else if (playing) {
            ports_.speakerDivisor(ax);
            port61Or(0x03);
        }
        s.wr16(bmf::kDelay, s.rd16(0x0F));
        return;
    }
}

}  // namespace tmport::gg
