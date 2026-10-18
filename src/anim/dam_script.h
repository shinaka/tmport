#pragma once
// DAM animation scripts (OTHR 20000-20308 in TMT.DLL; re/docs/code_1098-10d8.md §8.1,
// fmt_othr_tables.md §4.1). A script is the raw resource: the 4-byte tag "DAM\x1A" (only "DOP"
// in the first three bytes is distinguished: no dirty tracking) followed by byte code that the
// interpreter (AnimEngine::step, 10b8:0312) walks in place. Operands are little-endian:
//
//   F9 i16 dx, u8 dy, i16 w, u8 h      queue an update (present) rectangle
//   FA i16 target, u8 c                loop: counter c>>7, count c&0x7F (0 = forever), jump to base+target
//   FB u8 pad                          stop the sound effect
//   FC u8 pad, u8 n                    play sound effect SSND 7000+n
//   FD i16 dx, i16 dy, u8 frame, u8 mode   queue a sprite from frame slot frameBase+frame
//   FE                                 end of this tick for the channel
//   FF                                 end of the script (onEnd, channel freed)
//   00..F8                             1-byte no-ops
//
// This header also has a linear disassembler for tools and tests.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tmport::anim {

inline constexpr uint8_t kOpUpdateRect = 0xF9;
inline constexpr uint8_t kOpLoop = 0xFA;
inline constexpr uint8_t kOpSfxStop = 0xFB;
inline constexpr uint8_t kOpSfx = 0xFC;
inline constexpr uint8_t kOpSprite = 0xFD;
inline constexpr uint8_t kOpEndFrame = 0xFE;
inline constexpr uint8_t kOpEnd = 0xFF;

inline constexpr size_t kScriptStart = 4;  // execution starts after the tag (10b8:0119)
inline constexpr int kSfxBase = 7000;       // FC plays SSND 7000+n (1170:0000)

// Operand bytes that follow each opcode (0 for FE/FF and for the 1-byte no-ops).
size_t operandSize(uint8_t op);

// A loaded script. Channels hold it by shared pointer; Anim_IsRunning / Anim_Stop compare the
// script object (the original compares far pointers), so identity matters, not content.
struct DamScript {
    int id = 0;                  // resource id (diagnostics only)
    std::vector<uint8_t> bytes;  // the whole resource body (tag included, NE padding allowed)

    // "DAM\x1A" at offset 0.
    bool hasDamTag() const;
    // The first three bytes are "DOP": sprites and erasures bypass dirty tracking (10b8:05a6).
    bool isDop() const;
};
using ScriptPtr = std::shared_ptr<const DamScript>;

ScriptPtr makeScript(int id, std::vector<uint8_t> bytes);
ScriptPtr makeScript(int id, std::span<const uint8_t> bytes);

// One instruction of a linear walk.
struct DamOp {
    size_t offset = 0;  // of the opcode byte
    uint8_t op = 0;     // opcode byte; anything below 0xF9 is a 1-byte no-op
    size_t size = 1;    // opcode + operands
    // FD: x = dx, y = dy, frame, mode.  F9: x = dx, y = dy (u8), w, h (u8).
    int x = 0, y = 0, w = 0, h = 0;
    int frame = 0, mode = 0;
    // FA: target offset from the script base, counter index (c >> 7), count (c & 0x7F; 0 = forever).
    int target = 0, counter = 0, count = 0;
    // FC / FB: pad byte (ignored by the interpreter) and FC's sound number.
    int pad = 0, sound = 0;
};

// Linear decode from offset 4 up to and including the first FF. Throws tmport::FormatError if
// the tag is not "DAM\x1A"/"DOP", if an operand runs past the end, or if no FF is found.
std::vector<DamOp> disassemble(const DamScript& script);
// One line of text, e.g. "0004  FD sprite dx=0 dy=0 frame=1 mode=0".
std::string formatOp(const DamOp& op);

// Static facts about a script (linear walk; see disassemble).
struct DamStats {
    int sprites = 0, endFrames = 0, ends = 0, loops = 0, sounds = 0, sfxStops = 0, updateRects = 0, noOps = 0;
    int maxFrame = -1;             // highest FD frame byte
    int maxSpritesPerTick = 0;     // FDs between two FEs
    bool loopsForever = false;     // an FA with count 0
    bool endsWithFeFf = false;     // ... FE FF: the END runs in a tick of its own (last frame erased)
    bool nonZeroMode = false;      // an FD with mode != 0 (mirror / stamp)
    size_t endOffset = 0;          // offset of the first FF
};
DamStats analyze(const DamScript& script);

}  // namespace tmport::anim
