#pragma once
// The picture cache of segment 15df as the world, the HUD and the screens use it
// (re/gg/docs/code_runtime.md §16.4): pictures loaded by resource id and kept with a use count,
// sprite bundles whose pictures get consecutive cache slots, and the uncached read into the shared
// buffer (Pic_Read). Every one of them applies the palette ranges it meets to the shadow palette
// (Pal_SetShadowRange), which is how rooms, actors and part icons bring their colours.
//
//   PicCache_Load(id)        a plain picture whole; a bundle: member 1 when member 0 is a 'P'
//                            palette (applied), else member 0
//   Pic_Read(id)             the same, not cached (the shared resource buffer)
//   PicCache_LoadBundle(id)  every member up to the first whose payload starts with 0 (the 'L' end
//                            marker; a palette at DAC byte offset 0 would end it too): 'P' members
//                            are applied as palettes, every other member becomes the next slot
//
// The engine keeps each loaded resource in its own buffer, so a picture stays valid while it is
// cached (the spans point into the cache's node, which does not move); slot k of a bundle is picture
// k of the list loadBundle returns. The original's table g_picCache DS:6640 (300 records {i16 id, -1
// free; i16 use count; far data}) is mirrored beside it with the original's slot rules (slots()):
// what a DGROUP comparison with a DOSBox-X state sees, and what a game started from a state continues
// with (setSlots). Nothing the player sees depends on the slot numbers.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <vector>

#include "gg/data/archive.h"

namespace tmport::gg {

class Palette;
class PaletteLoad;

class PicCache {
public:
    // The cache applies palette members to `palette` (the shadow palette) and hands the DAC load
    // that Pal_SetShadowRange makes to `onLoad` (the runtime runs it on the next retraces; tools may
    // ignore it and show the shadow at the end). Both may be empty.
    explicit PicCache(const Archive& archive, Palette* palette = nullptr,
                      std::function<void(PaletteLoad)> onLoad = {});

    // 15df:00a1 PicCache_Load: use count + 1; loads the picture on the first use.
    std::span<const uint8_t> load(uint16_t id);
    // 15df:01a5 Pic_Read: the picture of resource `id` in a buffer that the next read replaces.
    std::span<const uint8_t> read(uint16_t id);
    // 15df:023b PicCache_LoadBundle: use count + 1; the bundle's pictures (its slots) in order. The
    // list stays valid while the bundle is cached.
    const std::vector<std::span<const uint8_t>>& loadBundle(uint16_t id);
    // 15df:0359 PicCache_Release / 15df:03a2 PicCache_ReleaseBundle: use count - 1, freed at 0. An
    // id that is not cached is ignored, as in the original.
    void release(uint16_t id);
    void releaseBundle(uint16_t id);

    // The mirror of g_picCache DS:6640 (PicCache_Init 15df:000d: every record free, use count 0).
    struct Slot {
        int16_t id = -1;   // +0: -1 free
        int16_t uses = 0;  // +2 (a bundle's member records keep theirs: only the bundle's record counts)
        bool operator==(const Slot&) const = default;
    };
    static constexpr int kSlots = 300;
    const std::array<Slot, kSlots>& slots() const { return slots_; }
    // A state's table (ids and use counts of DS:6640): the slot rules go on from there.
    void setSlots(const std::array<Slot, kSlots>& slots) { slots_ = slots; }

    bool cached(uint16_t id) const { return entries_.count(id) != 0; }
    // The slots of a cached bundle (the picture of a cached picture), nullptr when not cached; the use
    // count is not touched.
    const std::vector<std::span<const uint8_t>>* pictures(uint16_t id) const;
    int useCount(uint16_t id) const;
    size_t size() const { return entries_.size(); }

    const Archive& archive() const { return archive_; }

private:
    struct Entry {
        int uses = 0;
        std::vector<uint8_t> bytes;                     // the private copy
        std::vector<std::span<const uint8_t>> pictures; // a bundle's slots, or the one picture
    };
    // The picture of a loaded resource (Pic_Read / PicCache_Load), palette applied.
    std::span<const uint8_t> pictureOf(uint16_t id, const std::vector<uint8_t>& bytes, uint8_t type);
    void applyPalette(std::span<const uint8_t> range);

    const Archive& archive_;
    Palette* palette_;
    std::function<void(PaletteLoad)> onLoad_;
    // 15df:003e PicCache_Find(id): index | 0x8000 of the first record with that id, else the first free
    // index (id -2: the first free at once), -1 when there is none.
    int findSlot(int16_t id) const;
    // The slot half of PicCache_Load / PicCache_LoadBundle (with the bundle's picture count) and of the
    // releases.
    void slotLoad(uint16_t id);
    void slotLoadBundle(uint16_t id, size_t pictures);
    void slotRelease(uint16_t id, bool bundle);

    std::map<uint16_t, Entry> entries_;
    std::vector<uint8_t> readBuffer_;  // Pic_Read's shared buffer
    std::array<Slot, kSlots> slots_{};
};

}  // namespace tmport::gg
