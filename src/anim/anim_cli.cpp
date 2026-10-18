#include "anim/anim_cli.h"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <set>
#include <string_view>
#include <vector>

#include "anim/anim_bundle.h"
#include "anim/anim_engine.h"
#include "anim/anim_player.h"
#include "anim/dam_script.h"
#include "cli/contact_sheet.h"
#include "cli/tool_mods.h"
#include "data/exe_tables.h"
#include "platform/engine.h"
#include "res/game_files.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"
#include "util/png_writer.h"
#include "vfs/data_source.h"

namespace tmport::anim {

namespace {

template <class... Args>
void out(std::format_string<Args...> fmt, Args&&... args) {
    const std::string s = std::format(fmt, std::forward<Args>(args)...);
    std::fwrite(s.data(), 1, s.size(), stdout);
}

enum class Cmd { None, Render, Info, Check };

struct Args {
    Cmd cmd = Cmd::None;
    std::string dataPath;
    bool colour16 = false;
    int scriptId = 0, bndlId = 0, frameBase = 0, tick = 1;
    std::string outPath;
    int bgPmap = 0, bgX = 0, bgY = 20;
    int bgRoom = -1;
    int originX = 0, originY = 20;
    std::vector<std::pair<int, int>> slots;  // (slot, pmap id)
    int treasure = -1;
    bool framesOnly = false;
    bool playClip = false;
    int stopAt = 0;
    int sheetFirst = 0, sheetLast = 0, cols = 4;
    bool sheet = false;
    bool crop = false;
    int cropX = 0, cropY = 0, cropW = 512, cropH = 384;
    bool buffer = false;
    int fps = 10;
    std::string fbDump;
    struct Load {
        int bndlId, frameBase;
    };
    std::vector<Load> loads;  // --load: more bundles
    struct Also {
        int scriptId, frameBase, x, y;
    };
    std::vector<Also> also;   // --also: channels started before the main one
    int preTicks = 0;         // --pre: ticks of the --also channels before the main script starts
    std::vector<std::string> modDirs;  // --mods <dir> (repeatable, cli/tool_mods.h)
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
    static const std::set<std::string_view> kCommands = {"--render-dam", "--dam-info", "--dam-check"};
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
        if (s == "--render-dam") {
            if (!need(i, 5) || !num(i + 1, a.scriptId) || !num(i + 2, a.bndlId) || !num(i + 3, a.frameBase) ||
                !num(i + 4, a.tick))
                return a;
            a.cmd = Cmd::Render;
            a.outPath = argv[i + 5];
            i += 5;
        } else if (s == "--dam-info") {
            if (!need(i, 1) || !num(i + 1, a.scriptId)) return a;
            a.cmd = Cmd::Info;
            ++i;
        } else if (s == "--dam-check") {
            a.cmd = Cmd::Check;
        } else if (s == "--data") {
            if (!need(i, 1)) return a;
            a.dataPath = argv[++i];
        } else if (s == "--16") {
            a.colour16 = true;
        } else if (s == "--bg") {
            if (!need(i, 1) || !num(i + 1, a.bgPmap)) return a;
            ++i;
        } else if (s == "--bg-at") {
            if (!need(i, 2) || !num(i + 1, a.bgX) || !num(i + 2, a.bgY)) return a;
            i += 2;
        } else if (s == "--bg-room") {
            if (!need(i, 1) || !num(i + 1, a.bgRoom)) return a;
            ++i;
        } else if (s == "--origin") {
            if (!need(i, 2) || !num(i + 1, a.originX) || !num(i + 2, a.originY)) return a;
            i += 2;
        } else if (s == "--slot") {
            int slot = 0, id = 0;
            if (!need(i, 2) || !num(i + 1, slot) || !num(i + 2, id)) return a;
            a.slots.emplace_back(slot, id);
            i += 2;
        } else if (s == "--treasure") {
            if (!need(i, 1) || !num(i + 1, a.treasure)) return a;
            ++i;
        } else if (s == "--load") {
            Args::Load l{};
            if (!need(i, 2) || !num(i + 1, l.bndlId) || !num(i + 2, l.frameBase)) return a;
            a.loads.push_back(l);
            i += 2;
        } else if (s == "--also") {
            Args::Also al{};
            if (!need(i, 4) || !num(i + 1, al.scriptId) || !num(i + 2, al.frameBase) || !num(i + 3, al.x) ||
                !num(i + 4, al.y))
                return a;
            a.also.push_back(al);
            i += 4;
        } else if (s == "--pre") {
            if (!need(i, 1) || !num(i + 1, a.preTicks)) return a;
            ++i;
        } else if (s == "--frames-only") {
            a.framesOnly = true;
        } else if (s == "--play-clip") {
            a.playClip = true;
        } else if (s == "--stop-at") {
            if (!need(i, 1) || !num(i + 1, a.stopAt)) return a;
            ++i;
        } else if (s == "--sheet") {
            if (!need(i, 2) || !num(i + 1, a.sheetFirst) || !num(i + 2, a.sheetLast)) return a;
            a.sheet = true;
            i += 2;
        } else if (s == "--cols") {
            if (!need(i, 1) || !num(i + 1, a.cols)) return a;
            ++i;
        } else if (s == "--crop") {
            if (!need(i, 4) || !num(i + 1, a.cropX) || !num(i + 2, a.cropY) || !num(i + 3, a.cropW) ||
                !num(i + 4, a.cropH))
                return a;
            a.crop = true;
            i += 4;
        } else if (s == "--buffer") {
            a.buffer = true;
        } else if (s == "--fps") {
            if (!need(i, 1) || !num(i + 1, a.fps)) return a;
            ++i;
        } else if (s == "--fb-dump") {
            if (!need(i, 1)) return a;
            a.fbDump = argv[++i];
        } else if (s == "--mods") {
            if (!need(i, 1)) return a;
            a.modDirs.emplace_back(argv[++i]);
        } else if (s == "--verbose") {
            log::setVerbose(true);
        } else {
            error = std::format("unknown argument '{}'", s);
            return a;
        }
    }
    if (a.cmd == Cmd::Render) {
        if (a.tick < 0) error = "the tick must be >= 0";
        if (a.sheet && (a.sheetFirst < 0 || a.sheetLast < a.sheetFirst)) error = "bad --sheet range";
        if (a.cols < 1) a.cols = 1;
        if (a.cropW < 1 || a.cropH < 1) error = "bad --crop size";
    }
    return a;
}

std::optional<std::filesystem::path> resolveDataPath(const Args& a) {
    if (!a.dataPath.empty()) return pathFromUtf8(a.dataPath);
    if (auto env = getEnv("TM_DATA")) return pathFromUtf8(*env);
    const char* base = SDL_GetBasePath();
    return vfs::autoDetectDataPath(base ? pathFromUtf8(base) : std::filesystem::path());
}

// 512x384 RGBA of what the player sees (presented window) or of the buffer's screen rows.
std::vector<uint8_t> captureGameArea(Engine& engine, bool buffer) {
    if (!buffer) return engine.presenter().gameAreaRgba();
    const auto& rgba = engine.palette().rgba();
    const gfx::FrameBuffer& fb = engine.frame();
    std::vector<uint8_t> img(static_cast<size_t>(gfx::FrameBuffer::kWidth) * gfx::FrameBuffer::kScreenHeight * 4);
    for (int y = 0; y < gfx::FrameBuffer::kScreenHeight; ++y)
        for (int x = 0; x < gfx::FrameBuffer::kWidth; ++x) {
            const uint32_t c = rgba[fb.at(x, y)];
            uint8_t* p = img.data() + (static_cast<size_t>(y) * gfx::FrameBuffer::kWidth + static_cast<size_t>(x)) * 4;
            p[0] = static_cast<uint8_t>(c);
            p[1] = static_cast<uint8_t>(c >> 8);
            p[2] = static_cast<uint8_t>(c >> 16);
            p[3] = 0xFF;
        }
    return img;
}

std::vector<uint8_t> cropRgba(const std::vector<uint8_t>& src, int srcW, int srcH, int x, int y, int w, int h) {
    std::vector<uint8_t> dst(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            const int sx = x + c, sy = y + r;
            if (sx < 0 || sy < 0 || sx >= srcW || sy >= srcH) continue;
            std::memcpy(dst.data() + (static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(c)) * 4,
                        src.data() + (static_cast<size_t>(sy) * static_cast<size_t>(srcW) + static_cast<size_t>(sx)) * 4, 4);
        }
    return dst;
}

std::string describeTick(const AnimEngine& anim, const std::vector<int>& sounds, bool sfxStop) {
    std::string s;
    for (const AnimSprite& sp : anim.sprites())
        s += std::format(" f{}@({},{}){}", sp.frame, sp.x, sp.y, sp.mode ? std::format("m{}", sp.mode) : "");
    for (int n : sounds) s += std::format(" sfx:{}", kSfxBase + n);
    if (sfxStop) s += " sfx-stop";
    return s;
}

// ---------------------------------------------------------------------------------- --render-dam

int runRender(const Args& a, GameFiles& files, gfx::ColorMode mode) {
    Engine engine(files, mode);  // headless: no window is opened
    gfx::FrameBuffer& fb = engine.frame();
    const ResourceManager& res = *files.res;
    if (a.playClip)
        fb.setPlayClip(0);
    else
        fb.resetClip();

    // Background: black screen, the picture opaque, copied to the back page, presented.
    fb.setDirtyEnabled(false);
    fb.fillRectColor(0, 0, gfx::FrameBuffer::kWidth, gfx::FrameBuffer::kScreenHeight, 0);
    int bgPmap = a.bgPmap, bgX = a.bgX, bgY = a.bgY;
    if (a.bgRoom >= 0) {
        const auto rooms = files.exe->rooms();
        if (a.bgRoom >= static_cast<int>(rooms.size())) throw Error(std::format("no room {}", a.bgRoom));
        bgPmap = rooms[static_cast<size_t>(a.bgRoom)].bgPmap;
        bgX = 0;
        bgY = 20;
        out("room {} background: PMAP {}\n", a.bgRoom, bgPmap);
    }
    if (bgPmap) {
        auto pm = res.pixmap(bgPmap);
        if (!pm) throw Error(std::format("PMAP {} not found", bgPmap));
        fb.drawOpaque(bgX, bgY, *pm);
    }
    fb.copyRect(0, 0, gfx::FrameBuffer::kWidth, gfx::FrameBuffer::kScreenHeight, 0, gfx::FrameBuffer::kLayer1Y);
    engine.presentAll();

    // Frames and script.
    std::vector<int> sounds;
    bool sfxStop = false;
    AnimSound snd;
    snd.play = [&](int n) { sounds.push_back(n); };
    snd.stop = [&] { sfxStop = true; };
    AnimEngine anim(fb, enginePresent(engine), snd);
    // Loads a bundle; returns its script if member 0 is one (animation bundle).
    auto load = [&](int bndlId, int frameBase) -> ScriptPtr {
        const auto info = res.bundle(bndlId);
        if (!info) throw Error(std::format("BNDL {} not found", bndlId));
        const bool animBundle = !a.framesOnly && !info->ids.empty() && typeForId(info->ids[0]) == ResType::OTHR;
        if (animBundle) {
            const LoadedBundle b = loadBundle(res, anim, bndlId, frameBase);
            out("BNDL {}: script OTHR {}, {} frames -> slots {}..{}\n", bndlId, b.memberIds[0], b.frameCount, frameBase,
                frameBase + b.frameCount - 1);
            return b.script;
        }
        const int n = loadFrames(res, anim, bndlId, frameBase);
        out("BNDL {}: {} frames -> slots {}..{}\n", bndlId, n, frameBase, frameBase + n - 1);
        return nullptr;
    };
    std::vector<ScriptPtr> loadedScripts;
    for (const auto& l : a.loads)
        if (auto s = load(l.bndlId, l.frameBase)) loadedScripts.push_back(std::move(s));
    if (a.bndlId)
        if (auto s = load(a.bndlId, a.frameBase)) loadedScripts.push_back(std::move(s));
    // The bundle's own script object when it has that id (as the game uses it), else OTHR <id>.
    auto scriptFor = [&](int id) {
        for (const auto& s : loadedScripts)
            if (s->id == id) return s;
        return loadScript(res, id);
    };
    for (const auto& [slot, id] : a.slots) {
        anim.setFrame(slot, res.pixmap(id));
        out("slot {} = PMAP {}{}\n", slot, id, anim.frame(slot) ? "" : " (missing)");
    }
    if (a.treasure >= 0) {
        const int bndl = 330 + a.treasure;
        anim.setFrame(kDeliveredTreasureSlot, bundleMember(res, bndl, 0));
        out("slot {} = BNDL {} member 0{}\n", kDeliveredTreasureSlot, bndl, anim.frame(kDeliveredTreasureSlot) ? "" : " (missing)");
    }
    const ScriptPtr script = scriptFor(a.scriptId);
    const DamStats st = analyze(*script);
    out("DAM {}: {} bytes to FF, {} sprites, {} ticks (FE), loop-forever {}, ends {}\n", a.scriptId, st.endOffset + 1,
        st.sprites, st.endFrames, st.loopsForever ? "yes" : "no", st.endsWithFeFf ? "FE FF" : "FD..FF");

    // Play: the Anim_PlayModal tick (erase, step, draw layer 0, update rects, dirty flush).
    fb.setDirtyEnabled(true);
    for (const auto& al : a.also) {
        const int c = anim.start(layer::kDefault, al.x, al.y, al.frameBase, scriptFor(al.scriptId), 0, 0, kChannelFlagErase);
        out("channel {}: DAM {} at ({}, {}) frame base {}\n", c, al.scriptId, al.x, al.y, al.frameBase);
    }
    for (int t = 1; t <= a.preTicks; ++t) {
        anim.tick(layer::kDefault);
        engine.flushDirtyRects();
    }
    if (a.preTicks) out("{} ticks before the main script\n", a.preTicks);
    const int ch = anim.start(layer::kDefault, a.originX, a.originY, a.frameBase, script, 0, 0, kChannelFlagErase);
    out("channel {}: DAM {} at ({}, {}) frame base {}\n", ch, a.scriptId, a.originX, a.originY, a.frameBase);
    const int32_t period = modalTickMs(a.fps);
    const int lastTick = a.sheet ? std::max(a.tick, a.sheetLast) : a.tick;
    struct Cell {
        int tick;
        std::vector<uint8_t> rgba;
    };
    std::vector<Cell> cells;
    const int cx = a.crop ? a.cropX : 0, cy = a.crop ? a.cropY : 0;
    const int cw = a.crop ? a.cropW : 512, chh = a.crop ? a.cropH : 384;
    auto grab = [&](int t) {
        cells.push_back({t, cropRgba(captureGameArea(engine, a.buffer), 512, 384, cx, cy, cw, chh)});
    };
    if (a.sheet && a.sheetFirst == 0) grab(0);
    int ended = 0;
    for (int t = 1; t <= lastTick; ++t) {
        if (!anim.isRunning(ch, script)) {
            ended = t - 1;
            break;
        }
        if (a.stopAt == t) anim.stop(ch, script);
        sounds.clear();
        sfxStop = false;
        anim.tick(layer::kDefault);
        engine.flushDirtyRects();
        out("tick {:3} t={:5}ms:{}{}\n", t, static_cast<int64_t>(t - 1) * period, describeTick(anim, sounds, sfxStop),
            anim.isRunning(ch, script) ? "" : "  [END]");
        if (a.sheet && t >= a.sheetFirst && t <= a.sheetLast) grab(t);
    }
    if (ended) out("script ended after tick {}; later ticks are not played (the modal player returns)\n", ended);

    bool ok = true;
    if (a.sheet) {
        const int n = static_cast<int>(cells.size());
        const int cols = std::min(a.cols, std::max(n, 1));
        const int rows = (n + cols - 1) / cols;
        constexpr int kGap = 2, kLabel = 9;
        cli::SheetImage img;
        img.width = cols * (cw + kGap) + kGap;
        img.height = rows * (chh + kLabel + kGap) + kGap;
        img.rgba.assign(static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4, 0);
        for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 0xFF;
        for (int k = 0; k < n; ++k) {
            const int ox = kGap + (k % cols) * (cw + kGap);
            const int oy = kGap + (k / cols) * (chh + kLabel + kGap);
            cli::drawTinyText(img, ox, oy + 2, std::to_string(cells[static_cast<size_t>(k)].tick).c_str(), 0xFF00FFFFu);
            for (int r = 0; r < chh; ++r)
                std::memcpy(img.rgba.data() + (static_cast<size_t>(oy + kLabel + r) * static_cast<size_t>(img.width) +
                                               static_cast<size_t>(ox)) * 4,
                            cells[static_cast<size_t>(k)].rgba.data() + static_cast<size_t>(r) * static_cast<size_t>(cw) * 4,
                            static_cast<size_t>(cw) * 4);
        }
        ok = png::writeRgba(pathFromUtf8(a.outPath), img.width, img.height, img.rgba);
        out("wrote sheet of {} ticks ({}x{}) to {}\n", n, img.width, img.height, a.outPath);
    } else {
        const auto rgba = cropRgba(captureGameArea(engine, a.buffer), 512, 384, cx, cy, cw, chh);
        ok = png::writeRgba(pathFromUtf8(a.outPath), cw, chh, rgba);
        out("wrote tick {} ({}x{}) to {}\n", std::min(a.tick, ended ? ended : a.tick), cw, chh, a.outPath);
    }
    if (!a.fbDump.empty()) {
        std::vector<uint8_t> idx(fb.pixels(), fb.pixels() + static_cast<size_t>(gfx::FrameBuffer::kWidth) * gfx::FrameBuffer::kHeight);
        ok = png::writeIndexed(pathFromUtf8(a.fbDump), gfx::FrameBuffer::kWidth, gfx::FrameBuffer::kHeight, idx,
                               engine.palette().colours()) && ok;
    }
    return ok ? 0 : 1;
}

// ------------------------------------------------------------------------------------ --dam-info

int runInfo(const Args& a, GameFiles& files) {
    const ScriptPtr script = loadScript(*files.res, a.scriptId);
    out("DAM {} ({} bytes with NE padding){}\n", a.scriptId, script->bytes.size(), script->isDop() ? " DOP" : "");
    for (const DamOp& o : disassemble(*script)) out("  {}\n", formatOp(o));
    const DamStats st = analyze(*script);
    out("stats: {} FD, {} FE, {} FF, {} FA, {} FC, {} FB, {} F9, {} no-ops; max frame {}, max {} sprites/tick, ends {}{}\n",
        st.sprites, st.endFrames, st.ends, st.loops, st.sounds, st.sfxStops, st.updateRects, st.noOps, st.maxFrame,
        st.maxSpritesPerTick, st.endsWithFeFf ? "FE FF" : "FD..FF", st.loopsForever ? ", loops forever" : "");

    // Simulation (no drawing): a looping script is followed for two passes.
    gfx::FrameBuffer fb;
    std::vector<int> sounds;
    bool sfxStop = false;
    AnimSound snd;
    snd.play = [&](int n) { sounds.push_back(n); };
    snd.stop = [&] { sfxStop = true; };
    AnimEngine anim(fb, {}, snd);
    const int ch = anim.start(0, 0, 0, 0, script);
    const int maxTicks = st.loopsForever ? 2 * st.endFrames + 1 : 10000;
    int t = 0;
    while (anim.isRunning(ch, script) && t < maxTicks) {
        ++t;
        sounds.clear();
        sfxStop = false;
        anim.step();
        out("  tick {:3}:{}{}\n", t, describeTick(anim, sounds, sfxStop), anim.isRunning(ch, script) ? "" : "  [END]");
    }
    if (anim.isRunning(ch, script)) out("  ... still running after {} ticks (loops until stopped)\n", t);
    return 0;
}

// ----------------------------------------------------------------------------------- --dam-check

int runCheck(GameFiles& files) {
    const ResourceManager& res = *files.res;
    int scripts = 0, failed = 0;
    DamStats total;
    std::vector<int> skipped;
    for (int id : res.ids(ResType::OTHR, DllId::TMT)) {
        if (id < 20000 || id > 20399) continue;
        const ScriptPtr s = makeScript(id, *res.loadRaw(ResType::OTHR, id));
        if (!s->hasDamTag() && !s->isDop()) {
            skipped.push_back(id);
            continue;
        }
        ++scripts;
        try {
            const DamStats st = analyze(*s);
            out("DAM {:5}: {:4} bytes, {:3} FD {:3} FE {} FF {} FA {:2} FC {} FB {} F9 {} no-op, max frame {:2}, {} spr/tick, {}{}{}\n",
                id, st.endOffset + 1, st.sprites, st.endFrames, st.ends, st.loops, st.sounds, st.sfxStops, st.updateRects,
                st.noOps, st.maxFrame, st.maxSpritesPerTick, st.endsWithFeFf ? "FE FF" : "FD..FF",
                st.loopsForever ? ", loops" : "", st.nonZeroMode ? ", mode bits" : "");
            total.sprites += st.sprites;
            total.endFrames += st.endFrames;
            total.ends += st.ends;
            total.loops += st.loops;
            total.sounds += st.sounds;
            total.sfxStops += st.sfxStops;
            total.updateRects += st.updateRects;
            total.noOps += st.noOps;
        } catch (const std::exception& e) {
            ++failed;
            out("DAM {:5}: FAILED: {}\n", id, e.what());
        }
    }
    std::string sk;
    for (int id : skipped) sk += std::format(" {}", id);
    out("{} DAM scripts, {} failed; totals: {} FD, {} FE, {} FF, {} FA, {} FC, {} FB, {} F9, {} no-ops\n", scripts, failed,
        total.sprites, total.endFrames, total.ends, total.loops, total.sounds, total.sfxStops, total.updateRects, total.noOps);
    if (!skipped.empty()) out("non-DAM OTHR ids in 20000-20399:{}\n", sk);
    return failed ? 1 : 0;
}

}  // namespace

std::optional<int> runAnimCommand(int argc, char** argv) {
    std::string error;
    auto a = parse(argc, argv, error);
    if (!a) return std::nullopt;
    if (!error.empty() || a->cmd == Cmd::None) {
        std::fprintf(stderr, "tmport: %s\n\n%s", error.empty() ? "no command" : error.c_str(), animUsage().c_str());
        return 2;
    }
    const auto dataPath = resolveDataPath(*a);
    if (!dataPath) {
        std::fprintf(stderr, "tmport: the game data was not found. Pass --data <folder or CD image> or set TM_DATA.\n");
        return 2;
    }
    try {
        ResourceManager::Options ro;
        ro.colorMode = a->colour16 ? gfx::ColorMode::Colour16 : gfx::ColorMode::Colour256;
        ro.requireSounds = false;
        GameFiles files = GameFiles::open(*dataPath, ro);
        cli::applyToolMods(files, a->modDirs);
        switch (a->cmd) {
            case Cmd::Render: return runRender(*a, files, ro.colorMode);
            case Cmd::Info: return runInfo(*a, files);
            case Cmd::Check: return runCheck(files);
            default: break;
        }
    } catch (const MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
    return 2;
}

std::string animUsage() {
    return "DAM animation tools (headless):\n"
           "  --render-dam <scriptId> <bndlId> <frameBase> <tick> <out.png>\n"
           "        run a DAM script <tick> ticks over a background and write the presented game area\n"
           "        [--bg <pmap> [--bg-at <x> <y>] | --bg-room <n>] [--origin <x> <y>] [--slot <n> <pmap>]...\n"
           "        [--treasure <t>] [--load <bndl> <base>]... [--also <script> <base> <x> <y>]... [--pre <n>]\n"
           "        [--frames-only] [--play-clip] [--stop-at <tick>] [--sheet <first> <last>\n"
           "        [--cols <n>]] [--crop <x> <y> <w> <h>] [--buffer] [--fps <n>] [--fb-dump <png>]\n"
           "  --dam-info <scriptId>      disassemble and simulate one DAM script\n"
           "  --dam-check                parse every DAM script in TMT.DLL\n"
           "  options of all three: --data <path>, --16, --mods <dir> (repeatable), --verbose\n";
}

}  // namespace tmport::anim
