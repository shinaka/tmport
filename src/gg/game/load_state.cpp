// Starting from a DOSBox-X save state (load_state.h). Owner: flow agent.
#include "gg/game/load_state.h"

#include <format>
#include <optional>
#include <system_error>

#include "gg/data/dgroup_offsets.h"
#include "gg/game/actors.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/game/puzzles.h"
#include "gg/game/race.h"
#include "gg/game/workshop.h"
#include "gg/game/world.h"
#include "gg/save/gg_state_file.h"
#include "gg/save/players_store.h"
#include "gg/ui/signin.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

// Which modules rebuild their part for a state function's resume point: required (a failure refuses the
// moment), optional (a failure is a warning), or not called.
enum class Use : uint8_t { No, Optional, Required };
struct Restores {
    Use ui = Use::No, world = Use::No, actors = Use::No, vehicles = Use::No, puzzles = Use::No, flow = Use::No;
};

// The UI's part (the roster copy, the menu bar's records) is optional everywhere: without it the game
// goes on with the store's roster and without the menu bar's state until a state function sets it.
std::optional<Restores> restoresFor(int16_t state) {
    Restores r;
    r.ui = Use::Optional;
    switch (state) {
        case 3:  // the hub: flow's screen
            r.flow = Use::Required;
            return r;
        case 6:   // the workshop
        case 11:  // a race
            r.world = r.actors = r.vehicles = Use::Required;
            return r;
        case 7:  // a warehouse room
            r.world = r.actors = Use::Required;
            r.vehicles = Use::Optional;
            return r;
        case 9:  // a puzzle
            r.world = r.actors = r.puzzles = Use::Required;
            r.vehicles = Use::Optional;
            return r;
        default: return std::nullopt;
    }
}

// The frame's sprite list DS:05f4 / DS:05f6 (14-byte records {u16 flags, x, y, w, h, far picture}): the
// rectangles the next Sprites_EraseAll restores from the back page (the pictures are not needed, so the
// records are marked "do not draw").
void restoreSpriteList(GgGame& g, const GgStateFile& s) {
    SpriteList& list = g.video.sprites();
    list.reset();
    const int count = static_cast<int16_t>(s.dsWord(dg::kSpriteCount));
    for (int i = 0; i < count && i < static_cast<int>(dg::kSpritesCount); ++i) {
        const auto o = static_cast<uint16_t>(dg::kSprites + dg::kSpritesStride * static_cast<size_t>(i));
        Sprite sp;
        sp.flags = static_cast<uint16_t>(s.dsWord(o) | SpriteList::kNoDraw);
        sp.x = static_cast<int16_t>(s.dsWord(o + 2));
        sp.y = static_cast<int16_t>(s.dsWord(o + 4));
        sp.w = static_cast<int16_t>(s.dsWord(o + 6));
        sp.h = static_cast<int16_t>(s.dsWord(o + 8));
        list.add(sp);
    }
}

}  // namespace

bool isResumableState(int16_t state) { return restoresFor(state).has_value(); }

GgLoadedState loadGgState(GgGame& g, const GgStateFile& s) {
    GgLoadedState r;
    r.warnings = s.apply(g, true);
    r.ticks = g.ticks();
    r.state = g.flow.state;
    if (const auto ret = s.stackReturns(); !ret.empty()) r.cpu = std::format("{:04x}:{:04x}", ret.front().first, ret.front().second);

    // The state's PLAYERS.DAT copy into the store (the record of the player in g_player is the state's
    // own, from DGROUP), and the roster copy as Roster_LoadOrCreate read it (the UI's restore may refine
    // it from the state's heap copy).
    std::error_code ec;
    if (const auto players = GgStateFile::playersFileOf(s.path()); std::filesystem::is_regular_file(players, ec)) {
        if (g.players.path().empty()) {
            r.warnings.push_back("no preference folder: the state's PLAYERS.DAT copy is not used");
        } else if (const PlayersImport imp = importPlayers(players, g.players.path(), true); imp != PlayersImport::Imported) {
            r.warnings.push_back(std::format("the state's PLAYERS.DAT copy: {}", describe(imp)));
        } else {
            g.players.load();
            g.ui.roster = g.players.roster();
        }
    }
    restoreSpriteList(g, s);

    const auto need = restoresFor(r.state);
    if (!need) {
        r.why = std::format("state {} has no resume point (the hub 3, the workshop 6, a room 7, a puzzle 9, a race 11 have)", r.state);
        return r;
    }
    bool ok = true;
    const auto run = [&](Use use, const char* module, bool (*fn)(GgGame&, const GgStateFile&, std::string&)) {
        if (use == Use::No) return;
        std::string why;
        if (fn(g, s, why)) return;
        if (use == Use::Required) {
            ok = false;
            r.why += std::format("{}{}: {}", r.why.empty() ? "" : "; ", module, why.empty() ? "cannot resume" : why);
        } else {
            r.warnings.push_back(std::format("{}: {}", module, why));
        }
    };
    // The roster and menu bar first, then the rooms and parts, the actors (their sets and the rooms'
    // pictures), the workshop's or the race's objects, a puzzle, the hub.
    run(need->ui, "ui", &uiRestoreFromState);
    run(need->world, "world", &worldRestoreFromState);
    run(need->actors, "actors", &actorsRestoreFromState);
    run(need->vehicles, "vehicles", &vehiclesRestoreFromState);
    run(need->puzzles, "puzzles", &puzzlesRestoreFromState);
    run(need->flow, "flow", &flowRestoreFromState);
    // The picture cache's 300 records DS:6640 last: the modules' restores load their pictures again
    // (the actors' bundles, the rooms' layers), which counts uses the state's records already hold; and
    // those loads applied their palette members: the state's palette and DAC again.
    picCacheRestoreFromState(g, s);
    s.applyPalette(g);
    r.supported = ok;
    return r;
}

int16_t runFromLoadedState(GgGame& g, const GgLoadedState& loaded) {
    if (!loaded.supported) throw Error(std::format("g_state {} cannot be resumed: {}", loaded.state, loaded.why));
    const int16_t from = g.flow.state;
    int16_t r = 0;
    switch (from) {
        case 3: r = state03Hub(g, StateEntry::Resume); break;
        case 6: r = state06Workshop(g, StateEntry::Resume); break;
        case 7: r = state07Warehouse(g, StateEntry::Resume); break;
        case 9: r = state09Puzzle(g, StateEntry::Resume); break;
        case 11: r = state11Race(g, StateEntry::Resume); break;
        default: throw Error(std::format("g_state {} has no resume point", from));
    }
    // Seq_Run's rule for the resumed state's result, then Seq_Run on with that state as its history.
    if (r == -1) return -1;
    if (r == -2) {
        log::error("--gg-load-state: state {} returned -2 (back), but the state before it is not in the save state", from);
        return 0;
    }
    g.flow.state = r == 0 ? static_cast<int16_t>(from + 1) : r;
    const auto table = mainStateTable(g);
    return seqRun(table, g.flow.state, from);
}

}  // namespace tmport::gg
