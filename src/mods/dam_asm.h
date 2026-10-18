#pragma once
// Text assembler and disassembler for DAM animation scripts (OTHR resources that start with
// "DAM\x1A"; byte code in re/docs/code_1098-10d8.md §8.1 and anim/dam_script.h).
//
// Syntax: one statement per line, ';' starts a comment, "name:" defines a label. Numbers are
// decimal or 0x-prefixed hex, negative values allowed for signed fields.
//
//   tag DAM | tag DOP | tag b0 b1 b2 b3     the 4-byte header (must come first; DOP = no dirty
//                                          tracking for the channel)
//   sprite dx dy frame [mode]              FD i16 dx, i16 dy, u8 frame, u8 mode (mode 0 default)
//   tick                                   FE  end of this channel's tick
//   end                                    FF  end of the script (frees the channel)
//   loop target count [counter]            FA i16 target, u8 (counter << 7 | count);
//                                          target = label or offset from the script start,
//                                          count 1..127 or "forever" (0), counter 0 or 1
//   sound n [pad]                          FC u8 pad, u8 n: sound effect SSND 7000+n
//   stopsound [pad]                        FB u8 pad
//   update dx dy w h                       F9 i16 dx, u8 dy, i16 w, u8 h (present rectangle)
//   byte b ...                             raw bytes (00..F8 are 1-byte no-ops when executed)
//   size n                                 the body is zero-padded to n bytes (default: the
//                                          next multiple of 512, like an NE resource)
//
// The disassembler writes labels for loop targets, the original offsets as comments, and the
// bytes after the first END (never executed, kept so that import(export(x)) == x).

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tmport::mods {

struct DamAssembly {
    std::vector<uint8_t> bytes;          // tag + byte code, unpadded
    size_t paddedSize = 0;               // from "size", else roundUpNe(bytes.size())
    std::vector<std::string> warnings;   // "line N: ..." for questionable but valid scripts
    int endCount = 0;                    // FF statements
    int maxFrame = -1;                   // highest sprite frame
    std::vector<int> sounds;             // sound numbers used (7000 + n)
};

// Assembles a script. Throws ModSyntaxError (line/column) for syntax errors and for scripts the
// interpreter cannot run: missing tag, no "end" and no endless loop, a loop target outside the
// script, a backward loop whose body has no "tick" (Anim_Step would hang).
DamAssembly assembleDam(std::string_view text);

// The resource body (assembled and padded).
std::vector<uint8_t> damBodyFromText(std::string_view text);

// True if the resource starts with "DAM\x1A" or "DOP" (what the exporter writes as .dam.txt).
bool isDamResource(std::span<const uint8_t> resource);

// Full disassembly of a resource body (with NE padding). id is used for the header comment.
std::string disassembleDam(std::span<const uint8_t> resource, int id);

}  // namespace tmport::mods
