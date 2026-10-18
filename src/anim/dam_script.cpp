#include "anim/dam_script.h"

#include <algorithm>
#include <format>

#include "util/byte_reader.h"
#include "util/error.h"

namespace tmport::anim {

size_t operandSize(uint8_t op) {
    switch (op) {
        case kOpUpdateRect: return 6;  // i16, u8, i16, u8
        case kOpLoop: return 3;        // i16, u8
        case kOpSfxStop: return 1;     // pad
        case kOpSfx: return 2;         // pad, n
        case kOpSprite: return 6;      // i16, i16, u8, u8
        default: return 0;             // FE, FF and the no-ops
    }
}

bool DamScript::hasDamTag() const {
    return bytes.size() >= 4 && bytes[0] == 'D' && bytes[1] == 'A' && bytes[2] == 'M' && bytes[3] == 0x1A;
}

bool DamScript::isDop() const { return bytes.size() >= 3 && bytes[0] == 'D' && bytes[1] == 'O' && bytes[2] == 'P'; }

ScriptPtr makeScript(int id, std::vector<uint8_t> bytes) {
    auto s = std::make_shared<DamScript>();
    s->id = id;
    s->bytes = std::move(bytes);
    return s;
}

ScriptPtr makeScript(int id, std::span<const uint8_t> bytes) {
    return makeScript(id, std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

std::vector<DamOp> disassemble(const DamScript& script) {
    if (!script.hasDamTag() && !script.isDop())
        throw FormatError(std::format("DAM script {}: bad tag", script.id));
    const ByteReader r(script.bytes, std::format("DAM script {}", script.id));
    std::vector<DamOp> ops;
    size_t p = kScriptStart;
    for (;;) {
        if (p >= r.size()) throw FormatError(std::format("DAM script {}: no FF before the end of the data", script.id));
        DamOp o;
        o.offset = p;
        o.op = r.u8(p);
        o.size = 1 + operandSize(o.op);
        if (!r.has(p, o.size))
            throw FormatError(std::format("DAM script {}: opcode {:02X} at {:#x} runs past the end", script.id, o.op, p));
        switch (o.op) {
            case kOpUpdateRect:
                o.x = r.i16(p + 1);
                o.y = r.u8(p + 3);
                o.w = r.i16(p + 4);
                o.h = r.u8(p + 6);
                break;
            case kOpLoop: {
                o.target = r.i16(p + 1);
                const uint8_t c = r.u8(p + 3);
                o.counter = c >> 7;
                o.count = c & 0x7F;
                break;
            }
            case kOpSfxStop: o.pad = r.u8(p + 1); break;
            case kOpSfx:
                o.pad = r.u8(p + 1);
                o.sound = r.u8(p + 2);
                break;
            case kOpSprite:
                o.x = r.i16(p + 1);
                o.y = r.i16(p + 3);
                o.frame = r.u8(p + 5);
                o.mode = r.u8(p + 6);
                break;
            default: break;
        }
        ops.push_back(o);
        p += o.size;
        if (o.op == kOpEnd) break;
    }
    return ops;
}

std::string formatOp(const DamOp& o) {
    switch (o.op) {
        case kOpUpdateRect: return std::format("{:04x}  F9 rect   dx={} dy={} w={} h={}", o.offset, o.x, o.y, o.w, o.h);
        case kOpLoop:
            return std::format("{:04x}  FA loop   target={:#x} counter={} count={}{}", o.offset, o.target, o.counter, o.count,
                               o.count == 0 ? " (forever)" : "");
        case kOpSfxStop: return std::format("{:04x}  FB sfx-stop pad={}", o.offset, o.pad);
        case kOpSfx: return std::format("{:04x}  FC sfx    pad={} n={} (SSND {})", o.offset, o.pad, o.sound, kSfxBase + o.sound);
        case kOpSprite:
            return std::format("{:04x}  FD sprite dx={} dy={} frame={} mode={}", o.offset, o.x, o.y, o.frame, o.mode);
        case kOpEndFrame: return std::format("{:04x}  FE end-of-tick", o.offset);
        case kOpEnd: return std::format("{:04x}  FF end", o.offset);
        default: return std::format("{:04x}  {:02X} no-op", o.offset, o.op);
    }
}

DamStats analyze(const DamScript& script) {
    const auto ops = disassemble(script);
    DamStats s;
    int perTick = 0;
    for (size_t i = 0; i < ops.size(); ++i) {
        const DamOp& o = ops[i];
        switch (o.op) {
            case kOpSprite:
                ++s.sprites;
                ++perTick;
                s.maxSpritesPerTick = std::max(s.maxSpritesPerTick, perTick);
                s.maxFrame = std::max(s.maxFrame, o.frame);
                if (o.mode != 0) s.nonZeroMode = true;
                break;
            case kOpEndFrame:
                ++s.endFrames;
                perTick = 0;
                break;
            case kOpEnd:
                ++s.ends;
                s.endOffset = o.offset;
                s.endsWithFeFf = i > 0 && ops[i - 1].op == kOpEndFrame;
                break;
            case kOpLoop:
                ++s.loops;
                if (o.count == 0) s.loopsForever = true;
                break;
            case kOpSfx: ++s.sounds; break;
            case kOpSfxStop: ++s.sfxStops; break;
            case kOpUpdateRect: ++s.updateRects; break;
            default: ++s.noOps; break;
        }
    }
    return s;
}

}  // namespace tmport::anim
