#include "dos/mods/dos_mods.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <system_error>

#include "dos/audio/sound_data.h"
#include "dos/data/puzzles.h"
#include "dos/gfx/pak.h"
#include "dos/gfx/pan.h"
#include "dos/mods/dos_exe_strings.h"
#include "dos/mods/dos_pictures.h"
#include "dos/mods/dos_sound_text.h"
#include "dos/mods/dos_text_files.h"
#include "dos/text/dos_font.h"
#include "mods/json.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::dos {

namespace {

namespace json = mods::json;

std::string readText(const std::filesystem::path& p) {
    const auto bytes = readWholeFile(p);
    if (!bytes) throw FormatError("cannot read the file");
    return std::string(bytes->begin(), bytes->end());
}

std::vector<uint8_t> readBytes(const std::filesystem::path& p) {
    auto bytes = readWholeFile(p);
    if (!bytes) throw FormatError("cannot read the file");
    return std::move(*bytes);
}

bool isPak(FileIndex i) {
    return i == FileIndex::SpritesPak || i == FileIndex::BeginPak || i == FileIndex::ClosePak || i == FileIndex::ShackPak;
}
bool isPan(FileIndex i) {
    return i == FileIndex::MountainPan || i == FileIndex::Mount2n3Pan || i == FileIndex::Mount3Pan || i == FileIndex::ClosePan ||
           i == FileIndex::ShackPan || i == FileIndex::OpeningPan;
}
bool isText(FileIndex i) { return i == FileIndex::AllPuzs || i == FileIndex::PrgDict || i == FileIndex::HelpDat; }

uint64_t fnv(uint64_t h, std::span<const uint8_t> b) {
    for (const uint8_t c : b) h = (h ^ c) * 1099511628211ull;
    return (h ^ b.size()) * 1099511628211ull;
}

// A mod file with the manifest of its folder.
struct Item {
    const DosModFile* file = nullptr;
    const DosModManifest* manifest = nullptr;
};

class Applier {
public:
    Applier(const std::vector<DosModFolder>& folders, DosExe& exe, DosFileSet& files) : folders_(folders), exe_(exe), files_(files) {}

    DosModsReport run() {
        const DosExe origExe = exe_;
        const DosFileSet origFiles = files_;
        for (const DosModFolder& f : folders_) {
            rep_.folders.push_back(f.dir);
            rep_.modFiles += f.files.size();
            rep_.warnings.insert(rep_.warnings.end(), f.warnings.begin(), f.warnings.end());
            rep_.errors.insert(rep_.errors.end(), f.errors.begin(), f.errors.end());
        }
        try {
            table_ = FileTable::read(exe_);
            font_ = DosFont::fromExe(exe_);
        } catch (const std::exception& e) {
            rep_.errors.push_back(std::format("SST.EXE: {} (no mod is applied)", e.what()));
            return std::move(rep_);
        }
        collect();
        rawFiles();
        palettes();
        paks();
        pans();
        sounds();
        texts();
        strings();
        for (int i = 0; i < FileTable::kCount; ++i)
            if (files_[static_cast<size_t>(i)] != origFiles[static_cast<size_t>(i)])
                rep_.changedFiles.push_back(toUpperAscii(table_.entry(i).name));
        rep_.exeChanged = !std::equal(exe_.image().begin(), exe_.image().end(), origExe.image().begin(), origExe.image().end());
        return std::move(rep_);
    }

private:
    std::string nameOf(FileIndex i) const { return toUpperAscii(table_.name(i)); }
    void error(const DosModFile& f, const std::string& msg) { rep_.errors.push_back(std::format("{}: {}", pathToUtf8(f.path), msg)); }
    void warn(const DosModFile& f, const std::string& msg) { rep_.warnings.push_back(std::format("{}: {}", pathToUtf8(f.path), msg)); }

    // The data file a mod file names, if it is one its folder may change.
    std::optional<FileIndex> target(const DosModFile& f) {
        const auto i = table_.indexOf(f.target);
        const auto idx = i ? std::optional<FileIndex>(static_cast<FileIndex>(*i)) : std::nullopt;
        auto refuse = [&](const char* expected) -> std::optional<FileIndex> {
            error(f, std::format("{} is not {} of the DOS edition", f.target, expected));
            return std::nullopt;
        };
        switch (f.kind) {
            case DosModFileKind::PakFrame:
            case DosModFileKind::PakHeaders:
                if (!idx || !isPak(*idx)) return refuse("a sprite pack (SPRITES, BEGIN, CLOSE, SHACK)");
                break;
            case DosModFileKind::PanPicture:
                if (!idx || !isPan(*idx)) return refuse("a picture file (OPENING, MOUNTAIN, MOUNT2N3, MOUNT3, CLOSE, SHACK)");
                break;
            case DosModFileKind::SoundText:
                if (!idx || !isDosSoundFile(*idx)) return refuse("a sound file (TUNES.IBM, SFX.IBM, TUNES.TAN, SFX.TAN)");
                break;
            case DosModFileKind::TextFile:
                if (!idx || !isText(*idx)) return refuse("a text file (ALL_PUZS, PRG_DICT, HELP.DAT)");
                break;
            case DosModFileKind::RawFile:
                if (!idx && iequalsAscii(f.target, DosFiles::kExeName)) {
                    error(f, "SST.EXE cannot be replaced; its strings and colour tables go in EXE/strings.json and EXE/palettes.json");
                    return std::nullopt;
                }
                if (!idx) return refuse("a data file");
                if (*idx == FileIndex::UsersDat) {
                    error(f, "USERS.DAT holds the players, not game data: the TM Port keeps them in its preference folder");
                    return std::nullopt;
                }
                break;
            case DosModFileKind::Strings:
            case DosModFileKind::Palettes: break;
        }
        return idx;
    }

    void collect() {
        for (const DosModFolder& folder : folders_)
            for (const DosModFile& f : folder.files) {
                const Item item{&f, &folder.manifest};
                if (f.kind == DosModFileKind::Strings) {
                    strings_.push_back(item);
                    continue;
                }
                if (f.kind == DosModFileKind::Palettes) {
                    palettes_.push_back(item);
                    continue;
                }
                const auto idx = target(f);
                if (!idx) continue;
                switch (f.kind) {
                    case DosModFileKind::RawFile: raw_[*idx] = item; break;
                    case DosModFileKind::PakFrame: frames_[*idx][f.index] = item; break;
                    case DosModFileKind::PakHeaders: headers_[*idx].push_back(item); break;
                    case DosModFileKind::PanPicture: pictures_[*idx][f.index] = item; break;
                    case DosModFileKind::SoundText: sounds_[*idx] = item; break;
                    case DosModFileKind::TextFile: texts_[*idx] = item; break;
                    default: break;
                }
            }
    }

    std::vector<uint8_t>& file(FileIndex i) { return files_[static_cast<size_t>(i)]; }

    // What a replaced file must pass: the checks of DosFiles::open and the decoders the game runs on
    // it (DosGfxFiles decodes every PAN and PAK, the sound driver parses the sound files), so a mod
    // can never stop the game from starting. Throws tmport::FormatError.
    void checkLoadable(FileIndex idx, std::span<const uint8_t> bytes) {
        const std::string name = nameOf(idx);
        checkDosDataFile(idx, name, bytes);
        if (isPak(idx)) (void)PakFile::load(bytes, name);
        if (isPan(idx)) (void)PanFile::load(bytes, name);
        if (isDosSoundFile(idx)) {
            const DosSoundFormat fmt = dosSoundFormat(idx);
            (void)SoundFile::parse(bytes, fmt.voices, fmt.entries, fmt.bufferBytes, name);
        }
        if (idx == FileIndex::AllPuzs) checkPuzzleWindows(exe_, bytes);
    }

    void rawFiles() {
        for (const auto& [idx, item] : raw_) {
            try {
                auto bytes = readBytes(item.file->path);
                checkLoadable(idx, bytes);
                file(idx) = std::move(bytes);
                ++rep_.applied;
            } catch (const std::exception& e) {
                error(*item.file, std::format("{} (the game's own file is used)", e.what()));
            }
        }
    }

    void palettes() {
        for (const Item& item : palettes_) {
            try {
                (void)applyPalettesJson(exe_, readText(item.file->path));
                ++rep_.applied;
            } catch (const std::exception& e) {
                error(*item.file, std::format("{} (the file is ignored)", e.what()));
            }
        }
        colours_ = dosPngColours(exe_);
    }

    // PAK/<STEM>.json: {"frames": {"72": {"x": -3, "y": -20, "a": 0, "c": 150, "e": 150}}}.
    void readHeaders(const Item& item, const PakFile& pak, std::map<int, PakFrameEdit>& edits) {
        const json::Value v = json::parse(readText(item.file->path));
        const json::Value* frames = v.isObject() ? v.find("frames") : nullptr;
        if (!frames || !frames->isObject()) throw FormatError("needs an object \"frames\" keyed by frame number");
        std::map<int, PakFrameEdit> parsed;
        for (const auto& [key, val] : frames->asObject()) {
            if (key.starts_with('_')) continue;
            int k = -1;
            const auto [p, ec] = std::from_chars(key.data(), key.data() + key.size(), k);
            if (ec != std::errc() || p != key.data() + key.size() || k < 0 || k >= pak.count())
                throw FormatError(std::format("frames: \"{}\" is not a frame of {} (0-{})", key, pak.name(), pak.count() - 1));
            if (!val.isObject()) throw FormatError(std::format("frames: \"{}\" must be an object", key));
            PakFrameEdit& e = parsed[k];
            for (const auto& [field, n] : val.asObject()) {
                if (field.starts_with('_')) continue;
                const bool signedField = field == "x" || field == "y";
                const bool wordField = field == "a" || field == "c" || field == "e";
                if (!signedField && !wordField)
                    throw FormatError(std::format("frames: \"{}\".{}: unknown field (x, y, a, c, e)", key, field));
                const int64_t lo = signedField ? -32768 : 0, hi = signedField ? 32767 : 65535;
                if (!n.isInteger() || n.asInt() < lo || n.asInt() > hi)
                    throw FormatError(std::format("frames: \"{}\".{} must be a number {}..{}", key, field, lo, hi));
                const int x = static_cast<int>(n.asInt());
                if (field == "x") e.xOffset = x;
                if (field == "y") e.yOffset = x;
                if (field == "a") e.fieldA = static_cast<uint16_t>(x);
                if (field == "c") e.fieldC = static_cast<uint16_t>(x);
                if (field == "e") e.fieldE = static_cast<uint16_t>(x);
            }
        }
        for (auto& [k, e] : parsed) {
            PakFrameEdit& d = edits[k];
            if (e.xOffset) d.xOffset = e.xOffset;
            if (e.yOffset) d.yOffset = e.yOffset;
            if (e.fieldA) d.fieldA = e.fieldA;
            if (e.fieldC) d.fieldC = e.fieldC;
            if (e.fieldE) d.fieldE = e.fieldE;
        }
    }

    void paks() {
        std::set<FileIndex> targets;
        for (const auto& [i, m] : frames_) targets.insert(i);
        for (const auto& [i, v] : headers_) targets.insert(i);
        for (const FileIndex idx : targets) {
            const std::string name = nameOf(idx);
            std::optional<PakFile> loaded;
            try {
                loaded = PakFile::load(file(idx), name);
            } catch (const std::exception& e) {
                rep_.errors.push_back(std::format("{} (no frame of {} is replaced)", e.what(), name));
                continue;
            }
            const PakFile& pak = *loaded;
            std::map<int, PakFrameEdit> edits;
            size_t used = 0;
            for (const Item& item : headers_[idx]) {
                try {
                    readHeaders(item, pak, edits);
                    ++used;
                } catch (const std::exception& e) {
                    error(*item.file, std::format("{} (the file is ignored)", e.what()));
                }
            }
            for (const auto& [k, item] : frames_[idx]) {
                try {
                    if (k >= pak.count()) throw FormatError(std::format("{} has frames 0-{}", name, pak.count() - 1));
                    LogicalImage img = pngToLogical(readBytes(item.file->path), colours_, true);
                    const PakFrame& f = pak.frame(k);
                    if ((img.width != f.widthPixels() || img.height != f.height) && !item.manifest->frameResizable(name, k))
                        throw FormatError(std::format("{}x{} pixels; frame {} of {} is {}x{} (list \"{}:{}\" in mod.json's \"resizable\" to "
                                                      "change its size)",
                                                      img.width, img.height, k, name, f.widthPixels(), f.height, name, k));
                    if (img.remapped > 0) warn(*item.file, std::format("{} pixel(s) were not one of the 16 colours (the nearest is used)", img.remapped));
                    edits[k].image = std::move(img);
                    ++used;
                } catch (const std::exception& e) {
                    error(*item.file, std::format("{} (the frame is not replaced)", e.what()));
                }
            }
            if (edits.empty()) continue;
            try {
                PakRebuild rb = rebuildPak(file(idx), edits, name);
                checkLoadable(idx, rb.file);
                file(idx) = std::move(rb.file);
                rep_.applied += used;
            } catch (const std::exception& e) {
                rep_.errors.push_back(std::format("{} (the game's own {} is used)", e.what(), name));
            }
        }
    }

    void pans() {
        for (const auto& [idx, items] : pictures_) {
            const std::string name = nameOf(idx);
            std::map<int, LogicalImage> pics;
            for (const auto& [k, item] : items) {
                try {
                    LogicalImage img = pngToLogical(readBytes(item.file->path), colours_, false);
                    if (img.remapped > 0) warn(*item.file, std::format("{} pixel(s) were not one of the 16 colours (the nearest is used)", img.remapped));
                    pics[k] = std::move(img);
                } catch (const std::exception& e) {
                    error(*item.file, std::format("{} (the picture is not replaced)", e.what()));
                }
            }
            if (pics.empty()) continue;
            try {
                PanRebuild rb = rebuildPan(file(idx), pics, name);
                checkLoadable(idx, rb.file);
                file(idx) = std::move(rb.file);
                rep_.applied += pics.size();
            } catch (const std::exception& e) {
                rep_.errors.push_back(std::format("{} (the game's own {} is used)", e.what(), name));
            }
        }
        checkChute();
    }

    // The fall down the castle chute shows MOUNT3's blocks 0x2A and 0x43 from the buffers CLOSE.PAN
    // leaves untouched (code_world.md §6.10): a CLOSE.PAN that unpacks over them garbles that fall.
    void checkChute() {
        if (pictures_.count(FileIndex::ClosePan) == 0 && pictures_.count(FileIndex::Mount3Pan) == 0 && raw_.count(FileIndex::ClosePan) == 0 &&
            raw_.count(FileIndex::Mount3Pan) == 0)
            return;
        try {
            const PanFile close = PanFile::load(file(FileIndex::ClosePan), "CLOSE.PAN");
            const PanFile mount3 = PanFile::load(file(FileIndex::Mount3Pan), "MOUNT3.PAN");
            for (const int block : {0x2A, 0x43}) {
                const size_t mapLo = static_cast<size_t>(block) * PanFile::kBlockWords * 2, mapHi = mapLo + PanFile::kBlockWords * 2;
                const bool mapHit = mapLo < 2 * close.mapWordCount() || mapHi > PanFile::kMapBufferSize - close.mapPackedSize();
                bool tileHit = false;
                for (size_t w = 0; w < PanFile::kBlockWords; ++w) {
                    const size_t t = static_cast<size_t>(mount3.mapWord(block * PanFile::kBlockWords + w)) * 16;
                    if (t < close.tileByteCount() || t + PanFile::kTileBytes > PanFile::kTileBufferSize - close.tilesPackedSize()) tileHit = true;
                }
                if (mapHit || tileHit)
                    rep_.warnings.push_back(std::format("CLOSE.PAN / MOUNT3.PAN: the fall down the castle chute shows MOUNT3 block {} from "
                                                        "the buffers CLOSE.PAN leaves alone; the new files overlap its {} (the fall will "
                                                        "look wrong)",
                                                        block, mapHit ? "map" : "tiles"));
            }
        } catch (const std::exception&) {
        }
    }

    void sounds() {
        for (const auto& [idx, item] : sounds_) {
            try {
                auto bytes = soundFileFromText(readText(item.file->path), idx, nameOf(idx));
                checkLoadable(idx, bytes);
                file(idx) = std::move(bytes);
                ++rep_.applied;
            } catch (const std::exception& e) {
                error(*item.file, std::format("{} (the game's own {} is used)", e.what(), nameOf(idx)));
            }
        }
    }

    void texts() {
        std::optional<std::vector<uint8_t>> dict;
        const auto dictIt = texts_.find(FileIndex::PrgDict);
        if (dictIt != texts_.end()) {
            try {
                auto d = dictionaryFromText(readText(dictIt->second.file->path));
                checkDosDataFile(FileIndex::PrgDict, nameOf(FileIndex::PrgDict), d);
                dict = std::move(d);
            } catch (const std::exception& e) {
                error(*dictIt->second.file, std::format("{} (the game's own word table is used)", e.what()));
            }
        }
        const auto puzIt = texts_.find(FileIndex::AllPuzs);
        if (puzIt != texts_.end() || dict) {
            const DosModFile& blame = puzIt != texts_.end() ? *puzIt->second.file : *dictIt->second.file;
            try {
                std::vector<std::vector<uint8_t>> expanded;
                if (puzIt != texts_.end()) {
                    expanded = puzzlesFromText(readText(puzIt->second.file->path));
                } else {  // only the word table changed: the same questions, compressed with the new table
                    const Dictionary old = Dictionary::parse(file(FileIndex::PrgDict));
                    const PuzzleFile puz = PuzzleFile::parse(file(FileIndex::AllPuzs));
                    for (int i = 0; i < PuzzleFile::kCount; ++i) {
                        const std::string e = old.expand(puz.raw(i));
                        expanded.emplace_back(e.begin(), e.end());
                    }
                }
                const Dictionary d = Dictionary::parse(dict ? *dict : file(FileIndex::PrgDict));
                auto built = buildAllPuzs(expanded, d);
                checkDosDataFile(FileIndex::AllPuzs, nameOf(FileIndex::AllPuzs), built);
                checkPuzzleWindows(exe_, built);
                const QuizTiers tiers = QuizTiers::read(exe_, PuzzleFile::parse(built));
                std::vector<std::pair<int, int>> ranges;
                for (const int t : {0, 1, 2, 3, 5}) ranges.push_back(tiers.range(t));
                DosTextProblems pr;
                checkPuzzles(expanded, ranges, font_, pr);
                for (const auto& w : pr.warnings) warn(blame, w);
                if (!pr.errors.empty()) {
                    for (const auto& e : pr.errors) error(blame, e);
                    throw FormatError("the questions break the quiz's limits");
                }
                file(FileIndex::AllPuzs) = std::move(built);
                if (dict) {
                    file(FileIndex::PrgDict) = std::move(*dict);
                    ++rep_.applied;
                }
                if (puzIt != texts_.end()) ++rep_.applied;
            } catch (const std::exception& e) {
                error(blame, std::format("{} (the game's own questions{} are used)", e.what(), dict ? " and word table" : ""));
            }
        }
        if (const auto it = texts_.find(FileIndex::HelpDat); it != texts_.end()) {
            try {
                const auto counts = std::span<const uint8_t>(file(FileIndex::HelpDat)).first(4);
                auto help = helpFromText(readText(it->second.file->path), counts);
                DosTextProblems pr;
                checkHelp(help, font_, pr);
                for (const auto& w : pr.warnings) warn(*it->second.file, w);
                if (!pr.errors.empty()) {
                    for (const auto& e : pr.errors) error(*it->second.file, e);
                    throw FormatError("the pages break the help pager's limits");
                }
                checkDosDataFile(FileIndex::HelpDat, nameOf(FileIndex::HelpDat), help);
                file(FileIndex::HelpDat) = std::move(help);
                ++rep_.applied;
            } catch (const std::exception& e) {
                error(*it->second.file, std::format("{} (the game's own help is used)", e.what()));
            }
        }
    }

    void strings() {
        if (strings_.empty()) return;
        std::optional<DosStringCatalogue> catalogue;
        try {
            catalogue = DosStringCatalogue::read(exe_);
        } catch (const std::exception& e) {
            rep_.errors.push_back(std::format("SST.EXE: {} (no string is replaced)", e.what()));
            return;
        }
        const DosStringCatalogue& cat = *catalogue;
        DosStringEdits edits;
        size_t used = 0;
        for (const Item& item : strings_) {
            try {
                parseStringsJson(readText(item.file->path), cat, edits);
                ++used;
            } catch (const std::exception& e) {
                error(*item.file, std::format("{} (the file is ignored)", e.what()));
            }
        }
        if (edits.empty()) return;
        DosStringLayout layout;
        layout.font = &font_;
        try {
            const PakFile sprites = PakFile::load(file(FileIndex::SpritesPak), "SPRITES.PAK");
            if (sprites.count() > 71) {
                const PakFrame& box = sprites.frame(58);  // the HUD's clue box (an absolute position)
                layout.clueBoxRight = box.xOffset + box.widthBytes - 1;
                const PakFrame& cloud = sprites.frame(71);  // Flutter's cloud
                layout.cloudLeft = cloud.xOffset;
                layout.cloudTop = cloud.yOffset;
                layout.cloudRight = cloud.xOffset + cloud.widthBytes;
                layout.cloudBottom = cloud.yOffset + cloud.height;
            }
        } catch (const std::exception&) {
        }
        const DosStringsResult r = applyStringEdits(exe_, cat, edits, layout);
        for (const auto& e : r.errors) rep_.errors.push_back("EXE/strings.json: " + e);
        for (const auto& w : r.warnings) rep_.warnings.push_back("EXE/strings.json: " + w);
        rep_.applied += used;
    }

    const std::vector<DosModFolder>& folders_;
    DosExe& exe_;
    DosFileSet& files_;
    DosModsReport rep_;
    FileTable table_;
    DosFont font_;
    DosColours colours_{};
    std::map<FileIndex, Item> raw_, sounds_, texts_;
    std::map<FileIndex, std::map<int, Item>> frames_, pictures_;
    std::map<FileIndex, std::vector<Item>> headers_;
    std::vector<Item> palettes_, strings_;
};

}  // namespace

DosModsReport applyDosModFolders(const std::vector<DosModFolder>& folders, DosExe& exe, DosFileSet& files) {
    return Applier(folders, exe, files).run();
}

DosModSet::DosModSet(std::vector<DosModFolder> folders) : folders_(std::move(folders)) {}

void DosModSet::apply(DosExe& exe, DosFileSet& files) const {
    uint64_t key = fnv(1469598103934665603ull, exe.image());
    for (const auto& f : files) key = fnv(key, f);
    std::lock_guard lock(mutex_);
    if (cached_ && cacheKey_ == key) {
        exe = *cachedExe_;
        files = cachedFiles_;
        return;
    }
    report_ = applyDosModFolders(folders_, exe, files);
    cachedExe_ = std::make_unique<DosExe>(exe);
    cachedFiles_ = files;
    cacheKey_ = key;
    cached_ = true;
    std::string names;
    for (const auto& d : report_.folders) names += (names.empty() ? "" : ", ") + pathToUtf8(d);
    std::string changed;
    for (const auto& c : report_.changedFiles) changed += (changed.empty() ? "" : ", ") + c;
    log::info("DOS mods: {} folder(s) [{}]: {} of {} mod file(s) applied; changed: {}{}", report_.folders.size(), names, report_.applied,
              report_.modFiles, changed.empty() ? "no data file" : changed, report_.exeChanged ? ", SST.EXE strings/colours" : "");
    for (const auto& w : report_.warnings) log::warn("DOS mods: {}", w);
    for (const auto& e : report_.errors) log::error("DOS mods: {}", e);
}

DosModsReport DosModSet::lastReport() const {
    std::lock_guard lock(mutex_);
    return report_;
}

DosModsReport installDosMods(const std::vector<std::filesystem::path>& dirs) {
    DosModsReport rep;
    std::vector<DosModFolder> folders;
    for (const auto& d : dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(d, ec)) {
            log::debug("DOS mods: {} does not exist (skipped)", pathToUtf8(d));
            continue;
        }
        DosModFolder f = scanDosModFolder(d);
        rep.folders.push_back(d);
        rep.modFiles += f.files.size();
        rep.warnings.insert(rep.warnings.end(), f.warnings.begin(), f.warnings.end());
        rep.errors.insert(rep.errors.end(), f.errors.begin(), f.errors.end());
        folders.push_back(std::move(f));
    }
    if (folders.empty()) {
        setDosOverrides(nullptr);
        return rep;
    }
    setDosOverrides(std::make_shared<DosModSet>(std::move(folders)));
    return rep;
}

void uninstallDosMods() { setDosOverrides(nullptr); }

}  // namespace tmport::dos
