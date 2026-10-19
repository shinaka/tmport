#include "dos_game_fixture.h"

#include <atomic>
#include <chrono>
#include <format>
#include <system_error>

#include "dos/app/script_input.h"
#include "dos/game/init.h"
#include "dos/game/signin.h"
#include "dos/save/users_dat.h"
#include "dos_test_data.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::dos;

namespace testdata {

const DosGfxFiles* dosGfxFiles() {
    static std::unique_ptr<DosGfxFiles> files;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (auto p = dosDataPath()) files = std::make_unique<DosGfxFiles>(DosGfxFiles::open(*p));
    }
    return files.get();
}

// dosCapturesPath(): dos_test_data.cpp (TM_DOS_CAPTURES).

}  // namespace testdata

namespace dosgame {

namespace {

std::filesystem::path uniqueUsersPath() {
    static std::atomic<uint32_t> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::error_code ec;
    auto dir = std::filesystem::temp_directory_path(ec);
    if (ec) dir = std::filesystem::current_path();
    return dir / std::format("tmport_dosgame_{:x}_{}.dat", static_cast<uint64_t>(now), counter.fetch_add(1));
}

RuntimeOptions runtimeOptionsFor(const DosOptions& o) {
    RuntimeOptions ro;
    ro.frameSpeed = o.frameSpeed;
    return ro;
}

// PAN / PAK stems of scenes 0..4 (Scene_Update 1437:0034: PAN = scene, PAK = SPRITES or scene + 13).
std::string_view panOfScene(uint16_t scene) {
    static constexpr std::string_view kPans[] = {"MOUNTAIN", "MOUNT2N3", "MOUNT3", "CLOSE", "SHACK"};
    return scene < 5 ? kPans[scene] : kPans[0];
}
std::string_view pakOfScene(uint16_t scene) { return scene == 3 ? "CLOSE" : scene == 4 ? "SHACK" : "SPRITES"; }

}  // namespace

FixtureOptions::FixtureOptions() {
    options.look = Look::Ega;
    options.sound = SoundDevice::Off;
    options.frameSpeed = FrameSpeed::Reference;
    options.headless = true;
}

GameFixture::GameFixture(const DosGfxFiles& files, FixtureOptions o)
    : runtime_(runtimeOptionsFor(o.options)), usersPath_(uniqueUsersPath()) {
    users_ = std::make_unique<DosUsersStore>(usersPath_);
    game_ = std::make_unique<DosGame>(files, runtime_, sound_, *users_, o.options);
    game_->layoutSeedOverride = o.layoutSeed;
    runtime_.setClient(this);
    if (o.runGameInit) gameInit(*game_);
}

GameFixture::~GameFixture() {
    runtime_.setClient(nullptr);
    game_.reset();
    std::error_code ec;
    for (const char* ext : {"", ".bak", ".tmp"}) {
        auto p = usersPath_;
        p += ext;
        std::filesystem::remove(p, ec);
    }
}

bool GameFixture::loadUsers(const std::filesystem::path& usersDat) {
    const auto bytes = readWholeFile(usersDat);
    if (!bytes) return false;
    auto parsed = UsersDat::parse(*bytes);
    if (!parsed) return false;
    users_->users() = *parsed;
    return true;
}

void GameFixture::selectRecord(int rec) { game_->ui.userRec = static_cast<int16_t>(rec); }

void GameFixture::restoreRecord(int rec) {
    selectRecord(rec);
    usersLoadPlayer(*game_);
    gameRestore(*game_);
    game_->core.uiMode = 0;
    game_->ui.screen = 0;
}

void GameFixture::placePlayer(uint16_t x, uint16_t y, uint16_t camX, uint16_t camY, uint16_t level, uint16_t state) {
    DosGame& g = *game_;
    g.player.obj = Obj{x, y, 0, 0};
    g.world.cameraX = camX;
    g.world.cameraY = camY;
    g.core.level = level;
    g.world.sceneWanted = level;
    g.world.loadedScene = level;
    g.player.playerState = state;
    g.core.uiMode = 0;
    g.world.curObj = DsPtr{kPlayerObjOff};
    g.pan = &g.files.pan(panOfScene(level));
    g.bank = &g.files.pak(pakOfScene(level));
}

bool GameFixture::setScript(std::string_view keys, bool relative, std::string* error) {
    std::string err;
    auto script = ScriptedInput::parse(keys, err);
    if (!script) {
        if (error) *error = err;
        return false;
    }
    if (relative) {
        ScriptedInput shifted;
        const uint64_t base = runtime_.gameTicks();
        for (ScriptEvent e : script->events()) {
            e.tick += base;
            shifted.add(e);
        }
        runtime_.setScript(std::move(shifted));
    } else {
        runtime_.setScript(std::move(*script));
    }
    return true;
}

void GameFixture::gameTick(const TickInfo& info) {
    lastGated_ = info.gated;
    game_->gameTick(info);
}

TickRecord GameFixture::snapshot() const {
    const DosGame& g = *game_;
    TickRecord r;
    r.tick = runtime_.gameTicks();
    r.pitTicks = runtime_.ticks();
    r.x = g.player.obj.x;
    r.y = g.player.obj.y;
    r.camX = g.world.cameraX;
    r.camY = g.world.cameraY;
    r.state = g.player.playerState;
    r.keys = const_cast<DosRuntime&>(runtime_).keyboard().isr().keys;
    r.coins = g.core.coins;
    r.nets = g.core.nets;
    r.level = g.core.level;
    r.uiMode = g.core.uiMode;
    r.gated = lastGated_;
    return r;
}

std::vector<TickRecord> GameFixture::step(int n) {
    std::vector<TickRecord> log;
    log.reserve(static_cast<size_t>(n > 0 ? n : 0));
    for (int i = 0; i < n && runtime_.running(); ++i) {
        runtime_.stepGameTicks(1);
        log.push_back(snapshot());
    }
    return log;
}

std::string GameFixture::changes(const std::vector<TickRecord>& log) {
    std::string out;
    const TickRecord* prev = nullptr;
    for (const TickRecord& r : log) {
        if (!prev) {
            out += std::format("t={:5} x={:#x} y={:#x} camx={:#x} camy={:#x} state={:#x} keys={:#x} coins={:#x}\n", r.tick,
                               r.x, r.y, r.camX, r.camY, r.state, r.keys, r.coins);
        } else {
            std::string ch;
            auto f = [&](const char* name, unsigned a, unsigned b) {
                if (a != b) ch += std::format(" {}:{:#x}->{:#x}", name, a, b);
            };
            f("x", prev->x, r.x);
            f("y", prev->y, r.y);
            f("camx", prev->camX, r.camX);
            f("camy", prev->camY, r.camY);
            f("state", prev->state, r.state);
            f("keys", prev->keys, r.keys);
            f("coins", prev->coins, r.coins);
            if (!ch.empty()) out += std::format("t={:5}{}\n", r.tick, ch);
        }
        prev = &r;
    }
    return out;
}

}  // namespace dosgame
