# Gizmos & Gadgets (DOS v1.0): puzzles, part A

Scope: the puzzle framework (state 9: how a locked door starts a puzzle, the choice of kind and
variant with the per-player history, the dispatch, the common screen, panel, buttons, reward,
sounds and the door that opens) and four of the eight puzzles in full: **electric circuits**
(kind 0, ovl13 3083, `electric.c`), **energy forms** (kind 6, ovl15 33cd), **launch** (kind 5,
ovl17 3797, the only user of the DAM engine) and **simple machines** (kind 1, ovl20 3938,
`simple.c`). The other four puzzles (gears ovl22 3b2f, balance ovl14 32fe, magnets ovl16 35bb,
machine pieces ovl21 3a4a) are part B; §8 lists what both parts share. Wave 3 implements these
modules from this document alone.

Evidence: the named Ghidra export `re/gg/ghidra_export/` (assembly read for every switch and
every pascal call; the decompiled C of the overlays loses switch tables and swaps pascal
arguments), the bytes of `ssg.exe` (DGROUP tables, switch tables, code constants), the archives
through `tools/gg/ggarc.py`, and the reference frames `extracted/captures/gg/r2/puzzle/k0..k7`
(one screen per puzzle kind) and `w0/` (machine pieces). Scratch tools in
`extracted/scratch/gg/wave1/puzzles_a/`: `rd.py` (bytes at Ghidra or DS addresses, resource
dumps), `el.py` (electric variant decoder), `sm.py` (simple-machines variant decoder), `en.py`
(energy variant interpreter, §4.2), `geo.py` (electric terminal geometry read from the code,
§3.4), `la.py` (launch model and solvability check, §5.5).

Notation: **[O]** observed in code, bytes, data or captures; **[I]** inferred. `SSSS:OOOO` =
Ghidra address (overlay file offsets: `overlay_map.tsv`); `DS:XXXX` = DGROUP 1ea7. Names are
those of `re/names/gg/` (`game_puzzles.tsv` holds the functions of the five overlays, `dgroup.tsv`
the DGROUP variables and tables below; the wave 1 merge is recorded in `re/names/gg/README.md`);
the engine helpers the puzzles call carry the platform spec's names (`Zones_AddLocal`,
`MouseRegion_Add`, `Input_StripHeld`, `Gfx_SaveToBack`, …) and the world spec's
(`Hud_AddScoreNow`). Ticks are PIT ticks
(145.652 Hz). Game text is cited by resource id only. "Effect n" = member n of the puzzle's
effect bank (§2.4).

Conventions used in the descriptions:

* **Zone** = a 22-byte click-zone record `{i16 shape (0 rect, else circle of radius +6), x, y,
  w, h, priority, far next (+0x0c), press code (+0x10), release code (+0x12), tag (+0x14)}`
  (17ab:0507). `Zones_AddLocal` 17ab:0294 pushes a zone on the local list (pascal call),
  `Zones_RemoveLocal` 17ab:0314 unlinks it, `Zones_ClearLocal` 17ab:0465 empties the list.
* **Cursor region** = a 0x1c-byte record whose zone sits at +8 and whose cursor at +0x18;
  `MouseRegion_Add` 17ab:0754 puts one at the head of the list DS:0a8c, `MouseRegion_Clear`
  17ab:07a4 removes all but the bottom one (DS:0a54, the whole screen, whose cursor field is the
  current cursor DS:0a6c set by `Mouse_SetCursorPicture` 17ab:01f0). `Mouse_DrawCursor`
  (17ab:0612) shows the cursor of the first region that contains the mouse [O]. The cursor
  machinery belongs to the platform spec; the puzzles only push and pop regions.
* **Cursor record** (`Mouse_LoadCursors(id, 0)` from a resource, `(far picture)` from memory,
  17ab:0083): +6/+8 width and height of the picture, +0x0a/+0x0c the **click-point offset**
  (0, 0 unless set), +0x0e far picture. The picture is drawn with its top-left corner at the
  mouse (REFERENCE_CAPTURES §5). The mouse handler adds the current cursor's offset to every
  latched press and release position (17a0:0037..0040), so zone presses are tested at
  mouse + offset [O]. The puzzles add the same offset by hand where they test points
  (DS:7a74/7a76 = offset of the current cursor copy DS:7a6a; DS:7a88/7a86 = mouse x, y).
* **Button held** = DS:0a0c ≠ 0: the button bits of the last mouse event (17a0:0011), so the
  right button also counts [O].
* `Input_StripHeld(Input_GetCode())` (198d:0328): every puzzle reads input this way. A held
  repeat (bit 15) becomes 0, the first poll of a press loses bit 14, a release gives the
  zone's release code, a key gives its BIOS word [O]. Only press edges, releases and keys reach
  the puzzles.
* `Lpx_DrawKeyed(x, y, picture, mode)` 1399:02ee and `Lpx_Draw(x, y, picture)` 1399:0335 are
  **pascal** calls (the callee pops: `RETF 0xa` at 1399:0331, `RETF 8` at 1399:036a): the
  decompiler prints their arguments reversed [O].
* `Gfx_RestoreRect(x, y, w, h)` 1399:0233 copies back-page rows y+200.. to the screen.
  **`Gfx_SaveToBack(x, y, w, h)` 1399:02a2 (wave 0 name `Gfx_CopyFromBack`) copies the other
  way, screen rows y.. to back-page rows y+200..**, without marking tiles dirty (destination row
  + 0xc8 at 1399:02af) [O]. Every puzzle composes a picture on the screen and saves it with it.

---

## 1. From a locked door to the puzzle

### 1.1 The door [O]

* A room object of type **7** is a locked puzzle door, type **9** an open door; both carry a link
  word at +0x12 (`& 0xF` = target room) (ARCHITECTURE §2.6).
* When the player uses a door, the player actor's door callback **18f5:04cf** (stored at
  DS:7b12 by 18f5:0781) looks up the object DS:7ac0 of the current room (1622:11f7) and branches
  on its type (switch CS:0597, types 6..11). Type 7 (18f5:056e): DS:458c = 1 (cancels the
  delayed New Player Hint 0x1048), the next room = DS:08c2 (the room stays the same),
  DS:7aaa = DS:7ab6 = 0x21 (as for an open door; their meaning belongs to the world spec), and
  **DS:00bc (`g_puzzleDoorHit`) = 1**. The callback clears DS:00bc before it branches
  (18f5:0503). The actor tick then leaves the room (state 8).
* **State 8** `State_08_LeaveRoom` 12e0:09aa: menu bar off; when DS:00bc is set: font colour 0,
  DS:00bc = 0, 1cdd:03ef(DS:09e6) (the room music), `Pal_FadeOut`, 2a80:05e6 (chimps), 18f5:06b7
  (player), next state **9**.
* DS:7ac0 (the door's object index) and the room index DS:7aa4 are kept through the puzzle;
  `Room_OpenPuzzleDoor` uses them afterwards (§1.5).

### 1.2 State 9: `State_09_Puzzle` 38e2:0000 [O]

```
Pal_Save()                              1473:074e: shadow palette DS:024e -> DS:5c22
Zones_Push()
Menu_SetMenuBar(&DS:4053)               the File / Options / Help bar stays usable
DS:3332 = 0                             g_puzzleLeave
choose kind and variant (§1.3)          DS:b13a kind, DS:b138 variant
Puzzle_Dispatch()                       38e2:00d4 (§1.4): runs the puzzle, stores the result
Room_OpenPuzzleDoor(DS:b13c)            12e0:0a24 (§1.5)
Pal_FadeOut()                           no effect when already faded (1473:0265)
Zones_Pop(); Menu_SetMenuBar(NULL)
Pal_Restore()                           DS:5c22 -> shadow (no DAC write)
next state = 5 (enter the room); 16 (save and leave) when DS:3332 has bit 1 (DS:455a = 3, hub)
             or bit 0 (DS:455a = 2, sign-in; bit 0 is tested last and wins)
```

### 1.3 Choice of kind and variant, and the per-player history [O]

The history is the player record's solved flags **DS:45d0 + kind·0x42 + variant·2** (u16, 8
kinds × 33 slots; PLAYERS.DAT record +0x06E, `fmt_data.md` §8.3). Variants per kind:
`g_puzzleVariants` DS:3334 = 30, 20, 20, 31, 32, 30, 28, 30 (221).

```
loop:
    kind = random(8)                                     38e2:01e8: rand()·8 / 0x8000  (rand #1)
    Puzzle_PickUnsolved(kind):                           38e2:0202
        cand[0..2] (DS:b13e) = -1
        n = variants[kind]
        if Puzzle_FewUnsolvedLeft():                     38e2:019e: unsolved over all kinds <= 4
            cand[0] = random(n)                          38e2:022d                     (rand #2)
        else:
            cand = the FIRST up to 3 unsolved variants of the kind, in index order
    if cand[0] == -1: continue                           the kind has nothing unsolved: new kind
    k = 3 - (cand[2] == -1) - (cand[1] == -1)
    idx = random(k)                                      38e2:0052                     (rand #3)
    break
variant = cand[idx]
```

* `random(n)` here is always `rand()·n / 0x8000` computed in 32 bits (F_LXMUL, F_LDIV; for the
  kind `rand() << 3` then the division), i.e. `(rand() * n) >> 15` [O].
* **Order of rand() calls**: one per tried kind; when few are left one more for the variant;
  then one for the candidate index, which is made even when k = 1 (result 0). A kind without an
  unsolved variant costs one call and is retried. During a part-A puzzle the only other call is
  the success jingle (§2.6) [O: the only calls of `rand` 1000:06c1 in ovl13, 15, 17, 18, 19 and
  20 are at 38e2:0052, 01ed, 022d and 02a3]. Of part B, machine pieces calls rand() itself
  (3a4a:0780).
* "Unsolved first" therefore means: the three lowest-numbered unsolved variants of a random
  kind, one of them at random. Only when at most 4 variants are left in the whole game does any
  variant (solved or not) of the random kind come up [O].
* `Puzzle_FewUnsolvedLeft` 38e2:019e (wave 0 name `Puzzle_PickAny`) only tests; it picks
  nothing.

### 1.4 Dispatch: `Puzzle_Dispatch` 38e2:00d4 [O]

```
g_drawColor = 0; Gfx_HLine(0, 9, 320)        a black line under the menu bar
DS:b13c = 0
switch kind (jump table CS:018e, 8 entries):  DS:b13c = Pz…_Run(variant)   (far, cdecl)
if DS:45d0[kind][variant] == 0: DS:45d0[kind][variant] = DS:b13c
```

| Kind | Entry | Overlay | Puzzle | Variants | Screen picture / palette | Effect bank |
|---|---|---|---|---|---|---|
| 0 | 3083:08a1 `PzElectric_Run` | ovl13 | electric circuits (§3) | 30 | 0x20b2 / 0x20b3 | 0x11cc |
| 1 | 3938:0000 `PzSimple_Run` | ovl20 | simple machines (§6) | 20 | 0x204a / 0x204b | 0x11ca |
| 2 | 3a4a:0000 `PzPieces_Run` | ovl21 | machine pieces (part B) | 20 | 0x2084 / 0x2085 | 0x11ca |
| 3 | 3b2f:1ed0 `PzGears_Run` | ovl22 | gears and buttons (part B) | 31 | 0x2021 / 0x2022 | 0x11c8 |
| 4 | 32fe:0aa0 `PzBalance_Run` | ovl14 | balance (part B) | 32 | 0x2119 / 0x211a | 0x11c6 |
| 5 | 3797:1138 `PzLaunch_Run` | ovl17 | launch (§5) | 30 | 0x1133 / 0x1134 | 0x11ce |
| 6 | 33cd:0000 `PzEnergy_Run` | ovl15 | energy forms (§4) | 28 | 0x1093 / 0x1094 | 0x11d2 |
| 7 | 35bb:1c03 `PzMagnets_Run` | ovl16 | magnets (part B) | 30 | 0x119f / 0x11a0 | 0x11d0 |

**Kinds 3, 4 and 7 are confirmed [O]**: the jump table targets above (bytes at CS:018e and the
code at 38e2:0106..0160), the screen picture each overlay draws (code pairs of
`fmt_graphics.md` §4), and the reference frames `r2/puzzle/k3_01_screen.png` (gear panel,
crank, red buttons), `k4_01_screen.png` (a scale) and `k7_01_screen.png` (magnets, ROTATE
button), which show exactly those pictures (ARCHITECTURE §1.4 / §2.8 and `game_puzzles.tsv` now carry
them as observed; `code_puzzles_b.md` §1.1 confirms them from the code and the save states).

The stored value is the puzzle's return value: 1 when it was solved during this visit, else
0. So the flag is still 0 while the solved screen shows and is written when the puzzle is left
(seen in the round 2 states). The variant-0 solutions of every kind, in game coordinates, are in
REFERENCE_CAPTURES §9.1. A flag that is already set is never changed (a replayed solved variant cannot be "unsolved")
[O 38e2:0162..018b].

### 1.5 After the puzzle: the door opens [O]

`Room_OpenPuzzleDoor(solved)` 12e0:0a24 (`code_world.md` §6.6): nothing when solved = 0. Otherwise it fetches object
DS:7ac0 of room DS:7aa4 (1622:11f7); if it is still type 7:

* picture +0x0e = 0x33c if the room's flag (1622:090d(room)) is set, else 0x30e; type +0x10 = 9;
  +0x16 = 4 (the hit kind of an open door, `code_world.md` §5.3);
* target room = +0x12 & 0xF; **if `g_puzzleGoThrough` DS:b144 ≠ 0 the current room becomes the
  target room** (DS:7aa4 = target);
* the door on the other side (1622:087f(target, link word)) gets the picture by the target
  room's flag, type 9 and +0x16 = 4.

DS:b144 is written only by the puzzles: 1 by Enter or a click on the panel after solving, 0 by
Go Back. Leaving through the menu or Save & Exit leaves it as the previous puzzle left it (BSS
0 at start-up), so a puzzle solved and then left through the menu may move the player through
the door if the previous puzzle ended with "go through" [O; consequence I]. Keep it.

### 1.6 Leaving a puzzle (every puzzle of part A) [O]

| Input | Effect |
|---|---|
| Enter (0x0d) or the panel (release code 0x5de, zone DS:331c, only pushed once solved) | only when solved: DS:b144 = 1, leave |
| Go Back (press 0x5dd, zone DS:3306) | `Puzzle_PressGoBack`, DS:b144 = 0, leave (solved or not). Electric: only when idle (§3.5) |
| Esc (0x1b) | `Cmd_SaveAndExit` 4106:00b2: Yes/No box 0x1064; Yes saves and `exit(0)`; No returns into the puzzle |
| menu code 0x17a2 (Return to Technology Center) | DS:3332 = 2, leave (state 16, hub) |
| menu code 0x17a3 (Start New Player) | DS:3332 = 1, leave (state 16, sign-in) |

There is no failure: an unsolved puzzle stays locked, and the next use of the door chooses a
new kind and variant (§1.3).

---

## 2. Common screen, panel and helpers

### 2.1 Screen layout [O]

| Rows | Content |
|---|---|
| 0..8 | the menu bar (File, Options, Help), installed by state 9 |
| 9 | a black line (`Puzzle_Dispatch`) |
| 10.. | the puzzle picture, drawn with `Lpx_Draw(0, 10, picture)` after `Screen_LoadPicture(picture, palette)` 1473:0399 (each puzzle has a full 0..255 palette, `fmt_graphics.md` §3.1) |
| 165..199 | the **panel** 0x1091 (320×35) at (0, 165): message area and Go Back button; the score is drawn into it at (253, 188) |

### 2.2 Panel and buttons (ovl18) [O]

* `Puzzle_InitPanel(text)` 38e2:0357: loads 0x1091 (panel, DS:b134) and 0x1092 (pressed Go Back,
  65×19, DS:b130), `Zones_AddLocal(DS:3306)` (Go Back: rect (249,168) 65×19, press 0x5dd),
  `Font_Select(1)`, `Font_SetColor(0)`, draws the panel at (0,165) and
  `Text_DrawBox(4, 170, 208, text)` (2b89:0000). No flush (the caller fades in later).
* `Puzzle_PressGoBack()` 38e2:031a: 0x1092 at (249,168), flush, wait while a button is held,
  `Gfx_RestoreRect(249,168,65,19)`, flush.
* `Puzzle_ShowSolved(jingle)` 38e2:0299: `PzSnd_Play(jingle + random(4), 4)` (**rand()**,
  38e2:02a3: rand()·4 / 0x8000), `Zones_AddLocal(DS:331c)` (panel: rect (2,168) 212×32, priority
  100, release 0x5de), `Res_Read(0x2000)` (the solved message), panel redrawn at (0,165), font 1,
  colour 0, `Text_DrawBox(4,170,208, message)`, flush.
* `Puzzle_AwardPoints(sound)` 38e2:03bf: four times: `PzSnd_Play(sound, 5)`,
  `Hud_AddScoreNow(100)` (1a04:0d36: `Score_Add(100,1)`, then the score drawn by 1a04:0058 at
  (253,188) with 6 digits (arguments 6, 2), flush; it lands in the SCORE box of the panel),
  `PzSnd_WaitEnd(sound)`. **+400 points per solved puzzle**, matching the
  capture (score 0 → 400, REFERENCE_CAPTURES §4).
* `Puzzle_ShowHintText(text)` 38e2:0286 = `TextView_Run(text)` 4140:0667 (the scrolling text
  viewer of the UI library; its own spec covers it: it saves the area (0,28) 129×138, draws the
  scroll frame 0x11d4/0x11d5, disables the menu hook while open and returns when closed).
* `Puzzle_FreePanel()` 38e2:03ed: font 0, frees 0x1091 / 0x1092.

Each puzzle has its own **Hint** button (top left; the captures show "HINT") and some a
**check** button; both are drawn pressed while held:

| Puzzle | Hint zone → code | pressed picture at | restore rect | sound (prio 5) | check button |
|---|---|---|---|---|---|
| electric | DS:24d0 (26,13) 35×15 → 0x458 | 0x1095 at (26,13) | (26,13,35,15) | effect 0xb | — |
| simple machines | DS:33ec (11,13) 35×15 → 0x458 | 0x1095 at (11,13) | (11,13, w, h of 0x1095) | effect 0x12 | DS:33c0 (10,150) 36×13 → 0x4b0, 0x1096 at (10,150), effect 0x12 |
| energy | DS:289c (11,14) 38×17 → 0x44f | 0x1095 at (11,14) | (11,14,38,17) | effect 0x26 | DS:28b2 (12,131) 37×15 → 0x44e, 0x1096 at (12,131), effect 0x2b, restore (12,131,37,15) |
| launch | DS:2b02 (12,13) 36×15 → 0x44d | 0x1095 at (12,13) | (12,13,36,15) | effect 0xa | (the plunger, §5) |

Electric restores only 35 of the 36 columns of 0x1095, so its rightmost column stays drawn until
the next redraw of that spot [O; keep].

### 2.3 Common run skeleton [O]

Every part-A puzzle follows the same shape (details per puzzle below):

```
PzSnd_LoadBank(bank)                     (electric: after the screen is shown)
load resources and the variant
draw the screen, Puzzle_InitPanel(panel text), Gfx_SaveToBack(0,0,320,200)
Screen_FadeOutIn()                       1473:0325: the room was faded out by state 8
add zones and cursor regions
Hint_Show(0x1052)                        the puzzle New Player Hint: once per player (3060:013b)
loop until leave:
    code = Input_StripHeld(Input_GetCode())
    dispatch(code)
Pal_FadeOut(); free everything; PzSnd_FreeBank(); return solved
```

* **No frame pacing.** None of the four main loops calls `Timer_Set(0,4)` / `Timer_Wait`: they
  poll `Input_GetCode` as fast as the machine runs and draw only when something changes
  (explicit `Dirty_Flush`). The cursor moves at interrupt time. Animations use timer slots 6 and 7
  (§7) [O]. A port pumps the host in this poll like in every other wait.
* Codes that a loop does not know are ignored; the global keys of `Input_GetCode` (Ctrl+S, the
  menu keys) still act inside it (ARCHITECTURE §4.6).

### 2.4 Puzzle sound helpers (ovl19 3924) [O]

| Function | Behaviour |
|---|---|
| `PzSnd_LoadBank(id)` 3924:0000 | `Res_LoadCopy(id + DS:a0d8)` (speaker bank id, FM bank id + 1, `fmt_sound.md` §2) into DS:b1b0; for each member i (count = first word / 2 − 1): `Snd_RegisterSong(member payload)` → slot DS:b146[i]; count → DS:338c. A speaker `C FF` member registers as an error code and stays silent |
| `PzSnd_Play(i, prio)` 3924:00ae | 1bb4:10c1(slot, section 0, prio, loop 0): starts the effect, or restarts it from its section start when it already plays |
| `PzSnd_PlayLoop(i, prio)` 3924:00cb | the same with the loop flag (s+0x0d) set |
| `PzSnd_Stop(i)` 3924:00e8 | 1bb4:084a |
| `PzSnd_StopAll()` 3924:00fc | every member |
| `PzSnd_WaitEnd(i)` 3924:005b | busy-waits while 1bb4:0c1e(slot) = 1 (the slot is active); returns at once for an invalid slot |
| `PzSnd_FreeBank()` 3924:0074 | releases the slots (1bb4:0d8f), frees the copy, count 0 |
| `PzSnd_SetTranspose(i, n)` 3924:0124 | 1bb4:0d26(slot, 0, n) (only the balance puzzle uses it) |

The priority decides which song owns a channel (the sound-driver spec covers 1bb4:10c1 and
1bb4:102c). Waits on effects (`PzSnd_WaitEnd`, `Puzzle_AwardPoints`) depend on the effect's
length in driver ticks (72.83 Hz) and are instant when the effect slot is invalid (speaker `C`
member) [O code, I timing].

### 2.5 Common resources [O]

| Id | Use |
|---|---|
| 0x1091 | panel picture (320×35) |
| 0x1092 | pressed Go Back (65×19) |
| 0x1095 | pressed Hint button (36×15) |
| 0x1096 | pressed check button (36×13): simple machines, energy |
| 0x2000 | the solved message (S string) |
| 0x204f | bundle of 10 digit pictures 0..9 (simple machines, energy, launch; also part B) |
| resource 3 | the default puzzle cursor (a 15×17 hand; `Mouse_LoadCursors(3,0)`; `lpx.py 0003`, the hand of REFERENCE_CAPTURES §4) |
| 0x1052 | the puzzle New Player Hint |

### 2.6 The order of a success [O]

| Puzzle | Code that detects it | Then |
|---|---|---|
| electric | 0x4b0+ (switch), 0x454 (wire action) | `PzSnd_WaitEnd(1)`, `Puzzle_ShowSolved(7)`, `Puzzle_AwardPoints(0xd)` |
| simple machines | 0x4b0 (check) | `Puzzle_ShowSolved(3)`, `Puzzle_AwardPoints(0x16)` |
| energy | 0x44e (check) | `Puzzle_ShowSolved(0x27)`, `Puzzle_AwardPoints(0x31)` |
| launch | 0x459 (plunger released) | glow off, `PzLaunch_Redraw`, `Puzzle_ShowSolved(0xb)`, `Puzzle_AwardPoints(0x10)` |

A puzzle rewards once per visit (its solved local). The jingle is effect `base + random(4)`:
**the only rand() call inside a puzzle of part A** [O].

---

## 3. Electric circuits (kind 0, ovl13 3083, `electric.c`)

The player builds a circuit on a board from the pieces of a toolbox (battery, light bulb,
switch, Y-wire, wire) until every bulb lights and the switches control the light
(`r2/puzzle/k0_01_screen.png`: a battery and a bulb joined by one wire, one wire in the
toolbox).

### 3.1 Resources (`PzElec_LoadResources` 3083:256b) [O]

| Id | Use | Kept in |
|---|---|---|
| 0x20b2 / 0x20b3 | screen and palette | — |
| 0x20b7 | bundle of 5 tool pictures: battery, bulb, switch (open), Y-wire, wire spool | DS:a2f8, far pointers DS:24ea + 16·i |
| 0x20b4 | puzzle cursor picture (10×18), click offset x = 1 | cursor DS:a8d8 |
| resource 4 | a cursor loaded and freed, never shown | DS:a8d0 |
| 0x20b5 | 10 digits (5×5) | DS:a2f4 |
| 0x20b6 | 5 label pictures (BATTERY …), cursors over the toolbox | DS:a2f0 |
| 0x20ba | the plug picture shown at a free terminal while stringing a wire (38×30) | DS:a2d8 |
| 0x20bc | switch closed (19×57) | DS:a2cc |
| 0x20bb | bulb lit (24×44) | DS:a2ec |
| 0x20b8 / 0x20b9 | terminal dot (6×7), upper and lower terminals | DS:a2e4 / DS:a2e0 |
| 0x1095 | pressed Hint | DS:a2e8 |
| 0x20dd + v | hint text (30 texts) | DS:a2d4 |
| 0x20fb + v | panel text (30 texts) | DS:a2d0 |
| F 0x20bf + v | variant data (§3.2) | read into the load buffer |
| effect bank 0x11cc | sounds (§3.10) | |

### 3.2 Variant data: F 0x20bf + v (30 entries, 280 bytes = 140 words) [O 3083:22bf, `el.py`]

| Words | Field |
|---|---|
| 0..4 | toolbox counts: battery, bulb, switch, Y-wire, wire → DS:a8c6[0..4] |
| 5..19 | piece type per slot 0..14 (0 battery, 1 bulb, 2 switch, 3 Y-wire); the list ends at the first 5 |
| 20..34 | piece x per slot |
| 35..49 | piece y per slot |
| 50..64 | terminal 0: linked piece (99 none) per slot |
| 65..79 | terminal 0: linked terminal |
| 80..94 | terminal 1: linked piece |
| 95..109 | terminal 1: linked terminal |
| 110..124 | terminal 2: linked piece |
| 125..139 | terminal 2: linked terminal |

Loading (`PzElec_LoadVariant` 3083:22bf):
1. clear the terminal table DS:a3ec (0x492 bytes) and the piece table DS:a2fc (0xf0 bytes),
   clear every slot's used flag;
2. `Res_Read(0x20bf + v)`; counts → DS:a8c6;
3. for each listed piece (type ≠ 5, at most 15): DS:a8be = type; `PzElec_CreatePiece(x, y)`
   (§3.6); mark it **fixed** (+0x0e = 1): the player can neither move nor remove it;
4. read the six link arrays for **all 15 slots** (this overwrites whatever the auto-connect of
   step 3 linked); a terminal's wire flag (+0x16) = 1 where its link ≠ 99;
5. make the links symmetric: for every slot i, terminal t linked to (c, u):
   T[c][u].piece = i, T[c][u].term = t (the flag of the other end is left as it is: 0).

So a wire given by the data is owned (drawn) by the side listed in the data. Shipped data: 0..7
fixed pieces, toolbox counts up to 3 batteries, 2 bulbs, 2 switches, 4 Y-wires and 14 wires
(variant 29). Variant 26 links terminal 2 of its switch (a switch has no terminal 2) to unused
slot 6; the link is never drawn (flag 0 on the switch side, slot 6 unused) and is a dead end for
the path search: harmless [O].

### 3.3 State [O]

| DS | Type | Meaning |
|---|---|---|
| a2fc + 16·i | rec[15] | piece: +0 type, +2 x, +4 y, +6 far current picture, +0x0a used, +0x0c switch closed (also cleared for bulbs), +0x0e fixed |
| a3ec + 0x4e·i + 0x1a·t | rec[15][3] | terminal t of piece i (§3.4) |
| a138 | i16[10][20] | path table: rows of piece numbers ending with the start piece; DS:a2c8 = row count |
| a87e | i16[15] | battery i is shorted (`PzElec_CheckBatteries`) |
| a89c | i16[15] | piece is on a closed path of the **last** bulb evaluated |
| a8be | i16 | mode: 99 idle, 0..3 a piece of that type in the hand, 4 a wire in the hand |
| a8c6 | i16[5] | toolbox counts |
| a8c0, a8c2 | i16 | terminal and piece found by `PzElec_FindFreeTerminal` (99 = none) |
| a8bc, a8ba | i16 | x, y of the anchor where the wire being strung starts |
| a8c4 | i16 | 1 while the plug picture replaces the held cursor |
| a8ce | i16 | +1 for every wire lost; never read |
| a8d4 / a8d8 | far | held cursor record / puzzle cursor |
| a8dc + 0x16·i | zone[5] | toolbox zones (press 0x45b + i) |
| a94a + 0x16·i | zone[15] | piece zones (press 0x4b0 + i): x, y, picture w, h |
| aa94 + 0x1c·i | region[5] | label cursor regions over the toolbox |
| 24e6 + 16·i | rec[5] | **tool table** (initialised data, read from the EXE): +0 x, +2 y of the item picture, +4 far picture (filled at run time from 0x20b7), +8 digit x, +0x0a digit y, +0x0c / +0x0e click offset of the held cursor (zero except for the wire spool) |

### 3.4 Terminals [O 3083:1f35, jump table CS:2166]

A terminal record (0x1a bytes):

| Off | Field |
|---|---|
| +0x00..+0x06 | hit rectangle x, y, w, h (relative to the piece) |
| +0x08, +0x0a | anchor x, y (wire end; relative) |
| +0x0c, +0x0e | dot picture offset x, y (relative); the lower dot 0x20b9 is used when +0x0e > 5 |
| +0x10 | overlap height (signed) for the auto-connect strip |
| +0x12 | linked piece (99 none) |
| +0x14 | linked terminal |
| +0x16 | wire flag: 1 = this end owns a drawn wire, 3 = contact made by placing, 0 = the passive end |

`PzElec_SetTerminalGeometry(piece, type)` first sets the three links to 99, then fills the
constants below (immediates in the code; §9 says how the engine obtains them; `geo.py` reads
them back from the EXE):

| Type | Terminal | hit (x, y, w, h) | anchor | dot | overlap h |
|---|---|---|---|---|---|
| 0 battery | 0 | 9, 2, 8, 10 | 14, 0 | 10, −1 | 6 |
| | 1 | 9, 53, 8, 10 | 14, 60 | 10, 54 | −6 |
| 1 bulb | 0 | 0, 36, 8, 11 | 5, 44 | 1, 37 | −6 |
| | 1 | 7, 33, 8, 9 | 12, 39 | 8, 33 | −6 |
| 2 switch | 0 | 5, 2, 9, 12 | 11, 0 | 7, −2 | 6 |
| | 1 | 5, 48, 10, 12 | 11, 57 | 7, 51 | −6 |
| 3 Y-wire | 0 | −2, 1, 8, 8 | 2, 0 | −1, −1 | 6 |
| | 1 | 6, 6, 8, 8 | 11, 5 | 7, 4 | 6 |
| | 2 | 1, 26, 8, 8 | 6, 31 | 2, 25 | −6 |

Terminal 2 of types 0..2 keeps zeros (never hit). A type above 3 fails an assert.

### 3.5 Input [O, switch CS:0add for 14 codes; 0x4b0..0x4bf tested first]

Zones: board DS:24ba (91,17) 222×135 → 0x454; Hint DS:24d0 → 0x458; toolbox items DS:a8dc + i
→ 0x45b + i (x, y, w, h of the tool picture); placed pieces DS:a94a + i → 0x4b0 + i; the toolbox
area DS:24a4 (7,67) 78×94, priority 100 → 0x459, pushed only while something is in the hand
(it covers the item zones then); Go Back DS:3306; the panel DS:331c once solved. Cursor regions:
DS:2488 (whole screen, puzzle cursor), and the label regions DS:aa94 + i over the items with a
count (`PzElec_AddToolCursorRegions` 3083:1d83).

Main loop (3083:0902..0ac7):
```
loop:
    if mode == 4: PzElec_UpdatePlugCursor()           3083:1a94 (§3.7)
    code = Input_StripHeld(Input_GetCode()); if code == 0: continue
    0x4b0..0x4bf, piece p = code - 0x4b0:
        if type(p) == 2 and PzElec_ToggleSwitch(p, mouse x, mouse y):   lever click
            if PzElec_IsSolved() and not solved: solved = 1; success (§2.6)
            PzSnd_Play(closed ? 2 : 3, 3)            the click comes after the reward
        else: PzElec_PickUpPiece(p)
    0x454 (board): mode 4  -> PzElec_LayWire(); then the success test
                   mode 99 -> PzElec_CutWire(); then the success test
                   else    -> PzElec_PlacePiece()     (no success test)
    0x459 -> PzElec_ReturnToToolbox()
    0x45b..0x45f -> PzElec_TakeFromToolbox(code)
    0x458 (only idle) -> Hint (§2.2), text 0x20dd + v
    0x5dd (only idle) -> PzSnd_Play(0xc,5), Puzzle_PressGoBack, DS:b144 = 0, leave
    0x0d, 0x5de -> if solved: DS:b144 = 1, leave
    0x1b -> Cmd_SaveAndExit; 0x17a2 / 0x17a3 -> DS:3332 = 2 / 1, leave
```
The success test after a wire action is `PzElec_IsSolved()`; when it passes and the puzzle was
not solved yet: solved = 1 and the success sequence. **Placing a piece never runs the test**,
even when it completes the circuit; the player must then toggle a switch or lay or cut a wire
[O 3083:0a23; keep].

### 3.6 Pieces in and out of the hand [O]

* **Take from the toolbox** `PzElec_TakeFromToolbox(code)` 3083:15ad: item i = code − 0x45b; only
  when its count > 0: count −1; held cursor = the item's picture with the click offset of the tool
  table; `PzSnd_Play(0, 3)`; mode = i; remove the piece zones; `MouseRegion_Clear`; redraw the
  toolbox; `Zones_AddLocal(DS:24a4)`.
* **Put back** `PzElec_ReturnToToolbox` 3083:156a (0x459): count[mode] +1; puzzle cursor; free
  the held cursor; piece zones back; mode 99; remove DS:24a4; redraw the toolbox; label regions
  back.
* **Place** `PzElec_PlacePiece` 3083:172a (0x454 with a piece): the piece's top-left = the mouse
  (mx, my); accepted only when mx > 0x5a, mx + w < 0x138, my > 0x12, my + h < 0x97 (w, h of the
  tool picture) and its body (y + 7, h − 14) overlaps no placed piece's body
  (`PzElec_StripsOverlap` with the full widths); then: puzzle cursor, free the held cursor,
  `PzElec_CreatePiece(mx, my)`, `PzElec_DrawBoard(1)`, `PzSnd_Play(4, 3)`, mode 99, zones and
  regions back, remove DS:24a4. Otherwise nothing happens.
* **Create** `PzElec_CreatePiece(x, y)` 3083:1e20: first slot with used = 0 (assert when none);
  type = DS:a8be, used, x, y, picture = the tool picture of the type; zone (x, y, picture size,
  press 0x4b0 + slot); `PzElec_SetTerminalGeometry`; `PzElec_AutoConnect(slot)`. Free slots keep
  their old type (§3.11).
* **Auto-connect** `PzElec_AutoConnect(p)` 3083:1850: for every free terminal t of p, every used
  other piece q, every free terminal u of q, in that order: if
  `PzElec_StripsOverlap(xq + anchor_u.x − 2, yq + anchor_u.y, 3, ovl_u,
  xp + anchor_t.x − 2, yp + anchor_t.y, 3, ovl_t)` then link (t of p ↔ u of q) with flag 3 on p's
  side and 0 on q's. A terminal can be linked again by a later (q, u) pair of the same loop: the
  last overlapping free terminal wins for t, while the earlier partner keeps a link to p [O; the
  loop does not break; keep].
* `PzElec_StripsOverlap(x1,y1,w1,h1, x2,y2,w2,h2)` 3083:19a9, signed, per axis (shown for x):
  if w1 ≤ w2: hit when (x1 ≥ x2 and x2 + w2 > x1) or (x1 + w1 > x2 and x1 + w1 ≤ x2 + w2);
  else: hit when (x2 ≥ x1 and x1 + w1 > x2) or (x2 + w2 > x1 and x2 + w2 ≤ x1 + w1). The same on
  y with h. Result: both axes hit. Negative heights take part in these comparisons as they are.
* **Pick up** `PzElec_PickUpPiece(p)` 3083:13c5 (click on a placed piece that is not a switch
  lever hit): only when not fixed. Every linked terminal is unlinked on both sides (piece 99,
  flags 0); for each such link where neither end has flag 3, DS:a8ce +1 — **the wire is gone, its
  count is not given back** [O; keep]. Piece zones removed, `Zones_AddLocal(DS:24a4)`,
  `MouseRegion_Clear`, slot used = 0; a switch is opened and gets the open picture; a bulb gets
  the unlit picture; held cursor = its current picture with its type's click offset;
  `PzSnd_Play(0, 3)`; mode = its type; `PzElec_DrawBoard(0)`; toolbox redrawn (flush).
* **Switch** `PzElec_ToggleSwitch(p, mx, my)` 3083:1327: only a click inside (x + 7, y + 20, 9,
  16) of the switch counts (`PzElec_PointInRect`, half-open, raw mouse position): closed →
  picture 0x20bc, state 1; open → the tool picture, state 0; `PzElec_DrawBoard(1)`; returns 1.

### 3.7 Wires [O]

* **Taking a wire** from the toolbox gives mode 4 (item 4). In mode 4 every loop pass runs
  `PzElec_UpdatePlugCursor` 3083:1a94: hot point = mouse + current cursor offset; the first
  terminal (pieces 0..14, terminals 0..2, used pieces only) whose hit rectangle contains it
  decides: if it is free and the plug is not shown, the current cursor's picture (DS:7a78) becomes
  0x20ba and DS:a8c4 = 1; if no terminal contains the point and the plug is shown, the held
  cursor's own picture comes back and DS:a8c4 = 0.
* **Stringing** `PzElec_LayWire` 3083:0b15 (0x454 in mode 4):
  ```
  DS:a8c2 = 99
  if not PzElec_FindFreeTerminal(): return              nothing happens, the wire stays in hand
  start = (DS:a8c2, DS:a8c0); PzSnd_Play(4,3); PzElec_SetMouseRange(1)
  repeat:
      p = hot point; PzElec_DrawBoard(0); PzElec_DrawRubberBand(p); flush
      while hot point == p:
          code = Input_StripHeld(Input_GetCode()); PzElec_UpdatePlugCursor()
          if code == 0x454:
              if PzElec_FindFreeTerminal():               a free terminal of ANOTHER piece
                  T[target] = {start piece, start term, flag 0}
                  T[start]  = {target piece, target term, flag 1}
                  PzElec_DrawBoard(0); puzzle cursor; free held cursor; mode 99
                  piece zones and label regions back; remove DS:24a4; PzSnd_Play(4,3); done
              else:                                      a miss: THE WIRE IS LOST
                  DS:a8ce += 1; PzElec_DrawBoard(0); toolbox; puzzle cursor; free held cursor
                  mode 99; zones and regions back; remove DS:24a4; done
  until done
  PzElec_SetMouseRange(0)
  ```
  The branch "target = start terminal" of the code is unreachable: `PzElec_FindFreeTerminal`
  skips the piece DS:a8c2, which is the start piece during the rubber band [O]. Only the
  0x454 code is looked at while stringing; other codes (menu, keys) are swallowed.
* `PzElec_FindFreeTerminal` 3083:1b75: hot point as above; the first terminal of a used piece
  other than DS:a8c2 whose hit rectangle contains it **and** whose link is 99: DS:a8ba / a8bc =
  its anchor (absolute), DS:a8c0 / a8c2 = terminal / piece, returns 1.
* `PzElec_SetMouseRange(board)` 3083:0d1a: board: x range 2·(0x5b − cw + 3) .. 2·(0x139 − cw),
  y 0x0b .. 0x90; otherwise x 0 .. 2·(0x13f − cw), y 0 .. 0xc7 − ch (cw, ch = current cursor
  size DS:7a70/7a72); clamps the stored mouse position and the saved-under position (INT 33h
  fn 7/8).
* **Cutting** `PzElec_CutWire` 3083:0d97 (0x454 idle): for every used piece and terminal with a
  link and a non-zero flag, in order, test `PzElec_OnWire(mx + 1, my, own anchor, partner
  anchor)` (the 1 is the puzzle cursor's click offset); on the first hit: unlink both ends
  (partner: piece 99, flag 0; own: piece 99), remove piece zones, `Zones_AddLocal(DS:24a4)`,
  `MouseRegion_Clear`, held cursor = the wire spool picture with offset (36,8),
  `PzSnd_Play(5,3)`, mode 4, `PzElec_DrawBoard(1)`. The wire is in the hand again (the count is
  not touched). A contact (flag 3) is also cut this way when the click lands on its short L,
  which hands the player a wire that the toolbox never had [O consequence; keep].
* `PzElec_OnWire(px, py, x1, y1, x2, y2)` 3083:0f18 (0xb7 bytes; Ghidra kept only 2 of them):
  hit when the point is in one of: x1 < x2: (x1, y1−1, x2−x1−1, 3); x1 > x2: (x2, y1−1, x1−x2,
  3); y1 < y2: (x2−1, y1, 3, y2−y1); y1 > y2: (x2−1, y2, 3, y1−y2) (`PzElec_PointInRect`
  3083:1c63: x ≤ px < x+w, y ≤ py < y+h).

### 3.8 Circuit evaluation [O 3083:0129, 04c7, 0492, 216e, helpers 0000..00d5]

**Path search** (identical in `PzElec_BatteryShorted(s)` and `PzElec_BulbLit(s)`): enumerates
the closed paths that leave piece s through its terminal 0 and come back to it through terminal
1 or 2.

```
if T[s][0].piece == 99 or T[s][1].piece == 99: return 0
rows = 0; table DS:a138 = all 0xFFFF (10 rows x 20 words)
done = 0
while not done:
    comps[20] = terms[20] = -1                       local stacks
    cur = T[s][0].piece; t = T[s][0].term            entry terminal of cur
    comps[0] = cur; terms[0] = t; depth = 1
    found = 0
    while not found:
        t = (t + 1) % 3                              try the next exit terminal of cur
        if PzElec_SkipExit(T[cur][t].piece, comps, depth, t, terms): continue
        again = 0
        if T[cur][t].piece == s:
            if T[cur][t].term == 0: found = done = 1                back at the start: finished
            elif PzElec_PathKnown(comps, depth): again = 1
            else: PzElec_PathAdd(comps, depth, s); found = 1        a new closed path
        if again: continue
        if found: break                              (the original makes one more move here;
                                                      it has no effect: the stacks are rebuilt)
        if depth > 0 and comps[depth-1] == cur and terms[depth-1] == t:     backtrack
            (cur, t) = (T[cur][t].piece, T[cur][t].term)
            depth -= 1; comps[depth] = terms[depth] = 0
        else:                                                              advance
            (cur, t) = (T[cur][t].piece, T[cur][t].term)
            if not PzElec_StackHas(cur, comps, depth):
                comps[depth] = cur; terms[depth] = t; depth += 1
```

* `PzElec_SkipExit(c, comps, depth, t, terms)` 3083:002b: 1 when c = 99; when c is on the stack,
  1 unless terms[depth−1] = t (the exit is the top's entry terminal: the backtrack move); else 0.
* `PzElec_PathKnown(comps, depth)` 3083:007c: some row r < DS:a2c8 has row[0..depth−1] =
  comps[0..depth−1] (a prefix compare).
* `PzElec_PathAdd(comps, depth, s)` 3083:00d5: row DS:a2c8 = comps[0..depth−1], s; DS:a2c8 += 1.
* Each found path restarts the search from scratch; the duplicate test makes it go on to the
  next path. The search ends when the walk comes back to s's terminal 0, which happens once the
  first piece has exhausted its other exits. The table has room for **10 rows**; an 11th would
  overwrite DS:a2c8 itself [O layout; I: never reached by the shipped data].

**`PzElec_BatteryShorted(s)`** 3083:0129 (s a battery), for each row (the start piece excluded):
* a row with no battery, no bulb and no open switch (only Y-wires and closed switches) → 1;
* a row made only of Y-wires and batteries, with at least one battery besides s, in which no
  battery's terminal 0 is linked to the piece before it in the row (s for the first piece):
  every battery is passed from its terminal 1 to its terminal 0, the way s is left, so the
  batteries drive a loop with no load → 1 (the code compares each battery's test with the
  previous one, starting from "not linked", and accepts the row when nothing changed);
* otherwise 0. `PzElec_CheckBatteries` 3083:0492 sets DS:a87e[i] for every used battery
  (stale entries of other slots stay).

**`PzElec_BulbLit(s)`** 3083:04c7 (s a bulb): returns 0 at once when a terminal of s is unlinked
(DS:a87e then keeps its old values); else `PzElec_CheckBatteries()`, the path search, then:
```
DS:a89c[0..14] = 0; DS:a89c[s] = 1
bad = 0; lit = 0; first = 0
for each row:
    bat = 0; bulb = 0; open = 0
    for each piece c of the row (s excluded), position k:
        DS:a89c[c] = 1                                     (also for rows with an open switch)
        if type(c) == 0 and not DS:a87e[c]:
            bat = 1
            v = (T[c][0].piece != (k == 0 ? s : row[k-1]))  1: entered through terminal 1 or 2
            if not first: first = 1; ref = v
            elif v != ref: bad = 1                          batteries of opposite directions
        elif type(c) == 2 and not closed(c): open = 1
        elif type(c) == 1: bulb = 1
    if open: continue
    if bat: lit = 1
    elif not bulb: bad = 1                                  a plain wire loop shorts the bulb
return lit and not bad
```
`first`/`ref` hold across all rows: every working battery on every closed path must face the
same way. A shorted battery counts as no battery.

**`PzElec_AllBulbsLit`** 3083:216e: 1 when every slot 0..14 **whose type is 1** passes
`PzElec_BulbLit`; the used flag is not checked (§3.11).

### 3.9 Success test `PzElec_IsSolved` 3083:2196 [O]

```
if count[1] or count[2] or count[0]: return 0        bulbs, switches, batteries all placed
                                                      (Y-wires and wires may stay)
if not PzElec_AllBulbsLit(): return 0                 also leaves DS:a89c of the last bulb
any_switch = 0; matters = 0; list = []
for i in 0..14:
    if type(i) == 0 and used(i): if not DS:a89c[i]: return 0
    elif type(i) == 2:                                used flag NOT checked
        if not DS:a89c[i] or not closed(i): return 0
        any_switch = 1
        closed(i) = 0; if not PzElec_AllBulbsLit(): matters = 1; closed(i) = 1
        list.append(i)                                5 entries of stack space
if len(list) > 1 and not matters:
    open all listed switches; if not PzElec_AllBulbsLit(): matters = 1; close them again
return matters if any_switch else 1
```

So: no batteries, bulbs or switches left in the toolbox; every bulb lit; every placed battery
and every switch on a closed path of the last evaluated bulb (the highest slot of type 1);
every switch closed; and, when there are switches, opening one of them (or, failing that, all
of them) darkens a bulb [O]. A sixth switch would overwrite the "matters" local [O layout; the
shipped data has at most 2].

### 3.10 Drawing, sounds and timing [O]

* `PzElec_DrawScreen` 3083:250e: `Screen_LoadPicture(0x20b2, 0x20b3)`, `Lpx_Draw(0,10)`,
  `Puzzle_InitPanel(text 0x20fb + v)`, `Gfx_SaveToBack(0,0,320,200)`, `PzElec_DrawBoard(0)`,
  `Screen_FadeOutIn`, cursor = puzzle cursor.
* `PzElec_DrawBoard(flush)` 3083:1c99: `Gfx_RestoreRect(91,17,222,135)`; for slots 0..14 used:
  a bulb first gets `PzElec_SetBulbPicture(i, PzElec_BulbLit(i))` (3083:0844: lit → 0x20bb, and
  `PzSnd_Play(1,4)` when the picture changes to lit; unlit → the tool picture); then the piece is
  drawn keyed at (x, y); then `PzElec_DrawWires`; flush when asked.
* `PzElec_DrawWires` 3083:0fcf: g_drawColor = 1; for each used piece i and terminal t with a link
  and a non-zero flag: flag 1 → `PzElec_DrawWireL(own anchor, partner anchor)`; in every case the
  dots of both ends (`PzElec_DrawTerminalDot`). `PzElec_DrawWireL(x1,y1,x2,y2)` 3083:10ac:
  x1 < x2: HLines (x1, y1−1|y1|y1+1, x2−x1+1); x1 > x2: HLines (x2, y1−1|y1, x1−x2) and (x2,
  y1+1, x1−x2); then y1 < y2: VLines (x2−1|x2|x2+1, y1, y2−y1); y1 > y2: VLines (x2−1|x2, y2,
  y1−y2) and (x2+1, y2, y1−y2). `PzElec_DrawRubberBand(x,y)` 3083:11a8 draws the same shape from
  (DS:a8bc, DS:a8ba) and then the start dot.
* `PzElec_DrawToolbox` 3083:163c: `Gfx_RestoreRect(7,67,78,94)`; for each item with count > 0:
  its picture at (x, y), the units digit at (digit x, digit y) and, when count ≥ 10, the tens
  digit at (digit x − 2, digit y), drawn over the units digit [O; the 14 wires of variant 29 show
  the overlap; keep]; flush.
* Effects (bank 0x11cc): 0 take, 1 a bulb lights, 2 switch closed, 3 switch opened, 4 piece
  placed / wire started / wire connected, 5 wire cut, 7..10 jingle, 0xb Hint, 0xc Go Back, 0xd
  points.
* Timing: nothing is timed. The only waits are the button-release waits and the effect waits of
  the success sequence (`PzSnd_WaitEnd(1)` lets the bulb sound finish first) [O].

### 3.11 Quirks to keep [O]

1. The success test runs after switch clicks and wire actions only, never after placing a piece.
2. Wires attached to a piece that is picked up vanish; a wire whose second click misses a free
   terminal vanishes. The toolbox count is not restored in either case.
3. Free slots keep their type. `PzElec_AllBulbsLit` tests every slot of type 1 and
   `PzElec_IsSolved` every slot of type 2, used or not. A bulb or switch picked up from a slot
   above the lowest free slot and placed again leaves a stale type in its old slot (with all
   links 99): the puzzle cannot be solved until another piece fills that slot [O code; I
   reachable: take a Y-wire back to the toolbox, then move a bulb that sits above it].
4. DS:a89c reflects only the last bulb evaluated (the highest slot of type 1).
5. A contact can be cut into a wire (§3.7); the auto-connect loop does not stop at the first
   partner (§3.6).
6. The tens digit of a count overlaps the units digit.

---

## 4. Energy forms (kind 6, ovl15 33cd)

The player draws lines from up to four energy forms (left column, names drawn on the screen)
to up to four devices (right column, animated pictures) with a pencil, may erase lines with an
eraser, and checks (`r2/puzzle/k6_01_screen.png`).

### 4.1 Resources (`PzEnergy_LoadResources` 33cd:1cc7) [O]

| Id | Use | Kept in |
|---|---|---|
| 0x1093 / 0x1094 | screen and palette | — |
| 0x204f | digits | DS:ae80 |
| 0x1097 | pencil tool picture (33×23) | DS:ae34 |
| 0x1095, 0x1096 | pressed Hint, pressed check | DS:ae3c, DS:ae38 |
| 0x1130 | bundle of 8 texts: one per energy form, for the hint text | DS:ae84 |
| 0x1131 | hint text header (variants 0..2 only) | DS:ae28 |
| 0x1132 | panel text, a `sprintf` format with one `%d` (the number of lines) | DS:ae2c |
| cursor 3 / 7 / 8 | arrow (no line left) / pencil, offset (0,3) / eraser, offset (0,18) | DS:af44 / af3c / af40 |
| device R (bundle) | idle animation of a device | DS:ae60[d] |
| R + 1 (bundle), R + 2 (`I`, 2 words), R + 3 (`I`, a word per frame) | working animation, {repeat count, final frame}, effect per frame (−1 none); loaded only while it plays | DS:ae70 / ae40 / ae50 [d] |
| effect bank 0x11d2 | sounds | |

A device occupies four consecutive ids R..R+3; the groups run from 0x1098 to 0x112c (38 groups,
of which the variants use 34); R and R+1 are bundles of `G` pictures, R+2 and R+3 `I` arrays [O].

### 4.2 Variants: in the code, not in the data [O 33cd:1274, `en.py`]

There is no `F` resource. `PzEnergy_LoadVariant(v)` 33cd:1274 clears DS:adb8 (0x1a0 bytes) and
DS:ad70 (0x120 bytes), then jumps through **CS:1b69 (28 entries)** into straight-line code made
only of `MOV word [imm16], imm16` (C7 06 lo hi vlo vhi) and `JMP rel16 / rel8` (E9, EB) into
the tails of other cases, ending at **33cd:1b23**. The engine reads the variants by interpreting
exactly these three opcodes from the table target up to 33cd:1b23 (any other byte is an error):
`en.py` does this for all 28 cases without exception. The stores set:

| DS | Field |
|---|---|
| ae8a | the line budget (2..8) |
| adb8 + 0x1a·k, k 0..3 | energy form of row k (0..7, −1 = empty row) |
| ad70 + 0x12·d, d 0..3 | device of row d: resource id R (0xFFFF = empty row) |
| ad72 + 0x12·d + 2·f | 1 when device d uses energy form f |

Then (33cd:1b23) for every row k and device d: satisfied[k][d] (DS:adba + 0x1a·k + 2·d) = 0 when
device d uses form[k], else 1. The budget equals the number of needed lines in all 28 variants
[O, `en.py`]. Energy-form names: DS:28ea + 20·f (8 names; f 0 = "Mechanical", …).

### 4.3 State [O]

| DS | Type | Meaning |
|---|---|---|
| adb8 + 0x1a·k | i16 | form of row k |
| adba + 0x1a·k + 2·d | i16 | satisfied[k][d]: no line needed, or the needed line is drawn |
| adc2 + 0x1a·d + 2·k | i16 | sent[d][k]: the line k→d delivered energy (note the swapped index) |
| adca + 0x1a·k + 2·d | i16 | line[k][d] drawn |
| ad70 + 0x12·d | rec | device id and form flags |
| ae20[d] | i16 | idle frame of device d; −1 once it has worked |
| ae88 | i16 | mode: 0 pencil, 1 eraser, −1 no line left (arrow cursor) |
| ae8a | i16 | lines left |
| ae8c + 0x16·d / aee4 + 0x16·k | zone | device / form connectors: circles of radius 7 at (219, y) / (180, y), press 0x456 + d / 0x452 + k |
| 28c8.. | i16 | layout (initialised data, read from the EXE): DS:28c8 device picture x, DS:28ca[4] device picture y per row; DS:28d2 / DS:28d4 x of the form / device connector zones, DS:28d6[4] their y; DS:28de / DS:28e0 x of the line ends at the forms / devices, DS:28e2[4] their y. Below, "device area" = (DS:28c8, DS:28ca[d], 88, 34); fx = DS:28de and dx = DS:28e0 are the x of the line ends (the lines are 38 pixels wide in the shipped EXE) |

Fixed zones (`PzEnergy_AddZones` 33cd:0dde): eraser DS:2870 (10,90) 40×35 → 0x450; pencil
DS:2886 (10,56) 40×33 → 0x451; Hint DS:289c → 0x44f; check DS:28b2 → 0x44e; work area DS:285a
(59,16) 254×141 → 0x44c. `PzEnergy_AddConnectorZones` 33cd:1189 adds the circles of the used rows.
No cursor regions.

### 4.4 Screen (`PzEnergy_DrawScreen` 33cd:1ba1) [O]

`Screen_LoadPicture(0x1093, 0x1094)`, `Lpx_Draw(0,10)`; `PzEnergy_BuildHintAndLabels(v)`
(33cd:0735): the hint text in a 2000-byte buffer DS:ae30 = (v < 3: text 0x1131, LF LF) + for each
used row the form's text (member form of 0x1130) + LF LF, the last LF LF removed; meanwhile font
0, colour 0, the current font's spacing word (+6 of `Font_GetCurrent` 2b16:0426) = 1, each used
row's form name drawn right-aligned so that it ends at x = DS:28de − 4, at y = DS:28e2[k] − 4,
spacing back to 2. Then `sprintf(buf[150], text 0x1132, budget)` → `Puzzle_InitPanel(buf)`; for
each row: empty form row → `Gfx_FillRect(DS:28de − 4, DS:28e2[k] − 4, 15, 15)` colour 0x9d
(hides the connector), empty device row → the same at x DS:28e0 − 4, else the device's idle
frame 0 at (DS:28c8, DS:28ca[d]); `Gfx_SaveToBack(0,0,320,200)`; `PzEnergy_DrawPencilCount`;
cursor = pencil; `Screen_FadeOutIn`.

`PzEnergy_DrawPencilCount` 33cd:1135: `Gfx_RestoreRect(10,56,40,33)`; when lines are left: 0x1097
at (14,63) and digit (lines left) at (37,70); flush.

### 4.5 Input and the main loop [O, switch CS:01de, 19 codes]

```
PzSnd_LoadBank(0x11d2); mode = 0; load variant, resources; screen; zones; Hint_Show(0x1052)
Timer_Set(7, 30)
loop:
    code = Input_StripHeld(Input_GetCode())
    PzEnergy_IdleAnimate()                   every pass
    0x44c: if mode == 1: PzEnergy_EraseLines()
    0x44e: check button (pressed picture, effect 0x2b, wait release, restore, flush),
           PzEnergy_SendEnergy(); if not solved and PzEnergy_IsSolved(): solved = 1, success
    0x44f: Hint (§2.2), text = the built buffer
    0x450: PzEnergy_SelectEraser()           PzSnd_Play(0x16,3), eraser cursor, pencil count, mode 1
    0x451: PzEnergy_SelectPencil()           only when lines are left: pencil cursor, 0x16, mode 0
    0x452..0x455: PzSnd_Play(0x17,3); PzEnergy_DrawFromForm(code)
    0x456..0x459: PzSnd_Play(0x17,3); PzEnergy_DrawFromDevice(code)
    0x5dd: PzSnd_Play(0x30,5), Go Back, DS:b144 = 0, leave
    0x0d / 0x5de / 0x1b / 0x17a2 / 0x17a3: §1.6
```

* **Drawing a line** `PzEnergy_DrawFromForm(code)` 33cd:099c (only in mode 0): row k = code −
  0x452; start (fx, y[k]); `PzEnergy_SetMouseRange(1)` (33cd:022a: x 0x3d .. 0x13f − cw,
  y 0x12 .. 0x97); loop: code = strip(get); when the mouse moved: `PzEnergy_DrawLines()` and the
  rubber line `PzEnergy_DrawLine(start, (mx, my), 0)`, flush; then on code − 0x44c (table
  CS:0acb): 0x44c (work area) or 0x452..0x455 (a form) → stop; 0x456..0x459 (device d) → if no
  line k→d yet: `PzSnd_Play(0x18,3)`, line = 1, satisfied[k][d] = 1 when device d uses form[k],
  lines left −1, at 0: arrow cursor and mode −1; `PzEnergy_DrawPencilCount`; stop; other codes →
  go on. At the end: `PzEnergy_DrawLines`, `PzEnergy_SetMouseRange(0)`.
* `PzEnergy_DrawFromDevice(code)` 33cd:0ae7 is the mirror (start (dx, y[d]); table CS:0c22:
  0x452..0x455 completes with form k; 0x44c and 0x456..0x459 stop). Its rubber line ends at (mx,
  my + 3) (the pencil's offset), the other at (mx, my) [O; keep].
* A wrong line costs a line of the budget like a right one; it does not stop the success test.
* **Erasing** `PzEnergy_EraseLines` 33cd:0c97 (0x44c in mode 1): while a button is held, on every
  mouse move: point = (mx, my + 18); for every drawn line k→d (k outer, d inner) with
  `PzEnergy_PointOnLine((fx, y[k]), (dx, y[d]), point)`: satisfied[k][d] = (device d uses
  form[k]) ? 0 : 1, line = 0, sent[d][k] = 0, lines left +1, `PzEnergy_DrawPencilCount`,
  `PzEnergy_DrawLines`, `PzSnd_Play(0xb,3)`. The eraser stays selected; the pencil must be
  clicked again to draw.
* `PzEnergy_DrawLine(x1,y1,x2,y2,colour)` 33cd:0e0c: if x2 < x1 swap the ends; dx = x2 − x1, dy =
  |y2 − y1|, sy = ±1, e = 2·dy − dx; dx = 0 and dy ≠ 0: `Gfx_VLine(x1, min(y), dy + 1)`; else
  x = x1 − 1, repeat dx times: x += 1, pixel (x, y); while e ≥ 0: y += sy, pixel (x, y), e −=
  2·dx; then e += 2·dy. The column x2 is never drawn. `PzEnergy_PointOnLine` 33cd:10a9 walks the
  same pixels and returns 1 on an exact match (never for dx = 0).
* `PzEnergy_DrawLines` 33cd:08f5: `Gfx_RestoreRect(59,16,254,141)`, every drawn line in colour 0,
  flush.

### 4.6 Checking: `PzEnergy_SendEnergy` 33cd:02a0 [O]

```
g_drawColor = 15
for k in 0..3, for d in 0..3:
    if line[k][d] and not sent[d][k]:
        PzEnergy_AnimatePulse((fx,y[k]) -> (dx,y[d]), erase 0)
        if device d uses form[k]:
            PzEnergy_AnimateDevice(d); sent[d][k] = 1
            PzEnergy_AnimatePulse((dx,y[d]) -> (fx,y[k]), erase 0)
        else:
            line[k][d] = 0
            PzEnergy_AnimatePulse((dx,y[d]) -> (fx,y[k]), erase 1)    the line retracts
            lines left += 1; PzEnergy_DrawPencilCount()
            Gfx_RestoreRect(device area of d); flush
```

* `PzEnergy_AnimatePulse(x1,y1,x2,y2,erase)` 33cd:0ee7: the same stepping as `PzEnergy_DrawLine`
  but without the swap (it walks from (x1,y1) towards (x2,y2)); `Timer_Set(7,5)`;
  `PzSnd_Play(x2 > x1 ? 2 : 3, 3)`; a frame every 4th x step: `Timer_Wait(7)`,
  `PzEnergy_DrawLines`, (erase: the line from (x2,y2) to the current point in colour 0),
  `PzEnergy_DrawPulse(x, y)` (33cd:0ff7: a 5×5 diamond outline of HLines in colour 0x83, flush),
  `Timer_Set(7,5)`; at the end `PzEnergy_DrawLines` and `PzSnd_Stop` of the pulse effect. A
  38-pixel line gives 9 frames of 5 ticks.
* `PzEnergy_AnimateDevice(d)` 33cd:03c8: loads R+1, R+2, R+3; colour 0x9d; finishes the idle
  cycle: repeat {`Timer_Set(7,30)`; idle frame + 1; if it is < the idle frame count: fill the
  device area, draw it, flush, `Timer_Wait(7)`, again}; then for rep in 0..R+2[0]−1, for each
  frame f of R+1: fill, draw frame f, flush, effect R+3[f] (prio 3) when ≠ −1, `Timer_Wait(7)`,
  `Timer_Set(7,30)`; then idle frame = −1, frame R+2[1] of R+1 drawn keyed (no fill),
  `Gfx_SaveToBack(device area)`, `PzSnd_StopAll()`, the three resources freed. The device then
  shows that frame for good.
* `PzEnergy_IdleAnimate` 33cd:064d (every main-loop pass): when `Timer_Expired(7)`: colour 0x9d;
  for each device row in use whose idle frame ≠ −1: fill the device area, draw idle frame n,
  n += 1, wrap to 0 at the bundle's member count, `Gfx_SaveToBack(device area)`; then
  `Timer_Set(7,30)` and flush. Period **31 ticks** (Expired is strict) while the main loop runs;
  the idle animation stands still inside the drawing, erasing and checking loops.
* **Success** `PzEnergy_IsSolved` 33cd:0c3e: every satisfied[k][d] of the used rows is 1. It is
  tested only after the check button, and once solved the check button only replays the pulses
  of unsent lines.

Effects (bank 0x11d2): 2 / 3 pulse to the right / left, 0xb line erased, 0x16 tool chosen, 0x17
connector clicked, 0x18 line made, 0x26 Hint, 0x27..0x2a jingle, 0x2b check, 0x30 Go Back, 0x31
points, and the per-frame effects of the devices (R+3).

---

## 5. Launch (kind 5, ovl17 3797)

The player launches an object from a plunger-driven launcher across a sloped table into the
glowing hole of a row of 8 (`r2/puzzle/k5_01_screen.png`, REFERENCE_CAPTURES §3.2). Depending on
the variant the player chooses the object, the energy, the gravity, the ramp and the ramp's
surface. The surface changes are animated by the DAM engine.

### 5.1 Resources (`PzLaunch_LoadResources` 3797:0464) [O]

| Id | Use | Kept in |
|---|---|---|
| 0x1133 / 0x1134 | screen and palette | — |
| 0x1148 | plunger, 8 pictures by energy | DS:afdc[8] |
| 0x114a | gravity slider, 4 pictures (0 = locked, 1 low, 2 normal, 3 high) | DS:afcc[4] |
| 0x1143 / 0x1141 / 0x1142 | ramp picture sets for surface 0 / 1 / 2, 6 pictures each (by ramp 0..5) | DS:b02c / b014 / affc |
| 0x1144 / 0x1147 / 0x1146 / 0x1145 | objects 0 / 1 / 2 / 3, 8 flight frames each (frame 0 = full size) | DS:b0a4 / b044 / b064 / b084 |
| 0x1135, 0x1136, 0x1137, 0x1138 | surface tool icons (buttons 0x452, 0x453, 0x456, 0x457) | DS:b0d0, b0d4, b0c8, b0c4 |
| 0x1139 + target | 8 glow frames of the target hole | DS:afac[8] (loaded by the variant) |
| 0x204f, 0x1095 | digits, pressed Hint | DS:b0dc, b0cc |
| 0x1163 + v / 0x1181 + v | hint text / panel text (30 each) | DS:afa8 / afa4 |
| cursor 3 | puzzle cursor | DS:b104 |
| DAM 0x114b..0x1156, frames 0x1157..0x1162 | the four surface transitions × 3 ramps (§5.5) | loaded while playing |
| effect bank 0x11ce | sounds | |

### 5.2 Variant table DS:2c62 (30 records of 15 words) [O 3797:030c, 023c]

| Word | Field |
|---|---|
| 0 | initial ramp DS:b124: 0..2 choosable, 3..5 fixed |
| 1, 2, 3, 4 | offer surface tool 0x452 / 0x453 / 0x456 / 0x457 (0 / 1) |
| 5, 6, 7, 8 | offer object 2 / 3 / 0 / 1 (0 / 1) |
| 9 | initial gravity DS:b11e (0 high, 1 normal, 2 low), −1 = locked (the slider shows "normal" only) |
| 10 | target hole 0..7 (also its glow bundle 0x1139 + w10) |
| 11 | required ramp (6 = any) |
| 12 | required surface (4 = any) |
| 13 | required object (5 = any) |
| 14 | required gravity (4 = any; with gravity locked only 1 passes) |

In the shipped table the ramp is fixed and the gravity locked in the early variants; the
later ones open the choices one by one [O, `la.py`].

`PzLaunch_LoadVariant(v)` 3797:030c: DS:b12c = record; DS:b11e = 0, `PzLaunch_SetGravity(w9)`,
`PzLaunch_DrawGravity`; energy 0; target = w10; DS:af90 = 1 (glow on); load 0x1139 + w10;
ramp = w0; offers from w1..w8; surface DS:b11a = 1; object = −1 (`PzLaunch_SelectObject(−1)`);
`PzLaunch_DrawBox`; `PzLaunch_Redraw`.

### 5.3 State [O]

| DS | Type | Meaning |
|---|---|---|
| b118 | i16 | selected object 0..3, −1 none |
| b11a | i16 | surface state 0..2 (start 1) |
| b11c | i16 | landing position 0..12 of the last launch: 0..3 short, 4..11 hole 0..7, 12 over |
| b11e | i16 | gravity 0 high, 1 normal, 2 low, −1 locked |
| b124 | i16 | ramp 0..2, or fixed 3..5 |
| b126 | i16 | target hole |
| b128 | i16 | gravity picture: 3 high, 2 normal, 1 low |
| b12a | i16 | plunger energy 0..7 |
| b120 | far | frame set of the selected object |
| afa0 | far | ramp picture set of the current surface |
| af8c, af92..af9e, af9a | | object sprite: picture, the clip rectangle copied when it was set (af98 left, af96 right, af94 top, af92 bottom), y af9c, x af9e, valid af9a |
| af90 | i16 | the target glows (1 until solved) |
| 3304 | i16 | glow frame 0..7 |
| b108..b116 | i16 | offers (names in the TSV) |

### 5.4 Input [O, switch CS:144f, 22 codes]

Zones (`PzLaunch_AddZones` 3797:02c3, records DS:2b02..2c61): Hint (12,13) → 0x44d; ramp
right-hand area DS:2bc8 (225,55) 90×66 → 0x454 (ramp + 1); ramp middle area DS:2bde (111,97)
114×58 → 0x455 (ramp − 1); gravity slider DS:2bf4 (5,135) 17×31 → 0x458; gravity labels low
DS:2c0a (22,135) 35×11 → 0x45a, normal DS:2c20 (22,146) 35×10 → 0x45b, high DS:2c36 (22,156)
35×10 → 0x45c; plunger DS:2c4c (250,122) 54×33 → 0x459. The box (`PzLaunch_DrawBox` 3797:09b7:
`Gfx_RestoreRect(5,51,54,75)`, `Zones_RemoveLocalGroup(1000)`, then for each offer its icon and its
zone, tag 1000): object 0 icon at (7,91) zone DS:2b18 → 0x44e; object 3 (8,108) DS:2b2e → 0x44f;
object 2 (30,91) DS:2b44 → 0x450; object 1 (29,104) DS:2b5a → 0x451; tool 0x452 (10,53) DS:2b70;
tool 0x453 (37,54) DS:2b86; tool 0x456 (13,67) DS:2b9c; tool 0x457 (33,75) DS:2bb2 (icons:
member 0 of the object sets and 0x1135..0x1138).

```
loop:
    code = PzLaunch_GetCode()                  3797:018e: PzLaunch_Animate(0,0), then strip(get)
    0x44e / 0x44f / 0x450 / 0x451: PzSnd_Play(0,3); PzLaunch_SelectObject(0 / 3 / 2 / 1); Redraw
    0x452: surface 1 -> 0 (sound 0x15, DAM 0x1151 / frames 0x115d);
           surface 2 -> 1 (0x14, 0x114e / 0x115a) then 1 -> 0 (0x15, 0x1151 / 0x115d)
    0x453: surface 1 -> 2 (0x16, 0x114b / 0x1157);
           surface 0 -> 1 (0x13, 0x1154 / 0x1160) then 1 -> 2 (0x16, 0x114b / 0x1157)
    0x456: surface 0 -> 1 (0x13, 0x1154 / 0x1160)
    0x457: surface 2 -> 1 (0x14, 0x114e / 0x115a)
    0x454 / 0x455: PzLaunch_StepRamp(+1 / -1); Redraw
    0x458: PzLaunch_DragGravity()
    0x45a / 0x45b / 0x45c: PzLaunch_SetGravity(2 / 1 / 0); PzLaunch_DrawGravity()
    0x459: r = PzLaunch_PullPlunger(); if not solved: solved = r
           if solved and DS:af90: DS:af90 = 0; Redraw; success (§2.6)
    0x44d: Hint (§2.2); 0x5dd: PzSnd_Play(0xf,5), Go Back, DS:b144 = 0, leave
    0x0d / 0x5de / 0x1b / 0x17a2 / 0x17a3: §1.6
```
(sound = `PzSnd_Play(n, 3)` before each transition; a transition plays only from the listed
states.)

* `PzLaunch_StepRamp(delta)` 3797:08e2: when w0 is 3, 4 or 5 the ramp is set to w0 (fixed);
  else ramp += delta, below 0 → 2, from 3 → 0.
* `PzLaunch_SelectObject(o)` 3797:0bd9: DS:b118 = o; the frame set of o (unchanged for −1);
  `PzLaunch_PutObjectOnLauncher` (3797:0217: when an object is selected, sprite = its frame 0 at
  (239,111) with the current clip, DS:af9a = 1).
* `PzLaunch_SetGravity(g)` 3797:087d: ignored while gravity is −1; else DS:b11e = g and the
  picture index 3 − g. `PzLaunch_DrawGravity` 3797:08b6: picture DS:b128 (0 when locked) at
  (4,135), flush. `PzLaunch_DragGravity` 3797:0fca: while a button is held, on every vertical
  move: y < 0x92 → low, < 0x9d → normal, else high (`PzLaunch_Animate(0,0)` runs meanwhile).
* **Plunger** `PzLaunch_PullPlunger` 3797:0f33: x0 = mouse x; object back onto the launcher;
  `PzLaunch_Redraw`; `PzSnd_Play(1,3)`; while a button is held: animate; when the mouse x moved
  and x ≥ x0: energy = (x − x0) / 3 clamped to 0..7, `PzLaunch_Redraw` (moving left of x0 keeps
  the last energy). On release: `PzSnd_Stop(1)`; energy 0 → return 0; else
  `PzLaunch_ComputeLanding()`, energy = 0, `PzLaunch_Redraw`, `PzLaunch_Fly()` when an object is
  selected, return `PzLaunch_IsSolved()`.

### 5.5 Physics by table, flight and DAM [O]

**Landing** `PzLaunch_ComputeLanding` 3797:0b58:
```
i  = objectTerm[object]                    DS:2fe6, i16[4]   (object -1 reads the word DS:2fe4)
i += (object == 3) ? 2 : surfaceTerm[surface]          DS:2fee, i16[3]
i += (gravity == -1) ? DS:2ff6 : gravityTerm[gravity]  DS:2ff4, i16[3] (DS:2ff6 = the normal entry)
i += rampTerm[ramp]   ramp 0..2: DS:2ffa[ramp]; fixed ramp 3 -> DS:2ffe, 4 -> DS:2ffc, 5 -> DS:2ffa
landing = DS:3000[i + energy - 1]          i16[29], values 0..12
```
The engine reads the term tables and DS:3000 from the EXE (the constant 2 for object 3 is code,
3797:0b6c). In the shipped EXE the landing grows by one per index step over the playable range,
so one energy step moves the object by one hole.

**Flight** `PzLaunch_Fly` 3797:0c33. Ramp class: ramps 2/3 → A, 1/4 → B, 0/5 → C. Landing
effect by object: 0 → 0x12, 1 → 0x11, 3 → 3, 2 → 2.
* landing < 4 (falls short): `PzSnd_Stop(7)`, `PzSnd_Play(9,3)`; for i = 0..landing and then
  i = landing..0: `Timer_Set(7, 4·i + 10)`, sprite = frame 0 at (DS:3168[i], climbY[class][i]),
  `PzLaunch_Redraw`, `PzLaunch_WaitTimer(7)`; then the object back onto the launcher and Redraw.
  climbY: A DS:3170, B DS:321a, C DS:32c4 (4 words each).
* else: `PzSnd_Play(surface 0 → 5, 2 → 6, else 4; 3)`; climb for i = 1 and 3: `Timer_Set(7,10)`,
  sprite at (DS:3168[i], DS:3170[i]) (class A, whatever the ramp), Redraw, wait;
  `PzSnd_Play(7,3)`; for j = 0..landing − 4: `Timer_Set(7,15)`; on the last step the clip becomes
  left = DS:304c[landing − 4], bottom = DS:305c[landing − 4], top = 0x11 (the object sinks into
  the hole); sprite = frame DS:303a[j] at (x[j], y[j]) with x = far array DS:3120[landing − 4],
  y = far array A DS:3144 / B DS:31f6 / C DS:32a0 [landing − 4] (each array has landing − 3
  entries); clip back to left 0, bottom 199, top unchanged; Redraw; wait. Then `PzSnd_Stop(7)`
  and the landing effect (landing > 11 → effect 9, a miss) with priority 4. The object stays
  where it landed until it is selected or launched again.
* `PzLaunch_SetObjectSprite(x, y, picture)` 3797:01e1 copies the clip rectangle at the time of
  the call; `PzLaunch_DrawObject` 3797:0000 draws with that clip.

**Judge** `PzLaunch_IsSolved` 3797:023c: (w12 = 4 or w12 = surface) and (w13 = 5 or w13 =
object) and (w14 = 4, or gravity = −1 and w14 = 1, or gravity ≠ −1 and w14 = gravity) and (w10
= −1 or w10 = landing − 4) and (w11 = 6 or w11 = ramp).

`la.py` checks the model: every variant has a solution with an offered object (exactly one for
variants 0..14) [O under the model].

**Surface transitions with DAM** `Anim_PlayModal(script, frames, newSurface)` 3797:100e:
```
r = ramp % 3
s = Res_LoadCopy(script + r); PzLaunch_Animate(0,0)
f = Res_LoadCopy(frames + r); PzLaunch_Animate(0,0)
frame table DS:ba26[i] = member i payload (offset table entry + 2), for every member of f
ch = Anim_Start(0, 0, 0, 0, script s, 0, 0, 0, 0, 0)    every argument 0 but the script
                                         (3797:1095..10ad): channel 0, layer 0, position (0,0),
                                         frame base 0, flags 0, no callbacks
while Anim_IsRunning(ch, s):
    Timer_Set(7, 18)
    Gfx_RestoreRect(64, 17, 251, 139)        the play area
    Anim_Step()                              one DAM tick: the cel queue is rebuilt (3d39:0597)
    PzLaunch_PutObjectOnLauncher(); PzLaunch_DrawPlunger()
    if script == 0x1154: PzSnd_Play(0x13, 3)     restarted every DAM tick
    PzLaunch_WaitTimerAnim(7)                until Timer_Expired(7): PzLaunch_Animate(0,1)
surface = newSurface; PzLaunch_Redraw(); free f and s
```
* A DAM tick lasts **19 ticks** (deadline + 1, `Timer_Expired` is strict), not 18 as
  `fmt_graphics.md` §6.2 says [O].
* The cels are drawn only inside `PzLaunch_Animate(0,1)`, which draws when timer slot 6 expires
  (every 6 ticks): glow frame, ramp picture of the old surface, `Anim_DrawSprites(0)`, object,
  plunger, flush. So a DAM tick's picture appears at the next timer-6 expiry and is redrawn about
  three times per DAM tick [O].
* **The 0x7FF8 callback is never set**: DS:ba1e is BSS and no code writes it; the twelve scripts
  contain no 0x7FF8, and `Anim_Start` gets no end callback. The engine implements 0x7FF8 as
  `fmt_graphics.md` §6.1 describes, with an empty callback table, and needs nothing else for this
  puzzle [O].
* Channel use: one channel (0), position (0,0), frame base 0, keyed cels (channel flags 0).

**Animation tick** `PzLaunch_Animate(force, sprites)` 3797:0103: when `Timer_Expired(6)` or
force: glow frame DS:3304 of the target (DS:afac) at (DS:32cc, DS:32dc)[target] when DS:af90;
`PzLaunch_DrawRamp` (3797:0070: member ramp of DS:afa0 at (DS:32ec, DS:32f8)[ramp]); sprites →
`Anim_DrawSprites(0)`; `PzLaunch_DrawObject`; `PzLaunch_DrawPlunger` (3797:009f: 0x1148 member
energy at (250,122), the digit at x = (7 − digit width) / 2 + 283, y 130); unless forced: glow
frame + 1 (wrap 8), flush, `Timer_Set(6,5)` (period **6 ticks**). `PzLaunch_Redraw` 3797:0ae9:
ramp set by surface (0 → DS:b02c, 1 → DS:b014, 2 → DS:affc); `Gfx_RestoreRect(64,17,251,139)`;
ramp, object, plunger; `PzLaunch_Animate(1,0)`; flush.

### 5.6 Screen, effects and quirks [O]

* `PzLaunch_DrawScreen` 3797:040d: `Screen_LoadPicture(0x1133, 0x1134)`, `Lpx_Draw(0,10)`,
  `Puzzle_InitPanel(0x1181 + v)`, `Gfx_SaveToBack(0,0,320,200)`, puzzle cursor,
  `Screen_FadeOutIn`. The variant (box, gravity, ramp, plunger) is drawn **after** the fade-in
  (`PzLaunch_LoadVariant` runs after the screen), then `Hint_Show(0x1052)`, `Timer_Set(6,5)`.
* Effects (bank 0x11ce): 0 object chosen, 1 plunger pulled, 2 / 3 / 0x11 / 0x12 landing of
  object 2 / 3 / 1 / 0, 4 / 5 / 6 the climb on surface 1 / 0 / 2, 7 flight, 9 falls short or
  misses, 0xa Hint, 0xb..0xe jingle, 0xf Go Back, 0x10 points, 0x13..0x16 the four surface
  transitions.
* **No object selected**: the plunger still works; the landing uses DS:2fe4 (= 4, the last word
  of variant 29) as the object term, nothing flies, and the judge accepts it when the variant's
  required object is "any": variants 24, 25, 27, 28 and 29 can be solved without launching
  anything [O code; `la.py`; keep].

---

## 6. Simple machines (kind 1, ovl20 3938, `simple.c`)

The player places simple-machine parts (wedge, lever, …: 8 part types) on the matching places
of an everyday machine and checks (`r2/puzzle/k1_01_screen.png`: a nail and one wedge).

### 6.1 Resources (`PzSimple_LoadResources` 3938:0ed8) [O]

| Id | Use | Kept in |
|---|---|---|
| 0x204a / 0x204b | screen and palette | — |
| 0x204e | 8 label pictures, cursors over the inventory slots | DS:b37a |
| 0x204f | digits | DS:b356 |
| 0x2059 + v | machine bundle: member 0 the machine, 1 the finished machine, 2 + r the filled picture of record r | DS:b37e, far pointers DS:b1ee[] |
| 0x2050 | 8 part pictures (the part in the hand and placed) | DS:b34e, DS:b1ba[8] |
| 0x2051 + t (t 0..7) | part bundles: member 0 = inventory icon, members 1..n−1 = fit animation | DS:b35a[8] |
| 0x1096, 0x1095 | pressed check, pressed Hint | DS:b1de, DS:b1e2 |
| 0x2081 | 8 texts, one per part type, for the hint text | DS:b352 |
| 0x2082 | panel text (one for all variants) | DS:b1e6 |
| 0x2083 | hint text header (variants 0..2 only) | DS:b1ea |
| F 0x206d + v | variant data (§6.2) | DS:b1b4 |
| cursor 3 | puzzle cursor | DS:b41a |
| effect bank 0x11ca | sounds (shared with machine pieces) | |

### 6.2 Variant data: F 0x206d + v (20 entries, 1392 bytes) [O 3938:0dfe, `sm.py`]

`PzSimple_LoadVariant` allocates 0x570 bytes (assert), `Res_LoadTo(0x206d + v)`, empties the
10 placed slots (type 9). Layout:

| Off | Field |
|---|---|
| +0x00 | s8 count[8]: parts of each type in the inventory |
| +0x08, +0x0a | x, y of the machine picture |
| +0x0c + 0x8a·r, r 0..9 | record r: +0 x, +2 y of its filled picture; +4 six 22-byte acceptance rectangles (zone layout; only shape, x, y, w, h are read; **shape 2 = unused**); +0x88 needed part type 0..7, 8 = no record or already filled |

The number of records with a type below 8 equals the sum of the counts, and the machine bundle
has that many members + 2, in all 20 variants (1 to 9 parts) [O, `sm.py`]. Unused records hold
left-over editor data, which is harmless because type 8 matches no part.

### 6.3 State [O]

| DS | Type | Meaning |
|---|---|---|
| b1b4 | far | the variant copy (counts change as parts are taken; +0x88 of a record becomes 8 when filled) |
| b1b8 | i16 | part type in the hand, 9 none |
| b38a + 0x0e·s, s 0..9 | rec | placed part: type (9 free), x, y, w, h, test x, test y |
| b416 | far | held cursor (the part picture, click offset = the type's test offset) |
| b41a | far | puzzle cursor |
| b21a + 0x16·t | zone[8] | inventory slots: (icon x, icon y, icon size), press 0x44d + t |
| b41e + 0x1c·t | region[8] | label cursor regions over the slots (cursor 0x204e member t) |
| b1da | far | the hint text (2000 bytes, assert) |
| 3402 + 15·t | rec[8] | **part layout** (initialised data, 15-byte records): +0 icon x, +2 icon y, +4 byte (= t), +5 digit x, +7 digit y, +9 test-point dx, +0xb dy, +0xd fit effect |
| 347a[t], 348a[t] | i16 | x, y offset of the fit animation |

Zones: work area DS:33aa (66,14) 245×142 → 0x4b1; check DS:33c0 → 0x4b0; Hint DS:33ec → 0x458;
inventory column DS:33d6 (4,49) 48×98, priority 100 → 0x44c, pushed only while a part is in the
hand (it then covers the slot zones). Cursor regions: DS:338e (whole screen, puzzle cursor) and
the slot regions of the types with a count (`PzSimple_AddCursorRegions` 3938:0524).

### 6.4 Input and the main loop [O, codes 0x44d..0x454 first, then switch CS:020b, 10 codes]

```
PzSnd_LoadBank(0x11ca); held = 9; resources; variant; cursor 3; MouseRegion_Add(DS:338e)
PzSimple_DrawScreen(); PzSimple_BuildHintText(v); PzSimple_InitCursorRegions()
PzSimple_AddCursorRegions(); PzSimple_DrawInventory(); Hint_Show(0x1052)
loop:
    code = Input_StripHeld(Input_GetCode())
    0x44d..0x454: PzSimple_TakeFromInventory(code - 0x44d)
    0x44c: PzSimple_ReturnHeld()
    0x4b1: held == 9 ? PzSimple_PickUpPlaced(mx, my) : PzSimple_DropPart(mx, my)
           then PzSimple_DrawPlaced(1)
    0x4b0: check button (0x1096 at (10,150), effect 0x12, wait release, restore, flush)
           PzSimple_CheckParts(); if not solved: solved = PzSimple_JudgeAndReveal(); if solved: success
    0x458: Hint (§2.2) with the built text
    0x5dd: PzSnd_Play(0x15,5), Go Back, DS:b144 = 0, leave
    0x0d / 0x5de / 0x1b / 0x17a2 / 0x17a3: §1.6
```

* `PzSimple_TakeFromInventory(t)` 3938:09df: when count[t] > 0: `PzSnd_Play(0,3)`, count −1,
  held cursor = part picture t with click offset (dx, dy) of the layout, held = t,
  `Zones_AddLocal(DS:33d6)`, `MouseRegion_Clear`; always `PzSimple_DrawInventory`.
* `PzSimple_ReturnHeld` 3938:099f: `PzSnd_Play(1,3)`, remove DS:33d6, count[held] +1, puzzle
  cursor, slot regions back, `PzSimple_RemoveRecordZones` (removes the acceptance rectangles of
  that type from the zone list; they are never added, so this does nothing), held = 9, inventory
  redrawn. The held cursor record is not freed here [O; a small leak].
* `PzSimple_DropPart(mx, my)` 3938:0233: first free placed slot (10 slots; the shipped data needs
  at most 9); accepted only when mx ≥ 0x42, my ≥ 0x0e, mx + w ≤ 0x137, my + h ≤ 0x9c (w, h of
  the part picture); slot = {held, mx, my, w, h, mx + dx, my + dy}; picture drawn, flush;
  `PzSnd_Play(1,3)`; puzzle cursor; held cursor freed; slot regions back; remove DS:33d6; held =
  9. Outside the area nothing happens.
* `PzSimple_PickUpPlaced(mx, my)` 3938:0345: slots 9 down to 0 (the topmost first): the first
  used slot whose rectangle holds (mx, my) (`PzSimple_PointInRect` 3938:046b, half-open):
  `PzSnd_Play(0,3)`, held cursor = its picture with the type's offset, held = its type,
  `Zones_AddLocal(DS:33d6)`, `MouseRegion_Clear`, slot freed.
* `PzSimple_DrawPlaced(flush)` 3938:040f: `Gfx_RestoreRect(66,14,245,142)`, every used slot's
  picture at (x, y), flush when asked.
* `PzSimple_DrawInventory` 3938:0d63: `Gfx_RestoreRect(4,49,48,98)`; for each type with a count
  > 0: icon (member 0 of 0x2051 + t) at (icon x, y), digit count at (digit x, y); flush.

### 6.5 Checking [O]

`PzSimple_CheckParts` 3938:054d: `PzSnd_Play(0xe,3)`; for each placed slot s (0..9) in use:
* r = `PzSimple_FindRecord(s)` (3938:0a64: records 0..9 in order whose needed type = the part's
  type, rectangles 0..5 not of shape 2, the first containing the test point);
* r = −1 or record r already filled → **bounce back**: count[type] +1,
  `PzSimple_BounceBack(s)` (3938:0671: dx = (x − icon x) / 5, dy = (y − icon y) / 5 (signed
  integer division); slot freed; `PzSimple_DrawPlaced(0)`; `PzSnd_Play(0x11,3)`; 5 times:
  `PzSimple_DrawPlaced(0)`, the part at (x, y), flush, `Gfx_RestoreRect(x, y, w, h)`,
  `Timer_Set(7,5)`, `Timer_Wait(7)`, x −= dx, y −= dy), then `PzSimple_DrawInventory`;
* else **fit**: slot freed; `PzSimple_AnimateFit(type, x, y)` (3938:0886: `PzSnd_PlayLoop(fit
  effect, 7)`; for k = 1..n−1 of bundle 0x2051 + type: `PzSimple_DrawPlaced(0)`, frame k at (x −
  DS:347a[type], y − DS:348a[type]), flush, `Gfx_RestoreRect` of that frame,
  `Timer_Set(7,40)`, `Timer_Wait(7)`; `PzSnd_Stop`); `Gfx_RestoreRect(66,14,245,142)`; the record's
  filled picture (machine member r + 2) at the record's (x, y), saved with `Gfx_SaveToBack`;
  record type = 8;
* after each slot: `PzSimple_DrawPlaced(1)`.

`PzSimple_JudgeAndReveal` 3938:0750: returns 0 unless all 10 records are 8. Then
`Timer_Set(7,100)`, `Timer_Wait(7)`; the finished machine (member 1) drawn at the machine's
(x, y) and saved with `Gfx_SaveToBack(66,14,245,142)`; member 0 drawn again on the screen; flush;
`PzSnd_Play(0x13,5)`; a growing reveal: (x, y, w, h) = (187, 84, 1, 1); repeat
{`Gfx_RestoreRect(x,y,w,h)`, flush; if x > 66: x −= 3; if y > 14: y −= 2; if w < 245: w += 6;
if h < 142: h += 4; `Timer_Set(7,3)`, `Timer_Wait(7)`} until a pass changes nothing (42 passes;
the last rectangle (64,14,247,145) slightly exceeds the work area, which shows the frame that is
already there); `Gfx_RestoreRect(66,14,245,142)`, flush, `PzSnd_Stop(0x13)`; returns 1.

### 6.6 Hint text, screen and effects [O]

* `PzSimple_BuildHintText(v)` 3938:0b59: v < 3: text 0x2083 + LF LF; for t in 0..7: fill and push
  slot zone t; when count[t] ≠ 0: text t of 0x2081 + LF LF; assert the buffer is not passed; the
  last LF LF removed. Then `Zones_AddLocal` of DS:33aa, DS:33ec, DS:33c0.
* `PzSimple_DrawScreen` 3938:0e76: screen at (0,10), member 0 at the machine's (x, y),
  `Puzzle_InitPanel(0x2082)`, `Gfx_SaveToBack(0,0,320,200)`, `Screen_FadeOutIn`.
* Effects (bank 0x11ca): 0 take, 1 put down / put back, 3..6 jingle, the fit effect of each part
  type (word +0x0d of its layout record DS:3402; looped, priority 7), 0xe check start, 0x11
  bounce back, 0x12 Hint and check button, 0x13 reveal, 0x15 Go Back, 0x16 points.

### 6.7 The machine examples of ovl21

The simple-machines puzzle does not use ovl21 (machine pieces) nor its example names
(DS:357c[20] far pointers to the strings DS:35cc..36c7, read by 3a4a:0111..0157 to format the
machine-pieces hint text with `sprintf`). What the two puzzles share is the effect bank 0x11ca,
the digits 0x204f and the pressed Hint 0x1095 [O: no reference to DS:357c or to ovl21 in
ovl20]. The examples belong to part B.

---

## 7. Timing summary [O]

| Where | Timer | Duration |
|---|---|---|
| main loops of the four puzzles | none | unpaced polling; draws on change |
| electric | none | button-release waits, effect waits in the success sequence |
| energy idle animation | slot 7, `Timer_Expired` | 31 ticks per frame, only while the main loop polls |
| energy pulse | slot 7, `Timer_Wait` | 5 ticks per frame, a frame every 4 x steps (9 frames for a 38-px line) |
| energy device animation | slot 7, `Timer_Wait` | 30 ticks per frame (idle cycle completion, repeats × frames) |
| launch glow and redraw | slot 6, `Timer_Expired` | 6 ticks per frame |
| launch DAM transition | slot 7, `Timer_Expired` | 19 ticks per DAM tick |
| launch climb / flight | slot 7, `Timer_Expired` | 11 ticks per climb step, 16 per flight step, 4·i + 11 per roll-back step |
| simple machines bounce | slot 7, `Timer_Wait` | 5 × 5 ticks |
| simple machines fit | slot 7, `Timer_Wait` | 40 ticks per frame |
| simple machines reveal | slot 7, `Timer_Wait` | 100 ticks, then 42 × 3 ticks |
| reward | effect waits | 4 × the points effect's length (72.83 Hz driver ticks) |

`Timer_Wait` ends at tick ≥ deadline, `Timer_Expired` at tick > deadline (ARCHITECTURE §3.2): a
`Timer_Set(n)` followed by an Expired loop lasts n + 1 ticks.

---

## 8. Boundary with part B

* **Part A owns** the framework (§1, §2: ovl18 38e2 and ovl19 3924, the panel, Hint/Go Back
  conventions, the reward, and the use state 9 makes of `Room_OpenPuzzleDoor` 12e0:0a24) and
  ovl13, ovl15, ovl17, ovl20.
* **Part B** owns ovl14 32fe (balance), ovl16 35bb (magnets), ovl21 3a4a (machine pieces, with
  the example names DS:357c / DS:35cc), ovl22 3b2f (gears) and their data: F 0x2002..0x2020
  (31, gears [O], `code_puzzles_b.md` §3.2), F 0x209c..0x20af (20, machine pieces [O], §2.2),
  D 0x211b (balance), D 0x11a1
  (magnets), the screens 0x2021, 0x2084, 0x2119, 0x119f and the effect banks 0x11c6, 0x11c8,
  0x11d0, and 0x11ca as far as machine pieces use it.
* Shared and specified here: `Puzzle_InitPanel`, `Puzzle_ShowSolved`, `Puzzle_AwardPoints`,
  `Puzzle_PressGoBack`, `Puzzle_ShowHintText`, `Puzzle_FreePanel`, the `PzSnd_*` helpers
  (`PzSnd_SetTranspose` is used only by balance), the codes 0x0d / 0x1b / 0x5dd / 0x5de /
  0x17a2 / 0x17a3 and DS:3332 / DS:b144, the resources 0x1091, 0x1092, 0x1095, 0x2000, 0x204f.
  Part B's puzzles follow the same skeleton (they write DS:3332 and DS:b144 the same way:
  32fe:0852..0a0e, 35bb:18d6..19fe, 3a4a:007b..01a1, 3b2f:1f50..202e [O]). The 4-tick timer of
  magnets (35bb:119f) paces the simulation substeps (`code_puzzles_b.md` §5.7), not the main
  loop: every puzzle main loop is unpaced, as in part A.

---

## 9. Porting notes [R]

* Keep the variant data where it is: electric and simple machines read `F` resources; launch
  reads the 30-byte records DS:2c62 and the tables DS:2fe6..DS:32f8 from the EXE; energy runs the
  small store-and-jump interpreter of §4.2 over the code of ovl15 (file offset 0x25620 + 0x1b69
  for the table) at start-up. None of these values belong in the repository.
* The zone records, layout tables and launch tables are initialised DGROUP data: read them from
  the EXE. The electric terminal geometry (§3.4) is code: blocks of `MOV word [SI + disp16],
  imm16` (C7 84 lo hi vlo vhi; disp = DGROUP offset of the field of piece 0) at
  3083:1f67..2149, one block per type through the jump table CS:2166, types 0..2 joining a shared
  tail at 3083:1fcd by `JMP`, all ending at 3083:2162; the engine can read it the same way as the
  energy variants, or carry the 81 small geometry constants of §3.4.
* Reproduce the path search and both evaluations literally (§3.8), including the restart per
  path, the prefix-duplicate test and the 10-row table; a "cleaner" graph algorithm will differ on
  Y-wire circuits and bad wiring.
* The puzzles poll without pacing: the engine's poll pumps the host and must not add frame waits.
  Timed animations use the virtual PIT with the strict / non-strict distinction of §7.
* rand(): two or three calls in state 9 (§1.3) and one when a puzzle is solved (§2.6), nothing
  else; seeded fidelity runs stay in step through a puzzle as long as the solve happens.
* Quirks to keep: §1.5 (stale DS:b144), §3.11, §5.6 (no object), the electric Hint button's
  restore width, the 19-tick DAM tick, the rubber-line end asymmetry of energy, the device that
  stays in its final frame.

---

## 10. Open questions

1. Electric path table: can any reachable circuit (15 slots, up to 4 Y-wires in variant 29 plus
   the player's) produce more than 10 closed paths from one battery or bulb? If so the original
   corrupts DS:a2c8; the engine should detect it and report it.
2. *Answered by `code_ui.md` §3.1:* font record +0x06 is the spacing (file header +10), so
   energy's patch to 1 (33cd:0735) narrows the label spacing of the UI font from 2 to 1.
3. *Answered by `code_ui.md` §8:* `TextView_Run` does not freeze `g_ticks` (it only uses timer
   slot 6), so the idle animation of energy (slot 7) and the glow of launch (slot 6) step at once
   when the viewer closes. It also leaves `Mouse_ForceDefault(0)` and the cursor hot spot (0, 0)
   behind.
4. *Answered by `code_world.md` §5.3 / §6.6:* +0x16 is the object's hit kind (4 = an open door);
   after "go through" the player steps out of the twin door in the target room (pending sequence
   33, state 5).
5. The physical meaning of the launch surface states (oil can, brush and sponge-like tools in the
   DAM frames) and of the three ramps; names here are neutral.
6. Reference captures still to make (capture agent): a solved electric puzzle with a switch, an
   energy check with a wrong line, a launch with a surface change (DAM timing against the 19-tick
   model and the timer-6 redraws), a simple-machines reveal; and the sound waits of the reward on
   the speaker.
