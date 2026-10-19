#pragma once
// Headless reference composer for the 1990 DOS edition: rebuilds the screens of the scoping pass
// (re/dos/docs/SCOPING.md §3) from the user's DOS folder with the engine's drawing code, for
// pixel comparison with the DOSBox-X reference captures (tmport --dos-screen, dos_gfx_cli.h).
//
// The screens follow the original painters for fixed inputs: TitleStep_DrawTitle 4ba4:0045 and the
// title loop's sprites (4ba4:0434), SignIn_Draw 47c7:03cf, Hud_Redraw 130f:07a5 with the scenery and
// the actors (4306:0497), Quiz_DrawScreen 46ce:0271. The game modules of wave 3 own the real
// painters and their state; this composer only takes the inputs a capture needs (camera, HUD values,
// sprite frames and positions, the USERS.DAT record, the puzzle) and draws.

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dos/data/dos_exe.h"
#include "dos/data/dos_files.h"
#include "dos/data/puzzles.h"
#include "dos/gfx/back_buffer.h"
#include "dos/gfx/pak.h"
#include "dos/gfx/palettes.h"
#include "dos/gfx/pan.h"
#include "dos/text/dos_font.h"

namespace tmport::dos {

// The DOS folder (dos_data's DosFiles: SST.EXE and every file of its file table, checked) with the
// font and the six PAN and four PAK files decoded.
class DosGfxFiles {
public:
    // Throws MissingFileError / FormatError / std::runtime_error naming the file.
    static DosGfxFiles open(const std::filesystem::path& dir);

    const DosFiles& files() const { return files_; }
    const DosExe& exe() const { return files_.exe(); }
    const DosFont& font() const { return font_; }
    // "OPENING", "MOUNTAIN", "MOUNT2N3", "MOUNT3", "CLOSE", "SHACK" / "SPRITES", "BEGIN", "CLOSE",
    // "SHACK" (any case); throws std::out_of_range for another name.
    const PanFile& pan(std::string_view stem) const;
    const PakFile& pak(std::string_view stem) const;

private:
    explicit DosGfxFiles(DosFiles files) : files_(std::move(files)) {}

    DosFiles files_;
    DosFont font_;
    std::map<std::string, PanFile> pans_;
    std::map<std::string, PakFile> paks_;
};

// The six screens of the scoping pass, and three UI drawings (code_ui.md §4, §5, §9.3): an open
// drop-down menu or a message box over a background, and the sign-in's name entry.
enum class DosScreen { Title, SignIn, Clubhouse, Quiz, Path, Castle, Menu, MsgBox, NameEntry };
// title, signin, clubhouse, quiz, path, castle, menu, msgbox, newname
std::optional<DosScreen> parseDosScreen(std::string_view name);
std::string_view dosScreenName(DosScreen s);

// A sprite of the actor table: record `slot` (0 = mouse pointer, 1 = player, ...), frame code
// (sprite index * 2 + mirror bit), screen position in bytes / rows, depth.
struct ActorSpec {
    int slot = 1;
    uint16_t frame = 0xFFFF;
    int x = 0, y = 0, z = 0;
};

struct DosScreenSpec {
    DosScreen screen = DosScreen::Title;
    // Title: sparkle (slot 2) at spot k of DS:8A18/DS:8A30 showing BEGIN frame `sparkleFrame`
    // (-1 = none), the mouse pointer (slot 0, BEGIN frame 2) at mouse x bytes / y (none if empty).
    int sparkleSpot = -1, sparkleFrame = 0;
    std::optional<std::pair<int, int>> pointer;
    // Sign-in roster and the saved game of the game screens: a USERS.DAT file, else the image in
    // usersImage (the engine's DOS save, dos/save/users_store.h), else the folder's USERS.DAT if any.
    std::filesystem::path usersFile;
    std::vector<uint8_t> usersImage;
    int usersSlot = 1;          // record whose saved hiding places / castle layout fill the grid
    bool restoreGrid = false;   // apply the record to the block grid (quiz, path, castle)
    std::vector<std::pair<int, uint16_t>> cellPictures;  // then these (cell, picture) changes, e.g. a gate not yet opened
    // Game screens: scenery and HUD values.
    std::string pan = "MOUNTAIN";
    uint16_t camX = 0, camY = 0;
    int coins = 0, nets = 0, found = 0, keyState = 8, prompt = 0, tier = 0, level = 0, foundMask = 0;
    std::array<int, 3> clueWords{0x80, 0x80, 0x80};  // word index per clue slot, >= 0x80 = none
    std::vector<ActorSpec> actors;
    // Quiz: puzzle index into ALL_PUZS, display order (DS:5E9C), answer mask.
    int puzzle = 0, order = 0, mask = 7;
    // Menu: drop-down `menu` (0..2) with item `item` highlighted. MsgBox: record `box` of DS:59CC with
    // `button` selected (default: the record's). Both drawn over `background` (320x200 logical
    // colours, one byte per pixel; empty = colour 0).
    int menu = 0, item = 0, box = 0;
    std::optional<int> button;
    std::vector<uint8_t> background;
    // NameEntry: the typed name and the block cursor's blink phase (DS:C6CA).
    std::string typedName;
    bool cursorOn = true;
};

// The inputs that reproduce the reference capture of `screen` in `look` (REFERENCE_CAPTURES.md):
// title ega/001, tandy/01, mcga/01, cga/01 (sparkle and pointer as seen in each); signin ega/004
// (the folder's shipped USERS.DAT; tandy/06, mcga/04 and cga/05 need the USERS.DAT snapshot of
// that run as usersFile); clubhouse ega/008, tandy/21; quiz ega/030, path ega/042 and castle ega/099
// (record 1 of USERS_after_exit_level2_Ada.DAT as usersFile). Camera and HUD values are the scoping
// pass's (tile matching and status-bar fit, extracted/scratch/dos_scoping); the clue words, the
// player frames and positions of path and castle were found by matching the captures.
DosScreenSpec defaultScreenSpec(DosScreen screen, Look look);

// Draws the screen into a fresh back buffer.
BackBuffer composeScreen(const DosGfxFiles& files, const DosScreenSpec& spec);

// --- helpers the composer needs from other areas (stand-ins until integration) -----------------

// The block grid DS:0CAA as stored in SST.EXE (the level-1 layout).
std::vector<uint16_t> initialCellPictures(const DosExe& exe);
// The picture part of Game_Restore 47c7:0c62 for a 360-byte USERS.DAT record, then
// Layout_MirrorSharedCells 1a54:06dc, Castle_ApplyLayout 1a54:08dc and Cells_AdjustForCpu
// 1a54:0803 (fast CPU) as Game_Start runs it.
// TODO(integration): replace with the game layer's Game_Restore once wave 3 provides it.
void restoreCellPictures(const DosExe& exe, std::span<const uint8_t> record, std::vector<uint16_t>& pictures);
// Question `index` of ALL_PUZS expanded with PRG_DICT and split (dos_data's PuzzleFile, puzzles.h).
Puzzle loadPuzzle(const DosGfxFiles& files, int index);

}  // namespace tmport::dos
