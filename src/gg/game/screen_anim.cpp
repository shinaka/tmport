#include "gg/game/screen_anim.h"

#include <algorithm>
#include <format>

#include "gg/game/pic_cache.h"
#include "gg/gfx/screen.h"
#include "rtl/borland_rand.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr size_t kHeaderSize = 0x32;  // the record up to the sequence pointers
constexpr size_t kSeqHeader = 10;
constexpr size_t kStepSize = 10;

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

// Obj_SetSizeFromPicture (15df:0414): w, h from the LPX header (+6, +8).
void sizeFromPicture(Sprite& s) {
    const auto& p = s.picture;
    s.w = p.size() >= 10 ? (p[6] | (p[7] << 8)) : 0;
    s.h = p.size() >= 10 ? (p[8] | (p[9] << 8)) : 0;
}

}  // namespace

ScreenAnim ScreenAnim::parse(std::span<const uint8_t> p, std::vector<std::span<const uint8_t>> pictures) {
    if (p.size() < kHeaderSize) throw FormatError(std::format("screen animation: {} bytes", p.size()));
    ScreenAnim a;
    a.frameBundle_ = static_cast<uint16_t>(s16(p, 0x18));
    a.baseX_ = s16(p, 0x1E);
    a.baseY_ = s16(p, 0x20);
    a.wrapMin_ = s16(p, 0x28);
    a.wrapMax_ = s16(p, 0x2A);
    const int n = s16(p, 0x2C);
    if (n <= 0 || n > 256) throw FormatError(std::format("screen animation: {} sequences", n));
    size_t o = kHeaderSize + 4 * static_cast<size_t>(n);
    for (int i = 0; i < n; ++i) {
        if (o + kSeqHeader > p.size()) throw FormatError("screen animation: a sequence header past the end");
        AnimSequence seq;
        seq.delay = s16(p, o);
        seq.dir = s16(p, o + 2);
        const int count = s16(p, o + 8);
        if (count < 0 || o + kSeqHeader + kStepSize * static_cast<size_t>(count) > p.size())
            throw FormatError("screen animation: steps past the end");
        o += kSeqHeader;
        for (int k = 0; k < count; ++k, o += kStepSize)
            seq.steps.push_back({s16(p, o), s16(p, o + 2), s16(p, o + 4), static_cast<uint16_t>(s16(p, o + 6)),
                                 static_cast<uint16_t>(s16(p, o + 8))});
        a.sequences_.push_back(std::move(seq));
    }
    // 174b:0324: sequence 0, countdown 0, delay = the first sequence's word 0, state 0, step 0.
    a.delay_ = a.sequences_.front().delay;
    a.pictures_ = std::move(pictures);
    return a;
}

// 174b:0324 ScreenAnim_Load
ScreenAnim ScreenAnim::load(PicCache& cache, uint16_t id) {
    const auto bytes = cache.archive().read(id);  // Res_LoadCopy
    const uint16_t bundle = static_cast<uint16_t>(s16(bytes, 0x18));
    ScreenAnim a = parse(bytes, cache.loadBundle(bundle));
    a.id_ = id;
    return a;
}

// 174b:030b ScreenAnim_SetSequence
void ScreenAnim::setSequence(int seq) {
    seq_ = seq;
    step_ = 0;
    state_ = 0;
}

void ScreenAnim::showFrame(const AnimStep& s) {
    sprite_.flags = s.c;
    sprite_.x = baseX_ + s.a;
    sprite_.y = baseY_ + s.b;
    sprite_.picture = s.op >= 0 && static_cast<size_t>(s.op) < pictures_.size() ? pictures_[static_cast<size_t>(s.op)]
                                                                                   : std::span<const uint8_t>{};
    sizeFromPicture(sprite_);
}

// 174b:001a ScreenAnim_Step (op table CS:024d, 14 entries)
void ScreenAnim::step(BorlandRand& rng, const ScreenAnimSound& sound) {
    const int left = static_cast<int16_t>(countdown_) - 1;  // DEC, JL
    countdown_ = static_cast<int16_t>(left);
    if (left >= 0) return;
    countdown_ = delay_;
    if (seq_ < 0 || static_cast<size_t>(seq_) >= sequences_.size()) return;
    const auto& steps = sequences_[static_cast<size_t>(seq_)].steps;
    int i = step_;
    for (int guard = 0;; ++guard) {
        if (i < 0 || static_cast<size_t>(i) >= steps.size() || guard > 4096) {
            // The original reads past the steps (or loops for ever); no shipped set gets here.
            log::debug("ScreenAnim_Step {:04x}: step {} outside sequence {}", id_, i, seq_);
            break;
        }
        const AnimStep& s = steps[static_cast<size_t>(i)];
        if (s.op >= 0) {  // a frame
            showFrame(s);
            ++i;
            break;
        }
        bool stop = false;
        switch (s.op) {
        case -1000: state_ = kFinished; stop = true; break;  // step kept
        case -45:  // busy-waits while the effect plays
            if (sound.isPlaying)
                while (sound.isPlaying(s.a)) {
                }
            ++i;
            break;
        case -39:
            countdown_ = static_cast<int16_t>(countdown_ + s.a * 0x92);
            sprite_.flags |= kHidden;
            ++i;
            stop = true;
            break;
        case -37: if (sound.stop) sound.stop(s.a); ++i; break;
        case -36: if (sound.play) sound.play(s.a, s.b >> 8, s.b & 0xFF); ++i; break;
        case -35: baseY_ = static_cast<int16_t>(rng.random(s.b - s.a) + s.a); ++i; break;
        case -34: baseX_ = static_cast<int16_t>(rng.random(s.b - s.a) + s.a); ++i; break;
        case -15: baseY_ = s.a; ++i; break;
        case -14: baseX_ = s.a; ++i; break;
        case -12:
            baseX_ = static_cast<int16_t>(baseX_ + s.a);
            if (baseX_ < wrapMin_) baseX_ = wrapMax_;
            if (baseX_ > wrapMax_) baseX_ = wrapMin_;
            ++i;
            break;
        case -8: sprite_.flags |= kHidden; ++i; stop = true; break;
        case -4: i = s.a; break;
        case -2: state_ = kHolding; stop = true; break;  // the step repeats every frame
        case -1: i = 0; break;
        default:
            // An unknown command re-reads the same step for ever in the original (code_world.md
            // §20 bug 1); the engine skips it.
            log::debug("ScreenAnim_Step {:04x}: unknown op {} at step {}", id_, s.op, i);
            ++i;
            break;
        }
        if (stop) break;
    }
    step_ = i;
}

bool ScreenAnim::restore(int seq, int step, int countdown, int baseX, int baseY, int state) {
    seq_ = seq;
    step_ = step;
    countdown_ = countdown;
    baseX_ = baseX;
    baseY_ = baseY;
    state_ = state;
    if (seq < 0 || static_cast<size_t>(seq) >= sequences_.size()) return false;
    const auto& steps = sequences_[static_cast<size_t>(seq)].steps;
    for (int k = std::min(step, static_cast<int>(steps.size())) - 1; k >= 0; --k)
        if (steps[static_cast<size_t>(k)].op >= 0) {
            showFrame(steps[static_cast<size_t>(k)]);
            return k == step - 1;
        }
    return false;
}

// --- the running list ------------------------------------------------------------------------------

// 174b:0285 ScreenAnim_Start: List_InsertAt(list, a, -1); +0x24 = 1
void ScreenAnims::start(ScreenAnim& a) {
    list_.push_back(&a);
    a.active = true;
}

// 174b:02ab ScreenAnim_StartAt
void ScreenAnims::startAt(ScreenAnim& a, int i) {
    const auto pos = static_cast<size_t>(std::clamp(i, 0, static_cast<int>(list_.size())));
    list_.insert(list_.begin() + static_cast<ptrdiff_t>(pos), &a);
    a.active = true;
}

// 174b:02d2 ScreenAnim_Stop
void ScreenAnims::stop(ScreenAnim& a) {
    std::erase(list_, &a);
    a.active = false;
}

// 174b:02f6 ScreenAnim_StopAll (List_Clear: the records keep their +0x24)
void ScreenAnims::stopAll() { list_.clear(); }

// 174b:047e ScreenAnim_UpdateAll
void ScreenAnims::update(SpriteList& sprites, BorlandRand& rng, const ScreenAnimSound& sound) {
    for (size_t i = 0; i < list_.size();) {
        ScreenAnim* a = list_[i];
        a->step(rng, sound);
        if (!a->hidden()) sprites.add(a->sprite());
        if (a->state() & ScreenAnim::kFinished) {
            list_.erase(list_.begin() + static_cast<ptrdiff_t>(i));  // List_Remove (+0x24 stays)
            continue;
        }
        ++i;
    }
}

// 174b:0510 ScreenAnim_Frame
void ScreenAnims::frame(Screen& screen, SpriteList& sprites, BorlandRand& rng, const ScreenAnimSound& sound) {
    sprites.eraseAll(screen);
    update(sprites, rng, sound);
    sprites.drawAll(screen);
}

}  // namespace tmport::gg
