#pragma once

#include <array>
#include <cstdint>

namespace tmport {

// The original's 50-slot mouse hot-spot table (segment 1098, DS:4b7a; re/docs/code_1098-10d8.md
// §2.1 and §4). Contract shared by all modules:
//  * set(): an existing entry with the same id is updated in place, otherwise the LOWEST free
//    slot is used; ids double as input codes (e.g. 0x0D = a click that acts as Enter);
//  * hitTest(): searches from slot 49 DOWN, so the highest slot wins; rectangles are half-open
//    [x, x+w) x [y, y+h) with signed compares; returns 0 when nothing is hit;
//  * remove(id) frees the first slot holding id; clear() frees every slot.
// Input_GetCode (input/input_queue.h) hit-tests mouse events against this table.
class HotspotTable {
public:
    static constexpr int kSlots = 50;

    struct Entry {
        uint16_t id = 0;  // 0 = free
        int16_t x = 0, y = 0, w = 0, h = 0;
        bool operator==(const Entry&) const = default;
    };

    // 1098:0016 Hotspot_Set. If the id is absent and every slot is taken, the original writes
    // one record past the table, onto the first record of the (dead-code) save area at DS:4d6e;
    // the new spot is never hit. That is reproduced: the entry lands in saved()[0].
    void set(uint16_t id, int x, int y, int w, int h) {
        const Entry e{id, static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w),
                      static_cast<int16_t>(h)};
        int slot = -1;
        for (int i = 0; i < kSlots; ++i)
            if (slots_[static_cast<size_t>(i)].id == id) {
                slot = i;
                break;
            }
        if (slot < 0)
            for (int i = 0; i < kSlots; ++i)
                if (slots_[static_cast<size_t>(i)].id == 0) {
                    slot = i;
                    break;
                }
        if (slot < 0)
            saved_[0] = e;  // overflow quirk (see above)
        else
            slots_[static_cast<size_t>(slot)] = e;
    }

    // 1098:0089 Hotspot_Remove: clears the id of the first slot holding it (the rectangle stays,
    // which is unobservable because a free slot is never hit and set() rewrites all fields).
    void remove(uint16_t id) {
        if (id == 0) return;
        for (auto& e : slots_)
            if (e.id == id) {
                e.id = 0;
                return;
            }
    }

    // 1098:0000 Hotspot_ClearAll: id = 0 in all 50 slots (rectangles stay).
    void clear() {
        for (auto& e : slots_) e.id = 0;
    }

    // 1098:00b8 Hotspot_HitTest.
    uint16_t hitTest(int x, int y) const {
        for (int i = kSlots - 1; i >= 0; --i) {
            const Entry& e = slots_[static_cast<size_t>(i)];
            if (e.id != 0 && x >= e.x && x < e.x + e.w && y >= e.y && y < e.y + e.h) return e.id;
        }
        return 0;
    }

    // 1098:0130 Hotspot_SaveAll / 1098:0157 Hotspot_RestoreAll. Dead code in the original (no
    // caller); provided for engine code that needs to stash the table around a modal screen.
    void saveAll() { saved_ = slots_; }
    void restoreAll() { slots_ = saved_; }

    // Slot index holding id, or -1.
    int find(uint16_t id) const {
        if (id == 0) return -1;
        for (int i = 0; i < kSlots; ++i)
            if (slots_[static_cast<size_t>(i)].id == id) return i;
        return -1;
    }
    bool contains(uint16_t id) const { return find(id) >= 0; }
    // Number of occupied slots.
    int count() const {
        int n = 0;
        for (const auto& e : slots_) n += e.id != 0;
        return n;
    }

    const std::array<Entry, kSlots>& slots() const { return slots_; }
    const std::array<Entry, kSlots>& saved() const { return saved_; }

private:
    std::array<Entry, kSlots> slots_{};
    std::array<Entry, kSlots> saved_{};  // DS:4d6e
};

}  // namespace tmport
