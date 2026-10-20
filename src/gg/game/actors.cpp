#include "gg/game/actors.h"

#include <format>

#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr size_t kSeqHeader = 10;
constexpr size_t kStepSize = 10;

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

}  // namespace

// 1834:0088 Actors_LoadSet
ActorSet ActorSet::parse(std::span<const uint8_t> p) {
    ActorSet s;
    const int frames = s16(p, 0);
    size_t o = 2;
    if (frames < 0 || o + 4 * static_cast<size_t>(frames) + 2 > p.size())
        throw FormatError(std::format("actor set: {} frames in {} bytes", frames, p.size()));
    for (int i = 0; i < frames; ++i) s.widths.push_back(s16(p, o + 2 * static_cast<size_t>(i)));
    o += 2 * static_cast<size_t>(frames);
    for (int i = 0; i < frames; ++i) s.heights.push_back(s16(p, o + 2 * static_cast<size_t>(i)));
    o += 2 * static_cast<size_t>(frames);
    const int nSeq = s16(p, o);
    o += 2;
    if (nSeq < 0 || nSeq > 1024) throw FormatError(std::format("actor set: {} sequences", nSeq));
    for (int i = 0; i < nSeq; ++i) {
        if (o + kSeqHeader > p.size()) throw FormatError(std::format("actor set: sequence {} past the end", i));
        AnimSequence seq;
        seq.delay = s16(p, o);
        seq.dir = s16(p, o + 2);
        const int count = s16(p, o + 8);
        o += kSeqHeader;
        if (count < 0) throw FormatError(std::format("actor set: sequence {} has {} steps", i, count));
        for (int k = 0; k < count; ++k, o += kStepSize) {
            if (o + 2 > p.size()) throw FormatError(std::format("actor set: sequence {} step {} past the end", i, k));
            // The last sequence of a set stores only the op word of its final step (-1000).
            seq.steps.push_back({s16(p, o), s16(p, o + 2), s16(p, o + 4), static_cast<uint16_t>(s16(p, o + 6)),
                                 static_cast<uint16_t>(s16(p, o + 8))});
        }
        s.sequences.push_back(std::move(seq));
    }
    return s;
}

// Actor_RunStep 1834:01b6, the frame part (flags, size, position from the anchor, picture).
void actorShowFrame(Actor& a, const ActorScale& scale, const AnimStep& s) {
    a.sprite.flags = s.c;
    const auto op = static_cast<size_t>(s.op);
    if (scale.set && op < scale.set->widths.size()) {
        a.sprite.w = scale.set->widths[op];
        a.sprite.h = scale.set->heights[op];
    }
    a.sprite.x = a.anchorX;
    a.sprite.y = a.anchorY + s.b;
    a.sprite.picture = scale.pictures && op < scale.pictures->size() ? (*scale.pictures)[op] : std::span<const uint8_t>{};
}

const AnimStep* lastFrameStep(const Actor& a, const ActorSet& set) {
    const AnimSequence* seq = set.sequence(a.seq);
    if (!seq) return nullptr;
    for (int k = std::min(a.step, static_cast<int>(seq->steps.size())) - 1; k >= 0; --k)
        if (seq->steps[static_cast<size_t>(k)].op >= 0) return &seq->steps[static_cast<size_t>(k)];
    return nullptr;
}

// 1834:0733 Actors_Draw
void actorsDraw(const std::array<Actor, 12>& actors, int displayRoom, int& playerBlink, SpriteList& sprites) {
    for (size_t i = 1; i < actors.size(); ++i) {
        const Actor& a = actors[i];
        if (a.disabled == 0 && a.room == displayRoom && !a.hidden()) sprites.add(a.sprite);
    }
    const Actor& player = actors[0];
    if (player.immune == 0) playerBlink = 0;
    if (!player.hidden()) {
        if (playerBlink < 2) sprites.add(player.sprite);
        if (player.immune != 0) playerBlink = (playerBlink + 1) % 4;
    }
}

}  // namespace tmport::gg
