#pragma once
// Scenario helper for the tests of the DOS game modules (re/dos/docs/ENGINE_MAP.md §6): a DosGame
// on the user's TM_DATA_DOS folder with a recording sound facade, a users store in a temporary file
// that is only written at the original's save moments, and the headless DOS runtime. A test sets a
// scene up (a reference USERS.DAT record, the player and camera, a --keys style script) and steps
// game ticks, recording per tick what the r2 frame logs record (framelog.py STEP: x, y, camera,
// player state, keys, coins), so engine runs can be compared with the DOSBox-X logs tick for tick.
//
//   TM_REQUIRE_DOS_GAME_FILES();          // skips without TM_DATA_DOS; defines `gameFiles`
//   dosgame::GameFixture f(*gameFiles);
//   f.loadUsers(capturesDir / "savestates/r2/fl_walk2.users");
//   f.restoreRecord(1);                   // Users_LoadPlayer + Game_Restore (the ui agent's code)
//   f.setScript("55:down:right, 103:up:right");
//   auto log = f.step(120);
//
// With stub modules the steps run but change nothing; the helpers that only set state work anyway.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dos/app/dos_runtime.h"
#include "dos/game/game.h"
#include "dos/game/sound.h"
#include "dos/gfx/dos_screens.h"
#include "dos/save/users_store.h"
#include "edition/dos_options.h"

namespace testdata {
// The decoded DOS folder of TM_DATA_DOS (SST.EXE, data files, PANs, PAKs, font), shared by all
// tests; nullptr when TM_DATA_DOS is unset.
const tmport::dos::DosGfxFiles* dosGfxFiles();
// extracted/captures/dos (reference states, USERS.DAT snapshots) next to TM_DATA_DOS's parent if
// it exists (C:\Git\TM\extracted\captures\dos for TM_DATA_DOS = C:\Git\TM\TM-DOS).
std::optional<std::filesystem::path> dosCapturesPath();
}  // namespace testdata

// Skips the rest of a test case when TM_DATA_DOS is not set; defines `gameFiles`.
#define TM_REQUIRE_DOS_GAME_FILES()                                    \
    const auto* gameFiles = testdata::dosGfxFiles();                   \
    if (!gameFiles) {                                                  \
        MESSAGE("TM_DATA_DOS is not set: test skipped");               \
        return;                                                        \
    }                                                                  \
    (void)gameFiles

namespace dosgame {

// One executed game tick as the r2 frame logs record it: framelog.py's STEP record (ticks, player
// x, y, camera x, y, player state, g_keys, coins) plus a few more fields. The fixture records the
// state AFTER tick n and its frame; a DOSBox-X STEP record is written at the START of a tick, so
// engine record n corresponds to log record n + 1.
struct TickRecord {
    uint64_t tick = 0;      // game tick number (1, 2, ...)
    uint32_t pitTicks = 0;  // g_ticks DS:3D2B
    uint16_t x = 0, y = 0;  // g_player DS:37E2/37E4
    uint16_t camX = 0, camY = 0;  // g_cameraX/Y DS:37EA/37EC
    uint16_t state = 0;     // g_playerState DS:913C
    uint16_t keys = 0;      // g_keys DS:9105
    uint16_t coins = 0;     // g_coins DS:9134
    uint16_t nets = 0;      // g_nets DS:910F
    uint16_t level = 0;     // g_level DS:928F
    uint8_t uiMode = 0;     // g_uiMode DS:9119
    bool gated = false;     // the tick found g_stepDone set
    bool operator==(const TickRecord&) const = default;
};

struct FixtureOptions {
    tmport::dos::DosOptions options;   // look, sound device (for g_soundDevice), slow CPU, frame speed
    bool runGameInit = true;           // call gameInit (Game_Init's game part, Game_InitVars)
    std::optional<uint16_t> layoutSeed = uint16_t{1};  // DosGame::layoutSeedOverride (deterministic layouts)
    FixtureOptions();
};

class GameFixture : public tmport::dos::RuntimeClient {
public:
    explicit GameFixture(const tmport::dos::DosGfxFiles& files, FixtureOptions o = {});
    ~GameFixture() override;
    GameFixture(const GameFixture&) = delete;
    GameFixture& operator=(const GameFixture&) = delete;

    tmport::dos::DosGame& g() { return *game_; }
    tmport::dos::DosRuntime& runtime() { return runtime_; }
    tmport::dos::RecordingSound& sound() { return sound_; }
    tmport::dos::DosUsersStore& users() { return *users_; }

    // ---- scenario set-up
    // A USERS.DAT file (e.g. a savestates/*.users snapshot or a USERS_*.DAT) into the store's image
    // (nothing is written). False if the file is missing or not a whole USERS.DAT.
    bool loadUsers(const std::filesystem::path& usersDat);
    // g_userRec = record `rec` (Users_SelectRecord's effect).
    void selectRecord(int rec);
    // selectRecord, then Users_LoadPlayer and Game_Restore (the ui agent's functions; stubs do
    // nothing yet), g_uiMode = 0, g_screen = 0.
    void restoreRecord(int rec);
    // Puts the player and the camera somewhere directly: g_player x/y (dx = dy = 0), g_cameraX/Y,
    // g_level and the loaded/wanted scene (no load: g.pan / g.bank are set to the level's files),
    // g_playerState, g_uiMode = 0, g_curObj = the player.
    void placePlayer(uint16_t x, uint16_t y, uint16_t camX, uint16_t camY, uint16_t level, uint16_t state = 10);
    // A --keys script in game ticks (dos/app/script_input.h), relative to the current tick when
    // `relative` (each item's tick += ticks run so far). False (and `error`) when it does not parse.
    bool setScript(std::string_view keys, bool relative = true, std::string* error = nullptr);

    // ---- running
    // n game ticks (each with its frame in lockstep), one record per tick.
    std::vector<TickRecord> step(int n);
    // The current values as a record (tick = ticks run so far).
    TickRecord snapshot() const;
    // stepchanges.py's view: one line per record whose x, y, camera, state, keys or coins changed.
    static std::string changes(const std::vector<TickRecord>& log);

    // RuntimeClient: forwards to the game and notes the gate of each tick.
    void soundTick() override { game_->soundTick(); }
    void frameBegin() override { game_->frameBegin(); }
    void gameTick(const tmport::dos::TickInfo& info) override;
    void frameBody() override { game_->frameBody(); }
    uint8_t uiMode() const override { return game_->uiMode(); }
    bool quitRequested() const override { return game_->quitRequested(); }

private:
    tmport::dos::DosRuntime runtime_;
    tmport::dos::RecordingSound sound_;
    std::filesystem::path usersPath_;
    std::unique_ptr<tmport::dos::DosUsersStore> users_;
    std::unique_ptr<tmport::dos::DosGame> game_;
    bool lastGated_ = false;
};

}  // namespace dosgame
