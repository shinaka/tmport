#include "gg/game/parts.h"

#include <format>

#include "gg/data/archive.h"
#include "gg/data/resources.h"
#include "gg/game/pic_cache.h"
#include "rtl/borland_rand.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}
uint16_t u16(std::span<const uint8_t> p, size_t o) { return static_cast<uint16_t>(s16(p, o)); }

constexpr size_t kHeaderSize = 0x2D;

}  // namespace

PartCatalogue PartCatalogue::parse(std::span<const uint8_t> p) {
    if (p.size() < kHeaderSize) throw FormatError(std::format("part catalogue: {} bytes", p.size()));
    PartCatalogue c;
    c.building = s16(p, 0x00);
    const int n = s16(p, 0x02);
    c.categories = s16(p, 0x04);
    c.neededCount = s16(p, 0x06);
    c.workshopPalette = u16(p, 0x08);
    c.iconBundle = u16(p, 0x0A);
    c.paintWidth = s16(p, 0x0C);
    for (size_t i = 0; i < 4; ++i) c.paintBase[i] = u16(p, 0x0E + 2 * i);
    for (size_t i = 0; i < 15; ++i) c.paintable[i] = p[0x16 + i];
    size_t o = kHeaderSize;
    if (c.neededCount < 0 || n < 0 || o + 2 * static_cast<size_t>(c.neededCount) + PartRecord::kSize * static_cast<size_t>(n) > p.size())
        throw FormatError(std::format("part catalogue: {} records of {} needed categories do not fit {} bytes", n,
                                      c.neededCount, p.size()));
    for (int i = 0; i < c.neededCount; ++i, o += 2) c.categoryList.push_back(u16(p, o));
    for (int i = 0; i < n; ++i, o += PartRecord::kSize) {
        PartRecord r;
        r.category = s16(p, o + 0x08);
        r.variant = s16(p, o + 0x0A);
        r.holder = s16(p, o + 0x0C);
        r.required = u16(p, o + 0x0E);
        r.iconMember = u16(p, o + 0x10);
        r.shelfPicture = u16(p, o + 0x12);
        r.vehiclePicture = u16(p, o + 0x14);
        r.nameId = u16(p, o + 0x16);
        r.instances = u16(p, o + 0x18);
        r.pair = s16(p, o + 0x1C);
        for (size_t k = 0; k < 4; ++k) {
            r.instX[k] = s16(p, o + 0x1E + 2 * k);
            r.instY[k] = s16(p, o + 0x26 + 2 * k);
            r.instLayer[k] = s16(p, o + 0x2E + 2 * k);
        }
        r.flags = u16(p, o + 0x36);
        r.bodyColour = p[o + 0x38];
        r.hudSprite = {u16(p, o + 0x3A), s16(p, o + 0x3C), s16(p, o + 0x3E), s16(p, o + 0x40), s16(p, o + 0x42), {}};
        r.index = i;
        c.parts.push_back(std::move(r));
    }
    for (; o + 2 <= p.size(); o += 2) {  // Morty's vehicle, -1 terminated
        const int16_t v = s16(p, o);
        if (v == -1) break;
        c.morty.push_back(v);
    }
    return c;
}

// 153f:0002 Parts_LoadCatalogue
PartCatalogue PartCatalogue::load(const Archive& archive, PicCache& cache, int raceIndex) {
    const auto id = static_cast<uint16_t>(kFirstId + raceIndex);
    PartCatalogue c = parse(archive.read(id));  // Res_LoadCopy
    const auto& icons = cache.loadBundle(c.iconBundle);
    for (PartRecord& r : c.parts) {
        if (r.iconMember < icons.size()) r.icon = icons[r.iconMember];
        const auto name = archive.read(r.nameId);  // Res_LoadCopy(+0x16)
        r.name.assign(name.begin(), name.end());
    }
    return c;
}

// 2bd4:0000 Rand_Shuffle
void randShuffle(std::span<int> out, BorlandRand& rng) {
    const int n = static_cast<int>(out.size());
    for (int i = 0; i < n; ++i) out[static_cast<size_t>(i)] = i;
    const int swaps = static_cast<int16_t>(n * 10);
    for (int i = 0; i < swaps; ++i) {
        const int a = rng.random(n);
        const int b = rng.random(n);
        std::swap(out[static_cast<size_t>(a)], out[static_cast<size_t>(b)]);
    }
}

// 153f:01b6 Parts_ShuffleDeck
void PartDeck::shuffle(int total, int partCount, BorlandRand& rng) {
    next_ = 0;
    deck_.assign(static_cast<size_t>(std::max(total, 0)), 0);
    randShuffle(deck_, rng);
    for (int& e : deck_)
        if (e >= partCount) e = kCrate;
}

// 153f:0203 Parts_DealNext
int PartDeck::dealNext() {
    const int i = next_++;
    return i >= 0 && static_cast<size_t>(i) < deck_.size() ? deck_[static_cast<size_t>(i)] : kCrate;
}

}  // namespace tmport::gg
