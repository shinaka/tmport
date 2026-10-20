#include "gg/gfx/transitions.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/gfx/video.h"
#include "rtl/borland_rand.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr int kFirstRow = 9;               // the wipe covers rows 9..164
constexpr long kVramBase = kFirstRow * 320; // A000:0B40

// One span of screen row (9 + r) to VRAM, linear as the original's memcpy (a frontier left of 0
// reads and writes the end of the row above); bytes outside the buffers are skipped.
void copySpan(const Screen& screen, Vram& vram, int r, int from, int count) {
    const long rowOff = static_cast<long>(r) * Screen::kWidth;
    const auto src = screen.bytes();
    bool outside = false;
    for (int i = 0; i < count; ++i) {
        const long s = kVramBase + rowOff + from + i;  // the source is row 9 of the buffer + the same offset
        if (s < 0 || s >= static_cast<long>(vram.size()) || s >= static_cast<long>(src.size())) {
            outside = true;
            continue;
        }
        vram[static_cast<size_t>(s)] = src[static_cast<size_t>(s)];
    }
    if (outside) log::debug("Screen_Wipe: row {} span {}+{} reaches outside the screen", r, from, count);
}

}  // namespace

WipePattern WipePattern::parse(std::span<const uint8_t> p) {
    if (p.size() < kSize) throw FormatError(std::format("wipe pattern: {} bytes, expected {}", p.size(), kSize));
    auto u16 = [&](size_t o) { return static_cast<uint16_t>(p[o] | (p[o + 1] << 8)); };
    WipePattern w;
    w.unused = u16(0x000);
    w.period = u16(0x002);
    w.leftEnabled = u16(0x004);
    w.rightEnabled = u16(0x13E);
    for (size_t r = 0; r < kRows; ++r) {
        w.left[r] = static_cast<int16_t>(u16(0x006 + 2 * r));
        w.right[r] = static_cast<int16_t>(u16(0x140 + 2 * r));
    }
    w.leftStep = static_cast<int16_t>(u16(0x278));
    w.rightStep = static_cast<int16_t>(u16(0x27A));
    return w;
}

// 1473:0042 Screen_RandomWipe (the choice)
uint16_t randomWipePattern(BorlandRand& rng) { return static_cast<uint16_t>(kWipePatternFirst + rng.random(8)); }

// 1473:0479 Screen_Wipe, up to the loop
Wipe::Wipe(Screen& screen, const WipePattern& pattern, BorlandRand& rng) : pattern_(pattern) {
    screen.hideMouseOnce();
    for (size_t r = 0; r < WipePattern::kRows; ++r) {
        leftDone_[r] = 0;
        rightDone_[r] = Screen::kWidth;
        l_[r] = pattern.left[r];
        r_[r] = pattern.right[r];
    }
    sound_ = kWipeSoundBase + rng.random(2);  // rand() * 2 / 0x8000: CWD, SHL/RCL, F_LDIV
}

// 1473:0540..0729: one pass over the rows
void Wipe::step(const Screen& screen, Vram& vram) {
    for (int r = 0; r < WipePattern::kRows; ++r) {
        const auto i = static_cast<size_t>(r);
        if (done_[i]) continue;
        l_[i] = static_cast<int16_t>(l_[i] + pattern_.leftStep);
        r_[i] = static_cast<int16_t>(r_[i] + pattern_.rightStep);
        if (l_[i] > r_[i]) {
            done_[i] = true;
            l_[i] = r_[i];
        }
        if (r_[i] < l_[i]) {
            done_[i] = true;
            r_[i] = l_[i];
        }
        if (done_[i]) ++finished_;
        if (pattern_.leftEnabled) {
            const int n = static_cast<int16_t>(l_[i] - leftDone_[i]);
            if (n > 0) {
                copySpan(screen, vram, r, leftDone_[i], n);
                leftDone_[i] = l_[i];
            }
        }
        if (pattern_.rightEnabled) {
            const int n = static_cast<int16_t>(rightDone_[i] - r_[i]);
            if (n > 0) {
                copySpan(screen, vram, r, r_[i], n);
                rightDone_[i] = r_[i];
            }
        }
    }
    ++steps_;
}

// 1473:045a Screen_Show
ScreenShow::ScreenShow(Video& video, const Archive& archive, BorlandRand& rng) {
    if (video.palette().faded()) {
        fading_ = true;
        video.flush();
        fade_ = video.palette().fadeIn();
        return;
    }
    pattern_ = randomWipePattern(rng);  // Res_Read(0x11D6 + k)
    wipe_.emplace(video.screen(), WipePattern::parse(archive.read(pattern_)), rng);
}

void ScreenShow::finish(Video& video) {
    if (fading_) return;
    wipe_->end(video.screen());
    video.screen().dirty().clear();  // 1581:0470 Dirty_Clear: no flush
}

}  // namespace tmport::gg
