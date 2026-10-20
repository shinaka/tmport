// GgState: the field registry, the DGROUP dump and load, the tables and the quirk helpers
// (game_state.h).
#include "gg/game/game_state.h"

#include <algorithm>
#include <deque>
#include <format>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "gg/data/gg_exe.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kDgroupImageSeg = dg::kDgroupImageSeg;  // 0x0EA7, as stored in far pointers

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
    put16(p, static_cast<uint16_t>(v & 0xFFFF));
    put16(p + 2, static_cast<uint16_t>(v >> 16));
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int16_t gets16(const uint8_t* p) { return static_cast<int16_t>(get16(p)); }
uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }
void puti(uint8_t* p, int v) { put16(p, static_cast<uint16_t>(v)); }

// ---- codecs ---------------------------------------------------------------------------------------
// Codec<T>: kSize, store(v, p), load(v, p), modelled(byte). Integers, std::array, the record types of
// game_state.h (members), and the wave 2 records below (specialisations).

template <class T>
struct IsStdArray : std::false_type {};
template <class T, size_t N>
struct IsStdArray<std::array<T, N>> : std::true_type {};

template <class T, class = void>
struct Codec;

template <class T>
struct Codec<T, std::enable_if_t<std::is_integral_v<T>>> {
    static constexpr size_t kSize = sizeof(T);
    static void store(const T& v, uint8_t* p) {
        const auto u = static_cast<std::make_unsigned_t<T>>(v);
        for (size_t k = 0; k < sizeof(T); ++k) p[k] = static_cast<uint8_t>((static_cast<uint64_t>(u) >> (8 * k)) & 0xFF);
    }
    static void load(T& v, const uint8_t* p) {
        uint64_t u = 0;
        for (size_t k = 0; k < sizeof(T); ++k) u |= static_cast<uint64_t>(p[k]) << (8 * k);
        v = static_cast<T>(static_cast<std::make_unsigned_t<T>>(u));
    }
    static constexpr bool modelled(size_t) { return true; }
};

template <class T, size_t N>
struct Codec<std::array<T, N>> {
    using E = Codec<T>;
    static constexpr size_t kSize = N * E::kSize;
    static void store(const std::array<T, N>& v, uint8_t* p) {
        for (size_t i = 0; i < N; ++i) E::store(v[i], p + i * E::kSize);
    }
    static void load(std::array<T, N>& v, const uint8_t* p) {
        for (size_t i = 0; i < N; ++i) E::load(v[i], p + i * E::kSize);
    }
    static constexpr bool modelled(size_t i) { return E::modelled(i % E::kSize); }
};

template <class T>
struct Codec<T, std::void_t<decltype(T::kDgroupSize)>> {
    static constexpr size_t kSize = T::kDgroupSize;
    static void store(const T& v, uint8_t* p) { v.store(p); }
    static void load(T& v, const uint8_t* p) { v.load(p); }
    static constexpr bool modelled(size_t i) { return T::modelledByte(i); }
};

// g_actors DS:7a8a: 12 records of 0xA0 bytes (code_world.md §9.1).
template <>
struct Codec<Actor> {
    static constexpr size_t kSize = 0xA0;
    static constexpr bool modelled(size_t i) {
        if (i >= 0x0A && i < 0x0E) return false;  // far picture
        if (i >= 0x44 && i < 0x48) return false;  // never accessed
        if (i >= 0x50 && i < 0x6E) return false;  // never accessed, far tables, far callbacks
        if (i >= 0x72 && i < 0x90) return false;  // far list, far W/H tables, never accessed, far callbacks
        if (i >= 0x92 && i < 0x94) return false;  // never accessed
        if (i >= 0x9A) return false;              // far slots, never accessed
        return true;
    }
    static void store(const Actor& a, uint8_t* p) {
        put16(p + 0x00, a.sprite.flags);
        puti(p + 0x02, a.sprite.x);
        puti(p + 0x04, a.sprite.y);
        puti(p + 0x06, a.sprite.w);
        puti(p + 0x08, a.sprite.h);
        puti(p + 0x0E, a.bundle);
        puti(p + 0x10, a.countdown);
        puti(p + 0x12, a.delay);
        const int saved[] = {a.anchorX, a.anchorY, a.seq,    a.room,      a.floor,      a.prevSeq,  a.exitSeq, a.step,
                             a.doorCooldown, a.loopCount, a.loopStart, a.disabled, a.pendingSeq, a.wallsOn, a.contactsOn,
                             a.blocked, a.unused34, a.doorObj, a.pad, a.edgeObj, a.prevRoom, a.immune, a.item};
        for (size_t k = 0; k < std::size(saved); ++k) puti(p + 0x14 + 2 * k, saved[k]);
        puti(p + 0x42, a.id);
        puti(p + 0x48, a.contacts);
        puti(p + 0x4A, a.wallBits);
        puti(p + 0x4C, a.dir);
        puti(p + 0x4E, a.wallX);
        puti(p + 0x6E, a.toucher);
        puti(p + 0x70, a.carried);
        put16(p + 0x90, a.transTable);
        put32(p + 0x94, a.field94);
        put16(p + 0x98, a.spriteBase);
    }
    static void load(Actor& a, const uint8_t* p) {
        a.sprite.flags = get16(p + 0x00);
        a.sprite.x = gets16(p + 0x02);
        a.sprite.y = gets16(p + 0x04);
        a.sprite.w = gets16(p + 0x06);
        a.sprite.h = gets16(p + 0x08);
        a.bundle = gets16(p + 0x0E);
        a.countdown = gets16(p + 0x10);
        a.delay = gets16(p + 0x12);
        int* saved[] = {&a.anchorX, &a.anchorY, &a.seq, &a.room, &a.floor, &a.prevSeq, &a.exitSeq, &a.step,
                        &a.doorCooldown, &a.loopCount, &a.loopStart, &a.disabled, &a.pendingSeq, &a.wallsOn,
                        &a.contactsOn, &a.blocked, &a.unused34, &a.doorObj, &a.pad, &a.edgeObj, &a.prevRoom,
                        &a.immune, &a.item};
        for (size_t k = 0; k < std::size(saved); ++k) *saved[k] = gets16(p + 0x14 + 2 * k);
        a.id = gets16(p + 0x42);
        a.contacts = get16(p + 0x48);
        a.wallBits = gets16(p + 0x4A);
        a.dir = gets16(p + 0x4C);
        a.wallX = gets16(p + 0x4E);
        a.toucher = gets16(p + 0x6E);
        a.carried = gets16(p + 0x70);
        a.transTable = get16(p + 0x90);
        a.field94 = get32(p + 0x94);
        a.spriteBase = get16(p + 0x98);
    }
};

// g_itemPool DS:6fa8: 60 nodes of 0x2C bytes (code_world.md §7.1).
template <>
struct Codec<ItemNode> {
    static constexpr size_t kSize = 0x2C;
    static constexpr bool modelled(size_t i) {
        if (i < 0x08) return false;               // far next / prev (the list order is the room's)
        if (i >= 0x0A && i < 0x0C) return false;  // not read (0 in every state seen)
        if (i >= 0x16 && i < 0x1A) return false;  // far picture
        if (i >= 0x1C && i < 0x1E) return false;  // not read (0 in every state seen)
        return true;
    }
    static void store(const ItemNode& n, uint8_t* p) {
        put16(p + 0x08, n.free ? 1 : 0);
        put16(p + 0x0C, n.sprite.flags);
        puti(p + 0x0E, n.sprite.x);
        puti(p + 0x10, n.sprite.y);
        puti(p + 0x12, n.sprite.w);
        puti(p + 0x14, n.sprite.h);
        put16(p + 0x1A, n.pictureId);
        puti(p + 0x1E, n.kind);
        put16(p + 0x20, static_cast<uint16_t>(n.field20));
        put16(p + 0x22, n.hit.kind);
        put16(p + 0x24, static_cast<uint16_t>(n.hit.x));
        put16(p + 0x26, static_cast<uint16_t>(n.hit.y));
        put16(p + 0x28, static_cast<uint16_t>(n.hit.w));
        put16(p + 0x2A, static_cast<uint16_t>(n.hit.h));
    }
    static void load(ItemNode& n, const uint8_t* p) {
        n.free = get16(p + 0x08) == 1;
        n.sprite.flags = get16(p + 0x0C);
        n.sprite.x = gets16(p + 0x0E);
        n.sprite.y = gets16(p + 0x10);
        n.sprite.w = gets16(p + 0x12);
        n.sprite.h = gets16(p + 0x14);
        n.pictureId = get16(p + 0x1A);
        n.kind = gets16(p + 0x1E);
        n.field20 = gets16(p + 0x20);
        n.hit.kind = get16(p + 0x22);
        n.hit.x = gets16(p + 0x24);
        n.hit.y = gets16(p + 0x26);
        n.hit.w = gets16(p + 0x28);
        n.hit.h = gets16(p + 0x2A);
    }
};

// g_player DS:4562: the PLAYERS.DAT record byte for byte.
template <>
struct Codec<PlayerRecord> {
    static constexpr size_t kSize = kPlayerRecordSize;
    static constexpr bool modelled(size_t) { return true; }
    static void store(const PlayerRecord& r, uint8_t* p) { r.toBytes(std::span<uint8_t, kPlayerRecordSize>(p, kPlayerRecordSize)); }
    static void load(PlayerRecord& r, const uint8_t* p) {
        r = PlayerRecord::fromBytes(std::span<const uint8_t, kPlayerRecordSize>(p, kPlayerRecordSize));
    }
};

// g_rosterScrollBar DS:20dc (20 bytes, code_ui.md §9).
template <>
struct Codec<ScrollBar> {
    static constexpr size_t kSize = 20;
    static constexpr bool modelled(size_t) { return true; }
    static void store(const ScrollBar& b, uint8_t* p) {
        const int w[] = {b.x, b.y, b.w, b.h, b.pos, b.max, b.page};
        for (size_t k = 0; k < std::size(w); ++k) puti(p + 2 * k, w[k]);
        put32(p + 0x0E, b.nextRepeat);
        puti(p + 0x12, b.thumbY);
    }
    static void load(ScrollBar& b, const uint8_t* p) {
        int* w[] = {&b.x, &b.y, &b.w, &b.h, &b.pos, &b.max, &b.page};
        for (size_t k = 0; k < std::size(w); ++k) *w[k] = gets16(p + 2 * k);
        b.nextRepeat = get32(p + 0x0E);
        b.thumbY = gets16(p + 0x12);
    }
};

// g_partDeck DS:659c (60 words) and g_partDealIndex DS:6614.
template <>
struct Codec<PartDeck> {
    static constexpr size_t kSize = 2 * PartDeck::kImageWords + 2;
    static constexpr bool modelled(size_t) { return true; }
    static void store(const PartDeck& d, uint8_t* p) {
        Codec<std::array<int16_t, PartDeck::kImageWords>>::store(d.image(), p);
        puti(p + 2 * PartDeck::kImageWords, d.dealIndex());
    }
    static void load(PartDeck& d, const uint8_t* p) {
        std::array<int16_t, PartDeck::kImageWords> img{};
        Codec<std::array<int16_t, PartDeck::kImageWords>>::load(img, p);
        d.restore(img, gets16(p + 2 * PartDeck::kImageWords));
    }
};

// g_hintSlots DS:a0de: 5 records of 18 bytes {far next, far prev, i16 id, u32 due, far cancel},
// linked in due order (code_ui.md §13.1). The links are written from the queue's order but not
// compared (a free slot keeps stale links in the original); load follows them to get the order back.
template <>
struct Codec<HintQueue> {
    static constexpr size_t kSize = HintQueue::kSlots * HintQueue::kSlotSize;
    static constexpr bool modelled(size_t i) {
        const size_t k = i % HintQueue::kSlotSize;
        return (k >= 8 && k < 0x0E) || (k >= 0x0E && k < 0x10);  // id, due, the cancel flag's offset
    }
    static uint16_t slotDs(int k) { return static_cast<uint16_t>(dg::kHintSlots + HintQueue::kSlotSize * static_cast<size_t>(k)); }
    static void store(const HintQueue& q, uint8_t* p) {
        const auto& order = q.order();
        for (int k = 0; k < HintQueue::kSlots; ++k) {
            uint8_t* r = p + HintQueue::kSlotSize * static_cast<size_t>(k);
            const HintQueue::SlotInfo s = q.slot(k);
            DsPtr next, prev;
            const auto it = std::find(order.begin(), order.end(), k);
            if (it != order.end()) {
                if (it + 1 != order.end()) next.off = slotDs(*(it + 1));
                if (it != order.begin()) prev.off = slotDs(*(it - 1));
            }
            next.store(r);
            prev.store(r + 4);
            puti(r + 8, s.id);
            put32(r + 0x0A, s.due);
            DsPtr{s.cancelDs}.store(r + 0x0E);
        }
    }
    static void load(HintQueue& q, const uint8_t* p) {
        std::array<HintQueue::SlotInfo, HintQueue::kSlots> slots{};
        std::array<uint16_t, HintQueue::kSlots> next{}, prev{};
        for (int k = 0; k < HintQueue::kSlots; ++k) {
            const uint8_t* r = p + HintQueue::kSlotSize * static_cast<size_t>(k);
            next[static_cast<size_t>(k)] = get16(r);
            prev[static_cast<size_t>(k)] = get16(r + 4);
            slots[static_cast<size_t>(k)] = {gets16(r + 8), get32(r + 0x0A), get16(r + 0x0E)};
        }
        auto slotOf = [](uint16_t ds) -> int {
            if (ds < dg::kHintSlots) return -1;
            const size_t rel = ds - dg::kHintSlots;
            return rel % HintQueue::kSlotSize == 0 && rel / HintQueue::kSlotSize < HintQueue::kSlots ? static_cast<int>(rel / HintQueue::kSlotSize) : -1;
        };
        // The head: a pending slot whose prev is not a pending slot.
        std::vector<int> order;
        int head = -1;
        for (int k = 0; k < HintQueue::kSlots && head < 0; ++k) {
            if (slots[static_cast<size_t>(k)].id == -1) continue;
            const int pv = slotOf(prev[static_cast<size_t>(k)]);
            if (pv < 0 || slots[static_cast<size_t>(pv)].id == -1) head = k;
        }
        for (int k = head; k >= 0 && static_cast<int>(order.size()) < HintQueue::kSlots; k = slotOf(next[static_cast<size_t>(k)])) {
            if (slots[static_cast<size_t>(k)].id == -1 || std::find(order.begin(), order.end(), k) != order.end()) break;
            order.push_back(k);
        }
        // Links that do not reach every pending slot (an image whose links were not written, e.g. a dump
        // on a zero base: the links are not modelled): the pending slots in due order instead, which is
        // the list's order for every shipped delay (Hint_CompareDue, code_ui.md §13.1).
        const auto pending = std::count_if(slots.begin(), slots.end(), [](const HintQueue::SlotInfo& si) { return si.id != -1; });
        if (static_cast<std::ptrdiff_t>(order.size()) != pending) {
            order.clear();
            for (int k = 0; k < HintQueue::kSlots; ++k)
                if (slots[static_cast<size_t>(k)].id != -1) order.push_back(k);
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                return slots[static_cast<size_t>(a)].due < slots[static_cast<size_t>(b)].due;
            });
        }
        q.restore(slots, std::move(order));
    }
};

// ---- visitors ------------------------------------------------------------------------------------

template <class T>
struct As16Codec {
    static void store(const T& v, uint8_t* p) {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>) put16(p, v ? 1 : 0);
        else put16(p, static_cast<uint16_t>(v));
    }
    static void load(T& v, const uint8_t* p) {
        if constexpr (std::is_same_v<T, bool>) v = get16(p) != 0;
        else if constexpr (std::is_signed_v<T>) v = static_cast<T>(gets16(p));
        else v = static_cast<T>(get16(p));
    }
};

[[noreturn]] void pastDgroup(std::string_view name) {
    throw std::logic_error("GgState field " + std::string(name) + " lies past DGROUP");
}

struct Dumper {
    std::vector<uint8_t>& out;
    template <class T>
    void operator()(std::string_view name, uint16_t ds, const T& v) const {
        using C = Codec<T>;
        if (static_cast<size_t>(ds) + C::kSize > out.size()) pastDgroup(name);
        std::vector<uint8_t> tmp(out.begin() + ds, out.begin() + ds + static_cast<std::ptrdiff_t>(C::kSize));
        C::store(v, tmp.data());
        for (size_t i = 0; i < C::kSize; ++i)
            if (C::modelled(i)) out[ds + i] = tmp[i];
    }
    template <class T>
    void operator()(std::string_view name, uint16_t ds, As16<T> v) const {
        if (static_cast<size_t>(ds) + 2 > out.size()) pastDgroup(name);
        As16Codec<std::remove_const_t<T>>::store(v.v, out.data() + ds);
    }
};

struct Loader {
    std::span<const uint8_t> in;
    template <class T>
    void operator()(std::string_view, uint16_t ds, T& v) const {
        if (static_cast<size_t>(ds) + Codec<T>::kSize <= in.size()) Codec<T>::load(v, in.data() + ds);
    }
    template <class T>
    void operator()(std::string_view, uint16_t ds, As16<T> v) const {
        if (static_cast<size_t>(ds) + 2 <= in.size()) As16Codec<T>::load(v.v, in.data() + ds);
    }
};

struct Collector {
    std::vector<DgroupField>& out;
    std::deque<std::vector<uint8_t>>& masks;
    template <class T>
    void operator()(std::string_view name, uint16_t ds, const T&) const {
        using C = Codec<T>;
        DgroupField f;
        f.name = name;
        f.ds = ds;
        f.size = static_cast<uint16_t>(C::kSize);
        std::vector<uint8_t> m(C::kSize);
        bool all = true;
        for (size_t i = 0; i < C::kSize; ++i) {
            m[i] = C::modelled(i) ? 1 : 0;
            all = all && m[i];
        }
        if (!all) {
            masks.push_back(std::move(m));
            f.mask = masks.back();
            f.stride = f.size;
        }
        out.push_back(f);
    }
    template <class T>
    void operator()(std::string_view name, uint16_t ds, As16<T>) const {
        DgroupField f;
        f.name = name;
        f.ds = ds;
        f.size = 2;
        out.push_back(f);
    }
};

// ---- table parsers --------------------------------------------------------------------------------

std::vector<TransitionEntry> readTransitions(const GgExe& exe, uint16_t off) {
    std::vector<TransitionEntry> out;
    for (int n = 0;; ++n) {
        if (n > 64) throw FormatError(std::format("ssg.exe: transition list DS:{:04x} without its end", off));
        const uint16_t p = static_cast<uint16_t>(off + 12 * n);
        TransitionEntry e;
        e.match = exe.ds32(p);
        if (e.match == 0xFFFFFFFFu) return out;
        e.ignore = exe.ds32(static_cast<uint16_t>(p + 4));
        e.next = exe.dsS16(static_cast<uint16_t>(p + 8));
        e.unused = exe.ds16(static_cast<uint16_t>(p + 10));
        out.push_back(e);
    }
}

std::vector<int16_t> readMinusOneList(const GgExe& exe, uint16_t off, size_t max) {
    std::vector<int16_t> out;
    for (size_t i = 0; i < max; ++i) {
        const int16_t v = exe.dsS16(static_cast<uint16_t>(off + 2 * i));
        if (v == -1) return out;
        out.push_back(v);
    }
    throw FormatError(std::format("ssg.exe: list DS:{:04x} without its -1", off));
}

}  // namespace

// ---- record types -----------------------------------------------------------------------------------

void DsPtr::store(uint8_t* p) const {
    put16(p, off);
    put16(p + 2, off ? kDgroupImageSeg : uint16_t{0});
}
void DsPtr::load(const uint8_t* p) { off = get16(p); }

void SpriteRec::store(uint8_t* p) const {
    put16(p, flags);
    put16(p + 2, static_cast<uint16_t>(x));
    put16(p + 4, static_cast<uint16_t>(y));
    put16(p + 6, static_cast<uint16_t>(w));
    put16(p + 8, static_cast<uint16_t>(h));
}
void SpriteRec::load(const uint8_t* p) {
    flags = get16(p);
    x = gets16(p + 2);
    y = gets16(p + 4);
    w = gets16(p + 6);
    h = gets16(p + 8);
}

void RacerRec::store(uint8_t* p) const {
    put16(p, id);
    put16(p + 2, field02);
    sprite.store(p + 4);
    put32(p + 0x12, speed);
    put16(p + 0x16, static_cast<uint16_t>(index));
    put16(p + 0x1C, static_cast<uint16_t>(mode));
    put16(p + 0x1E, static_cast<uint16_t>(finished));
    put16(p + 0x20, static_cast<uint16_t>(laps));
    put32(p + 0x22, limit);
    put32(p + 0x26, max);
    put32(p + 0x2A, field2A);
    put32(p + 0x2E, field2E);
    put32(p + 0x32, acc2);
    put32(p + 0x36, acc);
}
void RacerRec::load(const uint8_t* p) {
    id = get16(p);
    field02 = get16(p + 2);
    sprite.load(p + 4);
    speed = get32(p + 0x12);
    index = gets16(p + 0x16);
    mode = gets16(p + 0x1C);
    finished = gets16(p + 0x1E);
    laps = gets16(p + 0x20);
    limit = get32(p + 0x22);
    max = get32(p + 0x26);
    field2A = get32(p + 0x2A);
    field2E = get32(p + 0x2E);
    acc2 = get32(p + 0x32);
    acc = get32(p + 0x36);
}

void AirRacerRec::store(uint8_t* p) const {
    sprite.store(p);
    put32(p + 0x0E, speed);
    put16(p + 0x12, static_cast<uint16_t>(index));
    put16(p + 0x14, static_cast<uint16_t>(mode));
    put16(p + 0x16, static_cast<uint16_t>(finished));
    for (size_t k = 0; k < 3; ++k) put32(p + 0x1C + 4 * k, speeds[k]);
    for (size_t k = 0; k < 3; ++k) put32(p + 0x28 + 4 * k, accs[k]);
    put32(p + 0x34, dist);
    put32(p + 0x38, prevDist);
}
void AirRacerRec::load(const uint8_t* p) {
    sprite.load(p);
    speed = get32(p + 0x0E);
    index = gets16(p + 0x12);
    mode = gets16(p + 0x14);
    finished = gets16(p + 0x16);
    for (size_t k = 0; k < 3; ++k) speeds[k] = get32(p + 0x1C + 4 * k);
    for (size_t k = 0; k < 3; ++k) accs[k] = get32(p + 0x28 + 4 * k);
    dist = get32(p + 0x34);
    prevDist = get32(p + 0x38);
}

void ElecPieceRec::store(uint8_t* p) const {
    put16(p, static_cast<uint16_t>(type));
    put16(p + 2, static_cast<uint16_t>(x));
    put16(p + 4, static_cast<uint16_t>(y));
    put16(p + 0x0A, static_cast<uint16_t>(used));
    put16(p + 0x0C, static_cast<uint16_t>(closed));
    put16(p + 0x0E, static_cast<uint16_t>(fixed));
}
void ElecPieceRec::load(const uint8_t* p) {
    type = gets16(p);
    x = gets16(p + 2);
    y = gets16(p + 4);
    used = gets16(p + 0x0A);
    closed = gets16(p + 0x0C);
    fixed = gets16(p + 0x0E);
}

void ElecTerminalRec::store(uint8_t* p) const {
    const int16_t w[] = {hitX, hitY, hitW, hitH, anchorX, anchorY, dotX, dotY, overlapH, piece, term, flag, field18};
    for (size_t k = 0; k < std::size(w); ++k) put16(p + 2 * k, static_cast<uint16_t>(w[k]));
}
void ElecTerminalRec::load(const uint8_t* p) {
    int16_t* w[] = {&hitX, &hitY, &hitW, &hitH, &anchorX, &anchorY, &dotX, &dotY, &overlapH, &piece, &term, &flag, &field18};
    for (size_t k = 0; k < std::size(w); ++k) *w[k] = gets16(p + 2 * k);
}

void SimplePlacedRec::store(uint8_t* p) const {
    const int16_t words[] = {type, x, y, w, h, testX, testY};
    for (size_t k = 0; k < std::size(words); ++k) put16(p + 2 * k, static_cast<uint16_t>(words[k]));
}
void SimplePlacedRec::load(const uint8_t* p) {
    int16_t* words[] = {&type, &x, &y, &w, &h, &testX, &testY};
    for (size_t k = 0; k < std::size(words); ++k) *words[k] = gets16(p + 2 * k);
}

void PieceRec::store(uint8_t* p) const {
    put16(p, static_cast<uint16_t>(x));
    put16(p + 2, static_cast<uint16_t>(y));
    put16(p + 4, static_cast<uint16_t>(w));
    put16(p + 6, static_cast<uint16_t>(h));
    p[0x0C] = dx;
    p[0x0D] = dy;
    put16(p + 0x0E, static_cast<uint16_t>(group));
    put16(p + 0x10, static_cast<uint16_t>(placed));
}
void PieceRec::load(const uint8_t* p) {
    x = gets16(p);
    y = gets16(p + 2);
    w = gets16(p + 4);
    h = gets16(p + 6);
    dx = p[0x0C];
    dy = p[0x0D];
    group = gets16(p + 0x0E);
    placed = gets16(p + 0x10);
}

void RectRec::store(uint8_t* p) const {
    put16(p, static_cast<uint16_t>(x0));
    put16(p + 2, static_cast<uint16_t>(y0));
    put16(p + 4, static_cast<uint16_t>(x1));
    put16(p + 6, static_cast<uint16_t>(y1));
}
void RectRec::load(const uint8_t* p) {
    x0 = gets16(p);
    y0 = gets16(p + 2);
    x1 = gets16(p + 4);
    y1 = gets16(p + 6);
}

void GearObjectRec::store(uint8_t* p) const {
    const int16_t w[] = {type, state, col, row, length, dir, visited, savedState, savedCol, savedRow};
    for (size_t k = 0; k < std::size(w); ++k) put16(p + 2 * k, static_cast<uint16_t>(w[k]));
}
void GearObjectRec::load(const uint8_t* p) {
    int16_t* w[] = {&type, &state, &col, &row, &length, &dir, &visited, &savedState, &savedCol, &savedRow};
    for (size_t k = 0; k < std::size(w); ++k) *w[k] = gets16(p + 2 * k);
}

void EnergyDeviceRec::store(uint8_t* p) const {
    put16(p, static_cast<uint16_t>(id));
    for (size_t k = 0; k < uses.size(); ++k) put16(p + 2 + 2 * k, static_cast<uint16_t>(uses[k]));
}
void EnergyDeviceRec::load(const uint8_t* p) {
    id = gets16(p);
    for (size_t k = 0; k < uses.size(); ++k) uses[k] = gets16(p + 2 + 2 * k);
}

void EnergyFormRec::store(uint8_t* p) const {
    put16(p, static_cast<uint16_t>(form));
    for (size_t k = 0; k < words.size(); ++k) put16(p + 2 + 2 * k, static_cast<uint16_t>(words[k]));
}
void EnergyFormRec::load(const uint8_t* p) {
    form = gets16(p);
    for (size_t k = 0; k < words.size(); ++k) words[k] = gets16(p + 2 + 2 * k);
}

void MagnetRec::store(uint8_t* p) const {
    const int16_t w[] = {kind, orientation, x, y, drawn, fx, fy, attached, matched};
    for (size_t k = 0; k < std::size(w); ++k) put16(p + 2 * k, static_cast<uint16_t>(w[k]));
}
void MagnetRec::load(const uint8_t* p) {
    int16_t* w[] = {&kind, &orientation, &x, &y, &drawn, &fx, &fy, &attached, &matched};
    for (size_t k = 0; k < std::size(w); ++k) *w[k] = gets16(p + 2 * k);
}

// ---- tables -----------------------------------------------------------------------------------------

GgTables GgTables::load(const GgExe& exe) {
    GgTables t;
    t.dgroup = exe.dsBytes(0, dg::kInitEnd);
    for (size_t i = 0; i < t.tubeRoomByLevel.size(); ++i) t.tubeRoomByLevel[i] = exe.dsS16(static_cast<uint16_t>(dg::kTubeRoomByLevel + 2 * i));
    for (size_t i = 0; i < t.roomSetIds.size(); ++i) t.roomSetIds[i] = exe.ds16(static_cast<uint16_t>(dg::kRoomSetIds + 2 * i));
    // g_playerTransitions DS:127e: 68 near pointers into DGROUP, 0 = no list.
    for (size_t s = 0; s < t.playerTransitions.size(); ++s) {
        const uint16_t p = exe.ds16(static_cast<uint16_t>(dg::kPlayerTransitions + 2 * s));
        t.playerHasTransitions[s] = p != 0;
        if (p != 0) t.playerTransitions[s] = readTransitions(exe, p);
    }
    t.contactsPlayer = readMinusOneList(exe, dg::kContactsPlayer, 16);
    t.contactsProjectile = readMinusOneList(exe, dg::kContactsProjectile, 16);
    t.contactsChimp = readMinusOneList(exe, dg::kContactsChimp, 16);
    for (size_t i = 0; i < t.chimpStunLoops.size(); ++i) t.chimpStunLoops[i] = exe.ds16(static_cast<uint16_t>(dg::kChimpStunLoops + 2 * i));
    // g_chimpLists DS:1bae: 15 far pointers into DS:1adc..1bad.
    for (size_t r = 0; r < t.chimpListOffsets.size(); ++r) {
        const auto a = exe.dsFarPtr(static_cast<uint16_t>(dg::kChimpLists + 4 * r));
        const uint16_t off = exe.dsOffsetOf(a);
        if (off < dg::kChimpListData || off >= dg::kChimpListDataEnd)
            throw FormatError(std::format("ssg.exe: chimp list {} at DS:{:04x} lies outside DS:1adc..1bad", r, off));
        const uint16_t n = exe.ds16(off);
        if (off + 2u + 2u * n > dg::kChimpListDataEnd) throw FormatError(std::format("ssg.exe: chimp list {} runs past DS:1bad", r));
        t.chimpListOffsets[r] = off;
    }
    // g_chimpMoves DS:1d2e / g_chimpTransitions DS:1f10: 42 near pointers each, 0 = none.
    for (int s = 0; s < kChimpSequences; ++s) {
        const uint16_t mp = exe.ds16(static_cast<uint16_t>(dg::kChimpMoves + 2 * s));
        if (mp != 0) {
            ChimpMoveList m;
            m.marker = exe.dsS16(mp);
            m.field2 = exe.dsS16(static_cast<uint16_t>(mp + 2));
            m.field4 = exe.dsS16(static_cast<uint16_t>(mp + 4));
            const uint16_t n = exe.ds16(static_cast<uint16_t>(mp + 6));
            m.defaultNext = exe.ds16(static_cast<uint16_t>(mp + 8));
            m.field10 = exe.dsS16(static_cast<uint16_t>(mp + 10));
            if (m.marker != -2) {
                if (n > 32) throw FormatError(std::format("ssg.exe: chimp move list {} has {} entries", s, n));
                for (uint16_t k = 0; k < n; ++k) {
                    const uint16_t e = static_cast<uint16_t>(mp + 12 + 12 * k);
                    m.entries.push_back(ChimpMoveEntry{exe.dsS16(e), exe.ds16(static_cast<uint16_t>(e + 2)),
                                                       exe.ds16(static_cast<uint16_t>(e + 4)), exe.ds16(static_cast<uint16_t>(e + 6)),
                                                       exe.ds16(static_cast<uint16_t>(e + 8)), exe.ds16(static_cast<uint16_t>(e + 10))});
                }
            }
            t.chimpMoves[static_cast<size_t>(s)] = std::move(m);
        }
        const uint16_t tp = exe.ds16(static_cast<uint16_t>(dg::kChimpTransitions + 2 * s));
        if (tp != 0) t.chimpTransitions[static_cast<size_t>(s)] = readTransitions(exe, tp);
    }
    for (size_t k = 0; k < t.puzzleVariants.size(); ++k) t.puzzleVariants[k] = exe.ds16(static_cast<uint16_t>(dg::kPuzzleVariants + 2 * k));
    for (size_t k = 0; k < t.rankThresholds.size(); ++k) t.rankThresholds[k] = exe.ds32(static_cast<uint16_t>(dg::kRankThresholds + 4 * k));
    return t;
}

// ---- DGROUP image -----------------------------------------------------------------------------------

std::vector<uint8_t> initialDgroupImage(const GgExe& exe) {
    const auto d = exe.dgroup();
    std::vector<uint8_t> img(dg::kDgroupSize, 0);
    std::copy_n(d.begin(), std::min<size_t>(d.size(), dg::kInitEnd), img.begin());
    return img;
}

const std::vector<DgroupField>& gameStateFields() {
    static std::deque<std::vector<uint8_t>> masks;
    static const std::vector<DgroupField> fields = [] {
        std::vector<DgroupField> out;
        const GgState s{};
        GgState::visit(s, Collector{out, masks});
        return out;
    }();
    return fields;
}

std::vector<uint8_t> dumpToDgroup(const GgState& s, std::span<const uint8_t> base) {
    std::vector<uint8_t> out(base.begin(), base.end());
    if (out.size() < dg::kDgroupSize) out.resize(dg::kDgroupSize, 0);
    GgState::visit(s, Dumper{out});
    return out;
}

void loadFromDgroup(GgState& s, std::span<const uint8_t> dgroup) { GgState::visit(s, Loader{dgroup}); }

std::vector<std::string_view> diffDgroup(std::span<const uint8_t> a, std::span<const uint8_t> b,
                                         const std::vector<DgroupField>* fields) {
    const std::vector<DgroupField>& fs = fields ? *fields : gameStateFields();
    std::vector<std::string_view> out;
    for (const DgroupField& f : fs) {
        const size_t end = static_cast<size_t>(f.ds) + f.size;
        if (end > a.size() || end > b.size()) {
            out.push_back(f.name);
            continue;
        }
        for (size_t i = 0; i < f.size; ++i) {
            if (!f.modelled(i)) continue;
            if (a[f.ds + i] != b[f.ds + i]) {
                out.push_back(f.name);
                break;
            }
        }
    }
    return out;
}

void gameInitState(GgState& s) {
    // main 12e0:0b38: g_state = 2, then 1 (the first store is dead).
    s.flow.state = 1;
    // Hud_Init 1a04:025b: flash off (DS:874c = -1), period 3, phase 0.
    s.world.hud.flashCount = -1;
    s.world.hud.flashPhase = 0;
    s.world.hud.flashPeriod = 3;
    // Hint_Init 3060:0013: every slot free.
    s.ui.hints.clear();
    // Actors_Init 18f5:0721 (code_world.md §9.1 "Initial values"): the player (room 0, id 0, delay 2,
    // walls and contacts on, the transition table DS:127e) and the projectile (disabled, delay 2, id 1,
    // DS:1276). The chimps' fields are Chimps_SetupActors' (every building set-up).
    Actor& p = s.actors.actors[kPlayerActor];
    p.id = 0;
    p.delay = 2;
    p.room = 0;
    p.wallsOn = 1;
    p.contactsOn = 1;
    p.transTable = dg::kPlayerTransitions;
    p.spriteBase = kPlayerKind.spriteBase;
    Actor& b = s.actors.actors[kProjectileActor];
    b.id = 1;
    b.delay = 2;
    b.disabled = 1;
    b.transTable = dg::kProjectileTransitions;
    b.spriteBase = kProjectileKind.spriteBase;
}

GgState initialState(const GgExe& exe) {
    GgState s;
    loadFromDgroup(s, initialDgroupImage(exe));
    gameInitState(s);
    return s;
}

// ---- quirk helpers ----------------------------------------------------------------------------------

ChimpListView chimpList(GgState& s, const GgTables& t, int raceIndex) {
    if (raceIndex < 0 || raceIndex >= static_cast<int>(t.chimpListOffsets.size())) throw std::out_of_range("chimpList: race index");
    const size_t w = (t.chimpListOffsets[static_cast<size_t>(raceIndex)] - dg::kChimpListData) / 2;
    ChimpListView v;
    v.count = s.actors.chimpListData[w];
    const size_t n = std::min<size_t>(v.count, s.actors.chimpListData.size() - w - 1);
    v.rooms = std::span<uint16_t>(s.actors.chimpListData).subspan(w + 1, n);
    return v;
}

uint16_t& chimpListFirstRoom(GgState& s, const GgTables& t, int raceIndex) {
    if (raceIndex < 0 || raceIndex >= static_cast<int>(t.chimpListOffsets.size())) throw std::out_of_range("chimpListFirstRoom: race index");
    const size_t w = (t.chimpListOffsets[static_cast<size_t>(raceIndex)] - dg::kChimpListData) / 2;
    return s.actors.chimpListData.at(w + 1);
}

uint8_t& typedNameByte(UiState& u, int i) {
    if (i >= 0 && i < static_cast<int>(u.typedName.size())) return u.typedName[static_cast<size_t>(i)];
    if (i == static_cast<int>(u.typedName.size())) return u.typedName16;
    throw std::out_of_range("typedNameByte: index past DS:a064");
}

int16_t& energySatisfied(EnergyState& e, int k, int d) { return e.forms.at(static_cast<size_t>(k)).words.at(static_cast<size_t>(d)); }
int16_t& energySent(EnergyState& e, int d, int k) { return e.forms.at(static_cast<size_t>(d)).words.at(4 + static_cast<size_t>(k)); }
int16_t& energyLine(EnergyState& e, int k, int d) { return e.forms.at(static_cast<size_t>(k)).words.at(8 + static_cast<size_t>(d)); }

int16_t* elecPathWord(ElectricState& e, int row, int col) {
    if (col < 0 || col >= 20 || row < 0) return nullptr;
    if (row < static_cast<int>(e.paths.size())) return &e.paths[static_cast<size_t>(row)][static_cast<size_t>(col)];
    if (row == static_cast<int>(e.paths.size()) && col == 0) return &e.pathCount;
    return nullptr;
}

uint16_t& puzzleSolved(PlayerRecord& p, int kind, int variant) {
    return p.puzzleSolved.at(static_cast<size_t>(kind)).at(static_cast<size_t>(variant));
}

uint16_t& hintSeen(PlayerRecord& p, uint16_t id) {
    if (id < kFirstHintId || id >= kFirstHintId + kHintCount) throw std::out_of_range("hintSeen: not a hint id");
    return p.hintSeen[static_cast<size_t>(id - kFirstHintId)];
}

BuildingBlock& buildingBlock(PlayerRecord& p, int b) { return p.blocks.at(static_cast<size_t>(b)); }

}  // namespace tmport::gg
