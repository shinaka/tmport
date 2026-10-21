#pragma once
// The sound files of the 1990 DOS edition (TUNES.IBM, SFX.IBM, TUNES.TAN, SFX.TAN;
// re/dos/docs/fmt_sound.md §4) <-> an editable note text (docs/MODDING.md, "Sound files"):
//
//   tune 0                     an entry: "tune N" in TUNES.*, "effect N" in SFX.*, in order
//     voice 0                  .TAN files: four voices per entry (3 = the Tandy noise channel)
//       tempo 112              commands: tempo N (ticks per whole note / 4), tempo+ N, tempo- N
//       release 6              (percent), release N (0 = legato), legato, rawdur N, envelope N
//       envelope 3             (Tandy, 0-35), sustain N (Tandy, 0-15), cmd K N (ignored codes)
//       C4 1/4                 a note: C C# D D# E F F# G G# A A# B, octave 1-16 (C4 = middle C),
//       D#5 1/8. tie           duration 1 1/2 1/4 ... 1/128 or "full", "." dotted, "tie"
//       rest 1/2               a rest
//       raw 3 120              a raw-pitch step: octave code 0-15 as stored, value 0-255
//       bytes 0x3D 0x81        any other two-byte event, as stored
//       end                    the end of the voice (required)
//       pad 0x00 0x13          bytes after the voice that no entry uses (kept for an exact copy)
//   slots 0x0096 0x00A4 ...    the stale header words after the entries (never read by the game)
//
// A word starting with '#' starts a comment (in "C#4" it is the sharp). Streams are laid out in the order written, right after the fixed header
// (0x96 bytes for .IBM, 0x230 for .TAN), so export -> import is byte-exact.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dos/data/file_table.h"

namespace tmport::dos {

struct DosSoundFormat {
    bool tandy = false;      // .TAN: 4 voices per entry
    bool tunes = false;      // TUNES.*: tunes, else effects
    int entries = 0;         // entries the game plays (3 tunes, 12 effects)
    int voices = 1;
    size_t headerBytes = 0;  // 0x96 / 0x230
    size_t bufferBytes = 0;  // the driver's static buffer (SFX 1712, TUNES 5056)
};
// The format of one of the four sound files (FileIndex::SfxIbm .. TunesTan).
DosSoundFormat dosSoundFormat(FileIndex index);
bool isDosSoundFile(FileIndex index);

// File -> text. Throws mischief::FormatError for a file whose streams overlap or are out of order
// (none of V 1.0).
std::string soundFileToText(std::span<const uint8_t> file, FileIndex index, std::string_view name);
// Text -> file. Throws mods::ModSyntaxError (line and what is wrong) or mischief::FormatError (an
// entry or voice missing, the file larger than the driver's buffer).
std::vector<uint8_t> soundFileFromText(std::string_view text, FileIndex index, std::string_view name);

}  // namespace tmport::dos
