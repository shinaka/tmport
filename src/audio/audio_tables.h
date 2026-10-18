#pragma once
// Sound and music tables of the original, read from the user's TMTWINCD.EXE (DGROUP; offsets in
// data/dgroup_offsets.h, layouts in re/docs/fmt_sound.md §6, §8.3, §10). Tests that run without
// game data fill the struct by hand.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tmport {
class ExeTables;
}

namespace tmport::audio {

// Speech stop-code lists (u16 arrays ending in 0; fmt_sound.md §6).
enum class StopList {
    ClueReplay,   // DS:0f54  {0x0D, 0x1B}            10e8:01ef clue-word replay
    Hint,         // DS:1414  {0x0D, 0x1B, 0x4B0}     1130:0166 hint popup
    Praise,       // DS:1e01  {0x1B, 0x0D}            1150:06ac praise + clue word
    WrongAnswer,  // DS:1e17  {0x1B, 0x0D}            1150:0891 wrong answer
    QuizLine,     // DS:1e1d  (18 codes)              1150:1409 question lines
    QuizAnswer,   // DS:1e43  (18 codes)              1150:14d7 answer choices
};
inline constexpr size_t kStopListCount = 6;

struct AudioTables {
    static constexpr size_t kTunes = 10;

    std::array<int16_t, kTunes> tuneIds{};      // DS:09fa: 9000..9009
    std::array<int16_t, kTunes> loopPos{};      // DS:0a0e: 0 = loop from the start, -1 = play once
    std::array<std::string, kTunes> tuneFiles;  // DS:0a22 -> file name templates; byte 0 is the B/E placeholder
    std::string soundsDir;                      // DS:0b16: sub-directory of the MIDI files
    uint8_t soundOn = 1;                        // DS:0b96 initial "Music and Sound"
    uint8_t voiceOn = 1;                        // DS:0b97 initial "Voice"
    int16_t initialTuneId = -1;                 // DS:13ca initial scene tune (none)
    std::array<std::vector<uint16_t>, kStopListCount> stopLists;  // without the terminating 0

    const std::vector<uint16_t>& stopCodes(StopList l) const { return stopLists[static_cast<size_t>(l)]; }
    // Index of a tune id in tuneIds, or -1 (Midi_OpenTrack's search; 10 = "not found" there).
    int tuneIndex(int tuneId) const;

    static AudioTables fromExe(const ExeTables& exe);
};

}  // namespace tmport::audio
