#include "gg/input/zones.h"

#include <stdexcept>

#include "util/log.h"

namespace tmport::gg {

namespace {

// A list that loops (a zone linked twice) would hang the original's walks; the engine stops after
// this many zones and logs it once.
constexpr int kMaxListLength = 4096;

void warnLoop() {
    static bool warned = false;
    if (!warned) {
        warned = true;
        log::warn("click zones: a zone list loops (a zone was linked twice); the walk was cut short");
    }
}

int16_t s16(std::span<const uint8_t> r, size_t off) {
    return static_cast<int16_t>(static_cast<uint16_t>(r[off] | (r[off + 1] << 8)));
}

}  // namespace

// ------------------------------------------------------------------------------------------- Zone

Zone Zone::fromRecord(std::span<const uint8_t> record) {
    if (record.size() < kRecordSize) throw std::out_of_range("zone record shorter than 22 bytes");
    Zone z;
    z.shape = s16(record, 0x00);
    z.x = s16(record, 0x02);
    z.y = s16(record, 0x04);
    z.w = s16(record, 0x06);
    z.h = s16(record, 0x08);
    z.priority = s16(record, 0x0A);
    z.pressCode = static_cast<uint16_t>(s16(record, 0x10));
    z.releaseCode = static_cast<uint16_t>(s16(record, 0x12));
    z.group = s16(record, 0x14);
    return z;
}

void Zone::init(int16_t zx, int16_t zy, int16_t zw, int16_t zh, int16_t zpriority, uint16_t press, uint16_t release) {
    // 17ab:083b Zone_Init
    shape = 0;
    x = zx;
    y = zy;
    w = zw;
    h = zh;
    next = nullptr;
    priority = zpriority;
    pressCode = press;
    releaseCode = release;
}

Zone Zone::rect(int16_t zx, int16_t zy, int16_t zw, int16_t zh, int16_t zpriority, uint16_t press, uint16_t release) {
    Zone z;
    z.init(zx, zy, zw, zh, zpriority, press, release);
    return z;
}

Zone Zone::circle(int16_t cx, int16_t cy, int16_t radius, int16_t zpriority, uint16_t press, uint16_t release) {
    Zone z = rect(cx, cy, radius, 0, zpriority, press, release);
    z.shape = 1;
    return z;
}

bool Zone::contains(int16_t px, int16_t py) const {
    if (shape == 0) {
        // 17ab:053c: signed compares; zx + w and zy + h are 16-bit sums.
        return px >= x && static_cast<int16_t>(x + w) > px && py >= y && static_cast<int16_t>(y + h) > py;
    }
    // 17ab:0574: r2 = low word of IMUL r, r (sign-extended by CWD); dx, dy 16-bit differences,
    // squared as 32-bit (F_LXMUL) and added with ADD/ADC; hit when r2 > sum (signed 32-bit).
    const auto r2 = static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(w * w)));
    const auto dx = static_cast<int32_t>(static_cast<int16_t>(px - x));
    const auto dy = static_cast<int32_t>(static_cast<int16_t>(py - y));
    const uint32_t sum = static_cast<uint32_t>(dx * dx) + static_cast<uint32_t>(dy * dy);
    return r2 > static_cast<int32_t>(sum);
}

// ------------------------------------------------------------------------------------- ClickZones

void ClickZones::addLocal(Zone& zone) {
    zone.next = local_;
    local_ = &zone;
}

void ClickZones::addGlobal(Zone& zone) {
    zone.next = global_;
    global_ = &zone;
}

void ClickZones::addLocalArray(std::span<Zone> zones) {
    for (Zone& z : zones) addLocal(z);
}

namespace {

// The unlink loops of 17ab:0314 / 03b7.
bool unlink(Zone*& head, Zone* zone) {
    Zone* prev = nullptr;
    int n = 0;
    for (Zone* cur = head; cur; cur = cur->next) {
        if (++n > kMaxListLength) {
            warnLoop();
            return false;
        }
        if (cur == zone) {
            if (prev) prev->next = cur->next;
            else head = cur->next;
            return true;
        }
        prev = cur;
    }
    return false;
}

}  // namespace

void ClickZones::removeLocal(Zone* zone) {
    if (!zone) return;
    if (unlink(local_, zone)) zone->next = nullptr;  // 17ab:037d
}

void ClickZones::removeGlobal(Zone* zone) {
    if (!zone) return;
    unlink(global_, zone);
}

void ClickZones::removeLocalArray(std::span<Zone> zones) {
    for (Zone& z : zones) removeLocal(&z);
}

void ClickZones::removeLocalGroup(int16_t id) {
    // 17ab:07f4: the next pointer is read before the zone is removed.
    Zone* cur = local_;
    int n = 0;
    while (cur) {
        if (++n > kMaxListLength) {
            warnLoop();
            return;
        }
        Zone* next = cur->next;
        if (cur->group == id) removeLocal(cur);
        cur = next;
    }
}

void ClickZones::clearLocal() {
    Zone* cur = local_;
    int n = 0;
    while (cur) {
        if (++n > kMaxListLength) {
            warnLoop();
            break;
        }
        Zone* next = cur->next;
        cur->next = nullptr;
        cur = next;
    }
    local_ = nullptr;
}

void ClickZones::push() {
    stack_.push_back(local_);
    if (static_cast<int>(stack_.size()) > kOriginalStackDepth && !warnedDepth_) {
        warnedDepth_ = true;
        log::warn("click zones: Zones_Push nested {} deep (the original has room for {})", stack_.size(),
                  kOriginalStackDepth);
    }
    local_ = nullptr;
}

void ClickZones::pop() {
    if (stack_.empty()) {
        log::warn("click zones: Zones_Pop without a Zones_Push; the local list is emptied");
        local_ = nullptr;
        return;
    }
    local_ = stack_.back();
    stack_.pop_back();
}

Zone* ClickZones::hitTest(int16_t x, int16_t y, Zone* list) {
    // 17ab:0507 Zones_HitTest
    Zone* best = nullptr;
    int16_t bestPriority = -1;
    int n = 0;
    for (Zone* z = list; z; z = z->next) {
        if (++n > kMaxListLength) {
            warnLoop();
            break;
        }
        if (z->contains(x, y) && z->priority > bestPriority) {
            best = z;
            bestPriority = z->priority;
        }
    }
    return best;
}

uint16_t ClickZones::hit(const InputRecord& record) const {
    // 198d:0008 Input_HitZones
    const Zone* z = hitTest(record.x, record.y, local_);
    if (!z) z = hitTest(record.x, record.y, global_);
    if (!z) return 0;
    return record.kind == 1 ? z->pressCode : z->releaseCode;
}

std::vector<const Zone*> ClickZones::listOf(const Zone* head) {
    std::vector<const Zone*> out;
    for (const Zone* z = head; z && static_cast<int>(out.size()) < kMaxListLength; z = z->next) out.push_back(z);
    return out;
}

}  // namespace tmport::gg
