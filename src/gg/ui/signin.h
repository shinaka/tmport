#pragma once
// The sign-in screen and its roster list (state 2, ovl07 2caa; re/gg/docs/code_ui.md §11): the
// screen, the rows of the list, the three picture buttons and the scroll bar, and the roster model
// that typing drives (Roster_UpdateTyped, SignIn_HandleCode). The polling loop around it
// (SignIn_Run 2caa:0eb3: the pressed pictures of the buttons, Esc's Save & Exit) and the state's
// ending (the chimps question, the record load or creation) are the game's (wave 3); the dialogs,
// sounds and file writes the handler needs go through SignInHost.
//
// The roster is g_roster (DS:a075): PLAYERS.DAT's header and 100 index entries {name[16], u32
// points, u16 slot}, entries 0..count-1 the players in name order (plain signed byte order), the
// rest the free slots in the order they are taken. In memory only, points -1 marks the entry being
// typed and -2 one New Player created (its name sorts as the placeholder 0x1F: first).
//
// The list: 8 rows of (62, 46 + 11 k, 180, 11); the selected row is a black bar with the name in
// colour 11 (while typing: the typed part in 14, the rest of the stored name in 11), the others the
// background from the back page with the name in 15; points right-aligned at x 164 with thousands
// commas and the rank name at x 169, in the name's colour, nothing for negative points; font 1.

#include <array>
#include <cstdint>
#include <span>

#include "gg/gfx/palette.h"
#include "gg/input/zones.h"
#include "gg/save/players_dat.h"
#include "gg/ui/button.h"
#include "gg/ui/rank.h"
#include "gg/ui/scroll_bar.h"
#include "gg/ui/ui_canvas.h"

namespace tmport::gg {

class Archive;
class GgExe;

// Pictures and texts of the sign-in screen (2caa:016e).
inline constexpr uint16_t kSignInPicture = 0x1029;
inline constexpr uint16_t kSignInPalette = 0x102A;
inline constexpr uint16_t kSignInTitle = 0x1027;  // keyed at (65, 28)
inline constexpr uint16_t kSignInEffects = 0x102F; // effect 0: a typed character
inline constexpr int kRosterRows = 8;
inline constexpr int kRosterMaxPlayers = 99;
inline constexpr int kMaxNameLength = 15;
inline constexpr int kMaxNameWidth = 60;  // Text_Width in the small font must stay below it

// Input codes of the sign-in (code_ui.md §11.4, §17).
inline constexpr uint16_t kCodeRosterList = 0x0700;   // release over the list
inline constexpr uint16_t kCodeRosterBar = 0x0701;    // press / release over the scroll bar
inline constexpr uint16_t kCodeCancel = 0x0702;
inline constexpr uint16_t kCodeNewPlayer = 0x0703;
inline constexpr uint16_t kCodeStartGame = 0x0704;

// 2caa:016e SignIn_DrawScreen: Screen_LoadPicture(0x1029, 0x102A), Lpx_Draw(0, 0), the title 0x1027
// keyed at (65, 28), Screen_SaveAll (the back page is the empty list). Returns the palette load of
// Screen_LoadPicture (empty while faded). SignIn_ShowScreen 2caa:01b3 = Font_Select(1), this,
// Screen_FadeOutIn.
PaletteLoad signInDrawScreen(Video& video, const Archive& archive);

// What SignIn_HandleCode needs from the rest of the game (all modal or outside the UI).
class SignInHost {
public:
    virtual ~SignInHost() = default;
    // 4106:006d Dlg_YesNoRes(id): 1 Yes, 0 No, -1 Esc (0x106A, remove a player).
    virtual int yesNoRes(uint16_t id) = 0;
    // 3f56:0d2a Dlg_MessageRes(id) (0x1068 duplicate name, 0x1069 list full).
    virtual void messageRes(uint16_t id) = 0;
    // SndRes_PlayEffect(g_effectBank = bank 0x102F, loop 0, priority 1): an accepted character.
    virtual void playTypeEffect() = 0;
    // 2caa:0145 Roster_Save: the header and index (0x8A0 bytes) to PLAYERS.DAT (Ctrl+R).
    virtual void saveRoster(const Roster& roster) = 0;
    // g_ticks (the click time stamps; written only) and the cursor position g_mouseX/Y DS:7a88/7a86.
    virtual uint32_t ticks() = 0;
    virtual int mouseX() = 0;
    virtual int mouseY() = 0;
};

class SignIn {
public:
    // The DGROUP records (the three picture buttons DS:20f0/2110/2130, the scroll bar DS:20dc, the
    // list and bar zones DS:2150/2166, the rank tables) and SignIn_LoadButtons' PicBtn_Load of the
    // three bundles (2caa:1382: Cancel at (250, 172), New Player at (197, 170), Start Game at (146,
    // 170)) and the scroll bar's pictures.
    SignIn(const GgExe& exe, const Archive& archive);
    // Its zones may be linked into a ClickZones list and its rank names point into the exe's DGROUP:
    // it stays where it is, and the GgExe outlives it.
    SignIn(const SignIn&) = delete;
    SignIn& operator=(const SignIn&) = delete;

    Roster& roster() { return roster_; }
    const Roster& roster() const { return roster_; }
    int count() const { return roster_.count; }
    // g_rosterIndex DS:a079 (later the signed-in player's entry).
    int index() const { return index_; }
    // g_typedName DS:a054 / g_typedLen DS:a071 (-1 right after entry).
    int typedLength() const { return typedLen_; }
    std::span<const uint8_t> typedBytes() const;  // the first typedLength() bytes
    ScrollBar& scrollBar() { return bar_; }
    PicButton& cancelButton() { return cancel_; }
    PicButton& newPlayerButton() { return newPlayer_; }
    PicButton& startGameButton() { return startGame_; }
    Zone& listZone() { return listZone_; }
    Zone& barZone() { return barZone_; }
    const RankTable& ranks() const { return ranks_; }
    // The signed value of an entry's points (-1, -2 in progress).
    static int32_t points(const RosterEntry& e) { return static_cast<int32_t>(e.points); }

    // 2caa:1240 State_02_SignIn after Roster_LoadOrCreate and SignIn_ShowScreen: Cancel enabled =
    // (count == 0), New Player and Start Game enabled = (count > 0); index 0, typed length -1; the
    // scroll bar max = max(0, count - 8), next = now, pos 0.
    void begin(const Roster& roster, uint32_t now);
    // 2caa:0eb3 SignIn_Run's entry: Screen_SaveAll; SignIn_DrawList(0, -1, -1); PicBtn_Draw x 3;
    // ScrollBar_Draw; the zones (list, Cancel, New Player, Start Game, bar) when `zones` is given;
    // Dirty_Flush; then (after the input flush) typed length -1 becomes 0 and an empty roster runs
    // SignIn_HandleCode(0x703) (New Player).
    void enter(const UiCanvas& ui, SignInHost& host, ClickZones* zones = nullptr);
    // SignIn_Run's exit: the zones it added are removed.
    void leave(ClickZones& zones);

    // 2caa:082e SignIn_HandleCode(code): Ctrl+R, Cancel, New Player, clicks on the list, Up / Down,
    // Backspace (the loop maps Del to 8), typed characters and Enter / Start Game. Returns 0x0D when
    // the selected entry may sign in (a player, or a new name that is not a duplicate), else 0.
    int handleCode(const UiCanvas& ui, SignInHost& host, uint16_t code);
    // 2caa:01c5 Roster_UpdateTyped(c): see code_ui.md §11.6. True when the count changed. `host` and
    // `ui` serve the list-full message (Hide/ShowButtons around Dlg_MessageRes(0x1069)).
    bool updateTyped(const UiCanvas& ui, SignInHost& host, uint8_t c);

    // 2caa:053a SignIn_DrawList(flush, a, b): b < 0 redraws every visible row, else only entries a
    // and b (and first scrolls the selection into view); clears the row below a short list; then the
    // buttons' enabled states (each change redrawn with a flush).
    void drawList(const UiCanvas& ui, bool flush, int a, int b);
    // 2caa:0000 SignIn_HideButtons (PicBtn_Erase x 3) / 2caa:0025 SignIn_ShowButtons (PicBtn_Draw x 3,
    // Dirty_Flush).
    void hideButtons(const UiCanvas& ui);
    void showButtons(const UiCanvas& ui);
    // 4034:0304/032f on one button (SignIn_Run's press feedback).
    void drawButtonPressed(const UiCanvas& ui, PicButton& b) { picBtnDrawPressed(ui, b); }
    void drawButtonNormal(const UiCanvas& ui, PicButton& b) { picBtnDrawNormal(ui, b); }

private:
    RosterEntry& entry(int i) { return roster_.entries[static_cast<size_t>(i)]; }
    bool indexValid() const { return index_ >= 0 && index_ < roster_.count; }
    void drawRow(const UiCanvas& ui, int k, int e);
    // SignIn_DrawScoreRank 2caa:04ae.
    void drawScoreRank(const UiCanvas& ui, uint32_t points, int x, int y);
    // The rescroll SignIn_HandleCode does after Backspace, typing and the trailing-space strip.
    void keepIndexVisible(const UiCanvas& ui, int& redrawFrom);

    Roster roster_;
    int index_ = 0;
    int typedLen_ = -1;
    std::array<uint8_t, 17> typed_{};  // char[16] + the byte after it that a 16th 0x1F reaches
    int clicked_ = 0;                   // g_signinClicked DS:a073 (written only)
    uint32_t lastClick_ = 0;            // g_signinLastClick DS:a06d (written only)

    PicButton cancel_, newPlayer_, startGame_;
    ScrollBar bar_;
    ScrollBarPictures barPics_;
    Zone listZone_, barZone_;
    RankTable ranks_;
};

}  // namespace tmport::gg
