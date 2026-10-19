// What the DOS autoplayer sees (dos_autoplay_vision.h).
#include "dos/autoplay/dos_autoplay_vision.h"

#include <algorithm>

#include "dos/game/game.h"
#include "dos/gfx/pak.h"

namespace tmport::dos::autoplay {

namespace {

// The elf scripts by kind (code_elves_layout_hints.md §3.5, §4.4-§4.8). Later entries win for a
// frame that two scripts share: the pause frame of 0x21 / 0x43 is a type-A elf whose question the
// picture does not tell.
struct ScriptKind {
    int script;
    SpriteKind kind;
};
constexpr ScriptKind kScripts[] = {
    {0x2F, SpriteKind::CaughtElf}, {0x30, SpriteKind::CaughtElf}, {0x31, SpriteKind::CaughtElf},
    {0x32, SpriteKind::CaughtElf}, {0x33, SpriteKind::CaughtElf}, {0x34, SpriteKind::CaughtElf},
    {0x35, SpriteKind::CaughtElf}, {0x45, SpriteKind::CaughtElf}, {0x46, SpriteKind::CaughtElf},
    {0x47, SpriteKind::CaughtElf}, {0x48, SpriteKind::CaughtElf}, {0x49, SpriteKind::CaughtElf},
    {0x4A, SpriteKind::CaughtElf}, {0x4B, SpriteKind::CaughtElf}, {0x4D, SpriteKind::CaughtElf},
    {0x4E, SpriteKind::CaughtElf},
    {0x2D, SpriteKind::PlainWalker}, {0x2E, SpriteKind::PlainWalker},
    {0x23, SpriteKind::Thrower},     {0x24, SpriteKind::Thrower},     {0x25, SpriteKind::Thrower},
    {0x26, SpriteKind::Thrower},
    {0x41, SpriteKind::Carrier},     {0x42, SpriteKind::Carrier},
    {0x1F, SpriteKind::Walker},      {0x20, SpriteKind::Walker},      {0x21, SpriteKind::Walker},
    {0x43, SpriteKind::Walker},
    {0x56, SpriteKind::CoinCreature}, {0x57, SpriteKind::CoinCreature}, {0x58, SpriteKind::CoinCreature},
    {0x22, SpriteKind::FrozenElf},
};

// The frame codes of script `anim` (code_player.md §3.1: delay, count, entries of [op] + count frame
// codes, -10 ends; ops -2..-5 take one word, -6 none, -7 two).
std::vector<int> scriptFrames(const DosGame& g, int anim) {
    std::vector<int> out;
    const int n = animScriptWord(g, g.tables, anim, 1);
    if (n < 1 || n > 4) return out;
    int k = 2;
    for (int guard = 0; guard < 256; ++guard) {
        const int16_t v = animScriptWord(g, g.tables, anim, k);
        if (v == -10) break;
        if (v >= -5 && v <= -2) k += 2;
        else if (v == -6) k += 1;
        else if (v == -7) k += 3;
        const int16_t f = animScriptWord(g, g.tables, anim, k);  // the slot's own frame (the first of n)
        if (f >= 0) out.push_back(f);
        k += n;
    }
    return out;
}

constexpr int kFieldLeft = 4, kFieldRight = 318, kFieldTop = 16, kFieldBottom = 144;  // the play field, pixels

}  // namespace

std::string_view kindName(SpriteKind k) {
    switch (k) {
        case SpriteKind::Carrier: return "carrier";
        case SpriteKind::Walker: return "walker";
        case SpriteKind::Thrower: return "thrower";
        case SpriteKind::PlainWalker: return "plain";
        case SpriteKind::CoinCreature: return "coin";
        case SpriteKind::CaughtElf: return "caught";
        case SpriteKind::FrozenElf: return "frozen";
        default: return "other";
    }
}

bool isElf(SpriteKind k) {
    return k == SpriteKind::Carrier || k == SpriteKind::Walker || k == SpriteKind::Thrower || k == SpriteKind::PlainWalker ||
           k == SpriteKind::FrozenElf;
}

Vision::Vision(const DosGame& g) {
    // The most specific kind wins a shared frame.
    auto rank = [](SpriteKind k) {
        switch (k) {
            case SpriteKind::Carrier: return 6;
            case SpriteKind::CoinCreature: return 5;
            case SpriteKind::Thrower: return 4;
            case SpriteKind::PlainWalker: return 3;
            case SpriteKind::Walker: return 2;
            case SpriteKind::CaughtElf:
            case SpriteKind::FrozenElf: return 1;
            default: return 0;
        }
    };
    for (const ScriptKind& s : kScripts)
        for (int f : scriptFrames(g, s.script)) {
            const size_t i = static_cast<size_t>(f >> 1);
            if (i >= kindOfFrame_.size()) continue;
            kindsOfFrame_[i] |= static_cast<uint8_t>(1u << static_cast<unsigned>(s.kind));
            if (rank(s.kind) > rank(kindOfFrame_[i])) kindOfFrame_[i] = s.kind;
        }
}

bool Vision::mayBe(uint16_t frameCode, SpriteKind k) const {
    if (frameCode == 0xFFFF) return false;
    const size_t i = frameCode >> 1;
    return i < kindsOfFrame_.size() && (kindsOfFrame_[i] & (1u << static_cast<unsigned>(k))) != 0;
}

SpriteKind Vision::classify(uint16_t frameCode) const {
    if (frameCode == 0xFFFF) return SpriteKind::Other;
    const size_t i = frameCode >> 1;
    return i < kindOfFrame_.size() ? kindOfFrame_[i] : SpriteKind::Other;
}

int Vision::playerScreenX(const DosGame& g) {
    const DrawRecord& r = g.sprites.records[1];
    return r.frame == 0xFFFF ? -1 : r.x;
}

std::vector<SeenSprite> Vision::look(const DosGame& g) const {
    std::vector<SeenSprite> out;
    if (!g.bank) return out;
    for (int i = 1; i < SpriteRenderer::kRecords; ++i) {
        const DrawRecord& r = g.sprites.records[static_cast<size_t>(i)];
        if (r.frame == 0xFFFF || (r.owner & 0x7FFF) != i || r.frame < 6) continue;  // children, pointer frames
        SeenSprite s;
        s.record = i;
        s.frame = r.frame;
        s.kind = classify(r.frame);
        s.x = r.x;
        s.y = r.y;
        const PakFrame* f = g.bankFrame(r.frame);
        if (!f) continue;
        // Sprites_DrawSorted's placement (masked, mirrored for an odd frame code), in pixels, inside the
        // play field it clips to (x bytes 2..0x9E, rows 16..143; ARCHITECTURE §3.4).
        const int bx = (r.frame & 1) ? r.x - (f->widthBytes + f->xOffset) : r.x + f->xOffset;
        s.left = std::clamp(bx * 2, kFieldLeft, kFieldRight);
        s.right = std::clamp((bx + f->widthBytes) * 2, kFieldLeft, kFieldRight);
        s.top = std::clamp(r.y + f->yOffset, kFieldTop, kFieldBottom);
        s.bottom = std::clamp(r.y + f->yOffset + f->height, kFieldTop, kFieldBottom);
        for (int y = s.top; y < s.bottom; ++y)
            for (int x = s.left; x < s.right; ++x)
                if (g.screen.pixel(x, y) != g.back.pixel(x, y)) ++s.differing;
        out.push_back(s);
    }
    return out;
}

std::vector<SeenSprite> Vision::elves(const DosGame& g, int minPixels) const {
    std::vector<SeenSprite> out;
    for (const SeenSprite& s : look(g))
        if ((isElf(s.kind) || s.kind == SpriteKind::CoinCreature) && s.differing >= minPixels) out.push_back(s);
    return out;
}

}  // namespace tmport::dos::autoplay
