#pragma once
// GameState: every run-time global of SST.EXE's game logic (the 1990 DOS edition), typed and named
// after the canonical names of re/names/dos/dgroup.tsv (g_ dropped), each with its DGROUP address.
// One struct per owning module, as in the original (re/dos/docs/ENGINE_MAP.md lists the owners):
//
//   world   scene.c, cells.c, cells2.c (world part), camera, scrolling, transitions   (world agent)
//   player  player.c, anim.c, search.c, end sequence, treasure room, rank chest     (player agent)
//   elves   elves.c, layout.c, hints.c                                              (elves agent)
//   ui      ui.c, title.c, signin.c, quiz.c, HUD                                    (ui agent)
//   core    main loop, init, and the game-wide counters every module reads/writes  (mainloop agent)
//
// The state holds plain data only (integers, std::array, the record structs below), so a GameState
// can be copied and compared. Globals that a wave-2 object already owns are reached through DosGame
// (dos/game/game.h) instead: g_keys / DS:9257 / DS:92FA (KbdIsr), g_cmd / DS:90F6 / DS:9238 /
// DS:9285 / g_inputContext (InputPoll), the mouse words (MousePointer), DS:90FE / DS:9100 / g_seconds
// (TickCounters), g_stepDone (StepGate), g_ticks (DosRuntime), the rand seed (BorlandRand), the
// draw records and save heap (SpriteRenderer), the zone/key lists (InputTables) and the USERS.DAT
// image (DosUsersStore).
//
// Initial values: loadFromDgroup() with the EXE's initialised data (initialDgroupImage) gives every
// field its file value (BSS = 0), as the original starts; Game_InitVars (init.h) then sets its
// values. Read-only tables are in GameTables (loaded once from the EXE).
//
// Parallel-work rule: an agent adds fields only to its own section (and to that section's visit()
// list, with the field's DS address), never reorders or removes others.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "dos/data/dgroup_offsets.h"
#include "dos/data/dos_exe.h"

namespace tmport::dos {

struct UserRecord;

// ------------------------------------------------------------------------------- record types
// Each record type has kDgroupSize and store/load in the original's byte layout (dumpToDgroup).

// A far pointer to a DGROUP address ({u16 off, u16 seg 0x3D94} in the file); off 0 = null. The
// engine keeps only the offset (the object or string it points at is found by its DS address).
struct DsPtr {
    uint16_t off = 0;

    static constexpr size_t kDgroupSize = 4;
    void store(uint8_t* p) const;  // segment 0x3D94 (DGROUP, image-relative) when off != 0
    void load(const uint8_t* p);   // takes the offset (a DOSBox-X dump has a relocated segment)
    bool operator==(const DsPtr&) const = default;
};

// The head every moving object shares (player DS:37E2, elf records): code_world.md §2.1.
struct Obj {
    uint16_t x = 0;  // +0 world x, 0..0x7FF within a row of the strip
    uint16_t y = 0;  // +2 world y; row = y >> 7, on a path when (y & 0x7F) == 0x70
    int16_t dx = 0;  // +4 player: bytes (x += 2*dx); elf: pixels per tick
    int16_t dy = 0;  // +6 pixels; cleared by Obj_WrapWorldX

    static constexpr size_t kDgroupSize = 8;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    bool operator==(const Obj&) const = default;
};

// One elf record of g_elves DS:37FA (32 bytes, code_elves_layout_hints.md §3.1).
struct Elf {
    Obj obj;                // +0x00
    uint16_t base = 0;      // +0x08 base state (type and facing: 1/5 A, 9/13 B, 17/21 C, 25 D; 0 unused)
    uint16_t state = 0;     // +0x0A index into g_elfStateHandlers; >= 0x2D = has a sprite
    uint16_t question = 0;  // +0x0C question slot 1..3 (clue carrier), 0 none
    uint16_t spriteSlot = 0;  // +0x0E sprite slot 6/8/10 (stale after a release is possible)
    std::array<uint8_t, 16> unused{};  // +0x10..0x1F, never used by the elf code

    static constexpr size_t kDgroupSize = 0x20;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    bool operator==(const Elf&) const = default;
};

// One sprite/animation slot of g_spriteSlots DS:8FD6 (22 bytes, code_player.md §3.1).
struct SpriteSlot {
    int16_t x = 0;           // +0x00 screen x in bytes (a child slot: offset from its owner)
    int16_t y = 0;           // +0x02 screen y in rows (child: offset)
    int16_t z = 0;           // +0x04 depth: drawn in descending order
    uint16_t frame = 0;      // +0x06 frame code (PAK frame * 2 + mirror bit), 0xFFFF = nothing
    uint16_t owner = 0;      // +0x08 slot whose position this one is relative to (itself = absolute)
    uint16_t anim = 0;       // +0x0A running script id, 0 = none
    uint16_t prev = 0;       // +0x0C last script that ended or was stopped
    uint16_t pos = 0;        // +0x0E script position in words
    int16_t delay = 0;       // +0x10 ticks left on the current entry
    DsPtr obj;               // +0x12 object of script op -4 (an elf); never cleared by Anim_InitSlots

    static constexpr size_t kDgroupSize = 22;
    void store(uint8_t* p) const;
    void load(const uint8_t* p);
    bool operator==(const SpriteSlot&) const = default;
};

inline constexpr uint16_t kPlayerObjOff = dg::kPlayer;  // DS:37E2: g_curObj == the player
inline constexpr uint16_t elfObjOff(int i) { return static_cast<uint16_t>(dg::kElves + 0x20 * i); }
inline constexpr int kElfCount = 15;
inline constexpr int kSpriteSlots = 13;
inline constexpr uint16_t kAnimScriptDataEnd = dg::kAnimScripts;  // DS:3D36..4F59 hold the script words
inline constexpr size_t kAnimScriptWords = (kAnimScriptDataEnd - dg::kAnimScriptData) / 2;
inline constexpr uint16_t kMessageTextStart = 0x6BC0;  // first byte of the hint/message texts
inline constexpr uint16_t kMessageTextEnd = 0x7CA3;    // = g_flutterText; the last text ends at 7CA2
inline constexpr uint16_t kUsersRecordBase = dg::kUsersFile + 3;  // DS:BA17 = record 0
inline constexpr uint16_t kDgroupSize = dg::kBssEnd;   // DS:0000..CC1F

// ------------------------------------------------------------------------------------ world
// scene.c (1437), cells.c (1568), cells2.c (176a world part), camera latches and scrolling (130f).
// code_world.md §2.4, §3, §8; code_runtime.md §3.5-3.7. Owner: world agent.
struct WorldState {
    // Initialised data that changes at run time (values from the file).
    std::array<uint16_t, 256> cellPicture{};  // DS:0CAA g_cellPicture: block number in bits 0..6
    std::array<int8_t, 256> cellType{};       // DS:0EAA g_cellType (signed char; Cells_AdjustForCpu patches it)
    std::array<uint16_t, 256> cellContent{};  // DS:0FAA g_cellContent: 0x80 empty, 6+2k treasure, 0x66/68/6A key; bit 7 searched
    std::array<std::array<uint16_t, 4>, 3> levelPicData{};  // DS:122E g_levelPicData {count, key picture, unused, places}
    uint16_t menuBarHidden = 0;    // DS:036C set by Scene_Update when scenes 3/4 blank the menu bar
    std::array<uint8_t, 8> shelfRoomRedraw{};  // DS:7D7C g_shelfRoomRedraw: [2k] draw room k, [2k+1] its collected-list base (0/12/24/36)
    uint8_t castleLayout = 0;      // DS:8F9E g_castleLayout (record +0x166)

    // Camera and frame latches (Frame_Begin 130f:025A).
    uint16_t cameraX = 0;          // DS:37EA g_cameraX
    uint16_t cameraY = 0;          // DS:37EC g_cameraY
    int16_t camStepX = 0;          // DS:37EE g_camStepX (step of the last Player_Move)
    int16_t camStepY = 0;          // DS:37F0 g_camStepY
    uint16_t frameCamX = 0;        // DS:37F2 g_frameCamX
    uint16_t frameCamY = 0;        // DS:37F4 g_frameCamY
    int16_t frameCamStepX = 0;     // DS:37F6 g_frameCamStepX
    int16_t frameCamStepY = 0;     // DS:37F8 g_frameCamStepY
    uint16_t slowScroll = 0;       // DS:3D31 g_slowScroll
    std::array<uint8_t, 4> frameInShelfRoom{};  // DS:8F9F g_frameInShelfRoom (latch of DS:9201..9204)
    uint16_t slowCpuSaved = 0;     // DS:8FA5 g_slowCpuSaved
    DsPtr curObj;                  // DS:8FAB g_curObj: kPlayerObjOff or elfObjOff(i) (stale in visible calls, §4.3)
    uint16_t frameTransition = 0;  // DS:8FB2 g_frameTransition
    uint8_t playfieldRedrawn = 0;  // DS:8FBA g_playfieldRedrawn
    uint16_t fullRedraw = 0;       // DS:8FBB g_fullRedraw (latch of g_fullRedrawReq; a word in every access)
    uint8_t frameRankState = 0;    // DS:8FBD g_frameRankState (latch of g_rankState)
    uint16_t loadedScene = 0;      // DS:8FBE g_loadedScene (0xFF none)
    int16_t cellScreenX = 0;       // DS:8FC0 g_cellScreenX
    int16_t cellScreenY = 0;       // DS:8FC2 g_cellScreenY
    uint16_t decorCount = 0;       // DS:8FC4 g_decorCount
    uint16_t framePartialRedraw = 0;  // DS:8FC8 g_framePartialRedraw
    uint16_t sceneWanted = 0;      // DS:8FCA g_sceneWanted
    uint16_t castleCellIdx = 0;    // DS:90FA g_castleCellIdx
    uint16_t camCellCol = 0;       // DS:9124 g_camCellCol
    uint16_t camCellRow16 = 0;     // DS:9126 g_camCellRow16
    uint8_t shelfCamRoom = 0;      // DS:9129 g_shelfCamRoom
    std::array<uint8_t, 4> inShelfRoom{};  // DS:9201 g_inShelfRoom
    uint16_t camTileColOff = 0;    // DS:920D g_camTileColOff
    uint16_t camTileRowOff = 0;    // DS:920F g_camTileRowOff
    uint16_t transition = 0;       // DS:9217 g_transition (1 or 2)
    uint16_t fullRedrawReq = 0;    // DS:924F g_fullRedrawReq
    uint16_t creatureStrike = 0;   // DS:9264 g_creatureStrike (anim op -6)
    uint16_t drawnCellCol = 0;     // DS:9270 g_drawnCellCol
    uint16_t drawnCellRow16 = 0;   // DS:9272 g_drawnCellRow16
    uint16_t cellCallKind = 0;     // DS:9274 g_cellCallKind: 1 visible call, 2 object call
    uint16_t partialRedrawReq = 0; // DS:9278 g_partialRedrawReq
    uint16_t creatureCell = 0;     // DS:9287 g_creatureCell
    uint16_t drawnCamX = 0;        // DS:9297 g_drawnCamX
    uint16_t drawnTileColOff = 0;  // DS:9299 g_drawnTileColOff
    uint16_t drawnCamY = 0;        // DS:929B g_drawnCamY
    uint16_t drawnTileRowOff = 0;  // DS:929D g_drawnTileRowOff
    uint8_t borderColor = 0;       // DS:92C8 g_borderColor
    uint16_t cellMask = 0;         // DS:92C9 g_cellMask
    uint16_t curCell = 0;          // DS:92CB g_curCell

    // Engine only (no DGROUP address, so not in visit()): the dissolve's cell counter, the local DI
    // of Frame_Present 130f:01e7, kept across the frames the engine spreads the 350 cells over
    // (camera.h transitionDissolve); 0 = no dissolve running.
    uint16_t dissolveCellsDone = 0;

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_cellPicture", dg::kCellPicture, s.cellPicture);
        v("g_cellType", dg::kCellType, s.cellType);
        v("g_cellContent", dg::kCellContent, s.cellContent);
        v("g_levelPicData", dg::kLevelPicData, s.levelPicData);
        v("DS_036C_menuBarHidden", uint16_t{0x036C}, s.menuBarHidden);
        v("g_shelfRoomRedraw", dg::kShelfRoomRedraw, s.shelfRoomRedraw);
        v("g_castleLayout", dg::kCastleLayout, s.castleLayout);
        v("g_cameraX", dg::kCameraX, s.cameraX);
        v("g_cameraY", dg::kCameraY, s.cameraY);
        v("g_camStepX", dg::kCamStepX, s.camStepX);
        v("g_camStepY", dg::kCamStepY, s.camStepY);
        v("g_frameCamX", dg::kFrameCamX, s.frameCamX);
        v("g_frameCamY", dg::kFrameCamY, s.frameCamY);
        v("g_frameCamStepX", dg::kFrameCamStepX, s.frameCamStepX);
        v("g_frameCamStepY", dg::kFrameCamStepY, s.frameCamStepY);
        v("g_slowScroll", dg::kSlowScroll, s.slowScroll);
        v("g_frameInShelfRoom", dg::kFrameInShelfRoom, s.frameInShelfRoom);
        v("g_slowCpuSaved", dg::kSlowCpuSaved, s.slowCpuSaved);
        v("g_curObj", dg::kCurObj, s.curObj);
        v("g_frameTransition", dg::kFrameTransition, s.frameTransition);
        v("g_playfieldRedrawn", dg::kPlayfieldRedrawn, s.playfieldRedrawn);
        v("g_fullRedraw", dg::kFullRedraw, s.fullRedraw);
        v("g_frameRankState", dg::kFrameRankState, s.frameRankState);
        v("g_loadedScene", dg::kLoadedScene, s.loadedScene);
        v("g_cellScreenX", dg::kCellScreenX, s.cellScreenX);
        v("g_cellScreenY", dg::kCellScreenY, s.cellScreenY);
        v("g_decorCount", dg::kDecorCount, s.decorCount);
        v("g_framePartialRedraw", dg::kFramePartialRedraw, s.framePartialRedraw);
        v("g_sceneWanted", dg::kSceneWanted, s.sceneWanted);
        v("g_castleCellIdx", dg::kCastleCellIdx, s.castleCellIdx);
        v("g_camCellCol", dg::kCamCellCol, s.camCellCol);
        v("g_camCellRow16", dg::kCamCellRow16, s.camCellRow16);
        v("g_shelfCamRoom", dg::kShelfCamRoom, s.shelfCamRoom);
        v("g_inShelfRoom", dg::kInShelfRoom, s.inShelfRoom);
        v("g_camTileColOff", dg::kCamTileColOff, s.camTileColOff);
        v("g_camTileRowOff", dg::kCamTileRowOff, s.camTileRowOff);
        v("g_transition", dg::kTransition, s.transition);
        v("g_fullRedrawReq", dg::kFullRedrawReq, s.fullRedrawReq);
        v("g_creatureStrike", dg::kCreatureStrike, s.creatureStrike);
        v("g_drawnCellCol", dg::kDrawnCellCol, s.drawnCellCol);
        v("g_drawnCellRow16", dg::kDrawnCellRow16, s.drawnCellRow16);
        v("g_cellCallKind", dg::kCellCallKind, s.cellCallKind);
        v("g_partialRedrawReq", dg::kPartialRedrawReq, s.partialRedrawReq);
        v("g_creatureCell", dg::kCreatureCell, s.creatureCell);
        v("g_drawnCamX", dg::kDrawnCamX, s.drawnCamX);
        v("g_drawnTileColOff", dg::kDrawnTileColOff, s.drawnTileColOff);
        v("g_drawnCamY", dg::kDrawnCamY, s.drawnCamY);
        v("g_drawnTileRowOff", dg::kDrawnTileRowOff, s.drawnTileRowOff);
        v("g_borderColor", dg::kBorderColor, s.borderColor);
        v("g_cellMask", dg::kCellMask, s.cellMask);
        v("g_curCell", dg::kCurCell, s.curCell);
    }
};

// ----------------------------------------------------------------------------------- player
// player.c (18cf), anim.c (4306), search.c (4a03), end sequence and treasure room (176a end part),
// rank chest (130f:0EDF). code_player.md §3.1, §4.1, §6.3, §7.1. Owner: player agent.
struct PlayerState {
    Obj obj;                        // DS:37E2 g_player (+4 = g_playerDX DS:37E6, +6 = g_playerDY DS:37E8)
    // Treasure room (initialised data; file values 0xFFFF, 0xFFFF, 0xFF, 0, 0x50, 0x50).
    int16_t shackSavedShown = 0;    // DS:00F6 g_shackSavedShown
    int16_t shackPartner1 = 0;      // DS:00F8 g_shackPartner1
    int8_t shackPartner2 = 0;       // DS:00FA g_shackPartner2
    uint8_t shackState = 0;         // DS:00FB g_shackState
    uint16_t shackPartner1X = 0;    // DS:00FC g_shackPartner1X
    uint16_t shackPartner2X = 0;    // DS:00FE g_shackPartner2X
    uint16_t champFlashTimer = 0;   // DS:0100 g_champFlashTimer
    // The animation scripts as a writable copy (the end sequence patches the frame word of script
    // 0x88, g_animScript88Frame DS:48DE). Word i = DS:3D36 + 2i; g_animScripts DS:4F5A points in.
    std::array<int16_t, kAnimScriptWords> animScriptData{};  // DS:3D36 g_animScriptData

    int16_t searchTimer = 0;        // DS:8FA3 g_searchTimer
    int16_t playerScrX = 0;         // DS:8FA7 g_playerScrX (Game_Tick)
    int16_t playerScrY = 0;         // DS:8FA9 g_playerScrY
    uint8_t shackNoCase = 0;        // DS:8FAF g_shackNoCase (never cleared)
    int16_t shownTreasure = 0;      // DS:8FB0 g_shownTreasure (record +0x164)
    uint16_t searchOpenBaseL0 = 0;  // DS:8FB4 (unnamed): base of level 0's opened pictures, 0x32 (Game_InitVars)
    uint16_t searchCell = 0;        // DS:8FC6 g_searchCell
    std::array<SpriteSlot, kSpriteSlots> slots{};  // DS:8FD6 g_spriteSlots (slot 1 = player, DS:9002 = slot 2 x)
    uint8_t searchPicture = 0;      // DS:9128 g_searchPicture
    uint16_t playerState = 0;       // DS:913C g_playerState
    int16_t shackY = 0;             // DS:9211 g_shackY
    uint16_t shackX = 0;            // DS:9213 g_shackX
    uint8_t showcasePick = 0;       // DS:9219 g_showcasePick
    uint16_t animInitMark = 0;      // DS:9236 (unnamed): 0 written by Anim_InitSlots, never read
    uint16_t keysUnmasked = 0;      // DS:923A g_keysUnmasked
    uint8_t shackFlags = 0;         // DS:9251 g_shackFlags
    int8_t storedShowcase = 0;      // DS:9254 g_storedShowcase
    uint8_t rankState = 0;          // DS:925D g_rankState
    uint16_t playerMask = 0;        // DS:926A g_playerMask
    int16_t searchSide = 0;         // DS:9276 g_searchSide
    uint8_t shackStay = 0;          // DS:9282 g_shackStay
    uint16_t rankRowsDone = 0;      // DS:92A6 g_rankRowsDone
    uint16_t rankRowsWanted = 0;    // DS:92A8 g_rankRowsWanted
    uint16_t rankTickPhase = 0;     // DS:92B0 g_rankTickPhase
    int16_t endCount = 0;           // DS:92D0 g_endCount
    uint16_t endSeqDone = 0;        // DS:92D2 g_endSeqDone
    uint16_t endScanIdx = 0;        // DS:92D4 g_endScanIdx
    uint16_t endAllFound = 0;       // DS:92D6 g_endAllFound
    uint16_t endPathIdx = 0;        // DS:92D8 g_endPathIdx
    uint16_t endTreasureCount = 0;  // DS:92DA g_endTreasureCount

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_player", dg::kPlayer, s.obj);
        v("g_shackSavedShown", dg::kShackSavedShown, s.shackSavedShown);
        v("g_shackPartner1", dg::kShackPartner1, s.shackPartner1);
        v("g_shackPartner2", dg::kShackPartner2, s.shackPartner2);
        v("g_shackState", dg::kShackState, s.shackState);
        v("g_shackPartner1X", dg::kShackPartner1X, s.shackPartner1X);
        v("g_shackPartner2X", dg::kShackPartner2X, s.shackPartner2X);
        v("g_champFlashTimer", dg::kChampFlashTimer, s.champFlashTimer);
        v("g_animScriptData", dg::kAnimScriptData, s.animScriptData);
        v("g_searchTimer", dg::kSearchTimer, s.searchTimer);
        v("g_playerScrX", dg::kPlayerScrX, s.playerScrX);
        v("g_playerScrY", dg::kPlayerScrY, s.playerScrY);
        v("g_shackNoCase", dg::kShackNoCase, s.shackNoCase);
        v("g_shownTreasure", dg::kShownTreasure, s.shownTreasure);
        v("DS_8FB4_searchOpenBaseL0", uint16_t{0x8FB4}, s.searchOpenBaseL0);
        v("g_searchCell", dg::kSearchCell, s.searchCell);
        v("g_spriteSlots", dg::kSpriteSlots, s.slots);
        v("g_searchPicture", dg::kSearchPicture, s.searchPicture);
        v("g_playerState", dg::kPlayerState, s.playerState);
        v("g_shackY", dg::kShackY, s.shackY);
        v("g_shackX", dg::kShackX, s.shackX);
        v("g_showcasePick", dg::kShowcasePick, s.showcasePick);
        v("DS_9236_animInitMark", uint16_t{0x9236}, s.animInitMark);
        v("g_keysUnmasked", dg::kKeysUnmasked, s.keysUnmasked);
        v("g_shackFlags", dg::kShackFlags, s.shackFlags);
        v("g_storedShowcase", dg::kStoredShowcase, s.storedShowcase);
        v("g_rankState", dg::kRankState, s.rankState);
        v("g_playerMask", dg::kPlayerMask, s.playerMask);
        v("g_searchSide", dg::kSearchSide, s.searchSide);
        v("g_shackStay", dg::kShackStay, s.shackStay);
        v("g_rankRowsDone", dg::kRankRowsDone, s.rankRowsDone);
        v("g_rankRowsWanted", dg::kRankRowsWanted, s.rankRowsWanted);
        v("g_rankTickPhase", dg::kRankTickPhase, s.rankTickPhase);
        v("g_endCount", dg::kEndCount, s.endCount);
        v("g_endSeqDone", dg::kEndSeqDone, s.endSeqDone);
        v("g_endScanIdx", dg::kEndScanIdx, s.endScanIdx);
        v("g_endAllFound", dg::kEndAllFound, s.endAllFound);
        v("g_endPathIdx", dg::kEndPathIdx, s.endPathIdx);
        v("g_endTreasureCount", dg::kEndTreasureCount, s.endTreasureCount);
    }
};

// ------------------------------------------------------------------------------------ elves
// elves.c (4521), layout.c (1a54), hints.c (49a6). code_elves_layout_hints.md §3.2, §5.1, §6.1.
// Owner: elves agent.
struct ElfState {
    std::array<Elf, kElfCount> elves{};  // DS:37FA g_elves
    // The hint and message texts as a writable copy (Gameover_UpdateMessages patches digits, the
    // plural letter and the player's name into them; the patches last for the session). Byte i =
    // DS:6BC0 + i; g_flutterText DS:7CA3 points in.
    std::array<uint8_t, kMessageTextEnd - kMessageTextStart> messageText{};  // DS:6BC0..7CA2
    uint16_t crownMsgToggle = 0;    // DS:7D79 g_crownMsgToggle (1 in the file)
    uint16_t messagePending = 0;    // DS:911C g_messagePending
    uint16_t elfSpriteCount = 0;    // DS:9132 g_elfSpriteCount
    uint16_t hintTimerReset = 0;    // DS:913F g_hintTimerReset
    uint16_t hintsOn = 0;           // DS:9224 g_hintsOn
    uint16_t lastGameTreasures = 0; // DS:9232 g_lastGameTreasures
    std::array<uint8_t, 15> hintFlags{};  // DS:923E g_hintFlags: 1 waiting, 0 due, 0x80 shown
    uint16_t keyCellL1 = 0;         // DS:927A g_keyCellL1 (written, never read)
    uint16_t keyCellL2 = 0;         // DS:927C g_keyCellL2
    uint16_t lastGameTier = 0;      // DS:9289 g_lastGameTier
    int8_t keyPicture = 0;          // DS:9295 g_keyPicture
    uint8_t clueScheme = 0;         // DS:9296 g_clueScheme
    int16_t elfRelX = 0;            // DS:A204 g_elfRelX
    int16_t elfRelY = 0;            // DS:A206 g_elfRelY
    int16_t playerRelX = 0;         // DS:A208 g_playerRelX
    uint16_t netCatchesThisTick = 0;  // DS:A20A g_netCatchesThisTick
    uint16_t elfNetClass = 0;       // DS:A20C g_elfNetClass
    uint16_t elfCallCounter = 0;    // DS:A20E g_elfCallCounter
    uint16_t elfAnimSel = 0;        // DS:A210 g_elfAnimSel (stale value read by ElfSt_03)
    uint16_t elfActiveCount = 0;    // DS:A212 g_elfActiveCount
    uint16_t hint3Level = 0;        // DS:C6CE g_hint3Level
    uint16_t hint3Start = 0;        // DS:C6D0 g_hint3Start
    uint16_t hintKeyState = 0;      // DS:C6D4 (unnamed): key state copied at a hint-timer reset, never read
    uint16_t hintSavedPrompt = 0;   // DS:C6D6 (unnamed): g_promptIcon saved while a bubble is open
    uint16_t hintTimerBase = 0;     // DS:C6D8 (unnamed): hint timer base (0 after a reset)
    uint16_t hintButtons = 0;       // DS:C6DA (unnamed): mouse buttons when the bubble opened

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_elves", dg::kElves, s.elves);
        v("DS_6BC0_messageText", kMessageTextStart, s.messageText);
        v("g_crownMsgToggle", dg::kCrownMsgToggle, s.crownMsgToggle);
        v("g_messagePending", dg::kMessagePending, s.messagePending);
        v("g_elfSpriteCount", dg::kElfSpriteCount, s.elfSpriteCount);
        v("g_hintTimerReset", dg::kHintTimerReset, s.hintTimerReset);
        v("g_hintsOn", dg::kHintsOn, s.hintsOn);
        v("g_lastGameTreasures", dg::kLastGameTreasures, s.lastGameTreasures);
        v("g_hintFlags", dg::kHintFlags, s.hintFlags);
        v("g_keyCellL1", dg::kKeyCellL1, s.keyCellL1);
        v("g_keyCellL2", dg::kKeyCellL2, s.keyCellL2);
        v("g_lastGameTier", dg::kLastGameTier, s.lastGameTier);
        v("g_keyPicture", dg::kKeyPicture, s.keyPicture);
        v("g_clueScheme", dg::kClueScheme, s.clueScheme);
        v("g_elfRelX", dg::kElfRelX, s.elfRelX);
        v("g_elfRelY", dg::kElfRelY, s.elfRelY);
        v("g_playerRelX", dg::kPlayerRelX, s.playerRelX);
        v("g_netCatchesThisTick", dg::kNetCatchesThisTick, s.netCatchesThisTick);
        v("g_elfNetClass", dg::kElfNetClass, s.elfNetClass);
        v("g_elfCallCounter", dg::kElfCallCounter, s.elfCallCounter);
        v("g_elfAnimSel", dg::kElfAnimSel, s.elfAnimSel);
        v("g_elfActiveCount", dg::kElfActiveCount, s.elfActiveCount);
        v("g_hint3Level", dg::kHint3Level, s.hint3Level);
        v("g_hint3Start", dg::kHint3Start, s.hint3Start);
        v("DS_C6D4_hintKeyState", uint16_t{0xC6D4}, s.hintKeyState);
        v("DS_C6D6_hintSavedPrompt", uint16_t{0xC6D6}, s.hintSavedPrompt);
        v("DS_C6D8_hintTimerBase", uint16_t{0xC6D8}, s.hintTimerBase);
        v("DS_C6DA_hintButtons", uint16_t{0xC6DA}, s.hintButtons);
    }
};

// --------------------------------------------------------------------------------------- ui
// ui.c (43cb), title.c (4ba4), signin.c (47c7), quiz.c (46ce), HUD (130f). code_ui.md §2, §4-§11.
// Owner: ui agent.
struct UiState {
    uint16_t boxSaved = 0;          // DS:0102 g_boxSaved (slow CPU: a box background was saved)
    uint16_t lineHeight = 0;        // DS:5B3C g_lineHeight (12 in the file)
    uint16_t dialogShown = 0;       // DS:5B3E g_dialogShown
    // The default-button bytes of boxes 6, 7, 8 of g_msgBoxes (+6, with the pad byte +7: the code
    // writes them as words), rewritten with the current setting (code_ui.md §4.1); the other
    // records never change (UiTables::msgBoxes).
    uint16_t hintsBoxDefault = 0;   // DS:5A32 (box 6 +6)
    uint16_t musicBoxDefault = 0;   // DS:5A42 (box 7 +6, 1 in the file)
    uint16_t soundBoxDefault = 0;   // DS:5A52 (box 8 +6)
    uint16_t soundChanged = 0;      // DS:5BC5 g_soundChanged
    uint16_t helpLoaded = 0;        // DS:5BEB g_helpLoaded (Help_LoadFile)
    uint8_t signinInit = 0;         // DS:6168 g_signinInit (1 in the file)
    uint8_t usersLoaded = 0;        // DS:6169 g_usersLoaded
    uint16_t ssPage = 0;            // DS:8A14 g_ssPage
    uint16_t titlePhase = 0;        // DS:8A16 g_titlePhase
    uint16_t promptIcon = 0;        // DS:90F8 g_promptIcon (record +0x14C)
    int16_t menuOpen = 0;           // DS:90FC g_menuOpen
    uint16_t menuItem = 0;          // DS:9111 g_menuItem
    uint16_t coinsShown = 0;        // DS:9122 g_coinsShown
    uint8_t screen = 0;             // DS:913E g_screen (a byte in every access; painter 0 HUD, 1 quiz, 2 sign-in)
    uint16_t hudPrompt = 0;         // DS:91FB g_hudPrompt
    uint16_t textInk = 0;           // DS:9215 g_textInk
    uint16_t quizClosed = 0;        // DS:9220 g_quizClosed
    // g_userRec DS:9228 and the record field pointers (DS:8FB6, 8FD2, 9101, 9107, 9138, 91FD, 9209,
    // 921A, 927E) are far pointers into the USERS.DAT image in the original; the engine keeps the
    // record index (dumpToDgroup writes the pointers from it, loadFromDgroup derives it).
    int16_t userRec = -1;           // DS:9228 g_userRec: selected record 0..8, -1 none (not visited)
    uint16_t hudRedraw = 0;         // DS:922E g_hudRedraw
    uint16_t hudNets = 0;           // DS:924D g_hudNets
    uint16_t hudFound = 0;          // DS:9259 g_hudFound
    uint16_t hudRedrawReq2 = 0;     // DS:925B g_hudRedrawReq2
    uint16_t hudKeyState = 0;       // DS:9268 g_hudKeyState
    uint16_t hudCoins = 0;          // DS:928D g_hudCoins
    int8_t helpPage = 0;            // DS:9301 g_helpPage
    // Help_DrawPage 43cb:10c4 keeps the page's pictures in globals (code_ui.md §6.1 step 5).
    int16_t helpPicX1 = 0;          // DS:9302 (unnamed): x of the first picture
    int16_t helpPicX2 = 0;          // DS:9304 (unnamed): x of the second picture
    int16_t helpPicFrame1 = 0;      // DS:9306 (unnamed): first picture (frame * 2, -1 none)
    int16_t helpPicFrame2 = 0;      // DS:9308 (unnamed): second picture
    int16_t helpPicY = 0;           // DS:930A (unnamed): y of both pictures
    int16_t uiCmd = 0;              // DS:930C g_uiCmd
    std::array<DsPtr, 2> buttonLabel{};      // DS:930E g_buttonLabel (DS address of the label string)
    std::array<uint16_t, 2> buttonX{};       // DS:9316 g_buttonX
    std::array<uint16_t, 2> buttonY{};       // DS:931A g_buttonY
    uint16_t boxWidth = 0;          // DS:931E g_boxWidth
    uint8_t helpPageCount = 0;      // DS:9322 g_helpPageCount
    uint16_t dialogId = 0;          // DS:9323 g_dialogId
    uint16_t buttonSel = 0;         // DS:A1FD g_buttonSel
    uint16_t boxTextY = 0;          // DS:A1FF g_boxTextY
    uint16_t boxLines = 0;          // DS:A201 g_boxLines
    // Elf questions.
    int16_t quizFocus = 0;          // DS:A214 g_quizFocus
    uint16_t quizQuestion = 0;      // DS:A216 g_quizQuestion
    uint16_t quizFileLength = 0;    // DS:A21A (unnamed): low word of the PRG_DICT / ALL_PUZS length
    uint16_t quizSlot = 0;          // DS:A21C g_quizSlot
    uint16_t quizOrder = 0;         // DS:A21E g_quizOrder
    DsPtr praiseWord;               // DS:A220 g_praiseWord (DS address of the clue word string)
    uint16_t tierEnd = 0;           // DS:A224 g_tierEnd
    std::array<std::array<char, 35>, 3> answerText{};  // DS:A22A g_answerText
    std::array<char, 1000> questionText{};             // DS:A293 g_questionText (to DS:A67A)
    std::array<uint8_t, 9> dealPlan{};                 // DS:A67B g_dealPlan
    std::array<DsPtr, 9> puzzleText{};                 // DS:A684 g_puzzleText (into expandedText)
    uint16_t answerWidth = 0;       // DS:A6A8 g_answerWidth
    uint16_t tierFirst = 0;         // DS:A6AA g_tierFirst
    std::array<uint16_t, 4> statusCount{};  // DS:A6AC g_statusCount
    DsPtr openText;                 // DS:A6B4 (unnamed): text of the open question (Quiz_PickQuestion)
    std::array<char, 0xB40> expandedText{};  // DS:A6B8..B1F7 (unnamed): the 9 dealt questions, expanded
    uint16_t quizCoins = 0;         // DS:B1F8 g_quizCoins
    uint16_t quizAnswered = 0;      // DS:B1FA g_quizAnswered
    std::array<uint16_t, 4> dealAvail{};  // DS:B1FC (unnamed): Quiz_PlanStatuses' copy of the counts
    uint8_t quizMask = 0;           // DS:BA04 g_quizMask
    // Sign-in.
    uint8_t signinChar = 0;         // DS:BA06 g_signinChar
    uint16_t signinState = 0;       // DS:BA07 g_signinState
    uint8_t caretArmed = 0;         // DS:BA09 g_caretArmed
    std::array<char, 9> newName{};  // DS:BA0A g_newName
    int8_t signinRow = 0;           // DS:BA13 g_signinRow
    int8_t newNameLen = 0;          // DS:C6BF g_newNameLen
    uint8_t signinAny = 0;          // DS:C6C0 g_signinAny
    std::array<uint8_t, 9> signinRecIdx{};  // DS:C6C1 g_signinRecIdx
    uint8_t caretOn = 0;            // DS:C6CA g_caretOn
    uint8_t signinStart = 0;        // DS:C6CB g_signinStart
    int8_t signinCount = 0;         // DS:C6CC g_signinCount
    uint32_t titleTimer = 0;        // DS:CB7E g_titleTimer
    // Engine state without a DS address (not visited): Button_Flash 43cb:0eb6 as a wait that spans
    // frames (ENGINE_MAP §3.4; ui.h uiFlashPending), and the line height Title_Run 4ba4:0434 keeps in
    // a local while its loop runs (titleStart .. titleFinish).
    uint8_t flashStep = 0;          // toggles drawn so far, 1..4; 0 = no flash running
    uint8_t flashButton = 0;        // the button that blinks (g_buttonSel when the flash began)
    uint32_t flashStart = 0;        // g_ticks when the flash began (toggle k at +18k, the end at +72)
    uint16_t titleSavedLineHeight = 0;
    // The reference machine's clock of Title_Run's loop and of the coin count-up (wave 4; title.cpp,
    // hud.cpp), in thousandths of a PIT tick on the g_ticks scale (g_ticks n = n * 1000).
    uint64_t titleLoopAt = 0;       // when the title loop's next iteration runs
    uint64_t sparkleFrom = 0;       // when the loop's first sparkle attempt of slot 2's idle time ran
    uint32_t sparkleAttempts = 0;   // attempts made since then
    bool sparkleIdle = false;       // slot 2 was idle (and attempts running) at the last iteration
    uint64_t sparkleEndAt = 0;      // the game tick that ended slot 2's script (0 = none pending)
    uint64_t countUpStart = 0;      // when the running coin count-up drew its first coin
    uint16_t countUpDone = 0;       // coins it has drawn (0 = no count-up running)

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_boxSaved", dg::kBoxSaved, s.boxSaved);
        v("g_lineHeight", dg::kLineHeight, s.lineHeight);
        v("g_dialogShown", dg::kDialogShown, s.dialogShown);
        v("DS_5A32_hintsBoxDefault", uint16_t{0x5A32}, s.hintsBoxDefault);
        v("DS_5A42_musicBoxDefault", uint16_t{0x5A42}, s.musicBoxDefault);
        v("DS_5A52_soundBoxDefault", uint16_t{0x5A52}, s.soundBoxDefault);
        v("g_soundChanged", dg::kSoundChanged, s.soundChanged);
        v("g_helpLoaded", dg::kHelpLoaded, s.helpLoaded);
        v("g_signinInit", dg::kSigninInit, s.signinInit);
        v("g_usersLoaded", dg::kUsersLoaded, s.usersLoaded);
        v("g_ssPage", dg::kSsPage, s.ssPage);
        v("g_titlePhase", dg::kTitlePhase, s.titlePhase);
        v("g_promptIcon", dg::kPromptIcon, s.promptIcon);
        v("g_menuOpen", dg::kMenuOpen, s.menuOpen);
        v("g_menuItem", dg::kMenuItem, s.menuItem);
        v("g_coinsShown", dg::kCoinsShown, s.coinsShown);
        v("g_screen", dg::kScreen, s.screen);
        v("g_hudPrompt", dg::kHudPrompt, s.hudPrompt);
        v("g_textInk", dg::kTextInk, s.textInk);
        v("g_quizClosed", dg::kQuizClosed, s.quizClosed);
        v("g_hudRedraw", dg::kHudRedraw, s.hudRedraw);
        v("g_hudNets", dg::kHudNets, s.hudNets);
        v("g_hudFound", dg::kHudFound, s.hudFound);
        v("g_hudRedrawReq2", dg::kHudRedrawReq2, s.hudRedrawReq2);
        v("g_hudKeyState", dg::kHudKeyState, s.hudKeyState);
        v("g_hudCoins", dg::kHudCoins, s.hudCoins);
        v("g_helpPage", dg::kHelpPage, s.helpPage);
        v("DS_9302_helpPicX1", uint16_t{0x9302}, s.helpPicX1);
        v("DS_9304_helpPicX2", uint16_t{0x9304}, s.helpPicX2);
        v("DS_9306_helpPicFrame1", uint16_t{0x9306}, s.helpPicFrame1);
        v("DS_9308_helpPicFrame2", uint16_t{0x9308}, s.helpPicFrame2);
        v("DS_930A_helpPicY", uint16_t{0x930A}, s.helpPicY);
        v("g_uiCmd", dg::kUiCmd, s.uiCmd);
        v("g_buttonLabel", dg::kButtonLabel, s.buttonLabel);
        v("g_buttonX", dg::kButtonX, s.buttonX);
        v("g_buttonY", dg::kButtonY, s.buttonY);
        v("g_boxWidth", dg::kBoxWidth, s.boxWidth);
        v("g_helpPageCount", dg::kHelpPageCount, s.helpPageCount);
        v("g_dialogId", dg::kDialogId, s.dialogId);
        v("g_buttonSel", dg::kButtonSel, s.buttonSel);
        v("g_boxTextY", dg::kBoxTextY, s.boxTextY);
        v("g_boxLines", dg::kBoxLines, s.boxLines);
        v("g_quizFocus", dg::kQuizFocus, s.quizFocus);
        v("g_quizQuestion", dg::kQuizQuestion, s.quizQuestion);
        v("DS_A21A_quizFileLength", uint16_t{0xA21A}, s.quizFileLength);
        v("g_quizSlot", dg::kQuizSlot, s.quizSlot);
        v("g_quizOrder", dg::kQuizOrder, s.quizOrder);
        v("g_praiseWord", dg::kPraiseWord, s.praiseWord);
        v("g_tierEnd", dg::kTierEnd, s.tierEnd);
        v("g_answerText", dg::kAnswerText, s.answerText);
        v("g_questionText", dg::kQuestionText, s.questionText);
        v("g_dealPlan", dg::kDealPlan, s.dealPlan);
        v("g_puzzleText", dg::kPuzzleText, s.puzzleText);
        v("g_answerWidth", dg::kAnswerWidth, s.answerWidth);
        v("g_tierFirst", dg::kTierFirst, s.tierFirst);
        v("g_statusCount", dg::kStatusCount, s.statusCount);
        v("DS_A6B4_openText", uint16_t{0xA6B4}, s.openText);
        v("DS_A6B8_expandedText", uint16_t{0xA6B8}, s.expandedText);
        v("g_quizCoins", dg::kQuizCoins, s.quizCoins);
        v("g_quizAnswered", dg::kQuizAnswered, s.quizAnswered);
        v("DS_B1FC_dealAvail", uint16_t{0xB1FC}, s.dealAvail);
        v("g_quizMask", dg::kQuizMask, s.quizMask);
        v("g_signinChar", dg::kSigninChar, s.signinChar);
        v("g_signinState", dg::kSigninState, s.signinState);
        v("g_caretArmed", dg::kCaretArmed, s.caretArmed);
        v("g_newName", dg::kNewName, s.newName);
        v("g_signinRow", dg::kSigninRow, s.signinRow);
        v("g_newNameLen", dg::kNewNameLen, s.newNameLen);
        v("g_signinAny", dg::kSigninAny, s.signinAny);
        v("g_signinRecIdx", dg::kSigninRecIdx, s.signinRecIdx);
        v("g_caretOn", dg::kCaretOn, s.caretOn);
        v("g_signinStart", dg::kSigninStart, s.signinStart);
        v("g_signinCount", dg::kSigninCount, s.signinCount);
        v("g_titleTimer", dg::kTitleTimer, s.titleTimer);
    }
};

// ------------------------------------------------------------------------------------- core
// main.c (130f main loop), init.c (1460) and the game-wide counters that every module reads and
// writes (most are saved in the player record). code_runtime.md §3, §4.4, §8. Owner: mainloop agent.
struct CoreState {
    uint8_t machineId = 0;          // DS:1300 g_machineId (0x21 = Tandy 1000)
    uint16_t musicGate = 0;         // DS:37CE g_musicGate (0xFFFF in the file; 0 pauses the music)
    uint16_t soundOn = 0;           // DS:63B4 g_soundOn (1 in the file)
    uint16_t musicOn = 0;           // DS:63B6 g_musicOn (1 in the file)
    uint16_t sfxLoop = 0;           // DS:63B8 g_sfxLoop (set by the end sequence)
    uint16_t musicLoop = 0;         // DS:63BA g_musicLoop
    uint16_t soundDevice = 0;       // DS:63BC g_soundDevice (0xFFFF in the file; 0 speaker, 1 Tandy, 2 AdLib)
    uint16_t area = 0;              // DS:910B g_area (record +0x156; music by area)
    uint16_t nets = 0;              // DS:910F g_nets (record +0x146)
    uint16_t treasuresFound = 0;    // DS:9113 g_treasuresFound (record +0x148)
    uint8_t uiMode = 0;             // DS:9119 g_uiMode: 0x80 title, 0x40 menu, 0x20 dialog, 0x10 hint, 8 sign-in, 4 message, 2 quiz
    uint16_t keyState = 0;          // DS:9120 g_keyState (record +0x14A)
    int16_t mouse = 0;              // DS:912A g_mouse: -1 when a mouse driver is present
    int16_t videoType = 0;          // DS:912C g_videoType: 1 EGA, 2 CGA, 3 Tandy, 4 Hercules, 5 MCGA
    uint16_t quit = 0;              // DS:912E g_quit (the engine's loads are instant, so it only ends the game)
    uint16_t tier = 0;              // DS:9130 g_tier (record +0x140)
    uint16_t coins = 0;             // DS:9134 g_coins (record +0x144)
    uint16_t startDrive = 0;        // DS:9136 g_startDrive (getdisk(); 2 = C: keeps the floppy paths off)
    uint16_t gameInProgress = 0;    // DS:9205 g_gameInProgress
    uint16_t caughtSlot = 0;        // DS:921E g_caughtSlot
    uint16_t endState = 0;          // DS:9234 g_endState
    uint16_t slowCpu = 0;           // DS:923C g_slowCpu
    uint16_t dirty = 0;             // DS:9255 g_dirty
    uint16_t unknown9260 = 0;       // DS:9260 (unnamed): zeroed by Game_InitVars, no reader known
    uint16_t unknown9262 = 0;       // DS:9262 (unnamed): zeroed by Game_InitVars, no reader known
    uint16_t menuLock = 0;          // DS:926C g_menuLock
    uint16_t gamesPlayed = 0;       // DS:926E g_gamesPlayed (record +0x162)
    uint16_t spriteSlotsDrawn = 0;  // DS:9283 g_spriteSlotsDrawn (13 or 1)
    uint16_t unknown928B = 0;       // DS:928B (unnamed): 6 from Game_InitVars, no reader known
    uint16_t level = 0;             // DS:928F g_level: 0..2 mountain levels, 3 castle top, 4 treasure room
    uint16_t unknown9291 = 0;       // DS:9291 (unnamed): zeroed by Game_InitVars, no reader known
    int16_t shopCoinsOwed = 0;      // DS:92A1 g_shopCoinsOwed (record +0x158)
    uint8_t lastUiMode = 0;         // DS:9300 g_lastUiMode (Sound_SyncState)

    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        v("g_machineId", dg::kMachineId, s.machineId);
        v("g_musicGate", dg::kMusicGate, s.musicGate);
        v("g_soundOn", dg::kSoundOn, s.soundOn);
        v("g_musicOn", dg::kMusicOn, s.musicOn);
        v("g_sfxLoop", dg::kSfxLoop, s.sfxLoop);
        v("g_musicLoop", dg::kMusicLoop, s.musicLoop);
        v("g_soundDevice", dg::kSoundDevice, s.soundDevice);
        v("g_area", dg::kArea, s.area);
        v("g_nets", dg::kNets, s.nets);
        v("g_treasuresFound", dg::kTreasuresFound, s.treasuresFound);
        v("g_uiMode", dg::kUiMode, s.uiMode);
        v("g_keyState", dg::kKeyState, s.keyState);
        v("g_mouse", dg::kMouse, s.mouse);
        v("g_videoType", dg::kVideoType, s.videoType);
        v("g_quit", dg::kQuit, s.quit);
        v("g_tier", dg::kTier, s.tier);
        v("g_coins", dg::kCoins, s.coins);
        v("g_startDrive", dg::kStartDrive, s.startDrive);
        v("g_gameInProgress", dg::kGameInProgress, s.gameInProgress);
        v("g_caughtSlot", dg::kCaughtSlot, s.caughtSlot);
        v("g_endState", dg::kEndState, s.endState);
        v("g_slowCpu", dg::kSlowCpu, s.slowCpu);
        v("g_dirty", dg::kDirty, s.dirty);
        v("DS_9260", uint16_t{0x9260}, s.unknown9260);
        v("DS_9262", uint16_t{0x9262}, s.unknown9262);
        v("g_menuLock", dg::kMenuLock, s.menuLock);
        v("g_gamesPlayed", dg::kGamesPlayed, s.gamesPlayed);
        v("g_spriteSlotsDrawn", dg::kSpriteSlotsDrawn, s.spriteSlotsDrawn);
        v("DS_928B", uint16_t{0x928B}, s.unknown928B);
        v("g_level", dg::kLevel, s.level);
        v("DS_9291", uint16_t{0x9291}, s.unknown9291);
        v("g_shopCoinsOwed", dg::kShopCoinsOwed, s.shopCoinsOwed);
        v("g_lastUiMode", dg::kLastUiMode, s.lastUiMode);
    }
};

// ------------------------------------------------------------------------------- GameState

struct GameState {
    WorldState world;
    PlayerState player;
    ElfState elves;
    UiState ui;
    CoreState core;

    // Every field, section by section: v(name, DS address, field).
    template <class Self, class V>
    static void visit(Self& s, V&& v) {
        WorldState::visit(s.world, v);
        PlayerState::visit(s.player, v);
        ElfState::visit(s.elves, v);
        UiState::visit(s.ui, v);
        CoreState::visit(s.core, v);
    }
};

// ------------------------------------------------------------------------------- GameTables
// Read-only tables of the EXE, loaded once (strings as DS offsets: DosGame::dsString resolves
// them). Same section rule as GameState: an agent adds tables only to its own section, loading
// them in that section's load().

namespace detail {
// Little-endian array reads from DGROUP (DosExe::dsBytes throws past the image).
template <class T, size_t N>
void readDsArray(const DosExe& exe, uint16_t off, std::array<T, N>& out) {
    const auto b = exe.dsBytes(off, N * sizeof(T));
    for (size_t i = 0; i < N; ++i) {
        uint32_t v = 0;
        for (size_t k = 0; k < sizeof(T); ++k) v |= static_cast<uint32_t>(b[i * sizeof(T) + k]) << (8 * k);
        out[i] = static_cast<T>(v);
    }
}
template <class T, size_t N, size_t M>
void readDsArray(const DosExe& exe, uint16_t off, std::array<std::array<T, M>, N>& out) {
    for (size_t i = 0; i < N; ++i) readDsArray(exe, static_cast<uint16_t>(off + i * M * sizeof(T)), out[i]);
}
// The DS offsets of a table of far pointers into DGROUP (0 for a null pointer).
template <size_t N>
void readDsFarOffsets(const DosExe& exe, uint16_t off, std::array<uint16_t, N>& out) {
    for (size_t i = 0; i < N; ++i) out[i] = exe.readFarPtr(static_cast<uint16_t>(off + 4 * i)).off;
}
// A -1-terminated i16 list (the terminator included).
std::vector<int16_t> readDsMinusOneList(const DosExe& exe, uint16_t off, size_t maxCount);
}  // namespace detail

// World (code_world.md §3.1).
struct WorldTables {
    std::array<uint16_t, 4> creatureMask{};             // DS:0AFA g_creatureMask (by tier)
    std::array<uint16_t, 48> hidingCells{};             // DS:11AA g_hidingCells (use hidingCellAt for slot -1)
    std::array<uint16_t, 9> variableCells{};            // DS:120A g_variableCells
    std::array<uint16_t, 9> variableCellDefaults{};     // DS:121C g_variableCellDefaults
    std::array<std::array<uint16_t, 20>, 4> castleLayouts{};  // DS:1252 g_castleLayouts
    std::array<uint16_t, 3> pictureAttrOffset{};        // DS:1246 g_pictureAttrTables -> 0184 / 0250 / 02EC
    std::array<size_t, 3> pictureAttrCount{};           // records per level (51, 39, 32; record 0 unused)
    uint16_t cellPictureBefore = 0;  // the word at DS:0CA8, read as g_cellPicture[-1] (cellPictureRaw)

    void load(const DosExe& exe) {
        detail::readDsArray(exe, dg::kCreatureMask, creatureMask);
        detail::readDsArray(exe, dg::kHidingCells, hidingCells);
        detail::readDsArray(exe, dg::kVariableCells, variableCells);
        detail::readDsArray(exe, dg::kVariableCellDefaults, variableCellDefaults);
        detail::readDsArray(exe, dg::kCastleLayouts, castleLayouts);
        detail::readDsFarOffsets(exe, dg::kPictureAttrTables, pictureAttrOffset);
        pictureAttrCount = {dg::kPictureAttrL0Count, dg::kPictureAttrL1Count, dg::kPictureAttrL2Count};
        cellPictureBefore = exe.ds16(static_cast<uint16_t>(dg::kCellPicture - 2));
    }
};

// Player, end sequence, rank chest (code_player.md §10).
struct PlayerTables {
    std::array<int16_t, 21> rankTable{};                // DS:0110 g_rankTable: 10 (threshold, value) pairs, -1
    std::array<uint16_t, dg::kAnimScriptsCount> animScriptOffset{};  // DS:4F5A g_animScripts -> DS offsets
    std::vector<int16_t> endFlightPathY;                // DS:0B06 g_endFlightPathY (with the -1)
    std::vector<int16_t> endShowcasePathY;              // DS:0B3C g_endShowcasePathY (with the -1)
    std::array<int16_t, 6> endRankSparkleX{};           // DS:0B5E
    std::array<int16_t, 6> endRankSparkleY{};           // DS:0B6A
    std::array<int16_t, 5> endChampSparkleX{};          // DS:0B76
    std::array<int16_t, 5> endChampSparkleY{};          // DS:0B80

    void load(const DosExe& exe) {
        detail::readDsArray(exe, dg::kRankTable, rankTable);
        detail::readDsFarOffsets(exe, dg::kAnimScripts, animScriptOffset);
        endFlightPathY = detail::readDsMinusOneList(exe, dg::kEndFlightPathY, 64);
        endShowcasePathY = detail::readDsMinusOneList(exe, dg::kEndShowcasePathY, 64);
        detail::readDsArray(exe, dg::kEndRankSparkleX, endRankSparkleX);
        detail::readDsArray(exe, dg::kEndRankSparkleY, endRankSparkleY);
        detail::readDsArray(exe, dg::kEndChampSparkleX, endChampSparkleX);
        detail::readDsArray(exe, dg::kEndChampSparkleY, endChampSparkleY);
    }
};

// Elves, layout clue tables, hints (code_elves_layout_hints.md §3.3, §5.1, §6.1).
struct ElfTables {
    std::array<std::array<uint16_t, 4>, 4> elfCounts{};    // DS:5D34 g_elfCounts [tier][A, B, C, D]
    std::array<std::array<uint16_t, 4>, 3> levelXRange{};  // DS:5D54 g_levelXRange [level]{x base, y, x mask, -}
    std::array<uint16_t, dg::kFlutterTextCount> messageOffset{};  // DS:7CA3 g_flutterText -> DS offsets
    std::array<uint16_t, 7> hintRankThresholds{};          // DS:7D37
    std::array<int8_t, 8> rankStartMsg{};                  // DS:7D45
    std::array<int16_t, 12> msgDigitOffsets{};             // DS:7D4D
    std::array<int16_t, 8> msgPluralOffsets{};             // DS:7D65
    std::array<int8_t, 4> endMsgIds{};                     // DS:7D75
    // Clue word tables (layout's Clue_BuildWords and quiz's Clue_Reveal).
    std::array<uint8_t, 3> clueAttrMask{};                 // DS:5F9B
    std::array<uint8_t, 4> clueSizeMask{};                 // DS:5F9E
    std::array<uint8_t, 3> clueAttrShift{};                // DS:5FA2
    std::array<uint8_t, 4> clueSizeShift{};                // DS:5FA5
    std::array<uint8_t, 5> clueAttrByte{};                 // DS:5FA9
    std::array<uint8_t, 5> clueWordBase{};                 // DS:5FAE
    std::array<std::array<uint8_t, 3>, 3> clueSchemes{};   // DS:5FB3
    std::array<std::array<uint8_t, 3>, 6> clueRevealOrders{};  // DS:5FBC

    void load(const DosExe& exe) {
        detail::readDsArray(exe, dg::kElfCounts, elfCounts);
        detail::readDsArray(exe, dg::kLevelXRange, levelXRange);
        detail::readDsFarOffsets(exe, dg::kFlutterText, messageOffset);
        detail::readDsArray(exe, dg::kHintRankThresholds, hintRankThresholds);
        detail::readDsArray(exe, dg::kRankStartMsg, rankStartMsg);
        detail::readDsArray(exe, dg::kMsgDigitOffsets, msgDigitOffsets);
        detail::readDsArray(exe, dg::kMsgPluralOffsets, msgPluralOffsets);
        detail::readDsArray(exe, dg::kEndMsgIds, endMsgIds);
        detail::readDsArray(exe, dg::kClueAttrMask, clueAttrMask);
        detail::readDsArray(exe, dg::kClueSizeMask, clueSizeMask);
        detail::readDsArray(exe, dg::kClueAttrShift, clueAttrShift);
        detail::readDsArray(exe, dg::kClueSizeShift, clueSizeShift);
        detail::readDsArray(exe, dg::kClueAttrByte, clueAttrByte);
        detail::readDsArray(exe, dg::kClueWordBase, clueWordBase);
        detail::readDsArray(exe, dg::kClueSchemes, clueSchemes);
        detail::readDsArray(exe, dg::kClueRevealOrders, clueRevealOrders);
    }
};

// UI, title, sign-in, quiz (code_ui.md §14).
struct UiTables {
    struct MsgBox {                 // one record of g_msgBoxes DS:59CC (16 bytes)
        uint8_t type = 0;           // +0
        int16_t x1 = 0, y1 = 0;     // +2, +4 (-1 = centred)
        uint8_t defaultButton = 0;  // +6 (boxes 6..8: the state's copy is UiState::*BoxDefault)
        uint16_t title = 0;         // +8 far char* -> DS offset (0 = none)
        uint16_t text = 0;          // +0xC far char* -> DS offset
    };
    std::array<MsgBox, dg::kMsgBoxesCount> msgBoxes{};    // DS:59CC g_msgBoxes
    std::array<uint16_t, 3> boxFooterText{};               // DS:5256 g_boxFooterText -> DS offsets
    std::array<uint16_t, 4> helpTopicTitles{};             // DS:52EE g_helpTopicTitles -> DS offsets
    std::array<int8_t, 17> menuStepH{};                    // DS:5B40 g_menuStepH
    std::array<int8_t, 18> menuStepV{};                    // DS:5B51 g_menuStepV (menuWrapAt reads past it)
    std::array<uint16_t, 3> menuFirstDialog{};             // DS:5B69 g_menuFirstDialog
    std::array<int16_t, 14> dialogBox{};                   // DS:5BA7 g_dialogBox
    std::array<int8_t, 2> helpPageStep{};                  // DS:5BC3 g_helpPageStep
    std::array<uint16_t, 3> menuTitleX0{}, menuTitleX1{};  // DS:5BC7 / 5BCD
    std::array<uint16_t, 6> menuTitleGlyph1{}, menuTitleGlyph2{};  // DS:5BD3 / 5BDF
    std::array<uint8_t, 3> menuZoneCounts{};               // DS:5C83
    std::array<uint8_t, 3> buttonZoneWidths{};             // DS:5C86
    std::array<uint8_t, 8> footerZoneX{};                  // DS:5C89
    // Quiz.
    std::array<std::array<uint8_t, 3>, 6> answerPerms{};   // DS:5E9C g_answerPerms
    std::array<uint8_t, 4> historyMasks{};                 // DS:5EAE
    std::array<uint16_t, dg::kClueWordTextCount> clueWordText{};  // DS:5EB2 g_clueWordText -> DS offsets
    std::array<int16_t, 2> quizFocusStep{};                // DS:5F86
    std::array<uint8_t, 3> answerBit{}, answerBit2{}, answerBit3{};  // DS:5F8A / 5F8D / 5F90
    std::array<uint8_t, 8> quizReward{};                   // DS:5F93
    std::array<uint8_t, 9> dealTemplate{};                 // DS:5FE0
    std::array<uint8_t, 3> dealFallback{};                 // DS:5FE9
    std::array<uint8_t, 4> dealFallbackIndex{};            // DS:5FEC
    std::array<uint8_t, 4> historyResetMasks{};            // DS:5FF0
    // Sign-in.
    std::array<uint16_t, 9> userRecOffsets{};              // DS:6132 (3 + 360 i)
    std::array<uint16_t, 7> signinStateKeys{};             // DS:6186
    std::array<uint8_t, 8> signinZone0Template{};          // DS:622D
    std::array<uint8_t, 32> signinZone8Template{};         // DS:6235
    std::array<uint16_t, 2> signinHeadings{};              // DS:6255 -> DS offsets
    std::array<uint16_t, 7> signinRankThresholds{};        // DS:629D
    std::array<uint8_t, 6> recPicDataRestore{};            // DS:62AB
    std::array<uint8_t, 6> recPicDataPack{};               // DS:62B1
    // Title.
    std::array<int16_t, 12> titleSparkleX{};               // DS:8A18
    std::array<int16_t, 12> titleSparkleY{};               // DS:8A30

    void load(const DosExe& exe) {
        for (size_t n = 0; n < msgBoxes.size(); ++n) {
            const uint16_t r = static_cast<uint16_t>(dg::kMsgBoxes + dg::kMsgBoxesStride * n);
            MsgBox& b = msgBoxes[n];
            b.type = exe.ds8(static_cast<uint16_t>(r + dg::boxrec::kType));
            b.x1 = exe.dsS16(static_cast<uint16_t>(r + dg::boxrec::kX1));
            b.y1 = exe.dsS16(static_cast<uint16_t>(r + dg::boxrec::kY1));
            b.defaultButton = exe.ds8(static_cast<uint16_t>(r + dg::boxrec::kDefaultButton));
            b.title = exe.readFarPtr(static_cast<uint16_t>(r + dg::boxrec::kTitle)).off;
            b.text = exe.readFarPtr(static_cast<uint16_t>(r + dg::boxrec::kText)).off;
        }
        detail::readDsFarOffsets(exe, dg::kBoxFooterText, boxFooterText);
        detail::readDsFarOffsets(exe, dg::kHelpTopicTitles, helpTopicTitles);
        detail::readDsArray(exe, dg::kMenuStepH, menuStepH);
        detail::readDsArray(exe, dg::kMenuStepV, menuStepV);
        detail::readDsArray(exe, dg::kMenuFirstDialog, menuFirstDialog);
        detail::readDsArray(exe, dg::kDialogBox, dialogBox);
        detail::readDsArray(exe, dg::kHelpPageStep, helpPageStep);
        detail::readDsArray(exe, dg::kMenuTitleX0, menuTitleX0);
        detail::readDsArray(exe, dg::kMenuTitleX1, menuTitleX1);
        detail::readDsArray(exe, dg::kMenuTitleGlyph1, menuTitleGlyph1);
        detail::readDsArray(exe, dg::kMenuTitleGlyph2, menuTitleGlyph2);
        detail::readDsArray(exe, dg::kMenuZoneCounts, menuZoneCounts);
        detail::readDsArray(exe, dg::kButtonZoneWidths, buttonZoneWidths);
        detail::readDsArray(exe, dg::kFooterZoneX, footerZoneX);
        detail::readDsArray(exe, dg::kAnswerPerms, answerPerms);
        detail::readDsArray(exe, dg::kHistoryMasks, historyMasks);
        detail::readDsFarOffsets(exe, dg::kClueWordText, clueWordText);
        detail::readDsArray(exe, dg::kQuizFocusStep, quizFocusStep);
        detail::readDsArray(exe, dg::kAnswerBit, answerBit);
        detail::readDsArray(exe, dg::kAnswerBit2, answerBit2);
        detail::readDsArray(exe, dg::kAnswerBit3, answerBit3);
        detail::readDsArray(exe, dg::kQuizReward, quizReward);
        detail::readDsArray(exe, dg::kDealTemplate, dealTemplate);
        detail::readDsArray(exe, dg::kDealFallback, dealFallback);
        detail::readDsArray(exe, dg::kDealFallbackIndex, dealFallbackIndex);
        detail::readDsArray(exe, dg::kHistoryResetMasks, historyResetMasks);
        detail::readDsArray(exe, dg::kUserRecOffsets, userRecOffsets);
        detail::readDsArray(exe, dg::kSigninStateKeys, signinStateKeys);
        detail::readDsArray(exe, dg::kSigninZone0Template, signinZone0Template);
        detail::readDsArray(exe, dg::kSigninZone8Template, signinZone8Template);
        detail::readDsFarOffsets(exe, dg::kSigninHeadings, signinHeadings);
        detail::readDsArray(exe, dg::kSigninRankThresholds, signinRankThresholds);
        detail::readDsArray(exe, dg::kRecPicDataRestore, recPicDataRestore);
        detail::readDsArray(exe, dg::kRecPicDataPack, recPicDataPack);
        detail::readDsArray(exe, dg::kTitleSparkleX, titleSparkleX);
        detail::readDsArray(exe, dg::kTitleSparkleY, titleSparkleY);
    }
};

// Core: nothing beyond the wave-2 objects yet (input tables: InputTables::load).
struct CoreTables {
    void load(const DosExe&) {}
};

struct GameTables {
    WorldTables world;
    PlayerTables player;
    ElfTables elves;
    UiTables ui;
    CoreTables core;
    // DS:0000..8F9F of the EXE, for the quirk helpers that read past a table (menuWrapAt).
    std::span<const uint8_t> dgroup;

    static GameTables load(const DosExe& exe);
};

// --------------------------------------------------------------- DGROUP image and field registry

// DS:0000..CC1F of a freshly loaded program: the EXE's initialised data (DS:0000..8F9F), BSS zero.
std::vector<uint8_t> initialDgroupImage(const DosExe& exe);

// Every GameState field (and, for dumpToDgroup(const DosGame&), the service-held globals) at its
// DGROUP address, for tools and the wave-4 comparison with DOSBox-X states (savmem.py).
struct DgroupField {
    std::string_view name;      // canonical name (dgroup.tsv), or DS_xxxx_<role> for unnamed globals
    uint16_t ds = 0;            // DGROUP offset
    uint16_t size = 0;          // bytes
    bool farPointer = false;    // holds far pointers, whose segment words differ between the engine and
                                // DOSBox-X: diffDgroup compares only the other bytes
    int16_t segWordOffset = -1; // offset of the skipped bytes (the segment word) in each element (-1 none)
    uint16_t stride = 0;        // element size for segWordOffset
    uint8_t skipLen = 2;        // bytes skipped at segWordOffset (4 for the draw records' save pointers)
    bool service = false;       // held by a wave-2 object (written only by dumpToDgroup(const DosGame&))
};
// The GameState fields in visit order (sizes from their types).
const std::vector<DgroupField>& gameStateFields();

// Writes the state into a copy of `base` (DS:0000..CC1F; bytes no field covers are kept) in the
// original's layout: little-endian words, far pointers {off, 0x3D94}, records byte for byte, and
// the record far pointers derived from UiState::userRec.
std::vector<uint8_t> dumpToDgroup(const GameState& s, std::span<const uint8_t> base);
// The inverse for every field (a DOSBox-X DGROUP dump, or initialDgroupImage() at start-up).
// UiState::userRec is derived from the far pointer at DS:9228.
void loadFromDgroup(GameState& s, std::span<const uint8_t> dgroup);

// The names of the fields whose bytes differ between two DGROUP images (far pointers compared by
// their offset words only, as DOSBox-X relocates segments). `fields` defaults to gameStateFields().
std::vector<std::string_view> diffDgroup(std::span<const uint8_t> a, std::span<const uint8_t> b,
                                         const std::vector<DgroupField>* fields = nullptr);

// ------------------------------------------------------------------------- quirk helpers
// Memory-layout behaviour the specs rely on (re/dos/docs/ENGINE_MAP.md §5 lists every quirk).

// The cell index of Cell_GetMask 1568:1fe4 / Cells_UpdateVisible / Frame_Begin (code_world.md §3.2):
// colpart(v) = ((v >> 7) & 0xFF) | ((v >> 15) << 8); index = colpart(x) + (colpart(y) << 4). Not
// masked: x = -1 gives column 0x1FF; for 0 <= x, y < 0x800 it is (y >> 7) * 16 + (x >> 7).
constexpr unsigned cellIndexOf(uint16_t x, uint16_t y) {
    const auto colpart = [](uint16_t v) { return static_cast<unsigned>(((v >> 7) & 0xFF) | ((v >> 15) << 8)); };
    return (colpart(x) + (colpart(y) << 4)) & 0xFFFF;
}
// g_cellType[idx] as the original reads it: 0..255 the table, 256..767 the bytes of g_cellContent
// (DS:0FAA follows DS:0EAA), beyond that 0 (never reached in play).
int8_t cellTypeRaw(const GameState& s, unsigned idx);
// g_cellPicture[idx] for idx = -1 (the word at DS:0CA8, last word of g_playerStateHandlers),
// 0..255, and 256..383 (words of g_cellType); Search_CanSearch reads -1 / 256 at the strip ends.
uint16_t cellPictureRaw(const GameState& s, const GameTables& t, int idx);
// g_hidingCells[slot] for slot -1..47: slot -1 is the word at DS:11A8 = g_cellContent[255]
// (Treasure_PickShowcase 4a03:038c, code_player.md §5.2).
uint16_t hidingCellAt(const GameState& s, const GameTables& t, int slot);
// g_menuWrap[i] for i = -2..5 (code_ui.md §5.2): elements -2 and -1 are the last two bytes of
// g_menuStepV.
int8_t menuWrapAt(const GameTables& t, int i);
// Byte i of a record's 90-byte question history; i == 90 is one past it, the low byte of question
// slot 0 (record +0x66), which Quiz_ResetHistory 46ce:0d60 writes (code_ui.md §10.2).
uint8_t historyByte(const UserRecord& r, int i);
void setHistoryByte(UserRecord& r, int i, uint8_t v);
// The object g_curObj points at (kPlayerObjOff or elfObjOff(i)); throws std::logic_error otherwise.
Obj& objAt(GameState& s, DsPtr p);
const Obj& objAt(const GameState& s, DsPtr p);
// The elf index of an object pointer, -1 for the player.
int elfIndexOf(DsPtr p);
// Word i of animation script `anim` (writable copy, so the DS:48DE patch shows): the script's
// DS offset from PlayerTables::animScriptOffset, then PlayerState::animScriptData.
int16_t animScriptWord(const GameState& s, const GameTables& t, int anim, int i);
// The writable script word at DS offset `ds` (DS:3D36..4F59), e.g. g_animScript88Frame DS:48DE.
int16_t& animScriptWordAt(GameState& s, uint16_t ds);
// Byte at DS offset `ds` of the hint/message texts (DS:6BC0..7CA2), writable (message patches).
uint8_t& messageTextByte(GameState& s, uint16_t ds);

}  // namespace tmport::dos
