#include "gg/gfx/gfx_cli.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <format>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "gg/data/cli_args.h"
#include "gg/data/gg_files.h"
#include "gg/data/resources.h"
#include "gg/gfx/lpx.h"
#include "gg/gfx/movie.h"
#include "gg/gfx/transitions.h"
#include "gg/gfx/video.h"
#include "gg/text/font.h"
#include "mods/png_codec.h"
#include "rtl/borland_rand.h"
#include "util/file_util.h"
#include "util/png_writer.h"

namespace tmport::gg {

namespace {

using cli::hasFlag;
using cli::insideFolder;
using cli::openFolder;
using cli::optionValue;
using cli::optionValues;

constexpr std::string_view kCommands[] = {"--gg-pic",      "--gg-pal",       "--gg-font",      "--gg-movie",
                                          "--gg-title",    "--gg-fade",      "--gg-lpx-list",  "--gg-font-list",
                                          "--gg-movie-list", "--gg-wipe"};

void out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); }
void err(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stderr); }

struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The n operands after the command.
std::vector<std::string> operands(const std::vector<std::string>& args, std::string_view cmd, size_t n) {
    const auto it = std::find(args.begin(), args.end(), cmd);
    if (static_cast<size_t>(args.end() - it) < n + 1) throw UsageError(std::format("{} needs {} arguments", cmd, n));
    return {it + 1, it + 1 + static_cast<ptrdiff_t>(n)};
}

int intOption(const std::vector<std::string>& args, std::string_view name, int def) {
    const auto v = optionValue(args, name);
    return v ? std::stoi(*v) : def;
}

uint16_t parseId(std::string_view s) {
    if (s.starts_with("0x") || s.starts_with("0X")) s.remove_prefix(2);
    if (s.empty() || s.size() > 4) throw UsageError(std::format("bad resource id '{}'", s));
    unsigned v = 0;
    for (const char c : s) {
        const int d = (c >= '0' && c <= '9')   ? c - '0'
                      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                      : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                               : -1;
        if (d < 0) throw UsageError(std::format("bad resource id '{}'", s));
        v = v * 16 + static_cast<unsigned>(d);
    }
    return static_cast<uint16_t>(v);
}

void checkOutput(const GgFiles& files, const std::filesystem::path& p) {
    if (insideFolder(p, files.folder())) throw UsageError("refusing to write into the game folder: " + pathToUtf8(p));
}

// --- images ----------------------------------------------------------------------------------------

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // R, G, B, A per pixel
    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        for (size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i] = r;
            rgba[i + 1] = g;
            rgba[i + 2] = b;
            rgba[i + 3] = a;
        }
    }
    static Image make(int w, int h) { return {w, h, std::vector<uint8_t>(static_cast<size_t>(w) * h * 4, 0)}; }
    uint8_t* at(int x, int y) { return rgba.data() + (static_cast<size_t>(y) * w + x) * 4; }
};

void writePng(const std::filesystem::path& path, const Image& img, int scale = 1) {
    if (scale <= 1) {
        if (!writeWholeFile(path, mods::encodePngRgba(img.w, img.h, img.rgba)))
            throw std::runtime_error("cannot write " + pathToUtf8(path));
        return;
    }
    Image big = Image::make(img.w * scale, img.h * scale);
    for (int y = 0; y < big.h; ++y)
        for (int x = 0; x < big.w; ++x)
            std::copy_n(img.rgba.data() + (static_cast<size_t>(y / scale) * img.w + x / scale) * 4, 4, big.at(x, y));
    writePng(path, big);
}

// Indices through a 6-bit palette (the engine's DAC expansion); alpha 0 where `mask` is 0.
Image indexedImage(int w, int h, std::span<const uint8_t> pixels, const Rgb6& pal, std::span<const uint8_t> mask = {}) {
    Image img = Image::make(w, h);
    const auto lut = rgba32(pal);
    for (size_t i = 0; i < pixels.size() && i < static_cast<size_t>(w) * h; ++i) {
        uint32_t c = lut[pixels[i]];
        if (!mask.empty() && !mask[i]) c &= 0x00FFFFFFu;
        for (int k = 0; k < 4; ++k) img.rgba[i * 4 + static_cast<size_t>(k)] = static_cast<uint8_t>(c >> (8 * k));
    }
    return img;
}

// tools/gg/lpx.py sheet(): pictures side by side, wrapped into rows of at most 1280 pixels.
Image sheet(const std::vector<Image>& images) {
    constexpr int kGap = 4, kMaxWidth = 1280;
    if (images.empty()) {
        Image s = Image::make(1, 1);
        s.fill(48, 48, 48);
        return s;
    }
    std::vector<std::vector<const Image*>> rows(1);
    int x = 0, wMax = 0;
    for (const Image& im : images) {
        if (!rows.back().empty() && x + im.w > kMaxWidth) {
            rows.emplace_back();
            x = 0;
        }
        rows.back().push_back(&im);
        x += im.w + kGap;
        wMax = std::max(wMax, x);
    }
    int height = kGap;
    for (const auto& r : rows) {
        int hMax = 0;
        for (const Image* im : r) hMax = std::max(hMax, im->h);
        height += hMax + kGap;
    }
    Image s = Image::make(wMax + kGap, height);
    s.fill(48, 48, 48);
    int y = kGap;
    for (const auto& r : rows) {
        int px = kGap, hMax = 0;
        for (const Image* im : r) {
            for (int yy = 0; yy < im->h; ++yy)
                for (int xx = 0; xx < im->w; ++xx) {
                    const uint8_t* p = im->rgba.data() + (static_cast<size_t>(yy) * im->w + xx) * 4;
                    if (p[3]) std::copy_n(p, 4, s.at(px + xx, y + yy));
                }
            px += im->w + kGap;
            hMax = std::max(hMax, im->h);
        }
        y += hMax + kGap;
    }
    return s;
}

// --- picture and palette pairing (fmt_graphics.md §4; tools/gg/lpx.py palettes_for) ------------------

// The code's picture/palette pairs (1473:0399 Screen_LoadPicture and Pal_LoadRes call sites).
const std::map<uint16_t, std::vector<uint16_t>>& codePairs() {
    static const std::map<uint16_t, std::vector<uint16_t>> pairs = {
        {0x1002, {0x1000}},           // 2c11:043f title
        {0x1009, {0x100A}},           // 2c11:0630 story pages
        {0x1029, {0x102A}},           // 2c11:084c, 2caa:016e sign-in
        {0x1076, {0x1077}},           // 2fcb:0000 race results
        {0x1093, {0x1094}},           // 33cd:1ba1 energy forms
        {0x1133, {0x1134}},           // 3797:040d launch
        {0x119F, {0x11A0}},           // 35bb:1c03 magnets
        {0x2021, {0x2022}},           // 3b2f:071c gears
        {0x204A, {0x204B}},           // 3938:0e76 simple machines
        {0x2084, {0x2085}},           // 3a4a:0be5 machine pieces
        {0x20B2, {0x20B3}},           // 3083:250e electric
        {0x2119, {0x211A}},           // 32fe:0aa0 balance
        {0x00DF, {0x00E0, 0x00F6}},   // race backgrounds DS:399a + id + 1, then 0xf6 (3dd6:0762)
        {0x00E1, {0x00E2, 0x00F6}},
        {0x00E3, {0x00E4, 0x00F6}},
    };
    return pairs;
}

constexpr uint16_t kLevelFirst = 0x02F5;  // 15 level descriptions (153f:0002): +8 palette, +0xa sprite bundle
constexpr int kLevels = 15;
constexpr uint16_t kDamFramesFirst = 0x1157, kDamFramesEnd = 0x1163;  // the launch puzzle's DAM frames
constexpr uint16_t kDamPalette = 0x1134;

bool isPaletteResource(const Archive& arc, uint16_t id) {
    if (!arc.exists(id)) return false;
    const auto r = arc.load(id);
    return classify(r.type, r.bytes, id) == "palette";
}

std::map<uint16_t, uint16_t> levelPalettes(const Archive& arc) {
    std::map<uint16_t, uint16_t> m;
    for (int k = 0; k < kLevels; ++k) {
        const auto id = static_cast<uint16_t>(kLevelFirst + k);
        if (!arc.exists(id)) break;
        const auto p = arc.read(id);
        if (p.size() < 12) continue;
        const auto pal = static_cast<uint16_t>(p[8] | (p[9] << 8));
        const auto bun = static_cast<uint16_t>(p[10] | (p[11] << 8));
        if (isPaletteResource(arc, pal)) m.emplace(bun, pal);
    }
    return m;
}

// The shadow palette for a picture of resource `id` (a bundle when isBundle): the base, then
// `extra` (else the context palette), then the bundle's palette members.
Rgb6 paletteFor(const GgFiles& files, uint16_t id, bool isBundle, const std::vector<uint16_t>& extra) {
    const Archive& arc = files.archive();
    Palette pal = Palette::fromExe(files.exe());
    auto store = [&](std::span<const uint8_t> payload) { pal.store(parsePaletteRange(payload)); };
    for (const uint16_t q : extra) store(arc.read(q));
    if (isBundle) {
        if (extra.empty()) {
            const auto levels = levelPalettes(arc);
            std::optional<uint16_t> ctx;
            if (const auto it = levels.find(id); it != levels.end()) ctx = it->second;
            else if (id >= kDamFramesFirst && id < kDamFramesEnd) ctx = kDamPalette;
            if (ctx) store(arc.read(*ctx));
        }
        const Bundle b = Bundle::load(arc, id);
        for (const auto& m : b.members())
            if (classify(m.type, m.payload, id, m.index) == "palette") store(m.payload);
    } else if (extra.empty()) {
        if (const auto it = codePairs().find(id); it != codePairs().end()) {
            for (const uint16_t q : it->second)
                if (arc.exists(q)) store(arc.read(q));
        } else if (isPaletteResource(arc, static_cast<uint16_t>(id + 1))) {
            store(arc.read(static_cast<uint16_t>(id + 1)));
        }
    }
    return pal.shadow();
}

Image pictureImage(std::span<const uint8_t> payload, const Rgb6& pal, bool keyed) {
    const DecodedLpx pic = decodeLpx(payload);
    const auto mask = keyed ? keyedMask(pic) : pic.opaque;
    return indexedImage(pic.width, pic.height, pic.pixels, pal, mask);
}

// --- commands --------------------------------------------------------------------------------------------

int cmdPic(const GgFiles& files, const std::vector<std::string>& args) {
    const auto ops = operands(args, "--gg-pic", 2);
    const auto outPath = pathFromUtf8(ops[1]);
    checkOutput(files, outPath);
    std::vector<uint16_t> extra;
    for (const auto& v : optionValues(args, "--pal")) extra.push_back(parseId(v));
    const bool keyed = !hasFlag(args, "--opaque");
    const Archive& arc = files.archive();
    const std::string& spec = ops[0];
    const auto slash = spec.find('/');
    const uint16_t id = parseId(std::string_view(spec).substr(0, slash));
    const auto res = arc.load(id);
    const bool isBundle = res.resType() == ResType::Bundle;
    const Rgb6 pal = paletteFor(files, id, isBundle, extra);
    Image img;
    if (slash != std::string::npos) {
        const Bundle b = Bundle::load(arc, id);
        img = pictureImage(b.member(static_cast<size_t>(std::stoul(spec.substr(slash + 1)))).payload, pal, keyed);
    } else if (isBundle) {
        std::vector<Image> pics;
        for (const auto& m : parseBundle(res.bytes))
            if (isLpx(m.payload)) pics.push_back(pictureImage(m.payload, pal, keyed));
        img = sheet(pics);
    } else {
        img = pictureImage(res.bytes, pal, keyed);
    }
    writePng(outPath, img, intOption(args, "--scale", 1));
    out(std::format("{} -> {} ({}x{})\n", spec, pathToUtf8(outPath), img.w, img.h));
    return 0;
}

int cmdPal(const GgFiles& files, const std::vector<std::string>& args) {
    const auto ops = operands(args, "--gg-pal", 2);
    const auto outPath = pathFromUtf8(ops[1]);
    checkOutput(files, outPath);
    Palette pal = Palette::fromExe(files.exe());
    int first = 0, count = 256;
    if (ops[0] != "base") {
        const auto bytes = files.archive().read(parseId(ops[0]));
        const PaletteRange r = parsePaletteRange(bytes);
        pal.store(r);
        first = r.firstColor();
        count = r.colorCount();
    }
    // tools/gg/lpx.py swatch(): 16x16 cells of 10 pixels, colours outside the range dimmed.
    constexpr int kCell = 10;
    Image img = Image::make(16 * kCell, 16 * kCell);
    img.fill(0, 0, 0);
    const auto lut = rgba32(pal.shadow());
    for (int i = 0; i < 256; ++i) {
        uint8_t c[3] = {static_cast<uint8_t>(lut[static_cast<size_t>(i)]), static_cast<uint8_t>(lut[static_cast<size_t>(i)] >> 8),
                        static_cast<uint8_t>(lut[static_cast<size_t>(i)] >> 16)};
        if (i < first || i >= first + count)
            for (auto& v : c) v = static_cast<uint8_t>(v / 4);
        for (int y = 0; y < kCell - 1; ++y)
            for (int x = 0; x < kCell - 1; ++x) {
                uint8_t* p = img.at((i % 16) * kCell + x, (i / 16) * kCell + y);
                p[0] = c[0];
                p[1] = c[1];
                p[2] = c[2];
                p[3] = 255;
            }
    }
    writePng(outPath, img, intOption(args, "--scale", 1));
    out(std::format("{} -> {} (colours {}..{})\n", ops[0], pathToUtf8(outPath), first, first + count - 1));
    return 0;
}

int cmdFont(const GgFiles& files, const std::vector<std::string>& args) {
    const auto ops = operands(args, "--gg-font", 2);
    const auto outPath = pathFromUtf8(ops[1]);
    checkOutput(files, outPath);
    const GgFont font = GgFont::parse(files.archive().read(parseId(ops[0])));
    // Each glyph is drawn by Gfx_DrawGlyph into a scratch screen and copied into a 16-column grid.
    int cw = 8, ch = font.height();
    for (int c = 0; c < font.records(); ++c) {
        const auto g = font.glyph(static_cast<uint8_t>(c));
        if (g.size() < 4) continue;
        cw = std::max(cw, g[0] * 8 + g[1]);
        ch = std::max(ch, font.yOffset(static_cast<uint8_t>(c)) + (g[2] | (g[3] << 8)));
    }
    cw += 4;
    ch += 4;
    const int rows = (font.records() + 15) / 16;
    Image img = Image::make(16 * cw, rows * ch);
    img.fill(255, 255, 255);
    Screen scratch;
    for (int c = 0; c < font.records(); ++c) {
        const int x0 = (c % 16) * cw, y0 = (c / 16) * ch;
        for (int x = 0; x < cw - 1; ++x) {  // the cell frame
            std::fill_n(img.at(x0 + x, y0), 3, uint8_t{220});
            std::fill_n(img.at(x0 + x, y0 + ch - 2), 3, uint8_t{220});
        }
        for (int y = 0; y < ch - 1; ++y) {
            std::fill_n(img.at(x0, y0 + y), 3, uint8_t{220});
            std::fill_n(img.at(x0 + cw - 2, y0 + y), 3, uint8_t{220});
        }
        const auto g = font.glyph(static_cast<uint8_t>(c));
        if (g.empty() || !font.oneBit()) continue;
        std::fill(scratch.bytes().begin(), scratch.bytes().end(), uint8_t{0});
        scratch.drawGlyph(2, 2 + font.yOffset(static_cast<uint8_t>(c)), g, 1);
        for (int y = 0; y < ch - 2; ++y)
            for (int x = 0; x < cw - 2; ++x)
                if (scratch.pixel(x, y)) std::fill_n(img.at(x0 + x, y0 + y), 3, uint8_t{0});
    }
    writePng(outPath, img, intOption(args, "--scale", 3));
    out(std::format("{} -> {} ({} records, height {}, spacing {})\n", ops[0], pathToUtf8(outPath), font.records(),
                    font.height(), font.spacing()));
    return 0;
}

// The VRAM the movie starts from in the tools: the results picture 0x1076 in its rows (movie.py's
// start), or colour 0 with --blank.
Vram movieStartVram(const Archive& arc, bool blank) {
    Vram vram{};
    if (!blank) {
        const DecodedLpx bg = decodeLpx(arc.read(0x1076));
        if (bg.width == Screen::kWidth && bg.height <= Screen::kHeight)
            std::copy(bg.pixels.begin(), bg.pixels.end(), vram.begin());
    }
    return vram;
}

int cmdMovieList(const GgFiles& files, const std::vector<std::string>& args) {
    const Archive& arc = files.archive();
    Vram vram = movieStartVram(arc, hasFlag(args, "--blank"));
    out("frame\tid\tsize\tused\tcomplete\tend\tcrc_vram\n");
    int incomplete = 0;
    for (uint16_t id = kMovieFirst; id < kMovieEnd; ++id) {
        const auto bytes = arc.read(id);
        const auto r = movieDecodeFrame(bytes, vram);
        if (!r.complete) ++incomplete;
        out(std::format("{}\t{:04x}\t{}\t{}\t{}\t{}\t{:08x}\n", id - kMovieFirst, id, bytes.size(), r.used,
                        r.complete ? 1 : 0, r.end, png::crc32(vram)));
    }
    return incomplete ? 1 : 0;
}

int cmdMovie(const GgFiles& files, const std::vector<std::string>& args) {
    const auto ops = operands(args, "--gg-movie", 3);
    const uint16_t first = parseId(ops[0]), end = parseId(ops[1]);
    const auto dir = pathFromUtf8(ops[2]);
    checkOutput(files, dir);
    const Archive& arc = files.archive();
    Palette pal = Palette::fromExe(files.exe());
    auto pals = optionValues(args, "--pal");
    if (pals.empty()) pals.push_back("1077");
    for (const auto& q : pals) pal.store(parsePaletteRange(arc.read(parseId(q))));
    Vram vram = movieStartVram(arc, hasFlag(args, "--blank"));
    std::filesystem::create_directories(dir);
    int written = 0, incomplete = 0;
    for (uint16_t id = kMovieFirst; id < std::max(end, first) && id < kMovieEnd; ++id) {
        const auto r = movieDecodeFrame(arc.read(id), vram);
        if (!r.complete) ++incomplete;
        if (id < first) continue;
        const Image img = indexedImage(Screen::kWidth, Screen::kHeight, vram, pal.shadow());
        writePng(dir / std::format("movie_{:03d}.png", id - kMovieFirst), img, intOption(args, "--scale", 1));
        ++written;
    }
    out(std::format("{} movie frames -> {} ({} incomplete)\n", written, pathToUtf8(dir), incomplete));
    return incomplete ? 1 : 0;
}

// VRAM through a DAC as an image.
Image frameImage(const Vram& vram, const Dac& dac) {
    const auto rgba = vramToRgba(vram, dac);
    Image img = Image::make(Screen::kWidth, Screen::kHeight);
    std::copy_n(reinterpret_cast<const uint8_t*>(rgba.data()), img.rgba.size(), img.rgba.begin());
    return img;
}

// fade_NNN.png: VRAM after every retrace of the fade's loads (2 per load); the DAC ends at the target.
int writeFadeFrames(const std::filesystem::path& dir, Video& video, const PalFade& fade, int scale) {
    std::filesystem::create_directories(dir);
    const auto frames = fadeRetraceFrames(video.dac(), fade);
    for (size_t k = 0; k < frames.size(); ++k) {
        Dac dac;
        dac.set(frames[k]);
        writePng(dir / std::format("fade_{:03d}.png", k), frameImage(video.vram(), dac), scale);
    }
    if (!frames.empty()) video.dac().set(frames.back());
    return static_cast<int>(frames.size());
}

// The title tool shows the two screen animations with this minimal reader of their frame steps (the
// N-th frame step of the first sequence, no timing). `--gg-screen title` (gg/ui/ui_cli.h) runs the
// attract's real screen animations (174b on gg/game/screen_anim.h, code_world.md §19.1).
struct AnimFrame {
    int member = 0, x = 0, y = 0;
    uint16_t flags = 0;
};

std::optional<AnimFrame> animFrameStep(std::span<const uint8_t> p, int n) {
    auto s16 = [&](size_t o) { return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{-1000}; };
    const int nSeq = s16(0x2C);
    const size_t seq = 0x32 + 4 * static_cast<size_t>(std::max(nSeq, 1));  // the first sequence's header
    const int count = s16(seq + 8);
    int baseX = s16(0x1E), baseY = s16(0x20), pc = 0, frames = 0;
    for (int guard = 0; guard < 4096 && pc >= 0 && pc < count; ++guard) {
        const size_t st = seq + 10 + 10 * static_cast<size_t>(pc);
        const int op = s16(st), a = s16(st + 2), b = s16(st + 4);
        const auto c = static_cast<uint16_t>(s16(st + 6));
        if (op >= 0) {
            if (frames++ == n) return AnimFrame{op, baseX + a, baseY + b, c};
            ++pc;
        } else if (op == -14) { baseX = a; ++pc; }
        else if (op == -15) { baseY = a; ++pc; }
        else if (op == -4) pc = a;
        else if (op == -1) pc = 0;
        else if (op == -1000 || op == -2) break;
        else ++pc;
    }
    return std::nullopt;
}

int cmdTitle(const GgFiles& files, const std::vector<std::string>& args) {
    const auto ops = operands(args, "--gg-title", 1);
    const auto outPath = pathFromUtf8(ops[0]);
    checkOutput(files, outPath);
    const Archive& arc = files.archive();
    const int phase = intOption(args, "--phase", 3);
    std::vector<int> steps = {0, 0};
    if (const auto v = optionValue(args, "--anim-step")) {
        const auto comma = v->find(',');
        steps = {std::stoi(v->substr(0, comma)), comma == std::string::npos ? 0 : std::stoi(v->substr(comma + 1))};
    }
    Video video;
    video.init(files.exe(), arc);
    video.cursor().hide();  // State_01_Attract: Mouse_Hide
    Screen& screen = video.screen();
    // 2c11:043f Attract_TitleAnim
    std::vector<uint8_t> background;
    video.loadPicture(arc, 0x1002, 0x1000, background);
    screen.lpxDraw(0, 0, background);
    screen.saveAll();
    {  // 2c11:03a7 Attract_DrawPictureSet(0x1001): {I x, y}, G pairs until an I whose first byte is FF
        const Bundle set = Bundle::load(arc, 0x1001);
        for (size_t i = 0; i + 1 < set.size(); i += 2) {
            const auto pos = set.member(i).payload;
            if (pos.empty() || pos[0] == 0xFF || pos.size() < 4) break;
            screen.lpxDrawKeyed(pos[0] | (pos[1] << 8), pos[2] | (pos[3] << 8), set.member(i + 1).payload, 0);
        }
    }
    const Bundle titles = Bundle::load(arc, 0x1028);  // Res_LoadCopy
    if (phase >= 2) screen.lpxDrawKeyed(59, 9, titles.member(0).payload, 0);
    if (phase >= 3) {
        screen.lpxDrawKeyed(52, 51, titles.member(1).payload, 0);
        screen.lpxDrawKeyed(99, 138, titles.member(2).payload, 0);
        screen.lpxDrawKeyed(0, 181, titles.member(3).payload, 0);
    }
    // Attract_PlayAnim(DS:2090): screen animations 0x1005 and 0x1007 as sprites (ScreenAnim_Frame).
    std::vector<Bundle> frameBundles;
    frameBundles.reserve(2);
    const uint16_t anims[2] = {0x1005, 0x1007};
    for (int k = 0; k < 2; ++k) {
        const auto rec = arc.read(anims[k]);
        const auto f = animFrameStep(rec, steps[static_cast<size_t>(k)]);
        if (!f || rec.size() < 0x1A) continue;
        frameBundles.push_back(Bundle::load(arc, static_cast<uint16_t>(rec[0x18] | (rec[0x19] << 8))));
        const auto pic = frameBundles.back().member(static_cast<size_t>(f->member)).payload;
        video.sprites().add({f->flags, f->x, f->y, pic.size() >= 10 ? (pic[6] | (pic[7] << 8)) : 0,
                             pic.size() >= 10 ? (pic[8] | (pic[9] << 8)) : 0, pic});
    }
    video.sprites().drawAll(screen);
    video.flush();
    if (const auto fadeDir = optionValue(args, "--fade-dir")) {  // the first frame's Pal_FadeIn
        const auto dir = pathFromUtf8(*fadeDir);
        checkOutput(files, dir);
        writeFadeFrames(dir, video, video.palette().fadeIn(), intOption(args, "--scale", 1));
    }
    video.dac().set(video.palette().shadow());  // after Pal_FadeIn
    writePng(outPath, frameImage(video.vram(), video.dac()), intOption(args, "--scale", 1));
    out(std::format("title (phase {}, animation steps {},{}) -> {}\n", phase, steps[0], steps[1], pathToUtf8(outPath)));
    return 0;
}

int cmdFade(const GgFiles& files, const std::vector<std::string>& args) {
    const auto ops = operands(args, "--gg-fade", 4);
    const bool in = ops[0] == "in";
    if (!in && ops[0] != "out") throw UsageError("--gg-fade in|out PIC PAL DIR");
    const auto dir = pathFromUtf8(ops[3]);
    checkOutput(files, dir);
    const Archive& arc = files.archive();
    Video video;
    video.init(files.exe(), arc);
    video.cursor().hide();
    std::vector<uint8_t> pic;
    video.loadPicture(arc, parseId(ops[1]), parseId(ops[2]), pic);  // faded: no load
    video.screen().lpxDraw(0, 0, pic);
    video.flush();
    PalFade fade;
    if (in) {
        fade = video.palette().fadeIn();
    } else {
        video.dac().set(video.palette().shadow());
        video.palette().setFaded(false);
        fade = video.palette().fadeOut();
    }
    const int frames = writeFadeFrames(dir, video, fade, intOption(args, "--scale", 1));
    out(std::format("fade {}: {} loads, {} frames -> {}\n", ops[0], fade.loads(), frames, pathToUtf8(dir)));
    return 0;
}

// The wipe of one pattern from a black VRAM to a white screen: wipe_NNN.png after every step.
int cmdWipe(const GgFiles& files, const std::vector<std::string>& args) {
    const auto ops = operands(args, "--gg-wipe", 2);
    const auto dir = pathFromUtf8(ops[1]);
    checkOutput(files, dir);
    const WipePattern pattern = WipePattern::parse(files.archive().read(parseId(ops[0])));
    Screen screen;
    screen.setDrawColor(15);
    screen.fillRect(0, 0, Screen::kWidth, Screen::kHeight);
    Vram vram{};
    BorlandRand rng;
    Wipe wipe(screen, pattern, rng);
    Dac dac;
    dac.set(Palette::fromExe(files.exe()).shadow());
    std::filesystem::create_directories(dir);
    while (!wipe.finished() && wipe.steps() < 1000) {
        wipe.step(screen, vram);
        writePng(dir / std::format("wipe_{:03d}.png", wipe.steps()), frameImage(vram, dac), intOption(args, "--scale", 1));
    }
    wipe.end(screen);
    out(std::format("wipe {}: period {}, {} steps ({} ticks) -> {}\n", ops[0], pattern.period, wipe.steps(),
                    wipe.steps() * pattern.period, pathToUtf8(dir)));
    return 0;
}

int cmdLpxList(const GgFiles& files) {
    const Archive& arc = files.archive();
    out("path\tformat\twidth\theight\tkey\tdata_off\tused\tsize\tcrc_pixels\tcrc_opaque\n");
    int pictures = 0, errors = 0;
    auto row = [&](const std::string& path, std::span<const uint8_t> p) {
        try {
            const DecodedLpx d = decodeLpx(p);
            ++pictures;
            out(std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:08x}\t{:08x}\n", path, d.format, d.width, d.height, d.key,
                            d.dataOffset, d.used, p.size(), png::crc32(d.pixels), png::crc32(d.opaque)));
        } catch (const std::exception& e) {
            ++errors;
            out(std::format("{}\tERROR {}\n", path, e.what()));
        }
    };
    for (const uint16_t id : arc.ids()) {
        const auto r = arc.load(id);
        if (r.resType() == ResType::Bundle) {
            for (const auto& m : parseBundle(r.bytes))
                if (classify(m.type, m.payload, id, m.index) == "picture") row(std::format("{:04x}/{}", id, m.index), m.payload);
        } else if (classify(r.type, r.bytes, id) == "picture") {
            row(std::format("{:04x}", id), r.bytes);
        }
    }
    err(std::format("{} pictures, {} errors\n", pictures, errors));
    return errors ? 1 : 0;
}

int cmdFontList(const GgFiles& files, const std::vector<std::string>& args) {
    const auto ops = operands(args, "--gg-font-list", 1);
    const GgFont f = GgFont::parse(files.archive().read(parseId(ops[0])));
    out(std::format("# flags {} records {} height {} spacing {}\n", f.flags(), f.records(), f.height(), f.spacing()));
    out("code\tyoff\twidth\trows\tbytes\tcrc\tadvance\n");
    for (int c = 0; c < f.records(); ++c) {
        const auto code = static_cast<uint8_t>(c);
        const auto g = f.glyph(code);
        if (g.empty()) continue;
        out(std::format("{:02x}\t{}\t{}\t{}\t{}\t{:08x}\t{}\n", c, f.yOffset(code), f.width(code),
                        g.size() >= 4 ? (g[2] | (g[3] << 8)) : 0, g.size(), png::crc32(g), f.charWidth(code)));
    }
    return 0;
}

}  // namespace

bool gfxCli(const std::vector<std::string>& args, int& exitCode) {
    const auto cmd = std::find_if(args.begin(), args.end(), [](const std::string& a) {
        return std::find(std::begin(kCommands), std::end(kCommands), a) != std::end(kCommands);
    });
    if (cmd == args.end()) return false;
    try {
        const GgFiles files = openFolder(args);
        if (*cmd == "--gg-pic") exitCode = cmdPic(files, args);
        else if (*cmd == "--gg-pal") exitCode = cmdPal(files, args);
        else if (*cmd == "--gg-font") exitCode = cmdFont(files, args);
        else if (*cmd == "--gg-movie") exitCode = cmdMovie(files, args);
        else if (*cmd == "--gg-title") exitCode = cmdTitle(files, args);
        else if (*cmd == "--gg-fade") exitCode = cmdFade(files, args);
        else if (*cmd == "--gg-lpx-list") exitCode = cmdLpxList(files);
        else if (*cmd == "--gg-movie-list") exitCode = cmdMovieList(files, args);
        else if (*cmd == "--gg-wipe") exitCode = cmdWipe(files, args);
        else exitCode = cmdFontList(files, args);
    } catch (const UsageError& e) {
        err(std::string("ggport: ") + e.what() + "\n");
        exitCode = 2;
    } catch (const std::exception& e) {
        err(std::string("ggport: ") + e.what() + "\n");
        exitCode = 1;
    }
    std::fflush(stdout);
    return true;
}

}  // namespace tmport::gg
