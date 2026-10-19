#pragma once
// Game text of the 1990 DOS edition <-> editable UTF-8 text (docs/MODDING.md, "The 1990 DOS
// edition"), and the checks the mod layer runs on it with the semantics of Text_Draw 43cb:0b63
// (re/dos/docs/code_ui.md §3.1, fmt_data.md §6).
//
// DOS text is 7-bit: printable ASCII, CR / LF (new line), the markup Text_Draw interprets ('~XXXYYY'
// move, '`' highlight toggle, '|NN' colour, TAB NN indent) and the font's pictograms 0x01..0x1F.
// In the editable form:
//
//   printable ASCII      itself, except '{' which is written "{{"
//   CR (0D)              a line break (Lines form: CR LF, LF or CR in the file all read as one CR;
//                        Json form: the character CR, written "\r" by the JSON writer; {cr}
//                        where a line cannot break)
//   LF (0A), TAB (09)    {lf}, {tab} (Json form: the characters LF / TAB, "\n" / "\t" in JSON)
//   key pictograms       {up} 1B, {down} 1C, {right} 1D, {left} 1E, {enter} 05, {spacebar} 07 06,
//                        {menu} 02, {crown} 1A
//   any other byte       {xHH} (not 00)
//
// The markup characters ~ ` | stay as they are: they mean the same in the file as in the game.
// export -> import is byte-exact for every byte string without NUL.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::dos {

class DosFont;

enum class DosTextForm {
    Lines,  // .txt files: line breaks are CR
    Json,   // JSON string values: CR, LF and TAB are characters of the string
};

// Game bytes (no NUL) -> editable text.
std::string escapeDosText(std::span<const uint8_t> bytes, DosTextForm form);

// Editable text -> game bytes. Throws mods::ModSyntaxError naming the line (counted from
// firstLine) and column; NUL, a '{' without a known name and characters outside ASCII are errors
// (the DOS font has 128 glyphs, no accented letters).
std::vector<uint8_t> unescapeDosText(std::string_view text, DosTextForm form, int firstLine = 1);

// The pictogram names (documentation, tests).
struct DosGlyphName {
    const char* name;
    uint8_t first;
    int second;  // -1: one byte
};
std::span<const DosGlyphName> dosGlyphNames();

// The markup of Text_Draw: '~' needs six digits, '|' and TAB two (the original reads them as
// digits without checking). Returns an empty string when the text is well formed, else what is
// wrong and where ("'~' at byte 12 is not followed by six digits").
std::string checkDosMarkup(std::span<const uint8_t> text);

// Where Text_Draw would draw a text starting at (x bytes, y rows) with a line height: the box of
// the glyphs drawn (bytes, rows; right/bottom exclusive; blanks do not count), the number of lines
// (Text_Draw's return value) and the glyphs it skips because they start at x >= 0x9A.
struct DosTextExtent {
    bool any = false;  // at least one non-blank glyph drawn
    int left = 0, top = 0, right = 0, bottom = 0;
    int lines = 1;
    int clipped = 0;
};
DosTextExtent measureDosText(const DosFont& font, std::span<const uint8_t> text, int x, int y, int lineHeight);

// Text_Width 43cb:0cfc: the advances (bytes) of every byte & 0x7F up to the end or a CR (control
// bytes included, LF too).
int dosTextWidth(const DosFont& font, std::span<const uint8_t> text);

// The lines of a text split at CR and LF (MsgBox_Draw's split), without their terminators.
std::vector<std::span<const uint8_t>> dosTextLines(std::span<const uint8_t> text);

}  // namespace tmport::dos
