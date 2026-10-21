#include "cli/text_ui_commands.h"

#include <SDL3/SDL_filesystem.h>

#include <charconv>
#include <cstdio>
#include <format>
#include <set>
#include <string_view>
#include <vector>

#include "cli/tool_mods.h"
#include "data/exe_tables.h"
#include "gfx/frame_buffer.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"
#include "mischief/util/log.h"
#include "mischief/util/png_writer.h"
#include "mischief/vfs/data_source.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "text/font.h"
#include "text/text_renderer.h"
#include "ui/dialog_desc.h"
#include "ui/game_dialog.h"
#include "ui/help_topics.h"

namespace tmport::cli {

namespace {

template <class... Args>
void out(std::format_string<Args...> fmt, Args&&... args) {
    const std::string s = std::format(fmt, std::forward<Args>(args)...);
    std::fwrite(s.data(), 1, s.size(), stdout);
}

enum class Cmd { None, Dialog, Help, Text, Font, Metrics };

struct Args {
    Cmd cmd = Cmd::None;
    std::string dataPath;
    bool colour16 = false;
    std::string target;           // dialog name/offset
    int topic = 0, page = 0;      // help (1-based on the command line)
    int othrId = 0, x = 0, y = 0, w = 512;
    std::string outPath;
    std::string bgPpm;
    int bgPmap = 0, bgX = 0, bgY = 20;
    std::optional<int> sel;
    std::optional<std::string> arg;
    int colour = 0;
    std::string fbDump;
    struct Fill {
        int x, y, w, h, ega;
    };
    std::vector<Fill> fills;  // --fill: solid rectangles drawn after the background
    std::vector<std::string> modDirs;  // --mods <dir> (repeatable, cli/tool_mods.h)
};

bool parseInt(std::string_view s, int& v) {
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
        base = 16;
    }
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
    return !s.empty() && ec == std::errc() && p == s.data() + s.size();
}

// Returns nullopt when no text_ui command is present; sets error on a malformed command line.
std::optional<Args> parse(int argc, char** argv, std::string& error) {
    static const std::set<std::string_view> kCommands = {"--render-dialog", "--render-help", "--render-text",
                                                         "--render-font", "--text-metrics"};
    bool found = false;
    for (int i = 1; i < argc; ++i)
        if (kCommands.count(argv[i])) found = true;
    if (!found) return std::nullopt;

    Args a;
    auto need = [&](int i, int n) {
        if (i + n >= argc) {
            error = std::format("{} needs {} argument(s)", argv[i], n);
            return false;
        }
        return true;
    };
    auto num = [&](int i, int& v) {
        if (!parseInt(argv[i], v)) {
            error = std::format("'{}' is not a number", argv[i]);
            return false;
        }
        return true;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view s = argv[i];
        if (s == "--render-dialog") {
            if (!need(i, 2)) return a;
            a.cmd = Cmd::Dialog;
            a.target = argv[i + 1];
            a.outPath = argv[i + 2];
            i += 2;
        } else if (s == "--render-help") {
            if (!need(i, 3) || !num(i + 1, a.topic) || !num(i + 2, a.page)) return a;
            a.cmd = Cmd::Help;
            a.outPath = argv[i + 3];
            i += 3;
        } else if (s == "--render-text") {
            if (!need(i, 5) || !num(i + 1, a.othrId) || !num(i + 2, a.x) || !num(i + 3, a.y) || !num(i + 4, a.w))
                return a;
            a.cmd = Cmd::Text;
            a.outPath = argv[i + 5];
            i += 5;
        } else if (s == "--render-font") {
            if (!need(i, 1)) return a;
            a.cmd = Cmd::Font;
            a.outPath = argv[++i];
        } else if (s == "--text-metrics") {
            if (!need(i, 1) || !num(i + 1, a.othrId)) return a;
            a.cmd = Cmd::Metrics;
            ++i;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                if (!num(i + 1, a.w)) return a;
                ++i;
            }
        } else if (s == "--data") {
            if (!need(i, 1)) return a;
            a.dataPath = argv[++i];
        } else if (s == "--16") {
            a.colour16 = true;
        } else if (s == "--bg") {
            if (!need(i, 1)) return a;
            a.bgPpm = argv[++i];
        } else if (s == "--bg-pmap") {
            if (!need(i, 1) || !num(i + 1, a.bgPmap)) return a;
            ++i;
        } else if (s == "--bg-at") {
            if (!need(i, 2) || !num(i + 1, a.bgX) || !num(i + 2, a.bgY)) return a;
            i += 2;
        } else if (s == "--sel") {
            int v = 0;
            if (!need(i, 1) || !num(i + 1, v)) return a;
            a.sel = v;
            ++i;
        } else if (s == "--arg") {
            if (!need(i, 1)) return a;
            a.arg = argv[++i];
        } else if (s == "--colour" || s == "--color") {
            if (!need(i, 1) || !num(i + 1, a.colour)) return a;
            ++i;
        } else if (s == "--fill") {
            Args::Fill f{};
            if (!need(i, 5) || !num(i + 1, f.x) || !num(i + 2, f.y) || !num(i + 3, f.w) || !num(i + 4, f.h) ||
                !num(i + 5, f.ega))
                return a;
            a.fills.push_back(f);
            i += 5;
        } else if (s == "--fb-dump") {
            if (!need(i, 1)) return a;
            a.fbDump = argv[++i];
        } else if (s == "--mods") {
            if (!need(i, 1)) return a;
            a.modDirs.emplace_back(argv[++i]);
        } else if (s == "--verbose") {
            mischief::log::setVerbose(true);
        } else {
            error = std::format("unknown argument '{}'", s);
            return a;
        }
    }
    return a;
}

std::optional<std::filesystem::path> resolveDataPath(const Args& a) {
    if (!a.dataPath.empty()) return mischief::pathFromUtf8(a.dataPath);
    if (auto env = mischief::getEnv("TM_DATA")) return mischief::pathFromUtf8(*env);
    const char* base = SDL_GetBasePath();
    return mischief::vfs::autoDetectDataPath(base ? mischief::pathFromUtf8(base) : std::filesystem::path());
}

// Binary PPM (P6, maxval 255) reader for --bg.
bool readPpm(const std::filesystem::path& path, int& w, int& h, std::vector<uint8_t>& rgb) {
    const auto data = mischief::readWholeFile(path);
    if (!data) return false;
    const auto& d = *data;
    size_t p = 0;
    auto skipWs = [&] {
        for (;;) {
            while (p < d.size() && (d[p] == ' ' || d[p] == '\n' || d[p] == '\r' || d[p] == '\t')) ++p;
            if (p < d.size() && d[p] == '#') {
                while (p < d.size() && d[p] != '\n') ++p;
                continue;
            }
            break;
        }
    };
    auto readNum = [&](int& v) {
        skipWs();
        const size_t start = p;
        while (p < d.size() && d[p] >= '0' && d[p] <= '9') ++p;
        if (p == start) return false;
        v = 0;
        for (size_t i = start; i < p; ++i) v = v * 10 + (d[i] - '0');
        return true;
    };
    if (d.size() < 2 || d[0] != 'P' || d[1] != '6') return false;
    p = 2;
    int maxv = 0;
    if (!readNum(w) || !readNum(h) || !readNum(maxv) || maxv != 255) return false;
    ++p;  // single whitespace after maxval
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
    if (d.size() < p + n) return false;
    rgb.assign(d.begin() + static_cast<std::ptrdiff_t>(p), d.begin() + static_cast<std::ptrdiff_t>(p + n));
    return true;
}

// Prepares the screen layer: PPM background, PMAP background or a grey fill.
bool drawBackground(const Args& a, Engine& engine) {
    auto& fb = engine.frame();
    fb.fillRectColor(0, 0, 512, 384, 8);
    if (!a.bgPpm.empty()) {
        int w = 0, h = 0;
        std::vector<uint8_t> rgb;
        if (!readPpm(mischief::pathFromUtf8(a.bgPpm), w, h, rgb) || w != 512 || h != 384) {
            mischief::log::error("--bg: {} is not a 512x384 binary PPM (P6, maxval 255)", a.bgPpm);
            return false;
        }
        const mischief::gfx::Palette& pal = engine.palette();
        // Map every RGB to the nearest palette index (cached per colour).
        std::vector<std::pair<uint32_t, uint8_t>> cache;
        for (int y = 0; y < 384; ++y) {
            uint8_t* row = fb.row(y);
            for (int x = 0; x < 512; ++x) {
                const size_t o = (static_cast<size_t>(y) * 512 + static_cast<size_t>(x)) * 3;
                const uint32_t key = (uint32_t{rgb[o]} << 16) | (uint32_t{rgb[o + 1]} << 8) | rgb[o + 2];
                uint8_t idx = 0;
                bool hit = false;
                for (const auto& [k, v] : cache)
                    if (k == key) {
                        idx = v;
                        hit = true;
                        break;
                    }
                if (!hit) {
                    idx = pal.nearestIndex(mischief::Rgb8{rgb[o], rgb[o + 1], rgb[o + 2]});
                    cache.emplace_back(key, idx);
                }
                row[x] = idx;
            }
        }
    }
    if (a.bgPmap) {
        auto pm = engine.files().res->pixmap(a.bgPmap);
        if (!pm) {
            mischief::log::error("--bg-pmap: PMAP {} does not exist", a.bgPmap);
            return false;
        }
        fb.drawOpaque(a.bgX, a.bgY, *pm);
    }
    for (const auto& f : a.fills) fb.fillRectColor(f.x, f.y, f.w, f.h, f.ega);
    return true;
}

bool writeScreen(const Args& a, Engine& engine) {
    const mischief::gfx::Palette& pal = engine.palette();
    const auto& fb = engine.frame();
    std::vector<mischief::Rgb8> colours(pal.colours().begin(), pal.colours().begin() + pal.size());
    const std::span<const uint8_t> screen(fb.pixels(), static_cast<size_t>(512) * 384);
    if (!mischief::png::writeIndexed(mischief::pathFromUtf8(a.outPath), 512, 384, screen, colours)) {
        mischief::log::error("cannot write {}", a.outPath);
        return false;
    }
    if (!a.fbDump.empty()) {
        const std::span<const uint8_t> all(fb.pixels(), static_cast<size_t>(gfx::FrameBuffer::kWidth) * gfx::FrameBuffer::kHeight);
        if (!mischief::png::writeIndexed(mischief::pathFromUtf8(a.fbDump), gfx::FrameBuffer::kWidth, gfx::FrameBuffer::kHeight, all, colours)) {
            mischief::log::error("cannot write {}", a.fbDump);
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> othrText(const ResourceManager& res, int id) {
    auto body = res.loadRaw(ResType::OTHR, id);
    if (!body) throw mischief::MissingFileError("TMT.DLL", std::format("OTHR {} not found in TMT.DLL", id));
    const mischief::text::Bytes s = mischief::text::untilNul(*body);
    return {s.begin(), s.end()};
}

// Line count of a layout: number of distinct cell rows that received a glyph.
struct LayoutStats {
    mischief::text::TextExtent extent;
    int lines = 0;
    int glyphs = 0;
};
LayoutStats layoutStats(text::TextRenderer& tr, mischief::text::Bytes s, int w) {
    LayoutStats st;
    std::set<int> rows;
    tr.setGlyphObserver([&](uint8_t, int, int y, int) {
        rows.insert(y);
        ++st.glyphs;
    });
    st.extent = tr.measure(s, {}, w);
    tr.setGlyphObserver({});
    st.lines = static_cast<int>(rows.size());
    return st;
}

void printGeometry(const ui::DialogGeometry& g) {
    out("box ({},{}) {}x{}  margins L{} T{} B{}  titleX {}  footer {:#06x} at ({},{})\n", g.x, g.y, g.w, g.h, g.marginL,
        g.marginT, g.marginB, g.titleX, g.footer, g.footerX, g.footerY);
    for (int i = 0; i < 2; ++i) {
        const auto& b = g.buttons[static_cast<size_t>(i)];
        if (b.label == 0) continue;
        out("button {}: ({},{}) {}x{} label {:#06x} at x {}{}\n", i, b.x, b.y, b.w, b.h, b.label, b.labelX,
            b.highlighted ? " (highlighted)" : "");
    }
}

int runDialog(const Args& a, GameFiles& files, Engine& engine, const text::Font& font) {
    const auto off = ui::findDialog(a.target);
    if (!off) {
        std::string names;
        for (const auto& d : ui::staticDialogs()) names += std::format(" {}({:04x})", d.name, d.offset);
        mischief::log::error("unknown dialog '{}'; known:{}", a.target, names);
        return 2;
    }
    ui::DlgDesc d = ui::DlgDesc::fromExe(*files.exe, *off);
    if (a.arg) d.body = ui::formatPercentS(d.body, mischief::text::asBytes(*a.arg));
    if (a.sel) d.defaultButton = static_cast<uint8_t>(*a.sel < 0 ? 0 : *a.sel);
    if (!drawBackground(a, engine)) return 1;
    ui::DialogSystem dialogs(engine, font);
    dialogs.draw(d, false);
    if (a.sel && *a.sel < 0) dialogs.toggleButton(0);  // nothing highlighted (between flash toggles)
    out("DlgDesc {:04x}: type {} footer {} default {} x {} y {} width {} lines {}\n", *off, d.type, d.footer,
        d.defaultButton, d.x, d.y, d.textWidth, d.lineCount);
    printGeometry(dialogs.geometry());
    if (!writeScreen(a, engine)) return 1;
    out("-> {}\n", a.outPath);
    return 0;
}

int runHelp(const Args& a, GameFiles& files, Engine& engine, const text::Font& font) {
    if (a.topic < 1 || a.topic > static_cast<int>(ui::kHelpTopics.size())) {
        mischief::log::error("help topics are 1..{}", ui::kHelpTopics.size());
        return 2;
    }
    const ui::PagerDesc p = ui::buildHelpPager(*files.exe, *files.res, a.topic - 1);
    if (a.page < 1 || a.page > p.pageCount) {
        mischief::log::error("help topic {} has pages 1..{}", a.topic, p.pageCount);
        return 2;
    }
    ui::DlgDesc d = ui::DialogSystem::pagerPage(p, a.page - 1);
    if (a.sel) d.defaultButton = static_cast<uint8_t>(*a.sel);
    if (!drawBackground(a, engine)) return 1;
    ui::DialogSystem dialogs(engine, font);
    dialogs.draw(d, false);
    out("help topic {} page {}/{}: type {}\n", a.topic, a.page, p.pageCount, d.type);
    printGeometry(dialogs.geometry());
    if (!writeScreen(a, engine)) return 1;
    out("-> {}\n", a.outPath);
    return 0;
}

int runText(const Args& a, GameFiles& files, Engine& engine, const text::Font& font) {
    const auto s = othrText(*files.res, a.othrId);
    if (!drawBackground(a, engine)) return 1;
    auto& fb = engine.frame();
    text::TextRenderer tr(font, &fb);
    fb.setForeColor(a.colour);
    const LayoutStats st = layoutStats(tr, s, a.w);
    fb.setForeColor(a.colour);
    tr.drawBox(a.x, a.y, a.w, s);
    out("OTHR {}: {} bytes, box ({},{}) w {}: extent {}x{}, {} rows with glyphs, {} glyphs\n", a.othrId, s.size(), a.x, a.y, a.w,
        st.extent.width, st.extent.height, st.lines, st.glyphs);
    if (!writeScreen(a, engine)) return 1;
    out("-> {}\n", a.outPath);
    return 0;
}

int runMetrics(const Args& a, GameFiles& files, const text::Font& font) {
    const auto s = othrText(*files.res, a.othrId);
    text::TextRenderer tr(font);
    const LayoutStats st = layoutStats(tr, s, a.w);
    out("OTHR {}: {} bytes, width {}: extent {}x{}, {} rows with glyphs, {} glyphs\n", a.othrId, s.size(), a.w, st.extent.width,
        st.extent.height, st.lines, st.glyphs);
    return 0;
}

// Glyph sheet: 8 columns x 32 rows of cells, each cell = advance x height on white with the glyph
// in black, cells separated by grey lines.
int runFont(const Args& a, Engine& engine, const text::Font& font) {
    auto& fb = engine.frame();
    int maxW = 1;
    for (int c = 0; c < font.glyphCount(); ++c) maxW = std::max(maxW, font.charWidth(static_cast<uint8_t>(c)));
    const int cellW = maxW + 4, cellH = font.height() + 4, cols = 8;
    const int rows = (font.glyphCount() + cols - 1) / cols;
    const int W = cols * cellW + 1, H = rows * cellH + 1;
    if (W > gfx::FrameBuffer::kWidth || H > gfx::FrameBuffer::kHeight) {
        mischief::log::error("font sheet {}x{} does not fit the frame buffer", W, H);
        return 1;
    }
    fb.fillRectColor(0, 0, W, H, 7);
    for (int c = 0; c < font.glyphCount(); ++c) {
        const int x = (c % cols) * cellW + 1, y = (c / cols) * cellH + 1;
        fb.fillRectColor(x, y, cellW - 1, cellH - 1, 15);
        fb.setForeColor(0);
        font.drawChar(fb, x + 2, y + 2, static_cast<uint8_t>(c));
    }
    const mischief::gfx::Palette& pal = engine.palette();
    std::vector<uint8_t> rgba(static_cast<size_t>(W) * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const mischief::Rgb8& c = pal.colour(fb.at(x, y));
            uint8_t* p = &rgba[(static_cast<size_t>(y) * W + x) * 4];
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    if (!mischief::png::writeRgba(mischief::pathFromUtf8(a.outPath), W, H, rgba)) {
        mischief::log::error("cannot write {}", a.outPath);
        return 1;
    }
    out("FFNT {}: {} glyphs, height {} -> {} ({}x{})\n", text::kGameFontId, font.glyphCount(), font.height(), a.outPath, W,
        H);
    return 0;
}

}  // namespace

std::string textUiUsage() {
    return
        "Text/UI render commands (headless, write the 512x384 game area as PNG):\n"
        "  --render-dialog <name|hexoffset> <out.png>  draw a DlgDesc (e.g. exit, about, 1533)\n"
        "  --render-help <topic 1..4> <page> <out.png> draw a help pager page\n"
        "  --render-text <othrId> <x> <y> <w> <out.png> lay out an OTHR text with Text_Render\n"
        "  --render-font <out.png>                     FFNT 999 glyph sheet\n"
        "  --text-metrics <othrId> [<w>]               print the measured extent and line count\n"
        "  options: --bg <512x384 P6 PPM> | --bg-pmap <id> [--bg-at <x> <y>], --fill <x> <y> <w> <h> <ega>,\n"
        "           --sel <n> (-1: no button highlighted), --arg <text> (%s of a sign-in dialog),\n"
        "           --colour <ega>, --fb-dump <png>, --16, --mods <dir>, --data <path>, --verbose\n";
}

std::optional<int> runTextUiCommand(int argc, char** argv) {
    std::string error;
    auto a = parse(argc, argv, error);
    if (!a) return std::nullopt;
    if (!error.empty() || a->cmd == Cmd::None) {
        std::fprintf(stderr, "tmport: %s\n\n%s", error.empty() ? "no command" : error.c_str(), textUiUsage().c_str());
        return 2;
    }
    const auto dataPath = resolveDataPath(*a);
    if (!dataPath) {
        std::fprintf(stderr, "tmport: the game data was not found. Pass --data <folder or CD image> or set TM_DATA.\n");
        return 2;
    }
    try {
        ResourceManager::Options ro;
        ro.colorMode = a->colour16 ? mischief::gfx::ColorMode::Colour16 : mischief::gfx::ColorMode::Colour256;
        ro.requireSounds = false;
        GameFiles files = GameFiles::open(*dataPath, ro);
        applyToolMods(files, a->modDirs);
        const text::Font font = text::Font::load(*files.res);
        if (a->cmd == Cmd::Metrics) return runMetrics(*a, files, font);
        Engine engine(files, ro.colorMode);  // headless: no window is opened
        switch (a->cmd) {
            case Cmd::Dialog: return runDialog(*a, files, engine, font);
            case Cmd::Help: return runHelp(*a, files, engine, font);
            case Cmd::Text: return runText(*a, files, engine, font);
            case Cmd::Font: return runFont(*a, engine, font);
            default: break;
        }
    } catch (const mischief::MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
    return 2;
}

}  // namespace tmport::cli
