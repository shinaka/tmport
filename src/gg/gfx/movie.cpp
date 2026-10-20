#include "gg/gfx/movie.h"

#include "gg/data/archive.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// The destination: a 16-bit offset into the A000 segment; the visible 64 000 bytes are stored.
struct Dest {
    Vram& vram;
    uint16_t di = 0;
    bool outside = false;
    void put(uint8_t v) {
        if (di < vram.size())
            vram[di] = v;
        else
            outside = true;
        ++di;
    }
    void skip(unsigned n) { di = static_cast<uint16_t>(di + n); }
};

}  // namespace

// 1399:0a85 Movie_DecodeFrame
MovieDecodeResult movieDecodeFrame(std::span<const uint8_t> src, Vram& vram) {
    MovieDecodeResult res;
    Dest d{vram};
    size_t s = 0;
    auto more = [&](size_t n) { return s + n <= src.size(); };
    for (;;) {
        if (!more(1)) break;
        const uint8_t c = src[s++];
        if (c == 0) {  // 00 n v: run
            if (!more(2)) break;
            const uint8_t n = src[s++], v = src[s++];
            if (v == 1) {
                d.skip(n);
            } else {
                for (unsigned i = 0; i < n; ++i) d.put(v);
            }
        } else if (c < 0x80) {  // literal
            if (!more(c)) break;
            for (unsigned i = 0; i < c; ++i) d.put(src[s++]);
        } else if (c > 0x80) {  // short skip
            d.skip(c - 0x80u);
        } else {  // 80 w
            if (!more(2)) break;
            const uint16_t w = static_cast<uint16_t>(src[s] | (src[s + 1] << 8));
            s += 2;
            if (w == 0) {
                res.complete = true;
                break;
            }
            if (w < 0x8000) {
                d.skip(w);
            } else if (((w >> 8) - 0x80) < 0x40) {  // SUB CH,80h; CMP CH,40h; JGE: a long literal
                const unsigned n = w & 0x3FFF;
                if (!more(n)) break;
                for (unsigned i = 0; i < n; ++i) d.put(src[s++]);
            } else {  // long run
                const unsigned n = w & 0x3FFF;
                if (!more(1)) break;
                const uint8_t v = src[s++];
                if (v == 1) {
                    d.skip(n);
                } else {
                    for (unsigned i = 0; i < n; ++i) d.put(v);
                }
            }
        }
    }
    if (!res.complete) log::debug("Movie_DecodeFrame: the frame data ends without its end code");
    if (d.outside) log::debug("Movie_DecodeFrame: writes past the visible screen were dropped");
    res.used = s;
    res.end = d.di;
    return res;
}

// 1473:07d9 Movie_Play, up to the loop: Res_Read(first)
MoviePlayer::MoviePlayer(const Archive& archive, uint16_t first, uint16_t end, int period,
                         std::span<const uint8_t> cueBytes, int bank)
    : archive_(archive), frame_(first), end_(end), period_(period), cues_(cueBytes), bank_(bank) {
    if (frame_ < end_) buffer_ = archive_.read(frame_);
}

std::optional<MovieCue> MoviePlayer::pendingCue() const {
    const size_t o = cueIndex_ * 8;
    if (o + 8 > cues_.size()) return std::nullopt;
    auto w = [&](size_t k) { return static_cast<int16_t>(cues_[o + 2 * k] | (cues_[o + 2 * k + 1] << 8)); };
    return MovieCue{w(0), w(1), w(2), w(3)};
}

// 1473:0802..08b2: one pass of the loop between Timer_Set(0, period) and Timer_Wait(0)
std::optional<MovieCue> MoviePlayer::step(Vram& vram) {
    if (finished()) return std::nullopt;
    last_ = movieDecodeFrame(buffer_, vram);
    const auto next = static_cast<uint16_t>(frame_ + 1);
    if (next != end_) buffer_ = archive_.read(next);
    std::optional<MovieCue> run;
    if (const auto cue = pendingCue(); cue && cue->frame == counter_) {
        ++cueIndex_;  // also for the types the switch ignores
        run = cue;
    }
    ++counter_;
    frame_ = next;
    return run;
}

}  // namespace tmport::gg
