#include "dos/mods/dos_text_files.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <functional>
#include <map>

#include "dos/data/puzzles.h"
#include "dos/mods/dos_text_codec.h"
#include "mischief/util/error.h"
#include "mods/mod_common.h"

namespace tmport::dos {

namespace {

constexpr uint8_t kCR = 0x0D;
constexpr size_t kAnswerMax = 34;         // DS:a22a + 35*n, NUL-terminated
constexpr size_t kQuestionMax = 999;      // DS:a293 .. a67a
constexpr size_t kExpandedBuffer = 0xB40; // DS:a6b8 .. b1f7: the nine dealt questions, back to back
constexpr size_t kHelpBuffer = 0xED8;     // DS:9325

// A file split into "=== header" sections; the lines of a section exclude the newline that ends its
// last line (a page or question ending with a line break has an empty last line).
struct Section {
    std::string_view header;  // text after "=== "
    int line = 0;             // line number of the header
    std::vector<std::string_view> lines;
};

std::vector<Section> splitSections(std::string_view text, std::string_view what) {
    std::vector<std::string_view> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string_view l = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        lines.push_back(l);
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();  // the newline that ends the file
    if (!lines.empty() && lines.front().starts_with("\xEF\xBB\xBF")) lines.front().remove_prefix(3);
    std::vector<Section> out;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view l = lines[i];
        if (l.starts_with("===")) {
            Section s;
            s.header = mods::trimView(l.substr(3));
            s.line = static_cast<int>(i + 1);
            out.push_back(s);
            continue;
        }
        if (out.empty()) {
            if (mods::trimView(l).empty() || l.starts_with('#')) continue;
            throw mods::ModSyntaxError(std::format("text before the first '=== {}' (comments start with '#')", what), static_cast<int>(i + 1));
        }
        out.back().lines.push_back(l);
    }
    return out;
}

// "question 12 (tier 0)" -> 12 after the keyword.
int headerNumber(std::string_view header, std::string_view keyword, int line) {
    if (!header.starts_with(keyword)) throw mods::ModSyntaxError(std::format("expected '=== {} N'", keyword), line);
    std::string_view rest = mods::trimView(header.substr(keyword.size()));
    int v = -1;
    const auto [p, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), v);
    if (ec != std::errc() || v < 0) throw mods::ModSyntaxError(std::format("expected '=== {} N'", keyword), line);
    (void)p;
    return v;
}

std::vector<uint8_t> joinLines(std::span<const std::string_view> lines, int firstLine) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out.push_back(kCR);
        const auto b = unescapeDosText(lines[i], DosTextForm::Lines, firstLine + static_cast<int>(i));
        out.insert(out.end(), b.begin(), b.end());
    }
    return out;
}

bool isWordChar(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'' || c == '`' || c == '-' || c == '_';
}

}  // namespace

// ---- ALL_PUZS ----------------------------------------------------------------------------------

std::string puzzlesToText(const PuzzleFile& puzzles, const Dictionary& dict, const DosExe& exe) {
    const QuizTiers tiers = QuizTiers::read(exe, puzzles);
    auto tierOf = [&](int q) {
        for (const int t : {0, 1, 2, 3}) {
            const auto [a, b] = tiers.range(t);
            if (q >= a && q < b) return t;
        }
        return -1;
    };
    std::string out =
        "# ALL_PUZS: the 360 elf questions of the 1990 DOS edition, written by tmport --export-mod-kit from\n"
        "# your game files. Each question: '=== question N', the question lines as drawn (the first line\n"
        "# starts with the game's two indent spaces), '--- answers', then three answers: the FIRST is the\n"
        "# correct one (the game shuffles them on screen). Text codes and limits: docs/MODDING.md.\n";
    for (int i = 0; i < PuzzleFile::kCount; ++i) {
        const std::string exp = dict.expand(puzzles.raw(i));
        const std::span<const uint8_t> b(reinterpret_cast<const uint8_t*>(exp.data()), exp.size());
        const size_t tilde = exp.find('~');
        if (tilde == std::string::npos) throw mischief::FormatError(std::format("ALL_PUZS: question {} has no '~'", i));
        out += std::format("\n=== question {} (tier {})\n", i, tierOf(i));
        out += escapeDosText(b.first(tilde), DosTextForm::Lines);
        out += "\n--- answers\n";
        size_t pos = tilde + 1;
        bool finalBreak = true;
        for (int a = 0; a < 3; ++a) {
            size_t end = pos;
            while (end < b.size() && b[end] != kCR) ++end;
            out += escapeDosText(b.subspan(pos, end - pos), DosTextForm::Lines) + '\n';
            if (end >= b.size()) {
                finalBreak = false;
                pos = b.size();
                if (a < 2) throw mischief::FormatError(std::format("ALL_PUZS: question {} has fewer than three answers", i));
                break;
            }
            pos = end + 1;
        }
        if (!finalBreak) out += "--- no final break\n";
        if (pos < b.size()) {
            std::string tail;
            for (size_t k = pos; k < b.size(); ++k) {
                const uint8_t c = b[k];
                tail += c == kCR ? std::string("{cr}") : escapeDosText(b.subspan(k, 1), DosTextForm::Lines);
            }
            out += "--- after " + tail + '\n';
        }
    }
    return out;
}

std::vector<std::vector<uint8_t>> puzzlesFromText(std::string_view text) {
    const auto sections = splitSections(text, "question N");
    std::vector<std::vector<uint8_t>> out;
    for (const Section& s : sections) {
        const int n = headerNumber(s.header, "question", s.line);
        if (n != static_cast<int>(out.size()))
            throw mods::ModSyntaxError(std::format("expected question {} (the questions in order)", out.size()), s.line);
        size_t marker = s.lines.size();
        for (size_t i = 0; i < s.lines.size(); ++i)
            if (s.lines[i] == "--- answers") {
                marker = i;
                break;
            }
        if (marker == s.lines.size()) throw mods::ModSyntaxError(std::format("question {} has no '--- answers' line", n), s.line);
        const std::span<const std::string_view> q(s.lines.data(), marker);
        for (size_t i = 0; i < q.size(); ++i)
            if (q[i].find('~') != std::string_view::npos)
                throw mods::ModSyntaxError("'~' ends the question in the game; it cannot appear in the question text",
                                           s.line + 1 + static_cast<int>(i));
        std::vector<uint8_t> e = joinLines(q, s.line + 1);
        e.push_back('~');
        int answers = 0;
        bool finalBreak = true;
        std::vector<uint8_t> tail;
        for (size_t i = marker + 1; i < s.lines.size(); ++i) {
            const std::string_view l = s.lines[i];
            const int lineNo = s.line + 1 + static_cast<int>(i);
            if (l.starts_with("--- ")) {
                const std::string_view what = l.substr(4);
                if (what == "no final break") {
                    finalBreak = false;
                } else if (what.starts_with("after ")) {
                    tail = unescapeDosText(what.substr(6), DosTextForm::Lines, lineNo);
                } else {
                    throw mods::ModSyntaxError(std::format("unknown line '{}'", l), lineNo);
                }
                continue;
            }
            if (answers == 3) {
                if (l.empty()) continue;  // blank lines between questions
                throw mods::ModSyntaxError(std::format("question {} has more than three answers", n), lineNo);
            }
            if (answers > 0) e.push_back(kCR);
            const auto a = unescapeDosText(l, DosTextForm::Lines, lineNo);
            e.insert(e.end(), a.begin(), a.end());
            ++answers;
        }
        if (answers != 3) throw mods::ModSyntaxError(std::format("question {} has {} answer(s); it needs exactly 3", n, answers), s.line);
        if (finalBreak) e.push_back(kCR);
        else if (!tail.empty()) throw mods::ModSyntaxError(std::format("question {}: '--- after' needs the final break", n), s.line);
        e.insert(e.end(), tail.begin(), tail.end());
        out.push_back(std::move(e));
    }
    if (out.size() != PuzzleFile::kCount)
        throw mods::ModSyntaxError(std::format("{} questions; the game has exactly {}", out.size(), PuzzleFile::kCount),
                                   sections.empty() ? 1 : sections.back().line);
    return out;
}

std::vector<uint8_t> compressPuzzle(std::span<const uint8_t> expanded, const Dictionary& dict) {
    // Candidates by first byte, longest first, then by entry number.
    std::map<uint8_t, std::vector<std::pair<std::string_view, int>>> byFirst;
    for (int k = 0; k < Dictionary::kReachable; ++k) {
        const std::string_view w = dict.word(k);
        if (!w.empty()) byFirst[static_cast<uint8_t>(w[0])].emplace_back(w, k);
    }
    for (auto& [c, v] : byFirst)
        std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    std::vector<uint8_t> out;
    out.reserve(expanded.size());
    const std::string_view text(reinterpret_cast<const char*>(expanded.data()), expanded.size());
    for (size_t i = 0; i < expanded.size();) {
        const uint8_t c = expanded[i];
        if (c >= 0x80) throw mischief::FormatError(std::format("byte {:#04x} cannot be stored in ALL_PUZS (it reads as a word code)", c));
        int hit = -1;
        size_t len = 0;
        if (i == 0 || !isWordChar(expanded[i - 1])) {
            const auto it = byFirst.find(c);
            if (it != byFirst.end())
                for (const auto& [w, k] : it->second) {
                    if (text.substr(i, w.size()) != w) continue;
                    const size_t j = i + w.size();
                    if (j < expanded.size() && isWordChar(expanded[j])) continue;
                    hit = k;
                    len = w.size();
                    break;
                }
        }
        if (hit >= 0) {
            out.push_back(static_cast<uint8_t>(0x80 | hit));
            i += len;
        } else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

std::vector<uint8_t> buildAllPuzs(const std::vector<std::vector<uint8_t>>& expanded, const Dictionary& dict) {
    if (expanded.size() != PuzzleFile::kCount)
        throw mischief::FormatError(std::format("ALL_PUZS: {} questions; the game has exactly {}", expanded.size(), PuzzleFile::kCount));
    std::vector<uint8_t> file(PuzzleFile::kTableSize, 0);
    for (size_t i = 0; i < expanded.size(); ++i) {
        const size_t off = file.size() - PuzzleFile::kTableSize;
        if (off > 0xFFFF) throw mischief::FormatError("ALL_PUZS: larger than 64 KB");
        file[2 * i] = static_cast<uint8_t>(off & 0xFF);
        file[2 * i + 1] = static_cast<uint8_t>(off >> 8);
        std::vector<uint8_t> c;
        try {
            c = compressPuzzle(expanded[i], dict);
        } catch (const mischief::FormatError& e) {
            throw mischief::FormatError(std::format("ALL_PUZS: question {}: {}", i, e.what()));
        }
        file.insert(file.end(), c.begin(), c.end());
        file.push_back(0);
    }
    if (file.size() > 0xFFFF) throw mischief::FormatError(std::format("ALL_PUZS: {} bytes; the game keeps its length in 16 bits (64 KB)", file.size()));
    return file;
}

void checkPuzzles(const std::vector<std::vector<uint8_t>>& expanded, std::span<const std::pair<int, int>> tiers, const DosFont& font,
                  DosTextProblems& out) {
    for (size_t i = 0; i < expanded.size(); ++i) {
        const auto& e = expanded[i];
        const auto tilde = std::find(e.begin(), e.end(), uint8_t{'~'});
        if (tilde == e.end()) {
            out.errors.push_back(std::format("question {}: no '~' between the question and the answers", i));
            continue;
        }
        const std::span<const uint8_t> question(e.data(), static_cast<size_t>(tilde - e.begin()));
        if (question.size() > kQuestionMax)
            out.errors.push_back(std::format("question {}: {} bytes; the question buffer holds {}", i, question.size(), kQuestionMax));
        if (auto m = checkDosMarkup(question); !m.empty()) out.errors.push_back(std::format("question {}: {}", i, m));
        const DosTextExtent qe = measureDosText(font, question, 10, 0x1C, 12);
        if (qe.any && qe.bottom > 0x6C)
            out.warnings.push_back(std::format("question {}: the question reaches row {}, into the answers (row 108 on)", i, qe.bottom));
        if (qe.clipped > 0) out.warnings.push_back(std::format("question {}: {} character(s) past the right edge are not drawn", i, qe.clipped));
        size_t pos = static_cast<size_t>(tilde - e.begin()) + 1;
        for (int a = 0; a < 3; ++a) {
            size_t end = pos;
            while (end < e.size() && e[end] != kCR) ++end;
            const std::span<const uint8_t> ans(e.data() + pos, end - pos);
            if (ans.size() > kAnswerMax)
                out.errors.push_back(std::format("question {} answer {}: {} bytes; the answer buffers hold {}", i, a + 1, ans.size(), kAnswerMax));
            if (auto m = checkDosMarkup(ans); !m.empty()) out.errors.push_back(std::format("question {} answer {}: {}", i, a + 1, m));
            const DosTextExtent ae = measureDosText(font, ans, 10, 0x6C, 12);
            if (ae.clipped > 0 || ae.lines > 1)
                out.warnings.push_back(std::format("question {} answer {}: does not fit its row", i, a + 1));
            pos = std::min(end + 1, e.size());
        }
    }
    // The nine dealt questions of a tier are expanded back to back into DS:a6b8 (Quiz_LoadQuestions).
    for (const auto& [first, tierEnd] : tiers) {
        const int end = std::min<int>(tierEnd, static_cast<int>(expanded.size()));
        std::vector<size_t> sizes;
        for (int q = first; q < end; ++q) sizes.push_back(expanded[static_cast<size_t>(q)].size());
        std::sort(sizes.begin(), sizes.end(), std::greater<>());
        size_t sum = 1;
        for (size_t k = 0; k < std::min<size_t>(9, sizes.size()); ++k) sum += sizes[k];
        if (sum > kExpandedBuffer)
            out.errors.push_back(std::format("questions {}-{}: the nine longest take {} bytes expanded; the game's buffer for the "
                                             "nine dealt questions holds {}",
                                             first, end - 1, sum, kExpandedBuffer));
    }
}

// ---- PRG_DICT ----------------------------------------------------------------------------------

std::string dictionaryToText(std::span<const uint8_t> prgDict) {
    std::string out =
        "# PRG_DICT: the word table of ALL_PUZS (1990 DOS edition), written by tmport --export-mod-kit from your\n"
        "# game files. 'N word' per entry; the questions can use entries 0-127, at most 15 characters each.\n"
        "# ALL_PUZS.txt holds the questions with the words written out: they are compressed with this table.\n";
    const size_t n = prgDict.size() / Dictionary::kEntrySize;
    if (prgDict.size() % Dictionary::kEntrySize != 0) throw mischief::FormatError("PRG_DICT: not a whole number of 16-byte entries");
    for (size_t i = 0; i < n; ++i) {
        const auto e = prgDict.subspan(i * Dictionary::kEntrySize, Dictionary::kEntrySize);
        const auto nul = std::find(e.begin(), e.end(), uint8_t{0});
        if (nul == e.end() || std::any_of(nul, e.end(), [](uint8_t b) { return b != 0; }))
            throw mischief::FormatError(std::format("PRG_DICT: entry {} is not a NUL-padded word", i));
        const auto word = e.first(static_cast<size_t>(nul - e.begin()));
        out += word.empty() ? std::format("{}\n", i) : std::format("{} {}\n", i, escapeDosText(word, DosTextForm::Lines));
    }
    return out;
}

std::vector<uint8_t> dictionaryFromText(std::string_view text) {
    std::vector<uint8_t> out;
    int lineNo = 0;
    size_t pos = 0;
    size_t count = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string_view l = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        ++lineNo;
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        if (lineNo == 1 && l.starts_with("\xEF\xBB\xBF")) l.remove_prefix(3);
        if (mods::trimView(l).empty() || l.starts_with('#')) continue;
        size_t d = 0;
        while (d < l.size() && l[d] >= '0' && l[d] <= '9') ++d;
        int idx = -1;
        if (d > 0) std::from_chars(l.data(), l.data() + d, idx);
        if (d == 0 || (d < l.size() && l[d] != ' '))
            throw mods::ModSyntaxError("expected 'N word' (an entry number, a space, the word)", lineNo);
        if (idx != static_cast<int>(count)) throw mods::ModSyntaxError(std::format("expected entry {} (the entries in order)", count), lineNo);
        const auto word = d < l.size() ? unescapeDosText(l.substr(d + 1), DosTextForm::Lines, lineNo) : std::vector<uint8_t>{};
        if (word.size() > Dictionary::kEntrySize - 1)
            throw mods::ModSyntaxError(std::format("entry {}: {} characters; an entry holds 15", idx, word.size()), lineNo);
        for (const uint8_t b : word)
            if (b >= 0x80 || b == 0x0D) throw mods::ModSyntaxError(std::format("entry {}: a line break or byte >= 0x80 is not allowed", idx), lineNo);
        std::vector<uint8_t> entry(Dictionary::kEntrySize, 0);
        std::copy(word.begin(), word.end(), entry.begin());
        out.insert(out.end(), entry.begin(), entry.end());
        ++count;
    }
    if (count < static_cast<size_t>(Dictionary::kReachable) || count > 208)
        throw mods::ModSyntaxError(std::format("{} entries; the table needs 128 to 208", count), lineNo);
    return out;
}

// ---- HELP.DAT ----------------------------------------------------------------------------------

std::string helpToText(std::span<const uint8_t> helpDat) {
    if (helpDat.size() < 4) throw mischief::FormatError("HELP.DAT: too short");
    int total = 0;
    for (int t = 0; t < 4; ++t) total += helpDat[static_cast<size_t>(t)];
    size_t expect = 4 + 2 * static_cast<size_t>(total);
    std::string out =
        "# HELP.DAT: the help pages of the 1990 DOS edition (the Help menu's topics), written by tmport\n"
        "# --export-mod-kit from your game files. '=== topic T page P', then the page as drawn (line height\n"
        "# 10; '~XXXYYY' moves to pixel column XXX, row YYY). Every line up to the next '===' is part of the\n"
        "# page (an empty last line = the page ends with a line break). Text codes: docs/MODDING.md.\n\n";
    int page = 0;
    for (int t = 0; t < 4; ++t)
        for (int p = 0; p < helpDat[static_cast<size_t>(t)]; ++p, ++page) {
            if (4 + 2 * static_cast<size_t>(page) + 2 > helpDat.size()) throw mischief::FormatError("HELP.DAT: truncated page table");
            const size_t off = static_cast<size_t>(helpDat[4 + 2 * page] | (helpDat[5 + 2 * page] << 8));
            if (off != expect) throw mischief::FormatError(std::format("HELP.DAT: page {} is not where the previous one ends", page));
            const auto nul = std::find(helpDat.begin() + static_cast<std::ptrdiff_t>(off), helpDat.end(), uint8_t{0});
            if (nul == helpDat.end()) throw mischief::FormatError(std::format("HELP.DAT: page {} has no end", page));
            const auto text = helpDat.subspan(off, static_cast<size_t>(nul - helpDat.begin()) - off);
            out += std::format("=== topic {} page {}\n", t, p) + escapeDosText(text, DosTextForm::Lines) + '\n';
            expect = off + text.size() + 1;
        }
    if (expect != helpDat.size()) throw mischief::FormatError("HELP.DAT: bytes after the last page");
    return out;
}

std::vector<uint8_t> helpFromText(std::string_view text, std::span<const uint8_t> pageCounts) {
    const auto sections = splitSections(text, "topic T page P");
    int total = 0;
    for (const uint8_t c : pageCounts) total += c;
    std::vector<std::vector<uint8_t>> pages;
    size_t topic = 0;
    int pageInTopic = 0;
    for (const Section& s : sections) {
        while (topic < pageCounts.size() && pageInTopic >= pageCounts[topic]) {
            ++topic;
            pageInTopic = 0;
        }
        const std::string expected = std::format("topic {} page {}", topic, pageInTopic);
        if (topic >= pageCounts.size() || s.header != expected)
            throw mods::ModSyntaxError(topic >= pageCounts.size()
                                           ? std::format("more than {} pages: the page pictures of the game are fixed per page", total)
                                           : std::format("expected '=== {}' (the topics have {}, {}, {} and {} pages)", expected,
                                                         pageCounts[0], pageCounts[1], pageCounts[2], pageCounts[3]),
                                       s.line);
        pages.push_back(joinLines(s.lines, s.line + 1));
        ++pageInTopic;
    }
    if (static_cast<int>(pages.size()) != total)
        throw mischief::FormatError(std::format("HELP.DAT: {} pages; the game has {} (topics of {}, {}, {} and {} pages)", pages.size(), total,
                                      pageCounts[0], pageCounts[1], pageCounts[2], pageCounts[3]));
    std::vector<uint8_t> file(pageCounts.begin(), pageCounts.end());
    file.resize(4 + 2 * pages.size(), 0);
    for (size_t p = 0; p < pages.size(); ++p) {
        const size_t off = file.size();
        file[4 + 2 * p] = static_cast<uint8_t>(off & 0xFF);
        file[5 + 2 * p] = static_cast<uint8_t>(off >> 8);
        file.insert(file.end(), pages[p].begin(), pages[p].end());
        file.push_back(0);
    }
    if (file.size() > kHelpBuffer)
        throw mischief::FormatError(std::format("HELP.DAT: {} bytes; the game reads at most {} into its help buffer ({} too many)", file.size(),
                                      kHelpBuffer, file.size() - kHelpBuffer));
    return file;
}

void checkHelp(std::span<const uint8_t> helpDat, const DosFont& font, DosTextProblems& out) {
    if (helpDat.size() < 4) return;
    int total = 0;
    for (int t = 0; t < 4; ++t) total += helpDat[static_cast<size_t>(t)];
    for (int p = 0; p < total && 6 + 2 * static_cast<size_t>(p) <= helpDat.size(); ++p) {
        const size_t off = static_cast<size_t>(helpDat[4 + 2 * p] | (helpDat[5 + 2 * p] << 8));
        if (off >= helpDat.size()) continue;
        const auto nul = std::find(helpDat.begin() + static_cast<std::ptrdiff_t>(off), helpDat.end(), uint8_t{0});
        const auto text = helpDat.subspan(off, static_cast<size_t>(nul - helpDat.begin()) - off);
        if (auto m = checkDosMarkup(text); !m.empty()) out.errors.push_back(std::format("help page {}: {}", p, m));
        const DosTextExtent e = measureDosText(font, text, 0x0E, 0x25, 10);
        if (e.any && (e.left < 10 || e.right > 0x94 || e.top < 0x23 || e.bottom > 0xA8))
            out.warnings.push_back(std::format("help page {}: the text runs outside the page area (bytes {}-{}, rows {}-{}; the page "
                                               "is bytes 10-147, rows 35-167)",
                                               p, e.left, e.right - 1, e.top, e.bottom - 1));
        if (e.clipped > 0) out.warnings.push_back(std::format("help page {}: {} character(s) past the right edge are not drawn", p, e.clipped));
    }
}

}  // namespace tmport::dos
