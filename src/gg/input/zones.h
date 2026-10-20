#pragma once
// Click zones of Gizmos & Gadgets (ssg.exe 17ab: Zone_Init, Zones_Add*/Remove*/Clear/Push/Pop,
// Zones_HitTest; 198d:0008 Input_HitZones; re/gg/docs/code_runtime.md §8).
//
// A zone is the original's 22-byte record; zones are linked into two intrusive singly linked lists,
// newest first: local (g_zonesLocal DS:0a32) and global (g_zonesGlobal DS:0a36: menu titles, HUD).
// As in the original the lists link the callers' records: a Zone must stay at its address while it
// is linked (the game keeps them in arrays, like the DGROUP tables they are loaded from). The hit
// test takes the zone with the highest priority (ties: the one found first, i.e. added last); a
// local zone beats every global zone.

#include <cstdint>
#include <span>
#include <vector>

#include "gg/input/keyboard.h"

namespace tmport::gg {

struct Zone {
    static constexpr size_t kRecordSize = 22;

    int16_t shape = 0;         // +00h: 0 rectangle, anything else a circle
    int16_t x = 0, y = 0;      // +02h, +04h: rectangle top-left, circle centre
    int16_t w = 0;             // +06h: width (circle: radius)
    int16_t h = 0;             // +08h: height (unused by circles)
    int16_t priority = 0;      // +0Ah: signed; must be >= 0 to be chosen
    Zone* next = nullptr;      // +0Ch: far next
    uint16_t pressCode = 0;    // +10h: returned while the button is down over it
    uint16_t releaseCode = 0;  // +12h: returned when the button is released over it
    int16_t group = 0;         // +14h: Zones_RemoveLocalGroup id

    // A record as stored in DGROUP (the hub DS:0132, the workshop DS:21ce, ...): the fields above in
    // little-endian order; the stored next pointer is ignored (nullptr). Throws std::out_of_range if
    // `record` is shorter than 22 bytes.
    static Zone fromRecord(std::span<const uint8_t> record);
    // 17ab:083b Zone_Init(zone, x, y, w, h, priority, press, release): a rectangle with next = NULL;
    // the group (+14h) is left as it was.
    void init(int16_t zx, int16_t zy, int16_t zw, int16_t zh, int16_t zpriority, uint16_t press, uint16_t release);
    static Zone rect(int16_t zx, int16_t zy, int16_t zw, int16_t zh, int16_t zpriority, uint16_t press, uint16_t release);
    static Zone circle(int16_t cx, int16_t cy, int16_t radius, int16_t zpriority, uint16_t press, uint16_t release);

    // The hit rule of Zones_HitTest for this zone (priority not considered):
    //   rectangle: x >= zx, zx + w > x, y >= zy, zy + h > y (16-bit signed, the sums wrap);
    //   circle:    r2 > dx * dx + dy * dy, signed 32-bit, with r2 the low word of the 16-bit IMUL of
    //              the radius sign-extended (a radius above 181 wraps) and dx, dy the 16-bit
    //              differences: the rim is outside.
    bool contains(int16_t px, int16_t py) const;
};

class ClickZones {
public:
    // The zone stack DS:7a4e: room for 5 entries before it runs into DS:7a62 (no check in the
    // original); deeper pushes work here and are logged once.
    static constexpr int kOriginalStackDepth = 5;

    // 17ab:0294 Zones_AddLocal: zone.next = local head; head = zone.
    void addLocal(Zone& zone);
    // 17ab:02ba Zones_AddGlobal.
    void addGlobal(Zone& zone);
    // 17ab:02de Zones_AddLocalArray(zones, n): adds them in order (the last one ends at the head).
    void addLocalArray(std::span<Zone> zones);
    // 17ab:0314 Zones_RemoveLocal: unlinks the zone (pointer compare) and clears its next. Nothing if
    // it is nullptr or not linked.
    void removeLocal(Zone* zone);
    // 17ab:03b7 Zones_RemoveGlobal: unlinks it from the global list (its next is not cleared).
    void removeGlobal(Zone* zone);
    // 17ab:0441 Zones_RemoveLocalArray.
    void removeLocalArray(std::span<Zone> zones);
    // 17ab:07f4 Zones_RemoveLocalGroup: removes every local zone whose group is `id`.
    void removeLocalGroup(int16_t id);
    // 17ab:0465 Zones_ClearLocal: clears every local zone's next, head = NULL.
    void clearLocal();
    // 17ab:04bd Zones_Push: stack[depth] = local head, depth += 1, local head = NULL.
    void push();
    // 17ab:04e8 Zones_Pop: depth -= 1, local head = stack[depth]. A pop without a push (the
    // original would read DS:7a4a, the cursor record pointer) leaves an empty local list.
    void pop();

    Zone* local() const { return local_; }
    Zone* global() const { return global_; }
    int depth() const { return static_cast<int>(stack_.size()); }

    // 17ab:0507 Zones_HitTest(x, y, list): the zone of highest priority (> the best so far, starting
    // at -1) whose shape contains (x, y); nullptr if none.
    static Zone* hitTest(int16_t x, int16_t y, Zone* list);
    // 198d:0008 Input_HitZones(record): the local list, then (only if nothing was hit there) the
    // global list, at the record's +0Ah/+0Ch; the hit zone's press code if record +00h == 1, else its
    // release code; 0 if no zone was hit.
    uint16_t hit(const InputRecord& record) const;

    // Every zone of a list, head first (tests, tools).
    static std::vector<const Zone*> listOf(const Zone* head);

private:
    Zone* local_ = nullptr;
    Zone* global_ = nullptr;
    std::vector<Zone*> stack_;
    bool warnedDepth_ = false;
};

}  // namespace tmport::gg
