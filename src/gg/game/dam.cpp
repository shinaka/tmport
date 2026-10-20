// The DAM engine (dam.h). Owner: puzzles_a agent.
#include "gg/game/dam.h"

#include <algorithm>

#include "gg/data/resources.h"
#include "gg/game/game.h"
#include "gg/game/puzzles.h"
#include "gg/game/pz_launch.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kOpCallback = 0x7FF8, kOpRect = 0x7FF9, kOpLoop = 0x7FFA, kOpSkip1 = 0x7FFB, kOpSkip2 = 0x7FFC,
                   kOpCel = 0x7FFD, kOpEndTick = 0x7FFE, kOpEnd = 0x7FFF;
constexpr size_t kHeader = 4;      // 'D' 'A' 'M' 0x1A
constexpr size_t kChannelSize = 0x22;

int16_t picWord(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

void put16(uint8_t* p, int v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

}  // namespace

// 3d39:03cb Anim_Start
int DamPlayer::start(GgGame& g, int layer, int x, int y, int frameBase, std::span<const uint8_t> script, uint32_t user,
                     int flags, std::function<void()> callback) {
    int ch = 0;
    while (ch < kChannels && channels_[static_cast<size_t>(ch)].cursor != 0) ++ch;
    if (ch == kChannels) return 0;  // the original's 0 both for "none free" and channel 0
    Channel& c = channels_[static_cast<size_t>(ch)];
    c.layer = static_cast<int16_t>(layer);
    c.x = static_cast<int16_t>(x);
    c.y = static_cast<int16_t>(y);
    c.frameBase = static_cast<int16_t>(frameBase);
    c.script.assign(script.begin(), script.end());
    c.cursor = kHeader;  // the script + 4
    c.callback = std::move(callback);
    c.counters = {-1, -1};
    c.user = user;
    c.flags = static_cast<int16_t>(flags);
    c.stop = 0;          // the pause word +4 is left as it is
    syncState(g.puzzles.dam);
    return ch;
}

// 3d39:0468 Anim_IsRunning
bool DamPlayer::isRunning(int channel, std::span<const uint8_t> script) const {
    if (channel < 0 || channel >= kChannels) return false;
    const Channel& c = channels_[static_cast<size_t>(channel)];
    // The same script base (the engine compares the copies' bytes) and a cursor.
    return c.cursor != 0 && std::equal(c.script.begin(), c.script.end(), script.begin(), script.end());
}

// 3d39:058f Anim_Step
void DamPlayer::step(GgGame& g) {
    cels_.clear();  // g_animCelCount = 0 (3d39:0597)
    for (int ch = 0; ch < kChannels; ++ch) {
        Channel& c = channels_[static_cast<size_t>(ch)];
        if (c.cursor == 0 || c.pause != 0) continue;
        auto read = [&c]() -> uint16_t {
            const size_t at = c.cursor;
            c.cursor += 2;
            return at + 2 <= c.script.size() ? static_cast<uint16_t>(c.script[at] | (c.script[at + 1] << 8)) : kOpEnd;
        };
        for (int guard = 0;; ++guard) {
            if (c.cursor + 2 > c.script.size() || guard > 4096) {
                log::debug("Anim_Step: channel {} runs past its script (the original reads on)", ch);
                c.cursor = 0;
                break;
            }
            uint16_t op = read();
            if (op == kOpEndTick) break;  // the end of this channel's tick
            if (c.stop != 0) op = kOpEnd;
            if (op < kOpCallback || op > kOpEnd) continue;  // any other word: a one-word no-op
            bool freed = false;
            switch (op) {
            case kOpCallback: {
                const uint16_t index = read();
                const uint16_t lo = read(), hi = read();
                const uint32_t arg = static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16);
                if (index < kCallbacks) {
                    if (callbacks[index]) callbacks[index](ch, arg);
                } else {
                    log::debug("Anim_Step: callback {} outside the table DS:ba1e (the original calls a frame pointer)", index);
                }
                break;
            }
            case kOpRect: {
                Rect r;
                r.x = static_cast<int16_t>(static_cast<int16_t>(read()) + c.x);
                r.y = static_cast<int16_t>(static_cast<int16_t>(read()) + c.y);
                r.w = static_cast<int16_t>(read());
                r.h = static_cast<int16_t>(read());
                rects_.push_back(r);  // the list DS:b86e has room for 2; the scripts never queue one
                break;
            }
            case kOpLoop: {
                const uint16_t target = read();
                const uint16_t n = read();
                auto& counter = c.counters[(n & 0x8000) ? 1 : 0];
                if (counter == -1) counter = static_cast<int16_t>(n & 0x7FFF);
                --counter;
                if (counter == 0) {
                    counter = -1;
                } else {
                    c.cursor = static_cast<size_t>(target) * 2;  // word `target` from the script's start
                }
                break;
            }
            case kOpSkip1: read(); break;
            case kOpSkip2:
                read();
                read();
                break;
            case kOpCel: {
                Cel cel;
                const int16_t dx = static_cast<int16_t>(read());
                const int16_t dy = static_cast<int16_t>(read());
                const int16_t frame = static_cast<int16_t>(read());
                cel.flags = static_cast<int16_t>(read());
                cel.layer = c.layer;
                cel.x = static_cast<int16_t>(c.x + dx);
                cel.y = static_cast<int16_t>(c.y + dy);
                const int f = frame + c.frameBase;
                if (f >= 0 && static_cast<size_t>(f) < frames_.size()) cel.picture = frames_[static_cast<size_t>(f)];
                else log::debug("Anim_Step: frame {} outside the frame table", f);
                cel.channelFlags = c.flags;
                if (static_cast<int>(cels_.size()) < kMaxCels) cels_.push_back(cel);
                else log::debug("Anim_Step: a 17th cel (the original writes past DS:b97e)");
                break;
            }
            case kOpEnd:
                if (c.callback) c.callback();
                c.cursor = 0;  // the channel is free
                freed = true;
                break;
            default: break;  // 0x7FFE is taken before the dispatch
            }
            if (freed) break;
        }
    }
    syncState(g.puzzles.dam);
}

// 3d39:084a Anim_DrawSprites
void DamPlayer::drawSprites(Screen& screen, int layer) {
    for (const Cel& cel : cels_) {
        if (cel.layer != layer) continue;
        if (cel.channelFlags & 2) screen.lpxDraw(cel.x, cel.y, cel.picture);  // Lpx_Draw 1399:0335
        else screen.lpxDrawKeyed(cel.x, cel.y, cel.picture, cel.flags & 1);   // Lpx_DrawKeyed 1399:02ee
        const int16_t w = picWord(cel.picture, 6), h = picWord(cel.picture, 8);
        if ((cel.flags & 2) == 0) {
            if ((cel.channelFlags & 1) != 0 && (cel.flags & 4) == 0) erase_.push_back({cel.x, cel.y, w, h});
        } else {
            screen.saveToBack(cel.x, cel.y, w, h);
        }
    }
}

// 3d39:00ba Anim_PresentRect
void DamPlayer::presentRect(GgGame& g, int x, int y, int w, int h) {
    const ClipRect& clip = g.video.screen().clip();
    if (x < clip.left) w -= clip.left - x;
    else if (clip.right < x + w - 1) w = clip.right - x + 1;
    if (y < clip.top) h -= clip.top - y;
    else if (clip.bottom < y + h - 1) h = clip.bottom - y + 1;
    if (w > 0 && h > 0) g.display.flush();
}

void DamPlayer::syncState(DamState& s) const {
    s.celCount = static_cast<int16_t>(cels_.size());
    s.rectCount = static_cast<int16_t>(rects_.size());
    for (size_t i = 0; i < channels_.size(); ++i) {
        const Channel& c = channels_[i];
        uint8_t* p = s.channels[i].data();
        std::fill(p, p + kChannelSize, uint8_t{0});
        put16(p + 0x00, c.layer);
        put16(p + 0x02, c.stop);
        put16(p + 0x04, c.pause);
        put16(p + 0x06, c.flags);
        put16(p + 0x08, c.x);
        put16(p + 0x0A, c.y);
        put16(p + 0x0C, c.frameBase);
        put16(p + 0x0E, c.counters[0]);
        put16(p + 0x10, c.counters[1]);
        put16(p + 0x12, static_cast<int>(c.user & 0xFFFF));
        put16(p + 0x14, static_cast<int>(c.user >> 16));
        if (c.cursor != 0) put16(p + 0x1A, static_cast<int>(c.cursor));  // the far cursor as {offset, 0}
    }
}

// 3797:100e Anim_PlayModal
void damPlayModal(GgGame& g, PzLaunch& launch, uint16_t script, uint16_t frames, int newSurface) {
    LaunchState& l = launch.st();
    const int r = l.ramp % 3;
    const std::vector<uint8_t> s = g.archive().read(static_cast<uint16_t>(script + r));  // Res_LoadCopy
    launch.animate(g, false, false);
    const Bundle f = Bundle::load(g.archive(), static_cast<uint16_t>(frames + r));
    launch.animate(g, false, false);
    std::vector<std::span<const uint8_t>> table;
    for (size_t i = 0; i < f.size(); ++i) table.push_back(f.member(i).payload);  // DS:ba26[i]
    DamPlayer& dam = g.puzzlesObjects().dam;
    dam.setFrames(std::move(table));
    const int ch = dam.start(g, 0, 0, 0, 0, s);  // every argument 0 but the script (3797:1095..10ad)
    while (dam.isRunning(ch, s)) {
        g.timers().set(7, 0x12);
        g.video.screen().restoreRect(0x40, 0x11, 0xFB, 0x8B);  // the play area
        dam.step(g);
        launch.putObjectOnLauncher();
        launch.drawPlunger(g);
        if (script == 0x1154) g.sound.pzPlay(0x13, 3);  // restarted every DAM tick
        launch.waitTimerAnim(g, 7);
    }
    l.surface = static_cast<int16_t>(newSurface);
    launch.redraw(g);
    dam.setFrames({});  // the copies are freed
}

}  // namespace tmport::gg
