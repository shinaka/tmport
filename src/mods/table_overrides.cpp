#include "mods/table_overrides.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <functional>

#include "data/dgroup_offsets.h"
#include "mods/text_codec.h"
#include "util/error.h"

namespace tmport::mods {

namespace {

enum class Elem { U8, I16, U16, U32 };

size_t elemSize(Elem e) {
    switch (e) {
        case Elem::U8: return 1;
        case Elem::I16:
        case Elem::U16: return 2;
        case Elem::U32: return 4;
    }
    return 1;
}

int64_t readElem(const ExeTables& exe, uint32_t off, Elem e) {
    switch (e) {
        case Elem::U8: return exe.u8(off);
        case Elem::I16: return exe.i16(off);
        case Elem::U16: return exe.u16(off);
        case Elem::U32: return exe.u32(off);
    }
    return 0;
}

void typeRange(Elem e, int64_t& lo, int64_t& hi) {
    switch (e) {
        case Elem::U8: lo = 0; hi = 255; break;
        case Elem::I16: lo = -32768; hi = 32767; break;
        case Elem::U16: lo = 0; hi = 65535; break;
        case Elem::U32: lo = 0; hi = 0xFFFFFFFFll; break;
    }
}

// --- table descriptions --------------------------------------------------------------------------

struct ArrayTable {
    const char* name;
    uint16_t off;
    Elem elem;
    int rows;  // 1 for a plain array
    int cols;
    int64_t lo, hi;
    const char* rangeText;  // for messages; nullptr = type range
};

const std::vector<ArrayTable>& arrayTables() {
    static const std::vector<ArrayTable> t = {
        {"rankThresholds", dg::kRankThresholds, Elem::U32, 1, 8, 0, 0x7FFFFFFF, nullptr},
        {"gemsNeeded", dg::kGemsNeeded, Elem::U8, 1, 4, 1, 5, "1-5 treasures per climb"},
        {"gemX", dg::kGemX, Elem::I16, 4, 5, -32768, 32767, nullptr},
        {"gemY", dg::kGemY, Elem::I16, 4, 5, -32768, 32767, nullptr},
        {"elfCountByTier", dg::kElfCountByTier, Elem::U8, 1, 4, 0, 32, "0-32 elves per level (Elf[32] at DS:6386)"},
        {"hintBit", dg::kHintBit, Elem::I16, 1, 34, -1, 31, "-1 (always shown) or a bit number 0-31"},
    };
    return t;
}

struct RoomField {
    const char* name;
    uint8_t off;
    Elem elem;
};

const std::vector<RoomField>& roomFields() {
    // RoomDef, dgroup_map.md §4.16 (the runtime slot fields +14..+19 are not table data).
    static const std::vector<RoomField> f = {
        {"startX", 0x00, Elem::I16},   {"startY", 0x02, Elem::I16},     {"startFrame", 0x04, Elem::U8},
        {"startFacingLeft", 0x05, Elem::U8}, {"exitLeft", 0x06, Elem::U8}, {"exitRight", 0x07, Elem::U8},
        {"exitDoor", 0x08, Elem::U8},  {"unused09", 0x09, Elem::U8},    {"typeLeft", 0x0A, Elem::U8},
        {"typeRight", 0x0B, Elem::U8}, {"typeDoor", 0x0C, Elem::U8},    {"unused0D", 0x0D, Elem::U8},
        {"doorX", 0x0E, Elem::I16},    {"unused10", 0x10, Elem::I16},   {"bgPmap", 0x12, Elem::U16},
        {"level", 0x1A, Elem::U8},     {"music", 0x1B, Elem::U8},
    };
    return f;
}

struct StringTable {
    const char* group;  // nullptr or "clueWords"
    const char* name;
    uint16_t off;       // far char* array
    int count;
};

const std::vector<StringTable>& stringTables() {
    static const std::vector<StringTable> s = {
        {nullptr, "rankNames", dg::kRankNames, 8},
        {"clueWords", "count", dg::kStrCount, 8},
        {"clueWords", "size", dg::kStrSize, 5},
        {"clueWords", "shape", dg::kStrShape, 5},
        {"clueWords", "noun", dg::kStrNoun, 16},
        {"clueWords", "nounPlural", dg::kStrNounPlural, 16},
    };
    return s;
}

std::string asString(std::span<const uint8_t> b) { return std::string(b.begin(), b.end()); }

// --- applying --------------------------------------------------------------------------------------

struct Plan {
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> patches;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> strings;  // site -> new text
    std::vector<std::string> warnings;
};

[[noreturn]] void fail(const std::string& path, const std::string& msg) { throw FormatError(path + ": " + msg); }

// Calls fn(index, value) for each element given by an array of length n (nulls skipped) or an
// object of decimal index keys.
void forEachIndexed(const json::Value& v, int n, const std::string& path, const std::function<void(int, const json::Value&, const std::string&)>& fn) {
    if (v.isArray()) {
        if (static_cast<int>(v.size()) != n)
            fail(path, std::format("needs {} elements, not {} (use null to keep one, or an object {{\"index\": value}})", n, v.size()));
        for (int i = 0; i < n; ++i) {
            const auto& e = v.asArray()[static_cast<size_t>(i)];
            if (!e.isNull()) fn(i, e, std::format("{}[{}]", path, i));
        }
        return;
    }
    if (v.isObject()) {
        for (const auto& [k, e] : v.asObject()) {
            int i = -1;
            auto [p, ec] = std::from_chars(k.data(), k.data() + k.size(), i);
            if (ec != std::errc() || p != k.data() + k.size() || i < 0 || i >= n)
                fail(path, std::format("\"{}\" is not an index 0-{}", k, n - 1));
            if (!e.isNull()) fn(i, e, std::format("{}[{}]", path, i));
        }
        return;
    }
    fail(path, std::format("must be an array of {} elements or an object of indices", n));
}

int64_t intValue(const json::Value& v, const std::string& path, int64_t lo, int64_t hi, const char* rangeText) {
    if (!v.isInteger()) fail(path, std::format("must be a whole number, not {}", json::Value::typeName(v.type())));
    const int64_t x = v.asInt();
    if (x < lo || x > hi) fail(path, rangeText ? std::format("{} is not {}", x, rangeText) : std::format("{} is outside {}..{}", x, lo, hi));
    return x;
}

void putElem(std::vector<uint8_t>& b, int64_t v, Elem e) {
    const uint64_t u = static_cast<uint64_t>(v);
    for (size_t k = 0; k < elemSize(e); ++k) b.push_back(static_cast<uint8_t>(u >> (8 * k)));
}

void planArray(const ExeTables& exe, const ArrayTable& t, const json::Value& v, Plan& plan) {
    const std::string path = t.name;
    std::vector<int64_t> values(static_cast<size_t>(t.rows * t.cols));
    for (int i = 0; i < t.rows * t.cols; ++i) values[static_cast<size_t>(i)] = readElem(exe, t.off + static_cast<uint32_t>(i * elemSize(t.elem)), t.elem);
    auto setOne = [&](int idx, const json::Value& e, const std::string& p) {
        values[static_cast<size_t>(idx)] = intValue(e, p, t.lo, t.hi, t.rangeText);
    };
    if (t.rows == 1) {
        forEachIndexed(v, t.cols, path, setOne);
    } else {
        forEachIndexed(v, t.rows, path, [&](int r, const json::Value& row, const std::string& p) {
            forEachIndexed(row, t.cols, p, [&](int c, const json::Value& e, const std::string& pp) { setOne(r * t.cols + c, e, pp); });
        });
    }
    if (std::string_view(t.name) == "rankThresholds") {
        if (values[0] != 0) fail(path, "element 0 must be 0 (every score must reach a rank)");
        for (size_t i = 1; i < values.size(); ++i)
            if (values[i] < values[i - 1]) fail(path, std::format("element {} ({}) is smaller than element {} ({})", i, values[i], i - 1, values[i - 1]));
    }
    if (std::string_view(t.name) == "elfCountByTier")
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i] < 3)
                plan.warnings.push_back(std::format("{}[{}] = {}: elves 0-2 of a level carry its three questions, so with fewer "
                                                    "than 3 elves some questions (and their clue words) cannot be reached",
                                                    path, i, values[i]));
    std::vector<uint8_t> bytes;
    for (int64_t x : values) putElem(bytes, x, t.elem);
    plan.patches.emplace_back(t.off, std::move(bytes));
}

void planRooms(const ExeTables& exe, const json::Value& v, Plan& plan) {
    forEachIndexed(v, dg::kRoomCount, "rooms", [&](int room, const json::Value& rec, const std::string& path) {
        if (!rec.isObject()) fail(path, "must be an object of room fields");
        const uint32_t base = dg::kRooms + static_cast<uint32_t>(room) * dg::kRoomSize;
        std::vector<uint8_t> bytes(exe.bytes(base, dg::kRoomSize).begin(), exe.bytes(base, dg::kRoomSize).end());
        for (const auto& [key, val] : rec.asObject()) {
            if (key.starts_with("_")) continue;
            const RoomField* f = nullptr;
            for (const auto& rf : roomFields())
                if (key == rf.name) f = &rf;
            const std::string p = path + "." + key;
            if (!f) fail(p, "unknown room field");
            int64_t lo = 0, hi = 0;
            typeRange(f->elem, lo, hi);
            const char* rangeText = nullptr;
            if (key == "exitLeft" || key == "exitRight" || key == "exitDoor") {
                const int64_t x = intValue(val, p, 0, 255, nullptr);
                if (x >= dg::kRoomCount && x != 255) fail(p, std::format("{} is not a room (0-49) or 255 (wall / no door)", x));
            } else if (key == "bgPmap") {
                lo = 1000;
                hi = 3999;
                rangeText = "a PMAP id (1000-3999)";
            } else if (key == "level") {
                hi = 3;
                rangeText = "0-2 (mountain levels) or 3 (castle wall)";
            }
            const int64_t x = intValue(val, p, lo, hi, rangeText);
            std::vector<uint8_t> e;
            putElem(e, x, f->elem);
            std::copy(e.begin(), e.end(), bytes.begin() + f->off);
        }
        plan.patches.emplace_back(base, std::move(bytes));
    });
}

void planStrings(const ExeTables& exe, const StringTable& t, const json::Value& v, const std::string& path, Plan& plan) {
    forEachIndexed(v, t.count, path, [&](int i, const json::Value& e, const std::string& p) {
        if (!e.isString()) fail(p, "must be a string");
        std::vector<uint8_t> text;
        try {
            text = unescapeGameText(e.asString());
        } catch (const FormatError& ex) {
            fail(p, ex.what());
        }
        const uint32_t site = t.off + static_cast<uint32_t>(4 * i);
        if (!exe.dgPtr(site)) fail(p, "this element has no string pointer in the EXE");
        const auto cur = exe.farCstr(site);
        if (text.size() == cur.size() && std::equal(text.begin(), text.end(), cur.begin())) return;  // unchanged
        plan.strings.emplace_back(site, std::move(text));
    });
}

}  // namespace

const std::vector<std::string>& supportedTables() {
    static const std::vector<std::string> n = {"rooms",      "rankThresholds", "rankNames", "gemsNeeded", "gemX",
                                               "gemY",       "elfCountByTier", "hintBit",   "clueWords"};
    return n;
}

json::Value exportTables(const ExeTables& exe) {
    json::Value doc = json::Value::object();
    // rooms
    json::Value rooms = json::Value::array();
    for (int r = 0; r < dg::kRoomCount; ++r) {
        const uint32_t base = dg::kRooms + static_cast<uint32_t>(r) * dg::kRoomSize;
        json::Value rec = json::Value::object();
        for (const auto& f : roomFields()) rec.set(f.name, json::Value(readElem(exe, base + f.off, f.elem)));
        rooms.push(std::move(rec));
    }
    auto arrayValue = [&](const ArrayTable& t) {
        auto row = [&](int r) {
            json::Value a = json::Value::array();
            for (int c = 0; c < t.cols; ++c)
                a.push(json::Value(readElem(exe, t.off + static_cast<uint32_t>((r * t.cols + c) * static_cast<int>(elemSize(t.elem))), t.elem)));
            return a;
        };
        if (t.rows == 1) return row(0);
        json::Value a = json::Value::array();
        for (int r = 0; r < t.rows; ++r) a.push(row(r));
        return a;
    };
    auto strings = [&](const StringTable& t) {
        json::Value a = json::Value::array();
        for (int i = 0; i < t.count; ++i) a.push(json::Value(escapeGameText(exe.farCstr(t.off + static_cast<uint32_t>(4 * i)))));
        return a;
    };
    json::Value clue = json::Value::object();
    for (const auto& name : supportedTables()) {
        if (name == "rooms") {
            doc.set("rooms", std::move(rooms));
            continue;
        }
        for (const auto& t : arrayTables())
            if (name == t.name) doc.set(name, arrayValue(t));
        for (const auto& t : stringTables()) {
            if (!t.group && name == t.name) doc.set(name, strings(t));
            if (t.group && name == t.group) clue.set(t.name, strings(t));
        }
        if (name == "clueWords") doc.set(name, std::move(clue));
    }
    return doc;
}

std::vector<std::pair<std::string, std::string>> exportTableFiles(const ExeTables& exe) {
    const json::Value doc = exportTables(exe);
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [k, v] : doc.asObject()) {
        json::Value one = json::Value::object();
        one.set(k, v);
        out.emplace_back(k + ".json", json::write(one));
    }
    return out;
}

TableApplyResult applyTables(ExeTables& exe, const json::Value& doc) {
    if (!doc.isObject()) throw FormatError("a table file must hold a JSON object keyed by table name");
    Plan plan;
    TableApplyResult res;
    for (const auto& [key, v] : doc.asObject()) {
        if (key.starts_with("_")) continue;
        bool known = false;
        if (key == "rooms") {
            planRooms(exe, v, plan);
            known = true;
        }
        for (const auto& t : arrayTables())
            if (key == t.name) {
                planArray(exe, t, v, plan);
                known = true;
            }
        for (const auto& t : stringTables())
            if (!t.group && key == t.name) {
                planStrings(exe, t, v, key, plan);
                known = true;
            }
        if (key == "clueWords") {
            if (!v.isObject()) fail(key, "must be an object with count, size, shape, noun and nounPlural lists");
            for (const auto& [sub, sv] : v.asObject()) {
                if (sub.starts_with("_")) continue;
                const StringTable* st = nullptr;
                for (const auto& t : stringTables())
                    if (t.group && sub == t.name) st = &t;
                if (!st) fail(key + "." + sub, "unknown list (count, size, shape, noun, nounPlural)");
                planStrings(exe, *st, sv, key + "." + sub, plan);
            }
            known = true;
        }
        if (!known) {
            std::string names;
            for (const auto& n : supportedTables()) names += (names.empty() ? "" : ", ") + n;
            fail(key, "unknown table (supported: " + names + ")");
        }
        res.tables.push_back(key);
    }
    res.warnings = std::move(plan.warnings);
    // Everything validated: apply.
    for (const auto& [off, bytes] : plan.patches) {
        const auto cur = exe.bytes(off, bytes.size());
        for (size_t i = 0; i < bytes.size(); ++i) res.changedBytes += cur[i] != bytes[i];
        exe.patch(off, bytes);
    }
    for (const auto& [site, text] : plan.strings) {
        exe.repointString(site, text);
        ++res.repointedStrings;
    }
    return res;
}

TableApplyResult applyTablesText(ExeTables& exe, std::string_view jsonText) { return applyTables(exe, json::parse(jsonText)); }

}  // namespace tmport::mods
