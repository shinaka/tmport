#pragma once
// Game text <-> editable UTF-8 text (OTHR .txt files and the strings of table overrides).
//
// Game text is raw bytes in the FFNT font's encoding (re/docs/fmt_othr_puzzles.md §3,
// fmt_bndl_ffnt.md §3.2/§4): printable ASCII, CR/LF/TAB, FE (back to the left margin), the
// key-cap glyphs 0x80..0x95 and control codes FF op ... FF. In the editable form:
//
//   printable ASCII     itself, except '{' which is written "{{"
//   CR (0D)             a line break (CR LF, LF or CR in the file all read back as one CR)
//   LF 0A / TAB 09      {lf} / {tab}   (a literal TAB character is accepted as 09, too)
//   FE                  {margin}
//   FF 02 FF            {ul}           underline on/off
//   FF 03 c FF          {color N}      pen colour EGA N (c = N + 1; 0 = black, 4 = dark red)
//   FF 05 FF            {center}       centring on/off
//   FF 06 text FF       {note text}    comment (the puzzle level tags "L=n")
//   FF 01 n FF          {arg n}        caller-supplied string n
//   FF 04 b.. FF        {tabs b ..}    tab stops (decimal bytes)
//   FF op b.. FF        {ff OP BB ..}  any other control code (hex bytes); {ff} is FF FF
//   0x80..0x95, 06, 17  key caps: {left} {right} {up} {down} {spacebar} {esc} {return}
//                       {backspace} {enter} {ctrl} {tab-key} ... (see kKeyCapNames)
//   any other byte      {xHH}
//   {end}               end of the text: anything after it (e.g. an editor's final newline)
//                       is ignored
//
// Typed non-ASCII characters are mapped to the font's Mac Roman positions 0x96..0xFD where the
// font has that character (e.g. "ñ" -> 0x96); the exporter itself only writes ASCII and escapes,
// so export -> import is byte-exact for every byte string without NUL.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::mods {

// Game bytes (no NUL) -> editable text (without "{end}").
std::string escapeGameText(std::span<const uint8_t> bytes);

// Editable text -> game bytes. Stops at "{end}". Throws ModSyntaxError naming line and column.
// firstLine: line number of text[0] in its file (for messages of strings embedded elsewhere).
std::vector<uint8_t> unescapeGameText(std::string_view text, int firstLine = 1);

// True if every byte has a plain or named form (no {xHH}, well-formed FF codes): such a byte
// string is exported as .txt; anything else stays a binary .bin.
bool isPlainGameText(std::span<const uint8_t> bytes);

// --- OTHR text resources -----------------------------------------------------------------------
// The bytes before the first NUL when the resource is a text resource laid out like the
// shipped ones (plain game text, NUL, zero padding to the NE size); nullopt otherwise.
std::optional<std::vector<uint8_t>> othrTextPayload(std::span<const uint8_t> resource);
// The .txt file content for a text payload ("{end}" and a newline appended).
std::string othrTextFile(std::span<const uint8_t> payload);
// A .txt file -> the resource body (text, NUL, zero padding to the NE size).
std::vector<uint8_t> othrBodyFromTextFile(std::string_view file);

// Key-cap glyph names (byte -> name), for documentation and tests.
struct KeyCapName {
    uint8_t byte;
    const char* name;
};
std::span<const KeyCapName> keyCapNames();

}  // namespace tmport::mods
