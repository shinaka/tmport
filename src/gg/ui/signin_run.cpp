// The sign-in on the game (signin.h, "wave 3: the run"; code_ui.md §11, §12.1-§12.3). Owner: ui agent.
//
// State 2 builds the screen (UiObjects::signIn, bound to GgState::ui), runs SignIn_Run's busy poll and
// then signs the selected player in: a new one gets a blank record, the chimps question and both saves
// (the record, then the roster); an existing one is loaded from PLAYERS.DAT.
#include "gg/ui/signin.h"

#include <algorithm>
#include <format>

#include "gg/app/seq_run.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/game/flow.h"
#include "gg/game/game.h"
#include "gg/save/gg_state_file.h"
#include "gg/ui/dialog.h"
#include "gg/ui/players.h"
#include "gg/ui/ui_objects.h"
#include "gg/ui/ui_run.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

constexpr uint16_t kTitleMusic = 0x1020;  // g_titleMusicBank DS:0098's bank (the sign-in, the hub)

// The sign-in's dialogs, typing effect, roster save, clock and pointer on the game.
class GameSignInHost final : public SignInHost {
public:
    explicit GameSignInHost(GgGame& g) : g_(g) {}
    int yesNoRes(uint16_t id) override { return dlgYesNoRes(g_, id); }
    void messageRes(uint16_t id) override { dlgMessageRes(g_, id); }
    // SndRes_PlayEffect(g_effectBank + 0, loop 0, priority 1) (2caa:0c87)
    void playTypeEffect() override { g_.sound.playEffect(g_.GgState::sound.effectBank, 0, 1); }
    // Ctrl+R's Roster_Save (the roster is GgState::ui.roster: the screen is bound to it)
    void saveRoster(const Roster&) override { rosterSave(g_, GgSaveMoment::RemovePlayer); }
    uint32_t ticks() override { return g_.ticks(); }
    int mouseX() override { return g_.mouse().x(); }  // g_mouseX DS:7a88
    int mouseY() override { return g_.mouse().y(); }  // g_mouseY DS:7a86

private:
    GgGame& g_;
};

}  // namespace

SignInVars signInVarsOf(UiState& ui) {
    SignInVars v;
    v.roster = &ui.roster;
    v.index = &ui.rosterIndex;
    v.typedLen = &ui.typedLen;
    v.typed = ui.typedName.data();
    v.typed16 = &ui.typedName16;
    v.clicked = &ui.signinClicked;
    v.lastClick = &ui.signinLastClick;
    v.prevClick = &ui.signinPrevClick;
    v.arrowRepeat = &ui.signinArrowRepeat;
    v.arrowCount = &ui.signinArrowCount;
    v.bar = &ui.rosterScrollBar;
    return v;
}

std::unique_ptr<SignInHost> makeSignInHost(GgGame& g) { return std::make_unique<GameSignInHost>(g); }

// 2caa:004f Roster_LoadOrCreate
void rosterLoadOrCreate(GgGame& g) {
    // The original opens players.dat at every sign-in: the store reads the engine's file again, or
    // creates it (missing, or another version: the old one is kept as .bak) as Roster_LoadOrCreate does.
    // A file the store created at start-up and nothing wrote since counts as created here.
    const bool createdAtStart = g.players.status() != PlayersStatus::Ok && g.players.writes() <= 1;
    const PlayersStatus st = g.players.load();
    g.ui.roster = g.players.roster();  // Players_Read(g_roster, 0x8a0)
    if (st != PlayersStatus::Ok || createdAtStart) {
        // The creation's loop leaves g_player the blank record of the last slot it saved (2caa:0101..011a:
        // Player_InitRecord, then g_playerSlot = 0..98 with a Players_SaveCurrent each).
        playerInitRecord(g);
        g.player.slot = static_cast<int16_t>(kPlayerRecordCount - 1);
        if (g.ui.rosterIndex != 0)
            log::warn("Roster_LoadOrCreate: the file was created with g_rosterIndex {}; the store's file has the "
                      "start-up anomaly (entry 0)",
                      g.ui.rosterIndex);
        if (st == PlayersStatus::WriteFailed) playersWriteError(g);
    }
}

// 2caa:0145 Roster_Save
void rosterSave(GgGame& g, GgSaveMoment moment) {
    if (!g.players.saveRoster(moment, g.ui.roster)) playersWriteError(g);  // Players_Write's failure
}

// 2caa:0eb3 SignIn_Run
uint16_t signInRun(GgGame& g, SignIn& s) {
    const UiCanvas ui = g.canvas();
    const std::unique_ptr<SignInHost> host = makeSignInHost(g);
    s.show(ui, &g.zones());
    OnUnwind unwind([&] { s.leave(g.zones()); });
    g.rt.flushInput();
    while (g.rt.getInputCode() != 0) {
    }
    s.start(ui, *host);

    // SI: the picture button shown pressed (1 Cancel, 2 New Player, 3 Start Game; 0 none).
    PicButton* const buttons[4] = {nullptr, &s.cancelButton(), &s.newPlayerButton(), &s.startGameButton()};
    int pressed = 0;
    // A press or a held poll over button k (2caa:100b, 108b, 1113): the other button shown pressed is
    // drawn *pressed* again (the original's PicBtn_DrawPressed, kept: it stays pressed on the screen),
    // then button k pressed unless it is disabled or already shown pressed.
    const auto press = [&](int k) {
        for (int j = 1; j <= 3; ++j)
            if (j != k && pressed == j) picBtnDrawPressed(ui, *buttons[j]);
        if (!buttons[k]->enabled || pressed == k) return;
        pressed = k;
        picBtnDrawPressed(ui, *buttons[k]);
    };
    // A release over button k (2caa:0fd7, 1049, 10c9): the other shown-pressed button normal, then
    // button k normal and its code handled, unless k is disabled (then the code is ignored entirely).
    const auto release = [&](int k) -> bool {
        for (int j = 1; j <= 3; ++j)
            if (j != k && pressed == j) picBtnDrawNormal(ui, *buttons[j]);
        pressed = 0;
        if (!buttons[k]->enabled) return false;
        picBtnDrawNormal(ui, *buttons[k]);
        return true;
    };

    uint16_t code = 0;
    for (bool done = false; !done;) {
        code = g.rt.getInputCode();  // the busy poll
        if (code == 0) {
            s.idlePoll();  // 2caa:0f86
            continue;
        }
        switch (code) {  // the table CS:1210
            case 0x4706: case 0x8706: press(1); break;  // Cancel pressed / held
            case 0x4707: case 0x8707: press(2); break;  // New Player
            case 0x4708: case 0x8708: press(3); break;  // Start Game
            case kCodeCancel:
                if (release(1)) s.handleCode(ui, *host, code);
                break;
            case kCodeNewPlayer:
                if (release(2)) s.handleCode(ui, *host, code);
                break;
            case kCodeStartGame:
            case 0x000D:  // Enter: ignored while Start Game is disabled
                if (release(3) && s.handleCode(ui, *host, code) == 0x0D) done = true;
                break;
            case 0x001B:
                // Esc: Save & Exit with saving disabled (Yes leaves to DOS without writing anything).
                s.hideButtons(ui);
                g.ui.saveDisabled = 1;
                cmdSaveAndExit(g);
                g.ui.saveDisabled = 0;
                s.showButtons(ui);
                break;
            case 0x0705: break;  // SndRes_Nop (no zone or key gives 0x705)
            default: {
                for (int j = 1; j <= 3; ++j)
                    if (pressed == j) picBtnDrawNormal(ui, *buttons[j]);
                pressed = 0;
                if ((code & 0x0FFF) == kCodeRosterBar) {
                    // ScrollBar_HandleClick (ovl30) on every poll of a press, held or release over the
                    // bar; on the thumb its drag polls Input_GetCode while the code has bit 15.
                    ScrollBar& bar = s.scrollBar();
                    const int mouseY = g.mouse().y();
                    if (const auto grab = scrollBarHandleClick(ui, s.barPictures(), bar, code, mouseY, g.ticks())) {
                        int dragY = mouseY;
                        // The thumb register (DI) holds SignIn_Run's input code until the pointer moves:
                        // a click on the thumb without a move scrolls to the end (code_ui.md §9; kept).
                        int thumb = code;
                        uint16_t c = 0;
                        do {
                            c = g.rt.getInputCode();
                            scrollBarDragMove(ui, bar, *grab, g.mouse().x(), g.mouse().y(), dragY, thumb);
                        } while (c & code::kStillHeld);
                        scrollBarDragEnd(ui, s.barPictures(), bar, thumb, g.ticks());
                    }
                    s.drawList(ui, true, -1, -1);
                } else {
                    if (code == 0x153) code = 8;  // Del is Backspace
                    if (s.handleCode(ui, *host, code) == 0x0D) done = true;
                }
                break;
            }
        }
    }
    s.leave(g.zones());
    return code;
}

// 2caa:1240 State_02_SignIn
int16_t state02SignIn(GgGame& g) {
    SoundState& snd = g.GgState::sound;
    g.keyboard().setPlayMode(0);  // g_kbdPlayMode
    g.world.inBuilding = 0;       // g_inBuilding
    rosterLoadOrCreate(g);
    if (snd.titleMusicBank == -1) snd.titleMusicBank = static_cast<int16_t>(g.sound.loadMusicBank(kTitleMusic));
    // SignIn_LoadButtons: the three picture buttons, the effect bank 0x102f, the title music
    // (section 0, looping) in arrangement 1.
    UiObjects& o = g.uiObjects();
    o.signIn = std::make_unique<SignIn>(g.exe(), g.archive());
    SignIn& s = *o.signIn;
    s.bind(signInVarsOf(g.ui));
    snd.effectBank = static_cast<int16_t>(g.sound.loadEffectBank(kSignInEffects));
    g.sound.setEffectBank(snd.effectBank);
    g.sound.playMusic(snd.titleMusicBank, 0, 1);
    g.sound.selectArrangement(snd.titleMusicBank, 1);
    // SignIn_ShowScreen: font 1, the screen, Screen_FadeOutIn
    g.fonts.select(1);
    g.display.runLoad(signInDrawScreen(g.video, g.archive()));
    g.display.fadeOutIn();  // Screen_FadeOutIn
    s.begin(g.ui.roster, g.ticks());

    const uint16_t code = signInRun(g, s);
    // (SignIn_Run returns only after Enter or Start Game signed somebody in: the test of 0x1b at 2caa:12db
    // never holds.)
    if (code != 0x1B) {
        RosterEntry& entry = g.ui.roster.entries.at(static_cast<size_t>(g.ui.rosterIndex));
        if (SignIn::points(entry) < 0) {
            // A new player: the blank record, the chimps question (no Esc), the entry's 22 bytes into the
            // record (its name, points 0, its slot), then the record and the whole index are saved.
            playerInitRecord(g);
            g.player.withChimps = dlgAskChimps(g) != 0 ? 1 : 0;
            entry.points = 0;
            g.player.name = entry.name;
            g.player.points = entry.points;
            g.player.slot = static_cast<int16_t>(entry.slot);
            playersSaveCurrent(g, GgSaveMoment::NewPlayer);
            rosterSave(g, GgSaveMoment::NewPlayer);
        } else {
            // A player: the record, and the record's sound option becomes the live switch.
            playersLoadRecord(g, entry.slot);
            g.sound.setSoundMuted(g.player.soundOff);  // DS:1ad4 = g_soundOff; SndRes_ApplySoundOption
        }
    }
    g.display.fadeOut();
    // SignIn_Free: the roster copy (GgState keeps it), the three buttons' pictures, the effect
    // bank (DS:7a44 keeps the freed handle, as in the original). The title music plays on into the hub.
    o.signIn.reset();
    g.sound.freeEffectBank(snd.effectBank);
    return 0;
}

// The UI's part of starting from a DOSBox-X state (game/load_state.h).
bool uiRestoreFromState(GgGame& g, const GgStateFile& s, std::string& why) {
    UiObjects& o = g.uiObjects();
    // A modal UI loop cannot be re-entered from a state: the drop-down (g_menuNesting), or a box, the
    // help pager or the text viewer (each pushes the local zones: g_zoneStackDepth above the state's own
    // pushes: State_09_Puzzle's and Blueprint_Open's).
    if (static_cast<int16_t>(s.dsWord(dg::kMenuNesting)) != 0) {
        why = "a drop-down menu is open in the state (Menu_RunDropDown cannot be resumed)";
        return false;
    }
    const int depth = static_cast<int16_t>(s.dsWord(dg::kZoneStackDepth));
    int own = 0;
    if (g.flow.state == 9) ++own;                                 // State_09_Puzzle's Zones_Push
    if (static_cast<int16_t>(s.dsWord(dg::kBlueprintOpen)) != 0) ++own;  // Blueprint_Open's
    if (depth > own) {
        why = std::format("a dialog box, the help pager or the text viewer is open in the state (zone stack depth {}, the "
                          "state's own {}): its modal loop cannot be resumed",
                          depth, own);
        return false;
    }
    // The roster copy g_roster DS:a075: a heap block while the sign-in runs (freed by SignIn_Free after).
    if (g.flow.state == 2) {
        if (const uint32_t at = s.farPointerAt(dg::kRoster); at != 0) {
            const auto bytes = s.bytesAt(at, kRosterSize);
            if (bytes.size() == kRosterSize) g.ui.roster = Roster::fromBytes(bytes.first<kRosterSize>());
        }
    }
    // The installed menu bar (g_menuBarCur = DS:4053): its records, the title zones (global) and the hook
    // g_menuHandler when the state has it set; the bar itself is on the state's screen already.
    if (g.ui.menuBarCur.off == dg::kMenuBar) {
        if (!o.menuBar) o.menuBar = MenuBarDef::fromExe(g.exe(), dg::kMenuBar);
        if (!o.menuPictures) o.menuPictures = MenuBarPictures::load(g.archive());
        if (o.titleZonesLinked)
            for (Zone& z : o.titleZones) g.zones().removeGlobal(&z);
        o.titleZones = menuTitleZones(*o.menuBar);
        for (Zone& z : o.titleZones) g.zones().addGlobal(z);
        o.titleZonesLinked = true;
        if (s.dsDword(dg::kMenuHandler) != 0) installMenuHook(g);
    }
    why.clear();
    return true;
}

}  // namespace tmport::gg
