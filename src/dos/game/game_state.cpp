// GameState field registry, DGROUP dump / load and the quirk helpers (game_state.h).
#include "dos/game/game_state.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>

#include "dos/save/users_dat.h"

namespace tmport::dos {

namespace {

constexpr uint16_t kDgroupImageSeg = DosExe::kDgroupSeg - DosExe::kGhidraBase;  // 0x3D94, as stored in the file

void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

// ---- generic field codec -------------------------------------------------------------------

template <class T>
struct IsStdArray : std::false_type {};
template <class T, size_t N>
struct IsStdArray<std::array<T, N>> : std::true_type {};

template <class T>
constexpr size_t dgroupSizeOf() {
    if constexpr (std::is_integral_v<T>) return sizeof(T);
    else if constexpr (IsStdArray<T>::value) return std::tuple_size_v<T> * dgroupSizeOf<typename T::value_type>();
    else return T::kDgroupSize;
}

// Offset of a segment word inside one element (far pointers), -1 if none; and the element stride.
template <class T>
constexpr int segWordOffset() {
    if constexpr (std::is_same_v<T, DsPtr>) return 2;
    else if constexpr (std::is_same_v<T, SpriteSlot>) return 0x14;
    else if constexpr (IsStdArray<T>::value) return segWordOffset<typename T::value_type>();
    else return -1;
}
template <class T>
constexpr size_t elementStride() {
    if constexpr (IsStdArray<T>::value) return elementStride<typename T::value_type>();
    else return dgroupSizeOf<T>();
}

template <class T>
void storeField(const T& v, uint8_t* p) {
    if constexpr (std::is_integral_v<T>) {
        using U = std::make_unsigned_t<T>;
        const auto u = static_cast<U>(v);
        for (size_t k = 0; k < sizeof(T); ++k) p[k] = static_cast<uint8_t>((static_cast<uint32_t>(u) >> (8 * k)) & 0xFF);
    } else if constexpr (IsStdArray<T>::value) {
        constexpr size_t es = dgroupSizeOf<typename T::value_type>();
        for (size_t i = 0; i < v.size(); ++i) storeField(v[i], p + i * es);
    } else {
        v.store(p);
    }
}

template <class T>
void loadField(T& v, const uint8_t* p) {
    if constexpr (std::is_integral_v<T>) {
        uint32_t u = 0;
        for (size_t k = 0; k < sizeof(T); ++k) u |= static_cast<uint32_t>(p[k]) << (8 * k);
        v = static_cast<T>(static_cast<std::make_unsigned_t<T>>(u));
    } else if constexpr (IsStdArray<T>::value) {
        constexpr size_t es = dgroupSizeOf<typename T::value_type>();
        for (size_t i = 0; i < v.size(); ++i) loadField(v[i], p + i * es);
    } else {
        v.load(p);
    }
}

struct Dumper {
    std::vector<uint8_t>& out;
    template <class T>
    void operator()(std::string_view name, uint16_t ds, const T& v) const {
        constexpr size_t n = dgroupSizeOf<T>();
        if (static_cast<size_t>(ds) + n > out.size())
            throw std::logic_error("dumpToDgroup: field " + std::string(name) + " past DGROUP");
        storeField(v, out.data() + ds);
    }
};

struct Loader {
    std::span<const uint8_t> in;
    template <class T>
    void operator()(std::string_view, uint16_t ds, T& v) const {
        constexpr size_t n = dgroupSizeOf<T>();
        if (static_cast<size_t>(ds) + n <= in.size()) loadField(v, in.data() + ds);
    }
};

struct Collector {
    std::vector<DgroupField>& out;
    template <class T>
    void operator()(std::string_view name, uint16_t ds, const T&) const {
        DgroupField f;
        f.name = name;
        f.ds = ds;
        f.size = static_cast<uint16_t>(dgroupSizeOf<T>());
        f.farPointer = segWordOffset<T>() >= 0;
        f.segWordOffset = static_cast<int16_t>(segWordOffset<T>());
        f.stride = static_cast<uint16_t>(elementStride<T>());
        out.push_back(f);
    }
};

// The far pointers into the selected USERS.DAT record (Users_SelectRecord 47c7:09dd).
struct RecordPointer {
    std::string_view name;
    uint16_t ds;
    uint16_t field;  // offset in the 360-byte record
};
constexpr RecordPointer kRecordPointers[] = {
    {"g_userRec", dg::kUserRec, 0x000},        {"g_totalTreasures", dg::kTotalTreasures, 0x00A},
    {"g_quizHistory", dg::kQuizHistory, 0x00C}, {"g_quizSlots", dg::kQuizSlots, 0x066},
    {"g_answerMasks", dg::kAnswerMasks, 0x078}, {"g_clueWords", dg::kClueWords, 0x081},
    {"g_collected", dg::kCollected, 0x08A},     {"g_foundMasks", dg::kFoundMasks, 0x130},
    {"g_shelfBits", dg::kShelfBits, 0x133},     {"g_quizTier", dg::kQuizTier, 0x13F},
};

}  // namespace

// ---- record types ------------------------------------------------------------------------------

void DsPtr::store(uint8_t* p) const {
    put16(p, off);
    put16(p + 2, off ? kDgroupImageSeg : uint16_t{0});
}
void DsPtr::load(const uint8_t* p) { off = get16(p); }

void Obj::store(uint8_t* p) const {
    put16(p, x);
    put16(p + 2, y);
    put16(p + 4, static_cast<uint16_t>(dx));
    put16(p + 6, static_cast<uint16_t>(dy));
}
void Obj::load(const uint8_t* p) {
    x = get16(p);
    y = get16(p + 2);
    dx = static_cast<int16_t>(get16(p + 4));
    dy = static_cast<int16_t>(get16(p + 6));
}

void Elf::store(uint8_t* p) const {
    obj.store(p);
    put16(p + 0x08, base);
    put16(p + 0x0A, state);
    put16(p + 0x0C, question);
    put16(p + 0x0E, spriteSlot);
    std::copy(unused.begin(), unused.end(), p + 0x10);
}
void Elf::load(const uint8_t* p) {
    obj.load(p);
    base = get16(p + 0x08);
    state = get16(p + 0x0A);
    question = get16(p + 0x0C);
    spriteSlot = get16(p + 0x0E);
    std::copy(p + 0x10, p + 0x20, unused.begin());
}

void SpriteSlot::store(uint8_t* p) const {
    put16(p + 0x00, static_cast<uint16_t>(x));
    put16(p + 0x02, static_cast<uint16_t>(y));
    put16(p + 0x04, static_cast<uint16_t>(z));
    put16(p + 0x06, frame);
    put16(p + 0x08, owner);
    put16(p + 0x0A, anim);
    put16(p + 0x0C, prev);
    put16(p + 0x0E, pos);
    put16(p + 0x10, static_cast<uint16_t>(delay));
    obj.store(p + 0x12);
}
void SpriteSlot::load(const uint8_t* p) {
    x = static_cast<int16_t>(get16(p + 0x00));
    y = static_cast<int16_t>(get16(p + 0x02));
    z = static_cast<int16_t>(get16(p + 0x04));
    frame = get16(p + 0x06);
    owner = get16(p + 0x08);
    anim = get16(p + 0x0A);
    prev = get16(p + 0x0C);
    pos = get16(p + 0x0E);
    delay = static_cast<int16_t>(get16(p + 0x10));
    obj.load(p + 0x12);
}

// ---- tables --------------------------------------------------------------------------------------

std::vector<int16_t> detail::readDsMinusOneList(const DosExe& exe, uint16_t off, size_t maxCount) {
    std::vector<int16_t> out;
    for (size_t i = 0; i < maxCount; ++i) {
        const int16_t v = exe.dsS16(static_cast<uint16_t>(off + 2 * i));
        out.push_back(v);
        if (v == -1) return out;
    }
    throw std::runtime_error("SST.EXE: unterminated list at DS:" + std::to_string(off));
}

GameTables GameTables::load(const DosExe& exe) {
    GameTables t;
    t.world.load(exe);
    t.player.load(exe);
    t.elves.load(exe);
    t.ui.load(exe);
    t.core.load(exe);
    t.dgroup = exe.dgroup();
    return t;
}

// ---- DGROUP image ------------------------------------------------------------------------------

std::vector<uint8_t> initialDgroupImage(const DosExe& exe) {
    std::vector<uint8_t> img(kDgroupSize, 0);
    const auto init = exe.dgroup();
    std::copy_n(init.begin(), std::min<size_t>(init.size(), dg::kInitSize), img.begin());
    return img;
}

const std::vector<DgroupField>& gameStateFields() {
    static const std::vector<DgroupField> fields = [] {
        std::vector<DgroupField> out;
        const GameState s{};
        GameState::visit(s, Collector{out});
        for (const RecordPointer& rp : kRecordPointers) {
            DgroupField f;
            f.name = rp.name;
            f.ds = rp.ds;
            f.size = 4;
            f.farPointer = true;
            f.segWordOffset = 2;
            f.stride = 4;
            out.push_back(f);
        }
        return out;
    }();
    return fields;
}

std::vector<uint8_t> dumpToDgroup(const GameState& s, std::span<const uint8_t> base) {
    std::vector<uint8_t> out(base.begin(), base.end());
    if (out.size() < kDgroupSize) out.resize(kDgroupSize, 0);
    GameState::visit(s, Dumper{out});
    const bool hasRec = s.ui.userRec >= 0 && s.ui.userRec < kUserRecordCount;
    for (const RecordPointer& rp : kRecordPointers) {
        const uint16_t off = hasRec ? static_cast<uint16_t>(kUsersRecordBase + kUserRecordSize * s.ui.userRec + rp.field) : 0;
        DsPtr{off}.store(out.data() + rp.ds);
    }
    return out;
}

void loadFromDgroup(GameState& s, std::span<const uint8_t> dgroup) {
    GameState::visit(s, Loader{dgroup});
    s.ui.userRec = -1;
    if (static_cast<size_t>(dg::kUserRec) + 4 <= dgroup.size()) {
        const uint16_t off = get16(dgroup.data() + dg::kUserRec);
        if (off >= kUsersRecordBase) {
            const unsigned rel = off - kUsersRecordBase;
            if (rel % kUserRecordSize == 0 && rel / kUserRecordSize < kUserRecordCount)
                s.ui.userRec = static_cast<int16_t>(rel / kUserRecordSize);
        }
    }
}

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
        bool differs = false;
        for (size_t i = 0; i < f.size && !differs; ++i) {
            if (f.farPointer && f.segWordOffset >= 0 && f.stride > 0) {
                const size_t inElem = i % f.stride;
                const auto skip = static_cast<size_t>(f.segWordOffset);
                if (inElem >= skip && inElem < skip + f.skipLen) continue;
            }
            differs = a[f.ds + i] != b[f.ds + i];
        }
        if (differs) out.push_back(f.name);
    }
    return out;
}

// ---- quirk helpers ------------------------------------------------------------------------------

int8_t cellTypeRaw(const GameState& s, unsigned idx) {
    if (idx < 256) return s.world.cellType[idx];
    if (idx < 256 + 512) {
        const unsigned b = idx - 256;  // byte b of g_cellContent (DS:0FAA follows DS:0EAA)
        const uint16_t w = s.world.cellContent[b / 2];
        return static_cast<int8_t>((b & 1) ? (w >> 8) : (w & 0xFF));
    }
    return 0;
}

uint16_t cellPictureRaw(const GameState& s, const GameTables& t, int idx) {
    if (idx == -1) return t.world.cellPictureBefore;
    if (idx >= 0 && idx < 256) return s.world.cellPicture[static_cast<size_t>(idx)];
    if (idx >= 256 && idx < 256 + 128) {  // words of g_cellType (DS:0EAA follows the 512 bytes of DS:0CAA)
        const size_t b = static_cast<size_t>(idx - 256) * 2;
        return static_cast<uint16_t>(static_cast<uint8_t>(s.world.cellType[b]) |
                                     (static_cast<uint8_t>(s.world.cellType[b + 1]) << 8));
    }
    return 0;
}

uint16_t hidingCellAt(const GameState& s, const GameTables& t, int slot) {
    if (slot == -1) return s.world.cellContent[255];  // DS:11A8 = the last word of g_cellContent
    if (slot >= 0 && slot < static_cast<int>(t.world.hidingCells.size())) return t.world.hidingCells[static_cast<size_t>(slot)];
    throw std::logic_error("hidingCellAt: slot out of range");
}

int8_t menuWrapAt(const GameTables& t, int i) {
    if (i < -2 || i > 5) throw std::logic_error("menuWrapAt: index out of -2..5");
    const size_t off = static_cast<size_t>(dg::kMenuWrap + i);
    return off < t.dgroup.size() ? static_cast<int8_t>(t.dgroup[off]) : int8_t{0};
}

uint8_t historyByte(const UserRecord& r, int i) {
    if (i >= 0 && i < static_cast<int>(r.quizHistory.size())) return r.quizHistory[static_cast<size_t>(i)];
    if (i == static_cast<int>(r.quizHistory.size())) return static_cast<uint8_t>(r.quizSlots[0] & 0xFF);
    throw std::logic_error("historyByte: index past the history and slot 0");
}

void setHistoryByte(UserRecord& r, int i, uint8_t v) {
    if (i >= 0 && i < static_cast<int>(r.quizHistory.size())) {
        r.quizHistory[static_cast<size_t>(i)] = v;
        return;
    }
    if (i == static_cast<int>(r.quizHistory.size())) {  // record +0x66: low byte of question slot 0
        r.quizSlots[0] = static_cast<uint16_t>((r.quizSlots[0] & 0xFF00) | v);
        return;
    }
    throw std::logic_error("setHistoryByte: index past the history and slot 0");
}

int elfIndexOf(DsPtr p) {
    if (p.off == kPlayerObjOff) return -1;
    const int rel = static_cast<int>(p.off) - static_cast<int>(dg::kElves);
    if (rel >= 0 && rel % 0x20 == 0 && rel / 0x20 < kElfCount) return rel / 0x20;
    throw std::logic_error("g_curObj points at neither the player nor an elf");
}

Obj& objAt(GameState& s, DsPtr p) {
    const int i = elfIndexOf(p);
    return i < 0 ? s.player.obj : s.elves.elves[static_cast<size_t>(i)].obj;
}

const Obj& objAt(const GameState& s, DsPtr p) {
    const int i = elfIndexOf(p);
    return i < 0 ? s.player.obj : s.elves.elves[static_cast<size_t>(i)].obj;
}

int16_t& animScriptWordAt(GameState& s, uint16_t ds) {
    if (ds < dg::kAnimScriptData || ds >= kAnimScriptDataEnd || (ds - dg::kAnimScriptData) % 2 != 0)
        throw std::logic_error("animScriptWordAt: not a script word");
    return s.player.animScriptData[(ds - dg::kAnimScriptData) / 2];
}

int16_t animScriptWord(const GameState& s, const GameTables& t, int anim, int i) {
    if (anim < 0 || anim >= static_cast<int>(dg::kAnimScriptsValid)) throw std::logic_error("animScriptWord: bad script id");
    const unsigned ds = t.player.animScriptOffset[static_cast<size_t>(anim)] + 2u * static_cast<unsigned>(i);
    if (ds < dg::kAnimScriptData || ds >= kAnimScriptDataEnd) return -10;  // past the scripts: an end op
    return s.player.animScriptData[(ds - dg::kAnimScriptData) / 2];
}

uint8_t& messageTextByte(GameState& s, uint16_t ds) {
    if (ds < kMessageTextStart || ds >= kMessageTextEnd) throw std::logic_error("messageTextByte: outside the texts");
    return s.elves.messageText[ds - kMessageTextStart];
}

}  // namespace tmport::dos
