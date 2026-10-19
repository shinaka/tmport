# DOS edition (SST.EXE): user interface: text, boxes, menus, help, title, sign-in, elf questions, HUD

Scope: every function of segments `43cb` (ui.c: menu bar, drop-downs, message boxes, buttons,
text, help pager), `4ba4` (title.c: title and attract pages; the three RTL routines at the end of
the segment excluded), `47c7` (signin.c: sign-in, USERS.DAT, game start/restore/end, record
packing), `46ce` (quiz.c: elf questions, dictionary, deal, promotion, clue words) and the HUD
part of `130f` (status bar, counters, rank-chest screen). It also gives, as context the UI code
depends on, the command input model of `4a85` (key tables, mouse zones, command queue) and a
table of every sound trigger of the program. Wave 3 implements these modules from this document
alone.

Evidence: Ghidra export `re/dos/ghidra_export/` (decompiled C and `disassembly.asm`; register
arguments of the segment-1000 primitives were read from the disassembly), DGROUP bytes of
`extracted/SST_unpacked.EXE`, the data files in `TM-DOS/`, and pixel comparisons against the
DOSBox-X captures (§16). Scratch: `extracted/scratch/dos_w1_ui/` (`ds.py` DGROUP reader,
`ui_check.py` box/menu/help/name-entry renderer, `title_check.py`, `hud_check.py`,
`rank_check.py`, `keys.py` key and zone tables, `sndcalls.py` sound call sites, `animsfx.py`
script effects, `msgboxes.py` box table).

Notation: **[O]** observed in code, bytes or captures; **[I]** inferred. `SSSS:OOOO` = Ghidra
address; `DS:XXXX` = DGROUP offset (file offset `0x3FB30 + XXXX`). x coordinates of the drawing
code are **bytes of the 4-bpp back buffer** (1 byte = 2 pixels, 0..159) unless written "px"; y is
in pixels (0..199). Rectangles are inclusive. A "colour word" such as `0xCCCC` is four equal
4-bit logical colours (logical colours: `fmt_graphics.md` §2.2; 0 blue, 3 light red, 4 red,
6 light magenta, 7 cyan, 10 light cyan, 11 yellow, 12 white, 13 light grey, 15 black); fills use
its low byte, so `0xFDFD` fills each byte with left pixel 15 and right pixel 13. Game text is
referred to by DGROUP offset. Function names follow `re/names/dos/` (canonical names of the wave 2
merge, decision table in `re/names/dos/README.md`).

---

## 1. Modules and coverage

| Seg | Module | Role | Functions specified |
|---|---|---|---|
| `43cb` | ui.c | menu bar and drop-downs, message boxes, Yes/No buttons, input zones of boxes, text renderer, help pager, menu commands, sound hot key and sound sync | 30 / 30 |
| `4ba4` | title.c | title page, two story pages, Super Solvers page, attract timing, keys | 12 / 12 (+3 Turbo C RTL routines `_rtl_creat`, `_rtl_trunc`, `open` at 4ba4:057d..05b0, not specified: use the host file API) |
| `47c7` | signin.c | Club Sign-In list, new name, remove player, dialogs, USERS.DAT read/write, record pack/restore, game start/end | 39 / 39 |
| `46ce` | quiz.c | elf question screen, answer judging, rewards, question loading and dictionary expansion, deal, promotion, slot reordering, clue words | 22 / 22 |
| `130f` | main.c (part) | HUD: status bar, coin count-up, key icon, clue words, prompt icon, numbers, rank-chest screen, screen painters | 6 / 14 (the other 8: main loop, tick, scrolling, mouse poll belong to the main-loop spec) |
| `4a85` | input.c (context) | `Input_Poll`, `Input_LookupKey`, `Input_HitZone`, `Input_Tick` (§2.3) | 4 / 6 given as reference (`Input_MouseWalk`, `Mouse_ChangedMask` are play input) |

---

## 2. Runtime context

### 2.1 When UI code runs

Two clocks (ARCHITECTURE §3): the timer interrupt at 236.696 Hz increments `g_ticks` (DS:3d2b,
u32) and every 5th interrupt runs `Game_Tick` (130f:00fe) = **47.3 Hz**; the frame loop of
`main` (130f:0007) runs as fast as the machine draws. UI code runs at both rates [O]:

| Per game tick (47.3 Hz, interrupt time) | Per drawn frame (main loop) |
|---|---|
| `Input_Tick` (4a85:03ac): reads one key or the mouse zone into `g_cmd`, opens the menu bar from play. It runs every tick, also while the step-done flag DS:910d is set (130f:0190). Nothing runs while `g_quit` (DS:912e) ≠ 0 (130f:00fe), which the I/O routines set around disk access. | `main` order (130f:0037..00cf): `Frame_Begin`; **only if** `g_fullRedraw`, DS:8fb2 and DS:8fc8 are all 0: `SignIn_Update`, `Gameover_UpdateMessages` (49a6), `Hints_Update` (49a6), `Menu_Update`; then always `Scene_Update`, `Scroll_Update`, `Search_Update`, `Quiz_Update`, `Sound_SyncState`, `Game_HandleHotkeys`, `Screen_DrawOverlay` (painter: HUD / quiz / sign-in), `Playfield_Redraw`, `ShelfRoom_DrawShelf`, `RankChest_Update`, `Sprites_DrawSorted(0, n)`, `Frame_Present`, `Sprites_EraseAll(0, n)` with n = 13 when `g_uiMode` = 0 else 1 (only the mouse pointer slot 0 is drawn while any UI mode is active). Loop until `g_quit` ≠ 0, then `Game_Shutdown`. |
| Counters: DS:90fe counts 12, 11, .., 0, 12 .. (one step per tick; the name-entry caret blinks on it, §9.5); DS:9100 (byte) counts down; when it reaches 0 **and** `g_uiMode` = 0 it is reloaded with 60 and `g_seconds` (DS:9115) += 1, otherwise it keeps decrementing through 255 [O 130f:019a..01bd]. | The title runs its own loop (`Title_Run`, §8) with the same Sprites/Frame_Present tail. |

Blocking waits exist only in `Button_Flash` (4 × 18 ticks, §4.4); ticks keep running meanwhile.

### 2.2 `g_uiMode` (DS:9119, byte)

| Bit | Mode | Set by | Cleared by |
|---|---|---|---|
| 0x80 | title | `Game_InitVars` 1460:03cb, `Title_Run` 4ba4:0440 (`= 0x80`) | `Title_Run` end (`= 0`), then `main` sets `= 8` |
| 0x40 | menu bar open | `Input_Tick` 4a85:0408/042e (Esc / F1-F3 in play), `Quiz_Update` 46ce:0203 (F1-F3 in a quiz), `MenuCmd_HelpPage` 43cb:026c (help closed) | `Menu_Update` 43cb:0080 (Esc or Enter) |
| 0x20 | dialog or help | `Menu_Update` 43cb:0062 (Enter on an item), `Input_Tick` 4a85:0421 (Esc in play on a slow CPU) | `Dialog_Update` 43cb:01dc, `MenuCmd_HelpPage` 43cb:0267 |
| 0x10 | hint bubble | `Hints_Update` 49a6:008a | 49a6:00f5 |
| 0x08 | sign-in | `main` 130f:0032, `MenuCmd_StartNewPlayer` 43cb:0321 (`= 8`) | `Game_Start` 47c7:0910 |
| 0x04 | start/end message | 49a6:052f (`= 4`) | 49a6:02c5 |
| 0x02 | elf question | `Elf_NetCatch` 4521:152b | `Quiz_Update` 46ce:009e/00c1/022d, `Game_Start` 47c7:0915 |

Several bits can be set together (a menu opened over a question gives 0x42). Each handler checks
its own condition (§5.2, §10.4). `Sound_SyncState` pauses the music while `g_uiMode` ≠ 0 (§6.3).

### 2.3 Commands: input contexts, key tables, mouse zones (4a85, reference)

All UI code consumes one **command** at a time in `g_cmd` (DS:9226, i16; −1 = none). Commands
come from per-context tables selected by `g_inputContext` (DS:922c) [O]:

| Ctx | Used by | Key list → command (BIOS key code = scan·256 + ascii) |
|---|---|---|
| 0 | play | Esc 0, F1 1, F2 2, F3 3, ← 4, → 5, ↑ 6, ↓ 7, Enter 8, Alt+M 12, s/S 13, Ctrl+S 14 |
| 1 | menu bar | Esc 0, F1-F3 1-3, ← 4, → 5, ↑ 6, ↓ 7, Enter 8 |
| 2 | dialogs and help | Esc 0, ← 4, → 5, Enter 8, Tab 4, ↑ 4, ↓ 5, Y 14, N 15, y 14, n 15 (entries 5-9 rewritten per box, §4.5) |
| 3 | elf question | same list as ctx 0 |
| 4 | sign-in | Esc 0, ↑ 6, ↓ 7, Enter 8, Space 32, Ctrl+R 30, Backspace 31, digits and letters = their ASCII code (48-57, 65-90, 97-122) |
| 5 | title pages | same list as ctx 0 |

Tables [O]: key lists far pointers DS:80f3[ctx] (each list: u16 count n, then n u16 key codes);
command lists DS:810b[ctx] (u16 count slot = −1 for "not found", then n i16 commands); zone lists
DS:80db[ctx] (u8 count, then 8-byte records `x1, y1, x2, y2, hover, press, held, release`, x in
bytes 0..159, y in pixels, commands as i8). Static lists: ctx 0 keys DS:7d84 / commands DS:7da0 /
zones DS:7f2e; ctx 1 DS:7dbe / 7dd2 / 7f87; ctx 2 DS:7de6 / 7dfe / 8010; ctx 3 keys and commands
= ctx 0, zones DS:8069; ctx 4 DS:7e16 / 7ea2 / zones DS:6194 (in the sign-in data); ctx 5 keys =
ctx 0, zones DS:80ca. The engine reads all of them from the EXE.

Zones that matter [O]: ctx 0: menu titles x 0..25 / 26..51 / 52..84, y 0..10 → 1/2/3 on press,
hold and release; play-field x 0..159 y 16..143: press/held 9, release 10; status bar y 146..199:
press/held 11, release 10. ctx 5: zone 0 (y 0..189) release → 8, zone 1 (y 189..199) release → 8
(byte DS:80da: 13 in the file, 8 from the title page on, 13 again on story page 2). Boxes, menus, help, quiz answers and sign-in rows
rewrite zones at run time (§4.5, §9.3, §10.4). A zone with x1 = 200 can never match (mouse x ≤
159): this is how entries are disabled.

`Input_Poll` (4a85:000e), once per tick from `Input_Tick` [O]:

```
if bioskey(1) == 0:                       # no key waiting
    DS:9285 = 0
    cmd = (i8) Input_HitZone(zones[ctx])   # -1 when no zone hit
else:
    DS:9285 = key = bioskey(0)
    cmd = cmds[ctx][ Input_LookupKey(key, keys[ctx]) ]   # index 0 = -1
if DS:9238 == 0 and g_uiMode != 0:        # previous command not yet consumed by a UI handler
    if cmd != -1 and cmd != g_cmd: DS:90f6 = cmd     # one-deep queue, last one wins
else:
    g_cmd = cmd
    if DS:90f6 != -1: g_cmd = DS:90f6; DS:90f6 = cmd # queued command first
    if g_cmd != -1: DS:9238 = 0                      # "unconsumed"
```

UI handlers mark a command consumed by setting DS:9238 = 1 (and usually `g_cmd = −1`). DS:90f6 is
also written directly to inject a command for the next tick ("Y" = Yes + Enter, PgDn, help
closing). `Input_LookupKey(key, list)`: `i = list[0]; while i != 0 and list[i] != key: i--;
return i` [O 4a85:0107]. `Input_HitZone(z)`: scans zones from `count−1` down to 0 and takes the
first with `x1 ≤ mouseX ≤ x2 and y1 ≤ mouseY ≤ y2` (mouse DS:911a/911e; buttons now DS:90f4,
previous DS:9143, both from `Mouse_Poll` 130f:0d94); it returns byte +5 (no button), +6 (button
went down), +7 (held) or +8 (released) [O 4a85:0130].

`Input_Tick` (4a85:03ac) [O]: `Input_Poll()`; then, if `g_cmd` ≠ −1 and `g_uiMode` = 0:
* if DS:926c (menu lock, set by cells during end sequences and searches) ≠ 0: commands 1..3 and
  ≥ 12 are discarded (`g_cmd = −1`);
* else command 0 (Esc): slow CPU (`g_slowCpu` = 1): `g_uiMode = 0x20` (an assignment; equal to an
  OR here because `g_uiMode` is 0), dialog id DS:9323 = 10
  (the Exit-and-Save box, §5.4); otherwise `g_uiMode = 0x40`, `g_cmd = 8`, DS:90fc = 1 (File),
  DS:9111 = 2 (third item): the next `Menu_Update` takes this as Enter on "Exit and Save", so Esc
  in play opens the Exit-and-Save box directly without drawing the menu [O, matches capture
  `tandy/13`];
* commands 1..3 (F1..F3 or a click on a title): `g_uiMode = 0x40`, DS:90fc = −1, DS:9111 = 0
  (`Menu_Update` then opens menu `cmd−1`).

### 2.4 Redraw protocol and screen painters

* `g_dirty` (DS:9255): something was drawn, `Frame_Present` must copy dirty spans.
* **Redraw request DS:922e** [O]: 0 none; 1 full redraw of the current screen; 2 full redraw
  that also draws the actors (used under menus so the frozen actors stay visible).
* **Screen painter** `g_screen` (DS:913e) → far pointer table DS:0104: 0 `Hud_Redraw`
  (130f:07a5), 1 `Quiz_DrawScreen` (46ce:0271), 2 `SignIn_Draw` (47c7:03cf) [O].
* `Screen_DrawOverlay` (130f:0c73), every frame and on demand [O]: if DS:922e ≠ 0 **and**
  DS:0102 = 1 (a box background was saved, slow CPU only, §4.2): draw the saved background back
  (`Sprite_Draw` of the save buffer DS:37ca at its own position, flag 1), clear DS:0102,
  DS:37c8/37ca/37cc, redraw the three menu titles unselected unless `g_area` = 4 (sign-in),
  `g_dirty++`, DS:922e = 0. Otherwise, if `g_sceneWanted` (DS:8fca) < 3, call painter
  DS:0104[`g_screen`]. In scenes 3 and 4 (castle-top close-up, treasure room) **no painter runs**:
  no HUD, no menu titles [O].
* `UI_ForceRedraw` (47c7:152b): if DS:0102 ≠ 0: DS:922e = 1, `Screen_DrawOverlay()`,
  `Present_Dirty()`. On a fast CPU it does nothing (DS:0102 stays 0) [O].
* Painters redraw fully only while DS:922e ≠ 0 and then clear it; otherwise `Hud_Redraw` updates
  only the parts whose cached values changed (§11).

### 2.5 Drawing primitives (segment 1000, used with register arguments)

| Routine | Arguments | Behaviour [O] |
|---|---|---|
| `Gfx_FillRect` 1000:169b | SI x1, DI y1, CX x2, DX y2 (bytes, inclusive), AL fill byte | every byte of the rectangle = AL; CGA translates through 2005:7E00; adds a dirty rectangle when DS:37ae bit 0 |
| `Gfx_ReplaceColorRect` 1000:176c | SI, DI, CX, DX as above, AX "from" colour word, BX "to" colour word | per pixel: if the pixel equals the "from" colour it becomes the "to" colour. Not an XOR. Highlights call it three times to exchange two colours through a temporary (colour 3) |
| `Font_DrawChar` 1000:2180 | AX colour word (1 = measure only), BX x bytes, CX y, DX glyph code | draws the glyph's 1-bpp rows (whole bytes of 8 pixels from x·2) with set bits in the colour and clear bits leaving the background; returns `width >> 1` (advance in bytes, glyph width from the font, `fmt_graphics.md` §6). Exact [O wave 2]: the buffer is addressed linearly, so a glyph running past byte 159 continues at the start of the next row; within each 4-pixel word, pixels 0/1 (the first byte) take the colour word's **low-byte** nibbles (high, low) and pixels 2/3 its **high-byte** nibbles |
| `Sprite_GetHeader` 1000:2159 / `Sprite_Draw` 1000:1b8d | AX = frame·2; BX x bytes, CX y, AL flags (0 opaque with hotspot, 0x40 masked colour 0 transparent, 0x20 mirror, 0x80 no hotspot) | `fmt_graphics.md` §5.3. Status-bar, panel and title frames have absolute positions in their hotspot and are drawn at (0,0) |
| `Tile_DrawColumn` 1000:19cf | BX map pointer, SI x bytes, DI y top, DX y bottom | one 8-px tile column of the loaded PAN picture; `Screen_DrawBackdrop` (47c7:042d) and the title call it 40 times (x 0..0x9C step 4, map pointer += 0x32) to draw OPENING.PAN full screen |

Helpers of 43cb (stack arguments) [O]: `Gfx_HLine(x, y, w, c)` = fill (x, y, x+w−1, y);
`Gfx_VLine(x, y, h, c)` = fill (x, y, x, y+h−1); `Gfx_FillRectColor(x1, y1, x2, y2, c)`;
`Gfx_FrameRect(x1, y1, x2, y2, c)` = fills (x1, y1, x1, y2), (x1, y1, x2, y1+1), (x2, y1, x2,
y2), (x1, y2−1, x2, y2): a frame with 1-byte (2-px) sides and 2-row top and bottom.

---

## 3. Text

### 3.1 `Text_Draw` (43cb:0b63) `int Text_Draw(int x, int y, char far *s)` [O, verified §16]

State: `x` (bytes), `y`, margin = x, line count = 1, current colour = DS:9215 (the **ink**,
read at entry), highlight flag = 0. For each byte `c` until NUL:

| `c` | Action |
|---|---|
| 0x0D or 0x0A | x = margin; y += DS:5B3C (line height, normally 12; 10 while the title runs and while a help page is drawn); lines++ |
| `~` (0x7E) | next 6 bytes are digits: x = (100·d1 + 10·d2 + d3) / 2 (signed division), margin = x, y = 100·d4 + 10·d5 + d6; skip them. Digits are used as `byte − 0x30`, no validation |
| `` ` `` (0x60) | toggle highlight: on → colour = 0x4444 (red); off → colour = DS:9215 (re-read) |
| `\|` (0x7C) | colour = n·0x11 in both bytes with n = 10·d1 + d2 (8-bit arithmetic: byte = (n<<4) OR n); skip 2 bytes. A later `` ` `` off restores DS:9215, not this colour |
| 0x09 | x += 10·d1 + d2; skip 2 bytes |
| other | if x < 0x9A (signed): draw glyph `c & 0x7F` at (x, y) in the current colour, x += its advance; else skip the glyph without advancing (so the rest of the line is clipped) |

Returns the line count. Codes 0x01..0x1F that are not listed are glyphs (key caps, crown, menu
labels). A glyph that starts at x ≤ 0x99 is drawn whole even if it extends past byte 0x9A.

### 3.2 `Text_Width` (43cb:0cfc) `int Text_Width(char far *s)` [O]

Returns 0 for a NULL pointer; otherwise the sum of `Font_DrawChar(measure)` advances of every
byte `& 0x7F` up to NUL or CR (not LF), **including control bytes** (a back-quote counts as its
30-px glyph, `~` as 10 px). Only plain strings are measured by the game, so this does not show.

### 3.3 Inks used

Black 0xFFFF (boxes, menus, help, rank screen), white 0xCCCC (selected button, open menu title,
HUD numbers and clue words), blue 0x0000 (title and story pages, sign-in heading), red 0x4444
(remove-mode heading, quiz praise word, highlighted words), light magenta 0x6666 (CHAMPION),
light grey 0xDDDD (disabled quiz answer). `MenuBar_DrawTitle` and `Button_Draw` change DS:9215 as
described below; callers set it before drawing.

---

## 4. Message boxes and buttons

### 4.1 Box table DS:59CC (23 records of 16 bytes) [O]

`{u8 type; u8 pad; i16 x1; i16 y1; u8 default_button; u8 pad; char far *title; char far *text}`
(x1/y1 = −1 → centred). Text = CR-separated lines. The 23 boxes:

| # | Type | Role (title / first words) | Opened by |
|---|---|---|---|
| 0, 1, 2 | 0 | drop-downs Help (DS:5262, 4 items), File (DS:52a7, 3), Options (DS:52d1, 3); x1 = 0, 26, 52; y1 = 13 | `Menu_Update` |
| 3 | 1 | "Start Over" confirmation | dialog 8 |
| 4 | 1 | "Start New Player" confirmation | dialog 9 |
| 5 | 1 | "Exit and Save" confirmation | dialog 10 |
| 6 | 1 | "New Player Hints" Yes/No (default = DS:5a32) | dialog 11 |
| 7 | 1 | "Music" Yes/No (default = DS:5a42) | dialog 12 |
| 8 | 1 | "Sound" Yes/No (default = DS:5a52) | dialog 13 |
| 9 | 1 | exit question at the sign-in (no title) | sign-in Esc (dialog 14) |
| 10 | 1 | "Remove Player Name" confirmation | remove mode Enter (dialog 15) |
| 11 | 1 | saved-game prompt (default Yes) | choosing a player with a saved game (dialog 16) |
| 12 | 3 | lose-your-saved-game warning | **never drawn** (no caller) [O] |
| 13 | 3 | sign-in list is full | dialog 18 |
| 14 | 6 | name already on the list | dialog 17 |
| 15 | 6 | "Enter New Player Name" | name entry |
| 16, 17, 21 | 4 | insert disk 1 / 2 / program disk | disk.c (153e) |
| 18 | 4 | "Disk Write Protected" | disk.c |
| 19, 20 | 7 | disk problem messages | disk.c |
| 22 | 7 | "ONE MOMENT PLEASE" | `Scene_Update` 1437:009b, `Quiz_LoadQuestions` 46ce:09ba, `SignIn_Update` 47c7:0036, only when the boot drive DS:9136 < 2 (A: or B:) |

The record's default byte for boxes 6/7/8 is rewritten with the current setting (DS:5a32 =
hints, DS:5a42 = music, DS:5a52 = sound; by `Game_Start` and the option commands), so these
boxes open with the current value selected [O].

### 4.2 `MsgBox_Draw` (43cb:0592) `void MsgBox_Draw(int n)` [O, verified §16]

```
ink (DS:9215) = 0xFFFF
w = Text_Width(title) + 0x18                   # DS:931e, box width in bytes
for each line of text (split at NUL, CR or LF):
    lw = sum of advances of every byte of the line (control bytes included) + 0x0F
    w = max(w, lw)
    # lines are counted by their terminators (CR, LF and the final NUL): "a" CR LF is 3 lines,
    # "a" is 1, an empty text 0 [O wave 2]
w = max(w, 0x5C if type == 3 else 0x4B)
h = lines*12 + 0x0F                            # lines as counted above
if title: h += 0x0E
if type in (1, 3): h += 0x0C
if type != 7: h += 0x13
if x1 < 0: x1 = (0xA0 - w) / 2                 # signed division
if y1 < 0: y1 = (200 - h) / 2
x2 = x1 + w; y2 = y1 + h
if slow CPU: save the background (x1, y1-2, w+1 bytes wide, h+3 rows) with Sprite_SaveUnder
             1000:2056 (AX = 1); DS:37c8 = DS:37cc = returned handle; DS:0102 = 1
Box_DrawFrame(type, x1, y1, x2, y2, title)
if type == 1:
    DS:a1fd (selected button) = default_button
    button 1 "Yes" (DS:5d03) at x = x2 - 0x3A, button 0 "No" (DS:5d00) at x = x2 - 0x1C,
    both y = y2 - 0x23 (DS:9316/9318 = x of 0/1, DS:931a/931c = y, DS:930e/9312 = label pointers)
    Button_Draw(0); Button_Draw(1)
Box_SetupInput(type, x1, x2, y2)               # §4.5
ty = y1 + 9 (+0x0E with a title); DS:a1ff = ty
DS:a201 = Text_Draw(x1 + 7, ty, text)          # line count = item count of a drop-down
if slow CPU and not (g_uiMode & 0x40): Present_Dirty()
```

Example geometries (all verified): Help drop-down (0, 13)-(83, 95); File (26, 13)-(104, 83);
Music box (42, 58)-(117, 142); Exit-and-Save box (26, 16)-(134, 184); name entry (25, 40)-(134,
160).

### 4.3 `Box_DrawFrame` (43cb:0848) `(type, x1, y1, x2, y2, title)` [O, verified]

1. fill (x1, y1−2, x2, y2) white 0xCC (note: starts 2 rows above y1);
2. `Gfx_FrameRect(x1+1, y1, x2−2, y2−4, 0xFF)`;
3. shadow: fill (x2−1, y1+2, x2−1, y2−2) and (x1+2, y2−3, x2−2, y2−2) light grey 0xDD;
4. title (if not NULL): `Gfx_HLine(x1+1, y1+0x0F, (x2−x1)−2, 0xFF)`; `Text_Draw(x2 − (x2−x1)/2 −
   Text_Width(title)/2, y1+4, title)` (signed divisions); type 5 only (unused by the table) adds
   `Gfx_VLine(x1+0x0B, y1+1, 0x0F, 0xFF)`, `Gfx_VLine(x2−0x0C, y1+1, 0x0F, 0xFF)` and glyph strings
   DS:5d07 at (x1+3, y1+4) and DS:5d09 at (x2−10, y1+4);
5. footer by type (switch table CS:09DF): types 0, 1, 5, 6: centre text DS:5256 ("Esc to Go Back"
   with the Esc key glyph), line offset 0x12; type 3: centre DS:5256 and left text DS:525a
   ("Press Enter to go on"), offset 0x12; type 2 (help): no text, offset 0x15; type 4: centre
   DS:525e ("Enter to Go On"), offset 0x12; type 7: nothing;
6. if offset ≠ 0: `Gfx_HLine(x1+1, y2−offset, (x2−x1)−2, 0xFF)`; `Text_Draw(x1+7, y2−0x20, left)`;
   `Text_Draw(x2 − (x2−x1)/2 − Text_Width(centre)/2, y2−0x10, centre)` (empty strings draw nothing).

The text colour is whatever DS:9215 holds (black from `MsgBox_Draw` / `Help_Open`).

### 4.4 Buttons

`Button_Draw(i)` (43cb:0d50) [O, verified]: label = DS:930e/9312[i], x = DS:9316[i], y =
DS:931a[i]; tw = `Text_Width(label)`; bw = 0x14 if tw < 0x14 else 0x33; xr = x + bw. Colours: face
0xCC, left inner edge 0xFC, right inner edge 0xCF, ink unchanged; if `i == DS:a1fd` (selected):
face and both edges 0xFF and ink 0xCCCC (white text on black). Drawing: fill (x−2, y+2, xr+2,
y+0x0C) 0xFF; `VLine(x−3, y+3, 9, 0xCF)`; `VLine(xr+3, y+3, 9, 0xFC)`; fill (x−1, y+3, xr+1, y+0x0B)
face; `VLine(x−2, y+3, 9, left)`; `VLine(xr+2, y+3, 9, right)`; `Text_Draw(x + (bw−tw)/2, y+3,
label)`; restore the ink. The 0xCF/0xFC bytes round the corners by one pixel.

`Button_Flash()` (43cb:0eb6) [O]: `s = DS:a1fd`; 4 times: `DS:a1fd ^= 1; Button_Draw(s);
Present_Dirty(); Delay_Ticks(18)`. The chosen button blinks unselected/selected twice and ends
selected; total 72 ticks = 0.304 s, blocking.

`Delay_Ticks(u32 n)` (43cb:0ee8) [O]: `t = g_ticks + n; while g_ticks < t: wait` (32-bit
compare of the 236.7 Hz counter).

### 4.5 `Box_SetupInput` (43cb:12cf) `(type, x1, x2, y2)` [O]

Selects the context and rewrites its zone (`z`) and key (`k`) lists:

* ctx = 1 for type 0 (drop-down), 4 for type 6 (name entry), else 2.
* Zone 0 (footer, release → Esc): x1' = x1 + DS:5c89[type] (bytes {1, 1, 0xA0, 1, 1, 0xA0, 1,
  0xA0}: types 2, 5, 7 disabled), y1' = y2−0x11, x2' = x2−2, y2' = y2−4; its release command =
  0, or 8 for type 4.
* type 0: zone count = DS:5c83[menu] (13, 12, 12); zones 9..16: x1 = x1, x2 = x2 (their y ranges
  20+12k..31+12k and commands press/held 9+k, release 8 are static).
* types 1 and 2: zone count 11; zone 8 disabled; zone 9 = button 0 (DS:9316−2, DS:931a+3,
  DS:5c86[type]+DS:9316, DS:931a+0x0D), zone 10 = button 1 (same with 9318/931c), DS:5c86 =
  {0, 0x16, 0x36}; key entries 5, 6, 7 = Tab, ↑, ↓. Type 1: key count 11, entries 2/3 = ←/→,
  8/9 = Y/N. Type 2 (help): key count 9, entry 8 = PgDn (→ command 14), entry 9 = PgUp (→ 15);
  if both buttons are at the same x (help page 0): entries 2, 3, 5, 6, 7, 9 = 0xFFFF (only Esc,
  Enter, PgDn left).
* type 3: zone count 9; zone 8 = (x1+0x1E, y2−0x1F, x1+0x32, y2−0x16) (the "Press Enter"
  line; release → 8).
* type 6: zone count 9; zone 8 disabled if y2 < 0x82, else (x1+0x40, y2−0x48, x1+0x54, y2−0x3F).
* types 4, 7: zone 0 only.
* Finally `g_cmd = −1`, DS:90f6 = −1.

---

## 5. Menu bar, drop-downs and dialogs

### 5.1 `MenuBar_DrawTitle(int i, char on)` (43cb:0f1d) [O, verified]

x0 = DS:5bc7[i] (0, 26, 52), x1 = DS:5bcd[i] (25, 51, 84); rows 0..10.
* off: fill (x0, 0, x1, 10) 0xFF; fill (x0, 1, x1−1, 10) 0xDD; fill (x0+1, 0, x1, 9) 0xCC; ink =
  0xFFFF; k = 0;
* on: fill (x0, 0, x1, 10) 0xFF; if i < 2: fill (x1, 1, x1, 10) 0xFD; fill (x0, 1, x1−1, 10)
  0x33 (light red); ink = 0xCCCC; k = 3.

Then glyph DS:5bd3[i+k] at (x0, 0) and glyph DS:5bdf[i+k] at (x0+0x12, 0) in the ink (pre-drawn
name and key-cap glyphs 14..25). DS:9215 keeps the ink afterwards.

### 5.2 `Menu_Update` (43cb:0001), per frame [O]

Runs only if `(g_uiMode & 0x80) == 0`, `(g_uiMode & 0x60) != 0`, DS:926c = 0 and a command is
waiting (`DS:930c = g_cmd` read with interrupts off, ≠ −1). DS:90fc = open menu (−1 none), DS:9111
= highlighted item.

```
if g_uiMode & 0x40:                                   # menu bar
    if cmd in (8, 0):
        if cmd == 8: DS:9323 = DS:5b69[menu] + item; g_uiMode |= 0x20; DS:922e = 2
        else:        g_inputContext = 0; DS:922e = 1
        Screen_DrawOverlay()                          # full redraw removes the drop-down
        g_uiMode &= ~0x40; DS:90f6 = -1; g_keys &= 0xFFF0
    else:
        old = menu
        t = cmd - 1 if cmd < 4 else (i8)DS:5b40[cmd] + menu   # <- -1, -> +1, others 0
        t = (i8)DS:5b63[t]                            # wrap: [-2]=0 [-1]=2 [0]=0 [1]=1 [2]=2 [3]=0
        menu = t
        if t != old:
            if old != -1 or not slowCpu:
                DS:922e = 2; Screen_DrawOverlay()
                if old != -1: MenuBar_DrawTitle(old, 0)
            MenuBar_DrawTitle(t, 1); MsgBox_Draw(t); item = 0; Menu_HighlightItem()
        Menu_MoveSelection()
    DS:930c = -1
Dialog_Update()
g_cmd = DS:930c; DS:9238 = 1
if g_uiMode & 2: DS:90f8 = 2 elif g_uiMode & 0x40: DS:90f8 = 0   # HUD prompt icon (§11.3)
```

`Menu_MoveSelection` (43cb:014a): `n = (i8)DS:5b51[cmd] + item` if cmd < 9 (↑ −1, ↓ +1, others
0), else `n = cmd − 9` (mouse over item n); `n == −1 → DS:a201−1`, `n == DS:a201 → 0`; if n ≠ item:
`Menu_HighlightItem(); item = n; Menu_HighlightItem()`; `g_dirty = 1` [O].

`Menu_HighlightItem` (43cb:04ff) [O, verified]: rectangle x1 = box.x1+2, y1 = DS:a1ff +
12·item − 1, x2 = box.x1 + DS:931e − 3, y2 = y1 + 12; swap colours 12→3, 15→12, 3→15 (white and
black exchanged); slow CPU: `Present_Dirty()`. Calling it twice restores the row.

Menu → dialog ids (DS:5b69 = {0, 8, 11} + item): Help items → 0..3, File → 8, 9, 10, Options →
11, 12, 13. Dialog → box (DS:5ba7, i16[14]): 0..7 → −1 (help pager, topic = dialog id), 8 → 3, 9 →
4, 10 → 5, 11 → 6, 12 → 7, 13 → 8. Sign-in dialogs 14..18 are opened directly (§9).

Handlers per dialog id [O]: confirm (Enter) DS:62b7[19] far pointers, cancel (Esc) DS:6303[19]:

| Id | Confirm handler | Cancel handler |
|---|---|---|
| 0..7 | `MenuCmd_HelpPage` 43cb:021d | `Dialog_CloseRedraw` 47c7:13d7 |
| 8 / 9 / 10 | `MenuCmd_StartOver` 028b / `MenuCmd_StartNewPlayer` 02f8 / `MenuCmd_ExitAndSave` 032c | `Dialog_CloseRedraw` |
| 11 / 12 / 13 | `MenuCmd_NewPlayerHints` 034f / `MenuCmd_Music` 0371 / `MenuCmd_Sound` 039d | `Dialog_CloseRedraw` |
| 14 | `SignInDlg_ExitYes` 47c7:13dd | `SignInDlg_Back` 13e9 |
| 15 | `SignInDlg_RemoveYes` 13fa | `SignInDlg_Back` |
| 16 | `SignInDlg_ContinueAnswer` 1414 | `SignInDlg_Back` |
| 17 | `SignInDlg_Reset` 1436 | `SignInDlg_Reset` |
| 18 | `SignInDlg_ListFull` 1442 | `SignInDlg_Back` |

DS:5b6f (`g_menuHandlers`, 14 far pointers) is a copy of entries 0..13 that no code reads [O].
Confirm handlers run for Enter on **either** button; each checks DS:a1fd (1 = Yes) itself.

### 5.3 `Dialog_Update` (43cb:019a), called by `Menu_Update` [O]

```
if g_uiMode & 0x20:
    b = DS:5ba7[DS:9323]
    if DS:5b3e == 0:                                  # not yet drawn
        Help_Open(DS:9323) if b == -1 else MsgBox_Draw(b)
        DS:5b3e = 1; g_dirty = 1
    else:
        if cmd in (8, 0):
            DS:5b3e = 0; g_inputContext = 0
            if b != -1 or cmd == 0: g_uiMode &= ~0x20; DS:922e = 1   # help stays open on Enter
            g_keys &= 0xFFF0
        Dialog_HandleButtons()
    DS:930c = -1
```

`Dialog_HandleButtons` (47c7:1454) [O]: Enter (8): `Button_Flash()`, then confirm handler
DS:62b7[DS:9323]; Esc (0): cancel handler DS:6303[DS:9323]; commands 4..15 only: 4/5 → DS:a1fd ^=
1; 9/10 → DS:a1fd = cmd−9; 15 → DS:a1fd = 0 and DS:90f6 = 8; 14 → DS:a1fd = 1 and DS:90f6 = 8; 6,
7, 11, 12, 13 change nothing; then (for every command 4..15) `Button_Draw(0); Button_Draw(1);
g_dirty = 1`. So Y/N select and confirm at once (the queued Enter arrives on the next tick).

`Dialog_HandleOk` (47c7:1502): Enter → confirm handler, Esc → cancel handler (no buttons, no
flash).

### 5.4 Menu commands (43cb, confirm handlers) [O]

* `MenuCmd_StartOver` (028b), if Yes: if `g_area` = 0x0D (castle) restore the CPU flags
  (`g_slowCpu = g_slowScroll = DS:8fa5`); `UI_ForceRedraw()`; DS:922e = 1; record +9 = 0;
  `Users_Pack(); Users_WriteFile(); Users_LoadPlayer(); Game_Start(); Scene_Update();
  Anim_InitSlots()`; clear DS:9201..9204 and DS:8f9f..8fa2 (bytes). The abandoned game's coins
  and nets are lost: `Users_LoadPlayer` reloads the banked values (+0x15C/+0x15A).
* `MenuCmd_StartNewPlayer` (02f8), if Yes: area 0x0D → `g_slowCpu = DS:8fa5`; record +9 =
  `g_gameInProgress` (DS:9205, byte); `Users_Pack(); Users_WriteFile()`; `g_uiMode = 8`;
  `Anim_InitSlots()`.
* `MenuCmd_ExitAndSave` (032c): if Yes: record +9 = `g_gameInProgress`; `Users_Pack();
  Users_WriteFile()`. Always `g_quit = DS:a1fd` (the main loop ends after this frame and
  `Game_Shutdown` stops sound, restores the timer and keyboard and returns to DOS).
* `MenuCmd_NewPlayerHints` (034f): DS:913f = `g_hintsOn` (DS:9224) = DS:5a32 = DS:a1fd; all 15
  hint flags (DS:923e) = 1 (re-armed, also for "No").
* `MenuCmd_Music` (0371): old = `g_musicOn`; `g_musicOn` = DS:5a42 = DS:a1fd; if Yes also
  `g_soundOn` = DS:5a52 = 1; if old ≠ new: DS:5bc5 = 1.
* `MenuCmd_Sound` (039d): `g_musicOn = g_soundOn` = DS:5a42 = DS:5a52 = DS:a1fd; if either
  changed: DS:5bc5 = 1.

`g_gameInProgress` is 0 from `Game_Start` until the start-of-game message is closed (49a6:03fe
sets 1), so leaving before that keeps no saved game.

---

## 6. Help pager and sound control

### 6.1 Help (43cb:107a, 1273, 10c4, 021d) [O, verified pages 1-10]

`Help_LoadFile` (107a), at start-up: once (flag DS:5beb), `File_OpenByIndex(5, 0x8001)`, read
0xED8 bytes of HELP.DAT into `g_helpData` (DS:9325), close; returns 0 if the open fails.
HELP.DAT = 4 page counts (6, 4, 4, 1), 15 u16 absolute page offsets, NUL-terminated pages
(`fmt_data.md` §3).

`Help_Open(topic)` (1273): DS:9322 = page count = `g_helpData[topic]`; DS:9301 (page, i8) = 0;
ink black; `Box_DrawFrame(2, 8, 0x13, 0x96, 0xBD, title DS:52ee[topic])`; DS:a1fd = 1;
`Help_DrawPage(topic, 0, g_helpData)`.

`Help_DrawPage(topic, page, data)` (10c4):
1. ink black; fill (10, 0x23, 0x93, 0xA7) and (10, 0xAA, 0x93, 0xB7) white;
2. idx = sum of the page counts of earlier topics + page; text = data + u16 at data[4 + 2·idx];
   DS:5b3c = 10; `Text_Draw(0x0E, 0x25, text)`; DS:5b3c = 12;
3. buttons at y 0xA9: button 1 at x 0x5C labelled "Done" (DS:5d0b) on the topic's last page,
   else "See Next Page" (DS:5d16); `Button_Draw(1)`; if page ≠ 0: button 0 = "Go Back a Page"
   (DS:5d24) at x 0x10, `Button_Draw(0)`; on page 0 button 0 is set equal to button 1 (same x and
   label) and not drawn;
4. `Box_SetupInput(2, 8, 0x96, 0xBD)`;
5. pictures: p1 = DS:5bed[idx], p2 = DS:5c0b[idx] (frame·2, −1 = none), x1 = DS:5c29[idx], x2 =
   DS:5c47[idx], y = DS:5c65[idx] (all i16[15]); p1 drawn at (x1, y), p2 at (x2, y), flags 0x40
   (masked, hotspot applied), from the **currently loaded sprite bank**. Pages with pictures:
   0 (SPRITES 218 and 129), 1 (94, 103), 3 (123, 128), 10 (72 = Flutter at x −115, y −62, an
   absolute-position frame).

`MenuCmd_HelpPage` (021d), confirm handler of dialogs 0..7: page += (i8)DS:5bc3[DS:a1fd] (−1 for
"Go Back", +1 for the other button). If page == page count: close: `g_inputContext = 1`,
`g_uiMode = (g_uiMode & ~0x20) | 0x40`, DS:90f6 = 1 (queued F1: the Help drop-down reopens with
item 0), DS:90fc = −1, slow CPU: DS:922e = 1. Else: if page == 0, DS:a1fd = 1; DS:5b3e = 1;
`Help_DrawPage(topic, page)`; `g_dirty = 1`. The selected button persists between pages. Esc
closes help completely (Dialog_Update, cancel handler).

Keys in help (ctx 2, §4.5): Enter = activate the selected button (with flash), ←/→/↑/↓/Tab =
toggle (from the second page on), PgDn = select "next" + Enter, PgUp = select "back" + Enter
(from the second page on), Esc = close.

Quirk: help can be opened in scenes 3/4 only if the menu is reachable there; the pictures would
then come from CLOSE.PAK/SHACK.PAK (frames ≥ 51 differ) [I, not observed].

### 6.2 `Game_HandleHotkeys` (43cb:03cb), per frame and in the title loop [O]

Only if `g_uiMode` = 0 or `g_area` = 6 (title):
* command 14 (Ctrl+S): v = `g_musicOn ^ 1`; DS:5a42 = DS:5a52 = `g_soundOn` = `g_musicOn` = v;
  v = 0: `Music_Stop(); Sfx_Stop(); Sound_SilenceAll()`; v = 1: `Music_Play(tune(g_area))`;
  `g_cmd = −1`; DS:9238 = 1.
* command 15 and `g_slowCpu` ≠ 2: `g_slowCpu ^= 1` (no key produces 15 in ctx 0/5: dead) [O].

tune(area) = 0 if area < 4, 1 if area < 7, 2 if area < 15, none otherwise [O, same rule in
`Sound_SyncState` and `Game_Start`].

### 6.3 `Sound_SyncState` (43cb:046e), per frame [O]

```
if DS:9300 != g_uiMode:
    DS:9300 = g_uiMode
    if g_uiMode == 0: DS:37ce = 0xFFFF            # music runs (the ISR ANDs the music status with it)
    else: DS:37ce = 0; Sfx_Stop(); Sound_SilenceAll()   # music paused, effect cut
if DS:5bc5 == 1:
    if g_musicOn == 0: Music_Stop(); Sfx_Stop(); Sound_SilenceAll()
    else: Music_Play(tune(g_area))
    DS:5bc5 = 0
```

Effects started while a UI mode is active (quiz praise 10/11, chest steps 3) still play; only
the music is paused (`fmt_sound.md` §7).

---

## 7. Keys and clicks per screen (summary of §2-§10)

| Screen (ctx) | Enter / click release | Esc | Arrows | Other |
|---|---|---|---|---|
| play (0) | – | Exit-and-Save box (via the File menu shortcut, §2.3) | movement (held bits of the keyboard ISR, not commands) | F1/F2/F3 or click on a title: open that menu; Ctrl+S: sound and music on/off; click on the play-field: walk, on the status bar: drop a coin |
| menu bar (1) | choose the highlighted item (opens its box or help) | close the bar | ←/→ other menu (wrap), ↑/↓ item (wrap) | F1-F3 / title click: that menu; mouse over an item highlights it, release chooses |
| Yes/No box (2) | run the box's command with the selected button (after a 0.3 s flash) | close, no effect | any arrow or Tab toggles Yes/No | Y / N: select and confirm; click on a button selects, release confirms |
| help (2) | selected button: next page / previous page / Done (Done reopens the Help menu) | close help (back to play) | toggle buttons (from the second page on) | PgDn = next, PgUp = back (from the second page on) |
| title pages (5) | next page (story 2: walk-off and sign-in) | quit to DOS (Super Solvers page: back to the title) | only restart the 30 s wait | S on story 2: Super Solvers page; Ctrl+S: sound on/off |
| sign-in list (4) | choose (new player / saved game question / start) | exit box | ↑/↓ row (wrap, "New Player" included) | letter or digit: start a new name with it; Ctrl+R: remove mode |
| name entry (4) | accept (unless empty or already used) | back to the list (exit box if no player exists) | – | letters, digits, space (not first), Backspace; 8 characters max |
| remove mode (4) | confirm box for the highlighted player | back to the list | ↑/↓ (players only) | – |
| elf question (3) | answer; after an answer: close | close (question kept by the elf if unanswered) | ↑/↓ focus (skips greyed answers) | F1-F3 open the menu bar over it; click on an answer focuses, release answers; after answering any click closes |

---

## 8. Title and attract sequence (4ba4)

### 8.1 `Title_Run` (4ba4:0434), called once by `main` before the frame loop [O]

```
save DS:5b3c; DS:5b3c = 10; g_uiMode = 0x80; g_area = 6
g_quit = 1; Scene_LoadPan(14) (OPENING.PAN); g_quit = 0
g_titlePhase (DS:8a16) = 0; DS:63ba (music loop flag) = 1; Music_Play(1)
while DS:8d83[g_titlePhase] != 0:                     # near pointers, segment 4ba4
    g_dirty = 0
    Game_HandleHotkeys()
    call DS:8d83[g_titlePhase]
    if DS:900c == 0 and DS:8a14 == 0:                 # sprite slot 2 idle, not on the SS page
        DS:9008 = -1; DS:900e = 0; Anim_Clear(2)
        r = rand()
        if (r & 0x7F) == 0:                           # 1 in 128 per frame
            r = rand(); k = r % 12
            DS:9002 = DS:8a18[k]; DS:9004 = DS:8a30[k]   # x bytes / y of one of 12 sparkle spots
            Anim_Set(2, 0x5C)                         # sparkle script in slot 2
    Sprites_Snapshot(0, 3); g_stepDone = 0; Sprites_DrawSorted(0, 3)
    Frame_Present(); Sprites_EraseAll(0, 3)
if slowCpu and video type is Tandy (3) or CGA (2): g_musicOn = 0; Music_Stop(); Sfx_Stop()
restore DS:5b3c; g_quit = 1, then 0; g_uiMode = 0; DS:63ba = 0; Anim_InitSlots()
```

The title music does **not** continue into the sign-in: `g_lastUiMode` DS:9300 is BSS (0) and is
never written during the title, so on the first main-loop frame (`g_uiMode` = 8) `Sound_SyncState`
sees a change and pauses the music (DS:37ce = 0, `Sfx_Stop`, `Sound_SilenceAll`); the audio of
`avi/ega_signin_idle.avi` is all zero [O wave 2]. `Game_Tick` keeps running
in the background; with `g_uiMode` = 0x80 it also runs the player/elf/cell updates (130f:0173),
which the walk-off animation uses [O; behaviour of those updates: player spec].

### 8.2 Phases (DS:8d83) [O, verified title, story 1, story 2, SS page pixel-exact]

| Phase | Function | Action |
|---|---|---|
| 0 | `TitleStep_DrawTitle` 0045 | OPENING.PAN full screen (40 `Tile_DrawColumn`, §2.5); slot 1 x/y/frame fields DS:8fec/8fee/8ff0 = 0; `Anim_Set(1, 0x5B)` (title figure); ink 0 (blue); `Text_Draw(0x11, 0xA0, DS:8a48)` ("Press Enter to go on" line); `g_inputContext = 5`; DS:80da = 8; phase++; `g_dirty++` |
| 1, 3, 5, 11 | `TitleStep_Wait` 00b2 | see 8.3 |
| 2 | `TitleStep_Story1` 017f | `Title_ClearBox()`; `Text_Draw(6, 0x20, DS:8a5a)` (story 1, LF and TAB markup); `Title_DrawSprite(0x0C, 0x48, frame 32, 0x40)`; `Text_Draw(0x11, 0xA0, DS:8a48)`; phase++; `g_dirty++` |
| 4 | `TitleStep_Story2` 01cc | `Title_ClearBox()`; `Text_Draw(6, 0x20, DS:8b54)`; `Text_Draw(10, 0xBD, DS:8c49)` (the S-key line for the series page); `Title_DrawSprite(8, 0x55, frame 33, 0x40)`; `Text_Draw(0x11, 0xA0, DS:8a48)`; phase++; `g_dirty++`; DS:80da = 13 (bottom-line click = S) |
| 6 | `TitleStep_StartWalkoff` 0233 | OPENING.PAN redrawn (removes the box and texts); `Sprites_EraseAll(1, 1)`; `Anim_Set(1, 0x5A)` (walk-off); phase++ |
| 7 | `TitleStep_WaitWalkoff` 027a | if slot-1 script id DS:8ff6 = 0 (script ended) or `g_cmd` = 8: phase++, DS:8ff2 = −1, DS:8ff8 = 0, `Anim_Clear(1)` (Enter does not consume the command) |
| 8 | `TitleStep_WaitActorGone` 02a3 | if actor-table frame DS:9159 = −1: phase = 9 |
| 9 | 0 | end of the title, `main` enters the sign-in |
| 10 | `Title_SuperSolversPage` 02b1 | 8.4 |
| 12 | `Title_ResetPhase` 042b | phase = 0, DS:8a14 = 0 (back to the title page) |
| 13 | 0 | (not reached) |

`Title_ClearBox` (000f): slow CPU: `Dirty_Clear()`; fill (5, 0x1E, 0x6F, 0xAA) 0xAA (light cyan);
slow CPU: `Present_Dirty()`, else `g_dirty++`. `Title_DrawSprite(x, y, frame2, flags)` (0156,
near): `Sprite_Draw` of BEGIN.PAK frame frame2/2 at (x, y) with the flags; `g_dirty++`. Story
texts use the ink left by phase 0 (blue).

### 8.3 `TitleStep_Wait` (4ba4:00b2) [O]

```
if timer (DS:cb7e, u32) == 0: timer = g_ticks + 0x1C20        # 7200 ticks = 30.42 s
if g_ticks > timer: g_cmd = 13 if g_titlePhase == 5 else 8    # time-out
if g_cmd >= 0:                                                # any command, also F1, arrows...
    timer = 0
    if g_cmd == 0:                                            # Esc
        if DS:8a14 == 0: Game_Shutdown()                      # quit to DOS from the title pages
        else: DS:8a14 = 0; g_titlePhase = 0                   # Esc on the SS page: title
    elif g_cmd == 8: g_titlePhase += 1
    elif g_cmd == 13 and g_titlePhase == 5: g_titlePhase = 10 # S on story 2 (or time-out)
    g_cmd = -1; DS:9238 = 1
```

Sequence: title (phase 1) → Enter/time-out → story 1 (3) → story 2 (5) → Enter = walk-off and
sign-in; time-out or S = Super Solvers page (11) → any of Enter/time-out = phase 12 → title.
Any other command (F1-F3, arrows) restarts the 30.42 s wait without changing the page; mouse
releases give 8 or 13 (§2.3). Ctrl+S is consumed by `Game_HandleHotkeys` (6.2) before the phase
runs, so it toggles the sound without touching the wait. Matches the
measured 1826 frames at 59.92 Hz (30.47 s) per page [O REFERENCE_CAPTURES §5.1]. The open
point in REFERENCE_CAPTURES §5.1 (walk-off after Enter on story 1) is answered by the code: Enter
on story 1 shows story 2; the walk-off needs Enter on story 2 [O].

### 8.4 `Title_SuperSolversPage` (4ba4:02b1) [O, verified]

Save DS:5b3c; DS:9008 = −1, DS:900e = 0, `Anim_Clear(2)`; DS:5b3c = 12; DS:8a14 = 1;
`Sprites_EraseAll(1, 1)`; DS:8ff2 = −1, DS:8ff8 = 0; fill (0, 0, 0x9F, 0xC7) 0x44; fill (1, 0x2E,
0x9E, 0xC5) 0xCC; fill (1, 2, 0x9E, 0x2D) 0x00; `Title_DrawSprite(0, 0, frame 36, 0)`; fill (0x4C,
0x1D, 0x9B, 0x29) 0x33; `Gfx_FrameRect(1, 2, 0x9E, 0xC5, 0xBB)`; with the current ink (blue):
`Text_Draw(3, 6, DS:8c6f)`, `Text_Draw(3, 0x31, DS:8cca)`, frame 35 at (5, 0x3E) flags 0,
`Text_Draw(0, 0x3E, DS:8d2b)`, frame 34 at (0x54, 0x78) flags 0, `Text_Draw(3, 0x82, DS:8cf0)`
(these strings use `|NN` colours and TABs); ink 0xFFFF; `Text_Draw(0x28, 0xB9, DS:8a48)`; phase++;
restore DS:5b3c; DS:80da = 8.

---

## 9. Sign-in, players and saved games (47c7)

### 9.1 State

| DS | Meaning |
|---|---|
| ba07 | sub-state: 1 list, 3 typing a new name (1 + 2), 5 remove mode, 9 exit box, 0x0D remove box, 0x11 saved-game box, 0x21 OK box (list full / name exists) |
| ba13 (i8) | highlighted row; c6c1[9] = record index of row k; c6cc = number of players; c6c0 = 1 if any player |
| ba0a[9] | new name buffer; c6bf (i8) = its length; ba06 = character of the current command; c6ca = caret drawn; ba09 = caret-blink armed |
| 6168 | 1 = (re)initialise the sign-in on the next frame (initial value 1); 6169 = 1 when USERS.DAT is in memory |
| c6cb | 1 = a player was chosen: start the game this frame |
| 9228 | `g_userRec`, far pointer to the selected 360-byte record in the USERS.DAT image DS:ba14 |

Handlers by sub-state: `SignIn_HandleInput` scans i = 13 down to 0 for DS:6186[i] == ba07 and
calls DS:616a[i] [O]. Keys DS:6186 = {1, 3, 5, 9, 0x0D, 0x11, 0x21}; handlers: 1
`SignInCmd_Select`, 3 `SignInCmd_NewName`, 5 `SignInCmd_Remove`, 9/0x0D/0x11
`Dialog_HandleButtons`, 0x21 `Dialog_HandleOk`. (Entries 7..13 of the scan overlap the zone table
at DS:6194; no sub-state value matches them.)

### 9.2 `SignIn_Update` (47c7:0007), per frame while `g_uiMode & 8` [O]

```
if DS:6168 == 0: ink = 0xFFFF; SignIn_HandleInput()
else:                                                   # (re)initialise
    if g_area not in (6, 4):                            # coming from a game
        g_quit = 1
        if boot drive DS:9136 < 2: MsgBox_Draw(22); Present_Dirty(); DS:0102 = 0;
                                    DS:37c8/37ca/37cc = 0; g_inputContext = 0
        Scene_LoadPan(14); Sprites_LoadBegin(); g_quit = 0; Screen_DrawBackdrop()
    Sfx_Stop(); g_area = 4; ba07 = 1; ink = 0xFFFF; DS:6168 = 0; c6cb = 0
    ba0a[0..8] = 0; c6bf = 0; c6c0 = Users_EnsureLoaded(); UI_ForceRedraw()
    DS:922e = 1; g_screen = 2
g_cmd = -1; DS:9238 = 1
```

`SignIn_HandleInput` (0f8b): DS:930c = `g_cmd`; DS:9238 = 0; if ba07 is 5 or 1: zone count
DS:6194 = c6cc + 9 + (ba07 ≠ 5); ba06 = low byte of `g_cmd`, and if ba06 > 0x60 and c6bf = 0:
ba06 &= 0xDF (**first letter upper-cased**; with no command (−1) ba06 becomes 0xFF, or 0xDF when
c6bf = 0, which every handler treats as non-printable); dispatch (9.1); if c6cb ≠ 0: `Users_LoadPlayer(); Game_Start();
DS:922e = 1`.

### 9.3 Drawing (painter 2) [O, verified: sign-in list 0 px (scoping), name entry 0 px]

`SignIn_Draw` (03cf), painter: only if DS:922e ≠ 0: `SignIn_DrawPanel()`; if no player (c6c0 =
0): ba06 = 0, `SignIn_DrawNameEntry()` (straight to name entry); else `Text_Draw(4, 0xBD,
DS:6380)` (key prompt line), ba13 = c6cc (the "New Player" row), or 0 in remove mode;
`SignIn_SetupZones(); Present_Dirty(); SignIn_MoveHighlight(); Present_Dirty()`; DS:922e = 0.

`SignIn_DrawPanel` (00e7): if not slow CPU `Screen_DrawBackdrop()`; fill (5, 0x1E, 0x6F, 0xAD)
0xAA; BEGIN frame 31 ("Club Sign-In") at (0, 0) flags 0x40; heading: remove mode → ink 0x4444,
string DS:6255[0] at x 0x16; else ink 0, DS:6255[1] at x 0x18; y 0x34; ink 0xFFFF; if c6c0:
`SignIn_DrawList()`; `g_dirty = 1`.

`SignIn_DrawList` (01e0): collect rows: for record i = 0..8 with a non-empty name (DS:6144[i]):
c6c1[n] = i, total[n] = record +0x0A (i16), n++ (c6cc = n). Shell sort, descending, exactly:

```
gap = n
while (gap = gap / 2) > 0:
    for i in gap .. n-1:
        j = i - gap
        while j >= 0 and total[j] < total[j + gap]:          # signed
            swap(total[j], total[j + gap]); swap(c6c1[j], c6c1[j + gap]); j -= gap
```

Rows at y = 0x41 + 11·k: name at (8, y), rank label DS:625d + 8·r at (0x3B, y) with r = number of
thresholds DS:629d[0..6] ≤ total (signed), total as decimal (`itoa`, signed) at (0x5E, y). Then,
unless remove mode, "New Player" (DS:6375) at (8, y of row n).

`SignIn_MoveHighlight` (071a): if c6c0: y = (u8)(0x41 + 11·ba13); swap colours 15↔10 over bytes
6..0x6E, rows y..y+9 (three swaps through colour 3, `SignIn_HighlightRow` 06d4); `g_dirty = 1`.
Called before and after every move (toggles).

`SignIn_SetupZones` (0831): copy DS:622d (8 bytes) → zone 0 (DS:6195) and DS:6235 (32 bytes) →
zones 8..11 (DS:61d5); zone count DS:6194 = (u8)(c6cc − (ba07 = 5) + 10); for rows 0..(that
count − 10): zone (9 + row) x1 = 6 (enables it; rows press/held 9+row, release 8); ctx 4;
`g_cmd = −1`; DS:90f6 = −1; DS:9238 = 1. Quirk [O]: the copied template gives rows 1 and 2 the y
ranges 0x4D..0x56 and 0x59..0x62 (12-px spacing) while the rows are drawn 11 px apart; the static
rows 3+ keep 0x62+11k.

`SignIn_DrawNameEntry` (0189): `SignIn_DrawPanel(); MsgBox_Draw(15)`; ba07 |= 2;
`Gfx_FrameRect(0x30, 0x6F, 0x70, 0x84, 0xFF)`; if ba06 ≠ 0: `SignIn_DrawTypedChar(0x35, 0x75, ba06)`;
c6ca = 1; `SignIn_DrawField(1)`.

`SignIn_DrawTypedChar(x, y, ch)` (05a9): `Font_DrawChar(ink, x, y, ch)` (x,
y as u8); `g_dirty = 1`. It draws a typed character, not a caret.

`SignIn_DrawField(on)` (05d4): w = (u8)`Text_Width(ba0a)`; x1 = (u8)(w + 0x36), x2 = x1 + 4; fill
(x1, 0x76, x2, 0x7E) with 0xFF if c6ca = 1 else 0xCC (the block cursor); if on: fill (x2+1,
0x7E, 0x6F, 0x7E) 0xCC; then for k = c6bf+1 .. 7: x1 = x2 + 2, x2 = x1 + 4, fill (x1, 0x7E, x2,
0x7E) 0xFF (one underscore per free character cell); `g_dirty = 1`.

### 9.4 List and remove mode handlers [O]

`SignInCmd_Select` (1011), ba07 = 1:

| Command | Action |
|---|---|
| 8 Enter | on the "New Player" row (ba13 = c6cc): c6cc < 9: ba06 = 0, `SignIn_DrawNameEntry()`; else list full: ba07 = 0x21, DS:9323 = 18, `MsgBox_Draw(13)`. On a player: `Users_SelectRecord()`; record +9 = 0 → c6cb = 1 (start); else ba07 = 0x11, `MsgBox_Draw(11)`, DS:9323 = 16. `g_dirty = 1` after each box |
| 0 Esc | ba07 = 9; `MsgBox_Draw(9)`; DS:9323 = 14 |
| 6 / 7 | `SignIn_MoveHighlight()`; ba13 −= / += 1; < 0 → c6cc; > c6cc → 0; `SignIn_MoveHighlight()` |
| 9..17 (mouse row) | highlight off; ba13 = (u8)(cmd − 9); highlight on |
| other, ba06 = 0x1E (Ctrl+R) | ba07 = 5 (remove mode); `UI_ForceRedraw()`; DS:922e = 1 |
| other, ba06 printable 0x21..0x7A | c6cc < 9: highlight → "New Player" row, ba0a[c6bf++] = ba06, `SignIn_DrawNameEntry()` (typing starts a new name); c6cc = 9: list-full box as above. Space is ignored |

`SignInCmd_Remove` (1328), ba07 = 5: Enter → ba07 = 0x0D, `MsgBox_Draw(10)` (default No),
DS:9323 = 15; Esc → ba07 = 1, `UI_ForceRedraw()`, DS:922e = 1; ↑/↓ with wrap over players only
(< 0 → c6cc−1, > c6cc−1 → 0); mouse rows as above; everything else ignored.

### 9.5 Typing a name: `SignInCmd_NewName` (1199), ba07 = 3 [O, verified]

| Command / ba06 | Action |
|---|---|
| 8 Enter, c6bf ≠ 0 | `UI_ForceRedraw()`; DS:922e = 1; `Users_NameExists()` false: c6cb = 1, `Users_AddPlayer()`; true: `SignIn_Draw()`, ba07 = 0x21, DS:9323 = 17, `MsgBox_Draw(14)`. Empty name: ignored |
| 0 Esc | no players: clear the name, c6bf = 0, `UI_ForceRedraw()`, `SignIn_DrawPanel()`, ba07 = 9, `MsgBox_Draw(9)`, DS:9323 = 14; else ba07 = 1 and DS:6168 = 1 (back to the list) |
| ba06 = 0x1F (Backspace), c6bf ≠ 0 | c6ca = 0; `SignIn_DrawField(0)` (cursor block white); c6bf−−; ba0a[c6bf] = 0; c6ca = 1; `SignIn_DrawField(1)`. The deleted glyph is only covered by the new cursor block (glyph column x−1 and row 0x75 are not cleared) [O, reproduce by drawing the same way] |
| other non-printable (also "no command") | caret blink: if ba09 ≠ 0 and DS:90fe ≤ 6: c6ca ^= 1, `SignIn_DrawField(0)`, ba09 = 0; elif DS:90fe > 6: ba09 = 1. One toggle per 13-tick cycle: the cursor blinks with period 26 ticks (0.55 s) [O; capture `tandy/19` shows the off phase] |
| printable 0x20..0x7A, c6bf < 8, not a leading space | w = (u8)`Text_Width(ba0a)`; c6ca = 0; `SignIn_DrawField(0)`; `SignIn_DrawTypedChar(w + 0x35, 0x75, ba06)`; ba0a[c6bf++] = ba06; c6ca = 1; `SignIn_DrawField(1)` |

Accepted characters: digits, letters, space (not leading), at most 8. Shifted keys never arrive
in DOSBox-X (REFERENCE_CAPTURES §5.1); the first letter is upper-cased by 9.2.

### 9.6 Sign-in dialog handlers (47c7:13d7..1442) [O]

* `Dialog_CloseRedraw` (13d7): `UI_ForceRedraw()`.
* `SignInDlg_ExitYes` (13dd): `g_quit = DS:a1fd`; `SignInDlg_Back()` (Yes quits to DOS after the
  frame).
* `SignInDlg_Back` (13e9): ba07 &= 7 (box state → 1, 3 or 5); `UI_ForceRedraw()`; DS:922e = 1.
* `SignInDlg_RemoveYes` (13fa): if Yes: `Users_ClearRecord()`, DS:6169 = 0 (re-read the file),
  DS:6168 = 1; `SignInDlg_Back()`. No: back to remove mode.
* `SignInDlg_ContinueAnswer` (1414): if No (DS:a1fd = 0): record +9 = 0 (the saved game is
  dropped silently; box 12 exists but is never shown); c6cb = 1 (start); `UI_ForceRedraw()`. Esc
  on this box = `SignInDlg_Back` (back to the list, nothing lost).
* `SignInDlg_Reset` (1436): ba07 = 1; DS:6168 = 1 (identical-name box, Enter or Esc).
* `SignInDlg_ListFull` (1442): ba07 = 5 (remove mode); `UI_ForceRedraw()`; DS:922e = 1. Esc =
  `SignInDlg_Back` (list).

### 9.7 USERS.DAT I/O [O]

* Image DS:ba14, 0xCAB = 3243 bytes = 3-byte header + 9 records of 0x168 bytes at offsets
  DS:6132[i] = 3 + 0x168·i; DS:6144[i] = far pointers to the names (DS:ba17 + 0x168·i).
* `Users_EnsureLoaded` (0452): if DS:6169 = 0: zero the image, `Users_ReadFile()`, DS:6169 = 1.
  Returns 1 if any record name is non-empty.
* `Users_ReadFile` (0514): `g_quit = 1` (ticks paused); `UI_ForceRedraw()`; `g_loadedScene++`;
  `File_OpenByIndex(9, 0x8004)`; read 0xCAB bytes; close; `g_loadedScene−−`; `g_quit = 0`.
* `Users_WriteFile` (055a): `g_quit = 1`; `UI_ForceRedraw()`; `g_loadedScene++`; repeat {open
  (9, 0x8104 = create, binary, read/write); write 0xCAB bytes; close} while the write result is 3
  (critical-error retry); `g_quit = 0`; `g_loadedScene−−`. Always the whole file.
* `Users_SelectRecord` (09dd): r = DS:6132[c6c1[ba13]]; `g_userRec` = DS:ba14 + r; field
  pointers (all DS:ba14 + r + offset): +0x0C history DS:8fd2, +0x66 slots DS:921a, +0x78 masks
  DS:9107, +0x81 clue words DS:91fd, +0x0A total DS:9138, +0x13F quiz tier DS:9101, +0x8A collected
  DS:927e, +0x130 found masks DS:9209, +0x133 shelf bits DS:8fb6.
* `Users_NameExists` (04a4): trailing blanks: for i = 8 down to 0 while ba0a[i] is 0 or space:
  ba0a[i] = 0 (modifies the buffer); then `stricmp` with every non-empty record name; true on a
  match (case-insensitive).
* `Users_AddPlayer` (07b9): first record i with an empty name; if i < 9: ba13 = c6cc, c6c1[c6cc]
  = i, c6cc++; `Users_SelectRecord(); Users_InitNewPlayer()`; copy 9 bytes ba0a → name;
  `Users_WriteFile()`.
* `Users_ClearRecord` (075d): record of row ba13 = 360 zero bytes, then its +0x78..+0x80 = 7;
  `Users_WriteFile()`.

Write points: new player, remove player, Start Over, Start New Player, Exit and Save, end of
every game (`Game_End`). No write at level changes or on the title [O, matches REFERENCE_CAPTURES
§4.5].

### 9.8 Record pack and restore, field by field (`Users_Pack` 0e0e, `Game_Restore` 0c62) [O]

| Record | Pack (game → record) | Restore (record → game) |
|---|---|---|
| +0x0BB+i, i = 0..47 | low byte of `g_cellPicture[g_hidingCells[i]]` (DS:0caa, DS:11aa) | u16 = byte (zero-extended) |
| +0x0F4+i, i = 0..47 | low byte of `g_cellContent[g_hidingCells[i]]` (DS:0faa) | same |
| +0x0EB+j (= +0x0BB+48+j), j = 0..8 | low byte of `g_cellPicture[g_variableCells[j]]` (DS:120a) | same |
| +0x124+2k, k = 5..0 | u16 DS:122e[DS:62b1[k]] (index bytes {1, 2, 5, 6, 9, 10}) | DS:122e[DS:62ab[k]] (same indices) |
| +0x140 | `g_tier` | `g_tier` |
| +0x142 | `g_level` | `g_level` |
| +0x144 / +0x146 | `g_coins` / `g_nets` | same |
| +0x148 | `g_treasuresFound` | same |
| +0x14A | `g_keyState` | same |
| +0x14C | DS:90f8 (HUD prompt icon) | same |
| +0x14E / +0x150 | player x / y (DS:37e2/37e4) | same |
| +0x152 / +0x154 | camera x / y (DS:37ea/37ec) | same |
| +0x156 | `g_area` | same |
| +0x158 | `g_shopCoinsOwed` | same |
| +0x15E / +0x160 | `g_musicOn` / `g_soundOn` | same (unconditionally) |
| +0x162 | `g_gamesPlayed` | same |
| +0x164 | `g_shownTreasure` | same |
| +0x166 | u16 = sign-extended `g_castleLayout` (DS:8f9e) | low byte only |
| header bytes 1-2 (u16 DS:ba15) | = DS:929f (word move 47c7:0f80) | DS:929f = header word (47c7:0de0) |

Fields used in place through the pointers of 9.7 (no copy): name, +0x09, +0x0A, +0x0C history,
+0x66 slots, +0x78 masks, +0x81 clue words, +0x8A collected, +0x130, +0x133, +0x13F. `Game_Restore`
then calls `Layout_MirrorSharedCells` (after the cell loops), and at the end: DS:8fa5 =
`g_slowCpu`; if `g_area` = 0x0D: `g_slowCpu` = 0; `g_slowScroll` = `g_slowCpu`;
`Castle_ApplyLayout()`; `Frame_Begin()`. Not saved: elf positions, the bonus-coin cell, hint
flags, RNG seed (ARCHITECTURE §6).

### 9.9 Player load, game start and end [O]

`Users_InitNewPlayer` (0a8b): `g_tier` = `g_level` = 0; record +0x15C = `g_coins` = 5; record
+0x15A = `g_nets` = 10; player (0x140, 0x70); camera (0xC0, 0); `g_gamesPlayed` = 0; player state
3; collected list (48 bytes) = 0x80; shelf bits (12) = 0; `g_shownTreasure` = 0xFFFF;
`Frame_Begin(); Users_Pack()` (fields not set here come from whatever the globals hold; harmless
because record +9 = 0 makes the next start a new game).

`Users_LoadPlayer` (0b19): `g_tier` = +0x140; `g_level` = 0; `g_coins` = +0x15C; `g_nets` =
+0x15A (banked values); `g_shownTreasure` = +0x164 (DS:9254 = its low byte); `g_gamesPlayed` =
+0x162; **only if `g_musicOn` ≠ 0**: `g_musicOn` = +0x15E, `g_soundOn` = +0x160 (once music is
off, later players' settings are not loaded for new games); player (0x140, 0x70), camera (0xC0,
0), state 3; `g_area` = 0; `g_loadedScene` = 0xFF; `g_hintsOn` = (total +0x0A < 5); 15 hint flags
= 1; `Frame_Begin()`.

`Game_Start` (08b5):

```
c = record +9
if c == 0: Game_NewSetup(); g_area = 5 else: Game_Restore()
Cells_AdjustForCpu(); Music_Stop(); Sfx_Stop()
g_quit = 1; r = Quiz_LoadQuestions(c); g_quit = 0
g_uiMode &= ~8; g_uiMode &= ~2; DS:6168 = 1; DS:6169 = 0; g_screen = 0; g_inputContext = 0
DS:913f = 1; DS:9220 = 1 (no question open); g_messagePending = 1 (start message);
g_gameInProgress = 0; DS:9122 (coins shown) = g_coins (no count-up at start)
Music_Play(tune(g_area)); DS:5bc5 = 1 (Sound_SyncState starts it once more)
DS:5a32 = g_hintsOn; DS:5a42 = g_musicOn; DS:5a52 = g_soundOn
return r != 0
```

`Game_NewSetup` (0bd6): `g_treasuresFound` = 0; `g_keyState` = 8; DS:90f8 = 4; `g_shopCoinsOwed` =
4; slots (18 bytes) = 0xFF; masks (9) = 7; clue words (9) = 0; found masks (3) = 0;
`Layout_ClearSlots(); Cells_ResetVariable(); Layout_Generate()`; DS:8fa5 = `g_slowCpu`.

`Game_End` (09a2): `g_gamesPlayed++`; `g_lastGameTreasures` (DS:9232) = `g_treasuresFound`;
DS:9289 = `g_tier`; record +9 = 0; record +0x15A = `g_nets`; record +0x15C = `g_coins` (banked
for the next game); `Users_Pack(); Users_WriteFile()`.

---

## 10. Elf questions (46ce)

### 10.1 Loading (at start-up and at every game start) [O]

`Quiz_LoadDictionary` (095f), start-up: open file 8 (PRG_DICT); DS:a21a = file length (low
word); read that many bytes into `g_dictionary` (DS:b204); close; `Dirty_Clear()`. Returns 1 if
opened.

`Quiz_LoadQuestions(char resume)` (09ab), from `Game_Start`:

```
if boot drive < 2: MsgBox_Draw(22); Present_Dirty()
g_loadedScene = 0xFF
open file 7 (ALL_PUZS) or return 0
DS:a21a = file length; read 0x2D0 bytes (360 u16 offsets) to the buffer DS:37c4 (far); DS:a226 = it
Quiz_FindTierEnd()
DS:a6aa = start = DS:5fd2[tier]; DS:a224 = end = DS:5fd2[tier + 1]      # tier = record +0x13F (i8)
if not resume:
    if not Quiz_TierHasWork():                 # >= 70 % right first time
        tier += 1
        if tier > 3: tier = 5; Quiz_ResetHistory()   # uses the OLD start (303)
        start, end = DS:5fd2[tier], DS:5fd2[tier + 1]
        Quiz_TierHasWork()                     # recount statuses for the new range
    Quiz_Deal()
lseek(start_offset + 0x2D0); read 0x7B00 bytes to buffer + 0x2D0
p = 0
for s in 0..8:                                 # expand the 9 dealt questions
    src = buffer + 0x2D0 + off[slot[s]] - off[start]
    DS:a684[s] = far DS:(a6b8 + p)
    p += Dict_Expand(src, DS:a6b8 + p)         # returns the length WITHOUT the NUL
close; return 1
```

Quirks [O]: (1) the expanded questions are packed back to back and each next one overwrites the
previous NUL, so only the last is terminated; a question is read only up to its third answer
line, and all 360 questions have at least three CRs after `~` (checked), so nothing shows.
(2) Answered slots keep bit 15 in a resumed game; the offset lookup doubles the slot value with
`SHL AX,1` (46ce:0b07), which drops bit 15, so they expand their own question normally.
(3) The 0x7B00-byte window holds every tier (largest span, tier 5: 30 660 bytes, checked).

`Dict_Expand(src, dst)` (08f1): copy bytes < 0x80; a byte b ≥ 0x80 inserts the NUL-terminated
16-byte entry `(b & 0x7F)·16` of `g_dictionary`; terminate dst; return the number of bytes written
before the NUL.

`Quiz_FindTierEnd` (0d9a): DS:5fdc = 0; if the file length ≠ 0x7B00: while DS:5fdc < 360 and
off[DS:5fdc] ≤ length − 0x7B00 (unsigned): DS:5fdc++; DS:5fde = 360. (X = 123 for the shipped
file, `fmt_data.md` §2.3.)

### 10.2 History, statuses, promotion [O]

status(i) = (history[i >> 2] & DS:5eae[i % 4]) >> (2·(i % 4)), DS:5eae = {03, 0C, 30, C0}; 0
never answered, 1 right first time, 2 right after a miss, 3 last answer wrong.

`Quiz_TierHasWork` (0cd0): counts DS:a6ac[4] = number of questions of each status in [start,
end); percent = (u16)(count[1]·100) / (end − start) (unsigned 16-bit); returns percent ≤ 69
(stay). Promotion at ≥ 70 %.

`Quiz_ResetHistory` (0d60): b = start >> 2; history[b] &= DS:5ff0[start & 3] (masks {00, FC, F0,
C0}); zero `0x5A − b` bytes from history[b+1]. With start = 303: keeps question 303's bits and
clears 300..302 in that byte (the mask table is the inverse of "keep the questions below the
start"), clears 304..359, and writes one byte past the 90-byte history into slot 0's low byte
(record +0x66), which `Quiz_Deal` overwrites right after [O].

### 10.3 Deal (`Quiz_PlanStatuses` 0c39, `Quiz_Deal` 0b6a) [O]

```
plan[0..8] = DS:5fe0 = {0,3,2, 0,3,2, 0,3,2}; avail[0..3] = count[0..3] (copy at DS:b1fc)
i = 0
while i < 9:
    c = plan[i]
    if avail[c] == 0:
        repl = DS:5fe9[DS:5fec[c]]             # 0->1, 1->0, 2->0, 3->2
        for j in i..8: if plan[j] == c: plan[j] = repl
        continue                               # re-examine position i
    avail[c] -= 1; i += 1

for s = 8 down to 0:
    c = plan[s]
    repeat:
        r = rand()
        k = 0; q = start
        while q < end:
            if status(q) == c:
                if k == r % count[c]: break    # count[] = the full pool, not avail[]
                k += 1
            q += 1
    until q is not in slot[0..8]               # duplicates of already dealt slots retried
    slot[s] = q
```

`Game_NewSetup` sets the slots to 0xFFFF before, so only this deal's slots can collide. If both
status 0 and status 1 are empty in the tier the planning loop never ends (0 → 1 → 0) [I from the
code; the engine should guard it, e.g. by falling back to any status].

### 10.4 The question screen: `Quiz_Update` (0007), per frame [O]

Runs only when `g_uiMode & 2` and no other bit (`g_uiMode & 0xFD` = 0). DS:9220 = 1 means no
question shown; DS:b1fa = answered; DS:a21c = slot; DS:a214 = focused display row; DS:a21e =
answer order; DS:ba04 = answer mask at opening; `g_quizCoins` (DS:b1f8) = coins including the
reward.

```
g_inputContext = 3; reopen_menu = false; DS:930c = g_cmd; DS:9238 = 0
if DS:9220 != 0:                                       # open a question
    s = highest slot 8..0 with bit 15 clear
    if none: g_uiMode &= ~2; g_inputContext = 0
    else: Quiz_PickQuestion(); Quiz_SplitText(); Quiz_SetupAnswers()
          DS:922e = 1; g_screen = 1; g_dirty = 1; DS:9220 = 0; DS:b1fa = 0
          DS:90f8 = 2; g_quizCoins = g_coins
elif cmd == 0:                                         # Esc, answered or not
    close(); if DS:b1fa == 0: g_caughtSlot = 0
elif DS:b1fa == 0:
    if cmd == 8:                                       # answer
        a = DS:5e9c[3*order + focus]                   # answer index in the file
        mask[slot] &= ~DS:5f8a[a]; g_dirty = 1; DS:b1fa = 1; DS:90f8 = 6
        Quiz_Correct() if a == 0 else Quiz_Wrong()
        Hud_Redraw()                                   # shows the new prompt icon
    elif 9 <= cmd <= 11: Quiz_SetFocus(cmd - 9)        # mouse over a row
    elif cmd in (6, 7):                                # up / down, skip disabled rows
        p = focus
        repeat 3 times:
            p += DS:5f86[cmd - 6]                      # -1 / +1
            p = 2 if p == -1 else 0 if p == 3 else p
            if DS:5f8a[DS:5e9c[3*order + p]] & DS:ba04: break
        Quiz_SetFocus(p)
    elif 1 <= cmd <= 3:                                # F1..F3: open the menu bar over it
        DS:90fc = -1; DS:9111 = 0; g_uiMode |= 0x40; reopen_menu = true
elif cmd == 8 or mouse buttons changed (DS:9143 != DS:90f4):
    close(); DS:90f4 = DS:9143 = 0
if not reopen_menu: g_cmd = -1; DS:9238 = 1

close(): DS:9220 = 1; g_fullRedraw++; DS:9268++ (forces the HUD clue box); g_uiMode &= ~2
         g_screen = 0; g_inputContext = 0; DS:90f8 = 4; if g_coins != g_quizCoins: g_coins = g_quizCoins
```

The reward reaches `g_coins` only when the screen closes; the HUD then counts it up (§11.2). After
a correct answer `g_caughtSlot` keeps the slot (the elf loses its question, `ElfSt_03`), after a
wrong answer or Esc before answering it is 0 (the elf keeps it).

`Quiz_PickQuestion` (02d9): DS:a21c = `g_caughtSlot − 1`; DS:a216 = slot value (question index);
DS:ba04 = mask[slot]; DS:a6b4/a6b6 = far text DS:a684[slot]; DS:a21e = `rand() % 6`.

`Quiz_SplitText` (0323): DS:a6a8 = 0; find `~`; copy the text before it (including the two pad
spaces) to DS:a293; then for the bytes after `~`: CR or NUL ends answer n (NUL-terminated at
DS:a22a + 35·n), DS:a6a8 = max(DS:a6a8, `Text_Width(answer n)`), n++ until 3 answers. No length
check (35-byte buffers).

`Quiz_SetupAnswers` (03ff): DS:a214 = −1; for display row i = 0..2 (zone 9+i of ctx 3, bytes at
DS:80b2 + 8i): if DS:5f8d[a_i] & DS:ba04 = 0: x1 = 200 (disabled); else x1 = 10, y1 = 0x6A + 12i,
x2 = DS:a6a8 + 12, y2 = y1 + 12 and the first such row becomes DS:a214. (a_i = DS:5e9c[3·order +
i].)

### 10.5 Drawing (painter 1) [O, verified by the scoping pass, 0 px]

`Quiz_DrawScreen` (0271), painter: if DS:922e ≠ 0: `Hud_Redraw()` (full, no actors because
DS:922e = 1), `Quiz_DrawFrame()`, `Quiz_DrawQuestion()`, `Quiz_DrawAnswers()`.
* `Quiz_DrawFrame` (028d): SPRITES frame 57 (panel) at (0, 0) flags 0x40; frames 94 and 103
  (the elf) at (0x8E, 0x8E) flags 0x60 (masked, mirrored).
* `Quiz_DrawQuestion` (0482): ink 0xFFFF; `Text_Draw(10, 0x1C, DS:a293)` (line height 12).
* `Quiz_DrawAnswers` (049e): for row i: ink 0xDDDD if DS:5f90[a_i] & DS:ba04 = 0 (disabled, light
  grey) else 0xFFFF; `Text_Draw(10, 0x6C + 12i, answer a_i)`; the first enabled row: DS:a214 = i,
  `Quiz_SetFocus(i)`.
* `Quiz_SetFocus(i)` (052d): if zone 9+i x1 ≠ 200: frame the old row (DS:a214) with
  `Gfx_FrameRect(x1−1, y1, x2−1, y2, 0x77)` (cyan = erase on the panel colour), DS:a214 = i, frame
  the new one with 0x44 (red); `g_dirty = 1`.

### 10.6 Judging [O]

`Quiz_Correct` (05bb): `Clue_Reveal()`; slot |= 0x8000; ink 0xFFFF; `Flutter_Show(15 + rand() %
4)` (praise text, 49a6:0565); ink 0x4444; `Text_Draw(0x3C, 0x5E, revealed word)` (verified on
capture `ega/031`); ink 0xFFFF; `g_quizCoins += (i8)DS:5f93[mask]` (mask after the pick: 6 → 2, 4
or 2 → 1, 0 → 0); `Sfx_Play(10)`; history(q) = 1 if mask = 6 else 2 (clear the two bits, then OR).

`Quiz_Wrong` (069e): `g_caughtSlot = 0`; ink 0xFFFF; `Flutter_Show(19 + rand() % 4)`;
`Sfx_Play(11)`; history(q) |= 3.

### 10.7 Clue words [O]

`Clue_BuildWords(base)` (079c), from `Layout_PlaceKey` with base = level·3: level = base / 3;
p = (i8)DS:9295 (key picture record); attribute table T = DS:0184 / 0250 / 02ec for level 0/1/2.

```
for k = 2 down to 0:
    a = (i8)DS:5fb3[3*g_clueScheme + k]
    b = T[p*4 + DS:5fa9[a]]
    if a < 3: v = (b & DS:5f9b[a]) >> DS:5fa2[a]
    else:
        repeat: s = rand() % 4; v = (b & DS:5f9e[s]) >> DS:5fa5[s]  until v != 0
        v = 2*s + v
    clue[base + k] = (DS:5fae[a] + v - 1) | 0x80          # word index, hidden
```

`Clue_Reveal()` (06f0): base = `g_level`·3; r = rand(); for j = 0..2: k = DS:5fbc[3·(r % 6) + j];
if clue[base+k] ≥ 0x80: clear bit 7 (revealed) and stop. If all were revealed already: k =
DS:5fbc[3·(r % 6) + 2]. DS:a220/a222 = far pointer DS:5eb2[clue[base+k]] (the byte is < 0x80 at
that point): the praise word. The HUD shows clue[base+k] on row k (§11.3); which attribute a row
holds follows the scheme row DS:5fb3 (row 0: number or size, row 1: size or shape, row 2: object).

### 10.8 Level change: `Quiz_ReorderSlots` (0df2), from `Scene_Update` [O]

Only when `g_uiMode` = 0. n = number of hidden clue words of `g_level` (returned: the number of
carrier elves). Order list L[9]:
1. first pass, slots 0..8 while fewer than 3 collected: mask bit 0 set and (mask & 6) ≠ 6
   (missed, not yet answered right) → append;
2. second pass, slots 0..8 while fewer than 9: mask & 7 = 7 (unasked) → append; mask bit 0 clear
   (answered right) → put at the back (from L[8] downwards);
3. new slot i = old slot L[i] (question, mask and expanded-text pointer); DS:9268 = `g_keyState`
   + 1 (forces the clue box redraw).

A 4th missed slot would be placed nowhere and leave an L entry unset (stack garbage); with at most
3 carriers per level this does not arise in play [I].

---

## 11. HUD and status bar (130f)

### 11.1 Full redraw (`Hud_Redraw` 130f:07a5 with DS:922e ≠ 0) [O, verified with the scoping compose, 0 px]

Fills (bytes, rows): (0, 0)-(0x9F, 0x0A) 0xFF; (0, 0x0B)-(0x9F, 0x0E) 0x00; (3, 0x0E)-(0x9C,
0x0E) 0xFF; (2, 0x0F)-(0x9D, 0x0F) 0xFF; (0, 0x0E)-(0, 0x91) 0x00; (1, 0x0F)-(1, 0x90) 0x0F; (1,
0x12)-(1, 0x8D) 0xFF; (0x9F, 0x0E)-(0x9F, 0x91) 0x00; (0x9E, 0x0F)-(0x9E, 0x90) 0xF0; (0x9E,
0x12)-(0x9E, 0x8D) 0xFF; (0, 0x91)-(0x9F, 0xC7) 0x00; (3, 0x91)-(0x9C, 0x91) 0xFF; (2, 0x90)-(0x9D,
0x90) 0xFF. Then the three menu titles off; `g_fullRedraw = 1; Playfield_Redraw()`; if any of
DS:8f9f..8fa2 ≠ 0: copy them to DS:7d7c/7d7e/7d80/7d82 and `ShelfRoom_DrawShelf()`; if DS:922e ≠ 1:
`Sprites_DrawSorted(1, 13)`; DS:922e = 0; DS:925b = 0; `g_dirty++`; the caches DS:928d (coins),
924d (nets), 9259 (found), 9268 (key state), 91fb (prompt) are each incremented so that the parts
below redraw.

### 11.2 Counters and coin count-up (every call, normally every frame) [O]

If `g_coins` ≠ DS:928d or `g_nets` ≠ DS:924d or `g_treasuresFound` ≠ DS:9259 or `g_coins` ≠
DS:9122:
1. SPRITES frame 60 (Coins/Net/Treasure box) at (0, 0), flags 0 (opaque, absolute position);
2. coin bag 64 if coins > 10, 65 if > 4, else 66; net 67 if nets > 3, 68 if > 0, else 69;
   treasure icon 70 (all opaque at (0, 0), unsigned compares);
3. treasure slots of the current level: for k = 0..tier+1: frame 192 if bit k of found-mask[level]
   is clear, else 193/194/195 for level 0/1/2; at (0x8D if k even else 0x90, 0xA4 + 6k), opaque;
4. **count-up**: if `g_coins` > DS:9122 (unsigned): `Sfx_Play(4)`, DS:9122 += 1; elif `g_coins` <
   DS:9122: DS:9122 −= 1 (silent);
5. numbers (white 0xCCCC, y 0xBA): DS:9122 at x 0x6E, `g_nets` at 0x7D, `g_treasuresFound` at 0x8F
   (`Hud_DrawNumber`);
6. caches = current values.

One count step per call, i.e. **per drawn frame**, each restarting effect 4 (`Sfx_Play` restarts an
effect of the same id). `Game_Start` sets DS:9122 = coins, so a game starts without counting.
Measured by the runtime agent (DOSBox-X, cycles 6000): 13.7 ms per count-up frame (3-4 PIT ticks,
73 frames/s) [O `dos_requests/player.answers.md` #9].

`Hud_DrawNumber(x, y, ink, v)` (0d00): h = number of times 100 can be subtracted with the result
staying ≥ 0 (signed), t likewise with 10 on the rest, units = rest; draw '0'+h if h ≠ 0 else x += 2;
'0'+t if t ≠ 0 or h ≠ 0 else x += 2; '0'+units; x advances by each glyph's advance (digits 4 bytes,
'1' 3, '4' 5). Values ≥ 1000 give glyph '0'+10 etc.; negative values give garbage glyphs [O].

### 11.3 Key icon, clue words, prompt icon [O, verified on 5 captures]

If `g_keyState` ≠ DS:9268: frame 58 ("Clue Words" box) at (0, 0) opaque; if key state ≠ 0: frame
50 + keyState/2 at (6, 0xBE) opaque (51/52/53 key of level 0/1/2 found, 54/55/56 still hidden on
level 0/1/2 for states 8/10/12); if `g_sceneWanted` < 3: ink 0xCCCC, for k = 2 down to 0 with y =
0xBA − 13·(2−k): if clue[3·scene + k] < 0x80: `Text_Draw(0x0C, y, DS:5eb2[clue])` (rows y 0xA0,
0xAD, 0xBA); DS:9268 = `g_keyState`; `g_dirty++`. The box is redrawn only when the key state
changes or someone moves the cache (quiz close, level change, full redraw).

If DS:90f8 ≠ DS:91fb: `Hud_DrawPromptIcon()` (0c2e): frame 59 (message box)
at (0, 0) opaque; if DS:90f8 ≠ 0: frame 60 + DS:90f8/2 (61 "move box / select", 62 "Press ↓ to
drop coin", 63 "Press Enter to go on"); DS:91fb = DS:90f8; `g_dirty++`. Writers of DS:90f8: 4 at a
new game, quiz close, and from `Input_MouseWalk` (4 where a coin can be dropped, else 0); 2 while
a question is open; 6 after an answer; 0 while the menu bar is open; saved in the record.

### 11.4 Rank chest screen: `RankChest_Update` (0edf), per frame [O, verified for totals 0, 6, 300]

Driven by DS:8fbd, a per-frame copy of DS:925d (`Frame_Begin` 130f:0285). DS:925d is set to 1 by
`Cell_P71_V71_ToRankScreen` (176a:102a) when the player enters the chest view (scene 4, SHACK.PAN/SHACK.PAK;
reached from the clubhouse rank picture and after the castle), and cleared by `Cell_P72_RankScreen`
(176a:10b9; 176a:110f also clears the CHAMPION timer) when he leaves; cells spec. No painter runs
in scene 4, so this screen has no HUD or menu titles.

* state 1 (first frame): ink black; `Text_Draw(0x44, 0x1D, player name)`; `Hud_DrawNumber(0x6C,
  0x77, 0xFFFF, total)`; i = 0; while DS:0110[i] ≠ −1 and DS:0110[i] < total + 1 (unsigned): i +=
  2; target DS:92a8 = DS:0110[i + 3] (pairs (threshold, value) from DS:0110: (5,1) (25,8) (70,16)
  (115,24) (170,32) (230,40) (300,48) (400,56) (9999,66) (9999,86) −1); DS:92b0 = DS:9100 & 0xFC;
  step DS:92a6 = 0; DS:925d++ (→ 2, stepping from the next frame).
  Targets: total 0-4 → 8, 5-24 → 16, 25-69 → 24, 70-114 → 32, 115-169 → 40, 170-229 → 48,
  230-299 → 56, 300-399 → 66.
* other states: only when (DS:9100 & 0xFC) ≠ DS:92b0 (a new 4-tick boundary; at most one step
  per frame): DS:92b0 = that; n = DS:92a6; swap colour 7 → 6 in bytes 0x1A..0x98, rows 0x6F−n ..
  0x70−n (the chest fills upward one row per step); at n = 8 one gold piece (frame 125 of the
  loaded bank = SHACK.PAK, 10×5 px, flags 0x40) at (0x51, 0x67); n = 16: two at y 0x5F (x 0x4E, 0x54); n = 24: three at 0x57 (0x4B,
  0x51, 0x57); 32: four at 0x4F (0x48..0x5A step 6); 40: five at 0x47 (0x45..0x5D); 48: six at
  0x3F (0x42..0x60); `Sfx_Play(3)` every step; `g_dirty++`; n++; when n = target: DS:925d =
  DS:8fbd = 0, and if n − 1 ≥ DS:012e (= 56): `g_champFlashTimer` (DS:0100) = 800.
* CHAMPION flash, every frame while DS:0100 ≠ 0: if DS:0100 % 160 = 0: ink 0x6666,
  `Text_Draw(0x40, 0x2E, DS:013a)` (light magenta on the band of the same colour: hidden); if
  % 160 = 80: the same text in black (visible); DS:0100 −= 1 (4 on a slow CPU). Five blinks of 160
  frames; it ends visible [O; runtime capture `avi/ega_r2_crown_nokey.avi`: visible at AVI frames
  3042, 3053, 3063, 3074, 3084, i.e. about 914 loop frames/s on that screen].

`RankChest_DrawStar(x, y, frame2)` (1261): `Sprite_Draw` frame frame2/2 at (x, y), flags 0x40.

---

## 12. Sound triggers (whole program)

Effects (`Sfx_Play(id)` 491b:06d7; on the speaker and AdLib a lower id has priority, a playing
effect is only replaced by an equal or lower id; **on the Tandy and the PS/1 there is no
priority**: `Sfx_Play` first calls `Sfx_Stop`, which resets the current id to 0xFFFF, so any effect
replaces any other [O wave 2]) and their triggers [O, call sites from `sndcalls.py`, script opcodes −5 from
`animsfx.py`]:

| Effect | Triggers |
|---|---|
| 0 | coin toss: second effect of scripts 0x1A/0x1B (`PlrSt_10_OnPath` 18cf:0a73/0ad3 when a coin is dropped) |
| 1 | player slides into a gap: `PlrSt_29_PassageUp` 18cf:1308, `PlrSt_30_SlideDown` 18cf:1379 |
| 2 | elf dust thrown: scripts 0x27/0x28 (`ElfSt_52` 4521:0802, `ElfSt_56` 4521:0a59); end sequence `PlrSt_44_EndSparkle` 18cf:0133; treasure room `Cell_P70_TreasureRoom` 176a:0625 |
| 3 | rank chest step `RankChest_Update` 130f:11b5; script 0x4F (`Elf_CheckNet` 4521:1731, a net catch); scripts 0x57/0x58 (`CoinElf_Check` 4521:0dc2..) |
| 4 | HUD coin count-up step `Hud_Redraw` 130f:0aee; bonus coins cell 1568:1ef8; script 0x59 (`CoinElf_Check` 4521:0e07, coin elf pays) |
| 5 | castle creature hit 1568:192c; first effect of the coin-toss scripts 0x1A/0x1B; elf dust hit, scripts 0x29/0x2A (`ElfSt_53` 4521:08c2/0939, `ElfSt_57` 4521:0b19/0b90) |
| 6 | key found `Search_Update` 4a03:0228/0257/0286 (level 0/1/2); `PlrSt_69_ChampCrowned` 18cf:0743 |
| 7 | treasure found `Search_Update` 4a03:01d0; `PlrSt_64_ChampChest` 18cf:05fc; `Cell_P70_TreasureRoom` 176a:0f01 |
| 8 | gate opens: `Cell_P31_GateL0` 1568:0fa8, `Cell_P39_V39_GateL1` 1568:13c8, `Cell_P42_GateL2` 176a:0362; `PlrSt_42_EndHorn` 18cf:00cf, `PlrSt_45_EndChestOpen` 18cf:015e |
| 9 | `Cell_P45_V45_Bridge` 1568:164d, `Cell_P62_IceSlide` 1568:1a9a, `PlrSt_66_ChampScream` 18cf:06b3 |
| 10 | right answer `Quiz_Correct` 46ce:0638 |
| 11 | wrong answer `Quiz_Wrong` 46ce:06c8 |

Music (`Music_Play(t)` 491b:0791; t by `g_area`: < 4 → 0, < 7 → 1, < 15 → 2) [O]:

| Tune | Started by |
|---|---|
| 1 | `Title_Run` 4ba4:0473 (loop flag DS:63ba = 1 for the title only); end sequence `PlrSt_48_EndFlightInit` 18cf:01fe; `Cell_P11_ClubDoor` 1568:03ca (area 5) |
| by area | `Game_Start` 47c7:0954/0967/097a; Ctrl+S on (43cb:0408..042e); option change via DS:5bc5 (`Sound_SyncState` 43cb:04b4..04da) |
| 2 | `Cell_P5_CaveDoorW` 1568:0073 (area 7), `Cell_P9_CaveDoorE` 01f9 (0x0B), `Cell_P35_UpperRoomDown` 037c (9), `Cell_P17_MineDoorW` 0b7c (0x0A), `Cell_P18_MineDoorE` 0c3f (0x0B), `Cell_P60_IceDoor` 19a1 (0x0C), `Cell_P42_GateL2` 176a:03aa (0x0D, castle) |
| 0 | `Cell_P6_CaveExitW` 012a, `Cell_P10_CaveExitE` 02f9, `Cell_P12_StartRoom` 046b, `Cell_P37_MineExitW` 0bf1, `Cell_P38_MineExitE` 0cb4, `Cell_P44_CastleDoor` 0d17, `Cell_P61_IceCaveExit` 1a16 (all set area 0; P12 and P44 call `Music_Stop` first) |

Areas set without a music call: 2 (1568:0af3, 0f51), 3 (13a5, 144c), 0x0E (0dd3, 1a8a), 0 (1b8d,
18cf:1327/1398), 4 (sign-in), 6 (title). Stops: `Music_Stop` in `Game_Start`, `Game_Shutdown`, the
title end on a slow Tandy/CGA machine, Ctrl+S off, option off; `Sfx_Stop` in `SignIn_Update`,
`Game_Start`, `Sound_SyncState` (a UI mode starts) and Ctrl+S off; music paused (DS:37ce = 0)
whenever `g_uiMode` ≠ 0.

Cross-check with `fmt_sound.md` §7 [O]: its list (3, 4, 6, 7, 10, 11 plus script effects) is
incomplete; direct calls also use 1, 2, 5, 8 and 9, and the scripts use 0, 2, 3, 4 and 5. Only ids
0..11 occur, as `fmt_sound.md` §4.1 states.

---

## 13. Randomness in these modules (order of `rand()` calls)

Turbo C `rand` = Borland LCG (`src/rtl/borland_rand`); `R(n)` = `rand() % n` (signed, result ≥ 0).
In the order the calls happen [O]:

| When | Calls |
|---|---|
| title loop, every frame while sprite slot 2 is idle and not on the SS page | `rand()` (& 0x7F == 0 → start a sparkle) then, only in that case, `R(12)` (position) |
| layout, per level (`Layout_PlaceKey` → `Clue_BuildWords`) | for k = 2, 1, 0: size attributes only: `R(4)` repeated until the sub-attribute is non-zero |
| game start, new game only (`Quiz_Deal`) | for slot 8 down to 0: one `rand()` per attempt (`r % count[status]`), repeated while the question is already dealt |
| question opens (`Quiz_PickQuestion`) | `R(6)` answer order |
| right answer | `Clue_Reveal`: `rand()` (order `r % 6`), then `R(4)` praise text |
| wrong answer | `R(4)` message |

Other modules call `rand()` in between (elves every tick, `Frame_Present` 130f:01eb/01fc during the
dissolve transition, layout, castle creature, AdLib orchestration), so the stream is shared; see
ARCHITECTURE §5.15.

---

## 14. Data tables used (read from SST.EXE at run time)

| DS | Layout | Use |
|---|---|---|
| 0100 | u16 | CHAMPION flash timer |
| 0104 | far ptr[3] | screen painters |
| 0110 | i16 pairs, −1 terminated (DS:0138) | chest targets (§11.4) |
| 013a | string | "CHAMPION" |
| 5256 / 525a / 525e | far ptr | footer strings of boxes |
| 5262 / 52a7 / 52d1 | strings | drop-down item lists (CR-separated) |
| 52ee | far ptr[4] | help topic titles |
| 52fe..5a0f | strings | box titles and texts (via DS:59cc) |
| 59cc | 23 × 16 bytes | box table (§4.1) |
| 5b3c | u16 | line height (12) |
| 5b40 / 5b51 | i8[17] / i8[18] | menu ←/→ and ↑/↓ steps by command |
| 5b63 | i8[] (index −2..5; elements −2, −1 are the last bytes of 5b51) | menu index wrap |
| 5b69 | u16[3] | first dialog id per menu {0, 8, 11} |
| 5b6f | far ptr[14] | unused copy of handlers 0..13 |
| 5ba7 | i16[14] | dialog id → box (−1 = help) |
| 5bc3 | i8[2] | help page step {−1, +1} |
| 5bc7 / 5bcd | u16[3] | menu title x0 / x1 |
| 5bd3 / 5bdf | u16[6] | menu title glyphs (off 3, on 3) |
| 5bed / 5c0b / 5c29 / 5c47 / 5c65 | i16[15] each | help page pictures: frame·2 ×2, x ×2, y |
| 5c83 / 5c86 / 5c89 | u8[3] / u8[3] / u8[8] | zone counts per menu, button zone widths per type, footer-zone x offset per type |
| 5c91..5d34 | strings | footer texts, help topic titles, button labels "No", "Yes", "Done", "See Next Page", "Go Back a Page", type-5 glyph strings 5d07/5d09 |
| 5e9c | u8[6][3] | answer display orders |
| 5eae | u8[4] | history bit masks |
| 5eb2 | far ptr[53] | clue word strings |
| 5f86 | i16[2] | quiz ↑/↓ step |
| 5f8a / 5f8d / 5f90 | u8[3] each | answer bit by answer index (three identical copies) |
| 5f93 | u8[8] | coins by mask after a right answer |
| 5f9b / 5f9e / 5fa2 / 5fa5 / 5fa9 / 5fae / 5fb3 / 5fbc | small byte tables | clue attribute masks, size sub-masks, shifts, byte index, word base, schemes (3×3), reveal orders (6×3) (`fmt_data.md` §7.4) |
| 5fd2 | u16[7] | tier starts {0, 63, 180, 303, 360, X, 360} (X computed into DS:5fdc, DS:5fde = 360) |
| 5fe0 / 5fe9 / 5fec / 5ff0 | u8[9] / u8[3] / u8[4] / u8[4] | deal template, fallback values, fallback index by status, reset masks |
| 6132 / 6144 | u16[9] / far ptr[9] | record offsets / name pointers |
| 6168 / 6169 | u8 | sign-in init / file loaded |
| 616a / 6186 | far ptr[7] / u16[7] | sign-in handlers / sub-state keys |
| 6194.. | zone list of ctx 4 (count + 8-byte records) | sign-in zones; templates DS:622d (8 bytes), DS:6235 (32 bytes) |
| 6255 | far ptr[2] | sign-in headings (remove, select) |
| 625d | char[8][8] | rank labels (Trainee, 1-6 stars, crown glyph) |
| 629d | i16[7] | rank thresholds |
| 62ab / 62b1 | u8[6] | word indices for record +0x124 (restore / pack) |
| 62b7 / 6303 | far ptr[19] each | dialog confirm / cancel handlers |
| 6375 / 6380 | strings | "New Player", key prompt line |
| 7d84..7f2f, 7f30..8123 | key, command and zone lists | §2.3 (80b2 = quiz answer zones) |
| 8a18 / 8a30 | i16[12] each | title sparkle x bytes / y |
| 8a48, 8a5a, 8b54, 8c49, 8c6f, 8cca, 8cf0, 8d2b | strings | title page texts |
| 8d83 | near ptr[14] | title phases |

---

## 15. Per-function reference

### 15.1 Segment 43cb (ui.c)

| Address | Name | Signature | Summary |
|---|---|---|---|
| 43cb:0001 | Menu_Update | `void far(void)` | Menu bar and dialog dispatcher (§5.2) |
| 43cb:014a | Menu_MoveSelection | `void far(void)` | ↑/↓/mouse item move with wrap |
| 43cb:019a | Dialog_Update | `void far(void)` | Open box/help on first frame, close on Enter/Esc, buttons (§5.3) |
| 43cb:021d | MenuCmd_HelpPage | `void far(void)` | Help page turn / close (§6.1) |
| 43cb:028b | MenuCmd_StartOver | `void far(void)` | §5.4 |
| 43cb:02f8 | MenuCmd_StartNewPlayer | `void far(void)` | §5.4 |
| 43cb:032c | MenuCmd_ExitAndSave | `void far(void)` | §5.4 |
| 43cb:034f | MenuCmd_NewPlayerHints | `void far(void)` | §5.4 |
| 43cb:0371 | MenuCmd_Music | `void far(void)` | §5.4 |
| 43cb:039d | MenuCmd_Sound | `void far(void)` | §5.4 |
| 43cb:03cb | Game_HandleHotkeys | `void far(void)` | Ctrl+S (§6.2) |
| 43cb:046e | Sound_SyncState | `void far(void)` | Music pause by UI mode, settings changes (§6.3) |
| 43cb:04ff | Menu_HighlightItem | `void far(void)` | Swap white/black on the item row |
| 43cb:0592 | MsgBox_Draw | `void far(int n)` | Box geometry and drawing (§4.2) |
| 43cb:0848 | Box_DrawFrame | `void far(u16 type,int x1,int y1,int x2,int y2,char far *title)` | §4.3 |
| 43cb:0ab9 | Gfx_HLine | `void far(int x,int y,int w,int c)` | §2.5 |
| 43cb:0ada | Gfx_VLine | `void far(int x,int y,int h,int c)` | §2.5 |
| 43cb:0afb | Gfx_FillRectColor | `void far(int x1,int y1,int x2,int y2,int c)` | §2.5 |
| 43cb:0b18 | Gfx_FrameRect | `void far(int x1,int y1,int x2,int y2,int c)` | §2.5 |
| 43cb:0b63 | Text_Draw | `int far(int x,int y,char far *s)` | §3.1 |
| 43cb:0cfc | Text_Width | `int far(char far *s)` | §3.2 |
| 43cb:0d50 | Button_Draw | `void far(int i)` | §4.4 |
| 43cb:0eb6 | Button_Flash | `void far(void)` | §4.4 |
| 43cb:0ee8 | Delay_Ticks | `void far(u32 n)` | §4.4 |
| 43cb:0f1d | MenuBar_DrawTitle | `void far(int i,char on)` | §5.1 |
| 43cb:107a | Help_LoadFile | `int far(void)` | §6.1 |
| 43cb:10c4 | Help_DrawPage | `void near(int topic,int page,u8 far *data)` | §6.1 |
| 43cb:1273 | Help_Open | `void near(int topic)` | §6.1 |
| 43cb:12cf | Box_SetupInput | `void near(int type,u8 x1,u8 x2,int y2)` | §4.5 |
| 43cb:156b | Null_Handler2 | `void far(void)` | `RETF` only; the "no handler" entry of the elf state table (also addressed as 4521:000b) |

### 15.2 Segment 4ba4 (title.c)

| Address | Name | Signature | Summary |
|---|---|---|---|
| 4ba4:000f | Title_ClearBox | `void far(void)` | Light-cyan story box (§8.2) |
| 4ba4:0045 | TitleStep_DrawTitle | `void near(void)` | Phase 0 |
| 4ba4:00b2 | TitleStep_Wait | `void near(void)` | 30.42 s wait and keys (§8.3) |
| 4ba4:0156 | Title_DrawSprite | `void near(int x,int y,int frame2,u8 flags)` | BEGIN frame |
| 4ba4:017f | TitleStep_Story1 | `void near(void)` | Phase 2 |
| 4ba4:01cc | TitleStep_Story2 | `void near(void)` | Phase 4 |
| 4ba4:0233 | TitleStep_StartWalkoff | `void near(void)` | Phase 6 |
| 4ba4:027a | TitleStep_WaitWalkoff | `void near(void)` | Phase 7 |
| 4ba4:02a3 | TitleStep_WaitActorGone | `void near(void)` | Phase 8 |
| 4ba4:02b1 | Title_SuperSolversPage | `void near(void)` | Phase 10 (§8.4) |
| 4ba4:042b | Title_ResetPhase | `void near(void)` | Phase 12 |
| 4ba4:0434 | Title_Run | `void far(void)` | Title loop (§8.1) |

### 15.3 Segment 47c7 (signin.c)

| Address | Name | Signature | Summary |
|---|---|---|---|
| 47c7:0007 | SignIn_Update | `void far(void)` | §9.2 |
| 47c7:00e7 | SignIn_DrawPanel | `void far(void)` | §9.3 |
| 47c7:0189 | SignIn_DrawNameEntry | `void far(void)` | §9.3 |
| 47c7:01e0 | SignIn_DrawList | `void far(void)` | Sorted roster (§9.3) |
| 47c7:03cf | SignIn_Draw | `void far(void)` | Painter 2 |
| 47c7:042d | Screen_DrawBackdrop | `void far(void)` | OPENING.PAN full screen |
| 47c7:0452 | Users_EnsureLoaded | `int far(void)` | §9.7 |
| 47c7:04a4 | Users_NameExists | `int far(void)` | §9.7 |
| 47c7:0514 | Users_ReadFile | `void far(void)` | §9.7 |
| 47c7:055a | Users_WriteFile | `void far(void)` | §9.7 |
| 47c7:05a9 | SignIn_DrawTypedChar | `void far(u8 x,u8 y,u8 ch)` | §9.3 |
| 47c7:05d4 | SignIn_DrawField | `void far(char on)` | Cursor block and underscores |
| 47c7:06d4 | SignIn_HighlightRow | `void far(u8 x1,u8 y1,u8 x2,u8 y2,u16 a,u16 b)` | Colour exchange a↔b via 3 |
| 47c7:071a | SignIn_MoveHighlight | `void far(void)` | Row highlight toggle |
| 47c7:075d | Users_ClearRecord | `void far(void)` | §9.7 |
| 47c7:07b9 | Users_AddPlayer | `void far(void)` | §9.7 |
| 47c7:0831 | SignIn_SetupZones | `void far(void)` | §9.3 |
| 47c7:08b5 | Game_Start | `int far(void)` | §9.9 |
| 47c7:09a2 | Game_End | `void far(void)` | §9.9 |
| 47c7:09dd | Users_SelectRecord | `void far(void)` | §9.7 |
| 47c7:0a8b | Users_InitNewPlayer | `void far(void)` | §9.9 |
| 47c7:0b19 | Users_LoadPlayer | `void far(void)` | §9.9 |
| 47c7:0bd6 | Game_NewSetup | `void far(void)` | §9.9 |
| 47c7:0c62 | Game_Restore | `void far(void)` | §9.8 |
| 47c7:0e0e | Users_Pack | `void far(void)` | §9.8 |
| 47c7:0f8b | SignIn_HandleInput | `void far(void)` | §9.2 |
| 47c7:1011 | SignInCmd_Select | `void far(void)` | §9.4 |
| 47c7:1199 | SignInCmd_NewName | `void far(void)` | §9.5 |
| 47c7:1328 | SignInCmd_Remove | `void far(void)` | §9.4 |
| 47c7:13d7 | Dialog_CloseRedraw | `void far(void)` | §9.6 |
| 47c7:13dd | SignInDlg_ExitYes | `void far(void)` | §9.6 |
| 47c7:13e9 | SignInDlg_Back | `void far(void)` | §9.6 |
| 47c7:13fa | SignInDlg_RemoveYes | `void far(void)` | §9.6 |
| 47c7:1414 | SignInDlg_ContinueAnswer | `void far(void)` | §9.6 |
| 47c7:1436 | SignInDlg_Reset | `void far(void)` | §9.6 |
| 47c7:1442 | SignInDlg_ListFull | `void far(void)` | §9.6 |
| 47c7:1454 | Dialog_HandleButtons | `void far(void)` | §5.3 |
| 47c7:1502 | Dialog_HandleOk | `void far(void)` | §5.3 |
| 47c7:152b | UI_ForceRedraw | `void far(void)` | §2.4 |

### 15.4 Segment 46ce (quiz.c)

| Address | Name | Signature | Summary |
|---|---|---|---|
| 46ce:0007 | Quiz_Update | `void far(void)` | §10.4 |
| 46ce:0271 | Quiz_DrawScreen | `void far(void)` | Painter 1 (§10.5) |
| 46ce:028d | Quiz_DrawFrame | `void far(void)` | Panel and elf frames |
| 46ce:02d9 | Quiz_PickQuestion | `int far(void)` | Slot, mask, text, `R(6)` order |
| 46ce:0323 | Quiz_SplitText | `void far(void)` | Question and 3 answers |
| 46ce:03ff | Quiz_SetupAnswers | `void far(void)` | Answer zones |
| 46ce:0482 | Quiz_DrawQuestion | `void far(void)` | Question text |
| 46ce:049e | Quiz_DrawAnswers | `void far(void)` | Answers, first focus |
| 46ce:052d | Quiz_SetFocus | `void far(int i)` | Red frame on a row |
| 46ce:05bb | Quiz_Correct | `void far(void)` | §10.6 |
| 46ce:069e | Quiz_Wrong | `void far(void)` | §10.6 |
| 46ce:06f0 | Clue_Reveal | `void far(void)` | §10.7 |
| 46ce:079c | Clue_BuildWords | `void far(int base)` | §10.7 |
| 46ce:08f1 | Dict_Expand | `int far(char far *src,char far *dst)` | §10.1 |
| 46ce:095f | Quiz_LoadDictionary | `int far(void)` | §10.1 |
| 46ce:09ab | Quiz_LoadQuestions | `int far(char resume)` | §10.1 |
| 46ce:0b6a | Quiz_Deal | `void far(void)` | §10.3 |
| 46ce:0c39 | Quiz_PlanStatuses | `void far(void)` | §10.3 |
| 46ce:0cd0 | Quiz_TierHasWork | `int far(void)` | §10.2 |
| 46ce:0d60 | Quiz_ResetHistory | `void far(void)` | §10.2 |
| 46ce:0d9a | Quiz_FindTierEnd | `void far(void)` | §10.1 |
| 46ce:0df2 | Quiz_ReorderSlots | `int far(void)` | §10.8 |

### 15.5 Segment 130f (HUD part)

| Address | Name | Signature | Summary |
|---|---|---|---|
| 130f:07a5 | Hud_Redraw | `void far(void)` | Painter 0 (§11.1-11.3) |
| 130f:0c2e | Hud_DrawPromptIcon | `void far(void)` | §11.3 |
| 130f:0c73 | Screen_DrawOverlay | `void far(void)` | §2.4 |
| 130f:0d00 | Hud_DrawNumber | `void far(int x,int y,int ink,int v)` | §11.2 |
| 130f:0edf | RankChest_Update | `void far(void)` | §11.4 |
| 130f:1261 | RankChest_DrawStar | `void far(int x,int y,int frame2)` | §11.4 |

---

## 16. Verification

Renderer re-implementations in `extracted/scratch/dos_w1_ui/` draw each screen part **on top of
the capture**; every box starts with a full fill of its rectangle, so 0 changed pixels proves the
geometry, colours, text layout and buttons of the whole box [O]:

| Check | Captures | Result |
|---|---|---|
| drop-downs + open title + highlight (§4-5) | `ega/010`, `021`, `022`, `070a` (item 3), `tandy/10`, `tandy/16` | 0 px each |
| Yes/No boxes | `ega/023` (Music, default Yes), `ega/070b` + `tandy/13` (Exit and Save), `tandy/17` (Start New Player), `tandy/07` (saved game) | 0 px each |
| help pages incl. pictures (pages 0, 1, 3) | `ega/011`-`020` (About 1-6, Keyboard 1-4), `tandy/11` | 0 px each |
| name entry box + typed characters + field | `ega/005` (empty), `ega/006` ("Ada"), `tandy/19` ("Ben", caret off phase) | 0 px each |
| title text, story pages, Super Solvers page | `ega/001`, `tandy/01`, `tandy/02`, `tandy/03`, `tandy/04`; `ega/002` is story page **2** (mislabelled "story1" in REFERENCE_CAPTURES) | 0 px each |
| HUD clue box (frame 58, key icon, words at y 0xA0/0xAD/0xBA) | `ega/008`, `032`, `049`, `076`, `094` (words found by search) | 0 px each |
| quiz praise word (red, at 0x3C, 0x5E) | `ega/031` | 0 px (word 40) |
| rank chest name, number, fill rows and gold pieces (`rank_check.py`) | `ega/068` + `ega/102` (total 0, target 8), `ega/r2_end_t6_rank_chest_1star` (total 6, target 16, one piece), `ega/r2_crown_b` (total 300, target 66, six rows of pieces) | 0 px each; colour 7 left just above the fill for totals 0 and 6; the runtime AVI shows one row per 4 game ticks |

Earlier passes (re-checked by reading the code they relied on): sign-in list, quiz screen and the
full HUD/new-game screen are pixel-exact (SCOPING §3). Table decodes: 23 box records, 14 dialog
→ box entries, 19+19 handlers, 6 input contexts (13/9/11/13/69/13 keys), 15 help pictures, 14 title
phases, 12 sparkle spots, 10 chest pairs, 52 sound call sites + 12 script effect opcodes.

---

## 17. Porting notes

**Structure** (`src/dos/game/`, namespace `tmport::dos`): `text_draw` (§3, shared with HUD and
hints), `box` (box table, geometry, frame, buttons, input zones), `menu` (`Menu_Update` state
machine, dialog ids and handler dispatch as a `switch` on the id instead of far pointers), `help`,
`title` (phase list as an enum + function table), `signin` (sub-state enum, `users_dat` for the
image and pack/restore), `quiz`, `hud`. Keep one `DosUiState` struct for the DGROUP globals named
in this document (selected button, open menu, item, dialog id, redraw request, painter, caches,
sign-in state) so the code reads like the original.

**Input.** Reproduce the command model of §2.3 exactly: per-context key and zone tables read from
the EXE, one command per game tick, the one-deep queue DS:90f6 and the "consumed" flag DS:9238.
The game depends on it: "Y" and PgDn inject Enter through the queue, closing help injects F1, Esc
in play becomes "Enter on Exit and Save". The SDL layer must deliver BIOS-style codes (`scan<<8 |
ascii`, Ctrl+S = 0x1F13, Ctrl+R = 0x1312, Backspace = 0x0E08, lower-case letters only for the
name, the game upper-cases the first). Mouse x must be in bytes (0..159).

**Drawing.** All UI drawing goes into the 4-bpp back buffer with the primitives of §2.5; the
highlight is a colour exchange, not XOR, so draw exactly the same operations (the deleted-glyph
remnant in the name field and the CR/LF line handling depend on it). Box widths include the
glyph widths of every byte (control codes too).

**Frame-rate dependence** (one step per drawn frame): coin count-up (1 coin and one effect 4 per
frame), CHAMPION flash (800 frames), title sparkles (chance 1/128 per frame), the rank chest fill
(one row per frame at most, normally every 4 ticks = 11.8 rows/s), menu, box and quiz commands
(at most one consumed per frame; input is sampled once per tick). At ~1700 idle frames/s the
title tries a sparkle about 13 times per second while slot 2 is free, so a new sparkle starts
almost as soon as the previous one ends [I]. The original has no frame limiter and
no retrace wait (the only port 0x3DA accesses are the Tandy palette set-up at 1000:096e..0996),
so its frame rate under DOSBox-X is not the 59.92 Hz of the captured display: idle frames only
run the 1000-iteration delay of `Frame_Present`. The reference rate therefore has to be measured
(runtime measurements at cycles 6000, `dos_requests/player.answers.md` #9: idle ~1690 loop
frames/s, walking ~850/s, HUD count-up 73/s, search blink ~55/s, rank screen ~914/s; the rate
depends on what each frame draws). The engine should not emulate this: run the frame-counted UI
logic at a fixed reference rate per feature chosen to match those measurements (e.g. count-up 73
steps/s, CHAMPION flash 914 frames/s ≈ 0.88 s), independent of the host refresh. Tick-based parts (title
wait 7200 ticks, caret blink 13 ticks, button flash 4 × 18 ticks of 236.7 Hz, chest step clock)
use the virtual PIT of the DOS runtime.

**Pitfalls.**
* Painters are skipped in scenes 3/4 (no HUD there).
* `Game_Start` starts the area tune twice (direct call and via DS:5bc5); harmless, keep or drop.
* `Users_LoadPlayer` ignores the record's sound settings once music is off globally.
* The saved-game "No" drops the save without the warning box (box 12 is unused).
* Answered slots keep bit 15; mask it (the original drops it by doubling) when indexing (§10.1).
* `Quiz_ResetHistory` clears the wrong questions of byte 75 and writes one byte past the
  history; reproduce on the record image so the following deal overwrites it as in the original.
* `Quiz_PlanStatuses` can loop forever when a tier has no status-0 and no status-1 questions;
  add a guard (document it as a deviation).
* `Quiz_ReorderSlots` leaves list entries unset with 4+ missed slots; the engine can fill them
  with the remaining slots (deviation, not reachable in normal play).
* Sign-in rows 1 and 2 have shifted mouse zones (§9.3).
* Keep `g_quit` semantics: 1 inside I/O suspends the game tick; the main loop exits only if it is
  still set at the end of a frame.
* Slow-CPU mode (switch `-X`): boxes save and restore their background instead of a full redraw,
  Esc opens the Exit box directly, the menu's first opening skips the redraw, and the CHAMPION
  timer counts by 4. Optional for the engine; the default (fast CPU) path is what the captures
  show.

---

## 18. Open questions

1. Frame-counted UI timing: measured for the coin count-up (73 frames/s) and the CHAMPION flash
   (~914 frames/s) by the runtime agent (`player.answers.md` #4, #9); the menu/box frame rate and
   the title sparkle rate are not measured (requests `ui.md` #6, #7).
2. Remaining boxes not yet compared with captures: Start Over, New Player Hints, Sound, sign-in
   exit, remove, identical name, list full; help topics 2 and 3 (requests `ui.md` #1-#5). They use
   the verified code paths with other table entries.
3. Help pictures in scenes 3/4 (other sprite bank), if the menu can be opened there (DS:926c is set
   by the end-sequence cells, so probably not) [I].
4. What DS:913f (set by `Game_Start` and the hints option, read by `Hints_Update` 49a6:004f)
   controls exactly: hints spec.

---

## Wave 3 corrections

Found while porting this document's modules (`src/dos/game/ui.cpp`, `title.cpp`, `signin.cpp`,
`quiz.cpp`, `hud.cpp`) and checking them against the captures:

* **§8.2 phase 0 zeroes slot 1's x, y and z**, not "x/y/frame": DS:8FEC, 8FEE, 8FF0 are slot 1
  (DS:8FD6 + 22) +0, +2, +4; its frame word is DS:8FF2 (the one `TitleStep_WaitWalkoff` sets to
  −1) [O 4ba4:0045, slot layout `code_player.md` §3.1].
* **§4.5 types 4, 5 and 7 leave the zone count alone**: `Box_SetupInput` rewrites zone 0 (and its
  release command) and nothing else for them, so the count of the context stays what it was [O
  43cb:12cf]. Type 0 takes its count from DS:5C83[`g_menuOpen`], so `g_menuOpen` must be set
  before `MsgBox_Draw` (Menu_Update does).
* **§4.5 key entries past a smaller count keep their bytes**: a help page sets the dialog key count
  to 9 and leaves entries 10 and 11 (lower-case y, n) in memory; the next Yes/No box sets the count
  back to 11 and they work again. Nothing rewrites entries 10 and 11 [O 43cb:12cf; engine: entries
  that come back are read from the EXE].
* **§11.1-11.2 the treasure markers are drawn only for `g_level` 0..2 and `g_tier` 0..3** (two
  nested loops that look for the matching level and tier), and `g_dirty` is incremented only when
  `g_level` <= 2, markers or not; the marker bit starts at 1 for the matching level [O
  130f:09f7..0ad8]. On the castle top and in the treasure room (levels 3, 4) the counters draw no
  markers.
* **§4.4 / §5.3 the confirm handler runs at the end of the flash on the sign-in boxes too**
  (dialogs 14..16 through `SignIn_HandleInput`): `Dialog_HandleButtons` -> `Button_Flash` -> handler,
  then `SignIn_HandleInput`'s game start and `SignIn_Update`'s `g_cmd = -1` [O 47c7:1454, 0f8b].
  Engine model (ENGINE_MAP §3.4): the four toggles at +0, +18, +36, +54 PIT ticks of the Enter,
  each presented at once, the handler at +72 (ideal deadlines: the original's busy waits return
  exactly at them); `ui.h` `uiFlashPending` / `uiFlashContinue`. For Yes/No boxes `Dialog_Update`
  has already cleared the dialog bit and set DS:922E = 1 before the flash, so during it the game
  ticks run with `g_uiMode` = 0 (the player can move) while the box stays on screen: the frame loop
  must not run a frame then (no `Frame_Begin`, no painter).
* **§9.8 a restore followed by a pack is not the identity for mirror hiding places**: four of the
  48 hiding places are mirror cells (0x86, 0x87, 0xC0, 0xC1: slots 11, 12, 27, 28 of DS:11AA), and
  `Game_Restore` runs `Layout_MirrorSharedCells`, which overwrites them with their source cells'
  picture and content. Every other byte of the reference records round-trips (`USERS_*.DAT`, all
  named records) [O engine test].
* **§9.7-9.9 the writes are reproduced byte for byte** [O engine tests `test_dos_signin.cpp`]:
  - a new player typed as in the EGA run (`a`, `d`, `a`) from the shipped `USERS.DAT` gives
    `USERS_after_signin_Ada.DAT` (header bytes 1-2, DS:929F, taken from the reference run: the sprite
    heap of the frames before);
  - Exit and Save (Yes) from the state `r2/tierb_exitbox` gives `USERS_crafted_tierb_layout.DAT`
    exactly; from `ega/after_castle` it gives `USERS_after_exit_post_castle_Ada.DAT` except bytes 1
    (DS:929F) and 695 (record 1 +0x14C, the prompt icon), which changed between the state (the end
    message still open) and the player's Esc;
  - `Game_End` from the rank-chest state `r2/end_t6_rankchest` gives the file of the next state
    `r2/end_t6_clubhouse` except 7 bytes, all in record 1's level (+0x142) and player / camera
    (+0x14E..+0x154) words: leaving the rank screen moves the player to the clubhouse before the
    end-of-game hook writes (the header word, the banked coins and nets, the dropped saved game and
    the games played are equal).
* **The shifted-A drop is not in the game's code**: the context-4 key list maps 0x1E41 to command
  65 (DS:7E16 entry 18), `SignIn_HandleInput` and `SignInCmd_NewName` accept 0x41 like any capital
  (no compare against it, 47c7:1244..129a), and `Kbd_ISR` has no entry for scan 0x1E or 0x2A. The
  drop seen in the Round 2 keyboard check must come from outside the game (the emulator's key
  injection) [O code; open]. The engine has no special case; the captures typed with other shifted
  letters (`r2_kbd_shift_test_*`) are reproduced exactly.
* **§10.1-10.3 the deal is reproduced against the reference records** [O engine test
  `test_dos_quiz.cpp`]: `Users_LoadPlayer` + `Game_Start` with the srand value of each layout
  (found by the elves agent, `test_dos_layout.cpp`) deals Rascal's 9 questions of the shipped file
  in slot order, and Ada's of her first and second games as sets (her slots were reordered at level
  changes); the second game plans with the statuses of the first game's history (statuses 1, 2, 3).
* **Deviations kept as guards** (§10.3, §10.8, §17): `Quiz_PlanStatuses` falls back to the first
  status with questions left after more fallbacks than the longest chain (3 -> 2 -> 0 -> 1), where
  the original loops forever; `Quiz_ReorderSlots` fills list entries a 4th missed slot leaves unset
  with the slots left out; `Quiz_TierHasWork` returns "promote" for an empty range instead of
  dividing by zero; text and answer copies are bounded by their buffers. None is reachable with
  V 1.0's data.
* **Round 2 mouse-help captures** (REFERENCE_CAPTURES R2.2): `r2_ui_help_mouse_p1` is page 0, `_p2`
  and `_after_pgdn` page 1 (PgDn on page 0), `_next` page 2, `_next2` page 3, `_next3` the Help
  drop-down reopened after Done, and `_p2_after_pgup` page 0 again (PgUp on page 1) [O 0 px each].
* **Verification with the engine's modules**: 64 renders driven through the module functions
  (title and story pages, the series page, the sign-in in every sub-state and box of Round 1 and 2,
  the drop-downs, the Yes/No boxes incl. Start Over, New Player Hints and Sound, all help pages of
  the four topics, the quiz question and right answer, the HUD clue box and status bar) equal the
  captures in every pixel (`extracted/scratch/dos_w3_ui/verify.py`; the test stores their hashes).
  The boxes of the Round 2 captures open with hints No, music Yes, sound Yes (the player's
  settings that `Game_Start` copied into DS:5A32/5A42/5A52).
