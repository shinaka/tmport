#include "mods/mod_kit.h"

#include <algorithm>
#include <format>
#include <system_error>

#include "audio/audio_tables.h"
#include "gfx/pixmap.h"
#include "mods/dam_asm.h"
#include "mods/json.h"
#include "mods/mod_data_source.h"
#include "mods/pmap_codec.h"
#include "mods/resource_import.h"
#include "mods/table_overrides.h"
#include "mods/text_codec.h"
#include "mods/wav_codec.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::mods {

namespace {

constexpr const char* kReadme =
    "This folder was written by tmport --export-mod-kit from YOUR copy of Treasure Mountain!.\n"
    "It is your private copy of the game's data: do not share or publish it.\n"
    "\n"
    "Every file is in an editable format (docs/MODDING.md of the TM Port explains them):\n"
    "  PMAP/<id>.png      256-colour pictures (indexed PNG with the game palette)\n"
    "  PMAP16/<id>.png    16-colour pictures (--16 mode)\n"
    "  SSND/<id>.wav      sounds (8-bit mono WAV)\n"
    "  OTHR/<id>.txt      text with {escape} codes; OTHR/<id>.bin binary tables\n"
    "  DAM/<id>.dam.txt   animation scripts (text assembler)\n"
    "  BNDL/<id>.json     bundle member lists\n"
    "  FFNT/<id>.ffnt     the font (binary)\n"
    "  RRGB/<id>.pal      the palette (768 bytes)\n"
    "  MIDI/<id>.mid      music (E*.MID set); MIDI/<id>.b.mid the B*.MID set\n"
    "  TABLES/*.json      tables of TMTWINCD.EXE\n"
    "\n"
    "To make a mod: create a new folder in a mod directory (e.g. <preference dir>/mods/MyMod),\n"
    "copy ONLY the files you change into the same sub-folders there, and edit them.\n";

class Exporter {
public:
    Exporter(const GameFiles& f, std::filesystem::path out, const ModKitOptions& o) : f_(f), out_(std::move(out)), o_(o) {
        ctx_ = importContextFor(*f.res, &manifest_);
    }

    ModKitReport run() {
        prepare();
        pmaps(gfx::ColorMode::Colour256);
        if (f_.res->hasDll(DllId::TMT16)) pmaps(gfx::ColorMode::Colour16);
        sounds();
        others();
        bundles();
        fontsAndPalette();
        music();
        tables();
        write("README.txt", std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(kReadme), std::char_traits<char>::length(kReadme)), "");
        const std::string manifest = "{\n  \"name\": \"Mod kit exported from your game files\",\n  \"newIds\": [],\n  \"resizable\": []\n}\n";
        write("mod.json", asBytes(manifest), "");
        return std::move(rep_);
    }

private:
    static std::span<const uint8_t> asBytes(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

    void prepare() {
        std::error_code ec;
        if (std::filesystem::exists(out_, ec)) {
            if (!std::filesystem::is_directory(out_, ec)) throw Error(std::format("{} exists and is not a directory", pathToUtf8(out_)));
            if (!o_.force && std::filesystem::directory_iterator(out_, ec) != std::filesystem::directory_iterator())
                throw Error(std::format("{} is not empty (use --force to overwrite the kit there)", pathToUtf8(out_)));
        }
        std::filesystem::create_directories(out_, ec);
        if (ec) throw Error(std::format("cannot create {}: {}", pathToUtf8(out_), ec.message()));
    }

    void write(const std::string& rel, std::span<const uint8_t> bytes, const std::string& group) {
        const auto p = out_ / pathFromUtf8(rel);
        if (!writeWholeFile(p, bytes)) throw Error(std::format("cannot write {}", pathToUtf8(p)));
        ++rep_.files;
        rep_.bytes += bytes.size();
        if (!group.empty()) ++rep_.counts[group];
    }

    void verify(ModFileKind kind, int id, std::span<const uint8_t> file, std::span<const uint8_t> original, const std::string& rel) {
        if (!o_.verify) return;
        try {
            const ImportResult r = importResourceBytes(kind, id, file, ctx_);
            if (r.body.size() == original.size() && std::equal(r.body.begin(), r.body.end(), original.begin())) {
                ++rep_.verified;
                return;
            }
            size_t diff = 0;
            while (diff < r.body.size() && diff < original.size() && r.body[diff] == original[diff]) ++diff;
            rep_.failures.push_back(std::format("{}: round trip differs (sizes {} / {}, first difference at byte {})", rel, r.body.size(),
                                                original.size(), diff));
        } catch (const std::exception& e) {
            rep_.failures.push_back(std::format("{}: re-import failed: {}", rel, e.what()));
        }
    }

    std::vector<int> sortedIds(ResType t, DllId d) {
        auto ids = f_.res->ids(t, d);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    // Exports, writes and verifies one resource.
    void one(ResType t, int id, gfx::ColorMode mode = gfx::ColorMode::Colour256) {
        try {
            const auto e = exportResource(*f_.res, t, id, mode);
            if (!e) return;
            const std::string group = e->rel.substr(0, e->rel.find('/'));
            write(e->rel, e->bytes, group);
            verify(e->kind, e->id, e->bytes, e->original, e->rel);
        } catch (const std::exception& ex) {
            rep_.failures.push_back(std::format("{} {}: {}", resTypeName(t), id, ex.what()));
        }
    }

    void pmaps(gfx::ColorMode mode) {
        const bool c16 = mode == gfx::ColorMode::Colour16;
        for (int id : sortedIds(ResType::PMAP, c16 ? DllId::TMT16 : DllId::TMT256)) one(ResType::PMAP, id, mode);
    }

    void sounds() {
        for (int n = 0; n < 9; ++n)
            for (int id : sortedIds(ResType::SSND, soundDll(n))) one(ResType::SSND, id);
    }

    void others() {
        for (int id : sortedIds(ResType::OTHR, DllId::TMT)) one(ResType::OTHR, id);
    }

    void bundles() {
        for (int id : sortedIds(ResType::BNDL, DllId::TMT)) one(ResType::BNDL, id);
    }

    void fontsAndPalette() {
        for (int id : sortedIds(ResType::FFNT, DllId::TMT)) one(ResType::FFNT, id);
        for (int id : sortedIds(ResType::RRGB, DllId::TMT)) one(ResType::RRGB, id);
    }

    void music() {
        const audio::AudioTables at = audio::AudioTables::fromExe(*f_.exe);
        std::map<std::string, std::filesystem::path> written;  // for the import check
        std::vector<std::pair<std::string, std::vector<uint8_t>>> originals;
        for (size_t i = 0; i < audio::AudioTables::kTunes; ++i) {
            if (at.tuneFiles[i].empty()) continue;
            for (const char set : {'E', 'B'}) {
                std::string name = at.tuneFiles[i];
                name[0] = set;
                const std::string gamePath = at.soundsDir + name;
                const std::string rel = std::format("MIDI/{}{}.mid", at.tuneIds[i], set == 'E' ? "" : ".b");
                auto file = f_.source->open(gamePath);
                if (!file) {
                    rep_.failures.push_back(std::format("{}: {} not found in the game data", rel, gamePath));
                    continue;
                }
                const auto bytes = file->readAll();
                write(rel, bytes, "MIDI");
                written[gamePath] = out_ / pathFromUtf8(rel);
                originals.emplace_back(gamePath, bytes);
                try {
                    validateMidiFile(at.tuneIds[i], bytes);
                } catch (const std::exception& e) {
                    rep_.failures.push_back(std::format("{}: {}", rel, e.what()));
                }
            }
        }
        if (!o_.verify || written.empty()) return;
        // Serve the written files through the same overlay a mod uses and compare.
        struct NoOwn final : vfs::DataSource {
            explicit NoOwn(const vfs::DataSource& b) : b_(b) {}
            std::string describe() const override { return b_.describe(); }
            std::shared_ptr<vfs::File> open(std::string_view p) const override { return b_.open(p); }
            std::vector<vfs::DirEntry> list(std::string_view d) const override { return b_.list(d); }
            const vfs::DataSource& b_;
        };
        const ModDataSource overlay(std::make_unique<NoOwn>(*f_.source), written);
        for (const auto& [path, bytes] : originals) {
            const auto f = overlay.open(path);
            const auto back = f ? f->readAll() : std::vector<uint8_t>{};
            if (back == bytes)
                ++rep_.verified;
            else
                rep_.failures.push_back(std::format("MIDI {}: round trip differs", path));
        }
    }

    void tables() {
        const auto filesOut = exportTableFiles(*f_.exe);
        for (const auto& [name, text] : filesOut) write("TABLES/" + name, asBytes(text), "TABLES");
        if (!o_.verify) return;
        try {
            // Apply the written files to a fresh copy of the EXE's DGROUP: nothing may change.
            const std::shared_ptr<const ne::NeImage> img(&f_.exe->exe(), [](const ne::NeImage*) {});
            ExeTables copy(img);
            for (const auto& [name, text] : filesOut) {
                const auto back = readWholeFile(out_ / "TABLES" / pathFromUtf8(name));
                if (!back) throw Error("cannot read TABLES/" + name);
                applyTablesText(copy, std::string_view(reinterpret_cast<const char*>(back->data()), back->size()));
            }
            const auto a = copy.raw(), b = f_.exe->raw();
            bool same = a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
            for (const auto& [site, p] : f_.exe->farPointers()) {
                const FarPtr q = copy.farPtr(site);
                same = same && q.kind == p.kind && q.offset == p.offset && q.segment == p.segment;
            }
            if (same)
                rep_.verified += filesOut.size();
            else
                rep_.failures.push_back("TABLES: applying the exported tables changed the EXE image");
        } catch (const std::exception& e) {
            rep_.failures.push_back(std::format("TABLES: re-import failed: {}", e.what()));
        }
    }

    const GameFiles& f_;
    std::filesystem::path out_;
    ModKitOptions o_;
    ModManifest manifest_;
    ImportContext ctx_;
    ModKitReport rep_;
};

}  // namespace

std::optional<ExportedFile> exportResource(const ResourceManager& res, ResType type, int id, gfx::ColorMode mode) {
    const bool c16 = type == ResType::PMAP && mode == gfx::ColorMode::Colour16;
    auto body = res.loadOriginal(type, id, c16 ? gfx::ColorMode::Colour16 : gfx::ColorMode::Colour256);
    if (!body) return std::nullopt;
    ExportedFile e;
    e.id = id;
    auto text = [&](const std::string& s) { e.bytes.assign(s.begin(), s.end()); };
    switch (type) {
        case ResType::PMAP: {
            // The palette from the DLL itself (a mod's RRGB must not leak into the user's kit).
            gfx::Palette pal = gfx::Palette::vga16();
            if (!c16) {
                const auto rrgb = res.loadOriginal(ResType::RRGB, 900);
                if (!rrgb) throw FormatError("RRGB 900 is missing");
                pal = gfx::Palette::fromRrgb(*rrgb);
            }
            const gfx::PixMap pm = gfx::decodePmap(*body, c16 ? 4 : 8);
            e.bytes = pixmapToEditablePng(pm, pal);
            e.kind = c16 ? ModFileKind::Pmap16 : ModFileKind::Pmap;
            e.rel = std::format("{}/{}.png", c16 ? "PMAP16" : "PMAP", id);
            break;
        }
        case ResType::SSND:
            e.bytes = ssndToWav(*body);
            e.kind = ModFileKind::Ssnd;
            e.rel = std::format("SSND/{}.wav", id);
            break;
        case ResType::OTHR:
            if (isDamResource(*body)) {
                text(disassembleDam(*body, id));
                e.kind = ModFileKind::Dam;
                e.rel = std::format("DAM/{}.dam.txt", id);
            } else if (const auto payload = othrTextPayload(*body)) {
                text(othrTextFile(*payload));
                e.kind = ModFileKind::OthrText;
                e.rel = std::format("OTHR/{}.txt", id);
            } else {
                e.bytes = *body;
                e.kind = ModFileKind::OthrBin;
                e.rel = std::format("OTHR/{}.bin", id);
            }
            break;
        case ResType::BNDL: {
            const auto b = parseBundle(*body);
            if (!b) throw FormatError("not a BNDL");
            text(bndlJson(b->rawIds));
            e.kind = ModFileKind::Bndl;
            e.rel = std::format("BNDL/{}.json", id);
            break;
        }
        case ResType::FFNT:
            e.bytes = *body;
            e.kind = ModFileKind::Ffnt;
            e.rel = std::format("FFNT/{}.ffnt", id);
            break;
        case ResType::RRGB:
            if (body->size() < 768) throw FormatError("palette shorter than 768 bytes");
            e.bytes.assign(body->begin(), body->begin() + 768);
            e.kind = ModFileKind::Rrgb;
            e.rel = std::format("RRGB/{}.pal", id);
            break;
    }
    e.original = std::move(*body);
    return e;
}

ModKitReport exportModKit(const GameFiles& files, const std::filesystem::path& outDir, const ModKitOptions& options) {
    return Exporter(files, outDir, options).run();
}

}  // namespace tmport::mods
