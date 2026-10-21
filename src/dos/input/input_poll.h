#pragma once
// Segment 4a85 (input.c) of SST.EXE: commands from the BIOS key buffer and from mouse zones
// (re/dos/docs/code_runtime.md §6, code_ui.md §2.3).
//
// g_inputContext (DS:922C) selects a zone table (far pointers DS:80DB[ctx]), a key list (DS:80F3[ctx])
// and a command list (DS:810B[ctx]); all three are read from the user's SST.EXE. Contexts: 0 play,
// 1 menu, 2 dialog, 3 quiz, 4 name entry / sign-in, 5 a two-zone screen. The menu, dialog, sign-in
// and title code fill zone records at run time (HotZones_Setup 43cb:12CF, SignIn_SetupZones
// 47c7:0831, Title_Run writes DS:80DA), so the zone tables are a mutable copy.
//
// Input_Poll runs once per game tick from Input_Tick (both paths of Game_Tick): at most one BIOS key
// per tick, and the mouse zones only in ticks without a key.

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "dos/input/keyboard.h"
#include "dos/input/mouse.h"
#include "mischief/dos/input/bios_keyboard.h"

namespace tmport::dos {

class DosExe;

inline constexpr int kInputContexts = 6;

// One 8-byte zone record: rectangle (x in bytes 0..159, y in rows, inclusive) and the command for
// hover, press, held and release (-1 none).
struct InputZone {
    uint8_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    int8_t hover = -1, press = -1, held = -1, release = -1;
    bool operator==(const InputZone&) const = default;
};

// A zone table: count byte, then records. records.size() is the room the original reserves.
struct InputZoneTable {
    uint8_t count = 0;
    std::vector<InputZone> records;
};

// A key list (u16 count n, then n BIOS key words) with its command list (n + 1 s16, entry 0 = -1).
struct InputKeyMap {
    std::vector<uint16_t> keys;      // keys[0] unused (the count's slot), keys[1..n]
    std::vector<int16_t> commands;   // commands[0] = -1 "no command"
};

class InputTables {
public:
    // Empty tables (no keys, no zones, each zone table with its room); tests fill them by hand.
    InputTables();
    // Reads the tables of every context through the far pointers at DS:80DB / 80F3 / 810B.
    // Throws mischief::FormatError when a pointer leaves DGROUP or a table does not fit.
    static InputTables load(const DosExe& exe);

    InputZoneTable& zones(int ctx) { return zones_.at(static_cast<size_t>(ctx)); }
    const InputZoneTable& zones(int ctx) const { return zones_.at(static_cast<size_t>(ctx)); }
    const InputKeyMap& keyMap(int ctx) const { return keys_.at(static_cast<size_t>(ctx)); }
    void setKeyMap(int ctx, InputKeyMap map) { keys_.at(static_cast<size_t>(ctx)) = std::move(map); }
    // DGROUP offset of a context's zone table / key list (for diagnostics).
    uint16_t zonesAddress(int ctx) const { return zoneAddr_.at(static_cast<size_t>(ctx)); }
    uint16_t keysAddress(int ctx) const { return keyAddr_.at(static_cast<size_t>(ctx)); }

    // Room in records of each zone table (up to the next table in DGROUP): 11, 17, 11, 12, 19, 2.
    static constexpr std::array<int, kInputContexts> kZoneCapacity = {11, 17, 11, 12, 19, 2};

private:
    std::array<InputZoneTable, kInputContexts> zones_;
    std::array<InputKeyMap, kInputContexts> keys_;
    std::array<uint16_t, kInputContexts> zoneAddr_{};
    std::array<uint16_t, kInputContexts> keyAddr_{};
};

// 4a85:0107 Input_LookupKey: index 1..n of the key (searched from the last entry down), 0 if absent.
int inputLookupKey(uint16_t key, const InputKeyMap& map);
// 4a85:0130 Input_HitZone: the command of the last record (searched from count-1 down) whose
// rectangle holds the pointer, by button history: release (was down, now up), press (was up, now
// down), held (both down), hover (both up); -1 when no record matches.
int8_t inputHitZone(const InputZoneTable& zones, const MousePointer& mouse);

// Effects of Input_Tick on UI state the caller owns (g_uiMode is 0 whenever one is returned).
enum class InputTickAction : uint8_t {
    None,
    ExitDialog,  // Esc on a slow CPU: g_uiMode = 0x20, DS:9323 = 10 (the Exit-and-Save box)
    MenuEscape,  // Esc: g_uiMode = 0x40, DS:90FC = 1, DS:9111 = 2 (g_cmd = 8 is set here)
    MenuOpen,    // F1/F2/F3 or a click on a menu title: g_uiMode = 0x40, DS:90FC = -1, DS:9111 = 0
};

// Inputs of Input_MouseWalk (4a85:0208) owned by other modules.
struct MouseWalkParams {
    int16_t playerScreenX = 0;  // DS:8FA7, bytes (computed by Game_Tick)
    int16_t playerScreenY = 0;  // DS:8FA9, rows
    uint8_t uiMode = 0;         // g_uiMode DS:9119
    bool scriptedMove = false;  // DS:926C != 0
    uint16_t terrainMask = 0;   // DS:926A (bits 0-1: Up/Down possible here)
    bool mousePresent = true;   // g_mouse DS:912A == -1
};

class InputPoll {
public:
    explicit InputPoll(const InputTables& tables) : tables_(&tables) {}

    int16_t cmd = -1;          // g_cmd DS:9226 (consumed, i.e. set back to -1, by its users)
    int16_t queuedCmd = -1;    // DS:90F6
    bool deliverNext = true;   // DS:9238 (Game_InitVars: 1)
    uint16_t lastKey = 0;      // DS:9285
    int context = 0;           // g_inputContext DS:922C

    // 4a85:000E Input_Poll.
    void poll(mischief::dos::BiosKeyboard& bios, const MousePointer& mouse, uint8_t uiMode);
    // 4a85:03AC Input_Tick: Input_Poll, then Esc/F1-F3 in play.
    InputTickAction tick(mischief::dos::BiosKeyboard& bios, const MousePointer& mouse, uint8_t uiMode, bool scriptedMove, bool slowCpu);
    // 4a85:0208 Input_MouseWalk (per executed game tick, before Player_Update): Down -> coin where
    // Down cannot climb, arrow commands dropped in play, play-field press/hold -> walk, climb, jump
    // or net relative to the player, status-bar press -> coin, release -> stop. Writes g_keys and
    // DS:9257 in `kbd`; returns the new prompt DS:90F8 when it sets one.
    std::optional<int16_t> mouseWalk(KbdIsr& kbd, const MousePointer& mouse, const MouseWalkParams& p);

private:
    const InputTables* tables_;
};

}  // namespace tmport::dos
