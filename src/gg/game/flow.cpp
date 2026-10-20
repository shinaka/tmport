// The game's flow (flow.h): main, Game_Init / Game_Shutdown, Seq_Run's main table, the hub, states 16
// and 17, Building_Free, Save & Exit, the LZH refill's key drain. Owner: flow agent.
#include "gg/game/flow.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>
#include <vector>

#include "gg/app/gg_app.h"
#include "gg/audio/gg_audio.h"
#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/game/actors.h"
#include "gg/game/chimps.h"
#include "gg/game/game.h"
#include "gg/game/load_state.h"
#include "gg/game/parts.h"
#include "gg/game/player.h"
#include "gg/game/puzzles.h"
#include "gg/game/race.h"
#include "gg/game/results.h"
#include "gg/game/rooms.h"
#include "gg/game/sound.h"
#include "gg/game/trace_log.h"
#include "gg/game/workshop.h"
#include "gg/game/world.h"
#include "gg/save/gg_state_file.h"
#include "gg/save/players_store.h"
#include "gg/ui/dialog.h"
#include "gg/ui/hints.h"
#include "gg/ui/menu_bar.h"
#include "gg/ui/menu_commands.h"
#include "gg/ui/players.h"
#include "gg/ui/signin.h"
#include "mods/wav_file.h"
#include "util/error.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kTitleMusic = 0x1020;       // DS:0098 g_titleMusicBank's bank (sign-in, hub)
constexpr uint16_t kMsgAllBuildingsDone = 0x106D;  // the hub's "complete" message (g_racesWonTotal 15)
constexpr uint16_t kMsgBuildingDone = 0x106E;      // a building whose four races are won
constexpr uint16_t kKeyEsc = 0x001B;

// What Init_CommandLine (303a:005c) leaves in DS:244e and DS:2450 on the reference machine (the
// DOSBox-X profile: re/gg/docs/REFERENCE_CAPTURES.md §1): Video_DccSupported returns AL = 1Ah on a BIOS
// with INT 10h function 1Ah (code_runtime.md §18), Cpu_Detect 4 on a 486. Only the chimp set-up's
// `== 2` test reads g_cpuType (the --cpu 286 path).
constexpr int16_t kDisplayCodeVga = 0x1A;
constexpr int16_t kCpuType486 = 4;

// ---- the sound device of a run (Init_CommandLine 303a:005c step 7, Game_Init's SndRes_Init)

// GgAudio as the launch asks for it: an SDL stream in a window, else the driver ticked by the virtual
// PIT (headless runs or no device) with --wav-dump's samples kept; --opl-log. finish() writes the dumps
// (after Game_Shutdown's Snd_Off).
class GameAudio {
public:
    explicit GameAudio(GgContext& ctx) : ctx_(ctx) {
        const GgOptions& o = ctx.launch.options;
        GgAudioConfig cfg;
        cfg.mode = ctx.launch.sound;
        cfg.logOpl = !o.oplLog.empty();
        audio_ = std::make_unique<GgAudio>(ctx.files.exe(), ctx.files.archive(), cfg);
        if (!ctx.launch.headless) {
            if (!audio_->open()) log::warn("no audio device: the sound runs silently");
        } else if (!o.wavDump.empty()) {
            keepPcm_ = true;
        }
        if (!audio_->isOpen()) {
            // Timer_ISR: Snd_Tick on every even g_ticks (code_runtime.md §1).
            pitSound_ = true;
            ctx.runtime.clock().setSoundTick([this](uint32_t) { audio_->step(keepPcm_ ? &pcm_ : nullptr); });
        }
    }
    ~GameAudio() {
        if (pitSound_) ctx_.runtime.clock().setSoundTick({});
    }
    GameAudio(const GameAudio&) = delete;
    GameAudio& operator=(const GameAudio&) = delete;

    GgAudio& audio() { return *audio_; }

    // --wav-dump and --opl-log.
    void finish() {
        if (finished_) return;
        finished_ = true;
        const GgOptions& o = ctx_.launch.options;
        if (!o.wavDump.empty()) {
            if (!keepPcm_) {
                log::warn("--wav-dump: only headless runs render their sound offline");
            } else {
                std::vector<uint8_t> bytes;
                bytes.reserve(pcm_.size() * 2);
                for (const float f : pcm_) {
                    const auto v = static_cast<int16_t>(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
                    bytes.push_back(static_cast<uint8_t>(v));
                    bytes.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
                }
                const auto wav = mods::makeWav(static_cast<uint32_t>(audio_->sampleRate()), 1, 16, bytes);
                if (writeWholeFile(pathFromUtf8(o.wavDump), wav))
                    log::info("--wav-dump: {} samples ({} sound ticks) -> {}", pcm_.size(), audio_->tickIndex(), o.wavDump);
                else
                    log::error("cannot write {}", o.wavDump);
            }
        }
        if (!o.oplLog.empty()) {
            if (audio_->isOpen()) audio_->close();  // the log is read while no stream runs
            if (audio_->oplLog().save(o.oplLog))
                log::info("--opl-log: {} OPL writes -> {}", audio_->oplLog().entries().size(), o.oplLog);
            else
                log::error("cannot write {}", o.oplLog);
        }
    }

private:
    GgContext& ctx_;
    std::unique_ptr<GgAudio> audio_;
    bool pitSound_ = false;
    bool keepPcm_ = false;
    std::vector<float> pcm_;
    bool finished_ = false;
};

// ---- Seq_Run's tables

// The engine function of each state function of v1.0 (Ghidra address after the overlay stub).
using StateBinder = StateFn (*)(GgGame&);
const std::map<uint32_t, StateBinder>& mainStateFunctions() {
    static const std::map<uint32_t, StateBinder> m = {
        {0x2c110913, [](GgGame& g) -> StateFn { return [&g] { return state01Attract(g); }; }},
        {0x2caa1240, [](GgGame& g) -> StateFn { return [&g] { return state02SignIn(g); }; }},
        {0x12e00148, [](GgGame& g) -> StateFn { return [&g] { return state03Hub(g); }; }},
        {0x12e00820, [](GgGame& g) -> StateFn { return [&g] { return state04BuildingSetup(g); }; }},
        {0x12e0096a, [](GgGame& g) -> StateFn { return [&g] { return state05EnterRoom(g); }; }},
        {0x2dee1a90, [](GgGame& g) -> StateFn { return [&g] { return state06Workshop(g); }; }},
        {0x12e00619, [](GgGame& g) -> StateFn { return [&g] { return state07Warehouse(g); }; }},
        {0x12e009aa, [](GgGame& g) -> StateFn { return [&g] { return state08LeaveRoom(g); }; }},
        {0x38e20000, [](GgGame& g) -> StateFn { return [&g] { return state09Puzzle(g); }; }},
        {0x12e00506, [](GgGame& g) -> StateFn { return [&g] { return state10ListVehicleParts(g); }; }},
        {0x12e00564, [](GgGame& g) -> StateFn { return [&g] { return state11Race(g); }; }},
        {0x2fcb0325, [](GgGame& g) -> StateFn { return [&g] { return state12RaceResult(g); }; }},
        {0x2fcb0573, [](GgGame& g) -> StateFn { return [&g] { return state13PostRaceMenu(g); }; }},
        {0x2fcb0315, [](GgGame& g) -> StateFn { return [&g] { return state14RedrawResult(g); }; }},
        {0x2fcb0640, [](GgGame& g) -> StateFn { return [&g] { return state15FinalMovie(g); }; }},
        {0x12e0012f, [](GgGame& g) -> StateFn { return [&g] { return state16SaveAndLeave(g); }; }},
        {0x12e00141, [](GgGame& g) -> StateFn { return [&g] { return state17End(g); }; }},
    };
    return m;
}

// The hub's progress (DS:4596, DS:5c10, DS:5c0e) for its screen.
HubProgress hubProgress(const PlayerRecord& p) {
    HubProgress h;
    for (size_t b = 0; b < h.racesWon.size(); ++b) h.racesWon[b] = p.racesWon[b];
    std::copy(p.vehicleColor.begin(), p.vehicleColor.end(), h.vehicleColor.begin());
    h.racesWonTotal = p.racesWonTotal;
    return h;
}

using HubResumeAt = FlowObjects::HubResume;

// The run's tools: --gg-trace FILE (with --gg-trace-polls) and --gg-dump-state TICK FILE.
struct RunTools {
    std::ofstream traceFile;
    std::unique_ptr<GgTraceLogger> trace;
    std::unique_ptr<GgStateDumper> dump;
};

void startTools(GgGame& g, const GgOptions& o, RunTools& t) {
    if (o.dumpStateTick) t.dump = std::make_unique<GgStateDumper>(g, *o.dumpStateTick, pathFromUtf8(o.dumpStateFile));
    if (!o.traceFile.empty()) {
        t.traceFile.open(pathFromUtf8(o.traceFile), std::ios::binary | std::ios::trunc);
        if (!t.traceFile) throw Error(std::format("--gg-trace: cannot write {}", o.traceFile));
        t.trace = std::make_unique<GgTraceLogger>(g, t.traceFile, o.tracePolls);
    }
}

void stopTools(RunTools& t) {
    if (t.trace) {
        log::info("--gg-trace: {} rows", t.trace->rows());
        t.trace.reset();
        t.traceFile.flush();
    }
    if (t.dump && !t.dump->written()) log::warn("--gg-dump-state: the run ended before PIT tick {}", t.dump->tick());
    t.dump.reset();
}

}  // namespace

// 12e0:0b38 main
int gameMain(GgContext& ctx) {
    const GgLaunch& launch = ctx.launch;
    const GgOptions& o = launch.options;
    // Init_CommandLine 303a:005c step 7 (Snd_Init, g_fmPresent, Snd_Silence for -SN) and Game_Init's
    // SndRes_Init (the global effect bank) are GgAudio's construction.
    GameAudio audio(ctx);
    GgAudioSound sound(audio.audio());
    // PLAYERS.DAT: the engine's copy in the preference folder (PORTING_NOTES §9); on the first run the
    // game folder's file is imported, so the players of the original carry over. Roster_LoadOrCreate
    // (the sign-in) reads or creates it.
    std::filesystem::path playersPath;
    if (!launch.prefDir.empty()) {
        playersPath = playersPathIn(launch.prefDir);
        std::error_code ec;
        if (const auto original = ctx.files.playersDatPath(); original && !std::filesystem::exists(playersPath, ec)) {
            const PlayersImport r = importPlayers(*original, playersPath, false);
            log::info("PLAYERS.DAT of the game folder: {}", describe(r));
        }
    } else {
        log::warn("no preference folder: the players are not saved");
    }
    PlayersStore players(playersPath);

    // Init_CommandLine's globals: the switches (-S.., -GV), the CPU class, the seed (srand(DS:2454) is
    // the app's, gg_app.cpp).
    GgGameOptions go;
    go.cpu = launch.cpu;
    // g_soundDevice: the -S switch's value; a sound chosen another way (--sound, ggport.ini) as the
    // switch that gives it (off -SN 0, speaker -SI 1, fm -SA 4; fm without a switch stays -1, detect).
    go.soundDevice = launch.switches.soundDevice;
    if (soundForDevice(go.soundDevice) != launch.sound)
        go.soundDevice = launch.sound == GgSound::Off ? int16_t{0} : launch.sound == GgSound::Speaker ? int16_t{1} : int16_t{4};
    go.displayCode = launch.switches.displayCode;
    go.seed = launch.seed;
    // atexit(Game_Shutdown); Gfx_Init, Font_Init, PicCache_Init, and Game_Init's Hud_Init are the
    // context's construction.
    GgGame g(ctx.files, ctx.runtime, sound, players, ctx.rand, go);
    // What Init_CommandLine's checks store on the reference machine (the checks themselves are
    // dropped, PORTING_NOTES §11): the display code of Video_DccSupported unless -GV, the CPU class.
    if (g.runtime.displayCode == -1) g.runtime.displayCode = kDisplayCodeVga;
    if (launch.cpu != GgCpu::Cpu286) g.runtime.cpuType = kCpuType486;

    struct DrainGuard {
        GgGame& g;
        ~DrainGuard() { installLzhKeyDrain(g, false); }
    } drainGuard{g};
    installLzhKeyDrain(g, true);

    int code = exit_code::kNormal;
    bool shutDown = false;
    const auto shutdown = [&] {
        // exit(): the atexit function Game_Shutdown, then the app's Input_RemoveHandlers.
        if (shutDown) return;
        shutDown = true;
        gameShutdown(g);
        audio.finish();
    };
    RunTools tools;
    try {
        gameInit(g);
        startTools(g, o, tools);
        if (!o.loadState.empty()) {
            // --gg-load-state: the moment of a DOSBox-X state instead of main's start (load_state.h).
            const GgStateFile state = GgStateFile::load(pathFromUtf8(o.loadState));
            const GgLoadedState loaded = loadGgState(g, state);
            for (const std::string& w : loaded.warnings) log::warn("--gg-load-state: {}", w);
            if (!loaded.supported)
                throw Error(std::format("--gg-load-state {}: g_state {} (CPU at {}) is not a moment the game can resume: {}",
                                        o.loadState, loaded.state, loaded.cpu, loaded.why));
            log::info("--gg-load-state: g_state {} at PIT tick {} (g_ticks {})", loaded.state, ctx.runtime.clock().interrupts(),
                      loaded.ticks);
            g.display.present();  // the state's VRAM through its DAC, before the first flush
            runFromLoadedState(g, loaded);
        } else {
            // 12e0:0b6d: g_state = 2 (a dead store), then 1.
            g.flow.state = 2;
            g.flow.state = 1;
            runMainSequence(g);
        }
        gameExit(exit_code::kNormal);  // 12e0:0b8a exit(0)
    } catch (const GgExit& e) {
        code = e.code;
    } catch (...) {
        stopTools(tools);
        shutdown();
        throw;
    }
    stopTools(tools);
    shutdown();
    return code;
}

// 12e0:0afb Game_Init
void gameInit(GgGame& g) {
    actorsInit(g);    // Actors_Init
    workshopInit(g);  // Workshop_Init
    // Hud_Init and SndRes_Init ran when the context and the sound were built (GgGame's constructor,
    // GgAudio's): neither reads or writes anything the two calls above use.
    hintInit(g);      // Hint_Init
}

// 12e0:0b19 Game_Shutdown
void gameShutdown(GgGame& g) {
    g.flow.shutdownFlag = -1;  // DS:00c0 (no reader)
    g.sound.shutdown();        // Snd_Off
    // Gfx_Shutdown (the text mode), Res_CloseFiles and Init_Shutdown (Input_RemoveHandlers) are the
    // app's: the window, the files and the runtime outlive the game.
}

// DS:00c2, the main table
std::array<StateFn, 19> mainStateTable(GgGame& g) {
    // The table's far pointers (code_runtime.md §3): image segments, most of them overlay stub entries,
    // each followed to its function (PORTING_NOTES §6).
    std::array<StateFn, 19> t{};
    for (size_t i = 0; i < t.size(); ++i) {
        const GgExe::Address p = g.exe().dsFarPtr(static_cast<uint16_t>(dg::kStateTable + 4 * i));
        if (p.seg == 0 && p.off == 0) continue;  // entry 0 (unused) and 18 (the end)
        const GgExe::Address f = g.exe().resolveCode(p);
        const auto it = mainStateFunctions().find((static_cast<uint32_t>(f.seg) << 16) | f.off);
        if (it == mainStateFunctions().end())
            throw FormatError(std::format("Seq_Run's table DS:00c2: entry {} points at {:04x}:{:04x}, no state function of v1.0", i,
                                          f.seg, f.off));
        t[i] = it->second(g);
    }
    return t;
}

// 12e0:000f Seq_Run(DS:00c2, &g_state)
int16_t runMainSequence(GgGame& g) {
    const auto table = mainStateTable(g);
    return seqRun(table, g.flow.state);
}

// 12e0:0148 State_03_Hub
int16_t state03Hub(GgGame& g, StateEntry entry) {
    FlowObjects& fo = g.flowObjects();
    int16_t result = 0;     // BP-2: 0 -> state 4, 2 -> the sign-in
    bool leave = false;     // BP-6
    bool first = true;      // BP-8: Screen_FadeOutIn on the first frame
    bool complete = false;  // SI: the complete box after the first frame
    HubResumeAt resumeAt = HubResumeAt::Frame;
    if (entry == StateEntry::Normal) {
        // 12e0:0160: the title music, loaded and started unless the sign-in left it playing (then
        // SndRes_PlayMusic would not restart it anyway), arrangement 2 on every entry.
        if (g.GgState::sound.titleMusicBank == -1) {
            g.GgState::sound.titleMusicBank = static_cast<int16_t>(g.sound.loadMusicBank(kTitleMusic));
            g.sound.playMusic(g.GgState::sound.titleMusicBank, 0, 1);
        }
        g.sound.selectArrangement(g.GgState::sound.titleMusicBank, 2);
        g.world.inBuilding = 0;        // DS:0094
        g.keyboard().setPlayMode(0);   // DS:0096
        // The screen (picture, palettes, back page, the two animations, the trophies, the cursor labels
        // and hover regions, Sprites_Reset) and Zones_AddLocalArray(8, DS:0132).
        fo.hub = std::make_unique<HubScreen>(&g.screenAnims);
        fo.hub->setup(g.video, g.exe(), g.archive(), g.cache, hubProgress(g.player));
        for (size_t i = 0; i < fo.hubZones.size(); ++i)
            fo.hubZones[i] = Zone::fromRecord(g.exe().dsBytes(static_cast<uint16_t>(dg::kHubZones + dg::kHubZonesStride * i),
                                                              Zone::kRecordSize));
        g.zones().addLocalArray(fo.hubZones);
        complete = fo.hub->complete();  // g_racesWonTotal == 15
    } else {
        // A DOSBox-X state in the frame loop: flowRestoreFromState built the screen's objects and linked
        // the zones; the state's screen is not faded.
        if (!fo.hub) throw Error("state03Hub: resumed without the hub's objects (flowRestoreFromState)");
        first = false;
        resumeAt = std::exchange(fo.hubResume, HubResumeAt::Frame);
    }
    // Leaving by an exception (exit(0) of Save & Exit, the end of a run): the hub's objects go.
    struct Cleanup {
        GgGame& g;
        bool armed = true;
        ~Cleanup() {
            if (!armed) return;
            FlowObjects& o = g.flowObjects();
            if (o.hub) o.hub->leave(g.video);
            o.hub.reset();
            g.zones().clearLocal();
        }
    } cleanup{g};

    while (!leave) {
        if (resumeAt == HubResumeAt::Frame) {
            // 12e0:03bf: a frame of 4 ticks.
            g.timers().set(0, 4);
            fo.hub->frame(g.video, g.rand);  // ScreenAnim_Frame
            if (first) {
                g.display.fadeOutIn();
                first = false;
            } else {
                g.display.flush();
            }
        }
        if (resumeAt != HubResumeAt::Input) {
            g.rt.timerWait(0);
            if (complete) {
                dlgMessageRes(g, kMsgAllBuildingsDone);
                complete = false;
            }
        }
        resumeAt = HubResumeAt::Frame;
        // 12e0:03fe Input_GetCode and the switch CS:04ee (6 codes).
        const uint16_t c = g.rt.getInputCode();
        if (c == kKeyEsc || c == kHubCodeSaveAndExit) {
            cmdSaveAndExit(g);  // exits on Yes; No and Esc go on in the hub
        } else if (c == kHubCodeSignIn) {
            leave = true;
            result = 2;
        } else if (c >= kHubCodeBuilding0 && c < kHubCodeBuilding0 + kBuildingCount) {
            const int b = c - kHubCodeBuilding0;
            if (g.player.racesWon[static_cast<size_t>(b)] == 4) {
                dlgMessageRes(g, kMsgBuildingDone);
            } else {
                g.player.building = static_cast<int16_t>(b);  // DS:4592
                leave = true;
            }
        }
    }
    // 12e0:047c: MouseRegion_Clear, the arrow, the cursors and labels freed, ScreenAnim_Free x 2,
    // Zones_ClearLocal, Pal_FadeOut.
    cleanup.armed = false;
    fo.hub->leave(g.video);
    fo.hub.reset();
    g.zones().clearLocal();
    g.display.fadeOut();
    return result;
}

// 12e0:012f State_16_SaveAndLeave
int16_t state16SaveAndLeave(GgGame& g) {
    playersSaveCurrent(g, GgSaveMoment::SaveAndLeave);  // the building's block too while g_inBuilding
    buildingFree(g);
    return g.flow.leaveRequest;  // DS:455a: 2 the sign-in, 3 the hub
}

// 12e0:0141 State_17_End
int16_t state17End(GgGame&) { return 0; }

// 12e0:05d5 Building_Free
void buildingFree(GgGame& g) {
    actorsFreePlayerSets(g);   // Actors_FreePlayerSets
    chimpsFree(g);             // Chimps_Free
    g.zones().clearLocal();    // Zones_ClearLocal
    menuSetMenuBar(g, false);  // Menu_SetMenuBar(NULL)
    roomLeave(g);              // Room_Leave
    roomsFree(g);              // Rooms_Free
    actorsLeaveRoom(g);        // Actors_LeaveRoom
    partsFreeCatalogue(g);     // Parts_FreeCatalogue
    // 19c0 List_Clear of the player's carried list (actor 0 +0x72, DS:7afc). The count +0x70 is not
    // cleared (code_world.md §20 bug 7, kept): a later building starts with the old count.
    g.actors.playerParts.clear();
}

// 4106:00b2 Cmd_SaveAndExit
int cmdSaveAndExit(GgGame& g) {
    const int answer = dlgYesNoRes(g, kMsgSaveAndExit);  // ui/menu_commands.h: 0x1064, default No
    if (answer == 1) {
        playersSaveCurrent(g, GgSaveMoment::SaveAndExit);  // Players_SaveCurrent
        gameExit(exit_code::kNormal);                      // exit(0)
    }
    return answer;
}

// 1b48:002e the LZH refill's key drain
void installLzhKeyDrain(GgGame& g, bool on) {
    if (!on) {
        setLzhRefillHook({});
        return;
    }
    GgKeyboard* keyboard = &g.keyboard();
    setLzhRefillHook([keyboard](int reads) {
        // After each 4000-byte read: Kbd_PopQueuedKey until it returns 0.
        for (int i = 0; i < reads; ++i) keyboard->drainQueuedKeys();
    });
}

// The flow's part of starting from a DOSBox-X state: the hub's frame loop.
bool flowRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    if (g.flow.state != 3) {
        why = std::format("g_state {} is not the hub's", g.flow.state);
        return false;
    }
    // Where State_03_Hub was (the innermost return into its code on the stack): in Timer_Wait (03ee),
    // in Input_GetCode (0403), drawing (ScreenAnim_Frame 03d0, Screen_FadeOutIn 03db, Dirty_Flush 03e7:
    // the frame is drawn anew). A dialog (03fb the complete box, 0426 Save & Exit, 0472 a building
    // done) or the set-up cannot be resumed.
    std::optional<uint16_t> ret;
    for (const auto& [seg, off] : s.stackReturns())
        if (seg == 0x12e0 && off >= 0x0148 && off < 0x04ee) {
            ret = off;
            break;
        }
    if (!ret) {
        why = "no return into State_03_Hub (12e0:0148) on the stack";
        return false;
    }
    HubResumeAt at = HubResumeAt::Frame;
    switch (*ret) {
        case 0x03ee: at = HubResumeAt::Wait; break;
        case 0x0403: at = HubResumeAt::Input; break;
        case 0x03d0:
        case 0x03db:
        case 0x03e7: at = HubResumeAt::Frame; break;
        default:
            why = std::format("State_03_Hub at 12e0:{:04x}: not in its frame loop (a dialog or the set-up)", *ret);
            return false;
    }

    FlowObjects& fo = g.flowObjects();
    fo.hub = std::make_unique<HubScreen>(&g.screenAnims);
    fo.hub->setupResumed(g.video, g.exe(), g.archive(), g.cache);
    // The two animations' run-time fields from the running list DS:7a46 (a far pointer to the list
    // record {far first, far last}; node +0 far next, the record of game/screen_anim.h), matched by
    // their frame bundles (+0x18).
    const auto farAt = [&](uint32_t linear) -> uint32_t {
        const auto b = s.bytesAt(linear, 4);
        if (b.size() < 4) return 0;
        const uint32_t off = b[0] | (b[1] << 8), seg = b[2] | (b[3] << 8);
        return seg * 16 + off;
    };
    const auto word = [&](uint32_t linear) -> int16_t {
        const auto b = s.bytesAt(linear, 2);
        return b.size() < 2 ? int16_t{0} : static_cast<int16_t>(b[0] | (b[1] << 8));
    };
    int restored = 0;
    if (const uint32_t list = s.farPointerAt(dg::kScreenAnims)) {
        uint32_t node = farAt(list);
        for (int guard = 0; node != 0 && guard < 32; ++guard, node = farAt(node)) {
            const auto bundle = static_cast<uint16_t>(word(node + 0x18));
            for (size_t i = 0; i < fo.hub->animCount(); ++i) {
                ScreenAnim& a = fo.hub->anim(i);
                if (a.frameBundle() != bundle) continue;
                a.restore(word(node + 0x22), word(node + 0x26), word(node + 0x1A), word(node + 0x1E), word(node + 0x20),
                          word(node + 0x08));
                ++restored;
                break;
            }
        }
    }
    if (restored != static_cast<int>(fo.hub->animCount())) {
        why = std::format("{} of the hub's {} animations are in the state's running list DS:7a46", restored, fo.hub->animCount());
        fo.hub.reset();
        return false;
    }
    for (size_t i = 0; i < fo.hubZones.size(); ++i)
        fo.hubZones[i] = Zone::fromRecord(g.exe().dsBytes(static_cast<uint16_t>(dg::kHubZones + dg::kHubZonesStride * i),
                                                          Zone::kRecordSize));
    g.zones().addLocalArray(fo.hubZones);
    // The title music: the state's handle names a bank of the original's bank layer; the engine's is
    // loaded and started now (from its beginning), arrangement 2.
    if (g.GgState::sound.titleMusicBank != -1) {
        g.GgState::sound.titleMusicBank = static_cast<int16_t>(g.sound.loadMusicBank(kTitleMusic));
        g.sound.playMusic(g.GgState::sound.titleMusicBank, 0, 1);
        g.sound.selectArrangement(g.GgState::sound.titleMusicBank, 2);
    }
    fo.hubResume = at;
    return true;
}

}  // namespace tmport::gg
