#pragma once
// The player-visible strings and the look palettes of SST.EXE (1990 DOS edition) for mods
// (docs/MODDING.md, "The 1990 DOS edition"): EXE/strings.json and EXE/palettes.json, written into
// the initialised DGROUP of the image before anything reads it (DosExe::writeDs), so dsString(), the
// text drawing, the tables and the writable copies of the game state all see them.
//
// Every string is keyed by its DS offset in V 1.0. Two kinds of place (re/dos/docs/fmt_data.md §7.2,
// code_ui.md §14):
//   * fixed: the code addresses the string with an immediate offset (the champion banner, button
//     labels, rank names, sign-in and title texts, the two champion messages). A new text must fit
//     the original's bytes (`max`).
//   * pooled: the string is reached only through far pointers in DGROUP tables (box titles and
//     texts DS:59cc, footers DS:5256, help titles DS:52ee, clue words DS:5eb2, sign-in headings
//     DS:6255, Flutter's messages DS:7ca3). The strings of a pool may grow and shrink as long as
//     all of them fit the pool's bytes: a changed pool is packed again in the original order and its
//     pointers rewritten (strings no table points at are dropped).
// Layout rules the code relies on are checked (dos_exe_strings.cpp): the item count of the drop-down
// menus, message boxes that stay on the screen (MsgBox_Draw's geometry), the champion's name bytes
// of message 35 (written at a fixed address, DS:7bb2), the patched count and plural letter of the
// rank and end messages (DS:7d4d / 7d65), and the markup of Text_Draw. Text that would be drawn
// outside its box, button, panel or Flutter's cloud gives a warning.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::dos {

class DosExe;
class DosFont;

enum class DosStringRole {
    MenuItems, BoxTitle, BoxText, Footer, HelpTitle, ClueWord, SigninHeading, Hint, CrownMessage,
    Champion, ButtonSmall, ButtonWide, RankName, NewPlayer, KeyLine, TitlePrompt, TitlePage,
};

struct DosStringEntry {
    uint16_t ds = 0;                 // where the string is in V 1.0 (the key)
    DosStringRole role = DosStringRole::BoxText;
    int index = 0;                   // box, hint, word, rank, button ... number
    std::string what;                // a description (no game text)
    int pool = -1;                   // index into pools(); -1 = a fixed place
    size_t max = 0;                  // fixed places: the longest text that fits
    std::vector<uint16_t> pointers;  // DS offsets of the far pointers to it (pooled strings)
    std::vector<uint8_t> text;       // the text in the EXE
};

struct DosStringPool {
    std::string name;
    uint16_t start = 0, end = 0;  // DS range
    size_t used = 0;              // bytes its strings take when packed
};

class DosStringCatalogue {
public:
    // Reads the tables of V 1.0. Throws tmport::FormatError if a pointer does not point into DGROUP.
    static DosStringCatalogue read(const DosExe& exe);
    const std::vector<DosStringEntry>& entries() const { return entries_; }
    const std::vector<DosStringPool>& pools() const { return pools_; }
    const DosStringEntry* find(uint16_t ds) const;

private:
    std::vector<DosStringEntry> entries_;
    std::vector<DosStringPool> pools_;
};

// The position of the treasure count and of its plural letter that Flutter's rank and end messages
// get patched with (DS:7d4d / DS:7d65 through DS:7d45 / 7d75); -1 = none.
struct DosMessagePatch {
    int countAt = -1;
    int pluralAt = -1;
    bool hasPlural = false;  // rank messages have a plural position, end messages not
};
std::map<int, DosMessagePatch> dosMessagePatches(const DosExe& exe);

// EXE/strings.json of the EXE.
std::string exportStringsJson(const DosExe& exe);

struct DosStringEdit {
    std::optional<std::vector<uint8_t>> text;
    std::optional<int> countAt, pluralAt;
};
using DosStringEdits = std::map<uint16_t, DosStringEdit>;
// Reads a strings.json into `edits` (a later file overrides an earlier one per field). Keys are
// "DS:XXXX" (or "XXXX"); in an entry only "text", "countAt" and "pluralAt" are read. Throws
// tmport::FormatError naming the key (nothing of the file is used then).
void parseStringsJson(std::string_view json, const DosStringCatalogue& catalogue, DosStringEdits& edits);

// What the layout checks know of the pictures (from the effective SPRITES.PAK).
struct DosStringLayout {
    const DosFont* font = nullptr;
    int clueBoxRight = 0;  // right edge (bytes) of the HUD's clue box, SPRITES frame 58; 0 = unknown
    int cloudLeft = 0, cloudTop = 0, cloudRight = 0, cloudBottom = 0;  // Flutter's cloud, frame 71; right 0 = unknown
};

struct DosStringsResult {
    size_t changed = 0;                // strings and patch positions written
    std::vector<std::string> errors;   // edits refused (the original text stays)
    std::vector<std::string> warnings;
};
// Checks and writes the edits. An edit that breaks a rule is refused with an error and the original
// stays; a pool that overflows refuses all edits of that pool.
DosStringsResult applyStringEdits(DosExe& exe, const DosStringCatalogue& catalogue, const DosStringEdits& edits,
                                  const DosStringLayout& layout);

// ---- EXE/palettes.json --------------------------------------------------------------------------
// {"ega": 17 attribute registers (RGBI bits, 0-63; the 17th is the border), "tandy": 16 palette
//  registers (IRGB, 0-15), "mcga": 16 [r, g, b] (6-bit DAC values), "mcgaModeSet": the 16 colours of
//  the mode set (replaced right away, never visible), "cga": 16 dither bytes (four 2-bit pixels)}.
std::string exportPalettesJson(const DosExe& exe);
// Checks the whole document, then writes it. A key may be left out; null keeps an element. Throws
// tmport::FormatError naming the place (nothing is written then). Returns the bytes changed.
size_t applyPalettesJson(DosExe& exe, std::string_view json);

}  // namespace tmport::dos
