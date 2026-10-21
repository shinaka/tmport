#include "dos/mods/dos_mod_kit.h"

#include <format>
#include <system_error>

#include "dos/data/dos_files.h"
#include "dos/data/puzzles.h"
#include "dos/gfx/pak.h"
#include "dos/gfx/pan.h"
#include "dos/mods/dos_exe_strings.h"
#include "dos/mods/dos_mods.h"
#include "dos/mods/dos_pictures.h"
#include "dos/mods/dos_sound_text.h"
#include "dos/mods/dos_text_files.h"
#include "mischief/mods/json.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"

namespace tmport::dos {

namespace {

constexpr const char* kReadme =
    "This folder was written by tmport --export-mod-kit from YOUR copy of Treasure Mountain! (1990 DOS edition).\n"
    "It is your private copy of the game's data: do not share or publish it.\n"
    "\n"
    "Every file is in an editable format (docs/MODDING.md of the TM Port, \"The 1990 DOS edition\"):\n"
    "  PAK/<STEM>/<k>.png     sprite frames (16 colours: the palette index is the game's colour number)\n"
    "  PAK/<STEM>.json        frame hotspots (x in 2-pixel steps, y in rows) and header words\n"
    "  PAN/<STEM>/<k>.png     scenery pictures (OPENING: the title picture; the others: 128x128 blocks)\n"
    "  SOUND/<NAME>.txt       music and sound effects as note text\n"
    "  TEXT/ALL_PUZS.txt      the elf questions; TEXT/PRG_DICT.txt their word table; TEXT/HELP.DAT.txt the help\n"
    "  EXE/strings.json       messages, box, menu, button, hint and title texts of SST.EXE\n"
    "  EXE/palettes.json      the colour tables of the EGA, Tandy, MCGA and CGA looks\n"
    "\n"
    "To make a mod: create a new folder in a DOS mod directory (e.g. <preference dir>/dos_mods),\n"
    "copy ONLY the files you change into the same sub-folders there, and edit them. In strings.json\n"
    "keep only the entries you change. Check it with: tmport --check-mods <folder> --edition dos\n";

class Exporter {
public:
    Exporter(const DosFiles& f, std::filesystem::path out, const DosModKitOptions& o) : f_(f), out_(std::move(out)), o_(o) {}

    DosModKitReport run() {
        prepare();
        const DosExe& exe = f_.exe();
        const DosColours colours = dosPngColours(exe);
        for (const FileIndex i : {FileIndex::SpritesPak, FileIndex::BeginPak, FileIndex::ClosePak, FileIndex::ShackPak}) pak(i, colours);
        for (const FileIndex i : {FileIndex::OpeningPan, FileIndex::MountainPan, FileIndex::Mount2n3Pan, FileIndex::Mount3Pan,
                                  FileIndex::ClosePan, FileIndex::ShackPan})
            pan(i, colours);
        for (const FileIndex i : {FileIndex::TunesIbm, FileIndex::SfxIbm, FileIndex::TunesTan, FileIndex::SfxTan})
            guarded("SOUND/" + name(i) + ".txt", [&] { writeText("SOUND/" + name(i) + ".txt", soundFileToText(f_.bytes(i), i, name(i)), "SOUND"); });
        guarded("TEXT/ALL_PUZS.txt", [&] {
            const Dictionary dict = Dictionary::parse(f_.bytes(FileIndex::PrgDict));
            writeText("TEXT/" + name(FileIndex::AllPuzs) + ".txt", puzzlesToText(PuzzleFile::parse(f_.bytes(FileIndex::AllPuzs)), dict, exe), "TEXT");
        });
        guarded("TEXT/PRG_DICT.txt",
                [&] { writeText("TEXT/" + name(FileIndex::PrgDict) + ".txt", dictionaryToText(f_.bytes(FileIndex::PrgDict)), "TEXT"); });
        guarded("TEXT/HELP.DAT.txt",
                [&] { writeText("TEXT/" + name(FileIndex::HelpDat) + ".txt", helpToText(f_.bytes(FileIndex::HelpDat)), "TEXT"); });
        guarded("EXE/strings.json", [&] { writeText("EXE/strings.json", exportStringsJson(exe), "EXE"); });
        guarded("EXE/palettes.json", [&] { writeText("EXE/palettes.json", exportPalettesJson(exe), "EXE"); });
        writeText("README.txt", kReadme, "");
        writeText("mod.json", "{\n  \"name\": \"DOS mod kit exported from your game files\",\n  \"edition\": \"dos\",\n  \"resizable\": []\n}\n", "");
        if (o_.verify) verify();
        return std::move(rep_);
    }

private:
    std::string name(FileIndex i) const { return mischief::toUpperAscii(f_.fileTable().name(i)); }
    std::string stem(FileIndex i) const {
        const std::string n = name(i);
        return n.substr(0, n.find('.'));
    }

    template <class F>
    void guarded(const std::string& what, F&& f) {
        try {
            f();
        } catch (const std::exception& e) {
            rep_.failures.push_back(std::format("{}: {}", what, e.what()));
        }
    }

    void prepare() {
        std::error_code ec;
        if (std::filesystem::exists(out_, ec)) {
            if (!std::filesystem::is_directory(out_, ec)) throw mischief::Error(std::format("{} exists and is not a directory", mischief::pathToUtf8(out_)));
            if (!o_.force && std::filesystem::directory_iterator(out_, ec) != std::filesystem::directory_iterator())
                throw mischief::Error(std::format("{} is not empty (use --force to overwrite the kit there)", mischief::pathToUtf8(out_)));
        }
        std::filesystem::create_directories(out_, ec);
        if (ec) throw mischief::Error(std::format("cannot create {}: {}", mischief::pathToUtf8(out_), ec.message()));
    }

    void write(const std::string& rel, std::span<const uint8_t> bytes, const std::string& group) {
        const std::filesystem::path p = out_ / mischief::pathFromUtf8(rel);
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (!mischief::writeWholeFile(p, bytes)) throw mischief::Error(std::format("cannot write {}", mischief::pathToUtf8(p)));
        ++rep_.files;
        rep_.bytes += bytes.size();
        if (!group.empty()) ++rep_.counts[group];
    }
    void writeText(const std::string& rel, const std::string& text, const std::string& group) {
        write(rel, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()), group);
    }

    void pak(FileIndex i, const DosColours& colours) {
        guarded(name(i), [&] {
            const PakFile p = PakFile::load(f_.bytes(i), name(i));
            mischief::mods::json::Value frames = mischief::mods::json::Value::object();
            for (const PakFrame& fr : p.frames()) {
                write(std::format("PAK/{}/{:03}.png", stem(i), fr.index), logicalToPng(pakFramePixels(fr), colours, true), "PAK");
                if (fr.word0 != 0) rep_.failures.push_back(std::format("{} frame {}: header word 0 is not 0 (not kept by the kit)", name(i), fr.index));
                mischief::mods::json::Value o = mischief::mods::json::Value::object();
                o.set("x", fr.xOffset);
                o.set("y", fr.yOffset);
                o.set("a", static_cast<int>(fr.fieldA));
                o.set("c", static_cast<int>(fr.fieldC));
                o.set("e", static_cast<int>(fr.fieldE));
                frames.set(std::to_string(fr.index), std::move(o));
            }
            mischief::mods::json::Value root = mischief::mods::json::Value::object();
            root.set("_about", std::format("The frame headers of {}: x = hotspot in 2-pixel steps, y = hotspot in rows (for the status "
                                           "bar, cloud and title frames: the position on the screen); a, c, e = header words the "
                                           "game does not use. Keep only the frames you change in a mod.",
                                           name(i)));
            root.set("frames", std::move(frames));
            writeText(std::format("PAK/{}.json", stem(i)), mischief::mods::json::write(root), "PAK");
            rep_.frames += static_cast<size_t>(p.count());
            rep_.framesReproduced += static_cast<size_t>(pakFramesReproduced(f_.bytes(i), name(i)));
        });
    }

    void pan(FileIndex i, const DosColours& colours) {
        guarded(name(i), [&] {
            const PanContent c = panContent(PanFile::load(f_.bytes(i), name(i)));
            for (int k = 0; k < c.pictures; ++k)
                write(std::format("PAN/{}/{:03}.png", stem(i), k), logicalToPng(panPicture(c, k), colours, false), "PAN");
            ++rep_.pictureFiles;
            const auto again = encodePan(c);
            const auto orig = f_.bytes(i);
            if (std::equal(again.begin(), again.end(), orig.begin(), orig.end())) ++rep_.picturesReproduced;
        });
    }

    void verify() {
        DosExe exe = f_.exe();
        DosFileSet files = f_.fileSet();
        const DosModsReport r = applyDosModFolders({scanDosModFolder(out_)}, exe, files);
        for (const auto& e : r.errors) rep_.failures.push_back("import: " + e);
        for (int i = 0; i < FileTable::kCount; ++i) {
            const auto idx = static_cast<FileIndex>(i);
            if (idx == FileIndex::UsersDat) continue;
            if (files[static_cast<size_t>(i)] == f_.fileSet()[static_cast<size_t>(i)])
                ++rep_.verified;
            else
                rep_.failures.push_back(std::format("{}: the kit imports to different bytes", name(idx)));
        }
        const auto a = exe.image(), b = f_.exe().image();
        if (std::equal(a.begin(), a.end(), b.begin(), b.end()))
            ++rep_.verified;
        else
            rep_.failures.push_back("SST.EXE: the kit's strings and palettes import to a different DGROUP");
        if (rep_.framesReproduced != rep_.frames)
            rep_.failures.push_back(std::format("PAK packer: {} of {} frames reproduced", rep_.framesReproduced, rep_.frames));
        if (rep_.picturesReproduced != rep_.pictureFiles)
            rep_.failures.push_back(std::format("PAN packer: {} of {} files reproduced", rep_.picturesReproduced, rep_.pictureFiles));
    }

    const DosFiles& f_;
    std::filesystem::path out_;
    DosModKitOptions o_;
    DosModKitReport rep_;
};

}  // namespace

DosModKitReport exportDosModKit(const DosFiles& files, const std::filesystem::path& outDir, const DosModKitOptions& options) {
    return Exporter(files, outDir, options).run();
}

}  // namespace tmport::dos
