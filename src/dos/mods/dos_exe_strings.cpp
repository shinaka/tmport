#include "dos/mods/dos_exe_strings.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <functional>

#include "dos/data/dgroup_offsets.h"
#include "dos/data/dos_exe.h"
#include "dos/mods/dos_text_codec.h"
#include "dos/text/dos_font.h"
#include "mods/json.h"
#include "mods/mod_common.h"
#include "util/error.h"

namespace tmport::dos {

namespace {

namespace json = mods::json;
using R = DosStringRole;

// Pools: DS ranges whose strings are reached only through far pointers (fmt_data.md §7.2).
struct PoolDef {
    const char* name;
    uint16_t start, end;
};
constexpr PoolDef kPools[] = {
    {"menus", 0x5262, 0x52EE},     // drop-down item lists (boxes 0-2); DS:52ee holds the help-title pointers
    {"boxes", 0x52FE, 0x59CC},     // box titles and texts; the box table follows
    {"footers", 0x5C91, 0x5D00},   // box footers and help topic titles; the fixed button labels follow
    {"words", 0x5FF4, 0x6132},     // clue words; the record offsets follow
    {"headings", 0x634F, 0x6375},  // sign-in headings; the fixed sign-in texts follow
    {"hints", 0x6BC0, 0x7B9D},     // Flutter's messages 0-34; message 35 is at a fixed address
};

// Strings the code addresses with immediate offsets, and where they are drawn (x bytes, y, line
// height; x = -1: placed by other code; for the layout warnings).
struct FixedDef {
    uint16_t ds;
    R role;
    int index;
    const char* what;
    int x, y, lineHeight;
};
constexpr FixedDef kFixed[] = {
    {dg::kStrChampion, R::Champion, 0, "rank chest: the champion banner (drawn flashing)", 0x40, 0x2E, 12},
    {dg::kStrBtnNo, R::ButtonSmall, 0, "Yes/No boxes: button 0", -1, 0, 12},
    {dg::kStrBtnYes, R::ButtonSmall, 1, "Yes/No boxes: button 1 (the default)", -1, 0, 12},
    {dg::kStrBtnDone, R::ButtonWide, 0, "help: button on a topic's last page", -1, 0, 12},
    {dg::kStrBtnNextPage, R::ButtonWide, 1, "help: next-page button", -1, 0, 12},
    {dg::kStrBtnPrevPage, R::ButtonWide, 2, "help: previous-page button", -1, 0, 12},
    {dg::kStrNewPlayer, R::NewPlayer, 0, "sign-in: the last row of the player list", 8, 0x41, 11},
    {dg::kStrSigninKeyLine, R::KeyLine, 0, "sign-in: the key line at the bottom", 4, 0xBD, 12},
    {dg::kStrTitlePrompt, R::TitlePrompt, 0, "title pages: the prompt line", 0x11, 0xA0, 10},
    {dg::kStrStory1, R::TitlePage, 1, "story page 1", 6, 0x20, 10},
    {dg::kStrStory2, R::TitlePage, 2, "story page 2", 6, 0x20, 10},
    {dg::kStrStory2Keys, R::TitlePage, 3, "story page 2: key line", 10, 0xBD, 10},
    {dg::kStrSsPage1, R::TitlePage, 4, "series page: heading", 3, 6, 10},
    {dg::kStrSsPage2, R::TitlePage, 5, "series page: first title block", 3, 0x31, 10},
    {dg::kStrSsPage3, R::TitlePage, 6, "series page: second block", 3, 0x82, 10},
    {dg::kStrSsPage4, R::TitlePage, 7, "series page: third block", 0, 0x3E, 10},
};

// The message boxes of DS:59cc (code_ui.md §4.1), described.
constexpr std::array<const char*, dg::kMsgBoxesCount> kBoxRoles = {
    "the Help menu's items, one per line",
    "the File menu's items, one per line",
    "the Options menu's items, one per line",
    "confirmation: start the game over",
    "confirmation: start a new player",
    "confirmation: exit and save",
    "question: New Player Hints on or off",
    "question: music on or off",
    "question: sound on or off",
    "question: exit at the sign-in",
    "confirmation: remove a player",
    "question: continue a saved game",
    "warning: a saved game will be lost (never shown by the game)",
    "message: the sign-in list is full",
    "message: the name is already on the list",
    "name entry",
    "floppy disk 1 prompt (never shown by the TM Port)",
    "floppy disk 2 prompt (never shown by the TM Port)",
    "write-protected disk (never shown by the TM Port)",
    "disk problem (never shown by the TM Port)",
    "program problem (never shown by the TM Port)",
    "program disk prompt (never shown by the TM Port)",
    "please wait while loading from floppies (never shown by the TM Port)",
};

std::string hintRole(int id) {
    if (id <= 13) return std::format("Flutter: New Player Hint {}", id);
    if (id == 14) return "Flutter: unused placeholder";
    if (id <= 18) return "Flutter: praise after a right answer (one of four, at random)";
    if (id <= 22) return "Flutter: after a wrong answer (one of four, at random)";
    if (id <= 30) return std::format("Flutter: start of a game at rank {}", id - 23);
    if (id <= 34) {
        constexpr const char* kEnd[] = {"no treasure", "one treasure", "some treasures", "all treasures"};
        return std::format("Flutter: end of a climb with {}", kEnd[id - 31]);
    }
    if (id == 35) return "Flutter: the champion won the crown back (the name is written at characters 22-29)";
    return "Flutter: the champion's second message";
}

constexpr int kCrownNameEnd = 0x1D;  // message 35: name bytes 0x15..0x1C (DS:7bb2..7bb9, a fixed address)

std::string dsKey(uint16_t ds) { return std::format("DS:{:04X}", ds); }

// The pool layout of the given texts (members in DS order): new offsets, bytes used. An empty text
// shares the NUL of the member before it.
struct PoolLayout {
    std::vector<uint16_t> offsets;
    size_t used = 0;
};
PoolLayout layOut(uint16_t start, const std::vector<const std::vector<uint8_t>*>& texts) {
    PoolLayout l;
    size_t pos = start;
    for (size_t i = 0; i < texts.size(); ++i) {
        const auto& t = *texts[i];
        if (t.empty() && i > 0) {
            l.offsets.push_back(static_cast<uint16_t>(pos - 1));  // the previous member's NUL
            continue;
        }
        l.offsets.push_back(static_cast<uint16_t>(pos));
        pos += t.size() + 1;
    }
    l.used = pos - start;
    return l;
}

int textWidthAll(const DosFont& font, std::span<const uint8_t> line) {
    int w = 0;
    for (const uint8_t c : line) w += font.advance(c & 0x7F);
    return w;
}

// MsgBox_Draw 43cb:0592's box for a title and text (code_ui.md §4.2).
struct Box {
    int x1, y1, x2, y2;
};
Box boxGeometry(const DosFont& font, int type, int rx1, int ry1, const std::vector<uint8_t>* title, const std::vector<uint8_t>& text) {
    int w = (title ? dosTextWidth(font, *title) : 0) + 0x18;
    int lines = 0;
    if (!text.empty()) {
        for (const auto line : dosTextLines(text)) w = std::max(w, textWidthAll(font, line) + 0x0F);
        lines = static_cast<int>(dosTextLines(text).size());
    }
    w = std::max(w, type == 3 ? 0x5C : 0x4B);
    int h = lines * 12 + 0x0F;
    if (title) h += 0x0E;
    if (type == 1 || type == 3) h += 0x0C;
    if (type != 7) h += 0x13;
    const int x1 = rx1 < 0 ? (0xA0 - w) / 2 : rx1;
    const int y1 = ry1 < 0 ? (200 - h) / 2 : ry1;
    return {x1, y1, x1 + w, y1 + h};
}

}  // namespace

// ---- catalogue ------------------------------------------------------------------------------------

DosStringCatalogue DosStringCatalogue::read(const DosExe& exe) {
    DosStringCatalogue c;
    for (const PoolDef& p : kPools) c.pools_.push_back({p.name, p.start, p.end, 0});
    std::map<uint16_t, DosStringEntry> byDs;
    auto addPointer = [&](uint16_t at, R role, int index, const std::string& what) {
        const DosExe::FarPtr fp = exe.readFarPtr(at);
        if (fp.off == 0 && fp.seg == DosExe::kGhidraBase) return;  // null (titles of boxes without one)
        if (fp.seg != DosExe::kDgroupSeg)
            throw FormatError(std::format("SST.EXE: the string pointer at {} does not point into DGROUP", dsKey(at)));
        auto [it, added] = byDs.try_emplace(fp.off);
        DosStringEntry& e = it->second;
        if (added) {
            e.ds = fp.off;
            e.role = role;
            e.index = index;
            e.what = what;
            const std::string_view s = exe.dsString(fp.off);
            e.text.assign(s.begin(), s.end());
        }
        e.pointers.push_back(at);
    };
    for (int b = 0; b < static_cast<int>(dg::kMsgBoxesCount); ++b) {
        const uint16_t rec = static_cast<uint16_t>(dg::kMsgBoxes + dg::kMsgBoxesStride * b);
        const std::string role = kBoxRoles[static_cast<size_t>(b)];
        addPointer(static_cast<uint16_t>(rec + dg::boxrec::kTitle), R::BoxTitle, b, std::format("box {} title ({})", b, role));
        addPointer(static_cast<uint16_t>(rec + dg::boxrec::kText), b < 3 ? R::MenuItems : R::BoxText, b,
                   b < 3 ? std::format("box {}: {}", b, role) : std::format("box {} text ({})", b, role));
    }
    for (int i = 0; i < 3; ++i)
        addPointer(static_cast<uint16_t>(dg::kBoxFooterText + 4 * i), R::Footer, i,
                   std::format("box footer {} ({})", i,
                               i == 0 ? "go-back line of menus and boxes" : i == 1 ? "go-on line of boxes of type 3" : "go-on line of disk boxes"));
    for (int i = 0; i < static_cast<int>(dg::kHelpTopicTitlesCount); ++i)
        addPointer(static_cast<uint16_t>(dg::kHelpTopicTitles + 4 * i), R::HelpTitle, i, std::format("help topic {} title", i));
    for (int i = 0; i < static_cast<int>(dg::kClueWordTextCount); ++i)
        addPointer(static_cast<uint16_t>(dg::kClueWordText + 4 * i), R::ClueWord, i, std::format("clue word {}", i));
    for (int i = 0; i < 2; ++i)
        addPointer(static_cast<uint16_t>(dg::kSigninHeadings + 4 * i), R::SigninHeading, i,
                   i == 0 ? "sign-in heading in remove mode" : "sign-in heading");
    for (int i = 0; i < static_cast<int>(dg::kFlutterTextCount); ++i)
        addPointer(static_cast<uint16_t>(dg::kFlutterText + 4 * i), i == 35 ? R::CrownMessage : R::Hint, i,
                   std::format("message {}: {}", i, hintRole(i)));
    for (const FixedDef& f : kFixed) {
        DosStringEntry e;
        e.ds = f.ds;
        e.role = f.role;
        e.index = f.index;
        e.what = f.what;
        const std::string_view s = exe.dsString(f.ds);
        e.text.assign(s.begin(), s.end());
        byDs.emplace(f.ds, std::move(e));
    }
    for (int r = 0; r < static_cast<int>(dg::kRankNamesCount); ++r) {
        DosStringEntry e;
        e.ds = static_cast<uint16_t>(dg::kRankNames + dg::kRankNamesStride * r);
        e.role = R::RankName;
        e.index = r;
        e.what = std::format("sign-in: rank {} label (8-byte slot)", r);
        const std::string_view s = exe.dsString(e.ds);
        e.text.assign(s.begin(), s.end());
        e.max = dg::kRankNamesStride - 1;
        byDs.emplace(e.ds, std::move(e));
    }
    for (auto& [ds, e] : byDs) {
        for (size_t p = 0; p < c.pools_.size(); ++p)
            if (ds >= c.pools_[p].start && ds < c.pools_[p].end) e.pool = static_cast<int>(p);
        if (e.pool < 0 && e.max == 0) e.max = e.text.size();
        c.entries_.push_back(std::move(e));
    }
    for (size_t p = 0; p < c.pools_.size(); ++p) {
        std::vector<const std::vector<uint8_t>*> texts;
        for (const auto& e : c.entries_)
            if (e.pool == static_cast<int>(p)) texts.push_back(&e.text);
        c.pools_[p].used = layOut(c.pools_[p].start, texts).used;
    }
    return c;
}

const DosStringEntry* DosStringCatalogue::find(uint16_t ds) const {
    const auto it = std::lower_bound(entries_.begin(), entries_.end(), ds, [](const DosStringEntry& e, uint16_t v) { return e.ds < v; });
    return it != entries_.end() && it->ds == ds ? &*it : nullptr;
}

std::map<int, DosMessagePatch> dosMessagePatches(const DosExe& exe) {
    std::map<int, DosMessagePatch> m;
    for (int r = 0; r < 8; ++r) {
        const int msg = static_cast<int8_t>(exe.ds8(static_cast<uint16_t>(dg::kRankStartMsg + r)));
        m.try_emplace(msg, DosMessagePatch{exe.dsS16(static_cast<uint16_t>(dg::kMsgDigitOffsets + 2 * r)),
                                           exe.dsS16(static_cast<uint16_t>(dg::kMsgPluralOffsets + 2 * r)), true});
    }
    for (int i = 0; i < 4; ++i) {
        const int msg = static_cast<int8_t>(exe.ds8(static_cast<uint16_t>(dg::kEndMsgIds + i)));
        m.try_emplace(msg, DosMessagePatch{exe.dsS16(static_cast<uint16_t>(dg::kMsgDigitOffsets + 2 * (8 + i))), -1, false});
    }
    return m;
}

// ---- strings.json ---------------------------------------------------------------------------------

std::string exportStringsJson(const DosExe& exe) {
    const DosStringCatalogue cat = DosStringCatalogue::read(exe);
    const auto patches = dosMessagePatches(exe);
    json::Value root = json::Value::object();
    root.set("_about", "The player-visible strings of SST.EXE (Treasure Mountain! 1990), written by tmport --export-mod-kit from "
                       "your game files. Keep only the entries you change in a mod; only \"text\" (and \"countAt\", \"pluralAt\" "
                       "of messages) are read. Format and limits: docs/MODDING.md (The 1990 DOS edition).");
    root.set("_codes", "Text codes: {up} {down} {left} {right} {enter} {spacebar} {menu} {crown} {xHH} {{; \\r = new line; "
                       "~XXXYYY moves to pixel column XXX, row YYY; ` toggles red; |NN colour NN; \\t NN indents.");
    json::Value pools = json::Value::object();
    for (const DosStringPool& p : cat.pools()) {
        json::Value o = json::Value::object();
        o.set("from", dsKey(p.start));
        o.set("to", dsKey(p.end));
        o.set("size", static_cast<int>(p.end - p.start));
        o.set("used", static_cast<int>(p.used));
        pools.set(p.name, std::move(o));
    }
    root.set("pools", std::move(pools));
    json::Value strings = json::Value::object();
    for (const DosStringEntry& e : cat.entries()) {
        json::Value o = json::Value::object();
        o.set("what", e.what);
        if (e.pool >= 0)
            o.set("pool", cat.pools()[static_cast<size_t>(e.pool)].name);
        else
            o.set("max", static_cast<int>(e.max));
        if (e.role == R::Hint)
            if (const auto it = patches.find(e.index); it != patches.end()) {
                o.set("countAt", it->second.countAt);
                if (it->second.hasPlural) o.set("pluralAt", it->second.pluralAt);
            }
        o.set("text", escapeDosText(e.text, DosTextForm::Json));
        strings.set(dsKey(e.ds), std::move(o));
    }
    root.set("strings", std::move(strings));
    return json::write(root);
}

void parseStringsJson(std::string_view text, const DosStringCatalogue& catalogue, DosStringEdits& edits) {
    const json::Value v = json::parse(text);
    const json::Value* strings = v.isObject() ? v.find("strings") : nullptr;
    if (!strings || !strings->isObject()) throw FormatError("strings.json needs an object \"strings\" keyed by DS offset (\"DS:5345\")");
    DosStringEdits parsed;
    for (const auto& [key, val] : strings->asObject()) {
        if (key.starts_with('_')) continue;
        std::string_view hex = key;
        if (hex.size() > 3 && (hex.substr(0, 3) == "DS:" || hex.substr(0, 3) == "ds:")) hex.remove_prefix(3);
        unsigned off = 0;
        const auto [p, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), off, 16);
        if (hex.empty() || ec != std::errc() || p != hex.data() + hex.size() || off > 0xFFFF)
            throw FormatError(std::format("strings: \"{}\" is not a DS offset (write \"DS:5345\")", key));
        const DosStringEntry* e = catalogue.find(static_cast<uint16_t>(off));
        if (!e) throw FormatError(std::format("strings: \"{}\" is not a string of SST.EXE V 1.0 (see the exported strings.json)", key));
        if (!val.isObject()) throw FormatError(std::format("strings: \"{}\" must be an object with \"text\"", key));
        DosStringEdit edit;
        if (const json::Value* t = val.find("text")) {
            if (!t->isString()) throw FormatError(std::format("strings: \"{}\".text must be a string", key));
            try {
                edit.text = unescapeDosText(t->asString(), DosTextForm::Json);
            } catch (const mods::ModSyntaxError& ex) {
                throw FormatError(std::format("strings: \"{}\".text: {}", key, ex.what()));
            }
        }
        for (const char* name : {"countAt", "pluralAt"})
            if (const json::Value* n = val.find(name)) {
                if (e->role != R::Hint) throw FormatError(std::format("strings: \"{}\".{} only applies to Flutter's rank and end messages", key, name));
                if (!n->isInteger() || n->asInt() < -1 || n->asInt() > 0x7FFF)
                    throw FormatError(std::format("strings: \"{}\".{} must be a position (or -1 for none)", key, name));
                (std::string_view(name) == "countAt" ? edit.countAt : edit.pluralAt) = static_cast<int>(n->asInt());
            }
        parsed[e->ds] = std::move(edit);
    }
    for (auto& [ds, e] : parsed) {
        DosStringEdit& d = edits[ds];
        if (e.text) d.text = std::move(e.text);
        if (e.countAt) d.countAt = e.countAt;
        if (e.pluralAt) d.pluralAt = e.pluralAt;
    }
}

DosStringsResult applyStringEdits(DosExe& exe, const DosStringCatalogue& catalogue, const DosStringEdits& edits,
                                  const DosStringLayout& layout) {
    DosStringsResult r;
    const DosFont& font = *layout.font;
    const auto origPatches = dosMessagePatches(exe);
    struct Work {
        const DosStringEntry* e = nullptr;
        std::vector<uint8_t> text;
        DosMessagePatch patch;
        bool hasPatch = false;
        bool changed() const { return text != e->text; }
    };
    std::vector<Work> work;
    for (const DosStringEntry& e : catalogue.entries()) {
        Work w{&e, e.text, {}, false};
        if (e.role == R::Hint)
            if (const auto it = origPatches.find(e.index); it != origPatches.end()) {
                w.patch = it->second;
                w.hasPatch = true;
            }
        if (const auto it = edits.find(e.ds); it != edits.end()) {
            if (it->second.text) w.text = *it->second.text;
            if (w.hasPatch && it->second.countAt) w.patch.countAt = *it->second.countAt;
            if (w.hasPatch && it->second.pluralAt) w.patch.pluralAt = *it->second.pluralAt;
            if (!w.hasPatch && (it->second.countAt || it->second.pluralAt))
                r.errors.push_back(std::format("{} ({}): countAt / pluralAt only apply to the rank and end messages 23-34", dsKey(e.ds), e.what));
        }
        work.push_back(std::move(w));
    }
    auto patchChanged = [&](const Work& w) {
        if (!w.hasPatch) return false;
        const auto& o = origPatches.at(w.e->index);
        return o.countAt != w.patch.countAt || (o.hasPlural && o.pluralAt != w.patch.pluralAt);
    };
    auto refuse = [&](Work& w, const std::string& why) {
        r.errors.push_back(std::format("{} ({}): {} (the original is kept)", dsKey(w.e->ds), w.e->what, why));
        w.text = w.e->text;
        if (w.hasPatch) w.patch = origPatches.at(w.e->index);
    };

    // 1. Rules for one string.
    for (Work& w : work) {
        if (!w.changed() && !patchChanged(w)) continue;
        const DosStringEntry& e = *w.e;
        if (const std::string m = checkDosMarkup(w.text); !m.empty()) {
            refuse(w, m);
            continue;
        }
        if (e.pool < 0 && w.text.size() > e.max) {
            refuse(w, std::format("{} characters; its place in SST.EXE holds {}", w.text.size(), e.max));
            continue;
        }
        if (e.role == R::MenuItems && dosTextLines(w.text).size() != dosTextLines(e.text).size()) {
            refuse(w, std::format("{} lines; the menu has {} items (one per line)", dosTextLines(w.text).size(), dosTextLines(e.text).size()));
            continue;
        }
        if (e.role == R::CrownMessage && static_cast<int>(w.text.size()) < kCrownNameEnd) {
            refuse(w, std::format("{} characters; the champion's name is written over characters 22-29, keep at least {}",
                                  w.text.size(), kCrownNameEnd));
            continue;
        }
        if (w.hasPatch) {
            const int n = static_cast<int>(w.text.size());
            if (w.patch.countAt != -1 && (w.patch.countAt < 0 || w.patch.countAt + 2 > n)) {
                refuse(w, std::format("countAt {} is not inside the text: the count takes two characters there", w.patch.countAt));
                continue;
            }
            if (w.patch.countAt != -1 && w.patch.hasPlural && (w.patch.pluralAt < 0 || w.patch.pluralAt >= n)) {
                refuse(w, std::format("pluralAt {} is not inside the text", w.patch.pluralAt));
                continue;
            }
        }
    }

    // 2. Message boxes stay on the screen (MsgBox_Draw draws them without clipping).
    for (int b = 0; b < static_cast<int>(dg::kMsgBoxesCount); ++b) {
        Work* title = nullptr;
        Work* text = nullptr;
        for (Work& w : work)
            if (w.e->index == b && (w.e->role == R::BoxTitle || w.e->role == R::BoxText || w.e->role == R::MenuItems))
                (w.e->role == R::BoxTitle ? title : text) = &w;
        if (!text || (!text->changed() && !(title && title->changed()))) continue;
        const uint16_t rec = static_cast<uint16_t>(dg::kMsgBoxes + dg::kMsgBoxesStride * b);
        const Box box = boxGeometry(font, exe.ds8(rec), exe.dsS16(static_cast<uint16_t>(rec + dg::boxrec::kX1)),
                                    exe.dsS16(static_cast<uint16_t>(rec + dg::boxrec::kY1)), title ? &title->text : nullptr, text->text);
        if (box.x1 < 0 || box.x2 > 0xA0 || box.y1 < 2 || box.y2 > 200) {
            const std::string why = std::format("box {} would be {}x{} pixels at ({}, {}), off the 320x200 screen (fewer or shorter lines)",
                                                b, 2 * (box.x2 - box.x1), box.y2 - box.y1, 2 * box.x1, box.y1);
            if (text->changed()) refuse(*text, why);
            if (title && title->changed()) refuse(*title, why);
        }
    }

    // 3. Pools: all their strings must fit.
    std::vector<PoolLayout> poolLayouts(catalogue.pools().size());
    std::vector<bool> poolChanged(catalogue.pools().size(), false);
    for (size_t p = 0; p < catalogue.pools().size(); ++p) {
        std::vector<Work*> members;
        for (Work& w : work)
            if (w.e->pool == static_cast<int>(p)) members.push_back(&w);
        if (std::none_of(members.begin(), members.end(), [](const Work* w) { return w->changed(); })) continue;
        std::vector<const std::vector<uint8_t>*> texts;
        for (const Work* w : members) texts.push_back(&w->text);
        const DosStringPool& pool = catalogue.pools()[p];
        PoolLayout l = layOut(pool.start, texts);
        if (l.used > static_cast<size_t>(pool.end - pool.start)) {
            const size_t over = l.used - (pool.end - pool.start);
            for (Work* w : members)
                if (w->changed())
                    refuse(*w, std::format("the \"{}\" strings need {} bytes, {} more than their place {}..{} holds", pool.name, l.used,
                                           over, dsKey(pool.start), dsKey(static_cast<uint16_t>(pool.end - 1))));
            continue;
        }
        poolLayouts[p] = std::move(l);
        poolChanged[p] = true;
    }

    // 4. Write.
    for (const Work& w : work) {
        const DosStringEntry& e = *w.e;
        if (e.pool < 0 && w.changed()) {
            std::vector<uint8_t> slot(e.max + 1, 0);
            std::copy(w.text.begin(), w.text.end(), slot.begin());
            exe.writeDs(e.ds, slot);
            ++r.changed;
        }
    }
    for (size_t p = 0; p < catalogue.pools().size(); ++p) {
        if (!poolChanged[p]) continue;
        const DosStringPool& pool = catalogue.pools()[p];
        std::vector<uint8_t> bytes(static_cast<size_t>(pool.end - pool.start), 0);
        size_t k = 0;
        for (const Work& w : work) {
            if (w.e->pool != static_cast<int>(p)) continue;
            const uint16_t at = poolLayouts[p].offsets[k++];
            std::copy(w.text.begin(), w.text.end(), bytes.begin() + (at - pool.start));
            for (const uint16_t ptr : w.e->pointers) exe.writeDs16(ptr, at);
            if (w.changed()) ++r.changed;
        }
        exe.writeDs(pool.start, bytes);
    }
    for (const Work& w : work) {
        if (!patchChanged(w)) continue;
        for (int rank = 0; rank < 8; ++rank)
            if (static_cast<int8_t>(exe.ds8(static_cast<uint16_t>(dg::kRankStartMsg + rank))) == w.e->index) {
                exe.writeDs16(static_cast<uint16_t>(dg::kMsgDigitOffsets + 2 * rank), static_cast<uint16_t>(w.patch.countAt));
                exe.writeDs16(static_cast<uint16_t>(dg::kMsgPluralOffsets + 2 * rank), static_cast<uint16_t>(w.patch.pluralAt));
            }
        for (int i = 0; i < 4; ++i)
            if (static_cast<int8_t>(exe.ds8(static_cast<uint16_t>(dg::kEndMsgIds + i))) == w.e->index)
                exe.writeDs16(static_cast<uint16_t>(dg::kMsgDigitOffsets + 2 * (8 + i)), static_cast<uint16_t>(w.patch.countAt));
        ++r.changed;
    }

    // 5. Warnings: changed text drawn outside its place.
    for (const Work& w : work) {
        if (!w.changed()) continue;
        const DosStringEntry& e = *w.e;
        const std::string who = std::format("{} ({})", dsKey(e.ds), e.what);
        const int width = dosTextWidth(font, w.text);
        auto warnIf = [&](bool cond, const std::string& msg) {
            if (cond) r.warnings.push_back(who + ": " + msg);
        };
        switch (e.role) {
            case R::Footer: warnIf(width > 0x4B - 0x0E, std::format("{} pixels wide; the narrowest box leaves {}", 2 * width, 2 * (0x4B - 0x0E))); break;
            case R::HelpTitle: warnIf(width > 0x8E - 4, std::format("{} pixels wide; the help box's title line holds {}", 2 * width, 2 * (0x8E - 4))); break;
            case R::ClueWord:
                warnIf(layout.clueBoxRight > 0 && 0x0C + width > layout.clueBoxRight,
                       std::format("{} pixels wide; the HUD's clue box holds {}", 2 * width, 2 * (layout.clueBoxRight - 0x0C)));
                break;
            case R::SigninHeading:
                warnIf((e.index == 0 ? 0x16 : 0x18) + width > 0x6F, std::format("{} pixels wide; runs out of the sign-in panel", 2 * width));
                break;
            case R::Hint:
            case R::CrownMessage:
                if (layout.cloudRight > 0) {
                    const DosTextExtent x = measureDosText(font, w.text, 0x14, 0x22, 12);
                    warnIf(x.any && (x.left < layout.cloudLeft || x.right > layout.cloudRight || x.top < layout.cloudTop ||
                                     x.bottom > layout.cloudBottom),
                           std::format("the text runs out of Flutter's cloud (pixels {}-{}, rows {}-{}; the cloud is pixels {}-{}, rows {}-{})",
                                       2 * x.left, 2 * x.right - 1, x.top, x.bottom - 1, 2 * layout.cloudLeft, 2 * layout.cloudRight - 1,
                                       layout.cloudTop, layout.cloudBottom - 1));
                }
                break;
            case R::ButtonSmall: warnIf(width >= 0x14, "40 pixels or wider: the button grows and covers the other one"); break;
            case R::ButtonWide: warnIf(width > 0x33, std::format("{} pixels wide; the button holds {}", 2 * width, 2 * 0x33)); break;
            case R::RankName: warnIf(0x3B + width > 0x5E, "runs into the treasure count of the sign-in list"); break;
            default: break;
        }
        for (const FixedDef& f : kFixed)
            if (f.ds == e.ds && f.x >= 0) {
                const DosTextExtent x = measureDosText(font, w.text, f.x, f.y, f.lineHeight);
                warnIf(x.clipped > 0, std::format("{} character(s) past the right edge of the screen are not drawn", x.clipped));
                warnIf(x.any && x.bottom > 200, "runs below the bottom of the screen");
            }
    }
    return r;
}

// ---- palettes.json --------------------------------------------------------------------------------

namespace {

struct PaletteTable {
    const char* key;
    uint16_t ds;
    int count;
    int components;  // 1, or 3 for [r, g, b]
    int max;
};
constexpr PaletteTable kPaletteTables[] = {
    {"ega", dg::kPalEga, 17, 1, 63},
    {"tandy", dg::kPalTandy, 16, 1, 15},
    {"mcga", dg::kPalMcga, 16, 3, 63},
    {"mcgaModeSet", dg::kPalMcgaModeSet, 16, 3, 63},
    {"cga", dg::kPalCga, 16, 1, 255},
};

}  // namespace

std::string exportPalettesJson(const DosExe& exe) {
    json::Value root = json::Value::object();
    root.set("_about", "The colour tables of the four looks in SST.EXE (Treasure Mountain! 1990), written by tmport --export-mod-kit "
                       "from your game files: logical colour i (the PNG palette index) is shown as entry i. Format: docs/MODDING.md.");
    root.set("_ega", "attribute registers, RGBI bits (1 blue, 2 green, 4 red, 16 intensity); the 17th is the border");
    root.set("_tandy", "palette registers, IRGB (1 blue, 2 green, 4 red, 8 intensity)");
    root.set("_mcga", "DAC colours [r, g, b], 0-63 each; mcgaModeSet is replaced right after the mode set (never visible)");
    root.set("_cga", "four 2-bit pixels per colour (most significant first; 0 black, 1 cyan, 2 magenta, 3 white), alternated by row");
    for (const PaletteTable& t : kPaletteTables) {
        json::Value a = json::Value::array();
        for (int i = 0; i < t.count; ++i) {
            if (t.components == 1) {
                a.push(static_cast<int>(exe.ds8(static_cast<uint16_t>(t.ds + i))));
            } else {
                json::Value rgb = json::Value::array();
                for (int k = 0; k < 3; ++k) rgb.push(static_cast<int>(exe.ds8(static_cast<uint16_t>(t.ds + 3 * i + k))));
                a.push(std::move(rgb));
            }
        }
        root.set(t.key, std::move(a));
    }
    return json::write(root);
}

size_t applyPalettesJson(DosExe& exe, std::string_view text) {
    const json::Value v = json::parse(text);
    if (!v.isObject()) throw FormatError("palettes.json must hold a JSON object");
    std::vector<std::pair<uint16_t, uint8_t>> writes;
    for (const auto& [key, val] : v.asObject()) {
        if (key.starts_with('_')) continue;
        const PaletteTable* t = nullptr;
        for (const PaletteTable& p : kPaletteTables)
            if (key == p.key) t = &p;
        if (!t) throw FormatError(std::format("palettes: unknown table \"{}\" (ega, tandy, mcga, mcgaModeSet, cga)", key));
        if (!val.isArray() || static_cast<int>(val.size()) != t->count)
            throw FormatError(std::format("palettes: \"{}\" must be an array of {} entries", key, t->count));
        auto value = [&](const json::Value& n, const std::string& where) -> uint8_t {
            if (!n.isInteger() || n.asInt() < 0 || n.asInt() > t->max)
                throw FormatError(std::format("palettes: {} must be a number 0-{}", where, t->max));
            return static_cast<uint8_t>(n.asInt());
        };
        for (int i = 0; i < t->count; ++i) {
            const json::Value& e = val.asArray()[static_cast<size_t>(i)];
            if (e.isNull()) continue;
            if (t->components == 1) {
                writes.emplace_back(static_cast<uint16_t>(t->ds + i), value(e, std::format("{}[{}]", key, i)));
            } else {
                if (!e.isArray() || e.size() != 3) throw FormatError(std::format("palettes: {}[{}] must be [r, g, b]", key, i));
                for (int k = 0; k < 3; ++k)
                    writes.emplace_back(static_cast<uint16_t>(t->ds + 3 * i + k),
                                        value(e.asArray()[static_cast<size_t>(k)], std::format("{}[{}][{}]", key, i, k)));
            }
        }
    }
    size_t changed = 0;
    for (const auto& [ds, b] : writes) {
        if (exe.ds8(ds) != b) ++changed;
        const uint8_t one[1] = {b};
        exe.writeDs(ds, one);
    }
    return changed;
}

}  // namespace tmport::dos
