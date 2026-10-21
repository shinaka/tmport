#include "dos/gfx/dos_gfx_cli.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <format>
#include <string_view>
#include <vector>

#include "dos/gfx/dos_screens.h"
#include "dos/gfx/scenery.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/util/png_writer.h"

namespace tmport::dos {

namespace {

enum class Cmd { None, Pan, Pak, Screen };

struct Args {
    Cmd cmd = Cmd::None;
    std::string target, outPath, dataPath, usersPath, logicalPath;
    Look look = Look::Ega;
    int scale = 1, slot = 1;
    bool blocks = false, opaque = false;
    // --dos-screen overrides
    std::optional<std::pair<int, int>> camera, sparkle, pointer;
    bool noSparkle = false, noPointer = false, noActors = false;
    std::optional<int> coins, nets, found, key, prompt, tier, level, foundMask, puzzle, order, mask;
    std::optional<std::array<int, 3>> clue;
    std::vector<ActorSpec> actors;
    std::vector<std::pair<int, uint16_t>> cells;
    bool noClue = false;
    std::optional<int> menu, item, box, button;
    std::string backgroundPath, typedName;
    bool cursorOff = false;
};

bool parseInt(std::string_view s, int& v) {
    int base = 10;
    bool neg = false;
    if (!s.empty() && s[0] == '-') {
        neg = true;
        s.remove_prefix(1);
    }
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
        base = 16;
    }
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
    if (s.empty() || ec != std::errc() || p != s.data() + s.size()) return false;
    if (neg) v = -v;
    return true;
}

std::optional<Args> parse(int argc, char** argv, std::string& error) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string_view s = argv[i];
        if (s == "--dos-pan") a.cmd = Cmd::Pan;
        else if (s == "--dos-pak") a.cmd = Cmd::Pak;
        else if (s == "--dos-screen") a.cmd = Cmd::Screen;
    }
    if (a.cmd == Cmd::None) return std::nullopt;
    std::vector<std::string> positional;
    auto need = [&](int& i, int n) -> bool {
        if (i + n >= argc) {
            error = std::format("{} needs {} value(s)", argv[i], n);
            return false;
        }
        return true;
    };
    auto ints = [&](int& i, int n, int* out) -> bool {
        if (!need(i, n)) return false;
        for (int k = 0; k < n; ++k)
            if (!parseInt(argv[i + 1 + k], out[k])) {
                error = std::format("{}: '{}' is not a number", argv[i], argv[i + 1 + k]);
                return false;
            }
        i += n;
        return true;
    };
    auto one = [&](int& i, std::optional<int>& v) -> bool {
        int x = 0;
        if (!ints(i, 1, &x)) return false;
        v = x;
        return true;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view s = argv[i];
        int v[5] = {};
        if (s == "--dos-pan" || s == "--dos-pak" || s == "--dos-screen") continue;
        if (s == "--data" || s == "--users" || s == "--logical" || s == "--look" || s == "--background" || s == "--name") {
            if (!need(i, 1)) return std::nullopt;
            const std::string val = argv[++i];
            if (s == "--data") a.dataPath = val;
            else if (s == "--background") a.backgroundPath = val;
            else if (s == "--name") a.typedName = val;
            else if (s == "--users") a.usersPath = val;
            else if (s == "--logical") a.logicalPath = val;
            else if (auto l = parseLook(val)) a.look = *l;
            else {
                error = "--look: use ega, tandy, mcga or cga";
                return std::nullopt;
            }
        } else if (s == "--scale") {
            if (!ints(i, 1, v)) return std::nullopt;
            a.scale = std::clamp(v[0], 1, 8);
        } else if (s == "--slot") {
            if (!ints(i, 1, v)) return std::nullopt;
            a.slot = v[0];
        } else if (s == "--blocks") a.blocks = true;
        else if (s == "--opaque") a.opaque = true;
        else if (s == "--no-sparkle") a.noSparkle = true;
        else if (s == "--no-pointer") a.noPointer = true;
        else if (s == "--no-actors") a.noActors = true;
        else if (s == "--camera" || s == "--sparkle" || s == "--pointer") {
            if (!ints(i, 2, v)) return std::nullopt;
            (s == "--camera" ? a.camera : s == "--sparkle" ? a.sparkle : a.pointer) = std::pair{v[0], v[1]};
        } else if (s == "--clue") {
            if (!ints(i, 3, v)) return std::nullopt;
            a.clue = std::array<int, 3>{v[0], v[1], v[2]};
        } else if (s == "--cell") {
            if (!ints(i, 2, v)) return std::nullopt;
            a.cells.emplace_back(v[0], static_cast<uint16_t>(v[1]));
        } else if (s == "--cursor-off") {
            a.cursorOff = true;
        } else if (s == "--menu") { if (!one(i, a.menu)) return std::nullopt; }
        else if (s == "--item") { if (!one(i, a.item)) return std::nullopt; }
        else if (s == "--box") { if (!one(i, a.box)) return std::nullopt; }
        else if (s == "--button") { if (!one(i, a.button)) return std::nullopt; }
        else if (s == "--no-clue") {
            a.noClue = true;
        } else if (s == "--actor") {
            if (!ints(i, 5, v)) return std::nullopt;
            a.actors.push_back({v[0], static_cast<uint16_t>(v[1]), v[2], v[3], v[4]});
        } else if (s == "--coins") { if (!one(i, a.coins)) return std::nullopt; }
        else if (s == "--nets") { if (!one(i, a.nets)) return std::nullopt; }
        else if (s == "--found") { if (!one(i, a.found)) return std::nullopt; }
        else if (s == "--key") { if (!one(i, a.key)) return std::nullopt; }
        else if (s == "--prompt") { if (!one(i, a.prompt)) return std::nullopt; }
        else if (s == "--tier") { if (!one(i, a.tier)) return std::nullopt; }
        else if (s == "--level") { if (!one(i, a.level)) return std::nullopt; }
        else if (s == "--found-mask") { if (!one(i, a.foundMask)) return std::nullopt; }
        else if (s == "--puzzle") { if (!one(i, a.puzzle)) return std::nullopt; }
        else if (s == "--order") { if (!one(i, a.order)) return std::nullopt; }
        else if (s == "--mask") { if (!one(i, a.mask)) return std::nullopt; }
        else if (s.starts_with("--")) {
            error = std::format("unknown option '{}'", s);
            return std::nullopt;
        } else positional.emplace_back(s);
    }
    if (positional.size() != 2) {
        error = "give the input and the output PNG";
        return std::nullopt;
    }
    a.target = positional[0];
    a.outPath = positional[1];
    return a;
}

// The DOS folder: --data, TM_DATA_DOS, or the folder of an input file.
std::optional<std::filesystem::path> dataDir(const Args& a) {
    if (!a.dataPath.empty()) return mischief::pathFromUtf8(a.dataPath);
    if (auto env = mischief::getEnv("TM_DATA_DOS")) return mischief::pathFromUtf8(*env);
    const auto p = mischief::pathFromUtf8(a.target);
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) return p.parent_path().empty() ? std::filesystem::path(".") : p.parent_path();
    return std::nullopt;
}

// A file given as a path, or by name / stem in the DOS folder; returns its upper-case stem.
std::string stemOf(const Args& a, std::string_view ext) {
    std::string stem = mischief::toUpperAscii(mischief::pathToUtf8(mischief::pathFromUtf8(a.target).filename()));
    if (stem.ends_with(ext)) stem.resize(stem.size() - ext.size());
    return stem;
}

// Logical-colour image (one byte per pixel) -> PNG: indexed with the look's 16 colours (plus
// `extra` colours for indices 16..), or RGBA for CGA (its colour depends on the pixel position).
bool writeLogical(const std::filesystem::path& out, int w, int h, const std::vector<uint8_t>& px, const LookPalette& pal,
                  int scale, std::span<const mischief::Rgb8> extra = {}) {
    const int sw = w * scale, sh = h * scale;
    auto src = [&](int x, int y) { return px[static_cast<size_t>(y / scale) * w + x / scale]; };
    if (pal.look == Look::Cga) {
        std::vector<uint8_t> rgba(static_cast<size_t>(sw) * sh * 4);
        for (int y = 0; y < sh; ++y)
            for (int x = 0; x < sw; ++x) {
                const uint8_t c = src(x, y);
                const mischief::Rgb8 rgb = c < 16 ? pal.at(c, x / scale, y / scale) : extra[c - 16];
                uint8_t* o = &rgba[(static_cast<size_t>(y) * sw + x) * 4];
                o[0] = rgb.r, o[1] = rgb.g, o[2] = rgb.b, o[3] = 0xFF;
            }
        return mischief::png::writeRgba(out, sw, sh, rgba);
    }
    std::vector<mischief::Rgb8> colours(pal.colours.begin(), pal.colours.end());
    colours.insert(colours.end(), extra.begin(), extra.end());
    std::vector<uint8_t> idx(static_cast<size_t>(sw) * sh);
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) idx[static_cast<size_t>(y) * sw + x] = src(x, y);
    return mischief::png::writeIndexed(out, sw, sh, idx, colours);
}

int runPan(const Args& a, const std::filesystem::path& dir) {
    const DosGfxFiles files = DosGfxFiles::open(dir);
    const LookPalette pal = lookPalette(files.exe(), a.look);
    const std::string stem = stemOf(a, ".PAN");
    const PanFile& pan = files.pan(stem);
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (pan.widthTiles() != 16 || a.blocks) {  // the picture(s): OPENING, or a sheet of blocks, 9 per row
        const int pw = pan.widthTiles() * 8, ph = pan.heightTiles() * 8, cols = pan.pictureCount() > 1 ? 9 : 1;
        const int rows = (pan.pictureCount() + cols - 1) / cols, gap = pan.pictureCount() > 1 ? 4 : 0;
        w = cols * (pw + gap) - gap;
        h = rows * (ph + gap) - gap;
        px.assign(static_cast<size_t>(w) * h, 16);
        for (int k = 0; k < pan.pictureCount(); ++k) {
            const auto pic = pan.picturePixels(k);
            const int ox = (k % cols) * (pw + gap), oy = (k / cols) * (ph + gap);
            for (int y = 0; y < ph; ++y) std::copy_n(&pic[static_cast<size_t>(y) * pw], pw, &px[static_cast<size_t>(oy + y) * w + ox]);
        }
    } else {  // the whole 16x16-block world on the block grid DS:0CAA
        auto pictures = initialCellPictures(files.exe());
        if (!a.usersPath.empty()) {
            const auto users = mischief::readWholeFile(mischief::pathFromUtf8(a.usersPath));
            const size_t at = 3 + static_cast<size_t>(a.slot) * 0x168;
            if (!users || users->size() < at + 0x168) throw mischief::FormatError(a.usersPath + ": no record " + std::to_string(a.slot));
            restoreCellPictures(files.exe(), std::span<const uint8_t>(*users).subspan(at, 0x168), pictures);
        }
        w = h = 16 * 128;
        px.assign(static_cast<size_t>(w) * h, 0);
        for (int cell = 0; cell < 256; ++cell) {
            const int block = pictures[static_cast<size_t>(cell)] & 0x7F;
            if (block >= pan.pictureCount()) continue;  // not a block of this file: left blank
            const auto pic = pan.picturePixels(block);
            const int ox = (cell % 16) * 128, oy = (cell / 16) * 128;
            for (int y = 0; y < 128; ++y) std::copy_n(&pic[static_cast<size_t>(y) * 128], 128, &px[static_cast<size_t>(oy + y) * w + ox]);
        }
    }
    const mischief::Rgb8 extra[] = {{40, 40, 52}};
    if (!writeLogical(mischief::pathFromUtf8(a.outPath), w, h, px, pal, a.scale, extra)) throw mischief::Error("cannot write " + a.outPath);
    std::printf("%s: %d picture(s) of %dx%d tiles, %d tiles -> %s (%dx%d)\n", pan.name().c_str(), pan.pictureCount(),
                pan.widthTiles(), pan.heightTiles(), pan.tileCount(), a.outPath.c_str(), w * a.scale, h * a.scale);
    return 0;
}

// Draws text with the DOS font into an 8-bit canvas (contact-sheet labels).
int drawLabel(std::vector<uint8_t>& px, int w, const DosFont& font, int x, int y, std::string_view s, uint8_t colour) {
    for (char ch : s) {
        const int code = static_cast<uint8_t>(ch) & 0x7F, bpr = font.bytesPerRow(code);
        const auto g = font.glyph(code);
        for (int r = 0; r < font.height(); ++r)
            for (int b = 0; b < font.width(code); ++b)
                if ((g[static_cast<size_t>(r * bpr + b / 8)] >> (7 - b % 8)) & 1) {
                    const size_t at = static_cast<size_t>(y + r) * w + x + b;
                    if (at < px.size()) px[at] = colour;
                }
        x += font.width(code);
    }
    return x;
}

int runPak(const Args& a, const std::filesystem::path& dir) {
    const DosGfxFiles files = DosGfxFiles::open(dir);
    const LookPalette pal = lookPalette(files.exe(), a.look);
    const PakFile& pak = files.pak(stemOf(a, ".PAK"));
    constexpr int kMaxWidth = 960, kGap = 6, kLabel = 11;
    constexpr uint8_t kBack = 16, kClear = 17, kText = 18;
    struct Place { int x, y; };
    std::vector<Place> places;
    int x = kGap, y = kGap, rowH = 0, w = 0;
    for (const PakFrame& f : pak.frames()) {
        const int cw = std::max(f.widthPixels(), 24);
        if (x + cw + kGap > kMaxWidth && x > kGap) {
            x = kGap;
            y += rowH + kLabel + kGap;
            rowH = 0;
        }
        places.push_back({x, y});
        x += cw + kGap;
        w = std::max(w, x);
        rowH = std::max(rowH, f.height);
    }
    const int h = y + rowH + kLabel + kGap;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h, kBack);
    for (const PakFrame& f : pak.frames()) {
        const Place p = places[static_cast<size_t>(f.index)];
        for (int yy = 0; yy < f.height; ++yy)
            for (int xx = 0; xx < f.widthPixels(); ++xx) {
                const uint8_t c = f.pixel(xx, yy);
                px[static_cast<size_t>(p.y + yy) * w + p.x + xx] = (c == 0 && !a.opaque) ? kClear : c;
            }
        drawLabel(px, w, files.font(), p.x, p.y + f.height + 1, std::to_string(f.index), kText);
    }
    const mischief::Rgb8 extra[] = {{40, 40, 52}, {72, 72, 90}, {230, 230, 230}};
    if (!writeLogical(mischief::pathFromUtf8(a.outPath), w, h, px, pal, a.scale, extra)) throw mischief::Error("cannot write " + a.outPath);
    std::printf("%s: %d frames -> %s (%dx%d)\n", pak.name().c_str(), pak.count(), a.outPath.c_str(), w * a.scale, h * a.scale);
    return 0;
}

int runScreen(const Args& a, const std::filesystem::path& dir) {
    const auto screen = parseDosScreen(a.target);
    if (!screen) {
        std::fprintf(stderr, "tmport: unknown DOS screen '%s' (title, signin, clubhouse, quiz, path, castle, menu, msgbox, newname)\n",
                     a.target.c_str());
        return 2;
    }
    const DosGfxFiles files = DosGfxFiles::open(dir);
    DosScreenSpec spec = defaultScreenSpec(*screen, a.look);
    if (!a.usersPath.empty()) spec.usersFile = mischief::pathFromUtf8(a.usersPath);
    spec.usersSlot = a.slot;
    if (a.camera) spec.camX = static_cast<uint16_t>(a.camera->first), spec.camY = static_cast<uint16_t>(a.camera->second);
    if (a.noSparkle) spec.sparkleSpot = -1;
    if (a.sparkle) spec.sparkleSpot = a.sparkle->first, spec.sparkleFrame = a.sparkle->second;
    if (a.noPointer) spec.pointer.reset();
    if (a.pointer) spec.pointer = a.pointer;
    if (a.noActors) spec.actors.clear();
    for (const ActorSpec& act : a.actors) spec.actors.push_back(act);
    spec.coins = a.coins.value_or(spec.coins);
    spec.nets = a.nets.value_or(spec.nets);
    spec.found = a.found.value_or(spec.found);
    spec.keyState = a.key.value_or(spec.keyState);
    spec.prompt = a.prompt.value_or(spec.prompt);
    spec.tier = a.tier.value_or(spec.tier);
    spec.level = a.level.value_or(spec.level);
    spec.foundMask = a.foundMask.value_or(spec.foundMask);
    spec.puzzle = a.puzzle.value_or(spec.puzzle);
    spec.order = a.order.value_or(spec.order);
    spec.mask = a.mask.value_or(spec.mask);
    if (a.noClue) spec.clueWords = {0x80, 0x80, 0x80};
    if (a.clue) spec.clueWords = *a.clue;
    for (const auto& c : a.cells) spec.cellPictures.push_back(c);
    spec.menu = a.menu.value_or(spec.menu);
    spec.item = a.item.value_or(spec.item);
    spec.box = a.box.value_or(spec.box);
    if (a.button) spec.button = a.button;
    spec.typedName = a.typedName;
    spec.cursorOn = !a.cursorOff;
    if (!a.backgroundPath.empty()) {
        const auto bg = mischief::readWholeFile(mischief::pathFromUtf8(a.backgroundPath));
        if (!bg || bg->size() != static_cast<size_t>(BackBuffer::kWidth) * BackBuffer::kHeight)
            throw mischief::Error(a.backgroundPath + ": expected 64000 bytes of logical colours (320x200)");
        spec.background = *bg;
    }
    const BackBuffer buf = composeScreen(files, spec);
    const auto logical = buf.logicalPixels();
    if (!a.logicalPath.empty() && !mischief::writeWholeFile(mischief::pathFromUtf8(a.logicalPath), logical)) throw mischief::Error("cannot write " + a.logicalPath);
    if (!writeLogical(mischief::pathFromUtf8(a.outPath), BackBuffer::kWidth, BackBuffer::kHeight, logical, lookPalette(files.exe(), a.look), a.scale))
        throw mischief::Error("cannot write " + a.outPath);
    std::printf("%s (%s) -> %s\n", std::string(dosScreenName(*screen)).c_str(), std::string(lookName(a.look)).c_str(), a.outPath.c_str());
    return 0;
}

}  // namespace

std::optional<int> runDosGfxCommand(int argc, char** argv) {
    std::string error;
    const auto args = parse(argc, argv, error);
    if (!args) {
        if (error.empty()) return std::nullopt;
        std::fprintf(stderr, "tmport: %s\n%s", error.c_str(), dosGfxUsage().c_str());
        return 2;
    }
    const auto dir = dataDir(*args);
    if (!dir) {
        std::fprintf(stderr, "tmport: the DOS game folder was not found. Pass --data <folder with SST.EXE> or set TM_DATA_DOS.\n");
        return 2;
    }
    try {
        switch (args->cmd) {
            case Cmd::Pan: return runPan(*args, *dir);
            case Cmd::Pak: return runPak(*args, *dir);
            case Cmd::Screen: return runScreen(*args, *dir);
            case Cmd::None: break;
        }
    } catch (const mischief::MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::out_of_range& e) {
        std::fprintf(stderr, "tmport: %s (not a DOS PAN/PAK name?)\n", e.what());
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
    return 0;
}

std::string dosGfxUsage() {
    return "DOS edition graphics (1990 SST.EXE; headless, PNG output):\n"
           "  --dos-pan <file|name> <out.png>   a PAN: OPENING's picture, or a scenery PAN as the 16x16-block world\n"
           "                                    [--blocks: sheet of its blocks] [--users <USERS.DAT> [--slot <n>]]\n"
           "  --dos-pak <file|name> <out.png>   contact sheet of a PAK's frames [--opaque]\n"
           "  --dos-screen <name> <out.png>     title, signin, clubhouse, quiz, path, castle (inputs of the reference\n"
           "                                    captures; override with --users, --slot, --camera <x> <y>, --coins,\n"
           "                                    --nets, --found, --key, --prompt, --tier, --level, --found-mask, --clue\n"
           "                                    <a> <b> <c>, --puzzle, --order, --mask, --sparkle <spot> <frame>,\n"
           "                                    --no-sparkle, --pointer <x> <y>, --no-pointer, --actor <slot> <frame>\n"
           "                                    <x> <y> <z>, --no-actors, --cell <cell> <picture>, --no-clue)\n"
           "                                    [--logical <out.bin>]\n"
           "                                    menu (--menu <0..2> --item <n>), msgbox (--box <n> [--button <0|1>]):\n"
           "                                    over --background <320x200 logical .bin>; newname (--name <text>\n"
           "                                    [--cursor-off] [--users <USERS.DAT>])\n"
           "  Common: --data <DOS folder> (default TM_DATA_DOS), --look ega|tandy|mcga|cga, --scale <n>\n";
}

}  // namespace tmport::dos
