// Tests of the DOS game's main loop, start-up, frame protocol, input wrappers, sound sync, the
// per-tick trace and the DGROUP dumps (dos/game/main_loop.h, init.h, game.h, trace.h; dos/app/
// dos_entry.cpp). Data-dependent tests skip without TM_DATA_DOS. Stored facts are numbers only.
#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/bootstrap.h"
#include "app_test_util.h"
#include "dos/app/dos_preview.h"
#include "dos/audio/dos_audio_device.h"
#include "dos/game/anim.h"
#include "dos/game/game.h"
#include "dos/game/init.h"
#include "dos/game/main_loop.h"
#include "dos/game/sound.h"
#include "dos/game/load_state.h"
#include "dos/game/trace.h"
#include "dos/save/dosbox_state.h"
#include "dos_game_fixture.h"
#include "dos_test_data.h"
#include "rtl/borland_rand.h"
#include "util/file_util.h"

using namespace tmport;
using namespace tmport::dos;
namespace fs = std::filesystem;

namespace {

using Op = RecordingSound::Op;

// BIOS key words (scan << 8 | ASCII) the play context's key list knows (code_runtime.md §6.1).
constexpr uint16_t kKeyEsc = 0x011B, kKeyF1 = 0x3B00, kKeyF3 = 0x3D00, kKeyEnter = 0x1C0D;

TickInfo tickInfo(uint64_t n, bool gated = false) {
    TickInfo t;
    t.gameTick = n;
    t.ticks = static_cast<uint32_t>(5 * n);
    t.gated = gated;
    return t;
}

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        const size_t c = line.find(',', pos);
        out.push_back(line.substr(pos, c == std::string::npos ? std::string::npos : c - pos));
        if (c == std::string::npos) break;
        pos = c + 1;
    }
    return out;
}

// A runtime client that records the order of the runtime's calls and forwards them to the game.
class OrderClient final : public RuntimeClient {
public:
    explicit OrderClient(DosGame& g) : g_(g) {}
    void soundTick() override {
        log += 'S';
        g_.soundTick();
    }
    void frameBegin() override {
        log += 'B';
        g_.frameBegin();
    }
    void gameTick(const TickInfo& info) override {
        log += 'T';
        g_.gameTick(info);
    }
    void frameBody() override {
        log += 'F';
        g_.frameBody();
    }
    uint8_t uiMode() const override { return g_.uiMode(); }
    bool quitRequested() const override { return g_.quitRequested(); }
    std::string log;

private:
    DosGame& g_;
};

}  // namespace

TEST_CASE("dos mainloop: Game_Init and Game_InitVars give the start-up values") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    // Game_Init (game part): the fixture has the EGA look and no sound, which the game sees as the speaker.
    CHECK(g.core.videoType == 1);
    CHECK(g.core.machineId == 0xFC);
    CHECK(g.core.soundDevice == 0);
    CHECK(g.core.slowCpu == 0);
    CHECK(g.core.startDrive == 2);
    CHECK(g.core.mouse == -1);
    CHECK(g.back.color() == 0xFF);  // Video_Init: Gfx_SetDirtyTracking(0xFF)
    CHECK(g.bank == &gameFiles->pak("BEGIN"));
    // Game_InitVars (code_runtime.md §4.4).
    CHECK(g.core.uiMode == 0x80);
    CHECK(g.world.loadedScene == 0xFF);
    CHECK(g.input.cmd == -1);
    CHECK(g.input.queuedCmd == -1);
    CHECK(g.input.deliverNext);
    CHECK(g.ui.menuOpen == -1);
    CHECK(g.ui.menuItem == 1);
    CHECK(g.counters().cycle13 == 12);
    CHECK(g.counters().secondCountdown == 60);
    CHECK(g.counters().seconds == 0);
    CHECK(g.back.clip() == ClipWindow{0, 0x9F, 0, 199});
    CHECK(g.keys() == 0);
    CHECK(g.player.searchOpenBaseL0 == 0x32);
    CHECK(g.core.unknown928B == 6);
    CHECK(g.player.shownTreasure == -1);
    CHECK(g.player.storedShowcase == -1);
    CHECK(g.player.showcasePick == 0);
    CHECK(g.player.shackX == 0x1A);
    CHECK(g.player.shackY == 0x44);
    CHECK(g.world.slowScroll == 0);
    CHECK(g.world.slowCpuSaved == 0);
    CHECK(g.elves.hintsOn == 0);
    CHECK(g.ui.hudRedraw == 0);
    CHECK(g.world.transition == 0);
    for (int i = 0; i < kSpriteSlots; ++i) {
        CAPTURE(i);
        CHECK(g.player.slots[static_cast<size_t>(i)].frame == 0xFFFF);  // Anim_InitSlots
    }
    // The DGROUP view of the same values (what --dos-dump-state writes).
    const auto img = dumpToDgroup(g);
    REQUIRE(img.size() == kDgroupSize);
    CHECK(img[dg::kUiMode] == 0x80);
    CHECK(img[0x37AE] == 0xFF);
    CHECK((img[dg::kLoadedScene] | img[dg::kLoadedScene + 1] << 8) == 0xFF);
}

TEST_CASE("dos mainloop: the options decide what Hw_Detect and the switches gave the original") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::FixtureOptions o;
    o.options.look = Look::Tandy;
    o.options.sound = SoundDevice::Speaker;
    o.options.slowCpu = true;
    dosgame::GameFixture f(*gameFiles, o);
    DosGame& g = f.g();
    CHECK(g.core.videoType == 3);
    CHECK(g.core.machineId == 0x21);
    CHECK(g.core.soundDevice == 0);
    CHECK(g.core.slowCpu == 1);
    CHECK(g.world.slowScroll == 1);
    CHECK(g.world.slowCpuSaved == 1);

    dosgame::FixtureOptions a;
    a.options.look = Look::Mcga;
    a.options.sound = SoundDevice::AdLib;
    dosgame::GameFixture f2(*gameFiles, a);
    CHECK(f2.g().core.videoType == 5);
    CHECK(f2.g().core.soundDevice == 2);
}

TEST_CASE("dos mainloop: each game tick is sound ticks, Game_Tick, Frame_Begin, frame body") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    OrderClient order(f.g());
    f.runtime().setClient(&order);
    f.runtime().stepGameTicks(3);
    // At the reference speed the frame that does a tick's work begins right after it (wave 5).
    CHECK(order.log == "SSSSSTBFSSSSSTBFSSSSSTBF");
    // Slow-machine mode: one frame per two ticks, the second tick without a Frame_Begin.
    RuntimeOptions ro;
    ro.frameMode = FrameMode::SlowMachine;
    ro.slowMachineTicksPerFrame = 2;
    DosRuntime slow(ro);
    RecordingSound sound;
    DosUsersStore users{fs::path{}};
    DosGame g(*gameFiles, slow, sound, users, DosOptions{});
    gameInit(g);
    OrderClient o2(g);
    slow.setClient(&o2);
    slow.stepGameTicks(4);
    CHECK(o2.log == "SSSSSBTSSSSSTFSSSSSBTSSSSSTF");
    slow.setClient(nullptr);
    f.runtime().setClient(&f);
}

TEST_CASE("dos mainloop: Game_Tick computes the player's screen position, wrapped rows included") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    // A frozen world (scene not loaded, no title) so that only Mouse_Poll, Input_Tick and Anim_Tick run.
    g.core.uiMode = 0;
    g.core.level = 0;
    g.world.loadedScene = 0xFF;
    g.player.obj = Obj{0x140, 0x70, 0, 0};
    g.world.cameraX = 0xC0;
    g.world.cameraY = 0;
    gameTick(g, tickInfo(1));
    CHECK(g.player.playerScrX == 0x40);  // (0x140 - 0xC0) >> 1
    CHECK(g.player.playerScrY == 0x70);
    // Left of the camera: a signed shift.
    g.player.obj.x = 0xB0;
    gameTick(g, tickInfo(2));
    CHECK(g.player.playerScrX == -8);
    // The player's x wrapped into the next strip row (row 1) while the camera is still at the end of
    // row 0: the difference is corrected by 8 * (row difference in pixels).
    g.player.obj = Obj{0x008, 0x0F0, 0, 0};
    g.world.cameraX = 0x7C0;
    g.world.cameraY = 0x070;
    gameTick(g, tickInfo(3));
    CHECK(g.player.playerScrX == 0x24);  // 0xFC24 + 0x80 * 8
    CHECK(g.player.playerScrY == 0);     // (0xF0 & 0x7F) - (0x70 & 0x7F)
    // The gated path keeps the old values (and does not poll the mouse).
    g.player.obj.x = 0x400;
    const uint16_t mouseYBefore = g.mouse.y;
    g.rt.mouse().setLogicalPosition(10, 20);
    gameTick(g, tickInfo(4, true));
    CHECK(g.player.playerScrX == 0x24);
    CHECK(g.mouse.y == mouseYBefore);
    gameTick(g, tickInfo(5));
    CHECK(g.mouse.x == 5);  // logical pixel 10 = byte 5
    CHECK(g.mouse.y == 20);
}

TEST_CASE("dos mainloop: Input_Tick runs in both paths and opens the menu bar or the Exit box") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    g.core.uiMode = 0;
    g.world.loadedScene = 0xFF;  // frozen world
    g.input.context = 0;
    // Esc in play: the File menu with a queued Enter on item 2 (the Exit-and-Save box at once).
    REQUIRE(g.bios().push(kKeyEsc));
    gameTick(g, tickInfo(1, true));  // the gated path reads the key too
    CHECK(g.input.lastKey == kKeyEsc);
    CHECK(g.core.uiMode == 0x40);
    CHECK(g.input.cmd == 8);
    CHECK(g.ui.menuOpen == 1);
    CHECK(g.ui.menuItem == 2);
    // F3: that menu.
    g.core.uiMode = 0;
    g.input.cmd = -1;
    REQUIRE(g.bios().push(kKeyF3));
    gameTick(g, tickInfo(2));
    CHECK(g.core.uiMode == 0x40);
    CHECK(g.input.cmd == 3);
    CHECK(g.ui.menuOpen == -1);
    CHECK(g.ui.menuItem == 0);
    // Slow CPU: Esc opens the Exit-and-Save box (dialog 10) directly.
    g.core.uiMode = 0;
    g.input.cmd = -1;
    g.core.slowCpu = 1;
    REQUIRE(g.bios().push(kKeyEsc));
    gameTick(g, tickInfo(3));
    CHECK(g.core.uiMode == 0x20);
    CHECK(g.ui.dialogId == 10);
    // During a scripted move (g_menuLock) F1 is dropped.
    g.core.uiMode = 0;
    g.input.cmd = -1;
    g.core.menuLock = 1;
    REQUIRE(g.bios().push(kKeyF1));
    gameTick(g, tickInfo(4));
    CHECK(g.core.uiMode == 0);
    CHECK(g.input.cmd == -1);
    // One key per tick: two keys take two ticks.
    g.core.menuLock = 0;
    g.core.uiMode = 0x20;  // a dialog: the commands wait for it
    REQUIRE(g.bios().push(kKeyEnter));
    REQUIRE(g.bios().push(kKeyEnter));
    gameTick(g, tickInfo(5));
    CHECK(g.bios().size() == 1);
    gameTick(g, tickInfo(6));
    CHECK(g.bios().size() == 0);
}

TEST_CASE("dos mainloop: Input_MouseWalk runs only with the scene loaded and sets the prompt") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    g.core.uiMode = 0;
    g.core.level = 0;
    g.world.loadedScene = 0xFF;
    g.player.playerMask = 0;  // no Up/Down here: Down drops a coin
    g.ui.promptIcon = 0;
    g.keys() = 2;
    gameTick(g, tickInfo(1));
    CHECK(g.ui.promptIcon == 0);  // frozen: no mouse walk
    CHECK(g.keys() == 2);
    // The wrapper on its own (Game_Tick then also runs Player_Update, which owns the keys after it).
    inputMouseWalk(g);
    CHECK(g.ui.promptIcon == 4);
    CHECK(g.keys() == 0x40);
    g.player.playerMask = 1;  // Up possible: no prompt
    inputMouseWalk(g);
    CHECK(g.ui.promptIcon == 0);
    // A play-field press to the right of the player walks right (mouse in bytes, rows).
    g.player.playerScrX = 0x40;
    g.player.playerScrY = 0x50;
    g.keys() = 0;
    g.mouse.x = 0x60;
    g.mouse.y = 0x58;
    g.input.cmd = 9;
    inputMouseWalk(g);
    CHECK(g.keys() == 8);
    CHECK(g.input.cmd == -1);
    // Release: stop.
    g.input.cmd = 10;
    inputMouseWalk(g);
    CHECK(g.keys() == 0);
}

TEST_CASE("dos mainloop: Mouse_Poll moves slot 0 and hides the pointer after 600 idle ticks") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    g.core.uiMode = 0;
    CHECK_FALSE(g.mouse.visible);  // BSS: hidden until the first activity
    g.rt.mouse().setLogicalPosition(100, 60);
    mousePoll(g);
    CHECK(g.mouse.visible);
    CHECK(g.player.slots[0].x == 50);
    CHECK(g.player.slots[0].y == 60);
    CHECK(g.player.slots[0].z == 0);
    for (int i = 0; i < 599; ++i) mousePoll(g);
    CHECK(g.mouse.visible);
    CHECK(g.mouse.idleTicks == 599);
    mousePoll(g);
    CHECK_FALSE(g.mouse.visible);
    CHECK(g.player.slots[0].frame == 0xFFFF);  // Anim_Clear(0)
    // Any activity shows it again.
    g.rt.mouse().setButtons(MouseDriver::kLeft);
    mousePoll(g);
    CHECK(g.mouse.visible);
    CHECK(g.mouse.idleTicks == 0);
    // Without a driver nothing is read.
    g.rt.mouse().present = false;
    g.rt.mouse().setLogicalPosition(0, 0);
    mousePoll(g);
    CHECK(g.mouse.x == 50);
}

TEST_CASE("dos mainloop: Sound_SyncState pauses the music in UI modes and applies option changes") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    RecordingSound& s = f.sound();
    g.core.uiMode = 8;  // the first main-loop frame after the title: g_lastUiMode is still 0
    g.core.lastUiMode = 0;
    s.clear();
    soundSyncState(g);
    CHECK(g.core.lastUiMode == 8);
    CHECK(g.core.musicGate == 0);
    CHECK(s.calls() == std::vector<RecordingSound::Call>{{Op::MusicGate, 0}, {Op::SfxStop, 0}, {Op::SilenceAll, 0}});
    s.clear();
    soundSyncState(g);  // no change: nothing
    CHECK(s.calls().empty());
    g.core.uiMode = 0;
    soundSyncState(g);
    CHECK(g.core.musicGate == 0xFFFF);
    CHECK(s.calls() == std::vector<RecordingSound::Call>{{Op::MusicGate, 0xFFFF}});
    // An option change: the area's tune, or everything off.
    s.clear();
    g.core.area = 5;
    g.core.musicOn = 1;
    g.ui.soundChanged = 1;
    soundSyncState(g);
    CHECK(g.ui.soundChanged == 0);
    CHECK(s.calls() == std::vector<RecordingSound::Call>{{Op::MusicPlay, 1}});
    s.clear();
    g.core.area = 15;  // no tune from area 15 on
    g.ui.soundChanged = 1;
    soundSyncState(g);
    CHECK(s.calls().empty());
    g.core.musicOn = 0;
    g.ui.soundChanged = 1;
    soundSyncState(g);
    CHECK(s.calls() == std::vector<RecordingSound::Call>{{Op::MusicStop, 0}, {Op::SfxStop, 0}, {Op::SilenceAll, 0}});
}

TEST_CASE("dos mainloop: Frame_Present copies the dirty spans, nothing when nothing was drawn") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    g.back.fillRect(10, 20, 19, 29, 0x77);
    const uint64_t v0 = g.screenVersion;
    g.core.dirty = 0;
    framePresent(g);
    CHECK(g.screenVersion == v0);
    CHECK(g.screen.byteAt(10, 20) == 0);
    g.core.dirty = 1;
    framePresent(g);
    CHECK(g.screenVersion == v0 + 1);
    CHECK(g.screen.byteAt(10, 20) == 0x77);
    CHECK(g.screen.byteAt(19, 29) == 0x77);
    CHECK(g.back.dirty().empty());
    CHECK_FALSE(g.dissolveRunning);
}

TEST_CASE("dos mainloop: a dissolve is one long frame: no Frame_Begin, then the sprites are erased") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    // The state a transition leaves: the latched request, something drawn.
    g.titleRunning = false;
    g.world.frameTransition = 1;
    g.world.transition = 1;
    g.core.dirty = 1;
    g.back.fillRect(2, 16, 0x9D, 143, 0x55);
    g.frameClock().beginFrame(kPitTicksPerGameTick);  // what the runtime does at each frame start
    framePresent(g);
    CHECK(g.dissolveRunning);
    g.rt.frameTiming().continueFrame();  // as mainFrameBody reports it after this present
    int frames = 1;
    while (g.dissolveRunning && frames < 50) {
        // The runtime's frame: no Frame_Begin latch (the latched camera stays), the gate kept (the body
        // reported FrameTiming::continueFrame, so the runtime does not clear g_stepDone).
        CHECK(f.runtime().frameTiming().continuesFrame());
        f.runtime().frameTiming().clear();
        g.world.cameraX = static_cast<uint16_t>(g.world.cameraX + 8);
        const uint16_t latched = g.world.frameCamX;
        g.frameClock().beginFrame(kPitTicksPerGameTick);
        const bool carried = g.dissolveGate;
        g.frameBegin();
        CHECK(g.world.frameCamX == latched);
        CHECK(g.stepDone() == carried);  // a mark set by a tick of the long frame stays
        if (frames == 2) g.stepGate().mark();  // as a tick of this frame would (Player_Move)
        g.frameBody();
        ++frames;
    }
    CHECK(frames >= 3);
    CHECK_FALSE(g.dissolveRunning);
    CHECK(g.world.transition == 0);
    CHECK(frames <= 8);  // 350 cells in 127 ms: 6-7 frames at the reference rate
    // The whole buffer is on the screen at the end (Present_Full).
    CHECK(g.screen.byteAt(2, 16) == 0x55);
    CHECK(g.screen.byteAt(0x9D, 143) == 0x55);
}

TEST_CASE("dos mainloop: Game_Shutdown stops the sound once and ends the run") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    f.sound().clear();
    gameShutdown(g);
    CHECK(g.core.quit == 1);
    CHECK(g.shutdownDone);
    CHECK(f.sound().calls() == std::vector<RecordingSound::Call>{{Op::MusicStop, 0}, {Op::SfxStop, 0}});
    gameShutdown(g);
    CHECK(f.sound().calls().size() == 2);
    f.runtime().stepGameTicks(1);
    CHECK_FALSE(f.runtime().running());  // g_quit: the loop ends after the frame
}

TEST_CASE("dos mainloop: the trace has one row per game tick with the values at the start of the tick") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    std::ostringstream out;
    {
        DosTrace trace(g, out);
        f.runtime().setClient(&trace);
        f.runtime().stepGameTicks(5);
        g.player.obj.x = 0x123;  // seen by the next row (taken before the tick)
        g.core.coins = 7;
        f.runtime().stepGameTicks(1);
        CHECK(trace.rows() == 6);
        f.runtime().setClient(&f);
    }
    std::istringstream in(out.str());
    std::string line;
    std::vector<std::string> rows;
    while (std::getline(in, line))
        if (!line.empty() && line[0] != '#') rows.push_back(line);
    REQUIRE(rows.size() == 7);
    CHECK(rows[0] == std::string(DosTrace::kHeader));
    const size_t columns = splitCsv(rows[0]).size();
    CHECK(columns == 16);
    for (size_t i = 1; i < rows.size(); ++i) {
        CAPTURE(rows[i]);
        const auto c = splitCsv(rows[i]);
        REQUIRE(c.size() == columns);
        CHECK(c[0] == std::to_string(i));          // tick
        CHECK(c[1] == std::to_string(5 * i));      // g_ticks
    }
    const auto last = splitCsv(rows.back());
    CHECK(last[3] == std::to_string(0x123));  // x
    CHECK(last[9] == "7");                    // coins
}

TEST_CASE("dos mainloop: the trace notes sound calls and UI mode changes as events") {
    TM_REQUIRE_DOS_GAME_FILES();
    DosRuntime rt;
    RecordingSound inner;
    TraceSound sound(inner);
    DosUsersStore users{fs::path{}};
    DosGame g(*gameFiles, rt, sound, users, DosOptions{});
    gameInit(g);
    std::ostringstream out;
    DosTrace trace(g, out, &sound);
    rt.setClient(&trace);
    g.core.uiMode = 8;  // as main sets it after the title
    rt.stepGameTicks(2);
    rt.setClient(nullptr);
    const std::string s = out.str();
    CHECK(s.find("gate=0;sfxstop;silence") != std::string::npos);  // Sound_SyncState's pause
    CHECK(s.find("ui=0x08") != std::string::npos);
    CHECK(inner.calls().size() >= 3);  // forwarded
}

TEST_CASE("dos mainloop: a DGROUP dump and its field list") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    g.core.coins = 0x1234;
    const fs::path dir = apptest::outDir("dos_mainloop_dump");
    const fs::path file = dir / "state.bin";
    REQUIRE(writeDgroupDump(g, file));
    const auto bytes = readWholeFile(file);
    REQUIRE(bytes);
    CHECK(bytes->size() == kDgroupSize);
    CHECK((*bytes)[dg::kCoins] == 0x34);
    CHECK((*bytes)[dg::kCoins + 1] == 0x12);
    CHECK(*bytes == dumpToDgroup(g));
    std::ifstream tsv(fs::path(file).concat(".fields.tsv"));
    REQUIRE(tsv);
    size_t lines = 0;
    std::string line;
    while (std::getline(tsv, line))
        if (!line.empty() && line[0] != '#') ++lines;
    CHECK(lines == gameStateFields().size() + serviceFields().size());
}

namespace {

// The number of rand() calls that take the Turbo C generator from seed 1 to `seed` (bounded search).
int randDrawsSinceStart(uint32_t seed, int limit = 200000) {
    BorlandRand r;
    for (int n = 0; n <= limit; ++n) {
        if (r.seed() == seed) return n;
        (void)r.rand();
    }
    return -1;
}

bool hasCall(const RecordingSound& s, RecordingSound::Call c) {
    return std::find(s.calls().begin(), s.calls().end(), c) != s.calls().end();
}

uint16_t word(const std::vector<uint8_t>& img, uint16_t ds) { return static_cast<uint16_t>(img[ds] | img[ds + 1u] << 8); }

// The title pages with Enter (title -> story 1 -> story 2 -> walk-off, skipped with a fourth Enter),
// stepping until the sign-in runs or `maxTicks` passed. Returns the tick the title ended (0: never).
uint64_t bootToSignIn(dosgame::GameFixture& f, int maxTicks = 2000) {
    REQUIRE(f.setScript("60:tap:enter, 120:tap:enter, 180:tap:enter, 240:tap:enter"));
    for (int i = 0; i < maxTicks; ++i) {
        f.step(1);
        if (!f.g().titleRunning) return f.runtime().gameTicks();
    }
    return 0;
}

}  // namespace

TEST_CASE("dos mainloop: the boot shows the title on the unseeded rand, Enter leads to a silent sign-in") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::FixtureOptions o;
    o.runGameInit = false;
    dosgame::GameFixture f(*gameFiles, o);
    DosGame& g = f.g();
    mainStart(g);
    CHECK(g.titleRunning);
    CHECK(g.core.uiMode == 0x80);
    CHECK(g.core.area == 6);
    CHECK(g.core.musicLoop == 1);
    CHECK(hasCall(f.sound(), {Op::MusicLoop, 1}));
    CHECK(hasCall(f.sound(), {Op::MusicPlay, 1}));
    CHECK(g.rand.seed() == 1);  // nothing drew from rand() before the title loop
    // Title_Run loads OPENING.PAN with g_quit = 1 right after Timer_Install: the first game tick falls
    // into the load (the reference timing; one tick lost before the sign-in in every DOSBox-X state).
    f.step(1);
    CHECK(f.runtime().ticksFrozen() == 1);
    CHECK(g.ui.titlePhase == 0);
    CHECK(f.runtime().counters().cycle13 == 12);  // Game_Tick did not run (its counters neither)
    f.step(1);
    CHECK(g.ui.titlePhase == 1);  // TitleStep_DrawTitle ran in the first frame
    CHECK(g.screenVersion > 0);
    CHECK(g.input.context == 5);
    f.step(48);
    // The sparkle attempts (one rand() each, 1/128 starts one, R(12) its spot) run on the unseeded
    // sequence: at the reference rate about 35.7 attempts per frame while slot 2 is idle.
    const int draws = randDrawsSinceStart(g.rand.seed());
    CAPTURE(draws);
    CHECK(draws > 100);
    CHECK(draws <= 50 * 40);
    const uint64_t ended = bootToSignIn(f);
    CAPTURE(ended);
    REQUIRE(ended > 0);
    CHECK(g.ui.titlePhase == 9);
    CHECK(g.core.uiMode == 8);
    CHECK(g.core.musicLoop == 0);
    // The first main-loop frame: Sound_SyncState pauses the title tune (ega_signin_idle is silent).
    f.sound().clear();
    f.step(2);
    CHECK(g.core.lastUiMode == 8);
    CHECK(g.core.musicGate == 0);
    CHECK(g.core.area == 4);       // SignIn_Update's initialisation
    CHECK(g.input.context == 4);   // name entry / sign-in zones
    CHECK(g.ui.screen == 2);       // painter: SignIn_Draw
}

TEST_CASE("dos mainloop: at the sign-in the deterministic globals equal the DOSBox-X state ega/signin") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::FixtureOptions o;
    o.runGameInit = false;
    o.options.sound = SoundDevice::AdLib;  // the reference machine's device (g_soundDevice 2)
    dosgame::GameFixture f(*gameFiles, o);
    DosGame& g = f.g();
    mainStart(g);
    REQUIRE(bootToSignIn(f) > 0);
    f.step(30);
    const auto img = dumpToDgroup(g);
    // (DS, size, value) read with savmem.py from extracted/captures/dos/savestates/ega/signin.sav: the
    // globals Game_InitVars, the title's end and the sign-in's initialisation leave, independent of
    // the timing (rand seed, tick counters, mouse idle count and the dealt puzzles differ by run).
    struct Expect {
        uint16_t ds;
        uint8_t size;
        uint16_t value;
    };
    static constexpr Expect kExpect[] = {
        {0x1300, 1, 0xFC},   {0x37AE, 1, 0xFF},   {0x37CE, 2, 0x0000}, {0x37DA, 2, 0x0000}, {0x37DC, 2, 0x009F},
        {0x37DE, 2, 0x0000}, {0x37E0, 2, 0x00C7}, {0x37E2, 2, 0x0000}, {0x37E4, 2, 0x0000}, {0x37EE, 2, 0x0000},
        {0x37F0, 2, 0x0000}, {0x3D31, 2, 0x0000}, {0x5B3C, 2, 0x000C}, {0x63B4, 2, 0x0001}, {0x63B6, 2, 0x0001},
        {0x63B8, 2, 0x0000}, {0x63BA, 2, 0x0000}, {0x63BC, 2, 0x0002}, {0x6169, 1, 0x01},   {0x8A14, 2, 0x0000},
        {0x8A16, 2, 0x0009}, {0x8FA5, 2, 0x0000}, {0x8FB0, 2, 0xFFFF}, {0x8FB4, 2, 0x0032}, {0x8FBE, 2, 0x00FF},
        {0x90F6, 2, 0xFFFF}, {0x90F8, 2, 0x0000}, {0x90FC, 2, 0xFFFF}, {0x9105, 2, 0x0000}, {0x910B, 2, 0x0004},
        {0x9111, 2, 0x0001}, {0x9115, 2, 0x0000}, {0x9119, 1, 0x08},   {0x912A, 2, 0xFFFF}, {0x912C, 2, 0x0001},
        {0x912E, 2, 0x0000}, {0x9136, 2, 0x0002}, {0x913E, 1, 0x02},   {0x9205, 2, 0x0000}, {0x9211, 2, 0x0044},
        {0x9213, 2, 0x001A}, {0x9217, 2, 0x0000}, {0x9219, 1, 0x00},   {0x9224, 2, 0x0000}, {0x922C, 2, 0x0004},
        {0x922E, 2, 0x0000}, {0x9234, 2, 0x0000}, {0x923C, 2, 0x0000}, {0x9254, 1, 0xFF},   {0x926C, 2, 0x0000},
        {0x9283, 2, 0x0001}, {0x928B, 2, 0x0006}, {0x928F, 2, 0x0000}, {0x9300, 1, 0x08},
    };
    int differ = 0;
    for (const Expect& e : kExpect) {
        const uint16_t v = e.size == 1 ? img[e.ds] : word(img, e.ds);
        CAPTURE(e.ds);
        CHECK(v == e.value);
        if (v != e.value) ++differ;
    }
    CHECK(differ == 0);
}

TEST_CASE("dos mainloop: a headless start-up runs the game with a trace, a state dump and screen dumps") {
    TM_REQUIRE_DOS_DATA();
    const bool videoBefore = SDL_WasInit(SDL_INIT_VIDEO) != 0;
    const bool audioBefore = SDL_WasInit(SDL_INIT_AUDIO) != 0;
    const fs::path dir = apptest::outDir("dos_mainloop_run");
    std::ofstream(dir / "tmport.ini") << "[saves]\nimported_original = true\n";
    const std::string data = pathToUtf8(*testdata::dosDataPath());
    const std::string pref = pathToUtf8(dir), ini = pathToUtf8(dir / "tmport.ini");
    const std::string trace = pathToUtf8(dir / "trace.csv"), frames = pathToUtf8(dir / "frames");
    const std::string dump = "150:" + pathToUtf8(dir / "state150.bin");
    std::vector<const char*> argv = {"tmport", "--edition", "dos", "--data", data.c_str(), "--headless", "--pref-dir", pref.c_str(),
                                     "--config", ini.c_str(), "--frame-dump", frames.c_str(), "--dos-trace", trace.c_str(),
                                     "--dos-dump-state", dump.c_str(), "--dos-ticks", "200", "--keys", "60:tap:enter"};
    std::vector<std::string> errors;
    auto sink = [&](const app::StartupError& e) { errors.push_back(e.what()); };
    CHECK(app::runGame(static_cast<int>(argv.size()), const_cast<char**>(argv.data()), {}, sink) == 0);
    CHECK(errors.empty());
    // The trace: header, one row per game tick.
    std::ifstream in(dir / "trace.csv");
    std::string line;
    int rows = 0;
    bool header = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!header) {
            CHECK(line == std::string(DosTrace::kHeader));
            header = true;
            continue;
        }
        ++rows;
    }
    CHECK(rows == 199);  // game tick 1 falls into Title_Run's OPENING load (g_quit): Game_Tick did not run
    CHECK(fs::file_size(dir / "state150.bin") == kDgroupSize);
    CHECK(fs::exists(fs::path(dir / "state150.bin").concat(".fields.tsv")));
    // Screen dumps: at least the final frame (320x200 PNGs), and the sound of the run.
    size_t pngs = 0;
    for (const auto& e : fs::directory_iterator(dir / "frames"))
        if (e.path().extension() == ".png") ++pngs;
    CHECK(pngs >= 1);
    CHECK(fs::file_size(dir / "frames" / "sound.wav") > 44u);
    if (testdata::dosFiles() && testdata::dosFiles()->hasUsersDat()) CHECK(fs::file_size(dir / "dos_users.dat") == 3243u);
    if (!videoBefore) CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    if (!audioBefore) CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
    // Bad diagnostics options are refused before anything starts.
    std::vector<const char*> bad = {"tmport", "--edition", "dos", "--data", data.c_str(), "--headless", "--dos-dump-state", "x"};
    CHECK(app::runGame(static_cast<int>(bad.size()), const_cast<char**>(bad.data()), {}, sink) == 2);
}

namespace {

// End to end from the boot: the title pages (Enter x 4, the last skips the walk-off), the name "zoe"
// + Enter, Enter on the start-of-game bubble, Up at the clubhouse door (Cell_P12_StartRoom: the
// dissolve to cell F3 of level 0), then `play` (keys of an r2 frame log whose first STEP record is
// engine tick 812: the player stands outside the clubhouse there). Returns FNV-1a 32 over x, y, camx,
// camy, state, keys, coins (u16 LE each) of the trace rows 812 .. 811 + records, the same hash
// extracted/scratch/dos_w3_mainloop/flhash.py computes over the log's STEP records.
struct EndToEnd {
    uint32_t hash = 0x811C9DC5u;
    int rows = 0;
    bool firstAtDoor = false;
};
EndToEnd runEndToEnd(const DosGfxFiles& files, const std::string& play, int records) {
    dosgame::FixtureOptions o;
    o.runGameInit = false;
    dosgame::GameFixture f(files, o);
    DosGame& g = f.g();
    std::ostringstream csv;
    DosTrace trace(g, csv);
    f.runtime().setClient(&trace);
    mainStart(g);
    REQUIRE(f.setScript("60:tap:enter, 120:tap:enter, 180:tap:enter, 240:tap:enter, 400:tap:z, 420:tap:o, 440:tap:e, "
                        "460:tap:enter, 600:tap:enter, 700:down:up, 760:up:up, " + play,
                        false));
    f.runtime().stepGameTicks(static_cast<uint64_t>(812 + records + 4));
    f.runtime().setClient(&f);
    CHECK(g.core.uiMode == 0);
    CHECK(g.core.level == 0);
    CHECK(g.world.loadedScene == 0);
    CHECK(g.core.gameInProgress == 1);
    CHECK(g.core.area == 0);
    EndToEnd r;
    std::istringstream in(csv.str());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == 't') continue;
        const auto c = splitCsv(line);
        const uint64_t tick = std::stoull(c[0]);
        if (tick < 812 || tick >= static_cast<uint64_t>(812 + records)) continue;
        for (int k : {3, 4, 5, 6, 7, 8, 9}) {  // x y camx camy state keys coins
            const auto v = static_cast<uint16_t>(std::stoul(c[static_cast<size_t>(k)]));
            for (const uint8_t b : {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)}) {
                r.hash ^= b;
                r.hash *= 0x01000193u;
            }
        }
        if (tick == 812)  // cell F3 of level 0, standing (state 10), 5 coins
            r.firstAtDoor = c[3] == "448" && c[4] == "2032" && c[5] == "288" && c[6] == "1920" && c[7] == "10" && c[9] == "5";
        ++r.rows;
    }
    return r;
}

}  // namespace

TEST_CASE("dos mainloop: end to end: a new player leaves the clubhouse and plays as in the DOSBox-X frame logs") {
    TM_REQUIRE_DOS_GAME_FILES();
    SUBCASE("r2/fl_walk2: walking right and left, the camera following (544 ticks)") {
        const EndToEnd r = runEndToEnd(*gameFiles, "900:down:right, 1091:up:right, 1106:down:left, 1249:up:left", 544);
        CHECK(r.rows == 544);
        CHECK(r.firstAtDoor);
        CHECK(r.hash == 0x130B2723u);
    }
    SUBCASE("r2/fl_actions: net swing, Down without a coin target, jumps left and right (619 ticks)") {
        const EndToEnd r = runEndToEnd(*gameFiles,
                                       "867:down:space, 871:up:space, 996:down:down, 997:up:down, 1125:down:home, "
                                       "1129:up:home, 1254:down:pgup, 1258:up:pgup",
                                       619);
        CHECK(r.rows == 619);
        CHECK(r.firstAtDoor);
        CHECK(r.hash == 0x8FCFA2AFu);
    }
}

TEST_CASE("dos integration: the effect loop flag DS:63B8 keeps an effect playing until it is cleared") {
    TM_REQUIRE_DOS_FILES();
    auto device = makeDosAudioDevice(*dosFiles, SoundDevice::Tandy);
    REQUIRE(device);
    DosAudioSound s(*device, true);
    // The plain length of effect 2 (the end sequence's sparkle).
    s.sfxPlay(2);
    int plain = 0;
    while (s.sfxActive() && plain < 20000) {
        s.pitTick();
        ++plain;
    }
    REQUIRE(plain > 1);
    REQUIRE(plain < 20000);
    // PlrSt_44_EndSparkle: loop on, then the effect; it still plays after three of its lengths.
    s.setSfxLoop(true);
    s.sfxPlay(2);
    for (int i = 0; i < 3 * plain + 10; ++i) s.pitTick();
    CHECK(s.sfxActive());
    // PlrSt_45_EndChestOpen: loop off; the pass that is playing is the last one.
    s.setSfxLoop(false);
    int rest = 0;
    while (s.sfxActive() && rest < 20000) {
        s.pitTick();
        ++rest;
    }
    CAPTURE(rest);
    CHECK(rest <= plain + 1);
}

TEST_CASE("dos mainloop: the driver facade shows an effect's status at once and while it plays") {
    TM_REQUIRE_DOS_FILES();
    auto device = makeDosAudioDevice(*dosFiles, SoundDevice::Tandy);
    REQUIRE(device);
    DosAudioSound s(*device, true);
    CHECK_FALSE(s.sfxActive());
    s.sfxPlay(4);
    CHECK(s.sfxActive());  // Sfx_Play sets DS:6448 at once, before the driver's next tick
    int ticks = 0;
    while (s.sfxActive() && ticks < 5000) {
        s.pitTick();
        ++ticks;
    }
    CAPTURE(ticks);
    CHECK(ticks > 1);       // the effect lasts some ticks
    CHECK(ticks < 5000);    // and ends
    s.sfxPlay(4);
    s.pitTick();
    CHECK(s.sfxActive());
    s.sfxStop();
    CHECK_FALSE(s.sfxActive());  // Sfx_Stop clears it at once
    s.pitTick();
    CHECK_FALSE(s.sfxActive());
    // With the effects off Sfx_Play does nothing.
    s.setSoundOn(false);
    s.sfxPlay(4);
    CHECK_FALSE(s.sfxActive());
    s.pitTick();
    CHECK_FALSE(s.sfxActive());
}

TEST_CASE("dos mainloop: a delayed call (inside a long frame) reaches the driver that many PIT ticks later, in time order") {
    TM_REQUIRE_DOS_FILES();
    auto device = makeDosAudioDevice(*dosFiles, SoundDevice::Tandy);
    REQUIRE(device);
    DosAudioSound s(*device, true);
    uint32_t delay = 3;
    s.setDelaySource([&delay] { return delay; });
    s.sfxPlay(4);  // made 3 PIT ticks into a frame: the 4th interrupt from now plays it
    delay = 0;
    CHECK(s.sfxActive());  // Sfx_Play set DS:6448 when it was made
    for (int t = 0; t < 3; ++t) {
        s.pitTick();
        CHECK(device->status().sfxStatus == 0);
        CHECK(s.sfxActive());
    }
    s.pitTick();
    CHECK(device->status().sfxStatus != 0);
    CHECK(s.sfxActive());
    // A stop made later but due earlier (a game tick's call while the frame's play waits) comes first.
    s.sfxStop();
    s.pitTick();
    CHECK(device->status().sfxStatus == 0);
    delay = 2;
    s.sfxPlay(4);
    delay = 0;
    s.sfxStop();
    s.pitTick();
    CHECK(device->status().sfxStatus == 0);
    s.pitTick();
    s.pitTick();
    CHECK(device->status().sfxStatus != 0);  // the play, due at the third interrupt, came after the stop
}

TEST_CASE("dos mainloop: a resumed game's tune and the start message's pause sound where the original's frames made them (wave 5)") {
    // tandy/signin: Ada, Enter, Yes on the saved-game prompt (looks/state_tandy_signin). The frame after
    // the flash: Game_Start's Quiz_LoadQuestions (7 PIT ticks), its Music_Play, Scene_Update's
    // MOUNT2N3 (20.3) and SPRITES.PAK (2.2), Sound_SyncState's gate and Music_Play; the next frames:
    // the HUD redraw (16), then the start message's bubble (27.5) before its Sound_SyncState pauses
    // the music (avi/tandy_r3_session.avi: the tune starts 23 PIT ticks after the flash's end, not at it).
    TM_REQUIRE_DOS_GAME_FILES();
    TM_REQUIRE_DOS_CAPTURES();
    dosgame::GameFixture f(*gameFiles);
    DosGame& g = f.g();
    (void)loadDosboxState(g, DosboxState::load(*captures / "savestates" / "tandy" / "signin.sav"));
    f.sound().clear();
    f.sound().setDelaySource([&f] { return f.runtime().soundDelay(); });
    REQUIRE(f.setScript("20:tap:up, 30:tap:enter, 60:tap:enter"));
    f.step(120);
    const auto& calls = f.sound().calls();
    const auto& delays = f.sound().delays();
    REQUIRE(calls.size() == delays.size());
    std::string seen;
    for (size_t i = 0; i < calls.size(); ++i) seen += std::format("{}:{}@{} ", static_cast<int>(calls[i].op), calls[i].arg, delays[i]);
    CAPTURE(seen);
    std::vector<uint32_t> tunes, gates;
    for (size_t i = 0; i < calls.size(); ++i) {
        if (calls[i].op == Op::MusicPlay) tunes.push_back(delays[i]);
        if (calls[i].op == Op::MusicGate) gates.push_back(delays[i]);
    }
    REQUIRE(tunes.size() == 2);
    // Game_Start's after its question load (7 PIT ticks from the flash's end, which lies 0..4 PIT ticks
    // before the runtime's tick), Sound_SyncState's after MOUNT2N3 and SPRITES.PAK as well (22.5 more).
    CHECK(tunes[1] - tunes[0] == 22);
    CHECK(tunes[0] >= 3);
    CHECK(tunes[0] <= 7);
    // The gate opens with the second (same frame); the bubble's pause comes 27.5 PIT ticks into its
    // frame, which began when the HUD redraw's frame ended, between two game ticks (before the
    // runtime's current one: 2 to 3 PIT ticks less from there).
    REQUIRE(gates.size() >= 2);
    CHECK(gates[gates.size() - 2] == tunes[1]);
    CHECK(gates.back() >= 24);
    CHECK(gates.back() <= 28);
}

TEST_CASE("dos mainloop: without keys the attract pages change on their 7200-PIT-tick time-outs") {
    TM_REQUIRE_DOS_GAME_FILES();
    dosgame::FixtureOptions o;
    o.runGameInit = false;
    dosgame::GameFixture f(*gameFiles, o);
    DosGame& g = f.g();
    mainStart(g);
    std::vector<std::pair<uint64_t, int>> changes;  // (game tick, new title phase)
    int phase = g.ui.titlePhase;
    for (int i = 0; i < 5800 && g.titleRunning; ++i) {
        f.step(1);
        if (g.ui.titlePhase != phase) changes.emplace_back(f.runtime().gameTicks(), phase = g.ui.titlePhase);
    }
    // Title (1) -> story 1 (2, drawn: 3) -> story 2 (4, 5) -> on its time-out the Super Solvers page
    // (10, 11) -> the title again (12, 0, 1). TitleStep_Wait sets its timer on its first call, after the
    // page's draw, and fires on the PIT interrupt that passes it; the draw follows in the same frame
    // (wave 4, code_ui.md §8.5: page draws of 12.8 / 13.3 / 7.5 / 14.3 PIT ticks for the title, story
    // 1, story 2 and the series page, avi/tandy_attract_full_cycle.avi: 7304 frames = 5770 game ticks
    // from story page 1 to story page 1). The title's first frame follows Title_Run's OPENING load,
    // into which game tick 1 falls.
    const std::vector<std::pair<uint64_t, int>> expect = {{2, 1}, {1444, 3}, {2887, 5}, {4329, 11}, {5772, 1}};
    CHECK(changes == expect);
    CHECK(g.titleRunning);
    CHECK(g.core.uiMode == 0x80);
}

// ---- the reference frame timing against the DOSBox-X frame logs (wave 4) -------------------------

namespace {

// The rings of the frame logger (extracted/scratch/dos_w1_runtime/framelog.py) in a DOSBox-X state:
// a STEP record per executed game tick (g_ticks low word, x, y, camera x, y, player state, g_keys,
// coins) at 8800:0010 (ring of 0x800 records, index 8800:0000, count 8800:0002), the g_ticks of every
// gated tick at 8000:4200 (ring of 0x1000 words, index 8000:0108, count 8000:010A).
struct FrameLog {
    struct Step {
        uint16_t ticks = 0, x = 0, y = 0, camX = 0, camY = 0, state = 0, keys = 0, coins = 0;
    };
    std::vector<Step> steps;
    std::vector<uint16_t> blocked;
};

FrameLog readFrameLog(const DosboxState& s) {
    FrameLog log;
    constexpr uint32_t kSteps = 0x88000, kRing = 0x80000;
    const uint16_t sidx = s.wordAt(kSteps), scnt = s.wordAt(kSteps + 2);
    for (int i = std::min<int>(scnt, 0x800); i >= 1; --i) {
        const uint32_t o = kSteps + 0x10 + ((static_cast<uint32_t>(sidx) - 16u * static_cast<uint32_t>(i)) & 0x7FF0u);
        FrameLog::Step st;
        uint16_t* f[] = {&st.ticks, &st.x, &st.y, &st.camX, &st.camY, &st.state, &st.keys, &st.coins};
        for (int k = 0; k < 8; ++k) *f[k] = s.wordAt(o + 2u * static_cast<uint32_t>(k));
        log.steps.push_back(st);
    }
    const uint16_t bidx = s.wordAt(kRing + 0x108), bcnt = s.wordAt(kRing + 0x10A);
    for (int i = std::min<int>(bcnt, 0x1000); i >= 1; --i)
        log.blocked.push_back(s.wordAt(kRing + 0x4200 + ((static_cast<uint32_t>(bidx) - 2u * static_cast<uint32_t>(i)) & 0x1FFFu)));
    return log;
}

struct LogReplay {
    int ticks = 0, matched = 0;
    std::string first;
};

// Replays a frame log: the game from the state the log started at (loadDosboxState), the log's keys
// (trace convention: an event at tick T shows in the trace row of tick T, the log's record T), its
// per-tick trace compared with the log's records: a STEP record needs an executed, ungated tick with
// the same x, y, camera, state, coins (and keys), a BLOCKED one a gated tick, a log tick with neither
// (Game_Tick returned at once: g_quit during disk I/O) no executed tick.
LogReplay replayFrameLog(const DosGfxFiles& files, const fs::path& captures, const char* start, const char* logName,
                         const std::string& keys, bool compareKeys, bool greyKeyArtefact = false) {
    LogReplay r;
    const DosboxState st = DosboxState::load(captures / "savestates" / "r2" / (std::string(start) + ".sav"));
    const FrameLog log = readFrameLog(DosboxState::load(captures / "savestates" / "r2" / (std::string(logName) + ".sav")));
    REQUIRE_FALSE(log.steps.empty());
    dosgame::GameFixture f(files);
    DosGame& g = f.g();
    if (greyKeyArtefact) {
        KeyboardOptions ko = f.runtime().keyboard().options();
        ko.greyKeyE0Artefact = true;
        f.runtime().keyboard().setOptions(ko);
    }
    const LoadedState loaded = loadDosboxState(g, st);
    std::ostringstream csv;
    DosTrace trace(g, csv);
    f.runtime().setClient(&trace);
    REQUIRE(f.setScript(keys));
    // The log's game ticks as the engine's: engine game tick n comes at the original's g_ticks
    // firstTickTicks + 5 (n - 1) (a tick a Game_Tick overrun delayed by a PIT tick keeps its number). A
    // full ring starts after the start state: its first ticks are not compared.
    const uint16_t tick1 = static_cast<uint16_t>(loaded.firstTickTicks);
    auto index = [tick1](uint16_t t) { return static_cast<int>((static_cast<uint16_t>(t - tick1) + 2) / 5) + 1; };
    std::map<int, const FrameLog::Step*> steps;
    for (const auto& s : log.steps)
        if (static_cast<uint16_t>(s.ticks - tick1 + 2) < 0x8000) steps[index(s.ticks)] = &s;
    REQUIRE_FALSE(steps.empty());
    const int firstLogged = steps.begin()->first, last = steps.rbegin()->first;
    std::map<int, bool> blocked;
    for (uint16_t b : log.blocked)
        if (static_cast<uint16_t>(b - tick1 + 2) < 0x8000 && index(b) >= firstLogged && index(b) <= last) blocked[index(b)] = true;
    f.runtime().stepGameTicks(static_cast<uint64_t>(last + 2));
    f.runtime().setClient(&f);
    std::map<int, std::vector<std::string>> rows;
    std::istringstream in(csv.str());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == 't') continue;
        const auto c = splitCsv(line);
        rows[std::stoi(c[0])] = c;
    }
    for (int t = firstLogged; t <= last; ++t) {
        ++r.ticks;
        const auto row = rows.find(t);
        std::string why;
        if (const auto s = steps.find(t); s != steps.end()) {
            const FrameLog::Step& e = *s->second;
            if (row == rows.end()) {
                why = "frozen";
            } else if (row->second[2] == "1") {
                why = "gated";
            } else {
                const auto& c = row->second;
                const uint16_t want[] = {e.x, e.y, e.camX, e.camY, e.state, e.keys, e.coins};
                for (int k = 0; k < 7; ++k) {
                    if (k == 5 && !compareKeys) continue;
                    if (static_cast<uint16_t>(std::stoul(c[static_cast<size_t>(3 + k)])) != want[k]) why = std::format("field {}", k);
                }
            }
        } else if (blocked.count(t)) {
            if (row == rows.end() || row->second[2] != "1") why = "not gated";
        } else if (row != rows.end()) {
            why = "ran in g_quit";
        }
        if (why.empty()) ++r.matched;
        else if (r.first.empty()) r.first = std::format("log tick {}: {}", t, why);
    }
    return r;
}

}  // namespace

TEST_CASE("dos mainloop: frame logs replayed from their start states match tick for tick across loads and long frames") {
    TM_REQUIRE_DOS_GAME_FILES();
    TM_REQUIRE_DOS_CAPTURES();
    struct Case {
        const char* start;
        const char* log;
        const char* keys;
        bool keysCompared;
        bool greyKey;
        int ticks, matched;
    };
    // Every log matches on every tick: r2/fl_bridge_l3's gated log tick 1111 comes from frames that ran
    // back to back (a scroll frame, the bridge's redraw, the fall's first scroll frame), the tierc
    // door's gated ticks from the host mouse the reference run moved (DS:911E 180 -> 97: the pointer
    // shows on the message and when it closes), the champion's arrival in r2/fl_crown_wait from the
    // load beginning after the chute's last scroll frame with the sprite bank (PORTING_NOTES §2.5).
    static constexpr Case kCases[] = {
        {"fl_outside2", "fl_walk2", "89:down:right, 280:up:right, 295:down:left, 438:up:left", true, false, 544, 544},
        // Sub-tick key times: PgUp held from 195.136 (its repeats' E0 bytes give the one gated-out jump
        // bit of the log at tick 275 with the grey-key artefact on), Down from 807.4923 (the phase of
        // the Down repeats).
        {"fl_outside2", "fl_jumps",
         "56:down:right, 104:up:right, 195.136:down:pgup, 291:up:pgup, 387:down:pgup, 391:up:pgup, 492:down:up, "
         "493:up:up, 503:down:right, 575:up:right",
         true, true, 752, 752},
        {"fl_outside2", "fl_typematic",
         "56:down:right, 104:up:right, 195.136:down:pgup, 291:up:pgup, 387:down:pgup, 391:up:pgup, 492:down:up, "
         "493:up:up, 503:down:right, 575:up:right, 807.4923:down:down, 902.2367:up:down",
         true, true, 975, 975},
        {"fl_at_lock", "fl_climb", "89:down:up, 104:up:up, 248:down:up, 533:up:up", false, false, 640, 640},
        {"fl_level3_start", "fl_geyser_slide", "162:down:left, 221:up:left", false, false, 663, 663},
        {"fl_l2_at_nests_tier2", "fl_bridge_l2", "90:down:right, 257:up:right", false, false, 746, 746},
        {"fl_l3_at_tree_tier2", "fl_bridge_l3", "147:down:right, 267:up:right, 1082:down:right, 1153:up:right", false, false,
         1596, 1596},
        {"fl_l3_at_tree_tier2", "fl_bridge_l3_jump", "89:down:right, 203:up:right, 204:down:pgup, 208:up:pgup", false, false,
         438, 438},
        {"fl_l2door", "fl_l2door_after", "56:down:up, 71:up:up", true, false, 190, 190},
        {"fl_shack", "fl_shack_left", "57:down:left, 294:up:left", false, false, 366, 366},
        {"fl_tierc_start", "fl_tierc_outside",
         "60:move:318:97, 430:move:316:97, 431:move:318:97, 56:down:up, 79:up:up, 83:down:up, 85:up:up, 1004:tap:enter", false,
         false, 1161, 1161},
        {"fl_slowcpu_clubhouse", "fl_slowcpu_walk",
         "55:down:up, 80:up:up, 914:down:right, 1199:up:right, 1214:down:left, 1499:up:left", false, false, 1745, 1745},
        // The end sequences (full rings: the logs' last 2048 executed ticks): castle top, treasures, the
        // chute, the castle gate with the treasure room's load, the rank screen.
        {"fl_castle_upper_t2", "fl_end_t2_wait", "82:down:up, 342:up:up", false, false, 2049, 2049},
        {"fl_castle_upper_t6_294", "fl_crown_wait", "81:down:up, 342:up:up", true, false, 2055, 2055},
    };
    for (const Case& c : kCases) {
        const auto startSav = *captures / "savestates" / "r2" / (std::string(c.start) + ".sav");
        if (!fs::exists(startSav)) continue;
        const LogReplay r = replayFrameLog(*gameFiles, *captures, c.start, c.log, c.keys, c.keysCompared, c.greyKey);
        INFO(std::string(c.log) << ": " << r.matched << " of " << r.ticks << " ticks match; first difference " << r.first);
        CHECK(r.ticks == c.ticks);
        CHECK(r.matched == c.matched);
    }
}

TEST_CASE("dos mainloop: a new player's game start lasts as in DOSBox-X (the sprite slots of ega/shack_hint1)") {
    TM_REQUIRE_DOS_GAME_FILES();
    TM_REQUIRE_DOS_CAPTURES();
    const auto sav = *captures / "savestates" / "ega" / "shack_hint1.sav";
    if (!fs::exists(sav)) return;
    dosgame::FixtureOptions o;
    o.runGameInit = false;
    o.layoutSeed = uint16_t{0x1EE2};
    dosgame::GameFixture f(*gameFiles, o);
    DosGame& g = f.g();
    mainStart(g);
    // The title pages after the pointer hid (600 idle ticks, as in the capture run), the name, Enter.
    REQUIRE(f.setScript("660:tap:enter, 720:tap:enter, 780:tap:enter, 840:tap:enter, 1000:tap:a, 1020:tap:d, 1040:tap:a, "
                        "1060:tap:enter",
                        false));
    f.step(1060);
    CHECK(f.runtime().ticksFrozen() == 1);  // Title_Run's OPENING load
    f.step(20);
    // USERS.DAT written, Quiz_LoadQuestions, MOUNTAIN.PAN with g_quit = 1: 7 more ticks lost (8 in all
    // in the DOSBox-X state: DS:90FE and g_ticks); the HUD redraw that follows ran two player ticks
    // (states 3 -> 4 -> 10), then the start message opened over the player's front pose.
    CHECK(f.runtime().ticksFrozen() == 8);
    CHECK(g.core.uiMode == 0x04);
    CHECK(g.player.playerState == 10);
    f.step(170);
    const DosboxState st = DosboxState::load(sav);
    const auto img = dumpToDgroup(g);
    const auto ref = st.dgroup();
    constexpr uint16_t kSlots = 0x8FD6;  // g_spriteSlots: 13 slots of 22 bytes
    int differ = 0;
    for (uint16_t i = 0; i < 13 * 22; ++i) differ += img[kSlots + i] != ref[kSlots + i] ? 1 : 0;
    CHECK(differ == 0);
    // The lost ticks as the DOSBox-X state counts them: (g_ticks / 5 - executed Game_Ticks) mod 13.
    const int executed13 = 12 - f.runtime().counters().cycle13;
    const int lost13 = static_cast<int>((f.runtime().gameTicks() - static_cast<uint64_t>(executed13)) % 13);
    const int refLost13 = static_cast<int>((st.dsDword(0x3D2B) / 5 - static_cast<uint32_t>(12 - st.dsWord(0x90FE))) % 13);
    CHECK(lost13 == refLost13);
}
