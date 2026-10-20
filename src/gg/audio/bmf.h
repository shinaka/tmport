#pragma once
// BMF songs (re/gg/docs/fmt_sound.md §3): the layout of the song block the driver registers and
// works on in place (offsets from s = BMF + 0x10), and a parser that reads a song the way the
// driver's interpreters do (Snd_ReadEvent 1bb4:093f, Snd_StepSpeaker 1bb4:0966, Snd_StepOpl
// 1bb4:0ad6, Snd_MetaEvent 1bb4:08ef). The parser is for tools, tests and checks; the driver
// (snd_driver.h) interprets the bytes itself, as the original does.
//
//   BMF:  'B' 'M' 'F' 1Ah, 12 zero bytes, then the song block s:
//   s+00 class (1 speaker, 3 OPL2; 0, 2, 4 dead)   s+4E loop section   s+52 section count n
//   s+01 delay countdown (run time)                 s+4F current section s+53 n x {u16 start,
//   s+03 tick counter (run time)                    s+50 live transpose        u8 next, u8 transpose}
//   s+05 stream position (run time)                 s+51 running status s+53+4n (class 3) patch
//   s+07 cue tick, s+09 far cue callback, s+0D loop                         count m, m x u16 offsets
//   s+0E 16 x {u8 flags, u8 program, u16 bend}
//
// Section stream: one delay byte that Snd_StartSection 1bb4:0045 skips (FFh: and a u16), then
// events, each followed by a delay byte (0 = the next event in the same tick, FFh = a u16 delay).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tmport::gg {

namespace bmf {

inline constexpr size_t kHeaderSize = 0x10;  // the song block starts here
inline constexpr std::array<uint8_t, 4> kMagic = {'B', 'M', 'F', 0x1A};

// Song block fields (offsets from s).
inline constexpr size_t kClass = 0x00;
inline constexpr size_t kDelay = 0x01;          // u16 delay countdown
inline constexpr size_t kTick = 0x03;           // u16 tick counter (cleared by a section start)
inline constexpr size_t kPosition = 0x05;       // u16 stream position, relative to s
inline constexpr size_t kCueTick = 0x07;        // u16
inline constexpr size_t kCueCallback = 0x09;    // far pointer (4 bytes)
inline constexpr size_t kLoop = 0x0D;           // u8 loop flag
inline constexpr size_t kChannels = 0x0E;       // 16 x 4-byte channel records
inline constexpr size_t kChannelRecordSize = 4; // {u8 flags, u8 program, u16 bend}
inline constexpr size_t kLoopSection = 0x4E;
inline constexpr size_t kSection = 0x4F;        // current section
inline constexpr size_t kTranspose = 0x50;      // live transpose (from the section entry)
inline constexpr size_t kRunningStatus = 0x51;
inline constexpr size_t kSectionCount = 0x52;
inline constexpr size_t kSections = 0x53;       // section entries, 4 bytes each
inline constexpr size_t kSectionEntrySize = 4;  // {u16 start, u8 next, u8 transpose}
inline constexpr size_t kMinBlockSize = 0x57;   // header fields and one section entry

inline constexpr int kChannelCount = 16;
inline constexpr size_t kPatchSize = 11;        // class 3 instrument patch (fmt_sound.md §3.4)

// Channel record flags.
inline constexpr uint8_t kChannelUsed = 0x80;     // from the file
inline constexpr uint8_t kChannelStopped = 0x02;  // owned by another song, or the song stopped
inline constexpr uint8_t kChannelMuted = 0x01;    // muted by the game (arrangements)

inline constexpr uint8_t kNoSection = 0xFF;       // section entry: no next section
inline constexpr uint8_t kNoProgram = 0xFF;

// Song classes (interpreters DS:1aaa).
inline constexpr uint8_t kClassSweep = 0;    // PIT divisor list (1bb4:087e, no song uses it)
inline constexpr uint8_t kClassSpeaker = 1;  // 1bb4:0966
inline constexpr uint8_t kClassPsg = 2;      // 1bb4:0a1f (dead)
inline constexpr uint8_t kClassOpl = 3;      // 1bb4:0ad6
inline constexpr uint8_t kClassMpu = 4;      // MPU-401 (dead; DS:1aaa[4] = the speaker interpreter)

inline uint16_t le16(std::span<const uint8_t> b, size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

}  // namespace bmf

enum class BmfEventKind : uint8_t {
    NoteOff,  // 8n (class 1 and 3; below 90h)
    NoteOn,   // class 1: 9n..Dn key; class 3: 9n..Bn key velocity
    Program,  // class 3: Cn..Dn program
    Bend,     // En lo hi
    Meta,     // class 1: status & F0h = F0h; class 3: >= F0h: len16 then len bytes (t, v, ...)
};

struct BmfEvent {
    int section = 0;
    uint32_t tick = 0;          // stream time in sound ticks from the section start
    uint16_t offset = 0;        // of the event's first byte (status, or the running-status data byte)
    BmfEventKind kind = BmfEventKind::NoteOff;
    uint8_t status = 0;         // the effective status byte (running status resolved)
    bool runningStatus = false; // no status byte in the stream
    uint8_t channel = 0;        // status & 0Fh
    uint8_t key = 0;            // NoteOn
    uint8_t velocity = 0;       // NoteOn, class 3
    uint8_t program = 0;        // Program
    uint16_t bend = 0;          // Bend
    uint16_t metaLength = 0;    // Meta: the length word
    uint8_t metaType = 0, metaValue = 0;  // Meta: the first two payload bytes
    bool endOfSection = false;  // Meta t = 1, v = the section: the section ends here
    uint32_t delay = 0;         // the delay after the event (0 for the end of a section)
};

struct BmfSection {
    uint16_t start = 0;
    uint8_t next = bmf::kNoSection;
    uint8_t transpose = 0;
};

struct BmfChannelRecord {
    uint8_t flags = 0;
    uint8_t program = bmf::kNoProgram;
    uint16_t bend = 0;
};

// A song read through the interpreters' rules. Every section is walked from its start to its end
// event (meta type 1 with its own number); the song's length is the longest section.
struct BmfSongInfo {
    uint8_t songClass = 0;
    std::array<BmfChannelRecord, bmf::kChannelCount> channels{};
    uint8_t loopSection = 0;
    uint8_t transposeField = 0;
    std::vector<BmfSection> sections;
    std::vector<uint16_t> patchOffsets;                       // class 3
    std::vector<std::array<uint8_t, bmf::kPatchSize>> patches; // class 3
    std::vector<BmfEvent> events;
    size_t blockSize = 0;  // bytes from s to the end of the payload
    size_t end = 0;        // first byte after the last section's end event (relative to s)
    uint32_t ticks = 0;    // length of the longest section in sound ticks

    uint16_t usedChannelMask() const;  // bit c = channel c has flag 80h
    int usedChannelCount() const;
    size_t countEvents(BmfEventKind kind) const;
};

// True when the payload starts with the BMF magic (Snd_RegisterSong 1bb4:0ebc's test).
bool isBmfSong(std::span<const uint8_t> payload);

// Parses a whole BMF resource or bundle member payload. Throws tmport::FormatError ("BMF: ...")
// when the magic is missing, the header or a section entry lies outside the payload, a patch
// lies outside it (class 3), a section runs past the end without its end event, or a status the
// song's class does not interpret (class 0, 2, 4 songs are not parsed).
BmfSongInfo parseBmfSong(std::span<const uint8_t> payload);

// One line per event, like tools/gg/bmf.py's listing ("  tick  @offs  on    ch0  key  60 vel 100";
// the velocity for class 3 songs).
std::string describeBmfEvent(const BmfEvent& e, uint8_t songClass);

}  // namespace tmport::gg
