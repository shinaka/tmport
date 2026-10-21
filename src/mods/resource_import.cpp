#include "mods/resource_import.h"

#include <algorithm>
#include <cstring>
#include <format>

#include "audio/smf.h"
#include "game/quiz.h"
#include "gfx/pixmap.h"
#include "anim/dam_script.h"
#include "mods/dam_asm.h"
#include "mischief/mods/json.h"
#include "mods/mod_common.h"
#include "mods/pmap_codec.h"
#include "mods/text_codec.h"
#include "mods/wav_codec.h"
#include "res/resource_manager.h"
#include "res/ssnd.h"
#include "text/font.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"

namespace tmport::mods {

namespace {

constexpr uint32_t kDefaultSoundRate = 22050;  // every shipped SSND (fmt_sound.md §3.2)
constexpr int kQuestionFirst = 11000, kQuestionLast = 11358;

std::string asText(std::span<const uint8_t> b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }

// The id range must match the kind; a new id needs the manifest.
void checkId(ModFileKind kind, int id, bool originalExists, const ImportContext& ctx) {
    const auto t = resTypeOf(kind);
    if (t && typeForId(id) != *t)
        throw mischief::FormatError(std::format("id {} is a {} id; {} ids are {} (the id range decides the type)", id,
                                      resTypeName(typeForId(id)), resTypeName(*t), idRangeText(*t)));
    if (!originalExists && !(ctx.manifest && ctx.manifest->newIds.contains(id)))
        throw mischief::FormatError(std::format("{} {} does not exist in the game; to add it, list it under \"newIds\" in the mod's mod.json",
                                      t ? std::string(resTypeName(*t)) : std::string("resource"), id));
}

bool resizable(int id, const ImportContext& ctx) { return ctx.manifest && ctx.manifest->resizable.contains(id); }

ImportResult importPmap(ModFileKind kind, int id, std::span<const uint8_t> bytes, const ImportContext& ctx) {
    const bool c16 = kind == ModFileKind::Pmap16;
    const mischief::gfx::ColorMode mode = c16 ? mischief::gfx::ColorMode::Colour16 : mischief::gfx::ColorMode::Colour256;
    auto orig = ctx.original ? ctx.original(ResType::PMAP, id, mode) : std::nullopt;
    int origDepth = c16 ? 4 : 8;
    if (!orig && c16 && ctx.original) {
        // TMT16.DLL is not loaded (256-colour mode): both DLLs hold the same ids and sizes.
        orig = ctx.original(ResType::PMAP, id, mischief::gfx::ColorMode::Colour256);
        origDepth = 8;
    }
    checkId(kind, id, orig.has_value(), ctx);
    const mischief::gfx::Palette pal = c16 ? mischief::gfx::Palette::vga16() : (ctx.palette256 ? ctx.palette256() : mischief::gfx::Palette());
    PmapImage img = pngToIndices(bytes, pal);
    ImportResult r;
    gfx::PixEncoding enc = c16 ? gfx::PixEncoding::Raw4 : gfx::PixEncoding::Rund;
    if (orig) {
        const gfx::PixMap o = gfx::decodePmap(*orig, origDepth);
        if ((o.width != img.width || o.height != img.height) && !resizable(id, ctx))
            throw mischief::FormatError(std::format(
                "the picture is {}x{} but PMAP {} is {}x{}; the game lays out, centres and hit-tests by the original size "
                "(list the id under \"resizable\" in mod.json to allow a new size)",
                img.width, img.height, id, o.width, o.height));
        if (!c16) enc = o.encoding == gfx::PixEncoding::Raw8 ? gfx::PixEncoding::Raw8 : gfx::PixEncoding::Rund;
    }
    if (img.remapped)
        r.notes.push_back(std::format("{} pixel(s) mapped to the nearest of the {} palette colours", img.remapped, pal.size()));
    r.body = encodePmapBody(img, c16 ? 4 : 8, enc);
    return r;
}

ImportResult importSsnd(int id, std::span<const uint8_t> bytes, const ImportContext& ctx) {
    const auto orig = ctx.original ? ctx.original(ResType::SSND, id, mischief::gfx::ColorMode::Colour256) : std::nullopt;
    checkId(ModFileKind::Ssnd, id, orig.has_value(), ctx);
    uint32_t rate = kDefaultSoundRate;
    if (orig)
        if (auto info = parseSsnd(*orig); info && info->sampleRate >= 1000) rate = info->sampleRate;
    ImportResult r;
    std::string note;
    r.body = wavToSsnd(bytes, rate, &note);
    r.notes.push_back(note);
    const auto info = parseSsnd(r.body);
    if (info && info->sampleCount > static_cast<uint32_t>(rate) * 120u)
        r.warnings.push_back(std::format("the sound is {} s long", info->sampleCount / rate));
    return r;
}

// OTHR 11000-11358: the puzzler grammar (fmt_othr_puzzles.md §4.1) and its narration ids.
void checkQuestion(int id, std::span<const uint8_t> body, const ImportContext& ctx, ImportResult& r) {
    const auto q = game::parseQuestion(body);
    if (!q || !q->terminated)
        throw mischief::FormatError(std::format(
            "question {} must end its text with two empty lines (three line breaks in a row), followed by three answers", id));
    if (q->answerCount != 3)
        throw mischief::FormatError(std::format("question {} needs exactly three answers, one per line; the first one is the correct answer", id));
    for (int a = 0; a < 3; ++a) {
        const auto s = q->answer(a);
        if (s.find('\xFF') != std::string_view::npos)
            throw mischief::FormatError(std::format("question {}: answer {} contains a control code (answers are plain text)", id, a + 1));
        if (a < 2 && s.empty()) throw mischief::FormatError(std::format("question {}: answer {} is empty", id, a + 1));
    }
    if (q->answer(2).empty() || q->answer(2) == "\r")
        throw mischief::FormatError(std::format("question {}: answer 3 is empty", id));
    if (q->lineSlots > 10) throw mischief::FormatError(std::format("question {} has {} lines; the question screen has room for 10", id, q->lineSlots));
    if (ctx.original)
        if (const auto o = ctx.original(ResType::OTHR, id, mischief::gfx::ColorMode::Colour256))
            if (const auto oq = game::parseQuestion(*o); oq && oq->numLines != q->numLines)
                r.warnings.push_back(std::format(
                    "question {} has {} text line(s), the original {}: the narration (SSND ids from the table at DS:1990, one "
                    "clip per line and answer) follows the original count, so with Voice on the wrong clips would play; "
                    "provide matching SSND overrides or keep the line count",
                    id, q->numLines, oq->numLines));
}

ImportResult importOthr(ModFileKind kind, int id, std::span<const uint8_t> bytes, const ImportContext& ctx) {
    const auto orig = ctx.original ? ctx.original(ResType::OTHR, id, mischief::gfx::ColorMode::Colour256) : std::nullopt;
    checkId(kind, id, orig.has_value(), ctx);
    ImportResult r;
    if (kind == ModFileKind::OthrText) {
        r.body = othrBodyFromTextFile(asText(bytes));
    } else if (kind == ModFileKind::Dam) {
        DamAssembly a = assembleDam(asText(bytes));
        for (auto& w : a.warnings) r.warnings.push_back(std::move(w));
        if (ctx.exists)
            for (int s : a.sounds)
                if (!ctx.exists(anim::kSfxBase + s))
                    r.warnings.push_back(std::format("sound {} (SSND {}) does not exist", s, anim::kSfxBase + s));
        a.bytes.resize(a.paddedSize, 0);
        r.body = std::move(a.bytes);
    } else {
        if (bytes.empty()) throw mischief::FormatError("the file is empty");
        r.body.assign(bytes.begin(), bytes.end());
        padToNe(r.body);
    }
    if (orig && isDamResource(*orig) && !isDamResource(r.body))
        r.warnings.push_back(std::format("OTHR {} is a DAM animation script in the game but the override has no DAM tag", id));
    if (id >= kQuestionFirst && id <= kQuestionLast) checkQuestion(id, r.body, ctx, r);
    if (id >= 10550 && id <= 10553 && r.body.size() >= 2) {
        const int count = static_cast<int16_t>(r.body[0] | (r.body[1] << 8));
        if (count <= 0 || static_cast<size_t>(count) * 38 + 2 > r.body.size())
            throw mischief::FormatError(std::format("OTHR {}: the layout table declares {} records of 38 bytes that do not fit", id, count));
        if (count < 3)  // a climb draws three different records (REVIEW_1 M-3)
            throw mischief::FormatError(std::format("OTHR {}: the layout table has {} records; a climb needs at least 3", id, count));
    }
    return r;
}

ImportResult importBndl(int id, std::span<const uint8_t> bytes, const ImportContext& ctx) {
    const auto orig = ctx.original ? ctx.original(ResType::BNDL, id, mischief::gfx::ColorMode::Colour256) : std::nullopt;
    checkId(ModFileKind::Bndl, id, orig.has_value(), ctx);
    ImportResult r;
    std::vector<uint32_t> members;
    r.body = bndlBodyFromJson(asText(bytes), &members);
    const int n = static_cast<int>(members.size());
    int origCount = -1;
    if (orig && orig->size() >= 8 && std::memcmp(orig->data(), "BNDL", 4) == 0) origCount = (*orig)[4] | ((*orig)[5] << 8);
    if (const auto rule = bundleRule(id)) {
        if (n < rule->minCount || n > rule->maxCount) {
            if (rule->minCount == rule->maxCount)
                throw mischief::FormatError(std::format("BNDL {} ({}) must have exactly {} members, not {}", id, rule->what, rule->minCount, n));
            throw mischief::FormatError(std::format("BNDL {} ({}) must have {} to {} members, not {}", id, rule->what, rule->minCount, rule->maxCount, n));
        }
        if (rule->firstIsScript && n > 0 && typeForId(static_cast<int>(members[0] & 0xFFFF)) != ResType::OTHR)
            throw mischief::FormatError(std::format("BNDL {} ({}): member 0 must be its DAM script (an OTHR id)", id, rule->what));
    } else if (origCount >= 0 && n != origCount && !resizable(id, ctx)) {
        throw mischief::FormatError(std::format(
            "BNDL {} has {} members but the game's has {}; the code reads members by position (list the id under \"resizable\" in "
            "mod.json if this bundle may change size)",
            id, n, origCount));
    }
    if (ctx.exists)
        for (uint32_t m : members) {
            const int mid = static_cast<int>(m & 0xFFFF);
            if (!ctx.exists(mid)) r.warnings.push_back(std::format("member {} does not exist in the game or in a mod", mid));
        }
    return r;
}

ImportResult importFfnt(int id, std::span<const uint8_t> bytes, const ImportContext& ctx) {
    const auto orig = ctx.original ? ctx.original(ResType::FFNT, id, mischief::gfx::ColorMode::Colour256) : std::nullopt;
    checkId(ModFileKind::Ffnt, id, orig.has_value(), ctx);
    ImportResult r;
    r.body.assign(bytes.begin(), bytes.end());
    padToNe(r.body);
    const text::Font f = text::Font::parse(r.body);  // throws FormatError for a broken font
    if (f.glyphCount() < 256) r.warnings.push_back(std::format("the font has {} glyphs; bytes past them draw nothing", f.glyphCount()));
    if (orig) {
        const text::Font o = text::Font::parse(*orig);
        if (o.height() != f.height())
            r.warnings.push_back(std::format("the font is {} px high, the original {}: text boxes and line spacing change", f.height(), o.height()));
    }
    return r;
}

ImportResult importRrgb(int id, std::span<const uint8_t> bytes, const ImportContext& ctx) {
    const auto orig = ctx.original ? ctx.original(ResType::RRGB, id, mischief::gfx::ColorMode::Colour256) : std::nullopt;
    checkId(ModFileKind::Rrgb, id, orig.has_value(), ctx);
    if (bytes.size() != 768) throw mischief::FormatError(std::format("a palette file holds 768 bytes (256 x R,G,B), not {}", bytes.size()));
    ImportResult r;
    r.body.assign(bytes.begin(), bytes.end());
    padToNe(r.body);
    bool hasKey = false;
    for (size_t i = 0; i < 768; i += 3) hasKey = hasKey || (bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 0xAA);
    if (!hasKey) r.warnings.push_back("no entry is the colour key (0,0,170): sprites will have no transparent pixels");
    return r;
}

}  // namespace

std::optional<BundleRule> bundleRule(int id) {
    // re/docs/fmt_bndl_ffnt.md §2.3 and PORTING_NOTES.md §7 (constraints a mod must respect).
    if (id == 300) return BundleRule{69, 69, false, "player frames"};
    if (id == 301) return BundleRule{10, 10, false, "status panel art"};
    if (id == 302) return BundleRule{6, 6, false, "net-shop sign"};
    if (id >= 311 && id <= 320) return BundleRule{1, 10, false, "elf animation set"};
    if (id == 327) return BundleRule{33, 0x7FFF, false, "hint texts"};
    if (id == 328) return BundleRule{1, 0x7FFF, false, "fountain cycle"};
    if (id >= 330 && id <= 380) return BundleRule{1, 10, false, "treasure frames"};
    if (id == 323 || id == 324 || id == 325 || id == 400 || id == 401 || id == 402 || id == 410 || id == 412)
        return BundleRule{2, 101, true, "DAM animation"};
    return std::nullopt;
}

std::vector<uint8_t> bndlBodyFromJson(std::string_view text, std::vector<uint32_t>* membersOut) {
    const mischief::mods::json::Value v = mischief::mods::json::parse(text);
    const mischief::mods::json::Value* list = &v;
    if (v.isObject()) {
        list = v.find("members");
        if (!list) throw mischief::FormatError("a BNDL file is a JSON array of ids or an object with a \"members\" array");
    }
    if (!list->isArray()) throw mischief::FormatError("\"members\" must be an array of resource ids");
    if (list->size() > 0x7FFF) throw mischief::FormatError("too many members");
    std::vector<uint32_t> members;
    for (size_t i = 0; i < list->size(); ++i) {
        const auto& e = list->asArray()[i];
        if (!e.isInteger() || e.asInt() < 0 || e.asInt() > 0xFFFFFFFFll)
            throw mischief::FormatError(std::format("member {} is not a resource id", i));
        if ((e.asInt() & 0xFFFF) > 32767) throw mischief::FormatError(std::format("member {}: id {} is outside 0-32767", i, e.asInt()));
        members.push_back(static_cast<uint32_t>(e.asInt()));
    }
    std::vector<uint8_t> b = {'B', 'N', 'D', 'L', static_cast<uint8_t>(members.size() & 0xFF), static_cast<uint8_t>(members.size() >> 8), 0, 0};
    for (uint32_t m : members)
        for (int k = 0; k < 4; ++k) b.push_back(static_cast<uint8_t>(m >> (8 * k)));
    padToNe(b);
    if (membersOut) *membersOut = std::move(members);
    return b;
}

std::string bndlJson(std::span<const uint32_t> members) {
    mischief::mods::json::Value arr = mischief::mods::json::Value::array();
    for (uint32_t m : members) arr.push(mischief::mods::json::Value(static_cast<int64_t>(m)));
    mischief::mods::json::Value o = mischief::mods::json::Value::object();
    o.set("members", std::move(arr));
    return mischief::mods::json::write(o);
}

void validateMidiFile(int id, std::span<const uint8_t> bytes) {
    if (id < 9000 || id > 9009) throw mischief::FormatError(std::format("music ids are 9000-9009, not {}", id));
    if (bytes.size() < 14 || std::memcmp(bytes.data(), "MThd", 4) != 0) throw mischief::FormatError("not a Standard MIDI File (no MThd header)");
    const audio::MidiSong s = audio::parseSmf(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    if (s.noteOnCount() == 0) throw mischief::FormatError("the MIDI file plays no notes");
}

ImportResult importResourceBytes(ModFileKind kind, int id, std::span<const uint8_t> bytes, const ImportContext& ctx) {
    switch (kind) {
        case ModFileKind::Pmap:
        case ModFileKind::Pmap16: return importPmap(kind, id, bytes, ctx);
        case ModFileKind::Ssnd: return importSsnd(id, bytes, ctx);
        case ModFileKind::OthrBin:
        case ModFileKind::OthrText:
        case ModFileKind::Dam: return importOthr(kind, id, bytes, ctx);
        case ModFileKind::Bndl: return importBndl(id, bytes, ctx);
        case ModFileKind::Ffnt: return importFfnt(id, bytes, ctx);
        case ModFileKind::Rrgb: return importRrgb(id, bytes, ctx);
        case ModFileKind::Midi:
        case ModFileKind::MidiE:
        case ModFileKind::MidiB: break;
    }
    throw mischief::FormatError("not a resource override");
}

ImportResult importResourceFile(const ModFile& file, const ImportContext& ctx) {
    const auto bytes = mischief::readWholeFile(file.path);
    if (!bytes) throw mischief::FormatError("cannot read the file");
    return importResourceBytes(file.kind, file.id, *bytes, ctx);
}

ImportContext importContextFor(const ResourceManager& res, const ModManifest* manifest) {
    ImportContext c;
    const ResourceManager* r = &res;
    c.original = [r](ResType t, int id, mischief::gfx::ColorMode m) { return r->loadOriginal(t, id, m); };
    c.palette256 = [r] { return r->palette(900); };
    c.exists = [r](int id) { return r->exists(id); };
    c.manifest = manifest;
    return c;
}

}  // namespace tmport::mods
