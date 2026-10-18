#include "audio/smf.h"

#include <algorithm>
#include <cstring>
#include <format>

#include "util/error.h"

namespace tmport::audio {

namespace {

uint32_t be32(const std::vector<uint8_t>& b, size_t p) {
    return (static_cast<uint32_t>(b[p]) << 24) | (static_cast<uint32_t>(b[p + 1]) << 16) |
           (static_cast<uint32_t>(b[p + 2]) << 8) | b[p + 3];
}
uint16_t be16(const std::vector<uint8_t>& b, size_t p) { return static_cast<uint16_t>((b[p] << 8) | b[p + 1]); }

class TrackParser {
public:
    TrackParser(MidiSong& song, size_t begin, size_t end, uint16_t track)
        : s_(song), b_(song.bytes), p_(begin), end_(end), track_(track) {}

    MidiTrackInfo run(std::vector<MidiEvent>& out) {
        MidiTrackInfo info;
        uint64_t tick = 0;
        uint8_t running = 0;
        while (p_ < end_) {
            tick += vlq();
            if (tick > 0x7FFFFFFF) fail("delta times overflow");
            if (p_ >= end_) {
                warn("ends inside an event");
                break;
            }
            MidiEvent e;
            e.tick = static_cast<uint32_t>(tick);
            e.track = track_;
            const uint8_t st = b_[p_];
            if (st == 0xFF) {  // meta event: FF type len data
                if (end_ - p_ < 2) {
                    warn("ends inside a meta event");
                    break;
                }
                e.status = 0xFF;
                e.data1 = b_[p_ + 1];
                p_ += 2;
                payload(e);
                out.push_back(e);
                ++info.eventCount;
                if (e.data1 == meta::kEndOfTrack) {
                    info.hasEndOfTrack = true;
                    break;
                }
                continue;
            }
            if (st == 0xF0 || st == 0xF7) {  // SysEx (or escape): F0/F7 len data; skipped by playback
                e.status = st;
                ++p_;
                payload(e);
                out.push_back(e);
                ++info.eventCount;
                running = 0;
                continue;
            }
            if (st & 0x80) {
                if (st >= 0xF0) fail(std::format("unexpected status byte {:#04x}", st));
                running = st;
                ++p_;
            } else if (running == 0) {
                fail(std::format("data byte {:#04x} without running status", st));
            }
            e.status = running;
            const uint8_t cmd = running & 0xF0;
            const size_t n = (cmd == 0xC0 || cmd == 0xD0) ? 1 : 2;
            if (end_ - p_ < n) {
                warn("ends inside a channel message");
                break;
            }
            e.data1 = b_[p_];
            e.data2 = n == 2 ? b_[p_ + 1] : 0;
            if ((e.data1 | e.data2) & 0x80) fail("channel message with a data byte >= 0x80");
            p_ += n;
            out.push_back(e);
            ++info.eventCount;
        }
        info.endTick = static_cast<uint32_t>(tick);
        if (!info.hasEndOfTrack) warn("has no end-of-track event");
        return info;
    }

private:
    uint32_t vlq() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            if (p_ >= end_) fail("ends inside a variable-length number");
            const uint8_t c = b_[p_++];
            v = (v << 7) | (c & 0x7F);
            if (!(c & 0x80)) return v;
        }
        fail("variable-length number longer than 4 bytes");
    }
    void payload(MidiEvent& e) {
        uint32_t len = vlq();
        if (len > end_ - p_) {
            warn("event data runs past the end of the track");
            len = static_cast<uint32_t>(end_ - p_);
        }
        e.dataOffset = static_cast<uint32_t>(p_);
        e.dataLength = len;
        p_ += len;
    }
    [[noreturn]] void fail(const std::string& what) const {
        throw FormatError(std::format("MIDI file: track {}: {} (offset {})", track_, what, p_));
    }
    void warn(const std::string& what) { s_.warnings.push_back(std::format("track {} {}", track_, what)); }

    MidiSong& s_;
    const std::vector<uint8_t>& b_;
    size_t p_;
    size_t end_;
    uint16_t track_;
};

}  // namespace

uint64_t MidiSong::tickToUs(uint32_t tick) const {
    if (ppqn == 0) {
        if (smpteFps <= 0 || smpteTicksPerFrame <= 0) return 0;
        // 29 means 29.97 frames per second (30000/1001).
        if (smpteFps == 29) return static_cast<uint64_t>(tick) * 1'001'000'000ull / (30000ull * smpteTicksPerFrame);
        return static_cast<uint64_t>(tick) * 1'000'000ull / (static_cast<uint64_t>(smpteFps) * smpteTicksPerFrame);
    }
    auto it = std::upper_bound(tempoMap.begin(), tempoMap.end(), tick,
                               [](uint32_t t, const TempoSegment& s) { return t < s.tick; });
    const TempoSegment& seg = it == tempoMap.begin() ? tempoMap.front() : *(it - 1);
    return seg.us + static_cast<uint64_t>(tick - seg.tick) * seg.usPerQuarter / ppqn;
}

size_t MidiSong::channelEventCount() const {
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [](const MidiEvent& e) { return e.isChannel(); }));
}

size_t MidiSong::noteOnCount() const {
    return static_cast<size_t>(std::count_if(events.begin(), events.end(), [](const MidiEvent& e) { return e.isNoteOn(); }));
}

uint32_t MidiSong::lastNoteOnTick() const {
    uint32_t t = 0;
    for (const auto& e : events)
        if (e.isNoteOn()) t = e.tick;
    return t;
}

MidiSong parseSmf(std::vector<uint8_t> bytes) {
    MidiSong s;
    s.bytes = std::move(bytes);
    const auto& b = s.bytes;
    if (b.size() < 14 || std::memcmp(b.data(), "MThd", 4) != 0)
        throw FormatError("not a Standard MIDI File (no MThd header)");
    const uint32_t hdrLen = be32(b, 4);
    if (hdrLen < 6 || hdrLen > b.size() - 8) throw FormatError("MIDI file: bad header length");
    s.format = be16(b, 8);
    s.trackCount = be16(b, 10);
    s.division = be16(b, 12);
    if (s.format > 2) throw FormatError(std::format("MIDI file: unsupported format {}", s.format));
    if (s.format == 2) s.warnings.push_back("format 2 (independent sequences) is played like format 1");
    if (s.division & 0x8000) {
        s.smpteFps = -static_cast<int>(static_cast<int8_t>(s.division >> 8));
        s.smpteTicksPerFrame = s.division & 0xFF;
        if ((s.smpteFps != 24 && s.smpteFps != 25 && s.smpteFps != 29 && s.smpteFps != 30) || s.smpteTicksPerFrame == 0)
            throw FormatError(std::format("MIDI file: bad SMPTE division {:#06x}", s.division));
    } else {
        s.ppqn = s.division;
        if (s.ppqn == 0) throw FormatError("MIDI file: zero ticks per quarter note");
    }

    std::vector<MidiEvent> events;
    size_t pos = 8 + static_cast<size_t>(hdrLen);
    uint16_t found = 0;
    while (found < s.trackCount && b.size() - pos >= 8) {
        size_t len = be32(b, pos + 4);
        const size_t start = pos + 8;
        if (len > b.size() - start) {
            s.warnings.push_back(std::format("chunk at offset {} runs past the end of the file", pos));
            len = b.size() - start;
        }
        if (std::memcmp(b.data() + pos, "MTrk", 4) == 0) {
            TrackParser tp(s, start, start + len, found);
            s.tracks.push_back(tp.run(events));
            ++found;
        }
        pos = start + len;
    }
    if (found == 0) throw FormatError("MIDI file: no tracks");
    if (found < s.trackCount) s.warnings.push_back(std::format("header lists {} tracks, {} found", s.trackCount, found));

    // Merge: a stable sort by tick keeps (track, file order) for equal ticks, since the tracks
    // were appended in order.
    std::stable_sort(events.begin(), events.end(), [](const MidiEvent& x, const MidiEvent& y) { return x.tick < y.tick; });

    // Tempo map (PPQN only; SMPTE time is absolute). Tempo events in any track apply globally.
    s.tempoMap.push_back(TempoSegment{});
    if (s.ppqn) {
        for (const auto& e : events) {
            if (!e.isMeta() || e.data1 != meta::kTempo || e.dataLength != 3) continue;
            const auto d = s.payload(e);
            const uint32_t tempo = (static_cast<uint32_t>(d[0]) << 16) | (static_cast<uint32_t>(d[1]) << 8) | d[2];
            if (tempo == 0) continue;
            TempoSegment& last = s.tempoMap.back();
            if (e.tick == last.tick) {
                last.usPerQuarter = tempo;
            } else {
                TempoSegment seg;
                seg.tick = e.tick;
                seg.us = last.us + static_cast<uint64_t>(e.tick - last.tick) * last.usPerQuarter / s.ppqn;
                seg.usPerQuarter = tempo;
                s.tempoMap.push_back(seg);
            }
        }
    }
    for (auto& e : events) e.us = s.tickToUs(e.tick);
    s.events = std::move(events);
    for (const auto& t : s.tracks) s.endTick = std::max(s.endTick, t.endTick);
    s.lengthUs = s.tickToUs(s.endTick);
    return s;
}

namespace {

std::string printable(std::span<const uint8_t> d, size_t max = 48) {
    std::string out;
    for (size_t i = 0; i < d.size() && i < max; ++i) {
        const uint8_t c = d[i];
        if (c >= 0x20 && c < 0x7F && c != '\\')
            out += static_cast<char>(c);
        else
            out += std::format("\\x{:02x}", c);
    }
    if (d.size() > max) out += "...";
    return out;
}

std::string hexBytes(std::span<const uint8_t> d, size_t max = 16) {
    std::string out;
    for (size_t i = 0; i < d.size() && i < max; ++i) out += std::format("{}{:02x}", i ? " " : "", d[i]);
    if (d.size() > max) out += " ...";
    return out;
}

}  // namespace

std::string describeEvent(const MidiSong& song, const MidiEvent& e) {
    if (e.isChannel()) {
        const int ch = e.channel() + 1;
        switch (e.command()) {
            case 0x80: return std::format("ch {:2} note-off {} vel {}", ch, e.data1, e.data2);
            case 0x90:
                return e.data2 ? std::format("ch {:2} note-on  {} vel {}", ch, e.data1, e.data2)
                               : std::format("ch {:2} note-off {} (note-on vel 0)", ch, e.data1);
            case 0xA0: return std::format("ch {:2} key-pressure {} {}", ch, e.data1, e.data2);
            case 0xB0: return std::format("ch {:2} control {} = {}", ch, e.data1, e.data2);
            case 0xC0: return std::format("ch {:2} program {}", ch, e.data1);
            case 0xD0: return std::format("ch {:2} channel-pressure {}", ch, e.data1);
            default: return std::format("ch {:2} pitch-bend {}", ch, (e.data2 << 7 | e.data1) - 8192);
        }
    }
    const auto d = song.payload(e);
    if (e.isSysEx()) return std::format("sysex {:02X} {} bytes (skipped)", e.status, d.size());
    switch (e.data1) {
        case 0x00: return std::format("meta sequence-number {}", hexBytes(d));
        case 0x01: return std::format("meta text '{}'", printable(d));
        case 0x02: return std::format("meta copyright '{}'", printable(d));
        case meta::kTrackName: return std::format("meta track-name '{}'", printable(d));
        case 0x04: return std::format("meta instrument '{}'", printable(d));
        case 0x05: return std::format("meta lyric '{}'", printable(d));
        case meta::kMarker: return std::format("meta marker '{}'", printable(d));
        case 0x07: return std::format("meta cue '{}'", printable(d));
        case 0x20: return std::format("meta channel-prefix {}", hexBytes(d));
        case 0x21: return std::format("meta port {}", hexBytes(d));
        case meta::kEndOfTrack: return "meta end-of-track";
        case meta::kTempo:
            if (d.size() == 3) {
                const uint32_t t = (static_cast<uint32_t>(d[0]) << 16) | (static_cast<uint32_t>(d[1]) << 8) | d[2];
                return std::format("meta tempo {} us/quarter ({:.2f} bpm)", t, t ? 60e6 / t : 0.0);
            }
            break;
        case meta::kSmpteOffset:
            if (d.size() == 5) return std::format("meta smpte-offset {:02}:{:02}:{:02} fr {} sub {}", d[0] & 0x1F, d[1], d[2], d[3], d[4]);
            break;
        case meta::kTimeSignature:
            if (d.size() == 4)
                return std::format("meta time-signature {}/{} clocks {} 32nds {}", d[0], 1 << (d[1] & 7), d[2], d[3]);
            break;
        case meta::kKeySignature:
            if (d.size() == 2) return std::format("meta key-signature {} {}", static_cast<int8_t>(d[0]), d[1] ? "minor" : "major");
            break;
        case meta::kSequencerSpecific: return std::format("meta sequencer-specific {}", hexBytes(d));
        default: break;
    }
    return std::format("meta {:#04x} [{}]", e.data1, hexBytes(d));
}

}  // namespace tmport::audio
