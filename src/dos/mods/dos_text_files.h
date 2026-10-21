#pragma once
// The text files of the 1990 DOS edition as editable UTF-8 text (docs/MODDING.md, "The 1990 DOS
// edition"): the elf questions (ALL_PUZS, compressed with the word table PRG_DICT) and the help
// pages (HELP.DAT). Text codes: dos/mods/dos_text_codec.h (Lines form). Specs:
// re/dos/docs/fmt_data.md §2, §3, code_ui.md §6.1, §10.
//
//   ALL_PUZS.txt   === question N           the 360 questions in order, expanded (no word codes)
//                  <question lines>         as drawn; the first line starts with the game's two
//                  --- answers              indent spaces; an empty last line = the question ends
//                  <correct answer>         with a line break
//                  <wrong answer>
//                  <wrong answer>
//                  --- after <codes>        optional: bytes after the third answer (never shown)
//                  --- no final break       optional: the third answer has no line break
//   PRG_DICT.txt   N word                   one entry per line, 0..127 are the words ALL_PUZS
//                                           uses; at most 15 characters
//   HELP.DAT.txt   === topic T page P       the 15 pages in order (topics = the Help menu items)
//                  <page lines>
//
// '#' lines before the first '===' are comments. Questions are compressed again with the word
// table the way the original was (a word is replaced where it stands as a whole token: delimited
// by characters other than letters, digits, ' ` - _), so export -> import is byte-exact.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tmport::dos {

class Dictionary;
class DosExe;
class DosFont;
class PuzzleFile;

// ---- ALL_PUZS ----------------------------------------------------------------------------------

std::string puzzlesToText(const PuzzleFile& puzzles, const Dictionary& dict, const DosExe& exe);
// The 360 expanded questions (question, '~', three answers ended by CR, the bytes after them).
// Throws mods::ModSyntaxError.
std::vector<std::vector<uint8_t>> puzzlesFromText(std::string_view text);
// One expanded question compressed with the word table (entries 0..127). Throws mischief::FormatError
// for a byte >= 0x80 (it would read as a word code).
std::vector<uint8_t> compressPuzzle(std::span<const uint8_t> expanded, const Dictionary& dict);
// The file: 360 offsets, then the compressed questions. Throws mischief::FormatError (count, 64 KB).
std::vector<uint8_t> buildAllPuzs(const std::vector<std::vector<uint8_t>>& expanded, const Dictionary& dict);
// The checks of the quiz code on expanded questions (code_ui.md §10): a '~', three answers of at
// most 34 bytes (DS:a22a holds 35 per answer), a question of at most 999 bytes (DS:a293), markup,
// and the nine largest questions of every tier together fit DS:a6b8..b1f7 (errors); a question that
// runs into the answers or an answer clipped at the screen edge (warnings). `tiers` are the
// [first, end) question ranges of the tiers the game plays (QuizTiers: 0-3 and 5).
struct DosTextProblems {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};
void checkPuzzles(const std::vector<std::vector<uint8_t>>& expanded, std::span<const std::pair<int, int>> tiers, const DosFont& font,
                  DosTextProblems& out);

// ---- PRG_DICT ----------------------------------------------------------------------------------

std::string dictionaryToText(std::span<const uint8_t> prgDict);
// Throws mods::ModSyntaxError (order, length, 128..208 entries).
std::vector<uint8_t> dictionaryFromText(std::string_view text);

// ---- HELP.DAT ----------------------------------------------------------------------------------

std::string helpToText(std::span<const uint8_t> helpDat);
// `pageCounts`: the pages per topic the file must have (those of the original: the page picture
// tables DS:5bed.. are fixed per page). Throws mods::ModSyntaxError / mischief::FormatError (0xED8
// bytes at most: the size of the game's buffer DS:9325).
std::vector<uint8_t> helpFromText(std::string_view text, std::span<const uint8_t> pageCounts);
// Markup errors; text outside the page area (x 10..0x93 bytes, y 0x23..0xA7) as warnings.
void checkHelp(std::span<const uint8_t> helpDat, const DosFont& font, DosTextProblems& out);

}  // namespace tmport::dos
