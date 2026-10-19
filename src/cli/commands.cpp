#include "cli/commands.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "anim/anim_cli.h"
#include "app/launch_options.h"
#include "game/room/room_cli.h"
#include "game/screens_cli.h"
#include "game/scenes/scenes_cli.h"
#include "audio/audio_cli.h"
#include "cli/contact_sheet.h"
#include "cli/test_scene.h"
#include "cli/text_ui_commands.h"
#include "cli/tool_mods.h"
#include "data/dgroup_offsets.h"
#include "dos/audio/dos_sound_cli.h"
#include "dos/gfx/dos_gfx_cli.h"
#include "gfx/image_export.h"
#include "mods/mods_cli.h"
#include "platform/engine.h"
#include "platform/engine_setup.h"
#include "res/game_files.h"
#include "tools/autoplay/autoplay.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"
#include "util/png_writer.h"
#include "vfs/data_source.h"

namespace tmport::cli {

namespace {

template <class... Args>
void out(std::format_string<Args...> fmt, Args&&... args) {
    const std::string s = std::format(fmt, std::forward<Args>(args)...);
    std::fwrite(s.data(), 1, s.size(), stdout);
}

gfx::ColorMode modeOf(const Options& o) { return o.colour16 ? gfx::ColorMode::Colour16 : gfx::ColorMode::Colour256; }

ResourceManager::Options resOptions(const Options& o, bool requireSounds) {
    ResourceManager::Options r;
    r.colorMode = modeOf(o);
    r.requireSounds = requireSounds;
    return r;
}

// GameFiles plus the mod folders of --mods (docs/MODDING.md, cli/tool_mods.h).
GameFiles openWithMods(const std::filesystem::path& dataPath, const Options& o) {
    GameFiles files = GameFiles::open(dataPath, resOptions(o, false));
    applyToolMods(files, o.modDirs);
    return files;
}

std::filesystem::path exeDirectory() {
    const char* base = SDL_GetBasePath();  // owned by SDL
    return base ? pathFromUtf8(base) : std::filesystem::path();
}

std::optional<std::filesystem::path> resolveDataPath(const Options& o) {
    if (!o.dataPath.empty()) return pathFromUtf8(o.dataPath);
    if (auto env = getEnv("TM_DATA")) return pathFromUtf8(*env);
    // tmport.ini [engine] data = ... (area input_save).
    if (const auto s = loadEngineSettings(pathFromUtf8(o.configPath)); !s.dataPath.empty()) return pathFromUtf8(s.dataPath);
    return vfs::autoDetectDataPath(exeDirectory());
}

std::string asText(std::span<const uint8_t> b) { return std::string(b.begin(), b.end()); }

}  // namespace

// ------------------------------------------------------------------------------------- --info

int runInfo(const Options& o, const std::filesystem::path& dataPath) {
    bool ok = true;
    auto source = vfs::openDataSource(dataPath);
    out("Data source: {}\n\n", source->describe());

    // File inventory.
    out("Files:\n");
    std::vector<std::string> names = {"TMTWINCD.EXE"};
    for (size_t d = 0; d < kDllCount; ++d) names.emplace_back(dllFileName(static_cast<DllId>(d)));
    for (const auto& n : names) {
        auto f = source->open(n);
        const bool optional = n == "TMT16.DLL" && !o.colour16;
        if (!f) {
            out("  {:<14} MISSING{}\n", n, optional ? " (optional)" : "");
            if (!optional) ok = false;
            continue;
        }
        out("  {:<14} {:>10} bytes\n", n, f->size());
    }
    const auto sounds = source->list("SOUNDS");
    size_t mids = 0;
    for (const auto& e : sounds)
        if (!e.isDirectory && toUpperAscii(e.name).ends_with(".MID")) ++mids;
    out("  SOUNDS\\        {} MIDI files\n\n", mids);

    auto exe = ExeTables::load(*source);
    ResourceManager res(*source, resOptions(o, false));

    // EXE.
    const ne::NeImage& ne = exe->exe();
    out("TMTWINCD.EXE: NE module '{}', {} segments, auto-data segment {}, {} imported modules, {} resources\n",
        ne.moduleName(), ne.segmentCount(), ne.autoDataSegment(), ne.importedModules().size(), ne.resources().size());
    out("DGROUP: {:#x} bytes at file offset {:#x}, {} relocated sites\n", exe->size(),
        ne.segment(exe->segmentNumber()).fileOffset, exe->relocationCount());
    const auto v = exe->validate();
    for (const auto& m : v.messages) out("  {}\n", m);
    if (!v.ok) ok = false;
    // DLL file names stored in DGROUP (sys_resources_saves.md §4) must match the ones we open.
    bool namesOk = asText(exe->cstr(dg::kStrTmt256Dll)) == "TMT256.DLL" &&
                   asText(exe->cstr(dg::kStrTmt16Dll)) == "TMT16.DLL" && asText(exe->cstr(dg::kStrTmtDll)) == "TMT.DLL";
    for (int n = 0; n < 9; ++n)
        namesOk = namesOk && asText(exe->cstr(dg::kStrSoundDll0 + dg::kSoundDllStride * n)) == dllFileName(soundDll(n));
    out("  {}DLL file names in DGROUP match\n", namesOk ? "ok:   " : "FAIL: ");
    if (!namesOk) ok = false;

    // DLLs.
    out("\nResource DLLs (type-15 name table -> NE resources):\n");
    size_t totalSsnd = 0;
    for (size_t d = 0; d < kDllCount; ++d) {
        const ne::NeImage* img = res.dll(static_cast<DllId>(d));
        if (!img) continue;
        std::map<std::string, size_t> counts;
        for (const auto& e : img->nameTable()) counts[e.typeName]++;
        std::string summary;
        for (const auto& [t, c] : counts) summary += std::format(" {} {}", t, c);
        size_t unmatched = 0;
        for (const auto& e : img->nameTable())
            if (!img->findByTypeName(e.typeName, e.idOrdinal)) ++unmatched;
        out("  {:<14} module {:<9} align {} : {} entries:{}{}\n", img->name(), img->moduleName(), img->resourceAlignShift(),
            img->nameTable().size(), summary, unmatched ? std::format("  ({} unmatched!)", unmatched) : "");
        if (unmatched) ok = false;
        totalSsnd += counts["SSND"];
    }
    out("  SSND total: {}\n", totalSsnd);

    // Palette.
    const gfx::Palette pal = res.palette(900);
    std::string keys;
    for (int i = 0; i < 256; ++i)
        if (pal.keyTable()[static_cast<size_t>(i)]) keys += std::format(" {}", i);
    out("\nRRGB 900: colour key (RGB 0000AA) at logical index(es):{}\n", keys);
    gfx::Palette pens = pal;
    pens.setPens(exe->egaColours(), exe->vga16Index());
    std::string penText;
    for (int k = 0; k < 16; ++k) penText += std::format(" {}", pens.pen(k));
    out("EGA pen colours (DS:07d8) -> nearest RRGB index:{}\n", penText);

    // PMAP decode check.
    for (gfx::ColorMode mode : {gfx::ColorMode::Colour256, gfx::ColorMode::Colour16}) {
        const DllId d = mode == gfx::ColorMode::Colour16 ? DllId::TMT16 : DllId::TMT256;
        if (!res.hasDll(d)) continue;
        std::map<gfx::PixEncoding, size_t> enc;
        size_t fail = 0;
        const auto ids = res.ids(ResType::PMAP, d);
        for (int id : ids) {
            try {
                auto pm = res.pixmap(id, mode);
                enc[pm->encoding]++;
            } catch (const std::exception& e) {
                ++fail;
                out("  PMAP {} failed: {}\n", id, e.what());
            }
        }
        res.clearCache();
        out("PMAP {}: {} images, {} RUND, {} raw 8 bpp, {} raw 4 bpp, {} failures\n", dllFileName(d), ids.size(),
            enc[gfx::PixEncoding::Rund], enc[gfx::PixEncoding::Raw8], enc[gfx::PixEncoding::Raw4], fail);
        if (fail) ok = false;
    }

    // BNDL check.
    {
        const auto ids = res.ids(ResType::BNDL, DllId::TMT);
        size_t refs = 0, missing = 0;
        std::map<std::string, size_t> memberTypes;
        for (int id : ids) {
            auto b = res.bundle(id);
            if (!b) continue;
            for (int m : b->ids) {
                ++refs;
                memberTypes[std::string(resTypeName(typeForId(m)))]++;
                if (!res.exists(m)) ++missing;
            }
        }
        std::string mt;
        for (const auto& [t, c] : memberTypes) mt += std::format(" {} {}", t, c);
        out("BNDL: {} bundles, {} member references ({} ), {} missing\n", ids.size(), refs, mt, missing);
        if (missing) ok = false;
    }

    // SSND rates.
    {
        std::map<uint32_t, size_t> rates;
        size_t truncated = 0, n = 0;
        for (int k = 0; k < 9; ++k) {
            if (!res.hasDll(soundDll(k))) continue;
            for (int id : res.ids(ResType::SSND, soundDll(k))) {
                if (auto s = res.soundInfo(id)) {
                    rates[s->sampleRate]++;
                    if (s->truncated) ++truncated;
                    ++n;
                }
            }
        }
        std::string rt;
        for (const auto& [r, c] : rates) rt += std::format(" {} Hz x{}", r, c);
        out("SSND: {} sounds,{}; {} truncated\n", n, rt, truncated);
    }

    // Rooms.
    {
        const auto rooms = exe->rooms();
        std::set<int> bgs;
        for (const auto& r : rooms) bgs.insert(r.bgPmap);
        size_t bgMissing = 0;
        for (int b : bgs)
            if (!res.exists(b)) ++bgMissing;
        out("Rooms (DS:23d8): 50 records, {} distinct backgrounds, {} missing; room 9: bg {}, start ({},{}), exits L{} R{} "
            "door {}\n",
            bgs.size(), bgMissing, rooms[9].bgPmap, rooms[9].startX, rooms[9].startY, rooms[9].exitLeft, rooms[9].exitRight,
            rooms[9].exitDoor);
        if (bgMissing) ok = false;
    }

    out("\nResult: {}\n", ok ? "OK" : "PROBLEMS FOUND");
    return ok ? 0 : 1;
}

// -------------------------------------------------------------------------------- --dump-pmap

int runDumpPmap(const Options& o, const std::filesystem::path& dataPath) {
    GameFiles files = openWithMods(dataPath, o);
    const auto mode = modeOf(o);
    auto pm = files.res->pixmap(o.pmapId, mode);
    if (!pm) {
        log::error("PMAP {} does not exist in {}", o.pmapId, mode == gfx::ColorMode::Colour16 ? "TMT16.DLL" : "TMT256.DLL");
        return 1;
    }
    const gfx::Palette pal = files.makePalette(mode);
    const auto path = pathFromUtf8(o.outPath);
    if (!gfx::writePixmapPng(path, *pm, pal, !o.opaque)) {
        log::error("cannot write {}", o.outPath);
        return 1;
    }
    static const char* encNames[] = {"raw 4 bpp", "raw 8 bpp", "RUND", "memory"};
    out("PMAP {}: {}x{}, {} -> {}\n", o.pmapId, pm->width, pm->height, encNames[static_cast<int>(pm->encoding)], o.outPath);
    return 0;
}

// ------------------------------------------------------------------------------- --dump-sheet

int runDumpSheet(const Options& o, const std::filesystem::path& dataPath) {
    GameFiles files = openWithMods(dataPath, o);
    const auto mode = modeOf(o);
    const gfx::Palette pal = files.makePalette(mode);
    std::vector<std::shared_ptr<const gfx::PixMap>> keep;
    std::vector<SheetItem> items;
    for (int id = o.firstId; id <= o.lastId; ++id) {
        if (typeForId(id) != ResType::PMAP) continue;
        auto pm = files.res->pixmap(id, mode);
        if (!pm) continue;
        keep.push_back(pm);
        items.push_back({id, pm.get()});
    }
    if (items.empty()) {
        log::error("no PMAPs in [{}, {}]", o.firstId, o.lastId);
        return 1;
    }
    const SheetImage img = renderContactSheet(items, pal, !o.opaque);
    if (!png::writeRgba(pathFromUtf8(o.outPath), img.width, img.height, img.rgba)) {
        log::error("cannot write {}", o.outPath);
        return 1;
    }
    out("{} PMAPs ({}..{}) -> {} ({}x{})\n", items.size(), items.front().id, items.back().id, o.outPath, img.width, img.height);
    return 0;
}

// --------------------------------------------------------------------------- --headless-frame

int runHeadlessFrame(const Options& o, const std::filesystem::path& dataPath) {
    GameFiles files = openWithMods(dataPath, o);
    const auto mode = modeOf(o);
    Engine engine(files, mode);  // no window is opened
    const TestSceneResult r = composeTestScene(engine.frame(), *files.res, *files.exe, engine.palette(), mode);
    for (const auto& n : r.notes) out("  {}\n", n);
    engine.presentAll();
    if (!engine.presenter().writeCanvasPng(pathFromUtf8(o.outPath))) {
        log::error("cannot write {}", o.outPath);
        return 1;
    }
    out("frame -> {} (640x480)\n", o.outPath);
    if (!o.fbDumpPath.empty()) {
        const gfx::Palette& pal = engine.palette();
        std::vector<Rgb8> colours(pal.colours().begin(), pal.colours().begin() + pal.size());
        const auto& fb = engine.frame();
        if (!png::writeIndexed(pathFromUtf8(o.fbDumpPath), gfx::FrameBuffer::kWidth, gfx::FrameBuffer::kHeight,
                               std::span<const uint8_t>(fb.pixels(), static_cast<size_t>(gfx::FrameBuffer::kWidth) * gfx::FrameBuffer::kHeight),
                               colours)) {
            log::error("cannot write {}", o.fbDumpPath);
            return 1;
        }
        out("frame buffer -> {} (512x1055)\n", o.fbDumpPath);
    }
    return r.ok ? 0 : 1;
}

// ------------------------------------------------------------------------------------- --view

int runView(const Options& o, const std::filesystem::path& dataPath) {
    GameFiles files = openWithMods(dataPath, o);
    gfx::ColorMode mode = modeOf(o);
    EngineConfig cfg = engineConfigFromSettings(loadEngineSettings(pathFromUtf8(o.configPath)));
    if (o.integerScale) cfg.integerScale = true;
    if (!o.inputMode.empty()) cfg.inputMode = parseInputMode(o.inputMode, cfg.inputMode);
    if (o.tapLatch) cfg.tapLatch = true;
    cfg.pauseOnFocusLoss = false;  // a viewer need not freeze
    if (!o.screenshotDir.empty()) cfg.screenshotDir = pathFromUtf8(o.screenshotDir);
    cfg.title = "TM Port - PMAP viewer";
    Engine engine(files, mode, cfg);
    if (!engine.openWindow()) return 1;

    auto idsFor = [&](gfx::ColorMode m) {
        auto ids = files.res->ids(ResType::PMAP, m == gfx::ColorMode::Colour16 ? DllId::TMT16 : DllId::TMT256);
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    std::vector<int> ids = idsFor(mode);
    if (ids.empty()) {
        log::error("no PMAPs found");
        return 1;
    }
    size_t pos = 0;
    if (o.pmapId) pos = static_cast<size_t>(std::lower_bound(ids.begin(), ids.end(), o.pmapId) - ids.begin());
    pos = std::min(pos, ids.size() - 1);
    bool mirror = false, opaque = false, redraw = true;

    while (!engine.quitRequested()) {
        if (redraw) {
            auto& fb = engine.frame();
            fb.resetClip();
            fb.setDirtyEnabled(false);
            fb.fillRectColor(0, 0, 512, 384, 8);
            const int id = ids[pos];
            std::string title;
            try {
                auto pm = files.res->pixmap(id, mode);
                const int x = std::max(0, (512 - pm->width) / 2), y = std::max(0, (384 - pm->height) / 2);
                if (opaque)
                    fb.drawOpaque(x, y, *pm);
                else
                    fb.drawSprite(x, y, *pm, mirror);
                static const char* enc[] = {"4bpp", "8bpp raw", "RUND", "mem"};
                title = std::format("PMAP {} - {}x{} {} - {} [{}/{}]{}{}", id, pm->width, pm->height,
                                    enc[static_cast<int>(pm->encoding)], mode == gfx::ColorMode::Colour16 ? "TMT16" : "TMT256",
                                    pos + 1, ids.size(), mirror ? " mirrored" : "", opaque ? " opaque" : "");
            } catch (const std::exception& e) {
                title = std::format("PMAP {}: {}", id, e.what());
            }
            engine.presentAll();
            engine.setTitle(title);
            redraw = false;
        }
        const uint16_t c = engine.getInputCode();
        if (c == 0) {
            SDL_Delay(5);
            continue;
        }
        const size_t last = ids.size() - 1;
        switch (c) {
            case input::kLeft: pos = pos > 0 ? pos - 1 : 0; break;
            case input::kRight: pos = std::min(pos + 1, last); break;
            case input::kPageUp: pos = pos >= 10 ? pos - 10 : 0; break;
            case input::kPageDown: pos = std::min(pos + 10, last); break;
            case input::kHome: pos = 0; break;
            case input::kEnd: pos = last; break;
            case 'm': case 'M': mirror = !mirror; break;
            case 'o': case 'O': opaque = !opaque; break;
            case 'i': case 'I': engine.presenter().setIntegerScale(!engine.presenter().integerScale()); break;
            case 't': case 'T': {
                const gfx::ColorMode other = mode == gfx::ColorMode::Colour16 ? gfx::ColorMode::Colour256 : gfx::ColorMode::Colour16;
                if (!files.res->hasDll(other == gfx::ColorMode::Colour16 ? DllId::TMT16 : DllId::TMT256)) break;
                const int cur = ids[pos];
                mode = other;
                engine.setPalette(files.makePalette(mode));
                ids = idsFor(mode);
                pos = std::min(static_cast<size_t>(std::lower_bound(ids.begin(), ids.end(), cur) - ids.begin()), ids.size() - 1);
                break;
            }
            case input::kEsc: return 0;
            default: continue;
        }
        redraw = true;
    }
    return 0;
}

// ------------------------------------------------------------------------------------- --help

namespace {

// One help block without its leading and trailing blank lines, then one blank line.
void putBlock(const std::string& text) {
    const size_t b = text.find_first_not_of('\n');
    if (b == std::string::npos) return;
    const size_t e = text.find_last_not_of('\n');
    const std::string block = text.substr(b, e - b + 1) + "\n\n";
    std::fwrite(block.data(), 1, block.size(), stdout);
}

struct HelpTopic {
    const char* name;
    std::string (*text)();
};

// The option lists of the parsers: the game's (app/launch_options.h) and every tool group's.
constexpr HelpTopic kHelpTopics[] = {
    {"game", app::launchUsage},           {"inspect", inspectUsage},       {"text", textUiUsage},
    {"anim", anim::animUsage},            {"screen", game::screensUsage},  {"scene", game::scenes::scenesUsage},
    {"room", game::room::roomUsage},      {"audio", audio::audioCliUsage}, {"mods", mods::modsUsage},
    {"dos-sound", dos::dosSoundUsage},   {"dos-gfx", dos::dosGfxUsage},
};

constexpr char kHelpHeader[] =
    "tmport - an engine re-implementation of Super Solvers Treasure Mountain! (Windows CD edition).\n"
    "It plays your own copy of the game (the TMTWINCD folder or the CD image); it contains no game data.\n";

}  // namespace

int printHelp(const std::string& topicArg) {
    std::string topic = toLowerAscii(topicArg);
    while (topic.starts_with("-")) topic.erase(0, 1);  // "--scene" -> "scene"
    static constexpr std::pair<std::string_view, std::string_view> kAliases[] = {
        {"play", "game"},   {"data", "inspect"}, {"info", "inspect"}, {"view", "inspect"}, {"dam", "anim"},
        {"screens", "screen"}, {"scenes", "scene"}, {"room-run", "room"}, {"midi", "audio"}, {"mod", "mods"}};
    for (const auto& [alias, name] : kAliases)
        if (topic == alias) topic = name;
    if (topic.empty() || topic == "all") {
        putBlock(kHelpHeader);
        if (topic.empty()) {
            putBlock(app::launchUsage());
            putBlock(toolsOverview());
        } else {
            for (const HelpTopic& t : kHelpTopics) putBlock(t.text());
        }
        return 0;
    }
    for (const HelpTopic& t : kHelpTopics)
        if (topic == t.name) {
            putBlock(t.text());
            return 0;
        }
    std::string names;
    for (const HelpTopic& t : kHelpTopics) names += std::string(names.empty() ? "" : ", ") + t.name;
    std::fprintf(stderr, "tmport: no help topic '%s' (topics: %s, all)\n", topicArg.c_str(), names.c_str());
    return 2;
}

// -------------------------------------------------------------------------------------- main

int run(int argc, char** argv) {
    // Each tool group has its own parser and runs when argv names one of its commands:
    // text/UI renders (--render-dialog/-help/-text/-font, --text-metrics), audio (--dump-ssnd,
    // --midi-dump, --midi-render, --make-test-soundfont), mods (--export-mod-kit, --check-mods),
    // DAM (--render-dam, --dam-info, --dam-check), screens (--screen), scenes (--scene) and the
    // room loop (--room-run). Otherwise the data-inspection tools and --help (cli/options.h).
    if (auto rc = runTextUiCommand(argc, argv)) return *rc;
    if (int audioRc = 0; audio::runAudioCli(argc, argv, audioRc)) return audioRc;
    if (auto rc = mods::runModsCommand(argc, argv)) return *rc;
    if (auto rc = anim::runAnimCommand(argc, argv)) return *rc;
    if (auto rc = game::runScreensCommand(argc, argv)) return *rc;
    if (auto rc = game::scenes::runScenesCommand(argc, argv)) return *rc;
    if (auto rc = game::room::runRoomCommand(argc, argv)) return *rc;
    if (auto rc = autoplay::runAutoplayCommand(argc, argv)) return *rc;  // --autoplay (area autoplay)
    if (auto rc = dos::runDosSoundCommand(argc, argv)) return *rc;      // --dos-sound (area dos_audio)
    if (auto rc = dos::runDosGfxCommand(argc, argv)) return *rc;        // --dos-pan/-pak/-screen (area dos_gfx)
    std::string error;
    auto opts = parseArgs(argc, argv, error);
    if (!opts) {
        std::fprintf(stderr, "tmport: %s\nRun `tmport --help` for the game options and the tools.\n", error.c_str());
        return 2;
    }
    log::setVerbose(opts->verbose);
    if (opts->command == Command::Help) return printHelp(opts->helpTopic);
    const auto dataPath = resolveDataPath(*opts);
    if (!dataPath) {
        std::fprintf(stderr,
                     "tmport: the game data was not found. Pass --data <TMTWINCD folder or CD image> or set TM_DATA.\n");
        return 2;
    }
    try {
        switch (opts->command) {
            case Command::Info: return runInfo(*opts, *dataPath);
            case Command::DumpPmap: return runDumpPmap(*opts, *dataPath);
            case Command::DumpSheet: return runDumpSheet(*opts, *dataPath);
            case Command::HeadlessFrame: return runHeadlessFrame(*opts, *dataPath);
            case Command::View: return runView(*opts, *dataPath);
            case Command::Help: break;
        }
    } catch (const MissingFileError& e) {
        std::fprintf(stderr, "tmport: missing game file %s: %s\n", e.file().c_str(), e.what());
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tmport: %s\n", e.what());
        return 3;
    }
    return 0;
}

}  // namespace tmport::cli
