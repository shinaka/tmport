#pragma once
// Standard MIDI File parser (our own; the game's SOUNDS\*.MID are SMF format 0, 480 PPQN,
// re/docs/fmt_sound.md §8.1). Supports formats 0 and 1 (format 2 is merged like format 1, with a
// warning), PPQN and SMPTE time division, running status, tempo changes in any track, meta events
// (kept for dumps), and SysEx (F0/F7, skipped by playback). Unknown chunks are skipped.
//
// Timing: each event gets an absolute time in microseconds from the tempo map, using integer
// arithmetic per tempo segment: us(t) = segUs + (t - segTick) * usPerQuarter / ppqn.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tmport::audio {

struct MidiEvent {
    uint32_t tick = 0;
    uint64_t us = 0;
    uint16_t track = 0;
    uint8_t status = 0;       // 0x80..0xEF channel message, 0xFF meta, 0xF0/0xF7 SysEx
    uint8_t data1 = 0;        // channel: first data byte; meta: the meta type
    uint8_t data2 = 0;        // channel: second data byte (0 for program change / pressure)
    uint32_t dataOffset = 0;  // meta / SysEx payload in MidiSong::bytes
    uint32_t dataLength = 0;

    bool isChannel() const { return status >= 0x80 && status < 0xF0; }
    bool isMeta() const { return status == 0xFF; }
    bool isSysEx() const { return status == 0xF0 || status == 0xF7; }
    uint8_t channel() const { return status & 0x0F; }
    uint8_t command() const { return status & 0xF0; }
    bool isNoteOn() const { return command() == 0x90 && data2 > 0; }
    bool isNoteOff() const { return command() == 0x80 || (command() == 0x90 && data2 == 0); }
};

// Meta types the engine looks at.
namespace meta {
inline constexpr uint8_t kTrackName = 0x03;
inline constexpr uint8_t kMarker = 0x06;
inline constexpr uint8_t kEndOfTrack = 0x2F;
inline constexpr uint8_t kTempo = 0x51;
inline constexpr uint8_t kSmpteOffset = 0x54;
inline constexpr uint8_t kTimeSignature = 0x58;
inline constexpr uint8_t kKeySignature = 0x59;
inline constexpr uint8_t kSequencerSpecific = 0x7F;
}  // namespace meta

struct TempoSegment {
    uint32_t tick = 0;
    uint64_t us = 0;
    uint32_t usPerQuarter = 500000;
};

struct MidiTrackInfo {
    uint32_t endTick = 0;     // tick of the end-of-track event (or of the last event)
    uint32_t eventCount = 0;  // all events, including meta and SysEx
    bool hasEndOfTrack = false;
};

struct MidiSong {
    uint16_t format = 0;
    uint16_t trackCount = 0;
    uint16_t division = 0;   // raw header word
    uint16_t ppqn = 0;       // ticks per quarter note (0 for SMPTE division)
    int smpteFps = 0;        // SMPTE division: frames per second (24, 25, 29 = 29.97, 30)
    int smpteTicksPerFrame = 0;

    std::vector<uint8_t> bytes;              // the file (meta / SysEx payloads point into it)
    std::vector<MidiEvent> events;           // all tracks merged, ordered by (tick, track, file order)
    std::vector<TempoSegment> tempoMap;      // starts at tick 0 (default 120 BPM)
    std::vector<MidiTrackInfo> tracks;
    uint32_t endTick = 0;                    // latest end of track
    uint64_t lengthUs = 0;                   // us(endTick): where playback ends (and a loop restarts)
    std::vector<std::string> warnings;

    uint64_t tickToUs(uint32_t tick) const;
    std::span<const uint8_t> payload(const MidiEvent& e) const {
        return std::span<const uint8_t>(bytes).subspan(e.dataOffset, e.dataLength);
    }
    // Statistics.
    size_t channelEventCount() const;
    size_t noteOnCount() const;
    uint32_t lastNoteOnTick() const;
};

// Parses an SMF. Throws mischief::FormatError for data that is not a usable SMF.
MidiSong parseSmf(std::vector<uint8_t> bytes);

// One-line description of an event for dumps (e.g. "ch 2 note-on 60 vel 100", "meta tempo 480000").
std::string describeEvent(const MidiSong& song, const MidiEvent& e);

}  // namespace tmport::audio
