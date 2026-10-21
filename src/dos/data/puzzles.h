#pragma once
// The elf questions of the DOS edition: ALL_PUZS (360 questions, compressed with PRG_DICT) and the
// PRG_DICT word table, with the byte semantics of quiz.c (segment 46ce):
//   * Dict_Expand 46ce:08f1: bytes < 0x80 are copied, a byte b >= 0x80 inserts the NUL-terminated
//     16-byte dictionary entry b & 0x7F;
//   * Quiz_SplitText 46ce:0323: the question is everything before '~' (with the two pad bytes), then
//     three answers each ended by CR or NUL; answer 0 is the correct one;
//   * Quiz_LoadQuestions 46ce:09ab: one 0x7B00-byte read from the tier's first question; tier
//     windows from DS:5fd2, word 5 computed by Quiz_FindTierEnd 46ce:0d9a.
// Strings stay raw game bytes (markup of Text_Draw, fmt_data.md §6). Specs: re/dos/docs/fmt_data.md
// §2, code_ui.md §10.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dos/data/dos_exe.h"

namespace tmport::dos {

// PRG_DICT, read whole into DS:b204 (Quiz_LoadDictionary 46ce:095f).
class Dictionary {
public:
    static constexpr size_t kEntrySize = 16;
    static constexpr int kReachable = 128;  // a byte b >= 0x80 selects entry b & 0x7F

    // Needs at least the 128 reachable entries. Throws mischief::FormatError.
    static Dictionary parse(std::span<const uint8_t> prgDict);

    // Entry i & 0x7F up to its NUL. Like the original's copy loop it runs into the next entry when
    // an entry has no NUL (none in V 1.0), and stops at the end of the file.
    std::string_view word(int i) const;
    size_t entryCount() const { return bytes_.size() / kEntrySize; }

    // Dict_Expand 46ce:08f1: `src` up to its first NUL (or its end), dictionary bytes expanded.
    std::string expand(std::span<const uint8_t> src) const;

private:
    std::vector<uint8_t> bytes_;
};

// One question after Quiz_SplitText 46ce:0323.
struct Puzzle {
    std::string question;               // bytes before '~', including the 2 pad bytes (DS:a293)
    std::array<std::string, 3> answers; // DS:a22a + 35*n; answer 0 is correct
};

// Splits an expanded question. Throws mischief::FormatError if there is no '~' (none in V 1.0).
Puzzle splitPuzzle(std::string_view expanded);

// ALL_PUZS: 360 u16 offsets (relative to the end of the table), then the NUL-terminated texts.
class PuzzleFile {
public:
    static constexpr int kCount = 360;            // 46ce:0d9a loops to 0x168
    static constexpr size_t kTableSize = 0x2D0;   // 360 offsets, read first by Quiz_LoadQuestions
    static constexpr size_t kWindowSize = 0x7B00; // the one read of question texts

    // Checks the table and that every text is NUL-terminated inside the file. Throws
    // mischief::FormatError.
    static PuzzleFile parse(std::span<const uint8_t> allPuzs);

    size_t fileSize() const { return bytes_.size(); }
    uint16_t offset(int i) const { return offsets_.at(static_cast<size_t>(i)); }
    // File position of question i's text (kTableSize + offset).
    size_t textPos(int i) const { return kTableSize + offset(i); }
    // Question i's compressed text without its NUL.
    std::span<const uint8_t> raw(int i) const;
    // raw(i) expanded and split, as the quiz screen shows it.
    Puzzle puzzle(int i, const Dictionary& dict) const;

    // Quiz_FindTierEnd 46ce:0d9a: the number of questions whose offset is at most fileSize - 0x7B00
    // (unsigned), i.e. the first question whose text no longer fits one read from the start of the
    // file (123 in V 1.0); 0 when the file is exactly 0x7B00 bytes, as in the original.
    int tier5Start() const;

    // True when question i's text and NUL lie inside the 0x7B00-byte read that Quiz_LoadQuestions
    // makes from the first question of a tier (`tierStart`).
    bool fitsWindow(int tierStart, int i) const;

private:
    std::vector<uint8_t> bytes_;
    std::array<uint16_t, kCount> offsets_{};
};

// The tier table DS:5fd2 as the quiz uses it after Quiz_FindTierEnd: {0, 63, 180, 303, 360, X, 360}.
// Tier t covers questions [start(t), start(t + 1)); tiers 0..3 and 5 are used, tier 4 never.
class QuizTiers {
public:
    static constexpr int kEntries = 7;

    // Words 0..4 from the EXE, word 5 = puzzles.tier5Start(), word 6 = 360. Checks that the words
    // ascend and fit 360. Throws mischief::FormatError.
    static QuizTiers read(const DosExe& exe, const PuzzleFile& puzzles);

    uint16_t start(int index) const { return starts_.at(static_cast<size_t>(index)); }
    // [first, end) of tier 0..3 or 5.
    std::pair<int, int> range(int tier) const { return {start(tier), start(tier + 1)}; }

private:
    std::array<uint16_t, kEntries> starts_{};
};

// History status of question i in the 90-byte history of a player record (+0x0C): 2 bits per
// question at bits 2*(i % 4) of byte i / 4 (masks DS:5eae). 0 never dealt, 1 right first time,
// 2 right after a miss, 3 last answer wrong.
inline int puzzleStatus(std::span<const uint8_t> history, int i) {
    return (history[static_cast<size_t>(i) >> 2] >> (2 * (i & 3))) & 3;
}

}  // namespace tmport::dos
