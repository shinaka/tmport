# Gizmos & Gadgets reference captures (DOSBox-X)

Wave 0 of the G&G port. This document records how the original game (*Super Solvers: Gizmos &
Gadgets!*, DOS v1.0, `SSG.EXE`) runs in DOSBox-X without touching the user's desktop, how guest
mouse input is injected (the game cannot be played without the mouse), what was captured, and what
was seen while doing it. Captures, AVIs and save states are under `C:\Git\TM\extracted\captures\gg\`
(gitignored); this file only describes them.

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

### 3.2 `w0_replay\`: reproducibility

`tools\gg\capture_tour.py run door` (a second fresh boot) and `run puzzle` (from the state
`w0/17_puzzle`) wrote the same 25 frame names. Against `w0\`:

| Frames | Differing pixels | Why |
|---|---|---|
| 02-06, 08-12, 16 | 0 | deterministic: fixed clock, `rand()` not called before the warehouse (the rand state DS:442a still equalled the seed in the hub) |
| 01, 07, 13 | 62 452 / 860 / 100 | taken at another moment of an animation (the title's fade-in, small animations in the hub and the warehouse) |
| 14, 15 | 1 116 / 1 840 | the chimp elsewhere: the Cyber Chimps call `rand()` every frame, so the warehouse depends on the real-time pace |
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
  15-30 of the replay AVI); title fade-out 19 frames (650-668); story fade-in 22 frames (861-882),
  then 16 more frames of loads (887-902) after a 4-frame pause. These lengths are lower bounds (a
  load that changes no visible pixel is not seen). The fade step counts of
  `ARCHITECTURE.md` (fade-out 12 loads, fade-in 9 loads) are for wave 1 to reconcile with this.
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
