#include "gg/game/world_cli.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "gg/data/cli_args.h"
#include "gg/data/gg_files.h"
#include "gg/data/resources.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/game/blueprint.h"
#include "gg/game/building.h"
#include "gg/game/hub.h"
#include "gg/game/pic_cache.h"
#include "gg/game/puzzles.h"
#include "gg/gfx/video.h"
#include "gg/text/font.h"
#include "gg/ui/button.h"
#include "gg/ui/menu_bar.h"
#include "mods/json.h"
#include "mods/png_codec.h"
#include "rtl/borland_rand.h"
#include "util/file_util.h"

namespace tmport::gg {

namespace {

using cli::hasFlag;
using cli::insideFolder;
using cli::openFolder;
using cli::optionValue;

namespace json = mods::json;

constexpr std::string_view kCommands[] = {"--gg-screen", "--gg-rooms"};
constexpr uint16_t kWorkshopPalette = 0x2B;  // Pal_LoadRes(0x2B) of State_06_Workshop: colours 32..63

void out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); }
void err(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stderr); }

struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::vector<std::string> operands(const std::vector<std::string>& args, std::string_view cmd, size_t n) {
    const auto it = std::find(args.begin(), args.end(), cmd);
    if (static_cast<size_t>(args.end() - it) < n + 1) throw UsageError(std::format("{} needs {} arguments", cmd, n));
    return {it + 1, it + 1 + static_cast<ptrdiff_t>(n)};
}

int toInt(const std::string& s) {
    size_t used = 0;
    const int v = std::stoi(s, &used, 0);
    if (used != s.size()) throw UsageError(std::format("bad number '{}'", s));
    return v;
}

void checkOutput(const GgFiles& files, const std::filesystem::path& p) {
    if (insideFolder(p, files.folder())) throw UsageError("refusing to write into the game folder: " + pathToUtf8(p));
}

// --- scene files -------------------------------------------------------------------------------------

json::Value loadScene(const std::vector<std::string>& args) {
    const auto path = optionValue(args, "--scene");
    if (!path) return {};
    const auto bytes = readWholeFile(pathFromUtf8(*path));
    if (!bytes) throw std::runtime_error("cannot read " + *path);
    return json::parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

int64_t num(const json::Value* v, int64_t def = 0) { return v && v->isNumber() ? v->asInt() : def; }
int64_t num(const json::Value& o, std::string_view key, int64_t def = 0) { return num(o.find(key), def); }

// The scene's shadow palette (hex of DS:024e) against the engine's: the colours that differ.
int compareShadow(const json::Value& scene, const Rgb6& shadow) {
    const json::Value* hex = scene.find("shadow");
    if (!hex || !hex->isString() || hex->asString().size() != 2 * shadow.size()) return -1;
    int differ = 0;
    for (size_t c = 0; c < 256; ++c) {
        bool same = true;
        for (size_t k = 0; k < 3; ++k) {
            const auto byte = static_cast<uint8_t>(std::stoi(hex->asString().substr(2 * (3 * c + k), 2), nullptr, 16));
            same = same && byte == shadow[3 * c + k];
        }
        if (!same) ++differ;
    }
    return differ;
}

// --- the frame -----------------------------------------------------------------------------------------

// The cursor at the scene's (or --mouse) position, then Dirty_Flush; the PNG of VRAM through a DAC
// holding the shadow palette (what the screen shows after its fade-in).
void finishAndWrite(Video& video, const GgFiles& files, const std::vector<std::string>& args, const json::Value& scene,
                    const std::filesystem::path& outPath) {
    std::optional<std::pair<int, int>> mouse;
    if (const json::Value* m = scene.find("mouse"); m && m->isObject())
        mouse = std::pair{static_cast<int>(num(*m, "x")), static_cast<int>(num(*m, "y"))};
    if (const auto v = optionValue(args, "--mouse")) {
        const auto comma = v->find(',');
        if (comma == std::string::npos) throw UsageError("--mouse X,Y");
        mouse = std::pair{toInt(v->substr(0, comma)), toInt(v->substr(comma + 1))};
    }
    video.cursor().restoreUnder();  // a cursor drawn by an earlier flush leaves the screen first
    if (hasFlag(args, "--no-cursor") || !mouse) {
        video.cursor().hide();
    } else {
        video.cursor().show();
        video.cursor().setPosition(mouse->first, mouse->second);
    }
    video.screen().dirty().mark(0, 0, 320, 200);  // the whole screen reaches VRAM (the fade-in's flush)
    video.flush();
    video.dac().set(video.palette().shadow());
    const auto rgba = video.frameRgba();
    std::vector<uint8_t> bytes(rgba.size() * 4);
    for (size_t i = 0; i < rgba.size(); ++i)
        for (size_t k = 0; k < 4; ++k) bytes[4 * i + k] = static_cast<uint8_t>(rgba[i] >> (8 * k));
    checkOutput(files, outPath);
    if (!writeWholeFile(outPath, mods::encodePngRgba(Screen::kWidth, Screen::kHeight, bytes)))
        throw std::runtime_error("cannot write " + pathToUtf8(outPath));
}

// The start of every flow: Gfx_Init + Mouse_Init (Video::init, faded) and Hud_Init's loads (its
// palette member is part of the shadow from start-up on).
struct Session {
    const GgFiles& files;
    Video video;
    PicCache cache;
    Hud hud;
    Fonts fonts;
    explicit Session(const GgFiles& f) : files(f), cache(f.archive(), &video.palette()), fonts(Fonts::load(f.archive())) {
        video.init(f.exe(), f.archive());  // the palette object stays the one the cache holds
        hud.init(f.exe(), f.archive(), cache);
    }
};

// 40a4:0000 Menu_SetMenuBar(DS:4053), the drawing (Menu_DrawBar(-1) of the UI area): state 5 and state
// 9 install the File / Options / Help bar before they draw (rows 0..8). --no-menu-bar leaves the rows
// as they are.
void drawMenuBar(Session& s, const std::vector<std::string>& args) {
    if (hasFlag(args, "--no-menu-bar")) return;
    menuDrawBar(UiCanvas::of(s.video, s.fonts), MenuBarDef::fromExe(s.files.exe()), MenuBarPictures::load(s.files.archive()), -1);
}

// --- hub ------------------------------------------------------------------------------------------------

int cmdHub(const GgFiles& files, const std::vector<std::string>& args, const std::filesystem::path& outPath) {
    const json::Value scene = loadScene(args);
    Session s(files);
    HubProgress progress;
    if (const json::Value* w = scene.find("racesWon"); w && w->isArray())
        for (size_t b = 0; b < 3 && b < w->size(); ++b) progress.racesWon[b] = static_cast<int>(num(&w->asArray()[b]));
    if (const json::Value* c = scene.find("vehicleColor"); c && c->isArray())
        for (size_t t = 0; t < 15 && t < c->size(); ++t) progress.vehicleColor[t] = static_cast<uint8_t>(num(&c->asArray()[t]));
    progress.racesWonTotal = static_cast<int>(num(scene, "racesWonTotal"));
    HubScreen hub;
    hub.setup(s.video, files.exe(), files.archive(), s.cache, progress);
    BorlandRand rng;
    rng.setRawSeed(static_cast<uint32_t>(num(scene, "randState", 1)));
    std::string anims;
    if (const json::Value* list = scene.find("anims"); list && list->isArray()) {
        // The animations as the state holds them (after the frame that drew them).
        for (const json::Value& a : list->asArray()) {
            const auto id = static_cast<uint16_t>(num(a, "id"));
            for (size_t i = 0; i < HubScreen::kAnims.size(); ++i) {
                if (HubScreen::kAnims[i] != id) continue;
                ScreenAnim& anim = hub.anim(i);
                const bool ok = anim.restore(static_cast<int>(num(a, "seq")), static_cast<int>(num(a, "step")),
                                             static_cast<int>(num(a, "countdown")), static_cast<int>(num(a, "baseX")),
                                             static_cast<int>(num(a, "baseY")), static_cast<int>(num(a, "state")));
                anims += std::format(" {:04x}:step {}{}", id, anim.stepIndex(), ok ? "" : " (no frame)");
                if (!anim.hidden()) s.video.sprites().add(anim.sprite());
            }
        }
        s.video.sprites().drawAll(s.video.screen());
    } else {
        hub.frame(s.video, rng);  // the first frame
    }
    finishAndWrite(s.video, files, args, scene, outPath);
    const int pal = compareShadow(scene, s.video.palette().shadow());
    out(std::format("hub ->{} {}{}\n", anims, pathToUtf8(outPath),
                    pal < 0 ? "" : std::format(" (shadow palette: {} colours differ from the scene's)", pal)));
    return 0;
}

// --- warehouse ----------------------------------------------------------------------------------------

// The items of the scene's displayed room replace the dealt ones (a run changes them).
void itemsFromScene(Rooms& rooms, const json::Value& scene, PicCache& cache) {
    const json::Value* list = scene.find("items");
    if (!list || !list->isArray()) return;
    Room& room = rooms.room(rooms.displayRoom);
    for (const int i : room.items) rooms.pool()[static_cast<size_t>(i)].free = true;
    room.items.clear();
    room.itemCount = 0;
    for (const json::Value& it : list->asArray()) {
        const int node = rooms.allocItem();
        if (node < 0) break;
        ItemNode& n = rooms.pool()[static_cast<size_t>(node)];
        n.free = false;
        n.pictureId = static_cast<uint16_t>(num(it, "picture"));
        n.kind = static_cast<int>(num(it, "kind"));
        n.sprite = Sprite{static_cast<uint16_t>(num(it, "flags")), static_cast<int>(num(it, "x")),
                          static_cast<int>(num(it, "y")),        static_cast<int>(num(it, "w")),
                          static_cast<int>(num(it, "h")),        cache.load(n.pictureId)};
        room.items.push_back(node);
        ++room.itemCount;
    }
}

// The dealt items of every room against the scene's (when it lists them): how many agree.
std::string compareItems(const Rooms& rooms, const json::Value& scene) {
    const json::Value* list = scene.find("allItems");
    if (!list || !list->isArray()) return {};
    int same = 0, total = 0;
    for (const json::Value& it : list->asArray()) {
        ++total;
        const int r = static_cast<int>(num(it, "room"));
        if (r < 0 || r >= rooms.count()) continue;
        for (const ItemNode* n : rooms.itemsOf(r))
            if (n->sprite.x == num(it, "x") && n->sprite.y == num(it, "y") && n->kind == num(it, "kind")) {
                ++same;
                break;
            }
    }
    return std::format("; items dealt from the set-up seed: {} of the scene's {} found", same, total);
}

int cmdWarehouse(const GgFiles& files, const std::vector<std::string>& args, const std::filesystem::path& outPath) {
    const json::Value scene = loadScene(args);
    Session s(files);
    // The hub's palettes come before the building (they stay where nothing loads over them).
    s.video.palette().store(parsePaletteRange(files.archive().read(HubScreen::kPalette)));
    s.video.palette().store(parsePaletteRange(files.archive().read(HubScreen::kPaletteExtra)));
    int race = static_cast<int>(num(scene, "race", -1));
    int room = static_cast<int>(num(scene, "room", -1));
    if (const auto v = optionValue(args, "--race")) race = toInt(*v);
    if (const auto v = optionValue(args, "--room")) room = toInt(*v);
    if (race < 0 || race > 14 || room < 0) throw UsageError("the warehouse needs --race R --room N or a --scene");
    BorlandRand rng;
    rng.setRawSeed(static_cast<uint32_t>(num(scene, "setupRandState", 0x29CF)));
    if (const auto v = optionValue(args, "--rand-state")) rng.setRawSeed(static_cast<uint32_t>(std::stoul(*v, nullptr, 16)));
    Building b(files.exe(), files.archive(), s.cache);
    b.state().player.withChimps = num(scene, "withChimps", 1) != 0 ? 1 : 0;
    b.setup(race / 5, race % 5, rng, s.hud, s.video);
    if (room >= b.rooms().count()) throw UsageError(std::format("race {} has {} rooms", race, b.rooms().count()));
    const std::string itemCheck = compareItems(b.rooms(), scene);
    if (const json::Value* d = scene.find("doors"); d && d->isArray()) {  // solved doors, as the save keeps them
        std::vector<uint8_t> doors;
        for (const json::Value& v : d->asArray()) doors.push_back(static_cast<uint8_t>(num(&v)));
        b.rooms().restoreDoors(doors);
    }
    drawMenuBar(s, args);  // State_05_EnterRoom installs the bar before Room_Enter
    b.enterRoom(room, s.video, s.hud);
    const bool hall = b.rooms().room(room).isHall();
    if (const json::Value* h = scene.find("hud"); h && h->isObject()) {
        b.state().player.live.partsNeeded = static_cast<int16_t>(num(*h, "partsNeeded"));
        b.state().player.live.projectiles = static_cast<int16_t>(num(*h, "projectiles"));
        b.state().player.points = static_cast<uint32_t>(num(*h, "score"));
    }
    if (const json::Value* list = scene.find("strip"); list && list->isArray())
        for (const json::Value& e : list->asArray()) {
            const int part = static_cast<int>(num(e, "part"));
            HudStripEntry entry;
            entry.part = part;
            entry.sprite = Sprite{static_cast<uint16_t>(num(e, "flags")), static_cast<int>(num(e, "x")),
                                  static_cast<int>(num(e, "y")),        static_cast<int>(num(e, "w")),
                                  static_cast<int>(num(e, "h")),        {}};
            if (part >= 0 && static_cast<size_t>(part) < b.parts().count()) entry.sprite.picture = b.parts().parts[static_cast<size_t>(part)].icon;
            b.strip.push_back(entry);
        }
    itemsFromScene(b.rooms(), scene, s.cache);
    // The animated objects of the displayed room as the state holds them.
    if (const json::Value* list = scene.find("anims"); list && list->isArray()) {
        auto& anims = b.rooms().room(room).anims[0];
        for (const json::Value& a : list->asArray()) {
            const auto k = static_cast<size_t>(num(a, "index"));
            if (k >= anims.size()) continue;
            RoomAnim& obj = anims[k];
            obj.sprite.flags = static_cast<uint16_t>(num(a, "flags"));
            obj.countdown = static_cast<int16_t>(num(a, "countdown"));
            obj.pos = static_cast<int16_t>(num(a, "pos"));
            const int pos = obj.pos - 1;  // the entry fetched last
            const int frame = pos >= 0 && static_cast<size_t>(pos) < obj.script.size() ? obj.script[static_cast<size_t>(pos)] : -1;
            if (obj.pictures && frame >= 0 && static_cast<size_t>(frame) < obj.pictures->size()) {
                obj.sprite.picture = (*obj.pictures)[static_cast<size_t>(frame)];
                obj.sprite.w = obj.sprite.picture.size() >= 10 ? (obj.sprite.picture[6] | (obj.sprite.picture[7] << 8)) : 0;
                obj.sprite.h = obj.sprite.picture.size() >= 10 ? (obj.sprite.picture[8] | (obj.sprite.picture[9] << 8)) : 0;
            }
        }
    }
    // The actors: room, position, flags and the frame of their sequence step.
    std::string frames;
    if (const json::Value* list = scene.find("actors"); list && list->isArray()) {
        for (const json::Value& j : list->asArray()) {
            const int slot = static_cast<int>(num(j, "slot"));
            if (slot < 0 || slot >= 12) continue;
            Actor& a = b.actors()[static_cast<size_t>(slot)];
            a.disabled = static_cast<int>(num(j, "disabled"));
            a.room = static_cast<int>(num(j, "room"));
            a.floor = static_cast<int>(num(j, "floor"));
            a.seq = static_cast<int>(num(j, "seq"));
            a.step = static_cast<int>(num(j, "step"));
            a.immune = static_cast<int>(num(j, "immune"));
            a.anchorX = static_cast<int>(num(j, "anchorX"));
            a.anchorY = static_cast<int>(num(j, "anchorY"));
            if (a.disabled || a.room != room) continue;
            const ActorScale scale = b.scaleOf(slot, hall);
            const AnimStep* st = scale.set ? lastFrameStep(a, *scale.set) : nullptr;
            if (st) actorShowFrame(a, scale, *st);
            a.sprite.flags = static_cast<uint16_t>(num(j, "flags"));
            a.sprite.x = static_cast<int>(num(j, "x"));
            a.sprite.y = static_cast<int>(num(j, "y"));
            const json::Value* member = j.find("member");
            const bool agrees = st && member && member->isNumber() && member->asInt() == st->op;
            frames += std::format(" {}:seq {} frame {}{}", slot, a.seq, st ? st->op : -1, agrees ? "" : " (scene differs)");
        }
    }
    b.state().actors.playerBlink = static_cast<int16_t>(num(scene, "playerBlink"));
    s.hud.state().dirty |= Hud::kFull;
    // --frames N: N more frames of the room's own animation (the pads and flaps step; the actors
    // stand: their logic is wave 3's), e.g. to reach the phase of a shot taken before its state.
    const int more = optionValue(args, "--frames") ? toInt(*optionValue(args, "--frames")) : 0;
    if (more <= 0) b.drawSavedFrame(s.video, s.hud);
    for (int f = 0; f < more; ++f) b.drawFrame(s.video, s.hud);
    finishAndWrite(s.video, files, args, scene, outPath);
    const int pal = compareShadow(scene, s.video.palette().shadow());
    out(std::format("warehouse race {} room {}: actors{} -> {}{}{}\n", race, room, frames, pathToUtf8(outPath),
                    pal < 0 ? "" : std::format(" (shadow palette: {} colours differ from the scene's)", pal), itemCheck));
    return 0;
}

// --- blueprint -----------------------------------------------------------------------------------------

// The blueprint viewer as it opens over the workshop (race from the scene or --race; --page N: then the
// part page N, as code 2000 + N redraws it). Under the box: room 0 (the workshop's background), the
// workshop's palettes and the HUD; the workshop's own pictures are wave 3's, so only the box (the last
// frame picture's rectangle, printed) is the viewer's to compare.
int cmdBlueprint(const GgFiles& files, const std::vector<std::string>& args, const std::filesystem::path& outPath) {
    const json::Value scene = loadScene(args);
    Session s(files);
    s.video.palette().store(parsePaletteRange(files.archive().read(HubScreen::kPalette)));
    s.video.palette().store(parsePaletteRange(files.archive().read(HubScreen::kPaletteExtra)));
    int race = static_cast<int>(num(scene, "race", -1));
    if (const auto v = optionValue(args, "--race")) race = toInt(*v);
    if (race < 0 || race > 14) throw UsageError("the blueprint needs --race R or a --scene");
    BorlandRand rng;
    rng.setRawSeed(static_cast<uint32_t>(num(scene, "setupRandState", 0x29CF)));
    Building b(files.exe(), files.archive(), s.cache);
    b.setup(race / 5, race % 5, rng, s.hud, s.video);
    drawMenuBar(s, args);
    b.enterRoom(0, s.video, s.hud);  // room 0, the workshop's background (State_05 before State_06)
    // State_06_Workshop: Pal_LoadRes(0x2B); Workshop_LayoutShelf: Pal_LoadRes(level descriptor +8).
    s.video.palette().store(parsePaletteRange(files.archive().read(kWorkshopPalette)));
    s.video.palette().store(parsePaletteRange(files.archive().read(b.parts().workshopPalette)));
    // The trophies, the tube signs, the shelf and the vehicle are drawn by the workshop
    // (game/workshop.cpp State_06_Workshop), not by this tool's warehouse screen.
    if (const json::Value* h = scene.find("hud"); h && h->isObject()) {
        s.hud.drawBar(s.video.screen());
        if (num(*h, "labelsShown", 1)) s.hud.drawLabels(s.video.screen(), 7);
        s.hud.drawCounters(s.video.screen(), {static_cast<int>(num(*h, "partsNeeded")), static_cast<int>(num(*h, "projectiles")),
                                              static_cast<uint32_t>(num(*h, "score"))});
    }
    BlueprintView view(files.exe(), files.archive());
    view.open(s.video, s.fonts, race, race % 5);
    PicButton done = PicButton::fromRecord(files.exe(), dg::kBlueprintDoneButton);
    picBtnLoad(files.archive(), done, 248, 142);  // Blueprint_Open: PicBtn_Load(DS:2280) at (248, 142)
    const auto drawDone = [&](Screen&) { picBtnDraw(UiCanvas::of(s.video, s.fonts), done); };
    view.draw(s.video, s.fonts, 1, -1, drawDone);
    int page = -1;
    if (const auto v = optionValue(args, "--page")) {
        page = toInt(*v);
        if (page < 0 || page >= view.pageCount()) throw UsageError(std::format("--page: 0..{}", view.pageCount() - 1));
        view.draw(s.video, s.fonts, 2, page, drawDone);
    }
    finishAndWrite(s.video, files, args, scene, outPath);
    const auto box = view.box();
    const int pal = compareShadow(scene, s.video.palette().shadow());
    out(std::format("blueprint race {}: {} pages, {} hot spots, {}; box ({}, {}) {} x {} -> {}{}\n", race, view.pageCount(),
                    view.hotSpots().size(), page < 0 ? std::string("overview") : std::format("page {}", page), box.x, box.y,
                    box.w, box.h, pathToUtf8(outPath),
                    pal < 0 ? "" : std::format(" (shadow palette: {} colours differ from the scene's)", pal)));
    return 0;
}

// --gg-rooms RACE: the room set of a race, its objects and the dealt items.
int cmdRooms(const GgFiles& files, const std::vector<std::string>& args) {
    const int race = toInt(operands(args, "--gg-rooms", 1)[0]);
    if (race < 0 || race > 14) throw UsageError("race index 0..14");
    PicCache cache(files.archive());
    BorlandRand rng;
    rng.setRawSeed(0x29CF);
    if (const auto v = optionValue(args, "--rand-state")) rng.setRawSeed(static_cast<uint32_t>(std::stoul(*v, nullptr, 16)));
    const uint32_t start = rng.seed();
    PartCatalogue parts = PartCatalogue::parse(files.archive().read(static_cast<uint16_t>(PartCatalogue::kFirstId + race)));
    PartDeck deck;
    Rooms rooms;
    rooms.load(files.archive(), files.exe(), race, parts, deck, rng);
    out(std::format("# race {} room set {:04x}: {} rooms, {} parts, {} crates; rand state {:08x} -> {:08x}\n", race,
                    Rooms::roomSetId(files.exe(), race), rooms.count(), parts.count(), rooms.cratesTotal, start, rng.seed()));
    out("room\tflags\tobjects\tanims\twalls\titems\n");
    for (int r = 0; r < rooms.count(); ++r) {
        const Room& room = rooms.room(r);
        size_t objects = 0, anims = 0, walls = 0;
        for (const auto& l : room.layers) objects += l.size();
        for (const auto& l : room.anims) anims += l.size();
        for (const auto& l : room.walls) walls += l.size();
        std::string items;
        for (const ItemNode* n : rooms.itemsOf(r)) items += std::format("{}({},{}) ", n->kind, n->sprite.x, n->sprite.y);
        out(std::format("{}\t{:04x}\t{}\t{}\t{}\t{}\n", r, room.flags, objects, anims, walls, items));
    }
    return 0;
}

}  // namespace

bool worldCli(const std::vector<std::string>& args, int& exitCode) {
    const auto cmd = std::find_if(args.begin(), args.end(), [](const std::string& a) {
        return std::find(std::begin(kCommands), std::end(kCommands), a) != std::end(kCommands);
    });
    if (cmd == args.end()) return false;
    // --gg-screen is shared with the UI tool (gg/ui/ui_cli.h): only the world's screens are taken here.
    if (*cmd == "--gg-screen") {
        constexpr std::string_view kScreens[] = {"hub", "warehouse", "blueprint", "puzzle"};
        if (cmd + 1 == args.end() || std::find(std::begin(kScreens), std::end(kScreens), *(cmd + 1)) == std::end(kScreens))
            return false;
    }
    try {
        const GgFiles files = openFolder(args);
        if (*cmd == "--gg-rooms") {
            exitCode = cmdRooms(files, args);
        } else {
            const auto what = operands(args, "--gg-screen", 1)[0];
            if (what == "hub") {
                exitCode = cmdHub(files, args, pathFromUtf8(operands(args, "--gg-screen", 2)[1]));
            } else if (what == "warehouse") {
                exitCode = cmdWarehouse(files, args, pathFromUtf8(operands(args, "--gg-screen", 2)[1]));
            } else if (what == "blueprint") {
                exitCode = cmdBlueprint(files, args, pathFromUtf8(operands(args, "--gg-screen", 2)[1]));
            } else if (what == "puzzle") {
                const auto ops = operands(args, "--gg-screen", 4);
                const json::Value scene = loadScene(args);
                Session s(files);
                // The screen a puzzle draws over: the warehouse's HUD (its score shows through the
                // panel's keyed hole at (253, 188); State_08 only fades out, the screen keeps it).
                if (const json::Value* h = scene.find("hud"); h && h->isObject()) {
                    s.hud.drawBar(s.video.screen());
                    if (num(*h, "labelsShown", 1)) s.hud.drawLabels(s.video.screen(), 7);
                    s.hud.drawCounters(s.video.screen(), {static_cast<int>(num(*h, "partsNeeded")),
                                                          static_cast<int>(num(*h, "projectiles")),
                                                          static_cast<uint32_t>(num(*h, "score"))});
                }
                drawMenuBar(s, args);  // State_09_Puzzle installs the bar before the dispatch
                PuzzleSceneValues extra;
                if (const json::Value* p = scene.find("puzzle"); p && p->isObject())
                    for (const auto& [key, value] : p->asObject())
                        if (value.isNumber()) extra[key] = static_cast<int>(value.asInt());
                const auto pz =
                    drawPuzzleScreen(s.video, files.exe(), files.archive(), s.fonts, toInt(ops[1]), toInt(ops[2]), extra);
                finishAndWrite(s.video, files, args, scene, pathFromUtf8(ops[3]));
                out(std::format("puzzle kind {}: {} -> {}\n", ops[1], pz->describe(), ops[3]));
                exitCode = 0;
            } else {
                throw UsageError(std::format("--gg-screen: unknown screen '{}' (hub, warehouse, blueprint, puzzle)", what));
            }
        }
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
