#include "dos/data/puzzles.h"

#include <algorithm>
#include <string>

#include "dos/data/dgroup_offsets.h"
#include "mischief/util/error.h"

namespace tmport::dos {

namespace {

[[noreturn]] void badPuzzles(const std::string& why) { throw mischief::FormatError("ALL_PUZS: " + why); }

}  // namespace

// --- PRG_DICT (Quiz_LoadDictionary 46ce:095f) ----------------------------------------------------

Dictionary Dictionary::parse(std::span<const uint8_t> prgDict) {
    if (prgDict.size() < kReachable * kEntrySize)
        throw mischief::FormatError("PRG_DICT: " + std::to_string(prgDict.size()) + " bytes, needs at least " +
                          std::to_string(kReachable * kEntrySize));
    Dictionary d;
    d.bytes_.assign(prgDict.begin(), prgDict.end());
    return d;
}

std::string_view Dictionary::word(int i) const {
    const size_t start = static_cast<size_t>(i & 0x7F) * kEntrySize;
    const auto first = bytes_.begin() + static_cast<std::ptrdiff_t>(start);
    const auto nul = std::find(first, bytes_.end(), uint8_t{0});
    return {reinterpret_cast<const char*>(bytes_.data() + start), static_cast<size_t>(nul - first)};
}

// 46ce:08f1 Dict_Expand
std::string Dictionary::expand(std::span<const uint8_t> src) const {
    std::string out;
    out.reserve(src.size() * 2);
    for (const uint8_t b : src) {
        if (b == 0) break;
        if (b < 0x80)
            out.push_back(static_cast<char>(b));
        else
            out.append(word(b & 0x7F));
    }
    return out;
}

// --- Quiz_SplitText 46ce:0323 --------------------------------------------------------------------

Puzzle splitPuzzle(std::string_view expanded) {
    const size_t tilde = expanded.find('~');
    if (tilde == std::string_view::npos) throw mischief::FormatError("ALL_PUZS: question without '~'");
    Puzzle p;
    p.question.assign(expanded.substr(0, tilde));
    size_t pos = tilde + 1;
    for (auto& answer : p.answers) {  // each answer ends at CR, NUL or the end of the text
        size_t end = pos;
        while (end < expanded.size() && expanded[end] != '\r' && expanded[end] != '\0') ++end;
        answer.assign(expanded.substr(pos, end - pos));
        pos = std::min(end + 1, expanded.size());
    }
    return p;
}

// --- ALL_PUZS (Quiz_LoadQuestions 46ce:09ab) ------------------------------------------------------

PuzzleFile PuzzleFile::parse(std::span<const uint8_t> allPuzs) {
    if (allPuzs.size() < kTableSize) badPuzzles("shorter than its offset table");
    if (allPuzs.size() > 0xFFFF) badPuzzles("larger than 64 KB");  // the length is kept in a u16 (DS:a21a)
    PuzzleFile f;
    f.bytes_.assign(allPuzs.begin(), allPuzs.end());
    for (int i = 0; i < kCount; ++i) {
        const uint16_t off = static_cast<uint16_t>(allPuzs[2 * i] | (allPuzs[2 * i + 1] << 8));
        if (i > 0 && off <= f.offsets_[static_cast<size_t>(i - 1)])
            badPuzzles("offsets not ascending at question " + std::to_string(i));
        if (kTableSize + off >= allPuzs.size()) badPuzzles("question " + std::to_string(i) + " outside the file");
        f.offsets_[static_cast<size_t>(i)] = off;
    }
    for (int i = 0; i < kCount; ++i) {
        const size_t end = i + 1 < kCount ? f.textPos(i + 1) : allPuzs.size();
        const auto first = allPuzs.begin() + static_cast<std::ptrdiff_t>(f.textPos(i));
        if (std::find(first, allPuzs.begin() + static_cast<std::ptrdiff_t>(end), uint8_t{0}) ==
            allPuzs.begin() + static_cast<std::ptrdiff_t>(end))
            badPuzzles("question " + std::to_string(i) + " is not NUL-terminated");
    }
    return f;
}

std::span<const uint8_t> PuzzleFile::raw(int i) const {
    const auto all = std::span<const uint8_t>(bytes_).subspan(textPos(i));
    const auto nul = std::find(all.begin(), all.end(), uint8_t{0});
    return all.first(static_cast<size_t>(nul - all.begin()));
}

Puzzle PuzzleFile::puzzle(int i, const Dictionary& dict) const { return splitPuzzle(dict.expand(raw(i))); }

// 46ce:0d9a Quiz_FindTierEnd (16-bit arithmetic as the original)
int PuzzleFile::tier5Start() const {
    const uint16_t length = static_cast<uint16_t>(bytes_.size());
    if (length == kWindowSize) return 0;
    const uint16_t limit = static_cast<uint16_t>(length - kWindowSize);
    int x = 0;
    while (x < kCount && offsets_[static_cast<size_t>(x)] <= limit) ++x;
    return x;
}

bool PuzzleFile::fitsWindow(int tierStart, int i) const {
    if (offset(i) < offset(tierStart)) return false;
    const size_t windowEnd = textPos(tierStart) + kWindowSize;
    return textPos(i) + raw(i).size() < std::min(windowEnd, bytes_.size());
}

// --- DS:5fd2 g_quizTierStart ----------------------------------------------------------------------

QuizTiers QuizTiers::read(const DosExe& exe, const PuzzleFile& puzzles) {
    QuizTiers t;
    for (int k = 0; k < 5; ++k) t.starts_[static_cast<size_t>(k)] = exe.ds16(static_cast<uint16_t>(dg::kQuizTierStart + 2 * k));
    t.starts_[5] = static_cast<uint16_t>(puzzles.tier5Start());  // DS:5fdc, Quiz_FindTierEnd
    t.starts_[6] = PuzzleFile::kCount;                           // DS:5fde
    if (t.starts_[0] != 0 || t.starts_[4] != PuzzleFile::kCount)
        throw mischief::FormatError("SST.EXE: unexpected quiz tier table at DS:5fd2");
    for (int k = 0; k < 4; ++k)
        if (t.starts_[static_cast<size_t>(k)] >= t.starts_[static_cast<size_t>(k + 1)])
            throw mischief::FormatError("SST.EXE: quiz tiers at DS:5fd2 do not ascend");
    return t;
}

}  // namespace tmport::dos
