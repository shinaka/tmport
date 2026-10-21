#include "anim/anim_engine.h"

#include "mischief/util/log.h"

namespace tmport::anim {

namespace {

constexpr int kMaxWarnings = 8;

}  // namespace

AnimEngine::AnimEngine(gfx::FrameBuffer& fb, PresentFn present, AnimSound sound)
    : fb_(fb), present_(std::move(present)), sound_(std::move(sound)), frames_(kFrameSlots) {}

// ------------------------------------------------------------------------------------ frame table

void AnimEngine::setFrame(int slot, std::shared_ptr<const gfx::PixMap> pm) {
    if (slot < 0) return;
    if (static_cast<size_t>(slot) >= frames_.size()) frames_.resize(static_cast<size_t>(slot) + 1);
    frames_[static_cast<size_t>(slot)] = std::move(pm);
}

std::shared_ptr<const gfx::PixMap> AnimEngine::frame(int slot) const {
    if (slot < 0 || static_cast<size_t>(slot) >= frames_.size()) return nullptr;
    return frames_[static_cast<size_t>(slot)];
}

void AnimEngine::clearFrames() {
    frames_.assign(kFrameSlots, nullptr);
}

// --------------------------------------------------------------------------------------- channels

// 10b8:00fd Anim_ResetAll
void AnimEngine::resetAll() {
    for (auto& c : channels_) {
        c.running = false;
        c.cursor = 0;
    }
}

// 10b8:0119 Anim_Start
int AnimEngine::start(uint8_t layer, int x, int y, int frameBase, ScriptPtr script, int user0, int user1,
                      uint8_t flags, std::function<void()> onEnd) {
    int ch = 0;
    while (ch < kChannelCount && channels_[static_cast<size_t>(ch)].running) ++ch;
    if (ch == kChannelCount) return 0;  // no free channel: indistinguishable from channel 0
    AnimChannel& c = channels_[static_cast<size_t>(ch)];
    c.layer = layer;
    c.x = static_cast<int16_t>(x);
    c.y = static_cast<int16_t>(y);
    c.frameBase = static_cast<int16_t>(frameBase);
    c.running = script != nullptr;
    c.cursor = kScriptStart;
    c.script = std::move(script);
    c.onEnd = std::move(onEnd);
    c.loopCount = {-1, -1};
    c.user0 = static_cast<int16_t>(user0);
    c.user1 = static_cast<int16_t>(user1);
    c.flags = flags;
    c.stop = false;
    return ch;
}

// 10b8:020a Anim_IsRunning
bool AnimEngine::isRunning(int ch, const DamScript* script) const {
    if (ch < 0 || ch >= kChannelCount) return false;
    const AnimChannel& c = channels_[static_cast<size_t>(ch)];
    return c.script.get() == script && c.running;
}

// 10b8:024e Anim_Stop
void AnimEngine::stop(int ch, const DamScript* script) {
    if (ch < 0 || ch >= kChannelCount) return;
    AnimChannel& c = channels_[static_cast<size_t>(ch)];
    if (c.running && c.script.get() == script) c.stop = true;
}

void AnimEngine::release(int ch) {
    if (ch < 0 || ch >= kChannelCount) return;
    AnimChannel& c = channels_[static_cast<size_t>(ch)];
    c.running = false;
    c.cursor = 0;
}

int AnimEngine::runningChannels() const {
    int n = 0;
    for (const auto& c : channels_) n += c.running ? 1 : 0;
    return n;
}

// ---------------------------------------------------------------------------------------- helpers

// The clipping of 10b8:0000 / 10b8:0082 against the inclusive clip rectangle (DS:0864..086a).
bool AnimEngine::clipToClipRect(int& x, int& y, int& w, int& h) const {
    const gfx::ClipRect& c = fb_.clip();
    if (x < c.left) {
        w -= c.left - x;
        x = c.left;
    }
    if (c.right < x + w - 1) w = c.right - x + 1;
    if (y < c.top) {
        h -= c.top - y;
        y = c.top;
    }
    if (c.bottom < y + h - 1) h = c.bottom - y + 1;
    return w > 0 && h > 0;
}

void AnimEngine::present(int x, int y, int w, int h) const {
    if (present_) present_(x, y, w, h);
}

// ------------------------------------------------------------------------------------ per tick

// 10b8:029d Anim_EraseSprites (+ 10b8:0000 Anim_RestoreBgRect)
void AnimEngine::eraseSprites(int bgYOff) {
    for (const AnimEraseRect& e : erase_) {
        const bool saved = fb_.dirtyEnabled();
        if (e.noDirty) fb_.setDirtyEnabled(false);
        int x = e.x, y = e.y, w = e.w, h = e.h;
        if (clipToClipRect(x, y, w, h)) fb_.copyRect(x, y + bgYOff, w, h, x, y);
        if (e.noDirty) fb_.setDirtyEnabled(saved);
    }
    erase_.clear();
}

// 10b8:0312 Anim_Step
void AnimEngine::step() {
    ++steps_;
    sprites_.clear();
    for (int ch = 0; ch < kChannelCount; ++ch)
        if (channels_[static_cast<size_t>(ch)].running) runChannel(ch);
}

void AnimEngine::runChannel(int ch) {
    AnimChannel& c = channels_[static_cast<size_t>(ch)];
    const ScriptPtr script = c.script;  // keeps the bytes alive even if onEnd restarts things
    const std::vector<uint8_t>& bytes = script->bytes;
    const bool dop = script->isDop();
    auto u8 = [&]() -> uint8_t {
        const uint8_t v = c.cursor < bytes.size() ? bytes[c.cursor] : 0;
        ++c.cursor;
        return v;
    };
    auto i16 = [&]() -> int16_t {
        const uint8_t lo = u8();
        const uint8_t hi = u8();
        return static_cast<int16_t>(lo | (hi << 8));
    };

    size_t ops = 0;
    for (;;) {
        uint8_t op = kOpEnd;
        if (!c.stop) {
            if (c.cursor < bytes.size()) {
                op = bytes[c.cursor++];
            } else {
                mischief::log::warn("DAM script {}: channel {} ran past the end of the script; ending it", script->id, ch);
            }
            if (op == kOpEndFrame) return;
        }
        if (c.stop) op = kOpEnd;  // a pending stop turns the next opcode into END
        if (++ops > kMaxOpsPerTick) {
            mischief::log::warn("DAM script {}: channel {} did not reach FE in {} opcodes; ending it", script->id, ch, kMaxOpsPerTick);
            op = kOpEnd;
        }
        switch (op) {
            case kOpUpdateRect: {
                const int16_t dx = i16();
                const uint8_t dy = u8();
                const int16_t w = i16();
                const uint8_t h = u8();
                AnimUpdateRect r;
                r.x = static_cast<int16_t>(c.x + dx);
                r.y = static_cast<uint8_t>(dy + static_cast<uint8_t>(c.y));  // low byte of y only
                r.w = static_cast<int16_t>(w + 2);
                r.h = static_cast<uint8_t>(h + 2);
                updates_.push_back(r);
                break;
            }
            case kOpLoop: {
                const int16_t target = i16();
                const uint8_t cnt = u8();
                int16_t& n = c.loopCount[cnt >> 7];
                if (n == -1) n = static_cast<int16_t>(cnt & 0x7F);
                --n;
                if (n == 0)
                    n = -1;  // done: fall through
                else
                    c.cursor = static_cast<uint16_t>(target);  // base + target (count 0: always)
                break;
            }
            case kOpSfxStop:
                (void)u8();  // pad byte, read and discarded
                if (sound_.stop) sound_.stop();
                break;
            case kOpSfx: {
                (void)u8();  // pad byte
                const uint8_t n = u8();
                if (sound_.play) sound_.play(n);
                break;
            }
            case kOpSprite: {
                const int16_t dx = i16();
                const int16_t dy = i16();
                const uint8_t f = u8();
                const uint8_t mode = u8();
                AnimSprite s;
                s.layer = c.layer;
                s.noDirty = dop;
                s.x = static_cast<int16_t>(c.x + dx);
                s.y = static_cast<int16_t>(c.y + dy);
                s.mode = mode;
                s.frame = static_cast<int16_t>(c.frameBase + f);
                s.pixmap = frame(s.frame);
                s.sflags = c.flags;
                sprites_.push_back(std::move(s));
                break;
            }
            case kOpEnd: {
                if (c.onEnd) {
                    const auto onEnd = c.onEnd;
                    onEnd();
                }
                c.running = false;
                c.cursor = 0;
                return;
            }
            default: break;  // 00..F8: 1-byte no-op
        }
    }
}

// 10b8:06e4 Anim_DrawSprites
void AnimEngine::drawSprites(uint8_t layerId, int yOff, int bgYOff) {
    static int missingWarnings = 0;
    for (const AnimSprite& s : sprites_) {
        if (s.layer != layerId) continue;
        if (!s.pixmap) {
            if (missingWarnings < kMaxWarnings) {
                ++missingWarnings;
                mischief::log::warn("DAM sprite at ({}, {}) uses empty frame slot {}; not drawn", s.x, s.y, s.frame);
            }
            continue;
        }
        const gfx::PixMap& pm = *s.pixmap;
        const bool saved = fb_.dirtyEnabled();
        if (s.noDirty) fb_.setDirtyEnabled(false);
        const bool mirror = (s.mode & kModeMirror) != 0;
        if (s.sflags & kChannelFlagOpaque) {
            fb_.drawOpaqueOrFlippedWhiteKey(s.x, s.y + yOff, pm, mirror);  // 1058:0cf3
            if (yOff == 0) present(s.x, s.y, pm.width, pm.height);
        } else {
            fb_.drawSprite(s.x, s.y + yOff, pm, mirror);  // 1058:1320
        }
        if (s.mode & kModeStamp) {
            fb_.copyRect(s.x, s.y, pm.width, pm.height, s.x, s.y + bgYOff);  // becomes scenery
        } else if (s.sflags & kChannelFlagErase) {
            AnimEraseRect e;
            e.noDirty = s.noDirty;
            e.x = s.x;
            e.y = s.y;
            e.w = static_cast<int16_t>(pm.width + 2);
            e.h = static_cast<int16_t>(pm.height + 2);
            erase_.push_back(e);
        }
        if (s.noDirty) fb_.setDirtyEnabled(saved);
    }
}

// 10b8:090a Anim_FlushUpdateRects (+ 10b8:0082 Anim_PresentRect)
void AnimEngine::flushUpdateRects() {
    for (const AnimUpdateRect& u : updates_) {
        int x = u.x, y = u.y, w = u.w, h = u.h;
        if (clipToClipRect(x, y, w, h)) present(x, y, w, h);
    }
    updates_.clear();
}

void AnimEngine::tick(uint8_t layerId) {
    eraseSprites(kBgPageOffset);
    step();
    drawSprites(layerId, 0, kBgPageOffset);
    flushUpdateRects();
}

}  // namespace tmport::anim
