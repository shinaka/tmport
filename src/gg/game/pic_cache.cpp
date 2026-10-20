#include "gg/game/pic_cache.h"

#include <format>

#include "gg/data/resources.h"
#include "gg/gfx/palette.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint8_t kPaletteType = 'P';

uint16_t le16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<uint16_t>(p[o] | (p[o + 1] << 8)) : 0;
}

}  // namespace

PicCache::PicCache(const Archive& archive, Palette* palette, std::function<void(PaletteLoad)> onLoad)
    : archive_(archive), palette_(palette), onLoad_(std::move(onLoad)) {}

// Pal_SetShadowRange(payload + 4, u16 +0 byte offset, u16 +2 byte count), as 15df calls it.
void PicCache::applyPalette(std::span<const uint8_t> range) {
    if (!palette_ || range.size() < 4) return;
    const int offset = le16(range, 0), count = le16(range, 2);
    PaletteLoad load = palette_->setShadowRange(range.subspan(4), offset, count);
    if (onLoad_) onLoad_(std::move(load));
}

// 15df:00a1 / 01a5: a bundle gives member 0, or member 1 after a leading 'P' palette (applied).
std::span<const uint8_t> PicCache::pictureOf(uint16_t id, const std::vector<uint8_t>& bytes, uint8_t type) {
    if (type != static_cast<uint8_t>(ResType::Bundle)) return bytes;
    const auto members = parseBundle(bytes);
    if (members.empty()) throw FormatError(std::format("picture bundle {:04x} is empty", id));
    size_t m = 0;
    if (members[0].type == kPaletteType) {
        applyPalette(members[0].payload);
        m = 1;
    }
    if (m >= members.size()) throw FormatError(std::format("picture bundle {:04x} has no picture", id));
    return members[m].payload;
}

// 15df:003e PicCache_Find
int PicCache::findSlot(int16_t id) const {
    int firstFree = -1;
    for (int i = 0; i < kSlots; ++i) {
        const Slot& s = slots_[static_cast<size_t>(i)];
        if (s.id == id) return i | 0x8000;
        if (s.id == -1 && firstFree == -1) {
            firstFree = i;
            if (id == -2) return firstFree;
        }
    }
    return firstFree;
}

// 15df:00a1's record: the use count + 1 on the found record (also on the free one it is about to fill);
// a new picture takes the first free record. A full table makes the original write past it.
void PicCache::slotLoad(uint16_t id) {
    const int r = findSlot(static_cast<int16_t>(id));
    if (r == -1) {
        log::debug("PicCache_Load({:04x}): the 300 records are taken (the original writes past the table)", id);
        return;
    }
    Slot& s = slots_[static_cast<size_t>(r & 0x7FFF)];
    ++s.uses;
    if (!(r & 0x8000)) s.id = static_cast<int16_t>(id);
}

// 15df:023b's records: the bundle's own (use count + 1; a new bundle takes the first free), then for a
// new bundle one free record per picture member with the bundle's id (their use counts untouched).
void PicCache::slotLoadBundle(uint16_t id, size_t pictures) {
    const int r = findSlot(static_cast<int16_t>(id));
    if (r == -1) {
        log::debug("PicCache_LoadBundle({:04x}): the 300 records are taken", id);
        return;
    }
    Slot& own = slots_[static_cast<size_t>(r & 0x7FFF)];
    ++own.uses;
    if (r & 0x8000) return;
    own.id = static_cast<int16_t>(id);
    for (size_t k = 0; k < pictures; ++k) {
        const int m = findSlot(-2);
        if (m == -1) {
            log::debug("PicCache_LoadBundle({:04x}): no record for picture {}", id, k);
            return;
        }
        slots_[static_cast<size_t>(m)].id = static_cast<int16_t>(id);
    }
}

// 15df:0359 / 03a2: the first record with the id: use count - 1, at 0 freed (a bundle's: every later
// record with its id too). PicCache_Release of a bundle leaves its member records (the original's
// dangling pointers), as the original does.
void PicCache::slotRelease(uint16_t id, bool bundle) {
    const int r = findSlot(static_cast<int16_t>(id));
    if (r == -1 || !(r & 0x8000)) return;
    const int at = r & 0x7FFF;
    Slot& s = slots_[static_cast<size_t>(at)];
    if (--s.uses != 0) return;
    s.id = -1;
    if (!bundle) return;
    for (int i = at + 1; i < kSlots; ++i)
        if (slots_[static_cast<size_t>(i)].id == static_cast<int16_t>(id)) slots_[static_cast<size_t>(i)].id = -1;
}

std::span<const uint8_t> PicCache::load(uint16_t id) {
    slotLoad(id);
    auto [it, fresh] = entries_.try_emplace(id);
    Entry& e = it->second;
    ++e.uses;
    if (fresh) {
        Resource r = archive_.load(id);
        e.bytes = std::move(r.bytes);
        e.pictures.push_back(pictureOf(id, e.bytes, r.type));
    }
    return e.pictures.empty() ? std::span<const uint8_t>{} : e.pictures.front();
}

std::span<const uint8_t> PicCache::read(uint16_t id) {
    Resource r = archive_.load(id);
    readBuffer_ = std::move(r.bytes);
    return pictureOf(id, readBuffer_, r.type);
}

const std::vector<std::span<const uint8_t>>& PicCache::loadBundle(uint16_t id) {
    auto [it, fresh] = entries_.try_emplace(id);
    Entry& e = it->second;
    ++e.uses;
    if (fresh) {
        e.bytes = archive_.load(id, ResType::Bundle).bytes;  // Res_LoadCopy
        for (const BundleMember& m : parseBundle(e.bytes)) {
            if (m.payload.empty() || m.payload[0] == 0) break;  // the end marker
            if (m.type == kPaletteType) applyPalette(m.payload);
            else e.pictures.push_back(m.payload);
        }
        slotLoadBundle(id, e.pictures.size());
    } else {
        slotLoadBundle(id, 0);
    }
    return e.pictures;
}

void PicCache::release(uint16_t id) {
    slotRelease(id, false);
    const auto it = entries_.find(id);
    if (it == entries_.end()) return;  // 15df:0359 acts only on a cached id
    if (--it->second.uses <= 0) entries_.erase(it);
}

void PicCache::releaseBundle(uint16_t id) {
    slotRelease(id, true);
    const auto it = entries_.find(id);
    if (it == entries_.end()) return;  // 15df:03a2 acts only on a cached id
    if (--it->second.uses <= 0) entries_.erase(it);
}

const std::vector<std::span<const uint8_t>>* PicCache::pictures(uint16_t id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : &it->second.pictures;
}

int PicCache::useCount(uint16_t id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? 0 : it->second.uses;
}

}  // namespace tmport::gg
