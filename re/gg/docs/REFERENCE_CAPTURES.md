# Gizmos & Gadgets reference captures (DOSBox-X)

Wave 0 of the G&G port (§1-§6, round 1) and wave 1 (§7-§13, round 2). This document records how
the original game (*Super Solvers: Gizmos & Gadgets!*, DOS v1.0, `SSG.EXE`) runs in DOSBox-X without
touching the user's desktop, how guest mouse input is injected (the game cannot be played without the
mouse), what was captured, and what was seen while doing it. Round 2 is the reference set that waves
3 and 4 check the engine against: all 8 puzzle kinds, a race in every building, the workshop, the
final movie, sound runs, a second player and a save/resume, with timing measured on the AVIs.
Captures, AVIs and save states are under `C:\Git\TM\extracted\captures\gg\` (gitignored); this file
only describes them.

**Observed** [O] means seen in a capture, an AVI frame, a save state, a file diff or the code at
the cited address. **Inferred** [I] marks conclusions drawn from those observations.

Addresses are Ghidra `SSSS:OOOO` of the G&G project and `DS:XXXX` is DGROUP (`1ea7`); see
`ARCHITECTURE.md` for the file-offset rules. In a running game the segments are relocated:
Ghidra segment S runs at S − 0x786 with the MOUSEKEY TSR loaded (DGROUP at `1721`) and at
S − 0x7dc without it (DGROUP at `16cb`) [O, save states]. Readers of save states should find
DGROUP by the Borland copyright at DS:0004, not by a fixed segment.

## 1. Setup

| Item | Value |
|---|---|
| Emulator | DOSBox-X 2026.10.01, `tools\dosbox-x\bin\x64\Release\dosbox-x.exe`: the **SDL1** build (its windows are classes `SDLParent` + child `DOSBox-X`, on two threads), the same binary as the TM profiles |
| Config | `tools\dosbox_gg.conf` (see the table below) |
| C: drive | `extracted\gg_c\`, a writable copy. `extracted\gg_c\GG\` is a byte-identical copy of `GG_DATA` (default `GadgetsAndGizmos\`; checked against `extracted\scratch\gg\wave0\originals.sha256`). The original folder is only read, never mounted. |
| Start-up | The autoexec runs `C:\RUN.BAT`, rewritten by `tmctl` on every gg start: `date 10/01/1993`, `time 09:00:00`, the guest clock into `C:\CLOCK.TXT`, `C:\MOUSEKEY.COM` (§2), `cd \GG`, `SSG <args>` |
| Driver | `tools\tmctl.py --profile gg` (additive: the `win` and `dos` profiles behave exactly as before; checked by running the old and the new `tmctl` side by side with the process start mocked: identical command lines, `RUN.BAT`, parking, profile file and state paths) |

Config choices:

| Setting | Value | Why |
|---|---|---|
| `machine` | `vgaonly` | The game uses mode 13h only, ports 3C8h/3C9h/3DAh, no VESA and no SVGA registers, and requires a display combination code ≥ 7 (`303a:0189`). Plain IBM VGA is the smallest machine that has all of this. It reports code 8, refreshes at 70 Hz, and has none of an SVGA BIOS's extras. |
| `cputype`, `cycles` | `486`, `fixed 26800` | `Cpu_Detect` (2c08:0032) gives 2 on a 286 and that changes the game: `2a80:07b1` then halves the first chimp's room word of each chimp list (ARCHITECTURE.md §2.6). 3 (386) and 4 (486) are treated alike (the only test is `== 2`). A 486 is the typical 1993 machine. Fixed cycles make emulated time independent of the host. The game paces its loops by PIT deadlines (4 ticks = 27.5 ms per frame), so the CPU only has to be fast enough; 26800 is about a 486DX2-66 in DOSBox terms. |
| `[sblaster]` | `sbtype=sb2`, `oplmode=opl2` | The game drives only an OPL2-compatible chip at 388h (`-sa`/`-sb`/`-sm` all mean that; there is no DSP or PCM code). Sound Blaster 2.0 = YM3812, which the engine's ymfm OPL2 reproduces. The `.dro` captures say hardware type 0 (OPL2). |
| `[mixer] nosound` | `true` | Nothing plays on the host; the mixer still renders into captures (as for TM DOS). |
| `[dos] int33` | `true` | The game refuses to start without a mouse driver (`Mouse_DriverPresent` 2c08:0011). The built-in driver is used, but MOUSEKEY takes over its event handler (§2). |
| `[sdl] autolock`, `middle_unlock` | `false`, `none` | Safeguard: nothing may capture the host pointer (a capture clips the real pointer to the window, which is parked off-screen). No mouse messages are posted anyway (§2.2). |
| `windowposition`, `windowresolution` | `-4000,100`, `640x400` | Parked off-screen; screenshots and AVIs are internal captures and do not depend on the window. |
| `memsize`, `ems` | `16`, `false` | Conventional memory only; the game uses no EMS/XMS and needs 363 500 bytes free (303a:01bd) [O: it starts]. |
| `saveremark`, `forceloadstate`, `show recorded filename` | `false`, `true`, `false` | No DOSBox-X prompts (as for TM DOS). |

The guest clock matters: `Init_CommandLine` seeds `rand()` with `time(NULL)` and keeps the value
in DS:2454 (low word = seed). With the fixed clock both fresh boots of this pass got the seed
**29CFh**, which is what Borland's `dostounix` gives for 1993-10-01 08:59:59 [O]. `C:\CLOCK.TXT`
read `10/01/1993` and `8:59:59` in both. DOSBox-X's `DATE` accepts only `MM/DD/YYYY` with slashes.
With dashes it prints the current date and changes nothing; the first boot of this pass therefore
ran with the host date, seed 65CFh [O]. While the game runs, its timer chains the BIOS INT 8 on
every tick, so the guest clock runs 8 times fast (ARCHITECTURE.md §3.1; observed: 09:00 → 09:40
within a few minutes).

Commands (bash, from `C:\Git\TM`):

```
python -P tools/tmctl.py start --profile gg --park --fresh     # fresh copy of GG_DATA, then boot
python -P tools/tmctl.py start --profile gg --args "-si" --park # PC speaker (switches: Read_1st.txt)
python -P tools/tmctl.py key space text:ada                    # attract -> sign-in, type a name
python -P tools/tmctl.py mouse click 164 180                   # game coordinates (320x200)
python -P tools/tmctl.py mouse drag 47 122 200 80 600          # left drag over 600 ms
python -P tools/tmctl.py shot OUT.png                          # 640x400 (mode 13h, 2x2)
python -P tools/tmctl.py state save w0/hub                     # -> savestates/w0/hub.sav + .players
python -P tools/tmctl.py state load w0/hub                     # also puts that PLAYERS.DAT back
python -P tools/tmctl.py rec start / rec stop OUT.avi          # ZMBV, 70.0863 frames/s
python -P tools/tmctl.py audio opl start / audio opl stop OUT.dro
python -P tools/tmctl.py stop
```

- Options go after the command (`tmctl.py rec start`, not `tmctl.py --profile gg rec start`;
  the latter prints the help and does nothing). Later commands follow the running profile.
- `--fresh` copies every file of `GG_DATA` over `extracted\gg_c\GG\` first. That restores the
  shipped `PLAYERS.DAT` (no players) and `GIZMO004.DAT`, the screen swap file the game rewrites at
  run time (`Screen_SaveToSwap` 14ff:0226; it differed from the shipped file after the first
  session [O]). The copy is also made automatically when `extracted\gg_c\GG\SSG.EXE` is missing.
- `--clock "MM/DD/YYYY HH:MM:SS"` sets another guest clock, `--clock host` keeps the host's,
  and `--no-mousekey` boots without the TSR.
- States go to `extracted\captures\gg\savestates\NAME.sav`, each with a copy of the guest's
  `PLAYERS.DAT` as `NAME.players`. `state load` puts it back unless `--keep-players`. As for the TM
  DOS profile, this goes through raw slot 10; slots 1..9 are shared with the other profiles.
- Focus: no command activated a window. The foreground window was checked before and after
  every experiment of §2.2 and the posted clicks of the tours, and it never changed. The window
  stays parked; `start --park` re-parks it for 45 s.

## 2. Guest mouse input

### 2.1 What the game needs [O]

`Input_InstallHandlers` (1454:0000) registers `Mouse_EventHandler` (17a0:0004) with INT 33h fn
0Ch, mask 1Fh. The handler runs at interrupt time:
- x = CX/2 (the driver's x is 0..639 in mode 13h);
- a move stores the position in DS:7a88/7a86 and redraws the cursor, unless the game is drawing
  (DS:01e2 ≠ 0), in which case the move is dropped;
- a left press latches DS:0a10 = 1 (held), DS:0a1e = 1 (first) and the position DS:0a1a/0a1c;
- a left release latches DS:0a20 = 2 with the position and a tick stamp;
- the right button is ignored.

`Input_GetCode` (198d:0106) turns the latches into zone codes (press code | 0x4000 on the first
poll, | 0x8000 while held; the release code once) through the zone lists DS:0a32/0a36. The game
also calls fn 0 (driver check), fn 3 (once, in `Mouse_Init` 17ab:000e), fn 4 (it moves the
pointer itself: 35bb:0103, 3b2f:09a2, 4140:0013) and fn 7/8 (ranges, `Mouse_SetBounds`
17ab:017f). Clicks are mandatory: in the sign-in (Start Game, With/Without Chimps), the hub
(buildings), menus and dialogs, and every puzzle (its first hint says so).

### 2.2 What was tried

**(a) Posted window messages: fails, and must not be used.** [O] This DOSBox-X build is SDL1 with
the DIB backend. Its window procedure (`vs/sdl/src/video/wincommon/SDL_sysevents.c` in the
DOSBox-X sources) reads no coordinates from WM_MOUSEMOVE. It only claims mouse focus and calls
`TrackMouseEvent(TME_LEAVE)`. Motion is generated in `DIB_PumpEvents` from `GetCursorPos`, the
real pointer (`windib/SDL_dibevents.c`), and only while the window has mouse focus. Because the
real pointer is not over the parked window, Windows posts WM_MOUSELEAVE at once and the focus is
gone before the pump looks. Button messages call `DIB_CheckMouse` (again `GetCursorPos`, clamped
to the window) before the button event, and `SetFocus`/`SetCapture` on the SDL window.
Experiments (states in `extracted\scratch\gg\wave0\capture\states\scratch\`):

| Posted | Result |
|---|---|
| WM_MOUSEMOVE at client (50,50), (100,100), (200,150) to the child window, then the same to the top window | the driver's position (the `Mouse` entry of the state: x, y as floats) stayed at the reset value (320,100); the game's cursor at (160,100) |
| WM_LBUTTONDOWN + WM_LBUTTONUP at client (200,100) | the driver jumped to (639,199), the bottom-right corner: the real pointer far to the right of the window, clamped. The game latched the press and release at (319,199) instead of (100,50) |

At the moment of that click, the cursor-clip rectangle of the user's foreground application
changed (from a one-point clip to the whole desktop). The cause was not established. No mouse
message has been posted since, and `tmctl` posts none. Setting the guest position by moving the
real pointer is out of the question. With `autolock` (the TM profiles' setting) a click would also
capture the host pointer. (a) is therefore unusable. Not tried: the SDL2 build in
`tools\dosbox-x\bin\x64\Release SDL2\` (whose message handling differs) and DOSBox-X's
`mcp_server` debugger port.

**(b) A guest-side TSR driven by posted keys: works.** `MOUSEKEY.COM` is generated as raw 8086
machine code by `tools\gg\mouse_tsr.py` (1220 bytes, 85 resident paragraphs; no assembler, no game
code or data), written to `C:\` and loaded by `RUN.BAT` before `SSG`. It hooks three vectors before
the game starts:

| Hook | Behaviour |
|---|---|
| INT 33h | fn 0Ch: remembers the program's handler and mask, and registers **mask 0** with the real driver, so host mouse events can never reach the game. fn 3: returns the injected position and buttons. fn 4: the injected position follows the program's own moves, then the call is passed on. fn 7/8: the ranges are kept for clamping (as a driver does), then passed on. fn 0/21h: reset (no handler, full range, centre 320,100), passed on. Everything else is passed on. |
| INT 21h | AX=2509h (set INT 9) makes the program's handler the TSR's chain target and leaves the vector alone; AX=3509h returns that chain target. The TSR stays **first** in the INT 9 chain: TSR → `Kbd_ISR` 1454:00e4 → BIOS. |
| INT 9 | A command is a command key followed by 4 data keys, each data key one of 32 keys carrying 5 bits: x = s0·32 + s1, y = s2·32 + s3 in INT 33h units. Command keys: F5 move, F6 left down, F7 left up, F8 right down, F9 right up. The 5 keys and their releases are acknowledged and EOI'd by the TSR, so the game and the BIOS never see them. Data keys without a pending command, and all other keys, pass through. A command not completed within 200 BIOS ticks is dropped. |

On the fifth key the TSR clamps the position to the current range and tells the driver (fn 4).
It then calls the program's handler like a driver would: AX = event bits ∧ mask, BX = buttons,
CX = x, DX = y, SI = DI = 0. The call runs inside the keyboard interrupt with interrupts enabled,
before the EOI. A move command always reports "moved", even to the same position, so a move the
game dropped while drawing can be repeated. A press or release reports "moved" too if the position
changed.

The first version sat *behind* the game's keyboard handler: it was installed before the game and
received the keys the game passes on. Moves worked, but clicks on buttons did nothing, although
the latches showed the press and the release at the right place (the Start Game zone, x 146..186,
y 170..192, codes 708h/704h). The game's INT 9 writes a ring record (type 3 + tick, DS:8556) for
every key it passes on. `Input_GetCode` pops one record per call and, when the BIOS buffer is empty,
**replaces the mouse code it has just read with 0** [O, 198d:0106]. The release that arrived with
a command's last key was lost that way. The INT 21h hook fixed it: the game never sees the
command keys.

**(c) Patching save states:** not needed.

### 2.3 Calibration and limits

`tmctl mouse` takes game coordinates and sends INT 33h x = 2·x. The tip of the game's arrow
cursor lands exactly on the commanded pixel [O]:

| Commanded (game) | TSR (INT 33h) | Game DS:7a88/7a86 | Arrow tip in the shot (640x400 halved) |
|---|---|---|---|
| (100,50) | (200,50) | (100,50) | (100,50) |
| (101,51) | (202,51) | (101,51) | (101,51) |
| (37,143) | (74,143) | (37,143) | (37,143) |
| (0,0) | (0,0) | (0,0) | (0,0) |
| (319,199) | (540,190) | (270,190) | clamped: the hub sets the range x 0..540, y 0..190 |

Ranges set by the game (fn 7/8, observed in the TSR's state): sign-in x ≤ 620, y ≤ 190; hub
x ≤ 540, y ≤ 190; puzzle x ≤ 620, y ≤ 190. Clamping is a real driver's behaviour, so injected
input stays faithful.

Speed and reliability: a command is 10 posted key edges. With the default 5 ms per edge a command
takes about 62 ms to post (15 ms per edge: 161 ms). Forty random moves were sent back to back at
15, 5 and 0 ms per edge. All 40 arrived each time (the TSR's event counter went up by 40), and the
TSR position, the game's cursor and the last command agreed [O]. `click` holds the button for 120
ms, which spans several 27.5 ms frames, so `Input_GetCode` sees the press (first and held) and then
the release, like a real click. Typing still works: the sign-in name came out as "Ada" exactly,
in the shot and in `PLAYERS.DAT`.

Limits:
- No mickeys (SI = DI = 0). G&G does not read them.
- fn 14h/18h are not intercepted (G&G does not call them).
- Right-button events are delivered, but the game ignores them.
- A move that arrives while the game draws is dropped by the game itself (DS:01e2). A press or
  release still carries its own position. Repeat the `move` if the cursor must be at a place for
  hover effects (the hub's building labels).
- The TSR moves the game 86 paragraphs up in memory. State readers must locate DGROUP by
  signature; `tools/gg/mouse_tsr.py status STATE` prints the TSR's variables and the game's mouse
  variables.
- Host mouse events are cut off completely, by design.

## 3. Capture inventory

Root: `extracted\captures\gg\`. Shots are DOSBox-X's 640x400 PNGs of mode 13h. Every pixel is a
2x2 block (checked on the first shot), so halving gives the native 320x200 frame.

### 3.1 `w0\`: the proof tour (25 frames, fresh boot)

| Files | Shows | Input |
|---|---|---|
| 01-02 | title; story page 1 on the airship | (boot) |
| 03-04 | sign-in: empty roster, the arrow cursor at the driver's reset position; name "Ada" typed | Space; `text:ada` |
| 05 | the With/Without Chimps dialog | mouse click Start Game (164,180) |
| 06-07 | Technology Center hub; hovering the left building shows the label "AIRCRAFT" | click With Chimps (93,137); move (100,50) |
| 08-10 | Aircraft building: New Player Hint over the Level-1 BLIMP blueprint; the blueprint; the Decal part's page | click (62,85); Enter; click (53,51) |
| 11-12 | workshop hint; the workshop (tube pad) | click Done (269,150) twice; Enter |
| 13-15 | warehouse hint; the first warehouse room (6 floors, X doors, a chimp, parts); in front of the first X door | ↑ (hold 300 ms); Enter; → (hold 2000 ms) |
| 16-17 | puzzle hint; the machine-pieces puzzle (its instructions, a box with 3 pieces) | ↑; Enter |
| 18-20 | shaft placed; the T-handle; **solved** ("Congratulations!…" in the message box) | drag from the box, clicks to place (§5) |
| 21-22 | the room behind the door (3 floors, a chimp); hovering a part | Enter; move |
| 23-25 | File menu; Return To Technology Center dialog; the hub again | click File (14,4), the item (107,27), Yes (221,133) |

States `savestates\w0\`: `01_title 03_signin 06_hub 09_blueprint 12_workshop 14_warehouse
15_at_door 17_puzzle 18_puzzle_piece1 20_puzzle_solved 21_room_behind_door 25_hub_again`,
each with its `.players`.

What the wave 2 engine found in these shots [O]:

* `01_title` shows the title in phase 3 with screen animation 0x1005 at its frame step 2 and 0x1007
  at step 1.
* The pointer (cursor top-left, game pixels) of the shots the engine reproduces whole, cursor
  included: 03 and 04 (160, 100), 05 (164, 180), 06 (93, 137), 08 (62, 85), 10 (53, 51), 13 and 16
  (269, 150), 23 (14, 4), 24 (107, 27); round 2: menus/01 (80, 4), 02 (136, 4), 03 (160, 17);
  player2/02 (60, 150), 03 and 04 (219, 180), 05 (164, 180), 02_file_menu (14, 4); resume/02 (14, 4),
  03 (99, 36), 05 (160, 100), 06 (100, 62); sound/si_02 (100, 73); the text-viewer shots
  puzzle/k0_02 (44, 20), k5_09 (28, 21), k3_09 (41, 31), k4_09 (24, 21), k7_09 (31, 20); the
  blueprint part pages altenergy/03 (170, 55), automotive/03 (160, 85), aircraft/46 (200, 75), 47
  (165, 140), 48 (220, 125).
* The first screen of a puzzle (r2/puzzle/k*_01, w0/17) shows the **arrow left by the hint dialog**
  at (269, 150), because nothing flushes until input; launch (5) and energy (6) redraw their own
  cursor with their timers. Likewise a blueprint overview whose pointer is on a hot spot shows that
  arrow, not the label cursor (r2/aircraft/02 at (50, 50), r2/altenergy/02 at (170, 60),
  r2/aircraft/39 at (270, 50)).
* `14_warehouse` is a **torn frame**: rows 0..95 show the room animations 1 frame after the
  `14_warehouse` state, rows 96..199 4 frames after. In general the shots are taken before their
  states, so pads and chimps differ; the chimps were fitted for 14 (seq 2 step 12 at (245, 120)), 15
  (room 1, seq 3 step 13 at (103, 143)) and 21 (seq 2 step 8, anchor (12, 131)).

### 3.2 `w0_replay\`: reproducibility

`tools\gg\capture_tour.py run door` (a second fresh boot) and `run puzzle` (from the state
`w0/17_puzzle`) wrote the same 25 frame names. Against `w0\`:

| Frames | Differing pixels | Why |
|---|---|---|
| 02-06, 08-12, 16 | 0 | deterministic: fixed clock, `rand()` not called before the warehouse (the rand state DS:442a still equalled the seed in the hub) |
| 01, 07, 13 | 62 452 / 860 / 100 | taken at another moment of an animation (the title's fade-in, small animations in the hub and the warehouse) |
| 14, 15 | 1 116 / 1 840 | the chimp elsewhere: a walking chimp calls `rand()` whenever one of its probes touches an edge, pad, door or item (often, and depending on where it stands), plus once when it starts (`code_world.md` §16), so the warehouse depends on the real-time pace |
| 17 | 32 | same puzzle type, another variant: "Pieces 2" instead of 3 (rand state at the door 9DEA5908h against 790EEC88h) |
| 18-20, 24 | 0 | the puzzle replayed from `w0/17_puzzle`: deterministic |
| 21-23, 25 | 2 924 / 4 460 / 3 164 / 328 | the chimp's walk; the hub's windmill |

The exploration session before (host date, seed 65CFh) met a different puzzle type behind the
same door: the launcher ("Shoot the basketball…"). It was solved by
dragging the ball onto the launcher, pulling the plunger to ENERGY 4 with the button held, and
releasing. Its frames are in `extracted\scratch\gg\wave0\capture\` (`w13`..`w16`).

### 3.3 AVIs and OPL captures

| File | Content |
|---|---|
| `avi\w0_title_attract.avi` | 640x400 ZMBV, 70.0863 frames/s, 4132 frames (58.96 s): title, story pages 1-2; started about 6 s after the title appeared |
| `audio\w0_title_attract.dro` | the same span, raw OPL log: DRO v2.0, hardware type 0 (OPL2), 4324 register writes in 56.9 s, rhythm mode (BD register 20h/30h/21h ...) |
| `avi\w0_replay_title_attract.avi`, `audio\w0_replay_title_attract.dro` | the second boot, started at the title's fade-in: 3303 frames (47.13 s), 3654 writes |
| `avi\w0_replay_boot_part1_640x480.avi`, `..._part2_720x400.avi` | the same recording before the game: DOSBox-X starts a new AVI file at every guest video-mode change (boot screen 640x480 at 59.94 Hz, 49 frames; text mode 720x400, 8 frames). `tmctl rec stop` moves only the newest file; the earlier parts stay in `extracted\captures\` |

The two OPL captures, from two boots, are **identical write for write** for 3165 writes (until
the shorter one ends), at a constant offset of 3716 ms [O]. `PLAYERS.DAT`: `original\PLAYERS.DAT`
is the shipped file (578 487 bytes, version 12, no players).

### 3.4 `PLAYERS.DAT` write points [O, the `.players` copies]

| After | Bytes differing from the shipped file |
|---|---|
| title, sign-in screen | 0 |
| With Chimps (the new player "Ada" enters the hub) | 8: the count (offset 6), the name in index entry 0 (offset 8) and in the record of **slot 98** (0x8A0 + 98·0x16BD = 572 666), and that record's chimps choice (+0x16 = 572 688). The shipped index lists slot 98 twice and slot 0 never, so the first new player gets slot 98, as `fmt_data.md` §8.2 predicts; `players.py FILE --index` shows entry 0 = slot 98, and record 0 stays empty. |
| building, warehouse, puzzle, room behind the door | unchanged |
| File > Return to Technology Center > Yes | 308 (offsets 6..576 730; the score 400 = 190h at offset 24) |

The size stays 578 487 bytes.

## 4. Observed behaviour and timing

- **Attract** [O, AVIs]: title (fade-in, about 8.9 s on screen), then the story pages on the
  airship, each **30.11 s** (story page 1 complete → page 2: 30 105.8 ms in both AVIs; 0x111c PIT
  ticks = 30.07 s plus the page's drawing). Enter advances a page, Space jumps to the sign-in. The
  last page (the welcome page, "Press Enter to sign in") shows the
  sign-in computer with buttons that are only part of the picture: a click on them did nothing.
  After that page's 30 s the attract restarts (an Enter some seconds later showed story page 1,
  i.e. it advanced from the title [I]; `ARCHITECTURE.md`: the last page's time-out restarts the loop).
- **Cursor**: hidden on the attract pages; a green arrow from the sign-in on; a hand over puzzle
  pieces; text labels next to the arrow in the hub ("SIGN IN", "AIRCRAFT", "AUTOMOTIVE").
- **Palette loads** [O]: in the AVIs every palette load is two consecutive frames, the first
  changing only the blocks that use colours 0..127, the second all blocks. That is `Pal_SetRange`
  1399:05d6, two 128-colour groups, one retrace each. Lengths seen: title fade-in 16 frames (frames
  15-30 of the replay AVI); 19 frames at 650-668, which wave 1 identified as `Pal_FadeFromSaved`
  (palette 0x1000 → 0x1004, 16 frames) after an animation frame and a wait [I], not a fade-out;
  22 frames at 861-882, which are the title's fade **to black** (`Pal_FadeOut` 2c11:061b), not a
  story fade-in; then, after a 4-frame pause, the story page's fade-in (887-902, 16 frames). These
  lengths are lower bounds (a load that changes no visible pixel is not seen). Reconciled with the
  code's 12 and 9 loads in `code_runtime.md` §13.4 and §10.2 below: 22 and 16 visible frames.
- **Animation pace**: the windmill on the attract pages steps every 7 or 8 frames, 109.8 ms on
  average = 16 PIT ticks at 145.65 Hz.
- **Sound tick**: the OPL writes fall on a **13.730 ms** grid (72.833 Hz; best fit over 1594 write
  groups). That is `Snd_Tick` on every second PIT tick at divisor 2000h (13.731 ms) [O].
- **Hints**: New Player Hint boxes come up when entering the building, at the workshop, the first
  warehouse room and the first puzzle; Enter closes them.
- **Warehouse**: ← / → walk, ↑ at a door or the tube pad. X doors are puzzle doors. A solved
  puzzle adds points (score 0 → 400 for one puzzle) and Enter goes through the door.

## 5. How to reproduce

```
python -P tools/gg/capture_tour.py run door OUTDIR [--states PREFIX]     # boots DOSBox-X itself
python -P tools/gg/capture_tour.py run puzzle OUTDIR [--states PREFIX]   # loads w0/17_puzzle
python -P tools/gg/capture_tour.py list puzzle                           # the steps
python -P tools/tmctl.py stop
```

Segment `door` starts DOSBox-X with `--fresh` in a background thread, so the title can be
recorded, and starts the AVI and OPL captures as soon as the DOSBox-X menu exists. It stops them
after the second story page and drives the tour up to the first puzzle. Up to the warehouse its
frames equal `w0\`. After that, the chimps and the puzzle behind the door depend on the run's pace
(§3.2). Segment `puzzle` replays the solution of the 3-piece machine puzzle from the state:
1. drag the first piece (a long shaft) from the box and click it in at (200,30);
2. take the wedge and click it at (120,40);
3. take the short bar and click it at (190,24) (a T-handle);
4. pick up the wedge (click at (125,48)) and click it at (200,126) under the shaft (solved).

A placed or carried piece hangs by its top-left corner at the cursor.

## 6. Not done / open points

- Speaker (`-si`) and no-sound runs; the movie, the races and the results; the chimps' banana
  throwing; puzzle types other than the machine pieces and the launcher.
- Why the foreground application's cursor clip changed at the posted click of §2.2.
- The fade lengths (§4) against the code.
- Determinism of the warehouse needs a state (as above) or input timed in emulated time; posted
  keys are timed in host time.

Round 2 (below) closed most of these: the speaker and no-sound runs, the movie, the races and
results, all 8 puzzle kinds and the fade lengths. Still open: the chimps' banana throwing (§13).

---

# Round 2 (wave 1)

## 7. Method

Round 2 was driven by the same profile and tools as round 1 (`tmctl.py --profile gg`, parked, the
MOUSEKEY TSR; no window was activated and no mouse message was posted). Three things are new.

1. **States first.** Every scenario starts from a named state under
   `extracted\captures\gg\savestates\r2\`, so it replays without walking the warehouse again. Posted
   keys and mouse commands are timed in host time; from a state, emulation itself is deterministic.
2. **A second player without chimps.** Most of round 2 was played by a second player, "Bob", signed
   in **Without Chimps** (§9.5). Without chimps the warehouse makes no `rand()` calls between frames
   (§10.7), so a run from a state depends only on where the input falls.
3. **State edits, used sparingly and listed in §9.9.** Two kinds:
   * *Puzzle choice.* `State_09_Puzzle` (38e2:0000) picks the kind with `random(8)` (38e2:01e8) and
     the variant with `random(n)` over the first three unsolved variants (38e2:0202) [O code]. A
     state with the player at a puzzle door is patched with `g_heldKeys` (DS:0218) = 4 (Up held)
     and a chosen `g_randState` (DS:442a); on load the door is taken within a frame and the chosen
     kind and variant come out. The rand state is computed by `capture_mem.py door` from the number
     of `rand()` calls that precede the choice (§10.7). Nothing else is changed: the puzzle screens
     and solutions are the game's own.
   * *Part boxes.* In two buildings, to avoid walking the maze, boxes of the warehouse were moved
     next to the tube and given the wanted part with `capture_mem.py boxset` (the box list is
     described in §9.9). The pick-ups, the workshop build and the races that follow are the game's.
     The Aircraft vehicle was collected the normal way except for one swap of two box contents.

The poke of `g_heldKeys` leaves Up "held" until a real Up key-up arrives (no break code was ever
sent), and a held Up takes the player back through the door he arrives at (§11). Every door
state was therefore followed by `key hold:up:100` while the puzzle was on the screen.

## 8. Tools

| Tool | What |
|---|---|
| `tools\gg\capture_mem.py` | reads and patches the guest RAM of a G&G save state (DGROUP found by the Borland copyright): `vars` (state, building, level, score, rand state, puzzle kind and variant, held keys, sound device ...), `peek`, `poke`/`pokew`/`poked`, `diffds`; `rand` (the Borland LCG), `seedfor`, `seedpuz`, `door` (puzzle choice, §7), `calls` (number of `rand()` calls between two states, by stepping the LCG backwards), `boxes` and `boxset` (part boxes, §9.9). Writes new `.sav` files (and copies the `.players`); never touches the running emulator |
| `tools\gg\capture_timing.py` | measurements on the AVIs and `.dro` logs: `palette` (global changes = palette loads and redraws, with brightness, grouped into fades), `pace` (update events inside a box, their intervals in frames, ms and PIT ticks), `gaps` (runs of identical frames), `dro` (DRO v2: writes, gaps, best-fit tick), `info` |
| `tools\gg\capture_r2.py` | replays round 2 segments from their states: `puzzle_k0`..`puzzle_k7` (solve variant 0), `race_aircraft`, `race_automotive`, `race_altenergy`, `movie`, `workshop_build`, `resume`; `list`, `run SEGMENT OUTDIR [--av] [--states PREFIX]`, `compare DIR_A DIR_B` |

DOSBox-X records mode 13h as **32-bit pixels** (ZMBV format 8), not as 8-bit frames with a palette
[O, the AVI headers]. A palette load therefore shows as a change of every 16x16 block that uses the
changed colours; `capture_timing.py palette` counts such blocks per frame.

## 9. Inventory

Shots are under `extracted\captures\gg\r2\<area>\`, states under
`extracted\captures\gg\savestates\r2\<area>\` (each with its `.players`), AVIs in
`extracted\captures\gg\avi\` and audio in `extracted\captures\gg\audio\` (names `r2_*`).

### 9.1 Puzzles (`r2\puzzle\`, states `r2/puzzle/`)

The puzzle behind a door is not tied to the door: every entry deals a kind with `random(8)` and a
variant among the first three unsolved ones (§7). All eight were taken from the round 1 state
`w0/15_at_door` (player Ada, **with** chimps, Aircraft building, level 1 = race index 5, warehouse
room 1, the first X door of the bottom floor) with the rand state set so that each kind came out
with **variant 0** (`kK_door`, K = kind). Kinds as numbered by the dispatch table 38e2:00d4.

| K | Kind | Shots `kK_*` | States `kK_*` | Solution of variant 0 (game coordinates) |
|---|---|---|---|---|
| 0 | electric circuits | `00_hint` (first-puzzle hint), `01_screen`, `02_hint_text` (HINT button, text viewer), `03_wire_strung`, `04_solved` | `door`, `screen`, `hint_text`, `wire_strung`, `solved` | take the wire spool from the box (32,145); press with the wire tip on the battery's free end (pointer 136,125: tip = pointer + hot spot 36,8); move; press with the tip on the bulb's free leg (pointer 192,58) |
| 1 | simple machines | `00`, `01`, `02_wedge_placed`, `03_solved`, `04_check_empty` (check with nothing placed: no reaction), `09_hint_text` | `door`, `screen`, `placed`, `solved`, `check_empty`, `hint_text` | wedge from the box (24,88), click it on the nail's tip (178,120), check button (28,156) |
| 2 | machine pieces (axe) | `00`, `01`, `02_head_misplaced`, `03_solved`, `09_hint_text` | `door`, `screen`, `solved`, `hint_text` | handle from the box (47,122), click at (150,80); head from the box, click at (146,59) so that its socket covers the handle's end |
| 3 | gears | `00`, `01`, `02_gear_placed`, `03_crank_wrong_way` (racks move left), `04_one_button` (one button on), `05_solved`, `09_hint_text` | `door`, `screen`, `gear_placed`, `solved`, `hint_text` | gear from the box (56,104) onto the peg (pointer 174,67 = gear centre 196,85); press on the crank (196,128) and move counter-clockwise around (196,112), radius 16, two turns; release |
| 4 | balance | `00`, `01`, `02_weight_carried`, `03_solved`, `04_unbalanced` (weight on the far left hook), `09_hint_text` | `door`, `screen`, `solved`, `unbalanced`, `hint_text` | weight 3 from the box (16,113) onto the left hook 3 (117,50) |
| 5 | launch | `00`, `01`, `01b_ball_on_launcher`, `02_energy4_held`, `03_solved`, `04_energy_other_held` (energy 7), `05_missed` (the ball in hole 7), `09_hint_text` | `door`, `screen`, `energy4_held`, `solved`, `missed`, `hint_text` | drag the ball (37,97) onto the launcher (228,102); press the plunger (283,141), pull to (297,149) = ENERGY 4, release: the flashing hole is the 4th; energy n lands in hole n with normal gravity [O, two shots] |
| 6 | energy forms | `00`, `01`, `02_drawing_line`, `03_matched`, `04_solved`, `09_hint_text` | `door`, `screen`, `matched`, `solved`, `hint_text` | pencil (24,72); click a left dot, then a right dot, for three pairs (mechanical-3, chemical-1, heat-2; dots at x 181 and 219, y 36/70/104); check (26,137). The pictures animate for about 4 s before the message |
| 7 | magnets | `00`, `01`, `02_magnet_carried`, `03_two_placed`, `04_solved`, `05_rotated_placed` (ROTATE turns 90°), `06_repelled` (like poles: pushed apart), `09_hint_text` | `door`, `screen`, `solved`, `rotated_placed`, `repelled`, `hint_text` | magnet from the box (28,56) to (160,83), (184,83), (208,83), unrotated |

Every solved screen adds 400 points [O, `k4_screen` 0 → `k4_solved` 400]. The solved flag
(DS:45d0 + kind·0x42 + variant·2) is still 0 on the solved screen; it is set when the puzzle is left
[O, `k0_solved`]. AVIs: `r2_puzzle_k5_launch.avi` (the launch), `r2_puzzle_k7_repel.avi` (the
repulsion), `r2_puzzle_k4_success.avi` + `.dro` (solve with sound), `r2_puzzle_k5_miss.avi` + `.dro`.

Bob (without chimps) solved further variant-0 puzzles of kinds 1, 2, 3, 4, 5, 6 and 7 at other
doors while collecting parts (§9.2); those door states are the `NN_doorNN_kK` states of
`r2/aircraft/`.

### 9.2 Buildings and races

Player Bob (without chimps), level 1 in each building.

**Aircraft** (`r2\aircraft\`, states `r2/aircraft/`; building 1, race index 5, Level-1 BLIMP;
3 parts needed: envelope, gondola, engine; paint and decal optional).

* Shots 01-08: building hint, blueprint, workshop hint, workshop, warehouse hint, room 1, the
  "gray doors" hint 0x1048 (it came up while walking), the decoration hint 0x1044.
* Collecting: room 1 (two boxes: decal, paint), room 5 through a puzzle door (gondola, hint
  0x104A), room 4 (engine), room 5 again (a second engine type; a third box gave the "two types"
  hint 0x1047, shot 23), room 6 (narrow envelope). One envelope (fat) came from a swapped box (§9.9).
  States 06-29 record the route: door states (`10_room1_door_k4`, `16_door34_k7`, `20_door27_k1`,
  `21_door35_k6`, `27_door30_k5`, `50_door42_k2`, `53_door31_k3`) and arrivals.
* Workshop (§9.3): shots 30-39 and the blueprint pages 46-48.
* Races: with the fat envelope **lost** twice (`40_race_start`, `41_race_mid` = lost hint 0x104F,
  `43_result_lost`; AVI `r2_aircraft_race1_lost.avi` + `.dro`, 58 s); with the narrow envelope
  **won** (`55_vehicle_narrow` → `56_won_rankup` → `57_won_postrace`; AVI `r2_aircraft_race1_won.avi`
  + `.dro`, 50 s: fade, race, fade, podium, count-up, rank-up box).

**Automotive** (`r2\automotive\`, building 0, race index 0, Level-1 DOWNHILL RACER; body, wheels,
brakes): blueprint, body page, workshop, room 1, boxes moved by the tube (§9.9), the three pick-ups,
the build, race won (`11_vehicle_built` → `13_won_rankup` → `14_won_postrace`; AVI
`r2_automotive_race1_won.avi` + `.dro`).

**Alternative Energy** (`r2\altenergy\`, building 2, race index 10, Level-1 PEDAL-POWERED RACER;
body, wheels, brakes, gears): the same steps, race won (`11_vehicle_built` → `13_won_rankup` →
`14_won_postrace`; AVI `r2_altenergy_race1_won.avi` + `.dro`); then **Next Level** (`15_next_level`:
the Level-2 SOLAR-POWERED RACER blueprint, 7 parts needed, race index 11).

Parts chosen by the blueprint pages (named by a word; the number is the record index in the
level's catalogue, `D` 0x2F5 + race index): Aircraft: the narrow envelope, a gondola and an engine;
Automotive: body (3), bushings (6), brakes (8); Alternative Energy: body (3), wheels (7), brakes
(11), gears (15).

### 9.3 Workshop (`r2\aircraft\` 30-39 and 44-55, `r2\automotive\` 10-11, `r2\altenergy\` 10-11)

Workshop with the parts hint 0x1045, conveyor belt, carrying a part, placing it on the floor (the
assembly area), the vehicle growing part by part (`33_envelope_placed`, `34_engine_placed`), the
"finished" hint 0x1053 and the open race-track door (`36_vehicle_built`), **removing** a part
(`37_remove_engine_carried`, `38_engine_back_on_belt`: the race door closes again), the blueprint
from the workshop (`39`), part pages (`46`-`48`, and `03_blueprint_body_page` of the other two
buildings), swapping the engine (`45_vehicle_other_engine`) and the envelope
(`55_vehicle_narrow_envelope`). AVI `r2_workshop_idle.avi` + `.dro` (remove, belt both ways) and
`r2_aircraft_tube_to_workshop.avi` (fade, the parts hint).

### 9.4 Final movie (`r2\movie\`, states `r2/movie/`)

Reached by one poke: `g_racesWonTotal` (DS:5c0e) = 14 in the state `r2/aircraft/55_vehicle_narrow`
(saved as `r2/movie/00_vehicle_ready_total14`); winning that race made it 15 and `State_12` went to
state 15 [O]. Shots: rank-up box (rank Scientist: the win added 300 000 points, §11), movie start,
10 s, 20 s, 35 s, 50 s (Morty gone, the game waits for a key), and the DOS prompt after the key: the
game saves and **exits to DOS** [O]. AVI `r2_movie_race15.avi` + `.dro` (119 s: the race, the
rank-up box, the movie, the wait). `PLAYERS.DAT` before and after the key:
`savestates\r2\movie\06_before_key.players`, `07_after_key.players`.

The movie draws over the results screen and keeps its palette: no palette load happens between the
rank-up box and the movie [O, `capture_timing.py palette` from frame 2900: no fade]. This answers
ARCHITECTURE §10 question 1 (the movie uses the results palette).

### 9.5 Second player, save and resume (`r2\player2\`, `r2\resume\`)

* `r2/player2/01_hub_ada` (round 1's hub with Ada) → the sign-in booth (zone 0xBB8) →
  `02_signin_roster` → New Player → `04_name_typed` ("bob") → Start Game → `05_chimps_dialog` →
  Without Chimps → `06_hub_bob`. `PLAYERS.DAT` differs in 7 bytes from `01_hub_ada.players`: the count
  (offset 6), the name in index entry 1 (offset 0x1E) and in the record of **slot 1**
  (0x8A0 + 1·0x16BD = 0x1F5D) [O]; the chimps word stays 0. Bob took slot 1 because index entry 1
  of the shipped file holds slot 1 (`fmt_data.md` §8.2).
* Save & Exit from a building (`r2/aircraft/26_all_parts`): File menu (`r2\resume\02_file_menu`),
  the confirmation box (`03`, Yes/No, `y` answers Yes), DOS prompt (`04`). `PLAYERS.DAT`
  (`savestates\r2\resume\01_before_save_exit.players` → `04_after_save_exit.players`): 295 bytes
  changed: 26 in the head of slot 1's record (score, live state), 267 in its **Aircraft block**
  (record +0x27E + 1·0x6BA) and 2 in index entry 1 (points) [O].
* Resume: `SSG` typed at the DOS prompt (same DOSBox-X session, clock not reset) → title → sign-in
  (`05`, Bob shown with 1,720 points) → Start Game → hub → Aircraft: the player is back in **room 4
  at the same spot** with all parts, no question, no building hint (`08_aircraft_resumed`) [O]. The
  HUD inventory lists the parts in another order than before the save [O, shots 01 and 08].

### 9.6 Menus and dialogs (`r2\menus\`)

Options menu, Help menu, the first help topic (pager), the Sound and Music question and the New
Player Hints question (states `r2/menus/*`). The Sound and Music question lacks a word ("Do want
...") in the original text; keep it.

### 9.7 Sound runs

| Run | Files | What |
|---|---|---|
| `-si` (PC speaker), fresh boot, title → sign-in (Ada) → hub → Aircraft | `audio\r2_si_boot_to_building.wav` (90.5 s, 48 kHz stereo), `avi\r2_si_boot_to_building.avi` (+ `r2_si_boot_part1/2.avi`, the boot screens), state `r2/sound/si_building`, shots `r2\sound\si_*` | `g_soundDevice` (DS:2452) = 1, `g_fmPresent` (DS:a0d8) = 0 [O]. The speaker shows as a square wave between 0 and about +3300 |
| `-sn` (no sound), fresh boot, title → hub | `audio\r2_sn_boot_to_hub.wav` (64.3 s, all samples 0), state `r2/sound/sn_hub`, shots `r2\sound\sn_*` | `g_soundDevice` = 0 but `g_fmPresent` = 1 [O]; **no key-on**: DOSBox-X wrote no `.dro` (it opens one only at the first key-on; the patch and volume writes that `code_sound.md` §12.3 expects with the sound off are therefore not recorded) |
| OPL (`.dro`) from states | `r2_hub_idle`, `r2_warehouse_walk`, `r2_workshop_idle`, `r2_puzzle_k4_success`, `r2_puzzle_k5_miss`, the three race `.dro`s, `r2_movie_race15`, `r2_postrace_to_hub` | title music: round 1's `w0_*title_attract.dro` |

### 9.8 Other AVIs

`r2_hub_idle` (24 s, pointer moved twice), `r2_warehouse_walk` (13 s: walk right, run, walk
left, idle), `r2_aircraft_door_to_room5` (16 s: through an open door), `r2_door_to_puzzle_k6`
(10.7 s: the puzzle hint and screen; recording began after the door fade), `r2_postrace_to_hub`
(12.9 s: Tech. Center sign → hub).

### 9.9 State edits (complete list)

| State(s) | Edit | Purpose |
|---|---|---|
| `r2/puzzle/kK_door`, every `r2/aircraft/*_door*_kK` | `g_heldKeys` = 4, `g_randState` = `seedpuz K 0 SKIP` | choose puzzle kind K, variant 0 (§7) |
| `r2/aircraft/25_room4_bottom_swapped` | room 4: the parts of the boxes at (195,150) and (275,98) swapped (17 ↔ 0) | an envelope within reach |
| `r2/automotive/06_room1_boxes_edited` | room 1: box (300,130) → part 3 at (45,156); box (220,156) → part 6 at (80,156); room 4 box (35,150) (part 8) relinked into room 1 at (110,156) | parts by the tube |
| `r2/altenergy/06_room1_boxes_edited` | room 1: box (160,26) (part 11) moved to (40,156); boxes of room 7 (195,150, part 3), room 6 (275,150, part 7) and room 4 (115,98, part 15) relinked into room 1 at (62,156), (84,156), (106,156) | parts by the tube |
| `r2/movie/00_vehicle_ready_total14` | `g_racesWonTotal` (DS:5c0e) = 14 | the 15th win |

Box list layout used by `boxset` [O, live memory; it agrees with the world spec's
`Room_RemoveItem` 1622:0d8f]: the room table DS:7a04 holds a far pointer per room; room +0x9E is
the box count (its copy for the displayed room is DS:0964 in `g_room`), room +0xA0 a far pointer to
the list head `{far first, far last}`. A box node is 0x2C bytes: +0 next, +4 prev, +0x0E x, +0x10 y,
+0x12 w, +0x14 h, +0x16 far picture, +0x1A picture resource (0x343 small crate in 6-floor rooms,
0x332 big crate in 3-floor rooms), +0x1E part (catalogue index; 1000 = bananas), +0x22 flags 0x800,
+0x24..+0x2A a copy of x, y, w, h. Box y in 6-floor rooms is 26·floor (floor 5 = 130, floor 6 =
156); in 3-floor rooms 46, 98, 150. A moved box shows after the room is entered again.

## 10. Measurements

All from the AVIs (70.0863 frames/s = 14.268 ms per frame) and `.dro` logs, with
`capture_timing.py`. One PIT tick = 6.866 ms = 0.481 video frames.

### 10.1 Frame pacing [O]

`pace` counts the frames in which a box of the screen changed; a game update caught by the capture
in the middle shows in two consecutive frames and is counted once; the mean interval over a steady
span is the period of whatever updates there.

| What | Where | Mean interval | PIT ticks |
|---|---|---|---|
| score count-up on the results screen (one step per game frame) | `r2_aircraft_race1_won` frames 1578-1669 (score box), 50 updates; `r2_automotive_race1_won` 676-767 | 1.896 / 1.857 frames = 27.05 / 26.50 ms | **3.94 / 3.86 → 4** |
| race (the scrolling course moves every frame) | `r2_aircraft_race1_won` 450-1200 | 3.821 frames = 54.5 ms | **7.94 → 8** |
| walk cycle and spring sparkle in the warehouse | `r2_warehouse_walk` 0-110 and 215-360 | 5.79 frames = 82.6 ms | **12.0** (3 frames of 4 ticks) |
| hub windmill | `r2_hub_idle`, box 480,0,100,70 | 7.73 frames = 110.3 ms | **16.1** |
| final movie | `r2_movie_race15` 5185-6002, 6708-7018, 7190-7423 | 8.69 / 8.61 / 8.63 frames | **18.06 / 17.90 / 17.93 → 18** |

So the hub, warehouse, workshop (the same 12-tick animations) and result loops run at 4 ticks per
frame and the races at 8, as ARCHITECTURE §3.3 says; no frame was seen to overrun in these spans.
The count-up is 50 updates, i.e. 50 frames of 4 ticks = 1.37 s [O].

### 10.2 Palette loads and fades [O]

Every palette load shows as **two consecutive frames**: one frame where a minority of the blocks
change (about 150-260 of 1000) and the next where the rest change (about 800-1000). That is
`Pal_SetRange` (1399:05d6) writing two 128-colour groups on two consecutive retraces. Loads follow
each other back to back (one load every 2 frames). Which group comes first cannot be told from
32-bit captures [I: the code writes 0..127 first].

| Fade | Visible frames | Loads seen | Code (ARCHITECTURE §4.2) |
|---|---|---|---|
| fade to black (`Pal_FadeOut` 1473:0265, n 6) | **22** (11 loads; e.g. `r2_aircraft_tube_to_workshop` 83-104, every race start, `r2_postrace_to_hub` 188-209, round 1's story page 861-882) | 11 | step 11, **12 loads**: after 11 loads every colour is already 0 (each step subtracts ≥ \|d\|/11 + 1), so the 12th writes black again and changes no pixel |
| fade in (1473:02a0, n 8) | **16** (8 loads; e.g. 123-138, 144-159, round 1's title 15-30 and 887-902) | 8 | step 8, **9 loads**: the 9th rewrites the final palette, invisible |

Round 1's "16/19/22+16" are these: 16 = a fade-in, 22 = a fade-out, 19 = a fade-in preceded by two
redraw frames (650, 651, 653).

Frame-exact in wave 2 [O]: `w0_replay_title_attract.avi` frames 15..30 equal the engine's
`Pal_FadeIn` retrace frames 0..15 and frames 861..882 its `Pal_FadeOut` retrace frames 0..21 pixel for
pixel, with group 0 (colours 0..127) loaded on the first retrace of each load; the wave 2 preview
shows every one of them at the same distance from the title's fade-in (`ENGINE_MAP.md`). The code's 12/9 loads are right; 24/18 retraces in all, of which the
last load of each is invisible. In the aircraft races only 10 of the 11 fade-out loads change many
blocks (19 frames with `--min-blocks 600`): the race screen uses few colours of one group.

The time between the last fade-out frame and the first fade-in frame (black, the next screen being
loaded and drawn): workshop → race 7 frames (Automotive, 100 ms), 8 (Alternative Energy), 14
(Aircraft, both runs); race → results 7-11; post-race → hub 8; warehouse tube → workshop 18
(257 ms) [O].

### 10.3 Other transitions [O]

* **Random wipe** (`Screen_RandomWipe` 1473:0042 / `Screen_Wipe` 1473:0479) entering a room through
  an open door: the screen changes on every frame for **45 frames (642 ms ≈ 93 ticks)**
  (`r2_aircraft_door_to_room5` 119-163, a diagonal pattern), no palette change. The pattern is
  **0x11DB** (48 steps of 2 ticks): every frame equals the engine wipe's state after step k or
  between steps k and k + 1 [O wave 2].
* **Door**: the player's door animation before the wipe (frames 0-110 of the same AVI), the arrival
  animation after it (165-208, steps about every 12 ticks), then the player stands in the doorway.
* **Puzzle → room** after a solved puzzle and Enter takes several seconds (the puzzle's fade-out,
  the old room, the door animation, the wipe) [O, states saved 6 s after Enter were still in the
  transition].

### 10.4 Overlay loads

DOSBox-X performs DOS file reads without emulated delay, so an overlay load costs only the
manager's CPU time (copy and relocation) [I]. No stall was visible: a first and a later entry to the
same screen (the Aircraft race: `r2_aircraft_race1_lost` from a state before any race,
`r2_aircraft_race1_won` after two races) show the same 14-frame black gap. The engine need not model
overlay loads; resource loading and decompression happen inside the black gaps of §10.2.

### 10.5 Final movie [O]

Movie frames every 18.0 ticks (§10.1); the three `Delay(1460)` cues (frames 15, 17, 19 of the cue
table, `movie.py --cues`) show as 705-706 unchanged video frames = **10.06-10.07 s** each
(1460 ticks = 10.02 s plus the frame period). The movie runs from frame 2942 to 7423 of
`r2_movie_race15.avi` (64 s), then waits for a key.

### 10.6 Sound [O]

* Every `.dro` starts with about 100 register writes at time 0: DOSBox-X's dump of the OPL state when
  a capture starts. Compare from the first write after them.
* OPL writes fall on a **13.731 ms** grid (72.83 Hz, `capture_timing.py dro` best fit) in all
  eleven round 2 logs, as in round 1: `Snd_Tick` every second PIT tick.
* Hub, warehouse, workshop, races, results and movie have music (rhythm mode: hundreds of BD
  writes). **Puzzle screens have no music**: no write at all for 10 s from a door into a puzzle
  (`r2_door_to_puzzle_k6`: DOSBox-X wrote no `.dro`); only effects (a pick-up, the solve, the miss).
* `-sn`: total silence, no key-on (so no `.dro`, see §9.7). `-si`: speaker only.
* How DOSBox-X records a `.dro` [O wave 2; all 13 G&G captures agree, `src/gg/audio/dro_compare.h`]:
  the capture opens on a B0h-B8h write with bit 5 or on a BDh write with value & 3Fh > 20h; the dump
  lists every non-zero cached register, with the B0h-B8h key bit and the five BDh drum bits masked;
  registers 02h-04h are never cached or logged; after the dump only writes that change a register
  are logged.
* Wave 2: the engine's driver reproduces 12 of the 13 captures write for write (`ggport-cli
  --gg-soundtest-check all`) and the final movie's up to the movie's end (7050 of 8320 writes);
  `fmt_sound.md` §5.

### 10.7 `rand()` and determinism [O]

* Without chimps the warehouse frames call `rand()` never: from a door state of Bob's to the puzzle
  choice there are exactly the 2 calls of the choice (`calls`).
* With the two chimps of Aircraft level 1 (`w0/15_at_door`), 11 calls precede the choice when the
  door is taken on the first frames after the load; the kind is the 12th call, the variant pick the
  13th. The puzzle set-ups of variant 0 call `rand()` no further before their screen.
* Races call `rand()` never (no caller in 3dd6/3e77) and are deterministic: replaying
  `race_automotive` from its state gave **identical** result and post-race screens (0 pixels), and
  542 of 600 race frames identical; the rest are updates caught on the other side of a retrace,
  because the click that starts the race falls on another PIT phase.
* Puzzle replays (`capture_r2.py run puzzle_kK`): kinds 0, 1, 2, 4, 6, 7 and (with the slow pull) 5
  reproduce the solved screen to the pixel. Kind 3 does not reliably: while the crank is held the
  game moves the pointer itself (INT 33h fn 4, 3b2f:09a2) and the outcome depends on where the
  posted moves fall (2 of 4 tries solved). A fast plunger pull in kind 5 gave energy 7: the plunger
  follows the pointer with a lag.

## 11. Behaviour observed (for the specs)

* **Click position = pointer + cursor hot spot.** The press and release latches hold the pointer
  plus the hot spot of the current cursor picture (DS:7a74/7a76), and zones are hit at that point:
  with the wire spool (hot spot 36,8) a press at pointer (138,120) was latched at (174,128) [O,
  `mouse_tsr.py status` before/after]. Carried pieces and gears are placed by their hot spot (the
  gear's centre).
* **Moves dropped while drawing.** In the hub single moves were often dropped (DS:01e2); a press or
  release still carries its own position. Send a move twice before a hover matters.
* **Held keys.** A held Up (DS:0218 bit 4) at the moment the player arrives through a door takes him
  back through that door as soon as another arrow is pressed [O, with the poked Up]. The real
  keyboard does the same if Up is still held.
* **Warehouse controls** [O]: walk about 16 px/s, run (Ctrl) about 54 px/s; Up+Left/Right jumps
  over floor gaps (with Ctrl farther, as the help says); springs (type 4 objects; +0x18/+0x1A hold
  the landing point) throw the player one floor up; those with +0x12 = 1 are not drawn [O] (whether
  they act was not tried); floor gaps drop him a floor (in the 3-floor rooms they are type 5 objects
  42 px wide); full-height pillars stop him;
  boxes are obstacles he can stand on; Down while overlapping a box picks it up; Up in front of a
  door, or at the bottom of the tube. The left and right edges of a room lead to other rooms.
* **Rooms and doors** [O, room objects]: a door object (type 7 locked, 9 open) carries a link id
  (`+0x12 >> 6`) and a target room (`& 0xF`); the target room's door with the same link id is where
  the player comes out; a solved door stays open from both sides.
* **Parts.** The parts counter counts categories still missing; a second type of a category is
  allowed, a third gives hint 0x1047 and stays in the box [O]. Decal and paint count for nothing.
  Pick-up +20 points; finishing the vehicle the first time +100 (removing and refitting adds
  nothing); a solved puzzle +400 [O, scores of the states].
* **Workshop zones** [O, zone lists]: belt arrows 0x3E8 / 0x3E9 (113,35 / 173,35, 34x14), recycler
  0x3EA, blueprint 0x3EB, belt item 0x3EC, the belt area 0x3F1 [I role], race-track door 0x3F0,
  tube 0x148 (the code of the Up key), the floor 0x3EE (72,117, 180x48) only while a part is carried, and one zone
  per placed part with the code 4000 + part index (0xFA0 envelope 0, 0xFA7 gondola 7, 0xFB3 decal
  19). The belt shows two shelves per position.
* **Hub zones** (release codes): 0xBB8 sign-in booth (14,118, 82x63) → sign-in (state 2): this
  answers ARCHITECTURE §10 question 6 for 0xBB8; 0xBB9 Automotive (four rectangles), 0xBBA Aircraft
  (0,1, 104x102), 0xBBB Alternative Energy (two rectangles).
* **Post-race zones** 0x7D0 Rerun (5,82), 0x7D1 Next Level (76,56), 0x7D2 Workshop (140,56), 0x7D3
  Tech. Center (204,56), 0x7D4 Exit (281,62) [O]; after a loss Next Level is greyed, after a win
  Workshop.
* **Rewards** [O]: a lost race 0 points; the first win of a player +4000 (all three buildings); the
  win that made `g_racesWonTotal` 15 +300 000. The reward seems to follow the number of races won
  in all, not the race index [I, two points].
* **The rank-up box comes after every win**, also when the rank does not change (Automotive and
  Alternative Energy at about 4 000 points: "rank of Trainee", next 5,000) [O].
* **Resume** restores the room and position without a question (ARCHITECTURE §2.5) [O].
* **Ending**: after the movie, a key → save → exit to DOS [O].

## 12. How to reproduce

```
python -P tools/tmctl.py start --profile gg --park
python -P tools/gg/capture_r2.py list
python -P tools/gg/capture_r2.py run puzzle_k4 OUTDIR                 # any of puzzle_k0..k7
python -P tools/gg/capture_r2.py run race_aircraft OUTDIR --av        # also race_automotive, race_altenergy, movie
python -P tools/gg/capture_r2.py compare OUTDIR extracted/captures/gg/r2/puzzle   # names differ: compare by hand
python -P tools/tmctl.py stop

# measurements
python -P tools/gg/capture_timing.py palette extracted/captures/gg/avi/r2_aircraft_tube_to_workshop.avi
python -P tools/gg/capture_timing.py pace extracted/captures/gg/avi/r2_aircraft_race1_won.avi --box 500,376,140,22 --from 1576
python -P tools/gg/capture_timing.py dro extracted/captures/gg/audio/r2_hub_idle.dro

# a puzzle of kind K from any door state (SKIP = rand calls before the choice: 0 without chimps)
python -P tools/gg/capture_mem.py door STATE r2/mine/door_kK K 0 SKIP
python -P tools/tmctl.py state load r2/mine/door_kK      # then: key hold:up:100 once the puzzle shows
```

Sound runs boot DOSBox-X themselves (`--args "-si"` / `"-sn"`) and start the WAV capture as soon as
the DOSBox-X menu exists (round 2 used a scratch copy of `capture_tour.py`'s boot thread). The
second player and the resume were driven by hand as §9.5 lists; the states make them repeatable.
`extracted\gg_c\GG\PLAYERS.DAT` now holds Ada and Bob; `tmctl.py start --fresh` restores the
shipped file.

## 13. Still missing

* Chimps in action: banana throwing, chimps taking parts and naps (all round 2 warehouse play was
  without chimps).
* Puzzle variants other than 0, and failure feedback beyond what §9.1 lists (the electric circuit
  and the machine pieces have none visible; wrong pieces return to the box).
* Races of levels 2-5; the post-race Rerun Race and Exit buttons; a lost Automotive or Alternative
  Energy race.
* The 286 path (`cputype=286`), Tandy-less machines and the `-gv` switch.
* Timed hints (0x1046 after 8 s, 0x1048 after 60 s) measured against the clock.
* A frame log of game variables per frame (the TM DOS fidelity tooling's equivalent) for the engine
  checks of wave 4.

Capture requests collected from the wave 1 specs (each names the claim it would confirm):

* Sound (`code_sound.md`): a `-si` capture of the final movie (the silent 4th music pass and its
  3.5-s delay, §13.4); the Aircraft room music on the speaker over 2+ loops (it never changes key,
  §14.1); a hint box with OPL logging (whether the 0x1056 chime is heard at all, §12.1).
* UI (`code_ui.md`): the Help > About the Game pager pages; the Ctrl+R removal box; the
  duplicate-name box (New Player, then an existing name); a roster of more than 8 names (the scroll
  bar); the Options Yes/No boxes; a pressed picture button.
* Puzzles (`code_puzzles_a.md` §10, `code_puzzles_b.md` §10): a solved electric circuit with a
  switch; an energy check with a wrong line; a launch with a surface change (the 19-tick DAM step and
  the 6-tick redraws); the simple-machines reveal; the reward waits on the speaker; a gears jam (the
  forward-and-back flick) and a drag that leaves some buttons up (the reset); a slow balance swing;
  horseshoe magnets (levels 9+); a multi-piece group slide in machine pieces.
* Vehicles (`code_vehicles_races.md` §19): a state at the start of race 14 (the uninitialised local
  of `Race_ComputePerformance`); the rematch double count (win, lost rematch, warehouse, win); races
  of levels 2-5 against the test vectors of §18.
* World (`code_world.md` open questions): the hall pads and flaps in use (player sequences 54..67,
  chimp sequences 36..41); a banana stun timed per level.
* Platform (`code_runtime.md` open questions): a resident-code frame logger for the costs of
  picture loads, full flushes and wipe steps on a slower reference machine.
