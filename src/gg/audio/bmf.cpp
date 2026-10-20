#include "gg/audio/bmf.h"

#include <algorithm>
#include <bit>
#include <format>

#include "util/error.h"

namespace tmport::gg {

namespace {

[[noreturn]] void fail(const std::string& what) { throw FormatError("BMF: " + what); }

// Bounds-checked reads relative to the song block.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> s) : s_(s) {}
    uint8_t u8(size_t at) const {
        if (at >= s_.size()) fail(std::format("read at s+{:#x} past the end ({:#x} bytes)", at, s_.size()));
        return s_[at];
    }
    uint16_t u16(size_t at) const { return static_cast<uint16_t>(u8(at) | (u8(at + 1) << 8)); }
    size_t size() const { return s_.size(); }

private:
    std::span<const uint8_t> s_;
};

}  // namespace

uint16_t BmfSongInfo::usedChannelMask() const {
    uint16_t m = 0;
    for (int c = 0; c < bmf::kChannelCount; ++c)
        if (channels[static_cast<size_t>(c)].flags & bmf::kChannelUsed) m |= static_cast<uint16_t>(1u << c);
    return m;
}

int BmfSongInfo::usedChannelCount() const { return std::popcount(usedChannelMask()); }

size_t BmfSongInfo::countEvents(BmfEventKind kind) const {
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [kind](const BmfEvent& e) { return e.kind == kind; }));
}

bool isBmfSong(std::span<const uint8_t> payload) {
    return payload.size() >= bmf::kMagic.size() && std::equal(bmf::kMagic.begin(), bmf::kMagic.end(), payload.begin());
}

BmfSongInfo parseBmfSong(std::span<const uint8_t> payload) {
    if (!isBmfSong(payload)) fail("no 'BMF' 1Ah magic");
    if (payload.size() < bmf::kHeaderSize + bmf::kMinBlockSize) fail(std::format("{} bytes is too short", payload.size()));
    const Reader s(payload.subspan(bmf::kHeaderSize));
    BmfSongInfo g;
    g.blockSize = s.size();
    g.songClass = s.u8(bmf::kClass);
    if (g.songClass != bmf::kClassSpeaker && g.songClass != bmf::kClassOpl)
        fail(std::format("class {} has no interpreter in use (only 1 and 3)", g.songClass));
    for (size_t c = 0; c < g.channels.size(); ++c) {
        const size_t r = bmf::kChannels + bmf::kChannelRecordSize * c;
        g.channels[c] = {s.u8(r), s.u8(r + 1), s.u16(r + 2)};
    }
    g.loopSection = s.u8(bmf::kLoopSection);
    g.transposeField = s.u8(bmf::kTranspose);
    const size_t n = s.u8(bmf::kSectionCount);
    if (n == 0) fail("no section");
    for (size_t k = 0; k < n; ++k) {
        const size_t e = bmf::kSections + bmf::kSectionEntrySize * k;
        g.sections.push_back({s.u16(e), s.u8(e + 2), s.u8(e + 3)});
    }
    if (g.songClass == bmf::kClassOpl) {
        // Opl_SetPatch 1bb4:0270: the offset table at s + 54h + 4n, the count byte before it.
        const size_t table = bmf::kSections + bmf::kSectionEntrySize * n;
        const size_t m = s.u8(table);
        for (size_t p = 0; p < m; ++p) {
            const uint16_t off = s.u16(table + 1 + 2 * p);
            if (off + bmf::kPatchSize > s.size()) fail(std::format("patch {} at s+{:#x} past the end", p, off));
            std::array<uint8_t, bmf::kPatchSize> patch{};
            for (size_t b = 0; b < bmf::kPatchSize; ++b) patch[b] = s.u8(off + b);
            g.patchOffsets.push_back(off);
            g.patches.push_back(patch);
        }
    }

    const bool opl = g.songClass == bmf::kClassOpl;
    for (size_t si = 0; si < n; ++si) {
        // Snd_StartSection 1bb4:0045: skip the initial delay byte (and its word if it is FFh).
        size_t i = g.sections[si].start;
        if (s.u8(i++) == 0xFF) i += 2;
        uint32_t tick = 0;
        uint8_t run = s.u8(bmf::kRunningStatus);  // the driver keeps +51h across sections
        while (true) {
            if (i >= s.size()) fail(std::format("section {} runs past the end", si));
            BmfEvent ev;
            ev.section = static_cast<int>(si);
            ev.tick = tick;
            ev.offset = static_cast<uint16_t>(i);
            // Snd_ReadEvent 1bb4:093f: a status byte consumes 2 bytes (status, first data byte), a
            // data byte is running status and consumes 1.
            uint8_t st = s.u8(i);
            uint8_t data = 0;
            if (st >= 0x80) {
                data = s.u8(i + 1);
                i += 2;
            } else {
                data = st;
                st = run;
                ev.runningStatus = true;
                i += 1;
            }
            run = st;
            ev.status = st;
            ev.channel = static_cast<uint8_t>(st & 0x0F);
            const uint8_t hi = static_cast<uint8_t>(st & 0xF0);
            bool meta = false;
            if (opl) {  // Snd_StepOpl 1bb4:0ad6 compares the whole status
                if (st >= 0xF0) {
                    meta = true;
                } else if (st >= 0xE0) {
                    ev.kind = BmfEventKind::Bend;
                    ev.bend = static_cast<uint16_t>(data | (s.u8(i++) << 8));
                } else if (st >= 0xC0) {
                    ev.kind = BmfEventKind::Program;
                    ev.program = data;
                } else if (st >= 0x90) {
                    ev.kind = BmfEventKind::NoteOn;
                    ev.key = data;
                    ev.velocity = s.u8(i++);
                } else {
                    ev.kind = BmfEventKind::NoteOff;
                    --i;  // no data byte
                }
            } else {  // Snd_StepSpeaker 1bb4:0966 compares status & F0h
                if (hi == 0xE0) {
                    ev.kind = BmfEventKind::Bend;
                    ev.bend = static_cast<uint16_t>(data | (s.u8(i++) << 8));
                } else if (hi > 0xE0) {
                    meta = true;
                } else if (hi < 0x90) {
                    ev.kind = BmfEventKind::NoteOff;
                    --i;
                } else {  // 90h..DFh: a note on (a program change would play a note)
                    ev.kind = BmfEventKind::NoteOn;
                    ev.key = data;
                }
            }
            if (meta) {
                // Snd_MetaEvent 1bb4:08ef: back to the length word; t and v are the first payload bytes.
                --i;
                ev.kind = BmfEventKind::Meta;
                ev.metaLength = s.u16(i);
                i += 2;
                ev.metaType = s.u8(i);
                ev.metaValue = s.u8(i + 1);
                i += ev.metaLength;
                if (ev.metaType == 1 && ev.metaValue == si) {
                    ev.endOfSection = true;
                    g.events.push_back(ev);
                    break;
                }
            }
            uint32_t d = s.u8(i++);
            if (d == 0xFF) {
                d = s.u16(i);
                i += 2;
            }
            ev.delay = d;
            g.events.push_back(ev);
            tick += d;
        }
        g.end = std::max(g.end, i);
        g.ticks = std::max(g.ticks, tick);
    }
    return g;
}

std::string describeBmfEvent(const BmfEvent& e, uint8_t songClass) {
    std::string t;
    switch (e.kind) {
        case BmfEventKind::NoteOff: t = std::format("off   ch{:<2}", e.channel); break;
        case BmfEventKind::NoteOn:
            t = std::format("on    ch{:<2} key {:3}", e.channel, e.key);
            if (songClass == bmf::kClassOpl) t += std::format(" vel {:3}", e.velocity);
            break;
        case BmfEventKind::Program: t = std::format("prog  ch{:<2} {}", e.channel, e.program); break;
        case BmfEventKind::Bend: t = std::format("bend  ch{:<2} {:#06x}", e.channel, e.bend); break;
        case BmfEventKind::Meta: t = std::format("meta  {:02x} len {}: {:02x} {:02x}", e.status, e.metaLength, e.metaType, e.metaValue); break;
    }
    return std::format("  {:6}  @{:04x}  {}", e.tick, e.offset, t);
}

}  // namespace tmport::gg
