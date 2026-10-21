#pragma once
// A test harness for the player modules of the DOS edition (player, anim, search, endgame,
// rank_chest): a DosGame on TM_DATA_DOS whose game tick runs only what those modules need, in the
// original's order (Game_Tick 130f:00fe: screen offsets, Anim_Tick, the keyboard part of
// Input_MouseWalk, Player_Update; gated ticks only Anim_Tick), with Search_Update and RankChest_Update
// in the frame body and a stand-in for the world's cell handlers (setTerrainMaskHookForTests). It
// records, at the START of every engine tick, what the frame logger of the reference recordings
// records (framelog.py STEP: x, y, camera, player state, g_keys, coins), so engine record e lines
// up with log record (e - 1 + t0) for a run that starts at log record t0.
//
// Scaffolding that stands in for other modules (the parts the player modules depend on):
//   * Input_MouseWalk's keyboard part (main_loop; code_runtime.md §6.6): Down becomes the coin key
//     where the terrain allows no vertical move, while g_menuLock is 0;
//   * Frame_Begin's latches (camera.h): g_sceneWanted, g_rankState, then Sprites_Snapshot;
//   * Frame_Present's transition (main_loop/camera): g_transition is cleared at the end of the
//     `dissolveFrames`-th frame after the one that latched it (the dissolve, ENGINE_MAP §3.4), counted
//     from the tick after the teleporting one: the frame that latches it runs right after that tick
//     (wave 5), one more frame then;
//   * the cell handlers: a few cell types (paths, the club door, the trunk ladder, the chute), and the
//     player module's own 176a handlers through the dispatch.

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "dos/app/dos_runtime.h"
#include "dos/game/anim.h"
#include "dos/game/endgame.h"
#include "dos/game/game.h"
#include "dos/game/init.h"
#include "dos/game/player.h"
#include "dos/game/rank_chest.h"
#include "dos/game/search.h"
#include "dos/game/sound.h"
#include "dos/input/keyboard.h"
#include "dos/save/users_store.h"
#include "dos_game_fixture.h"
#include "mischief/dos/app/script_input.h"

namespace dosplayer {

using namespace tmport;
using namespace tmport::dos;

// One record of a reference log: the values at the start of log tick t (0xFFFF keys: not compared).
struct LogPoint {
    uint16_t t, x, y, camX, camY, state, keys, coins;
};

// What the harness records at the start of each engine tick.
struct Rec {
    uint16_t x = 0, y = 0, camX = 0, camY = 0, state = 0, keys = 0, coins = 0;
};

inline std::string describe(const Rec& r) {
    return std::format("x={:#x} y={:#x} cam=({:#x},{:#x}) state={} keys={:#x} coins={}", r.x, r.y, r.camX, r.camY, r.state,
                       r.keys, r.coins);
}

// The cell types the stand-in terrain knows (code_world.md §6); everything else is a plain path.
inline uint16_t standInTerrain(DosGame& g) {
    WorldState& w = g.world;
    w.cellCallKind = 2;  // an object call (Cell_GetMask 1568:1fe4)
    Obj& o = objAt(g, w.curObj);
    w.curCell = static_cast<uint16_t>(cellIndexOf(o.x, o.y));
    const auto path = [&o](uint16_t mask) -> uint16_t {  // PATH(o): on the path line no Down
        if ((o.y & 0x7F) == 0x70) {
            if (o.dy > 0) o.dy = 0;
            mask &= 0xFFFD;
        }
        return mask;
    };
    switch (cellTypeRaw(g, w.curCell)) {
        case 11: w.cellMask = path(0xFF); break;             // Cell_P11_ClubDoor on the path line
        case 20: return cellP20CastleGate(g);
        case 23: w.cellMask = 0x1400; break;                 // Cell_P23_FallShaft (not on level 2)
        case 25:                                             // Cell_P25_TrunkShaft
        case 46: w.cellMask = 0x5F3; break;                  // Cell_P46_V46_Boundary01 (no boundary here)
        case 31: w.cellMask = (o.y & 0x7F) < 0x68 ? 0x5FF : path(0x4FF); break;  // Cell_P31_GateL0 with the key
        case 64: return cellP64EndScreen(g);
        case 70: return cellP70TreasureRoom(g);
        case 71: return cellP71V71ToRankScreen(g);
        case 72: return cellP72RankScreen(g);
        default:                                             // Cell_P28_Path
            if (o.dy > 0) o.dy = 0;
            w.cellMask = 0xFC;
            break;
    }
    return w.cellMask;
}

class Harness : public RuntimeClient {
public:
    explicit Harness(const DosGfxFiles& files, RuntimeOptions ro = {}) : runtime_(ro), usersPath_(uniquePath()) {
        users_ = std::make_unique<DosUsersStore>(usersPath_);
        DosOptions o;
        o.look = Look::Ega;
        o.sound = SoundDevice::Off;
        o.frameSpeed = FrameSpeed::Reference;
        o.headless = true;
        game_ = std::make_unique<DosGame>(files, runtime_, sound_, *users_, o);
        runtime_.setClient(this);
        DosGame& g = *game_;
        gameInit(g);
        g.core.uiMode = 0;
        g.ui.userRec = 0;
        g.rec().setName("tester");
        g.rec().collected.fill(0x80);
        g.bank = &files.pak("SPRITES");
        setScene(0);
        g.world.curObj = DsPtr{kPlayerObjOff};
        animInitSlots(g);
        setTerrainMaskHookForTests(standInTerrain);
    }
    ~Harness() override {
        setTerrainMaskHookForTests({});
        runtime_.setClient(nullptr);
        game_.reset();
        std::error_code ec;
        for (const char* ext : {"", ".bak", ".tmp"}) {
            auto p = usersPath_;
            p += ext;
            std::filesystem::remove(p, ec);
        }
    }
    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;

    DosGame& g() { return *game_; }
    DosRuntime& runtime() { return runtime_; }
    RecordingSound& sound() { return sound_; }
    const std::vector<Rec>& recs() const { return recs_; }
    // The record at the start of engine tick e (1, 2, ...).
    const Rec& at(uint64_t e) const { return recs_.at(static_cast<size_t>(e - 1)); }
    uint64_t ticks() const { return runtime_.gameTicks(); }

    // Frames a transition stays pending after the frame that latched it (the dissolve).
    int dissolveFrames = 6;
    // Run the keyboard part of Input_MouseWalk before Player_Update.
    bool mouseWalk = true;

    void setScene(uint16_t level) {
        DosGame& g = *game_;
        g.core.level = level;
        g.world.loadedScene = level;
        g.world.sceneWanted = level;
    }
    // The player at log record values (dx = dy = 0), the state, slot 1 idle with its last script.
    void place(const LogPoint& p, uint16_t prevScript) {
        DosGame& g = *game_;
        g.player.obj = Obj{p.x, p.y, 0, 0};
        g.world.cameraX = p.camX;
        g.world.cameraY = p.camY;
        g.player.playerState = p.state;
        g.core.coins = p.coins;
        g.keys() = p.keys == 0xFFFF ? 0 : p.keys;
        g.player.playerMask = static_cast<uint16_t>(0x00FF | (g.keys() & 0xFF00));  // the high byte the log shows
        SpriteSlot& s1 = g.player.slots[1];
        s1.anim = 0;
        s1.prev = prevScript;
        s1.delay = 0;
        s1.pos = 0;
    }
    // A key event at a time `usAfter` microseconds after the start of engine tick e - 1 (seen at tick
    // e). The keyboard takes events in time order: call it once the run has reached tick e - 1.
    void keyAt(std::string_view name, bool down, uint64_t e, uint64_t usAfter = 0) {
        REQUIRE(ticks() + 1 == e);
        const auto k = mischief::dos::pcKeyByName(name);
        REQUIRE(k.has_value());
        const uint64_t t = PitClock::timeOfGameTick(e - 1) + usAfter * 1000;
        if (down) runtime_.keyboard().keyDown(*k, t);
        else runtime_.keyboard().keyUp(*k, t);
    }
    // A --keys script in engine ticks (mischief/dos/app/script_input.h): events of tick e are applied right
    // before engine tick e, at its exact time.
    void script(std::string_view text) {
        std::string err;
        auto s = mischief::dos::ScriptedInput::parse(text, err);
        REQUIRE_MESSAGE(s.has_value(), err);
        runtime_.setScript(std::move(*s));
    }
    void step(uint64_t n) { runtime_.stepGameTicks(n); }
    // Steps until `pred` holds at the start of a tick (at most `limit` ticks); returns that tick or 0.
    template <class Pred>
    uint64_t stepUntil(Pred pred, uint64_t limit) {
        for (uint64_t i = 0; i < limit; ++i) {
            step(1);
            if (pred(recs_.back())) return ticks();
        }
        return 0;
    }

    // ---- RuntimeClient
    void frameBegin() override {
        DosGame& g = *game_;
        g.world.sceneWanted = g.core.level;  // Frame_Begin 130f:025a latches
        g.world.frameRankState = g.player.rankState;
        spritesSnapshot(g, 0, kSpriteSlots);
        // The frame after the teleporting tick latches it; one set before that tick (a scene set up
        // with it) counts from this frame.
        if (g.world.transition != 0 && dissolveLeft_ == 0) dissolveLeft_ = dissolveFrames + (transitionBeforeTick_ == 0 ? 1 : 0);
    }
    void gameTick(const TickInfo& info) override {
        DosGame& g = *game_;
        transitionBeforeTick_ = g.world.transition;
        Rec r;
        r.x = g.player.obj.x;
        r.y = g.player.obj.y;
        r.camX = g.world.cameraX;
        r.camY = g.world.cameraY;
        r.state = g.player.playerState;
        r.keys = g.keys();
        r.coins = g.core.coins;
        recs_.push_back(r);
        if (info.gated) {
            animTick(g);
            return;
        }
        // Game_Tick 130f:00fe: the player's screen offsets (bytes, rows), corrected for a row wrap.
        PlayerState& p = g.player;
        p.playerScrX = static_cast<int16_t>(static_cast<int16_t>(p.obj.x - g.world.cameraX) >> 1);
        p.playerScrY = static_cast<int16_t>(p.obj.y - g.world.cameraY);
        if (static_cast<uint16_t>(p.playerScrX) > 0x400) {
            p.playerScrX = static_cast<int16_t>(p.playerScrX + (((p.obj.y & 0xFF80) - (g.world.cameraY & 0xFF80)) << 3));
            p.playerScrY = static_cast<int16_t>((p.obj.y & 0x7F) - (g.world.cameraY & 0x7F));
        }
        animTick(g);
        if (mouseWalk && g.core.uiMode == 0 && g.core.menuLock == 0 && (g.player.playerMask & 3) == 0) {
            uint16_t& k = g.keys();  // 4a85:0228: Down drops a coin where no vertical move is allowed
            k = (k & 2) ? static_cast<uint16_t>((k & 0x00FC) | 0x40) : static_cast<uint16_t>(k & 0x00BF);
        }
        playerUpdate(g);
    }
    void frameBody() override {
        DosGame& g = *game_;
        searchUpdate(g);
        rankChestUpdate(g);
        if (dissolveLeft_ > 0 && --dissolveLeft_ == 0) g.world.transition = 0;
    }
    uint8_t uiMode() const override { return game_->core.uiMode; }

private:
    static std::filesystem::path uniquePath() {
        static std::atomic<uint32_t> counter{0};
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::error_code ec;
        auto dir = std::filesystem::temp_directory_path(ec);
        if (ec) dir = std::filesystem::current_path();
        return dir / std::format("tmport_dosplayer_{:x}_{}.dat", static_cast<uint64_t>(now), counter.fetch_add(1));
    }

    DosRuntime runtime_;
    RecordingSound sound_;
    std::filesystem::path usersPath_;
    std::unique_ptr<DosUsersStore> users_;
    std::unique_ptr<DosGame> game_;
    std::vector<Rec> recs_;
    int dissolveLeft_ = 0;
    uint16_t transitionBeforeTick_ = 0;  // g_transition when the last game tick began
};

// The log value at record t (the last change point at or before t).
inline const LogPoint& logAt(const LogPoint* log, size_t n, uint64_t t) {
    const LogPoint* best = &log[0];
    for (size_t i = 0; i < n && log[i].t <= t; ++i) best = &log[i];
    return *best;
}

struct CompareResult {
    int records = 0, mismatches = 0;
    std::string first;  // the first mismatch, described
};

// Compares engine records with log records: engine tick e <-> log record (e - 1 + t0), for log
// records t0..t1 (engine ticks 1 .. t1 - t0 + 1 must have run).
inline CompareResult compareWithLog(const Harness& h, const LogPoint* log, size_t n, uint64_t t0, uint64_t t1,
                                    uint64_t engineFirst = 1, bool withKeys = true) {
    CompareResult res;
    for (uint64_t t = t0; t <= t1; ++t) {
        const uint64_t e = engineFirst + (t - t0);
        if (e > h.recs().size()) break;
        const Rec& r = h.at(e);
        const LogPoint& l = logAt(log, n, t);
        ++res.records;
        const bool keysOk = !withKeys || l.keys == 0xFFFF || r.keys == l.keys;
        if (r.x != l.x || r.y != l.y || r.camX != l.camX || r.camY != l.camY || r.state != l.state || !keysOk ||
            r.coins != l.coins) {
            if (res.mismatches == 0)
                res.first = std::format("log t={} engine e={}: engine {} / log x={:#x} y={:#x} cam=({:#x},{:#x}) state={} keys={:#x} coins={}",
                                        t, e, describe(r), l.x, l.y, l.camX, l.camY, l.state, l.keys, l.coins);
            ++res.mismatches;
        }
    }
    return res;
}

}  // namespace dosplayer
