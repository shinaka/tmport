#pragma once
// GgState: every run-time global of ssg.exe's game logic (Gizmos & Gadgets DOS v1.0), typed and named
// after the canonical names of re/names/gg/dgroup.tsv (g_ dropped; DS_xxxx_<role> for the few globals
// that have no row there yet), each with its DGROUP address, grouped in one section per owning module
// (re/gg/docs/ENGINE_MAP.md, "Wave 3 map", lists the owners and who writes what):
//
//   player    g_player DS:4562, the PLAYERS.DAT record (written by every module, field by field)
//   flow      Seq_Run, start-up, the attract, the hub, leaving a building, the exit paths     (flow)
//   ui        dialogs, menus, help, the text viewer, the sign-in, PLAYERS.DAT, the hints        (ui)
//   world     building set-up, rooms, doors, items, the HUD strip, the warehouse loop         (world)
//   actors    the 12 actors, the step interpreter, the player, the projectile, the chimps     (actors)
//   puzzles   the puzzle framework and the eight puzzles' globals              (puzzles_a, puzzles_b)
//   vehicles  the workshop, the blueprint, the races, the results, the movie               (vehicles)
//   sound     the sound handles the game keeps                                    (each caller's)
//   runtime   the start-up values of Init_CommandLine                                         (flow)
//
// What GgState is: the values the game logic reads and writes, in one copyable object, so the engine
// can be compared with and started from DOSBox-X save states field by field (dumpToDgroup /
// loadFromDgroup) and a test can set up any scenario by assignment. Where a DGROUP global is a far
// pointer to a mutable heap structure of the game (the room set copy with its item lists and doors,
// the part catalogue with its holders and flags, the carried-part lists, the roster copy, the puzzle
// variant copies), GgState holds the structure itself (Rooms, PartCatalogue, std::vector, ...) and
// the far pointer is not a field; such structures are not in the DGROUP image (a state loader reads
// them from the state's RAM through the pointer: gg/save/gg_state_file.h).
//
// What GgState is not:
//   * read-only tables (GgTables below, loaded once from ssg.exe);
//   * the globals held by the wave 2 platform objects (GgGame reaches them, gg/game/game.h, and
//     serviceFields() dumps them): g_ticks, the timers, the keyboard and its ring, the mouse latches,
//     the click zones' lists, Input_GetCode's hooks and flags, the screen, palette, sprite list and
//     cursor, the fonts, the sound driver and bank layer with the music cue counters, PzSnd, the
//     Borland rand() state;
//   * widget records: click zones, picture buttons, cursor regions and dialog button records (their
//     DGROUP copies are initial values; the engine's live Zone / PicButton / CursorRegion objects are
//     held by the module that links them, as the original's lists link the DGROUP records);
//   * far pointers to pictures, bundles, texts, cursors and callbacks (resources and code: the module
//     that loaded them holds them).
//
// Initial values: initialState() = the exe's initialised data (DS:0000..4551, BSS zero) through
// loadFromDgroup, then the start-up init functions' values (code_runtime.md §2: Gfx_Init, Font_Init,
// PicCache_Init, Game_Init with Actors_Init, Workshop_Init, Hud_Init, SndRes_Init, Hint_Init; the
// parts of them that set GgState fields are done by gameInitState(), the rest by their modules).
//
// Parallel-work rule: an agent adds fields only to its own section (and to that section's visit()
// list, with the field's DS address), never reorders or removes others; re-read this file right
// before each edit. Fields are fixed-width (int16_t, uint16_t, uint32_t, uint8_t) as in the original,
// so its 16-bit arithmetic stays visible; the wave 2 records reused here (Actor, ItemNode,
// PlayerRecord, ScrollBar) keep their own types and are stored with the original's widths.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "gg/data/dgroup_offsets.h"
#include "gg/game/actors.h"
#include "gg/game/hud.h"
#include "gg/game/parts.h"
#include "gg/game/rooms.h"
#include "gg/save/players_dat.h"
#include "gg/ui/hints.h"
#include "gg/ui/scroll_bar.h"

namespace tmport::gg {

class GgExe;

// ------------------------------------------------------------------------------ record types
// Each record type has kDgroupSize, store/load in the original's byte layout and modelledByte(i)
// (false for the bytes the engine does not model: far pointers to resources, code and heap, unused
// words). The wave 2 records reused by GgState (Actor, ItemNode, PlayerRecord, ScrollBar, PartDeck,
// HintQueue) get the same through DgCodec<T> specialisations in game_state.cpp.

// A wave 2 field of another C++ type (int, bool) that the original keeps as a 16-bit word: visit it as
// as16(field) (stored truncated to 16 bits; loaded sign-extended for signed types, as 0/1 for bool).
template <class T>
struct As16 {
    T& v;
};
template <class T>
As16<T> as16(T& v) {
    return As16<T>{v};
}

// A pointer into DGROUP stored as a far pointer {u16 off, u16 seg 0x0EA7} (off 0 = NULL). The
// engine keeps the offset; the segment word is not modelled (a DOSBox-X state holds a relocated one).
struct DsPtr {
    uint16_t off = 0;
    static constexpr size_t kDgroupSize = 4;
    void store(uint8_t* p) const;  // segment 0x0EA7 when off != 0
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t i) { return i < 2; }
    bool operator==(const DsPtr&) const = default;
};

// The 14-byte sprite record {u16 flags, i16 x, y, w, h, far picture} without the picture (the races'
// racer sprites; the world's sprites are wave 2's Sprite with their pictures).
struct SpriteRec {
    uint16_t flags = 0;
    int16_t x = 0, y = 0, w = 0, h = 0;
    // +0x0A far picture: not modelled (the race module holds the frame it drew).
    static constexpr size_t kDgroupSize = 14;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t i) { return i < 10; }
    bool operator==(const SpriteRec&) const = default;
};

// A land / alternative-energy racer (0x3A bytes: player DS:bae8, Morty DS:baae;
// code_vehicles_races.md §9.2).
struct RacerRec {
    uint16_t id = 0;            // +0x00 1 player, 2 Morty (the bit of g_raceFinishBits)
    uint16_t field02 = 0;       // +0x02 (not read by the race code seen; modelled as found)
    SpriteRec sprite;           // +0x04
    uint32_t speed = 0;         // +0x12 path points per frame, 16.16
    int16_t index = 0;          // +0x16 path index
    // +0x18 far path: not modelled (the race module holds the path copy)
    int16_t mode = 0;           // +0x1C 0 open road, 1 limited section
    int16_t finished = 0;       // +0x1E
    int16_t laps = 0;           // +0x20 laps to run before the finish counts
    uint32_t limit = 0;         // +0x22 limit in limited sections
    uint32_t max = 0;           // +0x26 top speed
    uint32_t field2A = 0;       // +0x2A (Race_ComputePerformance intermediates; modelled as found)
    uint32_t field2E = 0;       // +0x2E
    uint32_t acc2 = 0;          // +0x32 acceleration in limited sections
    uint32_t acc = 0;           // +0x36 acceleration
    static constexpr size_t kDgroupSize = 0x3A;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t i) { return !(i >= 0x0E && i < 0x12) && !(i >= 0x18 && i < 0x1C); }
    bool operator==(const RacerRec&) const = default;
};

// An aircraft racer (0x3C bytes: player DS:bb64, Morty DS:bb28; code_vehicles_races.md §10.2).
struct AirRacerRec {
    SpriteRec sprite;                 // +0x00
    uint32_t speed = 0;               // +0x0E px per frame along the course, 16.16
    int16_t index = 0;                // +0x12
    int16_t mode = 0;                 // +0x14 flight mode 0..2, never initialised by the race (quirk §10.6)
    int16_t finished = 0;             // +0x16
    // +0x18 far path: not modelled
    std::array<uint32_t, 3> speeds{}; // +0x1C target speed per mode
    std::array<uint32_t, 3> accs{};   // +0x28 acceleration per mode
    uint32_t dist = 0;                // +0x34 distance travelled, 16.16
    uint32_t prevDist = 0;            // +0x38
    static constexpr size_t kDgroupSize = 0x3C;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t i) { return SpriteRec::modelledByte(i) && !(i >= 0x18 && i < 0x1C); }
    bool operator==(const AirRacerRec&) const = default;
};

// Electric circuits: a piece (16 bytes at DS:a2fc + 16 i) and a terminal (26 bytes at DS:a3ec + 0x4e i
// + 0x1a t) (code_puzzles_a.md §3.3, §3.4).
struct ElecPieceRec {
    int16_t type = 0;    // +0x00 0 battery, 1 bulb, 2 switch, 3 Y-wire, 4 wire
    int16_t x = 0, y = 0;
    // +0x06 far current picture: not modelled (the puzzle draws by type and state)
    int16_t used = 0;    // +0x0A
    int16_t closed = 0;  // +0x0C switch closed (also cleared for bulbs)
    int16_t fixed = 0;   // +0x0E
    static constexpr size_t kDgroupSize = 16;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t i) { return i < 6 || i >= 10; }
    bool operator==(const ElecPieceRec&) const = default;
};
struct ElecTerminalRec {
    int16_t hitX = 0, hitY = 0, hitW = 0, hitH = 0;  // +0x00 hit rectangle
    int16_t anchorX = 0, anchorY = 0;                // +0x08
    int16_t dotX = 0, dotY = 0;                      // +0x0C dot offset
    int16_t overlapH = 0;                            // +0x10
    int16_t piece = 99;                              // +0x12 linked piece (99 none)
    int16_t term = 0;                                // +0x14 linked terminal
    int16_t flag = 0;                                // +0x16 1 owns a drawn wire, 3 contact by placing, 0 passive
    int16_t field18 = 0;                             // +0x18 (not read)
    static constexpr size_t kDgroupSize = 26;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t) { return true; }
    bool operator==(const ElecTerminalRec&) const = default;
};
// Simple machines: a placed part (14 bytes at DS:b38a + 14 s; code_puzzles_a.md §6.3).
struct SimplePlacedRec {
    int16_t type = 9;  // 9 free
    int16_t x = 0, y = 0, w = 0, h = 0, testX = 0, testY = 0;
    static constexpr size_t kDgroupSize = 14;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t) { return true; }
    bool operator==(const SimplePlacedRec&) const = default;
};
// Machine pieces: a piece (18 bytes at DS:b51e + 18 i; code_puzzles_b.md §2.3).
struct PieceRec {
    int16_t x = 0, y = 0, w = 0, h = 0;
    // +0x08 far picture: not modelled (member i of the variant's bundle)
    uint8_t dx = 0, dy = 0;  // +0x0C, +0x0D the piece's place inside the machine picture
    int16_t group = 0;       // +0x0E
    int16_t placed = 0;      // +0x10 1 = on the work area
    static constexpr size_t kDgroupSize = 18;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t i) { return i < 8 || i >= 12; }
    bool operator==(const PieceRec&) const = default;
};
// Gears: a crank handle rectangle (8 bytes at DS:b706 + 8 k) and an object of the variant copy
// (20 bytes at the F copy + 0x116 + 20 i: heap, not in the DGROUP image).
struct RectRec {
    int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    static constexpr size_t kDgroupSize = 8;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t) { return true; }
    bool operator==(const RectRec&) const = default;
};
struct GearObjectRec {
    int16_t type = 0, state = 0, col = 0, row = 0, length = 0;          // file fields
    int16_t dir = 0, visited = 0, savedState = 0, savedCol = 0, savedRow = 0;  // +0x0A.. run time
    static constexpr size_t kDgroupSize = 20;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t) { return true; }
    bool operator==(const GearObjectRec&) const = default;
};
// Energy forms: a device (18 bytes at DS:ad70 + 0x12 d) and a form row (26 bytes at DS:adb8 + 0x1a k)
// (code_puzzles_a.md §4.3). The row's satisfied / sent / line words are indexed as the original does,
// sent with the swapped index: use the helpers energySatisfied / energySent / energyLine below.
struct EnergyDeviceRec {
    int16_t id = -1;                  // device resource id, -1 none
    std::array<int16_t, 8> uses{};    // uses-form flags
    static constexpr size_t kDgroupSize = 18;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t) { return true; }
    bool operator==(const EnergyDeviceRec&) const = default;
};
struct EnergyFormRec {
    int16_t form = -1;                // +0x00 form index, -1 none
    std::array<int16_t, 12> words{};  // +0x02 satisfied[4], +0x0A sent[4], +0x12 line[4] of this row
    static constexpr size_t kDgroupSize = 26;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t) { return true; }
    bool operator==(const EnergyFormRec&) const = default;
};
// Magnets: a magnet slot (0x12 bytes of the Mem_Alloc(0x480) block DS:af52: heap, not in the image).
struct MagnetRec {
    int16_t kind = -1;  // -1 free
    int16_t orientation = 0, x = 0, y = 0;
    int16_t drawn = 0;                // +0x08 draw pass
    int16_t fx = 0, fy = 0;           // +0x0A, +0x0C force accumulators (i16, wrapping)
    int16_t attached = 0;             // +0x0E
    int16_t matched = 0;              // +0x10
    static constexpr size_t kDgroupSize = 0x12;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    static constexpr bool modelledByte(size_t) { return true; }
    bool operator==(const MagnetRec&) const = default;
};
// A workshop vehicle sprite (0x36 bytes of the pool DS:a0cc: heap, not in the image;
// code_vehicles_races.md §5.1).
struct VehicleSpriteRec {
    int16_t inUse = 0;   // +0x08
    int16_t layer = 0;   // +0x0A
    SpriteRec sprite;    // +0x0C
    // +0x1A the zone (x, y, w, h of the sprite, priority layer + 20, release 4000 + part, tag 6):
    // a widget, held by the workshop
    int16_t part = 0;    // +0x30 part index (+0x32 far part record)
    bool operator==(const VehicleSpriteRec&) const = default;
};

// ------------------------------------------------------------------------------------- flow
// 12e0 main, Seq_Run and states 3, 16, 17; State_01_Attract (2c11); the exit paths. code_runtime.md
// §2-§4, code_world.md §2, §3, §15.4, code_ui.md §10, §14. Owner: flow agent.
struct FlowState {
    int16_t state = 0;          // DS:455c g_state: Seq_Run state of the main sequence (2 then 1 at start)
    int16_t leaveRequest = 0;   // DS:455a g_leaveRequest: -1 quit, 2 sign-in, 3 hub (state 16)
    int16_t shutdownFlag = 0;   // DS:00c0 g_shutdownFlag: -1 after Game_Shutdown (no reader)
    int16_t storyTextId = 0;    // DS:a04e g_storyTextId: text of the next story page (0x1019..0x101e)

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_state", dg::kState, s.state);
        v("g_leaveRequest", dg::kLeaveRequest, s.leaveRequest);
        v("g_shutdownFlag", dg::kShutdownFlag, s.shutdownFlag);
        v("g_storyTextId", dg::kStoryTextId, s.storyTextId);
    }
};

// --------------------------------------------------------------------------------------- ui
// 3f56 dialogs, 40a4 menus, 4106 commands, 411d help, 4140 text viewer, 2caa sign-in, 2b5a players,
// 3060 hints. code_ui.md §5-§13. Owner: ui agent.
struct UiState {
    // Sign-in (2caa, code_ui.md §11.1).
    int16_t saveDisabled = 0;        // DS:1f7a g_saveDisabled
    Roster roster;                   // DS:a075 g_roster -> the 0x8A0-byte header and index copy (heap)
    int16_t rosterIndex = 0;         // DS:a079 g_rosterIndex
    std::array<uint8_t, 16> typedName{};  // DS:a054 g_typedName (0x1F = an empty new entry's placeholder)
    uint8_t typedName16 = 0;         // DS:a064 the byte a 16th 0x1F marker writes (typedNameByte(16))
    int16_t signinArrowRepeat = 0;   // DS:a065 g_signinArrowRepeat (written only)
    int16_t signinArrowCount = 0;    // DS:a067 g_signinArrowCount (never incremented)
    uint32_t signinPrevClick = 0;    // DS:a069 g_signinPrevClick (written only)
    uint32_t signinLastClick = 0;    // DS:a06d g_signinLastClick (written only)
    int16_t typedLen = 0;            // DS:a071 g_typedLen (-1 right after entry)
    int16_t signinClicked = 0;       // DS:a073 g_signinClicked (written only)
    ScrollBar rosterScrollBar;       // DS:20dc g_rosterScrollBar (pos, max, next repeat, thumb change)
    // Menus (40a4, code_ui.md §6).
    int16_t menuNesting = 0;         // DS:3dd2 g_menuNesting
    DsPtr menuBarCur;                // DS:bc70 g_menuBarCur: DS:4053 installed, DS:405a removed, 0 never
    // Dialogs (3f56, code_ui.md §5.2): the layout globals the last Dlg_Layout wrote.
    int16_t dlgPicY = 0;             // DS:bbb8 g_dlgPicY
    int16_t dlgPicX = 0;             // DS:bbba g_dlgPicX
    int16_t dlgLineH = 0;            // DS:bc10 g_dlgLineH
    int16_t dlgFooterY = 0;          // DS:bc12 g_dlgFooterY
    int16_t dlgFooterX = 0;          // DS:bc14 g_dlgFooterX
    int16_t dlgBodyW = 0;            // DS:bc16 g_dlgBodyW
    int16_t dlgTitleY = 0;           // DS:bc18 g_dlgTitleY
    int16_t dlgTitleX = 0;           // DS:bc1a g_dlgTitleX
    int16_t dlgBodyY = 0;            // DS:bc1c g_dlgBodyY
    int16_t dlgBodyX = 0;            // DS:bc1e g_dlgBodyX
    int16_t dlgH = 0;                // DS:bc20 g_dlgH
    int16_t dlgW = 0;                // DS:bc22 g_dlgW
    int16_t dlgY = 0;                // DS:bc24 g_dlgY
    int16_t dlgX = 0;                // DS:bc26 g_dlgX
    int16_t dlgInnerW = 0;           // DS:bc28 g_dlgInnerW (the menu highlight width)
    uint32_t savedTicks = 0;         // DS:6598 g_savedTicks: g_ticks at Dlg_RunModal's entry
    // Text viewer (4140, code_ui.md §8).
    int16_t viewThumbStep = 0;       // DS:bc74 g_viewThumbStep
    int16_t viewThumbY = 0;          // DS:bc76 g_viewThumbY
    int16_t viewScrollMax = 0;       // DS:bc78 g_viewScrollMax
    // Hints (3060, code_ui.md §13): the queue g_hintSlots DS:a0de (5 x 18 bytes, linked in due order;
    // the list header DS:a0da is heap). The engine's HintQueue is the record.
    HintQueue hints;                 // DS:a0de g_hintSlots

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_saveDisabled", dg::kSaveDisabled, s.saveDisabled);
        v("g_rosterIndex", dg::kRosterIndex, s.rosterIndex);
        v("g_typedName", dg::kTypedName, s.typedName);
        v("DS_a064_typedName16", uint16_t{0xA064}, s.typedName16);
        v("g_signinArrowRepeat", dg::kSigninArrowRepeat, s.signinArrowRepeat);
        v("g_signinArrowCount", dg::kSigninArrowCount, s.signinArrowCount);
        v("g_signinPrevClick", dg::kSigninPrevClick, s.signinPrevClick);
        v("g_signinLastClick", dg::kSigninLastClick, s.signinLastClick);
        v("g_typedLen", dg::kTypedLen, s.typedLen);
        v("g_signinClicked", dg::kSigninClicked, s.signinClicked);
        v("g_rosterScrollBar", dg::kRosterScrollBar, s.rosterScrollBar);
        v("g_menuNesting", dg::kMenuNesting, s.menuNesting);
        v("g_menuBarCur", dg::kMenuBarCur, s.menuBarCur);
        v("g_dlgPicY", dg::kDlgPicY, s.dlgPicY);
        v("g_dlgPicX", dg::kDlgPicX, s.dlgPicX);
        v("g_dlgLineH", dg::kDlgLineH, s.dlgLineH);
        v("g_dlgFooterY", dg::kDlgFooterY, s.dlgFooterY);
        v("g_dlgFooterX", dg::kDlgFooterX, s.dlgFooterX);
        v("g_dlgBodyW", dg::kDlgBodyW, s.dlgBodyW);
        v("g_dlgTitleY", dg::kDlgTitleY, s.dlgTitleY);
        v("g_dlgTitleX", dg::kDlgTitleX, s.dlgTitleX);
        v("g_dlgBodyY", dg::kDlgBodyY, s.dlgBodyY);
        v("g_dlgBodyX", dg::kDlgBodyX, s.dlgBodyX);
        v("g_dlgH", dg::kDlgH, s.dlgH);
        v("g_dlgW", dg::kDlgW, s.dlgW);
        v("g_dlgY", dg::kDlgY, s.dlgY);
        v("g_dlgX", dg::kDlgX, s.dlgX);
        v("g_dlgInnerW", dg::kDlgInnerW, s.dlgInnerW);
        v("g_savedTicks", dg::kSavedTicks, s.savedTicks);
        v("g_viewThumbStep", dg::kViewThumbStep, s.viewThumbStep);
        v("g_viewThumbY", dg::kViewThumbY, s.viewThumbY);
        v("g_viewScrollMax", dg::kViewScrollMax, s.viewScrollMax);
        v("g_hintSlots", dg::kHintSlots, s.hints);
    }
};

// ------------------------------------------------------------------------------------ world
// States 4, 5, 7, 8 (12e0), rooms (1622), parts (153f), the HUD (1a04), Building_NewVisit (18f5:094d),
// Room_OpenPuzzleDoor, Score_Add, Projectiles_Add. code_world.md §4-§8, §14-§17,
// code_vehicles_races.md §3, §4. Owner: world agent.

struct WorldState {
    int16_t inBuilding = 0;       // DS:0094 g_inBuilding (writers: world states 4/8, the workshop, the hub, the sign-in, Players_SaveCurrent)
    int16_t puzzleDoorHit = 0;    // DS:00bc g_puzzleDoorHit (Player_SequenceEnd -> state 8 -> 9)
    int16_t vehicleDone = 0;      // DS:00be g_vehicleDone (the workshop's race door -> state 8 -> 10)
    int16_t roomCount = 0;        // DS:08c4 g_roomCount (Rooms_Load; = rooms.count())
    // The building in progress: the room set copy with its item lists and door types (g_roomSet
    // DS:79f8, g_roomTable DS:7a04, g_roomItemCounts DS:79fc) and the run-time room globals
    // g_displayRoom DS:08c2, g_roomLoaded DS:08c0, g_cratesLeft DS:7a00, g_cratesTotal DS:7a02 and the
    // item pool g_itemPool DS:6fa8, which Rooms holds (gg/game/rooms.h; visited through it).
    Rooms rooms;
    PartDeck deck;                // DS:659c g_partDeck, DS:6614 g_partDealIndex
    PartCatalogue parts;          // DS:6638 g_levelDesc / DS:663c g_parts -> the catalogue copy (heap)
    HudState hud;                 // the HUD (gg/game/hud.h; Hud draws from it)
    // g_room DS:08c6 (the displayed room's header copy) is not modelled: the engine shares the table
    // entry (rooms.h); its item count DS:0964 is the entry's.

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_inBuilding", dg::kInBuilding, s.inBuilding);
        v("g_puzzleDoorHit", dg::kPuzzleDoorHit, s.puzzleDoorHit);
        v("g_vehicleDone", dg::kVehicleDone, s.vehicleDone);
        v("g_roomCount", dg::kRoomCount, s.roomCount);
        v("g_roomLoaded", dg::kRoomLoaded, as16(s.rooms.roomLoaded));
        v("g_displayRoom", dg::kDisplayRoom, as16(s.rooms.displayRoom));
        v("g_cratesLeft", dg::kCratesLeft, as16(s.rooms.cratesLeft));
        v("g_cratesTotal", dg::kCratesTotal, as16(s.rooms.cratesTotal));
        v("g_itemPool", dg::kItemPool, s.rooms.pool());
        v("g_partDeck", dg::kPartDeck, s.deck);  // the 60-word deck DS:659c and g_partDealIndex DS:6614
        HudState::visit(s.hud, v);
    }
};

// ----------------------------------------------------------------------------------- actors
// The actor system (1834), the player and the projectile (18f5), collision (1adb), the Cyber Chimps
// (2a80). code_world.md §9-§13. Owner: actors agent.
inline constexpr int kActorCount = 12;
inline constexpr int kPlayerActor = 0;
inline constexpr int kProjectileActor = 1;
inline constexpr int kFirstChimpActor = 2;
inline constexpr size_t kChimpListWords = (dg::kChimpListDataEnd - dg::kChimpListData) / 2;  // DS:1adc..1bad

struct ActorsState {
    std::array<Actor, kActorCount> actors{};  // DS:7a8a g_actors (0 player, 1 projectile, 2..11 chimps)
    int16_t playerBlink = 0;                   // DS:0a9a g_playerBlink
    int16_t actorSpritesLoaded = 0;            // DS:0a9c g_actorSpritesLoaded
    std::array<int16_t, 16> roomChimpCount{};  // DS:a01c g_roomChimpCount (grows at every display: §13.8)
    // The chimp lists as the session's mutable copy (the 286 path halves the first room word in
    // place, PORTING_NOTES §5): u16 words DS:1adc..1bad; chimpList() resolves g_chimpLists into it.
    std::array<uint16_t, kChimpListWords> chimpListData{};  // DS:1adc g_chimpListData
    // The carried-part lists (heap lists; a part record is itself the node): the player's own list
    // (actor 0 +0x72) and the one all chimps share (g_chimpParts DS:a03c), as part indexes in list order.
    std::vector<int16_t> playerParts;
    std::vector<int16_t> chimpParts;

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_actors", dg::kActors, s.actors);
        v("g_playerBlink", dg::kPlayerBlink, s.playerBlink);
        v("g_actorSpritesLoaded", dg::kActorSpritesLoaded, s.actorSpritesLoaded);
        v("g_roomChimpCount", dg::kRoomChimpCount, s.roomChimpCount);
        v("g_chimpListData", dg::kChimpListData, s.chimpListData);
    }
};

// ---------------------------------------------------------------------------------- puzzles
// State 9 and the framework (38e2), the DAM engine of the launch puzzle (3d39, 3797:100e) and the eight
// puzzles' globals. code_puzzles_a.md, code_puzzles_b.md. Owners: puzzles_a (framework, electric,
// energy, launch, simple machines, DAM), puzzles_b (machine pieces, gears, balance, magnets). Each
// puzzle's struct is only meaningful while that puzzle runs (its overlay's BSS); the Pz* classes
// (gg/game/pz_*.h) bind to it.

struct ElectricState {  // ovl13 3083, code_puzzles_a.md §3.3 (puzzles_a)
    std::array<std::array<int16_t, 20>, 10> paths{};  // DS:a138 g_elecPaths (row 10 would overrun into DS:a2c8: elecPathWord)
    int16_t pathCount = 0;                             // DS:a2c8 g_elecPathCount
    std::array<ElecPieceRec, 15> pieces{};             // DS:a2fc g_elecPieces
    std::array<std::array<ElecTerminalRec, 3>, 15> terminals{};  // DS:a3ec g_elecTerminals
    std::array<int16_t, 15> batteryShorted{};          // DS:a87e g_elecBatteryShorted
    std::array<int16_t, 15> onPath{};                  // DS:a89c g_elecOnPath
    int16_t wireAnchorY = 0;                           // DS:a8ba (unnamed): y of the strung wire's start anchor
    int16_t wireAnchorX = 0;                           // DS:a8bc (unnamed): its x
    int16_t mode = 99;                                 // DS:a8be g_elecMode: 99 idle, 0..3 a piece, 4 a wire
    int16_t foundTerm = 0;                             // DS:a8c0 g_elecFoundTerm
    int16_t foundPiece = 99;                           // DS:a8c2 g_elecFoundPiece (99 none)
    int16_t plugShown = 0;                             // DS:a8c4 g_elecPlugShown
    // DS:a8c6 g_elecToolCounts: battery, bulb, switch, Y-wire, wire (DS:a8ce, the wire count, is the
    // word wave 1 named g_elecWiresLost: the code that increments it gives a wire back, code_puzzles_a.md §3.6)
    std::array<int16_t, 5> toolCounts{};
};
struct SimpleState {  // ovl20 3938, code_puzzles_a.md §6.3 (puzzles_a)
    std::vector<uint8_t> data;                         // DS:b1b4 g_simpleData -> F 0x206d + v copy (0x570 bytes, heap)
    int16_t held = 9;                                  // DS:b1b8 g_simpleHeld (9 none)
    std::array<SimplePlacedRec, 10> placed{};          // DS:b38a g_simplePlaced
};
struct PiecesState {  // ovl21 3a4a, code_puzzles_b.md §2.3 (puzzles_b)
    std::array<PieceRec, 10> pieces{};                 // DS:b51e g_pieces
    int16_t inBox = 0;                                 // DS:b5d2 g_piecesInBox
    int16_t count = 0;                                 // DS:b5d4 g_piecesCount
    int16_t group = 0;                                 // DS:b5d6 g_piecesGroup
    int16_t held = 0x0B;                               // DS:b5d8 g_piecesHeld (0x0b none)
};
struct GearsState {  // ovl22 3b2f, code_puzzles_b.md §3.3 (puzzles_b)
    std::array<int16_t, 2> stock{};                    // DS:b702 g_gearsStock
    std::array<RectRec, 26> crankRect{};               // DS:b706 g_gearsCrankRect
    int16_t held = 0;                                  // DS:b7de g_gearsHeld
    int16_t objectCount = 0;                           // DS:b7e0 g_gearsObjectCount
    int16_t jam = 0;                                   // DS:b7e2 g_gearsJam
    int16_t crankCY = 0;                               // DS:b7e4 g_gearsCrankCY
    int16_t crankCX = 0;                               // DS:b7e6 g_gearsCrankCX
    // The F 0x2002 + v copy (DS:b6d2, heap): the peg grid (DS:b7da) and the objects (DS:b7d6).
    std::array<uint8_t, 12 * 23> grid{};
    std::vector<GearObjectRec> objects;
    bool carrying = false;                             // engine: the cursor is DS:b6c6 (the carried gear's record)
    bool turning = false;                              // engine: DS:b6e2 = 0x2025 (the crank with the hand)
};
struct BalanceState {  // ovl14 32fe, code_puzzles_b.md §4.3 (puzzles_b)
    int16_t rightHeavy = 0;                            // DS:ab20 g_balanceRightHeavy
    int16_t zoneSet = -1;                              // DS:ab22 g_balanceZoneSet
    int16_t tilt = -5;                                 // DS:ab24 g_balanceTilt
    int16_t held = -1;                                 // DS:ab26 g_balanceHeld
    int16_t solved = 0;                                // DS:ab50 g_balanceSolved
    std::array<std::array<int16_t, 3>, 10> hookWeights{};  // DS:ad28 g_balanceHookWeights
    std::array<int16_t, 6> stock{};                    // DS:ad64 g_balanceStock
};
struct EnergyState {  // ovl15 33cd, code_puzzles_a.md §4.3 (puzzles_a)
    std::array<EnergyDeviceRec, 4> devices{};          // DS:ad70 g_energyDevices
    std::array<EnergyFormRec, 4> forms{};              // DS:adb8 g_energyForms (satisfied/sent/lines inside)
    std::array<int16_t, 4> idleFrame{};                // DS:ae20 g_energyIdleFrame
    int16_t mode = 0;                                  // DS:ae88 g_energyMode
    int16_t linesLeft = 0;                             // DS:ae8a g_energyLinesLeft
};
struct MagnetsState {  // ovl16 35bb, code_puzzles_b.md §5.4 (puzzles_b)
    int16_t heldOrient = 0;                            // DS:af48 g_magnetsHeldOrient
    int16_t held = -1;                                 // DS:af4a g_magnetsHeld
    std::array<int16_t, 3> counts{};                   // DS:af4c g_magnetsCounts
    int16_t field56 = 0;                               // DS:af56 (unnamed): 0 at start, never read
    std::array<MagnetRec, 64> slots{};                 // DS:af52 g_magnetsSlots -> 64 records (heap)
    int16_t level = 0;                                 // engine: DS:2ae4[variant] - 1 (a local of PzMagnets_Run)
};
struct LaunchState {  // ovl17 3797, code_puzzles_a.md §5.3 (puzzles_a)
    int16_t glowOn = 1;                                // DS:af90 g_launchGlowOn
    int16_t clipBottom = 0, clipTop = 0;               // DS:af92 / af94 (unnamed): the clip copied with the object sprite
    int16_t clipRight = 0, clipLeft = 0;               // DS:af96 / af98 (unnamed)
    int16_t objectShown = 0;                           // DS:af9a g_launchObjectShown
    int16_t objectY = 0;                               // DS:af9c (unnamed)
    int16_t objectX = 0;                               // DS:af9e (unnamed)
    int16_t offerTool457 = 0;                          // DS:b108 (PzLaunch_SetOfferTool457)
    int16_t offerTool456 = 0;                          // DS:b10a
    int16_t offerObject1 = 0;                          // DS:b10c
    int16_t offerObject2 = 0;                          // DS:b10e
    int16_t offerObject3 = 0;                          // DS:b110
    int16_t offerObject0 = 0;                          // DS:b112
    int16_t offerTool453 = 0;                          // DS:b114
    int16_t offerTool452 = 0;                          // DS:b116
    int16_t object = -1;                               // DS:b118 g_launchObject
    int16_t surface = 1;                               // DS:b11a g_launchSurface
    int16_t landing = 0;                               // DS:b11c g_launchLanding
    int16_t gravity = 0;                               // DS:b11e g_launchGravity
    int16_t ramp = 0;                                  // DS:b124 g_launchRamp
    int16_t target = 0;                                // DS:b126 g_launchTarget
    int16_t gravityPic = 0;                            // DS:b128 g_launchGravityPic
    int16_t energy = 0;                                // DS:b12a g_launchEnergy
    int16_t glowFrame = 0;                             // DS:3304 g_launchGlowFrame (initialised data: 0)
};
// The DAM engine (3d39; fmt_graphics.md §6), used by the launch puzzle's cut-scenes.
struct DamState {
    int16_t celCount = 0;                              // DS:3988 g_animCelCount
    int16_t rectCount = 0;                             // DS:398a g_animRectCount
    std::array<std::array<uint8_t, 0x22>, 2> channels{};  // DS:ba66 g_animChannels (raw: far cursors inside)
};

struct PuzzlesState {
    // The framework (38e2, code_puzzles_a.md §1, §2).
    int16_t leave = 0;                       // DS:3332 g_puzzleLeave: 1 Start New Player, 2 Return to Technology Center
    int16_t variant = 0;                     // DS:b138 g_puzzleVariant
    int16_t kind = 0;                        // DS:b13a g_puzzleKind
    int16_t result = 0;                      // DS:b13c g_puzzleResult (1 solved)
    std::array<int16_t, 3> candidates{};     // DS:b13e g_puzzleCandidates (-1 empty)
    int16_t goThrough = 0;                   // DS:b144 g_puzzleGoThrough (stale after a menu exit: keep)
    ElectricState electric;
    SimpleState simple;
    PiecesState pieces;
    GearsState gears;
    BalanceState balance;
    EnergyState energy;
    MagnetsState magnets;
    LaunchState launch;
    DamState dam;

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_puzzleLeave", dg::kPuzzleLeave, s.leave);
        v("g_puzzleVariant", dg::kPuzzleVariant, s.variant);
        v("g_puzzleKind", dg::kPuzzleKind, s.kind);
        v("g_puzzleResult", dg::kPuzzleResult, s.result);
        v("g_puzzleCandidates", dg::kPuzzleCandidates, s.candidates);
        v("g_puzzleGoThrough", dg::kPuzzleGoThrough, s.goThrough);
        auto& e = s.electric;
        v("g_elecPaths", dg::kElecPaths, e.paths);
        v("g_elecPathCount", dg::kElecPathCount, e.pathCount);
        v("g_elecPieces", dg::kElecPieces, e.pieces);
        v("g_elecTerminals", dg::kElecTerminals, e.terminals);
        v("g_elecBatteryShorted", dg::kElecBatteryShorted, e.batteryShorted);
        v("g_elecOnPath", dg::kElecOnPath, e.onPath);
        v("DS_a8ba_elecWireAnchorY", uint16_t{0xA8BA}, e.wireAnchorY);
        v("DS_a8bc_elecWireAnchorX", uint16_t{0xA8BC}, e.wireAnchorX);
        v("g_elecMode", dg::kElecMode, e.mode);
        v("g_elecFoundTerm", dg::kElecFoundTerm, e.foundTerm);
        v("g_elecFoundPiece", dg::kElecFoundPiece, e.foundPiece);
        v("g_elecPlugShown", dg::kElecPlugShown, e.plugShown);
        v("g_elecToolCounts", dg::kElecToolCounts, e.toolCounts);
        v("g_simpleHeld", dg::kSimpleHeld, s.simple.held);
        v("g_simplePlaced", dg::kSimplePlaced, s.simple.placed);
        v("g_pieces", dg::kPieces, s.pieces.pieces);
        v("g_piecesInBox", dg::kPiecesInBox, s.pieces.inBox);
        v("g_piecesCount", dg::kPiecesCount, s.pieces.count);
        v("g_piecesGroup", dg::kPiecesGroup, s.pieces.group);
        v("g_piecesHeld", dg::kPiecesHeld, s.pieces.held);
        v("g_gearsStock", dg::kGearsStock, s.gears.stock);
        v("g_gearsCrankRect", dg::kGearsCrankRect, s.gears.crankRect);
        v("g_gearsHeld", dg::kGearsHeld, s.gears.held);
        v("g_gearsObjectCount", dg::kGearsObjectCount, s.gears.objectCount);
        v("g_gearsJam", dg::kGearsJam, s.gears.jam);
        v("g_gearsCrankCY", dg::kGearsCrankCY, s.gears.crankCY);
        v("g_gearsCrankCX", dg::kGearsCrankCX, s.gears.crankCX);
        v("g_balanceRightHeavy", dg::kBalanceRightHeavy, s.balance.rightHeavy);
        v("g_balanceZoneSet", dg::kBalanceZoneSet, s.balance.zoneSet);
        v("g_balanceTilt", dg::kBalanceTilt, s.balance.tilt);
        v("g_balanceHeld", dg::kBalanceHeld, s.balance.held);
        v("g_balanceSolved", dg::kBalanceSolved, s.balance.solved);
        v("g_balanceHookWeights", dg::kBalanceHookWeights, s.balance.hookWeights);
        v("g_balanceStock", dg::kBalanceStock, s.balance.stock);
        v("g_energyDevices", dg::kEnergyDevices, s.energy.devices);
        v("g_energyForms", dg::kEnergyForms, s.energy.forms);
        v("g_energyIdleFrame", dg::kEnergyIdleFrame, s.energy.idleFrame);
        v("g_energyMode", dg::kEnergyMode, s.energy.mode);
        v("g_energyLinesLeft", dg::kEnergyLinesLeft, s.energy.linesLeft);
        v("g_magnetsHeldOrient", dg::kMagnetsHeldOrient, s.magnets.heldOrient);
        v("g_magnetsHeld", dg::kMagnetsHeld, s.magnets.held);
        v("g_magnetsCounts", dg::kMagnetsCounts, s.magnets.counts);
        v("DS_af56_magnetsUnused", uint16_t{0xAF56}, s.magnets.field56);
        auto& l = s.launch;
        v("g_launchGlowOn", dg::kLaunchGlowOn, l.glowOn);
        v("DS_af92_launchClipBottom", uint16_t{0xAF92}, l.clipBottom);
        v("DS_af94_launchClipTop", uint16_t{0xAF94}, l.clipTop);
        v("DS_af96_launchClipRight", uint16_t{0xAF96}, l.clipRight);
        v("DS_af98_launchClipLeft", uint16_t{0xAF98}, l.clipLeft);
        v("g_launchObjectShown", dg::kLaunchObjectShown, l.objectShown);
        v("DS_af9c_launchObjectY", uint16_t{0xAF9C}, l.objectY);
        v("DS_af9e_launchObjectX", uint16_t{0xAF9E}, l.objectX);
        v("DS_b108_launchOfferTool457", uint16_t{0xB108}, l.offerTool457);
        v("DS_b10a_launchOfferTool456", uint16_t{0xB10A}, l.offerTool456);
        v("DS_b10c_launchOfferObject1", uint16_t{0xB10C}, l.offerObject1);
        v("DS_b10e_launchOfferObject2", uint16_t{0xB10E}, l.offerObject2);
        v("DS_b110_launchOfferObject3", uint16_t{0xB110}, l.offerObject3);
        v("DS_b112_launchOfferObject0", uint16_t{0xB112}, l.offerObject0);
        v("DS_b114_launchOfferTool453", uint16_t{0xB114}, l.offerTool453);
        v("DS_b116_launchOfferTool452", uint16_t{0xB116}, l.offerTool452);
        v("g_launchObject", dg::kLaunchObject, l.object);
        v("g_launchSurface", dg::kLaunchSurface, l.surface);
        v("g_launchLanding", dg::kLaunchLanding, l.landing);
        v("g_launchGravity", dg::kLaunchGravity, l.gravity);
        v("g_launchRamp", dg::kLaunchRamp, l.ramp);
        v("g_launchTarget", dg::kLaunchTarget, l.target);
        v("g_launchGravityPic", dg::kLaunchGravityPic, l.gravityPic);
        v("g_launchEnergy", dg::kLaunchEnergy, l.energy);
        v("g_launchGlowFrame", dg::kLaunchGlowFrame, l.glowFrame);
        v("g_animCelCount", dg::kAnimCelCount, s.dam.celCount);
        v("g_animRectCount", dg::kAnimRectCount, s.dam.rectCount);
        v("g_animChannels", dg::kAnimChannels, s.dam.channels);
    }
};

// --------------------------------------------------------------------------------- vehicles
// The workshop and the blueprint (2dee), states 10 and 11 (12e0), the races (3dd6, 3e77, 3f50), the
// results, the post-race menu and the final movie (2fcb). code_vehicles_races.md §5-§14. Owner:
// vehicles agent.
struct VehiclesState {
    // Workshop (§5.1).
    int16_t carriedRow = -1;            // DS:22a4 g_carriedRow: -1 nothing carried, else the part's flags & 0x7fff
    int16_t carriedPart = -1;           // DS:a07c g_carriedPart -> engine: the carried part's index (-1 none)
    int16_t shelfDirty = 0;             // DS:a080 g_shelfDirty
    int16_t scrollLeft = 0;             // DS:a084 g_scrollLeft
    int16_t scrollDir = 0;              // DS:a086 g_scrollDir
    int16_t workshopSeqState = 0;       // DS:a088 g_workshopSeqState (never written: 0)
    int16_t blueprintPageCount = 0;     // DS:a092 g_blueprintPageCount
    int16_t blueprintPage = 0;          // DS:a094 g_blueprintPage
    int16_t shelfCount = 0;             // DS:a0c8 g_shelfCount
    int16_t workshopNextState = 0;      // DS:a0ca g_workshopNextState: 8 or 16
    int16_t raceDoorOpen = 0;           // DS:a0d0 g_raceDoorOpen
    int16_t resultLost = 0;             // DS:a0d2 g_resultLost
    int16_t resultNextRace = 0;         // DS:a0d4 g_resultNextRace
    // The vehicle sprites (DS:a0cc pool of 20 x 0x36 bytes and the layer-sorted list DS:a0b4, heap).
    std::array<VehicleSpriteRec, 20> vehiclePool{};
    std::vector<int16_t> vehicleOrder;  // pool indexes in list order (back to front)
    // States 10 / 11 and the results.
    std::array<int16_t, 15> playerVehicle{};  // DS:6616 g_playerVehicle: part indexes, -1 terminated
    int16_t rematch = 0;                // DS:455e g_rematch
    int16_t raceResult = 0;             // DS:4560 g_raceResult: 0 the player won
    // Races (§9, §10).
    RacerRec raceMorty;                 // DS:baae g_raceMorty
    RacerRec racePlayer;                // DS:bae8 g_racePlayer
    uint16_t raceFinishBits = 0;        // DS:bb22 g_raceFinishBits
    int16_t airPathEnd = 0;             // DS:bb26 g_airPathEnd (never read)
    AirRacerRec airMorty;               // DS:bb28 g_airMorty
    AirRacerRec airPlayer;              // DS:bb64 g_airPlayer
    uint32_t airCamera10 = 0;           // DS:3b70 g_airCamera10 (16.16)
    uint32_t airCamera = 0;             // DS:3b74 g_airCamera (16.16)
    // Added by the vehicles agent (wave 3).
    int16_t blueprintOpen = 0;          // DS:2198 g_blueprintOpen (Blueprint_Open / _Close; MusicCues reads it)
    int16_t airUnusedBb24 = 0;          // DS:bb24 (unnamed): 0 at AirRace_Run's start (3e77:064d), no reader seen
    // The workshop's fields of the part records (heap, per part index; code_vehicles_races.md §3.2):
    // +0x5E the shelf sprite {flags, x, y, w, h} (its picture +0x68 is the workshop's: VehiclesObjects)
    // and +0x8A the workshop state (bit 0 carried, value 2 unavailable on the shelf).
    struct ShelfSlot {
        SpriteRec sprite;
        uint16_t state = 0;
        bool operator==(const ShelfSlot&) const = default;
    };
    std::vector<ShelfSlot> shelf;

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_blueprintOpen", dg::kBlueprintOpen, s.blueprintOpen);
        v("DS_bb24_airUnused", uint16_t{0xBB24}, s.airUnusedBb24);
        v("g_carriedRow", dg::kCarriedRow, s.carriedRow);
        v("g_shelfDirty", dg::kShelfDirty, s.shelfDirty);
        v("g_scrollLeft", dg::kScrollLeft, s.scrollLeft);
        v("g_scrollDir", dg::kScrollDir, s.scrollDir);
        v("g_workshopSeqState", dg::kWorkshopSeqState, s.workshopSeqState);
        v("g_blueprintPageCount", dg::kBlueprintPageCount, s.blueprintPageCount);
        v("g_blueprintPage", dg::kBlueprintPage, s.blueprintPage);
        v("g_shelfCount", dg::kShelfCount, s.shelfCount);
        v("g_workshopNextState", dg::kWorkshopNextState, s.workshopNextState);
        v("g_raceDoorOpen", dg::kRaceDoorOpen, s.raceDoorOpen);
        v("g_resultLost", dg::kResultLost, s.resultLost);
        v("g_resultNextRace", dg::kResultNextRace, s.resultNextRace);
        v("g_playerVehicle", dg::kPlayerVehicle, s.playerVehicle);
        v("g_rematch", dg::kRematch, s.rematch);
        v("g_raceResult", dg::kRaceResult, s.raceResult);
        v("g_raceMorty", dg::kRaceMorty, s.raceMorty);
        v("g_racePlayer", dg::kRacePlayer, s.racePlayer);
        v("g_raceFinishBits", dg::kRaceFinishBits, s.raceFinishBits);
        v("g_airPathEnd", dg::kAirPathEnd, s.airPathEnd);
        v("g_airMorty", dg::kAirMorty, s.airMorty);
        v("g_airPlayer", dg::kAirPlayer, s.airPlayer);
        v("g_airCamera10", dg::kAirCamera10, s.airCamera10);
        v("g_airCamera", dg::kAirCamera, s.airCamera);
    }
};

// ------------------------------------------------------------------------------------ sound
// The sound handles the game keeps (SndRes_* return values; code_sound.md §13, §15). Each is written by
// the module that loads the bank and read by the modules that play it. The driver and the bank layer
// keep their own state (GgAudio: the slots, g_effectBank's copy for SndRes_StopAllEffects and the
// screen-animation ops, the music cue counters of MusicCues, PzSnd's bank); a handle the driver also
// needs is passed to it by the facade call that stores it here (SoundFacade::setEffectBank, ...).
struct SoundState {
    int16_t titleMusicBank = -1;  // DS:0098 g_titleMusicBank (sign-in, hub; freed by state 4)
    int16_t roomMusicBank = -1;   // DS:09e6 g_roomMusicBank (Rooms_Load; also MusicCues' copy)
    int16_t effectBank = -1;      // DS:7a44 g_effectBank (the screen's effect bank)
    int16_t musicBank = -1;       // DS:9dc4 g_musicBank (attract, workshop)
    int16_t movieMusicBank = -1;  // DS:4552 g_movieMusicBank (state 15; also MusicCues' copy)
    int16_t raceEffects = -1;     // DS:baaa g_raceEffects
    int16_t raceMusic = -1;       // DS:baac g_raceMusic
    int16_t airEffects = -1;      // DS:bbb0 g_airEffects
    int16_t airMusic = -1;        // DS:bbb2 g_airMusic

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_titleMusicBank", dg::kTitleMusicBank, s.titleMusicBank);
        v("g_roomMusicBank", dg::kRoomMusicBank, s.roomMusicBank);
        v("g_effectBank", dg::kEffectBank, s.effectBank);
        v("g_musicBank", dg::kMusicBank, s.musicBank);
        v("g_movieMusicBank", dg::kMovieMusicBank, s.movieMusicBank);
        v("g_raceEffects", dg::kRaceEffects, s.raceEffects);
        v("g_raceMusic", dg::kRaceMusic, s.raceMusic);
        v("g_airEffects", dg::kAirEffects, s.airEffects);
        v("g_airMusic", dg::kAirMusic, s.airMusic);
    }
};

// ---------------------------------------------------------------------------------- runtime
// Init_CommandLine 303a:005c (code_runtime.md §2.2). Owner: flow agent (start-up).
struct RuntimeState {
    int16_t displayCode = -1;  // DS:244e g_displayCode (-1 in the file; 8 after -GV; the check is dropped)
    int16_t cpuType = -1;      // DS:2450 g_cpuType (-1 in the file; 2 = the 286 path, else 3)
    int16_t soundDevice = -1;  // DS:2452 g_soundDevice (-1 detect, 0 off, 1 speaker, 4 FM)
    uint16_t randSeed = 0;     // DS:2454 g_randSeed: srand's argument

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_displayCode", dg::kDisplayCode, s.displayCode);
        v("g_cpuType", dg::kCpuType, s.cpuType);
        v("g_soundDevice", dg::kSoundDevice, s.soundDevice);
        v("g_randSeed", dg::kRandSeed, s.randSeed);
    }
};

// ------------------------------------------------------------------------------------ state
struct GgState {
    PlayerRecord player;  // DS:4562 g_player (0x16BD bytes; g_score, g_building, g_level, ... are its fields)
    FlowState flow;
    UiState ui;
    WorldState world;
    ActorsState actors;
    PuzzlesState puzzles;
    VehiclesState vehicles;
    SoundState sound;
    RuntimeState runtime;

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_player", dg::kPlayer, s.player);
        FlowState::visit(s.flow, v);
        UiState::visit(s.ui, v);
        WorldState::visit(s.world, v);
        ActorsState::visit(s.actors, v);
        PuzzlesState::visit(s.puzzles, v);
        VehiclesState::visit(s.vehicles, v);
        SoundState::visit(s.sound, v);
        RuntimeState::visit(s.runtime, v);
    }

    // Convenience views of the player record (the original's names).
    uint32_t& score() { return player.points; }                       // g_score DS:4572
    int16_t& building() { return player.building; }                   // g_building DS:4592
    int16_t& level() { return player.level; }                         // g_level DS:4594
    int16_t& raceIndex() { return player.raceIndex; }                 // g_raceIndex DS:4590
    LiveState& live() { return player.live; }                         // DS:457c..458f
    Actor& playerActor() { return actors.actors[kPlayerActor]; }      // g_actors[0] DS:7a8a
    int playerRoom() const { return actors.actors[kPlayerActor].room; }  // g_playerRoom DS:7aa4
};

// --------------------------------------------------------------------------- read-only tables
// The tables of ssg.exe's DGROUP the wave 3 logic reads, parsed once (GgTables::load). Modules read
// other tables of their own through GgExe (dgroup_offsets.h), as wave 2 does.

// A transition list entry {u32 match, u32 ignore, u16 next, u16 unused} (code_world.md §11.2, §13.3).
struct TransitionEntry {
    uint32_t match = 0;
    uint32_t ignore = 0;
    int16_t next = 0;
    uint16_t unused = 0;
};
// A Cyber Chimp move list (code_world.md §13.3): 12-byte header, n 12-byte entries.
struct ChimpMoveEntry {
    int16_t dist = 0;     // -1 = unconditional
    uint16_t kinds = 0;   // contact kinds the probe looks for
    uint16_t unused = 0;
    uint16_t chance = 0;  // per mille
    uint16_t next = 0;    // next sequence
    uint16_t flag = 0;    // != 0 never fires
};
struct ChimpMoveList {
    int16_t marker = 0;   // -2 = "stay" (only transitions apply), else active
    int16_t field2 = 0, field4 = 0;
    uint16_t defaultNext = 0;
    int16_t field10 = 0;
    std::vector<ChimpMoveEntry> entries;
};

struct GgTables {
    std::span<const uint8_t> dgroup;  // DS:0000..4551 of the exe (initialised data; BSS is zero)
    // World.
    std::array<int16_t, 5> tubeRoomByLevel{};      // DS:00b2 g_tubeRoomByLevel
    std::array<uint16_t, 15> roomSetIds{};         // DS:09e8 g_roomSetIds
    // Actors.
    std::array<std::vector<TransitionEntry>, 68> playerTransitions{};  // DS:127e (empty: a 0 pointer)
    std::array<bool, 68> playerHasTransitions{};
    std::vector<int16_t> contactsPlayer;           // DS:13a0 g_contactsPlayer (without the -1)
    std::vector<int16_t> contactsProjectile;       // DS:13b6
    std::vector<int16_t> contactsChimp;            // DS:13cc
    std::array<uint16_t, 5> chimpStunLoops{};      // DS:0a90 g_chimpStunLoops
    // g_chimpLists DS:1bae: per race the DGROUP offset of its list in DS:1adc..1bad (into chimpListData).
    std::array<uint16_t, 15> chimpListOffsets{};
    static constexpr int kChimpSequences = 42;
    std::array<std::optional<ChimpMoveList>, kChimpSequences> chimpMoves{};         // DS:1d2e g_chimpMoves
    std::array<std::optional<std::vector<TransitionEntry>>, kChimpSequences> chimpTransitions{};  // DS:1f10
    // Puzzles.
    std::array<uint16_t, 8> puzzleVariants{};      // DS:3334 g_puzzleVariants
    // Vehicles and results.
    std::array<uint32_t, 7> rankThresholds{};      // DS:1f9c g_rankThresholds

    // Throws tmport::FormatError when a table does not have the shape the specs describe (another
    // version of ssg.exe).
    static GgTables load(const GgExe& exe);
};

// ----------------------------------------------------------- DGROUP image and field registry

// DS:0000..BD03 of a freshly loaded program: the exe's initialised data, BSS zero.
std::vector<uint8_t> initialDgroupImage(const GgExe& exe);

// Every DGROUP field of GgState (and, for dumpToDgroup(const GgGame&), the service-held globals), for
// tools and the comparisons with DOSBox-X states (tools/gg/capture_mem.py).
struct DgroupField {
    std::string_view name;        // canonical name (dgroup.tsv) or DS_xxxx_<role>
    uint16_t ds = 0;              // DGROUP offset
    uint16_t size = 0;            // bytes
    uint16_t stride = 0;          // element size of the record type (0: plain integers, all modelled)
    // Per byte of one element: 1 modelled, 0 not (far pointers, unknown words). Empty: all modelled.
    std::span<const uint8_t> mask;
    bool service = false;         // held by a service object (written by dumpToDgroup(const GgGame&))
    bool modelled(size_t byteInField) const {
        return mask.empty() || stride == 0 || mask[byteInField % stride] != 0;
    }
};
// The GgState fields in visit order (g_partDeck covers the deck and g_partDealIndex, DS:659c..6615).
const std::vector<DgroupField>& gameStateFields();

// Writes the state into a copy of `base` (DS:0000..BD03; bytes no field models keep base's value) in
// the original's layout: little-endian words, DGROUP far pointers {off, 0x0EA7}, records byte for byte.
std::vector<uint8_t> dumpToDgroup(const GgState& s, std::span<const uint8_t> base);
// The inverse for every modelled field (a DOSBox-X DGROUP, or initialDgroupImage() at start-up).
// Heap structures (rooms, catalogue, carried lists, roster, puzzle copies) are not touched.
void loadFromDgroup(GgState& s, std::span<const uint8_t> dgroup);
// The names of the fields whose modelled bytes differ between two DGROUP images. `fields` defaults to
// gameStateFields().
std::vector<std::string_view> diffDgroup(std::span<const uint8_t> a, std::span<const uint8_t> b,
                                         const std::vector<DgroupField>* fields = nullptr);

// The state of a freshly started program: loadFromDgroup(initialDgroupImage(exe)), then the values the
// start-up's init functions give GgState fields (gameInitState). The tables are needed for the chimp
// lists' copy (it is initialised data, so loadFromDgroup fills it).
GgState initialState(const GgExe& exe);
// The GgState part of main's start-up (12e0:0b38; code_runtime.md §2.1): g_state = 1 (after its dead
// store of 2), Hud_Init's DS:874c = -1 / DS:8746 = 3 / flash 0, Hint_Init's empty queue, the actors'
// fixed fields of Actors_Init (18f5:0721: ids, delays, the transition pointers, the projectile
// disabled; the callbacks are the slots' roles). The rest of start-up (Gfx_Init, Font_Init, SndRes_Init,
// Workshop_Init's list) belongs to the services and the vehicles module.
void gameInitState(GgState& s);

// --------------------------------------------------------------------------- quirk helpers
// Memory-layout behaviour the specs rely on (re/gg/docs/ENGINE_MAP.md, "Wave 3 map", lists them).

// The chimp list of a race as the session's mutable copy sees it (g_chimpLists DS:1bae[race] ->
// chimpListData): {count, room per chimp}. The 286 path writes its first room word in place
// (chimpListFirstRoom). Throws std::out_of_range for a race outside 0..14.
struct ChimpListView {
    uint16_t count = 0;
    std::span<uint16_t> rooms;
};
ChimpListView chimpList(GgState& s, const GgTables& t, int raceIndex);
uint16_t& chimpListFirstRoom(GgState& s, const GgTables& t, int raceIndex);

// g_typedName[i] for i = 0..16: byte 16 is DS:a064 (a 16th 0x1F marker writes there; code_ui.md §11.5).
uint8_t& typedNameByte(UiState& u, int i);

// Energy forms: the words of the form rows DS:adb8 as the original indexes them (code_puzzles_a.md §4.3):
// satisfied[k][d] at +0x1a k + 2 + 2 d, sent[d][k] at +0x1a d + 0x0a + 2 k (swapped), line[k][d] at
// +0x1a k + 0x12 + 2 d.
int16_t& energySatisfied(EnergyState& e, int k, int d);
int16_t& energySent(EnergyState& e, int d, int k);
int16_t& energyLine(EnergyState& e, int k, int d);

// Electric circuits: word `col` of row `row` of the path table DS:a138 (10 rows of 20 words). Row 10
// lies past the table: its word 0 is g_elecPathCount DS:a2c8 (an 11th path overwrites the count, the
// original's overrun); returns nullptr for any other word past the table (not modelled: the engine
// stops there and logs).
int16_t* elecPathWord(ElectricState& e, int row, int col);

// The solved flag of a puzzle variant (g_puzzleSolved DS:45d0 + kind * 0x42 + variant * 2, record
// +0x6E). Throws std::out_of_range outside 8 kinds x 33 slots.
uint16_t& puzzleSolved(PlayerRecord& p, int kind, int variant);
// g_hintSeen[id - 0x1043] (record +0x42); throws std::out_of_range for an id outside 0x1043..0x1055.
uint16_t& hintSeen(PlayerRecord& p, uint16_t id);
// The building block of building b (DS:47e0 + b * 0x6ba, record +0x27E).
BuildingBlock& buildingBlock(PlayerRecord& p, int b);

}  // namespace tmport::gg
