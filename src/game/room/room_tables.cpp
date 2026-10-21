#include "game/room/room_tables.h"

#include <format>

#include "data/dgroup_offsets.h"
#include "data/exe_tables.h"
#include "mischief/util/error.h"

namespace tmport::game::room {

namespace {

template <class T, size_t N>
std::array<T, N> arr(const ExeTables& exe, uint32_t off) {
    std::array<T, N> out{};
    for (size_t i = 0; i < N; ++i) {
        const uint32_t o = off + static_cast<uint32_t>(i * sizeof(T));
        if constexpr (sizeof(T) == 1)
            out[i] = static_cast<T>(exe.u8(o));
        else
            out[i] = static_cast<T>(exe.u16(o));
    }
    return out;
}

std::vector<uint8_t> bytesOf(const ExeTables& exe, uint32_t off, size_t n) {
    const auto b = exe.bytes(off, n);
    return {b.begin(), b.end()};
}

// A behaviour script: i8 frames up to and including the -1 / -2 terminator, then i16 dx and y
// for every step before it (dgroup_map.md §4.18), optionally followed by i16 sfx.
ElfScript readScript(const ExeTables& exe, uint32_t framesOff, bool withSfx) {
    constexpr size_t kMaxFrames = 17;  // the longest scripts (behaviours 0, 1, 3) have 16 steps
    ElfScript s;
    for (size_t i = 0; i < kMaxFrames; ++i) {
        const int8_t f = exe.i8(framesOff + static_cast<uint32_t>(i));
        s.frames.push_back(f);
        if (f == -1 || f == -2) break;
    }
    if (s.frames.empty() || (s.frames.back() != -1 && s.frames.back() != -2))
        throw mischief::FormatError(std::format("elf script at DS:{:04x} has no terminator", framesOff));
    const size_t steps = s.frames.size() - 1;
    const uint32_t dxOff = framesOff + static_cast<uint32_t>(s.frames.size());
    const uint32_t yOff = dxOff + static_cast<uint32_t>(2 * steps);
    for (size_t i = 0; i < steps; ++i) {
        s.dx.push_back(exe.i16(dxOff + static_cast<uint32_t>(2 * i)));
        s.y.push_back(exe.i16(yOff + static_cast<uint32_t>(2 * i)));
    }
    if (withSfx) {
        const uint32_t sfxOff = yOff + static_cast<uint32_t>(2 * steps);
        for (size_t i = 0; i < steps; ++i) s.sfx.push_back(exe.i16(sfxOff + static_cast<uint32_t>(2 * i)));
    }
    return s;
}

}  // namespace

std::span<const uint8_t> untilNul(std::span<const uint8_t> b) {
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i] == 0) return b.first(i);
    return b;
}

RoomTables RoomTables::fromExe(const ExeTables& exe) {
    RoomTables t;
    t.netSwing = arr<uint8_t, 12>(exe, dg::kNetSwingFrames);
    t.emptySwing = arr<uint8_t, 12>(exe, dg::kEmptySwingFrames);
    t.coinToss = arr<uint8_t, 14>(exe, dg::kCoinTossFrames);
    t.coinFx = arr<uint8_t, 14>(exe, dg::kCoinFxFrames);
    t.door = arr<uint8_t, 3>(exe, dg::kDoorFrames);
    t.bump = arr<uint8_t, 13>(exe, dg::kBumpFrames);
    t.bumpDxRight = arr<int16_t, 13>(exe, dg::kBumpDxRight);
    t.bumpDxLeft = arr<int16_t, 13>(exe, dg::kBumpDxLeft);
    t.obstacleMinXRight = arr<int16_t, 7>(exe, dg::kObstacleMinXRight);
    t.obstacleMaxXLeft = arr<int16_t, 7>(exe, dg::kObstacleMaxXLeft);
    t.nextFrame = arr<uint8_t, kPlayerFrameSlots>(exe, dg::kNextFrame);
    t.dxRight = arr<int16_t, kPlayerFrameSlots>(exe, dg::kFrameDxRight);
    t.dxLeft = arr<int16_t, kPlayerFrameSlots>(exe, dg::kFrameDxLeft);
    t.frameY = arr<int16_t, kPlayerFrameSlots>(exe, dg::kFrameY);
    t.shopWalkX = arr<int16_t, 3>(exe, dg::kShopWalkX);

    t.gemsNeeded = arr<uint8_t, 4>(exe, dg::kGemsNeeded);
    for (size_t tier = 0; tier < 4; ++tier) {
        t.gemX[tier] = arr<int16_t, 5>(exe, dg::kGemX + static_cast<uint32_t>(10 * tier));
        t.gemY[tier] = arr<int16_t, 5>(exe, dg::kGemY + static_cast<uint32_t>(10 * tier));
    }
    {
        const auto p = exe.cstr(dg::kStrAnswerPrompt);
        t.answerPrompt.assign(p.begin(), p.end());
    }
    // F_SCOPY byte counts of 10e8:02db / 0359 / 04be (CX = 6 / 6 / 9).
    t.tmplCoins = bytesOf(exe, dg::kTmplCoins, 6);
    t.tmplNets = bytesOf(exe, dg::kTmplNets, 6);
    t.tmplPoints = bytesOf(exe, dg::kTmplPoints, 9);

    t.netSignX = arr<int16_t, 3>(exe, dg::kNetSignX);

    t.elfStartRoom = arr<uint8_t, 3>(exe, dg::kElfLevelStartRoom);
    t.elfScripts[0] = readScript(exe, dg::kElfBeh0Frames, false);
    t.elfScripts[1] = readScript(exe, dg::kElfBeh1Frames, false);
    t.elfScripts[2] = readScript(exe, dg::kElfBeh2Frames, false);
    t.elfScripts[3] = readScript(exe, dg::kElfBeh3Frames, false);
    t.elfScripts[4] = readScript(exe, dg::kElfBeh4Frames, true);
    t.elfNetFrames = arr<int8_t, 10>(exe, dg::kElfNetFrames);
    t.elfNetDx = arr<int16_t, 9>(exe, dg::kElfNetDx);
    t.elfNetY = arr<int16_t, 9>(exe, dg::kElfNetY);
    t.elfThrowDxRight = arr<int16_t, 5>(exe, dg::kElfThrowDxR);
    t.elfThrowDxLeft = arr<int16_t, 5>(exe, dg::kElfThrowDxL);
    t.elfThrowY = arr<int16_t, 5>(exe, dg::kElfThrowY);
    t.elfProjY = arr<int16_t, 9>(exe, dg::kElfProjY);

    t.hallPedX = arr<int16_t, 6>(exe, dg::kHallPedX);
    t.hallPedY = arr<int16_t, 6>(exe, dg::kHallPedY);
    t.hallPedFrameBase = arr<int16_t, 6>(exe, dg::kHallPedFrameBase);
    t.slotX = arr<int16_t, 3>(exe, dg::kSceneSlotX);
    t.revealX = arr<int16_t, 3>(exe, dg::kSceneRevealX);
    t.dissolveColOrder = arr<uint8_t, 16>(exe, dg::kDissolveColOrder);
    t.dissolveRowShift = arr<uint8_t, 16>(exe, dg::kDissolveRowShift);
    return t;
}

}  // namespace tmport::game::room
