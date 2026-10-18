#pragma once
// Club Sign-In (segment 1160 signin.cpp; re/docs/code_1150-1160.md §5 with the capture-verified
// corrections of REFERENCE_CAPTURES.md §4.8, ARCHITECTURE.md §2.5).
//
// SignIn_Run (1160:00ee): the roster (7 users.dat slots) is read, or, when a player is still
// signed in ("Start Another Game"), that player is saved first (Users_SaveCurrent); the backdrop
// is PMAP 2100 on restart, else whatever the back page holds (the title picture); the wood panel
// PMAP 2180 (BNDL 322) at (27,54); then SignIn_Step repeats until a name is chosen:
//   * the player list (rows 25 px from y 57: name, Trainee / stars / Champion by rank, total with
//     thousands commas), the "Start New Player" row; keys Up/Down (wrap), Enter, Ctrl+R (remove,
//     the file is written even after "No"), Esc (exit prompt DS:2234 -> QuitGame), any
//     printable key starts a new name; mouse hot spots 0x4AD (new row) / 0x4AE (player rows);
//   * the non-modal name-entry box (DlgDesc DS:221a, box X 105 W 301) with the name field
//     (140,182,192,36), a caret blinking every 333 ms (timer 0), max 8 characters, the first one
//     upper-cased, a case-sensitive duplicate check (DS:2129) and the list-full box (DS:2200).
// The result is the chosen record (Users_LoadSelected) or a new player (Users_InitNewPlayer),
// applied to AppContext::state when set, with the sound quirk (sign-in can switch Music and Sound
// off but never on; a new player switches it on) through AppContext::audio.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/app_context.h"
#include "game/screens_common.h"
#include "gfx/pixmap.h"
#include "save/user_record.h"
#include "save/users_file.h"

namespace tmport {
class DeadlineTimers;
}

namespace tmport::game {

struct GameTables;

// Fixed facts of 1160 (code constants, not game data).
namespace signin {
inline constexpr int kMaxPlayers = 7;            // users.dat slots
inline constexpr int kMaxNameLength = 8;
inline constexpr int kArtBndl = 322;             // PMAP 2180 panel, 2181/2182 row bars, 2183 bottom
inline constexpr int kBackdropPmap = 2100;       // restart backdrop at (0,20)
inline constexpr int kPanelX = 27, kPanelY = 54, kPanelW = 296, kPanelH = 202;
inline constexpr int kBottomY = 288, kBottomH = 96;  // SignIn_ClearPanel / DrawBottomPanel (0x120, 0x60)
inline constexpr int kListX = 35, kListY = 57;   // row bars
inline constexpr int kNameX = 40, kRankX = 195, kStarDx = 8, kRowTextDy = 3, kNewRowTextDy = 1;
inline constexpr int kTotalRightPad = 31;        // totals are right-aligned at rowW + 31
inline constexpr int kBoxX = 140, kBoxY = 182, kBoxW = 192, kBoxH = 36;  // name field
inline constexpr int kBoxTextX = 152, kBoxTextY = 191;
inline constexpr int kCaretX = 154, kCaretY = 191, kCaretW = 8, kCaretH = 10;
inline constexpr int kCaretSlot = 0, kCaretMs = 333;  // 0x14d
// Hot spots (ids double as input codes).
inline constexpr uint16_t kHotNewRow = 0x4AD;    // "Start New Player" row
inline constexpr uint16_t kHotRows = 0x4AE;      // the player rows
inline constexpr uint16_t kHotEsc = 0x4AF;       // name entry: the footer line (41,253,424,23)
inline constexpr uint16_t kHotEnter = 0x4B0;     // name entry: "Then press Enter" (198,155,88,19)
inline constexpr int kMusicZone = -94;           // OPEN2

// 1160:0000 ULongToDec: decimal digits of v ("0" for 0).
std::string ulongToDec(uint32_t v);
// 1160:0cdb SignIn_DrawNumberRight's string: thousands commas for 1..7 digits; a value with 8 or
// more digits gives an empty string (quirk kept, PORTING_NOTES.md §6.1).
std::string formatTotal(uint32_t v);
// Row top y, computed in 8 bits like the original ((uint8_t)(row * rowH + 57)).
int rowY(int row, int rowH);
// 1160:0b1b: what the rank column shows for a rank 0..7 (Rank_FromTotal 10e8:07a6).
struct RankMark {
    enum Kind { Trainee, Stars, Champion } kind = Trainee;
    int stars = 0;  // Stars: 1..6
};
RankMark rankMark(int rank);
// Hot spot 0x4AE: the row under mouse y, as the original computes it ((int8_t)((y - 57) / rowH),
// signed division); clamped to 0..count-1 for a button press (0x84AE / 0x44AE), not for a release.
int rowFromMouseY(int mouseY, int rowH);
int rowFromMouseYClamped(int mouseY, int rowH, int count);
}  // namespace signin

// g_playerName (DS:224e, char[9]) with the insertion point g_nameCursor (DS:616a): '_' marks an
// empty cell; "________" = nobody signed in.
class NameBuffer {
public:
    std::array<char, save::kNameSize> bytes{'_', '_', '_', '_', '_', '_', '_', '_', 0};
    int cursor = 0;

    // strcpy(g_playerName, "________"); g_nameCursor = g_playerName.
    void reset();
    bool empty() const { return bytes[0] == '_'; }
    // Bytes up to the first NUL (at most 9).
    std::string string() const;
    // The list's type-to-create (1160:076a): *g_nameCursor++ = c (already upper-cased).
    void put(uint8_t c);
    // SignIn_NameKey 0x08: one cell back. False if at the start.
    bool backspace();
    // SignIn_NameKey printable path: c < 0xFF, printable, not '_', and room left. False if refused.
    bool append(unsigned c, const CType& ct);
    // SignIn_NameKey Enter path, first part: trailing spaces become '_', a NUL ends the name,
    // the cursor moves to the NUL.
    void trimForSubmit();
    // Duplicate name: the NUL becomes '_' again (if the cursor is before the 8th cell).
    void restoreAfterDuplicate();
    // SignIn_DrawNameBox: g_playerName[0] = toupper(g_playerName[0]).
    void upperFirst(const CType& ct) { bytes[0] = static_cast<char>(ct.toUpper(static_cast<uint8_t>(bytes[0]))); }
    // SignIn_DrawCaret: index of the first '_' (8 if none in the first 8 cells).
    int caretIndex() const;
};

// The caret blink of SignIn_EnterNewName: colour 0 (black) at the start, XOR 15 every time
// timer 0 is found expired (polled once per loop pass after the input fetch), then re-armed
// with 333 ms; a late poll delays every later toggle (no catch-up).
class CaretBlink {
public:
    void start(DeadlineTimers& timers);
    // expired = the result of Timer_Expired(0) of this pass. True when the caret must be redrawn.
    bool update(bool expired, DeadlineTimers& timers);
    int colour() const { return colour_; }

private:
    int colour_ = 0;
};

// The outcome of a sign-in.
struct SignInResult {
    bool newPlayer = false;    // Users_InitNewPlayer (room 0xFE: the climb loop starts a new climb)
    int userSel = 0;           // DS:6171: the slot Users_SaveCurrent writes
    std::string name;          // the signed-in name (raw bytes)
    save::PlayerRecord record; // existing player: the loaded record; new player: new-player defaults
    save::UserSlot slot;       // existing player: the raw slot as loaded (untouched bytes included)
};

class SignInScreen {
public:
    explicit SignInScreen(AppContext& ctx);

    // 1160:00ee SignIn_Run(restart). restart = the original's setjmp value 1 ("Start Another
    // Game"); 0 at program start. Throws QuitGame when the player confirms the exit prompt.
    SignInResult run(bool restart);

    // 1160:0f28 Users_SaveCurrent: no-op unless AppContext::state has a player signed in; re-reads
    // users.dat, merges the player into slot userSel (appending if new), sorts, re-finds the
    // player, writes. Shows the "Write Error" box (DS:2084) when the write fails.
    void saveCurrent();

    // DS:6171 g_userSel (persists between sign-in and the later saves).
    // TODO(integration): the game area may want this in GameState (PlayerState).
    int userSel() const { return userSel_; }
    void setUserSel(int s) { userSel_ = s; }

    // Users_SaveCurrent with an explicit record (no GameState needed); updates userSel.
    // `table` receives the table as merged (and written when possible).
    void saveRecord(const save::PlayerRecord& player, save::UsersTable* table = nullptr);

    void setTrace(ScreenTrace t) { io_.setTrace(std::move(t)); }

private:
    // 1160 helpers, in the original's order.
    void loadUsers();                      // 1160:0dd6 Users_Load
    void loadArt();                        // 1160:0e97 SignIn_LoadArt
    void freeArtAndFlush();                // 1160:0f1f (+ 0f00: Music_StopScene)
    void drawBackground();                 // 1160:0e2d SignIn_DrawBackground
    void step();                           // 1160:029c SignIn_Step
    void drawList();                       // 1160:051b SignIn_DrawList
    void listLoop();                       // 1160:0595 SignIn_ListLoop
    void drawRow(int row, bool normal);    // 1160:0b1b SignIn_DrawRow
    void drawNumberRight(int rightX, int y, uint32_t v);  // 1160:0cdb
    void clearPanel();                     // 1160:0834 SignIn_ClearPanel
    void drawBottomPanel();                // 1160:0869 SignIn_DrawBottomPanel
    void removePlayer(int index);          // 1160:089f SignIn_RemovePlayer
    void confirmExit();                    // 1160:04f6 SignIn_ConfirmExit
    void enterNewName();                   // 1160:02e0 SignIn_EnterNewName
    void nameKey(uint16_t code);           // 1160:0957 SignIn_NameKey
    void drawNameBox();                    // 1160:0ab0 SignIn_DrawNameBox
    void drawCaret(int colour);            // 1160:0488 SignIn_DrawCaret
    void loadSelected();                   // 1160:137d Users_LoadSelected
    void initNewPlayer();                  // 1160:1632 Users_InitNewPlayer
    bool writeUsers();                     // 1158:00bb Users_WriteFile (+ the Write Error box)
    void listFullBox(bool forceCursor);
    void syncOptionsFromAudio();

    ScreenIo io_;
    CType ctype_;
    std::shared_ptr<const GameTables> rankTables_;  // DS:0ea0 thresholds for Rank_FromTotal
    save::UsersTable table_;                    // g_users + g_userCount
    int userSel_ = 0;                           // g_userSel (DS:6171, signed byte)
    NameBuffer name_;                           // g_playerName + g_nameCursor
    CaretBlink caret_;
    bool nameDone_ = false;                     // g_nameDone (DS:6176)
    std::shared_ptr<const gfx::PixMap> panel_, rowNormal_, rowHilite_, bottom_;  // BNDL 322
    int rowW_ = 280, rowH_ = 25;                // g_rowW / g_rowH (from PMAP 2181)
    std::optional<SignInResult> result_;
};

// Convenience: SignInScreen(ctx).run(restart) (userSel starts at 0).
SignInResult runSignIn(AppContext& ctx, bool restart);

}  // namespace tmport::game
