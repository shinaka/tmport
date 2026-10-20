// GgGame: the game context (game.h), the service-held globals and the DGROUP dump of the whole game.
#include "gg/game/game.h"

#include <algorithm>
#include <array>
#include <deque>
#include <functional>

#include "gg/data/gg_exe.h"
#include "gg/game/actors.h"
#include "gg/game/flow.h"
#include "gg/game/puzzles.h"
#include "gg/game/workshop.h"
#include "gg/game/world.h"
#include "gg/ui/ui_objects.h"

namespace tmport::gg {

namespace {

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }
void put16(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void put32(uint8_t* p, uint32_t v) {
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

// A global held by a service: where it is, how the game's object gives it and takes it.
struct ServiceField {
    std::string_view name;
    uint16_t ds = 0;
    uint16_t size = 0;
    std::function<void(GgGame&, uint8_t*)> store;           // writes `size` bytes
    std::function<void(GgGame&, const uint8_t*)> load;      // empty: not restored from an image
    bool time = false;                                      // restored only with withTime
    uint16_t stride = 0;                                    // record size when only some bytes are modelled
    std::span<const uint8_t> mask = {};                     // per byte of a record: 1 modelled (DgroupField)
};

// g_picCache DS:6640: {i16 id, i16 uses} modelled, the far data pointer not (a run-time segment).
constexpr std::array<uint8_t, 8> kPicCacheMask{1, 1, 1, 1, 0, 0, 0, 0};

const std::vector<ServiceField>& services() {
    static const std::vector<ServiceField> fields = [] {
        std::vector<ServiceField> f;
        f.push_back({"g_ticks", dg::kTicks, 4, [](GgGame& g, uint8_t* p) { put32(p, g.ticks()); },
                     [](GgGame& g, const uint8_t* p) { g.clock().setTicks(get32(p)); }, true});
        f.push_back({"g_timers", dg::kTimers, 4 * GgTimers::kSlots,
                     [](GgGame& g, uint8_t* p) {
                         for (int k = 0; k < GgTimers::kSlots; ++k) put32(p + 4 * k, static_cast<uint32_t>(g.timers().deadline(k)));
                     },
                     [](GgGame& g, const uint8_t* p) {
                         // The deadline relative to g_ticks (negative: already passed), so the clock's
                         // own count needs no offset.
                         for (int k = 0; k < GgTimers::kSlots; ++k) {
                             const auto ahead = static_cast<int32_t>(get32(p + 4 * k) - g.ticks());
                             g.timers().core().set(k, ahead);
                         }
                     },
                     true});
        f.push_back({"g_kbdPlayMode", dg::kKbdPlayMode, 2, [](GgGame& g, uint8_t* p) { put16(p, g.keyboard().isr().playMode); },
                     [](GgGame& g, const uint8_t* p) { g.keyboard().isr().playMode = get16(p); }});
        f.push_back({"g_ctrlHeld", dg::kCtrlHeld, 2, [](GgGame& g, uint8_t* p) { put16(p, g.keyboard().isr().ctrlHeld); },
                     [](GgGame& g, const uint8_t* p) { g.keyboard().isr().ctrlHeld = get16(p); }});
        f.push_back({"g_heldKeys", dg::kHeldKeys, 2, [](GgGame& g, uint8_t* p) { put16(p, g.keyboard().isr().heldKeys); },
                     [](GgGame& g, const uint8_t* p) { g.keyboard().isr().heldKeys = get16(p); }});
        f.push_back({"g_kbdRingWrite", dg::kKbdRingWrite, 2, [](GgGame& g, uint8_t* p) { put16(p, g.keyboard().ring().writeIndex()); }, {}});
        f.push_back({"g_kbdRingRead", dg::kKbdRingRead, 2, [](GgGame& g, uint8_t* p) { put16(p, g.keyboard().ring().readIndex()); }, {}});
        f.push_back({"g_lastKeyHeldTick", dg::kLastKeyHeldTick, 4, [](GgGame& g, uint8_t* p) { put32(p, g.input().lastHeldTick()); },
                     [](GgGame& g, const uint8_t* p) { g.input().setLastHeldTick(get32(p)); }});
        f.push_back({"g_idleReported", dg::kIdleReported, 2, [](GgGame& g, uint8_t* p) { put16(p, g.input().idleReported); },
                     [](GgGame& g, const uint8_t* p) { g.input().idleReported = get16(p); }});
        f.push_back({"g_keyHeldSeen", dg::kKeyHeldSeen, 2, [](GgGame& g, uint8_t* p) { put16(p, g.input().keyHeldSeen); },
                     [](GgGame& g, const uint8_t* p) { g.input().keyHeldSeen = get16(p); }});
        f.push_back({"g_menuRequest", dg::kMenuRequest, 2, [](GgGame& g, uint8_t* p) { put16(p, g.input().menuRequest); },
                     [](GgGame& g, const uint8_t* p) { g.input().menuRequest = get16(p); }});
        f.push_back({"g_menuDisabled", dg::kMenuDisabled, 2, [](GgGame& g, uint8_t* p) { put16(p, g.input().menuDisabled); },
                     [](GgGame& g, const uint8_t* p) { g.input().menuDisabled = get16(p); }});
        f.push_back({"g_mouseButtons", dg::kMouseButtons, 2, [](GgGame& g, uint8_t* p) { put16(p, g.mouse().buttons()); }, {}});
        f.push_back({"g_mouseY", dg::kMouseY, 2, [](GgGame& g, uint8_t* p) { put16(p, static_cast<uint16_t>(g.mouse().y())); }, {}});
        f.push_back({"g_mouseX", dg::kMouseX, 2, [](GgGame& g, uint8_t* p) { put16(p, static_cast<uint16_t>(g.mouse().x())); },
                     [](GgGame& g, const uint8_t* p) {
                         // g_mouseY DS:7a86 lies right before g_mouseX DS:7a88.
                         g.mouse().setPosition(static_cast<int16_t>(get16(p)), static_cast<int16_t>(get16(p - 2)));
                     }});
        f.push_back({"g_mouseHideState", dg::kMouseHideState, 2,
                     [](GgGame& g, uint8_t* p) { put16(p, static_cast<uint16_t>(g.video.screen().mouseHideState())); }, {}});
        f.push_back({"g_drawColor", dg::kDrawColor, 1, [](GgGame& g, uint8_t* p) { p[0] = g.video.screen().drawColor(); },
                     [](GgGame& g, const uint8_t* p) { g.video.screen().setDrawColor(p[0]); }});
        f.push_back({"g_cursorHidden", dg::kCursorHidden, 2, [](GgGame& g, uint8_t* p) { put16(p, g.video.cursor().hidden() ? 1 : 0); },
                     [](GgGame& g, const uint8_t* p) {
                         if (get16(p)) g.video.cursor().hide();
                         else g.video.cursor().show();
                     }});
        f.push_back({"g_palFaded", dg::kPalFaded, 2, [](GgGame& g, uint8_t* p) { put16(p, g.video.palette().faded() ? 1 : 0); },
                     [](GgGame& g, const uint8_t* p) { g.video.palette().setFaded(get16(p) != 0); }});
        f.push_back({"g_palShadow", dg::kPalShadow, static_cast<uint16_t>(kPaletteBytes),
                     [](GgGame& g, uint8_t* p) { std::copy_n(g.video.palette().shadow().begin(), kPaletteBytes, p); },
                     [](GgGame& g, const uint8_t* p) { std::copy_n(p, kPaletteBytes, g.video.palette().shadow().begin()); }});
        f.push_back({"g_palSaved", dg::kPalSaved, static_cast<uint16_t>(kPaletteBytes),
                     [](GgGame& g, uint8_t* p) { std::copy_n(g.video.palette().saved().begin(), kPaletteBytes, p); },
                     [](GgGame& g, const uint8_t* p) {
                         // Pal_Save copies the shadow: put the saved colours there for a moment.
                         Palette& pal = g.video.palette();
                         const Rgb6 shadow = pal.shadow();
                         std::copy_n(p, kPaletteBytes, pal.shadow().begin());
                         pal.save();
                         pal.shadow() = shadow;
                     }});
        f.push_back({"g_randState", dg::kRandState, 4, [](GgGame& g, uint8_t* p) { put32(p, g.rand.seed()); },
                     [](GgGame& g, const uint8_t* p) { g.rand.setRawSeed(get32(p)); }});
        // The picture cache's slot table (PicCache::slots, the world's mirror of 15df's records).
        f.push_back({"g_picCache", dg::kPicCache, static_cast<uint16_t>(8 * PicCache::kSlots),
                     [](GgGame& g, uint8_t* p) {
                         for (const PicCache::Slot& sl : g.cache.slots()) {
                             put16(p, static_cast<uint16_t>(sl.id));
                             put16(p + 2, static_cast<uint16_t>(sl.uses));
                             p += 8;  // the far data pointer keeps the base image's bytes
                         }
                     },
                     [](GgGame& g, const uint8_t* p) {
                         std::array<PicCache::Slot, PicCache::kSlots> slots{};
                         for (PicCache::Slot& sl : slots) {
                             sl.id = static_cast<int16_t>(get16(p));
                             sl.uses = static_cast<int16_t>(get16(p + 2));
                             p += 8;
                         }
                         g.cache.setSlots(slots);
                     },
                     false, 8, kPicCacheMask});
        return f;
    }();
    return fields;
}

}  // namespace

GgGame::GgGame(const GgFiles& f, GgRuntime& runtimeIn, SoundFacade& snd, PlayersStore& store, BorlandRand& rng, GgGameOptions o)
    : GgState(initialState(f.exe())),
      files(f),
      options(o),
      tables(GgTables::load(f.exe())),
      strings(DialogStrings::fromExe(f.exe())),
      rt(runtimeIn),
      sound(snd),
      rand(rng),
      players(store),
      display(runtimeIn, video),
      fonts(Fonts::load(f.archive())),
      cache(f.archive(), &video.palette(), [this](PaletteLoad l) { display.runLoad(std::move(l)); }),
      flow_(std::make_unique<FlowObjects>()),
      ui_(std::make_unique<UiObjects>()),
      world_(std::make_unique<WorldObjects>()),
      actors_(std::make_unique<ActorsObjects>()),
      puzzles_(std::make_unique<PuzzlesObjects>()),
      vehicles_(std::make_unique<VehiclesObjects>()) {
    // Init_CommandLine 303a:005c: the CPU class, the switches, the seed (srand is the caller's).
    this->runtime.cpuType = o.cpu == GgCpu::Cpu286 ? int16_t{2} : int16_t{3};
    this->runtime.soundDevice = o.soundDevice;
    this->runtime.displayCode = o.displayCode;
    this->runtime.randSeed = o.seed;
    hud.bind(world.hud);
    // 1473:0074 Gfx_Init (main, after Init_CommandLine), then Game_Init's Hud_Init (1a04:025b).
    if (o.initDisplay) display.init(f.exe(), f.archive());
    if (o.initHud) hud.init(f.exe(), f.archive(), cache);
    // Input_GetCode's Ctrl+S: 1cdd:05c8 Snd_ToggleOption (DS:1ad4 ^= 1 in the bank layer, DS:45cc ^= 1).
    input().hooks().toggleSound = [this] {
        sound.toggleSound();
        player.soundOff = static_cast<int16_t>(player.soundOff ^ 1);
    };
}

GgGame::~GgGame() { input().hooks().toggleSound = {}; }

const std::vector<DgroupField>& serviceFields() {
    static const std::vector<DgroupField> fields = [] {
        std::vector<DgroupField> out;
        for (const ServiceField& s : services()) {
            DgroupField f;
            f.name = s.name;
            f.ds = s.ds;
            f.size = s.size;
            f.stride = s.stride;
            f.mask = s.mask;
            f.service = true;
            out.push_back(f);
        }
        return out;
    }();
    return fields;
}

std::vector<uint8_t> dumpToDgroup(const GgGame& g, std::span<const uint8_t> base) {
    const std::vector<uint8_t> initial = base.empty() ? initialDgroupImage(g.exe()) : std::vector<uint8_t>{};
    std::vector<uint8_t> out = dumpToDgroup(static_cast<const GgState&>(g), base.empty() ? std::span<const uint8_t>(initial) : base);
    auto& game = const_cast<GgGame&>(g);  // the services' getters are not const; nothing is changed
    for (const ServiceField& s : services())
        if (static_cast<size_t>(s.ds) + s.size <= out.size()) s.store(game, out.data() + s.ds);
    return out;
}

void loadFromDgroup(GgGame& g, std::span<const uint8_t> dgroup, bool withTime) {
    loadFromDgroup(static_cast<GgState&>(g), dgroup);
    for (const ServiceField& s : services()) {
        if (!s.load || (s.time && !withTime)) continue;
        if (static_cast<size_t>(s.ds) + s.size <= dgroup.size()) s.load(g, dgroup.data() + s.ds);
    }
}

}  // namespace tmport::gg
