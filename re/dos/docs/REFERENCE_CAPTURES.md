# DOS edition reference captures (DOSBox-X)

Scoping pass for a DOS "edition backend". This document records how the original 1990 DOS game
(`SST.EXE`, "V 1.0", Turbo C 2.0, EXEPACK) was run in DOSBox-X without touching the user's
desktop focus, how it picks its graphics and sound modes, what was captured, and what was seen
while playing. Captures and AVIs are under `C:\Git\TM\extracted\captures\dos\` (gitignored); this
file only describes them.

**Observed** means seen in a capture, an AVI frame, a file diff or the disassembly at the cited
address. **Inferred** marks conclusions drawn from those observations.

All addresses are in the Ghidra export `re\dos\ghidra_export\` (program `SST_unpacked.EXE`).
File offsets are into `extracted\SST_unpacked.EXE`. The MZ header is 0x21f0 bytes, so
`SSSS:OOOO` maps to offset `0x21f0 + (SSSS-0x1000)*16 + OOOO`. DGROUP is segment `4d94`, at file
offset `0x3fb30`.

## 1. Setup

| Item | Value |
|---|---|
| Emulator | DOSBox-X 2026.10.01 (`tools\dosbox-x\`), the same build as the Windows profile |
| Config | `tools\dosbox_tmdos.conf`: `machine=ega` (the default), `memsize=16`, `cycles=fixed 6000`, SB16 (with OPL), `[mixer] nosound=true` (devices are still emulated, but nothing plays on the host speakers), `windowposition=-4000,100`, `saveremark=false`, `forceloadstate=true`, `show recorded filename=false` |
| C: drive | `extracted\dos_c\`, a writable copy. `extracted\dos_c\TM\` is a byte-identical copy of `TM-DOS\*` (checked with `diff -r` before the first run). `TM-DOS` itself is never mounted. |
| Start-up | The autoexec runs `C:\RUN.BAT`. `tmctl` rewrites that file on every DOS start as `cd \TM` followed by `SST <args>`. |
| Driver | `tools\tmctl.py --profile dos` (additive; the default profile is still `win` and behaves exactly as before) |

Commands (bash, from `C:\Git\TM`):

```
python -P tools/tmctl.py start --profile dos --park                     # EGA (machine=ega)
python -P tools/tmctl.py start --profile dos --machine tandy --park     # Tandy 1000
python -P tools/tmctl.py start --profile dos --machine mcga --park      # MCGA (or --machine cga ...)
python -P tools/tmctl.py start --profile dos --args "-T" --set "cycles=fixed 3000"   # explicit switches
python -P tools/tmctl.py key enter text:ada enter        # same key syntax as the Windows profile
python -P tools/tmctl.py shot OUT.png                    # EGA: 640x400 (2x2 doubled); Tandy/CGA/MCGA: 320x200
python -P tools/tmctl.py state save ega/castle           # -> extracted/captures/dos/savestates/ega/castle.sav + .users
python -P tools/tmctl.py state load ega/castle           # also restores the matching USERS.DAT copy
python -P tools/tmctl.py stop
```

- `start --profile dos` writes `extracted\captures\dosbox.profile`. Later `state` commands pick
  the DOS state library and the DOS `USERS.DAT` path from that file. `stop` deletes it.
- `--machine M` becomes `-set machine=M`. `--set K=V` passes any other option the same way.
- Raw `save N` / `load N` slots 1..9 live in `tools\dosbox-x\bin\x64\Release\save\` and are
  **shared** with the Windows profile. I used only the named state library, which goes through
  slot 10 the way the Windows profile does.
- A state saved in one DOSBox-X process loads fine in a fresh process (observed several times:
  `state load ega/level2_after_tunnel` right after `start`).
- Focus: nothing activated a window during the session. The window stays parked at
  (-4000,100); `--park` re-parks it for 45 s while the guest changes video mode.
- To reset the game data, copy `TM-DOS\USERS.DAT` over `extracted\dos_c\TM\USERS.DAT`. A copy of
  the shipped file is also at `extracted\captures\dos\original\USERS.DAT`.
- Harmless log line (observed, not explained): on every start, `dosbox.log` contains
  `INT 21h WRITE warning: Detected 'Packed file is corrupt' message`. The game starts and runs
  normally every time, so the EXEPACK stub did not abort. Cause unknown.

`INSTALL.BAT` is a third-party copy/installer batch file. It copies the floppy contents to a
target directory and prints `To play, type "RUNME"`, but there is no RUNME file. It says nothing
about video or sound modes. The game is started as `SST`.

## 2. How the game picks its graphics and sound modes

Everything below is **observed in the disassembly** unless marked otherwise.

**Command-line switches.** `1460:0463` (called from `1460:0080` in the init routine `1460:0003`)
walks `argv`. An argument that starts with `-` or `/` has its second character looked up in a
14-entry table at `1460:04c2` (file offset `0x6cb2`): `C E H M S T X c e h m s t x`. The jump
targets follow at `1460:04de`.

| Switch | Effect (target) |
|---|---|
| `-E` | video type `[4d94:912c] = 1` (EGA) (`1460:04fa`) |
| `-C` | video type 2 (CGA) (`1460:0502`) |
| `-T` | video type 3 (Tandy) (`1460:050a`) |
| `-H` | video type 4 (Hercules) (`1460:0512`) |
| `-M` | video type 5 (MCGA) (`1460:051a`) |
| `-X` | toggles `[4d94:923c]` (purpose unknown) (`1460:0522`) |
| `-S<c>` | sound type `[4d94:63bc]`: `<c>` is looked up in the string at `4d94:0620` (file `0x40150`), `iItTaAsS`. The value table at `4d94:0629` gives `i`/`I`→0, `t`/`T`→1, `a`/`A`→2, `s`/`S`→2 (`1460:0529`) |

**Video auto-detect** (`1000:05bf`, which runs before the switches are parsed):

1. The BIOS machine ID at `F000:FFFE` is 0xFF and the byte at `F000:C000` is `'!'` → Tandy
   (type 3). This test is in `1000:04a1`, which sets `[4d94:1300]='!'`.
2. INT 10h AX=1A00h (display combination code) is supported: DCC ≥ 0x0B → MCGA (5); DCC 4 or 5
   → EGA (1), or CGA (2) if INT 10h AH=12h BL=10h returns BL=0 (64 KB EGA); DCC 2 → CGA (2);
   DCC 1 (MDA) → no adapter.
3. Anything else, including VGA (DCC 7/8), runs a Hercules retrace test on port 0x3BA (sets 4),
   then the EGA BIOS query (INT 10h AH=12h BL=10h; BH≤1 and CL≤0x0F → EGA). Failing that, a CRTC
   register test at 0x3D4 → CGA, or Tandy when `'!'` was found.
   **Inferred:** VGA and SVGA cards run the game in EGA mode. Only a real MCGA (or `-M`) gets
   type 5.
4. Still -1 → the "no graphics adapter" message at `4d94:0693` and exit.

**Sound auto-detect** (`1000:04a1`): Tandy machine → 1. Otherwise the AdLib timer probe
`4ac9:0d0f` succeeds → 2. Otherwise INT 15h AH=C0h reporting model/submodel `0xFC/0x0B`, plus a
port 0x203 echo test → 3 (**inferred:** IBM PS/1 audio). Otherwise 0 (PC speaker). The data files
`TUNES.IBM`/`SFX.IBM` and `TUNES.TAN`/`SFX.TAN` go with these types. **Inferred:** `.TAN` is the
Tandy/SN76489 data; how AdLib maps onto them was not checked.

**Timer:** `1000:03c5` hooks INT 08h and programs PIT channel 0 with divisor `0x13B1`
(`1000:03f8`), about 236.7 Hz. `1000:0407` restores divisor 0 on exit. The game paces itself
from this timer, so the emulated CPU speed does not change gameplay speed (observed: EGA and
Tandy runs have identical tick timing, see §5.9).

**What the captures used:** auto-detect only, with no switches. `machine=ega` gave EGA,
`machine=tandy` gave Tandy, `machine=mcga` gave MCGA (a different palette, see §6) and
`machine=cga` gave CGA. Sound was emulated but muted on the host.

## 3. Native resolution and aspect ratio

- All four modes produce a **320x200, 16-colour frame** (CGA: 4 colours). Observed sizes:
  - DOSBox-X saves EGA (`machine=ega`) screenshots and AVIs at **640x400**, with every pixel an
    exact 2x2 block. The curated PNGs in `ega\` are reduced to 320x200 after a lossless check
    that the doubling is exact. The untouched DOSBox-X files are in `ega\raw\`.
  - Tandy, CGA and MCGA screenshots and AVIs are 320x200 already (`raw\` copies are identical).
- Refresh, from the AVI headers: EGA 59.92 Hz, Tandy 59.92 Hz, MCGA 70.24 Hz. Wave 4: the EGA AVIs
  hold more frames than that: **60.29 frames per emulated second** by their audio tracks
  (`ega_r2_end_t2_nokey` 2991 frames in 49.613 s, `ega_r2_crown_nokey`, `ega_castle_ladder_climb`) and
  by the frame-by-frame fits of the engine's per-tick pictures (`tools/dos/avifit.py`: k = 47.339 /
  60.29 game ticks per frame puts 1216 of 1224 frames of `ega_r2_end_t2_nokey` on their tick); the
  Tandy AVIs run at their header's 59.92. Single recordings drift around that (the tierc
  standing-elves AVI fits 0.7946 ticks per frame, the level 2-3 AVIs -0.45..-0.92 %), so a timing
  taken from an AVI is fitted per recording; durations measured from EGA AVIs at 59.92 Hz are about
  0.6 % long.
- Aspect: DOSBox-X logs `pixratio 1.200` with `Aspect ratio: 640 x 480` for the game's mode.
  Pixels are 1.2x taller than wide, so the intended display is 4:3 (320x240 with square pixels).
  None of the captures applies aspect correction (`aspect=false`).
- EGA palette in the captures: the 16 standard RGBI colours (`000000 0000aa 00aa00 00aaaa aa0000
  aa00aa aa5500 aaaaaa 555555 5555ff 55ff55 55ffff ff5555 ff55ff ffff55 ffffff`). Every EGA and
  Tandy frame captured uses only these colours.

## 4. Capture inventory

Root: `extracted\captures\dos\`. Each `<mode>\NNN_name.png` is the native 320x200 frame.
`<mode>\raw\` holds the DOSBox-X originals. A few frames were taken from AVIs or from the
fast-grab loop (see §7), and those have no separate raw copy where none existed.

### 4.1 `ega\` (104 frames): a complete climb, from the title to the castle

| Files | Shows |
|---|---|
| 001-003 | Title page; story page **2** (the file is named `002_story1.png`, but it shows the second page with the S key line, `code_ui.md` §16); the walk-off (title redrawn without "Press Enter" while the Super Solver walks up the path) |
| 004-007 | Club Sign-In list (shipped "Rascal" + New Player); Enter New Player Name dialog (empty, then "Ada" with a block cursor); first in-game frame: clubhouse with Flutter's first New Player Hint |
| 008-009 | Clubhouse interior (rank button = chest picture, prize button = arrows picture, door, bench); hint about the arrow keys |
| 010-020 | Help menu (F1); About the Game pages 1-6; Using the Keyboard pages 1-4 |
| 021-023 | File menu (F2); Options menu (F3); Music Yes/No dialog |
| 024-029 | Outside the clubhouse (level 1); net hint; net swing; net on the ground; elf caught (coins +1, net -1); after the catch |
| 030-032 | Quiz screen (cyan panel, three answer choices, red answer box); "Yes! That's right" bubble with the new clue word; clue word added to the panel |
| 033-036 | Path with flags ("Press ↓ to drop coin" in the message box); coin toss; POOF cloud (nothing found); the hiding place blinking |
| 037-043 | More of the level 1 path: cave, plants, an elf throwing dust, two tall "ELF XING" signs, the NETS 4 COINS net cave, the locked tree-trunk gate, the path looping back to the clubhouse |
| 044-046 | Quizzes 2 and 3; coin hint |
| 047-050 | Coin dropped at the key place: POOF, the place blinks and a key appears, the key icon turns yellow, key-found hint |
| 051-057 | At the lock; the trunk turns into a ladder; climbing the trunk, the treetop and a rope ladder (vertical scrolling); arriving on level 2 |
| 058-067 | Level 2: path, "FLYING FISH" signs, a cave gated by a portcullis (the lock), a wooden doorway, a tree with two big nests, at the doorway, the dissolve transition, inside the mine tunnel, the tunnel exit (another doorway), the level 2 net cave |
| 068-069 | Clubhouse rank screen (treasure chest with thresholds 300/230/170/115/70/25/5, rank "Trainee", Total Treasures 0); prize button with no prizes yet (the player looking up at the prize picture, the frame before the treasure room opens; wave 4) |
| 070a-c | File > Exit and Save; confirm dialog; Yes (the game then returns to the DOS prompt) |
| 071-077 | Level 2 quiz; worn-out-net hint; net cave: the rock counter goes 4→3→... and the rock slides open; net repaired (0→5); three clue words; level 2 key found (key icon turns pink) |
| 078-081 | Level 2 lock opened: a geyser cloud lifts the player (vertical scroll) while the clue box is redrawn (whole, when g_keyState changes at the level change, `code_ui.md` §11.3; 080 is a torn present: its rows from 166 still show the old box, the AVI replaces the box within frames 520-521); arriving on level 3 (snow) |
| 082-088 | Level 3: shovels and sleds, snowmen and a rope bridge over a gap, the bridge, the stone arch with a blue portcullis and keyhole (the castle lock), an ice doorway, snowballs, a level 3 quiz |
| 089-093 | Walking onto the geyser top on level 3 drops the player back to level 2; inside the ice cave (a slide-down tunnel); falling down a rock shaft; landing on level 2 at the mine-tunnel exit; the open level 2 gate with the geyser |
| 094-096 | Level 3 clue words; key found (key icon turns green); arch opened |
| 097-101 | Castle hall; ladder; ladder shaft with several ladders; jumping between ladders; **castle-top end screen** (scene 3, CLOSE.PAN, cells A8–AA: Master of Mischief with the crown in a window, treasure chest, chute, urn, sparkles; not the treasure room of scene 4) |
| 102-104 | Rank chest screen shown after the castle (no menu bar); back in the clubhouse with the reached-the-top message; File-menu hint |

### 4.2 `tandy\` (22 frames)

Title; story pages 1 and 2 (page 2 adds the S key line for the series page); the other
Super Solvers titles page; walk-off; sign-in; the saved-game prompt; the
resumed level 2 game (from the USERS.DAT saved by the EGA run); Help menu; About page 1; the Esc
exit dialog; an elf caught on level 2; File menu; Start New Player confirmation; sign-in again;
the new name "Ben"; clubhouse hint; clubhouse; controls hint; outside (different scenery from
Ada's game, §5.10).

### 4.3 `mcga\` (5) and `cga\` (6), extra modes for comparison

MCGA: title, story page 2, walk-off, sign-in, a level 2 game frame. CGA: title, story pages 1
and 2, walk-off, sign-in, a level 2 game frame.

### 4.4 AVIs (`avi\`, ZMBV; decode with `tools\avi_frames.py`)

`ega_opening_title`, `ega_signin_idle`, `ega_net_swing`, `ega_net_swings_repeated`,
`ega_walk_and_scroll`, `ega_coin_drop_nothing`, `ega_coin_drop_key_found`,
`ega_lock1_open_climb`, `ega_climb_to_level2`, `ega_doorway_dissolve`, `ega_net_cave_fix`,
`ega_gate2_geyser_up`, `ega_lock3_arch_to_castle`, `ega_castle_ladder_climb`,
`ega_castle_ladder_jump_top`, `tandy_opening_title`, `tandy_attract_story2_ad`,
`tandy_attract_full_cycle` (150 s, a whole attract loop), `tandy_net_catch`,
`mcga_opening_title`.

Where some of them start (wave 4, matched frame by frame against engine runs from the states):
`ega_walk_and_scroll` is `ega/outside_clue1` with Right held from tick 83 to 273 (`ega/path_flags` was
saved 2267 ticks later in the same session); `ega_coin_drop_nothing` starts at the `ega/path_flags`
screen; `ega_net_swings_repeated` at `ega/outside2` (swings starting at AVI frames 43, 165, 286, 406,
528, 649); `ega_r2_jumps_path` is the run of the log `r2/fl_jumps`; `ega_r2_bonus_coins` the run
`ega/w_bonus` -> `r2/w_bonus_after`; `ega_r2_coin_creature_touch` ticks 1339-1589 of
`r2/fl_tierc_coincreature` -> `fl_coin_after` (`r2_coin_creature_03/04` = AVI frames 155/193);
`ega_r2_tierc_standing_elves` from tick 408 of `r2/fl_tierc_outside` -> `fl_tierc_dust1`
(`r2_elf_dust_01..03` = AVI frames 1485, 1491, 1510). `r2_w_bonus_02` and `_03` are identical
pictures. The scenario files of `tools/dos/scenarios/` give the key times found for each run.

### 4.5 USERS.DAT snapshots (for the format checks)

| File | State of the game data |
|---|---|
| `original\USERS.DAT` | Shipped file (3243 bytes, one player "Rascal") |
| `USERS_after_signin_Ada.DAT` | Right after creating player "Ada" (EGA). 75 bytes differ from the original (offsets 1-2, 363-365, 501-548, 598-606, 687-720 ...) |
| `USERS_after_exit_level2_Ada.DAT` | Ada after Exit and Save on level 2: coins 14, net 5, treasure 0, no clue words, level 1 passed. 151 bytes differ from the sign-in copy |
| `USERS_after_tandy_newplayer_Ben.DAT` | The previous file, then (Tandy) Ada continued and caught one elf (coins 15, net 4), then File > Start New Player (which saves Ada) and player "Ben" created |
| `USERS_after_castle_climb_Ada.DAT` | Starting from the sign-in copy: Ada climbed all three levels and reached the castle's treasure room, then (Enter, Enter) saw the rank chest screen and came back to the clubhouse. Copied from the state `ega/after_castle`. 155 bytes differ from the sign-in copy |
| `USERS_after_exit_post_castle_Ada.DAT` | The previous state (in the clubhouse: coins 31, net 0, 0 treasures), then Exit and Save. 115 bytes differ from the previous file (offsets 1-2, 465-500, 550-663, 693, 695, 705) |

Observed write points: sign-in of a new player; the end of a climb (somewhere between the
treasure room and the clubhouse message; the state `castle_top` still had the old file and
`after_castle` the new one); Exit and Save; Start New Player (its dialog says the game will be
saved; that write was followed by Ben's sign-in and the two were not separated). Nothing was
written while playing
the levels: the copies stored with `l3_clue3`, `l3_at_arch`, `castle` and `castle_top` are
identical to the sign-in copy. The file stays 3243 bytes.
File timestamps are not meaningful: `state load` restores the copy with its old timestamp, and
writes from the guest seem to carry the guest DOS clock, which goes back to the time in the
state.

### 4.6 Save-state library (`savestates\`)

EGA: `signin shack_hint1 shack outside1 outside2 outside_caught1 quiz1 outside_clue1 path_flags
path_plants path_cave2 loop_clubhouse clue2 clue3 at_signs key1 at_lock level2_start level2_gate
level2_doorway tunnel_inside level2_after_tunnel l2_clue1 l2_netfixed l2_clue2 l2_clue3
l2_at_nests l2_key l2_at_gate level3_start l3_ice_doorway l3_clue1 l3_ice_tunnel l2_netfixed2
l3_back_net5 l3_clue2 l3_clue3 l3_at_tree l3_key l3_at_arch castle castle_shaft castle_top
after_castle`. Tandy: `signin level2_resumed level2_caught outside`. Each `.sav` has a matching
`.users` copy of USERS.DAT.

## 5. Observed behaviour

### 5.1 Attract loop and sign-in

- Pages: title → story 1 → story 2 → "other Super Solvers adventures" → title ... Each page
  stays up for **1826 frames ≈ 30.5 s** (measured in `tandy_attract_full_cycle.avi`; the EGA
  title also lasted about 31 s). Small sparkles animate on the mountain the whole time.
  (Wave 4: 1825.5 to 1827 frames by page, 7201 PIT ticks plus the page's draw, 7304 frames = 5770
  game ticks per cycle of four; the sparkles' order follows the rand() sequence: `code_ui.md` §8.5.)
- Enter skips to the next page (CGA test: title → story 1 → story 2). Enter on story page 2 starts
  the walk-off and then the sign-in. (Round 1 thought the EGA walk-off followed an Enter on
  story 1; the frame taken as story 1, `ega/002`, is story page 2, and the code advances one page
  per Enter, `code_ui.md` §8.3.)
  `S` on story page 2 is offered for the series information page.
- The sign-in list shows name, rank and score. ↑/↓ move the highlight and Enter selects. "New
  Player" opens a name box with 8 character cells (shown as underscores), a block cursor, and Esc
  to go back.
- **Shifted keys are dropped:** `shift+a` typed nothing (twice), and the game capitalises the
  first letter by itself ("ada" became "Ada"). Type names in lower case.
- A player with a saved game gets "You have a saved game ... continue?" (Yes/No). Yes resumes at
  the saved position. The scroll position and facing on resume are not identical to those at
  the save (observed: player facing front, screen scrolled differently).

### 5.2 Controls (from the in-game "Using the Keyboard" pages and testing)

←/→ walk; ↑ enter a tunnel or cave, open a lock with the key, or climb; ←+↑ / →+↑ flip (jump)
left or right; Space drops the net; ↓ drops a coin or climbs down; F1/F2/F3 open the Help, File
and Options menus; ↓/↑ cycle menu items or move the answer box; Enter selects; Esc quits. In
play, Esc opens the Exit and Save dialog (observed). In the clubhouse, ↑ at a picture opens the
rank or prize display, and ↓ walks away.
While a Flutter hint bubble is up, every key except Enter is ignored (F1 did nothing).
The mouse is supported according to the help menu, but it cannot be driven from tmctl.

### 5.3 Screen layout (native 320x200, measured in `ega\008_shack.png`)

- Rows 0-9: menu bar, three white tabs ("Help F1", "File F2", "Options F3") ending at x=168,
  black to the right. The active menu's tab turns red (light red) while its menu is open.
- Rows 11-13 and row 146: dark-blue (0000aa) frame. Columns 0-1 and 318-319 are frame too.
  Playfield content is rows 14-145, columns 2-317.
- Rows 147-199: status panel on dark blue, three boxes with light-blue (5555ff) edges at
  x = 2..116 ("Clue Words:", a key icon and three ruled lines), 119..214 (message box: "Press
  [↓] to drop coin.", "Press [Enter] to go on.", "↓ or ↑ to Move Box / Enter to Select"), and
  217..318 (Coins, Net, Treasure, each an icon over a number).
- Dialogs are white boxes with a black border, a title bar and an "[Esc] to Go Back" footer. Hint
  bubbles are white clouds pointing at Flutter (a yellow butterfly) on the right.
- The rank screen and the castle rank screen hide the menu bar.

### 5.4 Movement and scrolling

- Walking: the player moves **8 px per step, one step every ~6.5 frames ≈ 108 ms** (59.92 Hz
  AVI, `ega_walk_and_scroll.avi`).
- The playfield **scrolls horizontally in 8-px steps** (the frame-to-frame shift matches
  exactly, 100% of pixels), once per walking step, when the player passes a threshold. Walking
  right, the player stays at about x=232..272 (centre ≈252) and the world scrolls. Walking left,
  the player first moves to x≈65 (centre) and then the world scrolls. Each scroll step is redrawn
  in two bands over two consecutive 60 Hz frames, so mid-step frames show tearing.
- Climbing (ladders, the trunk, the geyser) **scrolls vertically in 8-row steps, one every ~9.2
  frames ≈ 153 ms**. The player keeps climbing only while ↑ is held, and has to climb off the
  top of a ladder with ↑ before ←/→ work again.
- Each level is a horizontal **loop**: walking right on level 1 passes flags, a cave, plants,
  signs, the net cave and the gate, and comes back to the clubhouse.

### 5.5 Elves, net and quizzes

- Elves run, stand and turn. One kind (blue cloak) throws dust. Their animation advances every
  ~9.1 frames ≈ 152 ms (EGA and Tandy alike).
- Space swings the net **in the facing direction**. It lands about 33..73 px in front of the
  player's centre roughly 0.6 s after the key press, and the whole swing lasts ≈2 s. A miss costs
  nothing (the net stayed at 10). A catch gives **+1 coin and -1 net**.
- Some catches turn into a quiz instead: a full-playfield cyan panel with the question, three
  choices and a red box (↓/↑, Enter). Correct answers show one of three praise messages ("Yes! …",
  "Great job, …", and a longer "Good job, …" one that announces the clue word). Each gives a clue
  word and +2 coins. Question kinds seen: addition and subtraction word
  problems, first-sound or starting-letter words, a number sequence, sentence completion, and
  odd-one-out by category.
- Clue words fill **fixed lines by category** (observed on all three levels): line 1 is a number
  (two, four), line 2 an attribute (tall, big, small) and line 3 an object (signs, nests,
  pinecones). Clue words belong to a level: they are wiped when you go up, and come back when
  you return to a level (seen after sliding back down to level 2).
- New game: coins 5, net 10, treasure 0. A net at 0 gives the worn-out-net hint. The
  net cave rock reads "NETS 4 COINS". Each coin dropped there lowers the number, the rock slides
  aside after the fourth, and ↑ refills the net to 5 (seen twice, from 0).

### 5.6 Coins, treasures and keys

- ↓ throws a coin ahead (-1 coin). A POOF cloud follows, then the hiding place (the scenery
  object) **blinks** by being redrawn and erased every frame for ~0.3 s. Nothing found: the
  object is simply redrawn. The key place (it matches all three clue words): a key sprite
  appears and the panel's key icon changes colour. The level key colours are **yellow (1), pink
  (2) and green (3)**. Key places found: two tall ELF XING signs; a tree with two big nests; a
  small fir tree with four pinecones.
- No treasure was found in this session (no 2-of-3 places were tried), so the treasure and
  star-rank celebrations were **not captured**.

### 5.7 Locks, tunnels and level changes

- Level 1 → 2: the lock is a tree-trunk door with a keyhole. ↑ with the key turns the trunk into
  a ladder, and the climb scrolls up through the treetop and a rope ladder onto level 2.
- Level 2 → 3: the lock is a cave with a portcullis. ↑ removes the portcullis and a cyan geyser
  cloud (with green sparkles) carries the player up. The gate stays open: ↑ there carries you up
  again, and walking onto the geyser top on level 3 drops you back to level 2.
- Level 3 → castle: the lock is a stone arch with a blue portcullis. The castle hall has a
  ladder, which leads to a shaft of several parallel ladders. →+↑ jumps to the next ladder
  (the ladder-jump hint text), and the top of the shaft is the treasure room. Then
  comes the rank chest screen, then the clubhouse with the reached-the-top message,
  which says to leave through the door for another game. The clue words and key are reset.
- Wooden doorways on level 2 (↑) lead into a mine tunnel. Walking through it comes out at
  another doorway elsewhere on the same level. The cut into the tunnel is a **block dissolve**
  over ~7 frames (~120 ms) in 20x8-px cells (10 bytes x 8 rows, `code_world.md` §8.6;
  `ega_doorway_dissolve.avi`, frames 74-81).
- The level 3 ice doorway is a **slide-down tunnel**: an ice cave, then a fall down a rock shaft,
  then landing on level 2 at the mine-tunnel exit. The slid-down-a-tunnel hint
  was not shown, presumably because hints only appear on first occurrences (inferred).
- The gap and the rope bridge on level 3 were crossed by walking (the bridge was present). The
  "flip over gaps" and elf-dust dodging were not exercised.

### 5.8 Menus, help, rank

- Help: About the Game (6 pages), Using the Keyboard (4 pages), Using the Mouse, Getting More
  Hints. Pages have "See Next Page", "Go Back a Page" and "Done" buttons. After Done the help
  menu comes back with the highlight reset to the first item.
- File: Start Over, Start New Player, Exit and Save (each with a confirmation; No is the
  default, ↑ selects Yes). Options: New Player Hints, Music, Sound (Yes/No dialogs).
- Rank screen (chest picture in the clubhouse): the chest is filled to the rank thresholds 5,
  25, 70, 115, 170, 230, 300, with the name and "Total Treasures".
- After Exit and Save the game returns to the DOS prompt (`C:\TM>`) in text mode.

### 5.9 Timing summary (all measured from AVIs at 59.92 Hz; the EGA AVIs run at 60.29, §3: their
durations are about 0.6 % shorter than given)

| Thing | Period |
|---|---|
| Attract page | 1826 frames ≈ 30.5 s |
| Walk step / horizontal scroll step (8 px) | ~6.5 frames ≈ 108 ms |
| Climb step (8 rows) | ~9.2 frames ≈ 153 ms |
| Elf / net animation frame | ~9.1 frames ≈ 152 ms (identical on EGA and Tandy) |
| Net swing, key press to net on the ground | ≈0.6 s; whole swing ≈2 s |
| Doorway dissolve | ~7 frames |

### 5.10 Randomness

Player "Ben" (new game, Tandy) had different scenery outside the clubhouse (bushes and white
lilies) from player "Ada" (sunflowers both sides). **Inferred:** the hiding-place objects on the
path, and with them the clue/key solutions, are placed at random for each new game. The fixed
structures (clubhouse, net caves, locks, doorways) looked the same.

## 6. EGA vs Tandy (and MCGA/CGA)

- **EGA and Tandy frames are pixel-identical.** Compared 320x200 frames: the clubhouse with its
  first hint (`ega\007` vs `tandy\020`), the clubhouse (`008` vs `021`) and the controls hint
  (`009` vs `022`) differ by **0 pixels**. The title differs only in 43 pixels of a sparkle
  animation: the sparkle frame differs (wave 2: EGA spot 0 showing BEGIN frame 40, Tandy spot 0
  frame 38). The mouse pointer (BEGIN frame 2) is in every title capture, at the mouse driver's
  initial position: EGA and Tandy (159, 101), MCGA (158, 82), CGA (159, 146) (x in bytes, y in
  rows). The help dialog and the status panel are identical where the scene behind them is
  the same. Same 16 colours, same layout, same tick timing.
- Differences are outside the image: DOSBox-X captures EGA at 640x400 and Tandy at 320x200, and
  the sound hardware differs (type 1 Tandy versus type 2 AdLib; nothing was listened to).
  **Inferred:** a DOS backend needs only one 16-colour renderer for both.
- **MCGA (video type 5)** uses the same pixels with a **different 16-colour palette**. Mapping
  observed on the title (each EGA colour → one MCGA colour): `00aa00→049e00`, `5555ff→006db6`,
  `55ff55→00e700`, `aa00aa→9e009e`, `aa5500→b65500`, `aaaaaa→9a9a9a`, `ff5555→ff7900`. The other
  9 colours are unchanged. These are DOSBox-X's 8-bit expansions of 6-bit DAC values, so
  0x6d≈27, 0x9e≈39, 0xb6≈45, 0xe7≈57 and 0x79≈30 (inferred). It runs at 70 Hz (mode 13h-like).
  The palette table in the EXE was not looked up.
- **CGA** uses palette 1 high intensity (black, cyan, magenta, white). The 16-colour art is
  converted at run time to 4 colours with dithering: each drawing routine converts its 4-bpp
  colours through the tables of `Gfx_BuildCgaColorMap` with its own row phase (tiles alternate from
  their first row, fills use the even tables on every row, sprites alternate from their first drawn
  row, text turns its colour pattern by one pixel per glyph row; `fmt_graphics.md` §2.4, wave 4).
- Hercules (type 4) was not captured.
- Wave 4 (looks agent): every Tandy, MCGA and CGA capture is reproduced through the engine's game
  with 0 differing pixels (`fmt_graphics.md` §11.1). The rosters of the capture runs: Tandy
  `USERS_after_exit_level2_Ada.DAT` (Rascal, Ada with a level-2 saved game), MCGA and CGA (taken
  after the Tandy session) `USERS_after_tandy_newplayer_Ben.DAT` (Rascal, Ada, Ben); `cga/06_level2`
  and `mcga/05_level2` show Ada's resumed game after Flutter's first hint was closed. The pointer is
  missing from every capture taken more than 600 idle ticks after the start (all but the titles).
  `tandy/14_net_catch` and `tandy/15_after_catch` are frames 328 and 389 of `avi/tandy_net_catch.avi`.

## 7. How the play-through was driven (for reproduction)

- Everything was keyboard input posted through `tmctl` (`key`, `hold:`, `down:`/`up:`), with
  screenshots through the DOSBox-X menu. The emulator window never had focus.
- Catching elves needs reaction time, so a scratch script did it:
  `extracted\scratch\dos_captures\catcher.py` (not part of the repo). It takes DOSBox-X
  screenshots in a fast loop (~30 ms each, through the same posted WM_COMMAND). It builds a
  background as the per-pixel mode of 3 s of frames, finds moving blobs outside the player's
  columns, estimates their speed over 0.3/0.6 s windows, and posts Space when a blob is
  predicted to sit in the net's landing zone (33..73 px in front of the player) 0.6 s later.
  `hunt.sh` repeats that until a quiz or hint appears (`isquiz.py`), and the quiz is then
  answered by hand. About 1-2 catches per minute; roughly every second catch was a quiz.
- `dshot.py MODE NAME` takes a screenshot and stores the native and raw copies. `nat.py` does the
  same for an AVI frame.
- Typical sequence used for the EGA climb: sign in a new player, catch elves until three clue
  words, read the three words, find the scenery that matches all three, drop a coin there (key),
  walk to the level's lock and press ↑. Repeat on levels 2 and 3. When the net runs out, pay 4
  coins at a NETS 4 COINS rock and press ↑.

## 8. Not captured / open points

- Treasure found (2-of-3 place), the chest filling, a star rank-up, the crown or champion
  ending, and the prize rooms and prizes in the clubhouse (requires treasures).
- Flipping (←+↑ / →+↑) on the path, falling through a gap, elf dust taking coins.
- "Using the Mouse" and "Getting More Hints" help pages; Start Over; the New Player Hints and
  Sound option dialogs.
- Hercules mode; explicit `-S` sound switches; listening to the audio (muted by design). Flip
  `nosound=false` in `tools\dosbox_tmdos.conf`, or record with `rec start` (the AVI carries
  PCM), when sound is needed.
- What `-X` toggles (`[4d94:923c]`); exactly how Enter behaves on each attract page (§5.1); the
  reason for the "Packed file is corrupt" log line (§1).

---

## Round 2 (wave 1, runtime agent)

Second capture pass, made for the wave-1 logic specs and the requests of the other wave-1 agents
(answers with frame numbers: `extracted/scratch/dos_requests/*.answers.md`). Same setup as §1
(DOSBox-X 2026.10.01, `tools/dosbox_tmdos.conf`, EGA, `cycles=fixed 6000`, host sound muted,
window parked, keyboard posted). All names below are relative to `extracted/captures/dos/`.
Native PNGs are 320x200 (2x2 doubling checked and removed), DOSBox-X originals in `ega/raw/`;
AVIs are ZMBV with a 48 kHz stereo PCM track; their header says 59.9228 fps, the EGA ones run at
60.29 frames per emulated second (§3, wave 4).

### R2.1 New tools and techniques

* **Save-state RAM access** (`extracted/scratch/dos_w1_runtime/savmem.py`): a DOSBox-X `.sav` is a
  zip whose `Memory` entry is a 0x88-byte header followed by the guest's physical RAM. DGROUP is
  located by the Turbo C copyright at DS:0004 (in these sessions DGROUP is at segment 0x45B8, i.e.
  Ghidra segment S runs at S − 0x7DC). `vars`/`peek` read game variables; `poke`/`pokew` write
  them into a new state (rezipped with the same entry names). `where.py` lists position, level,
  coins, state for every state of the library; `layout.py` prints the 48 hiding places with their
  pictures and contents. This made "crafted" situations possible without a full play-through:
  found treasures, totals, tier, coins and nets were set in RAM of real game states.
* **Frame and tick logger** (`framelog.py`): patches a state with three far-called stubs placed in
  the free DOS block above the game (8000:0000..8800:7FFF; checked with `mcb.py`): main loop
  130f:0097 (one `g_ticks` entry per loop iteration), Game_Tick step path 130f:0112 (a 16-byte record
  per executed logic tick: ticks, player x/y, camera x/y, player state, `g_keys`, coins), Game_Tick
  blocked path 130f:0190 (ticks blocked by DS:910D). `stepchanges.py STATE` prints the ticks where
  something changed, `flseq.py STATE` the frame timeline. States named `savestates/r2/fl_*` carry
  this patch (behaviour unchanged, measurement only; use the plain states for anything else).
* **Sound test harness** (`soundtest.py`): extends the patched frame stub with a scheduler that calls
  `Music_Stop`, `Sfx_Play(n)` and `Music_Play(t)` at fixed PIT-tick delays, so every effect and tune
  can be recorded in isolation on each sound device from a real game state.
* **Silent audio capture**: with `[mixer] nosound=true` DOSBox-X still renders its mixer into
  captures. `tmctl.py audio wav|opl|mtavi start|stop OUT` (new, additive) drives "Record audio to
  WAV", "Record FM (OPL) output" (.dro register log) and the multi-track AVI. Nothing reached the
  host speakers at any time. The AVIs of `rec` carry the same audio.
* AVI helpers: `avimeasure.py` (scroll-step detection, region change list, pixel diffs) and
  `avi2png.py` (curated native frames from AVIs).

### R2.2 Scenes captured (EGA)

| Scene | Files | How / notes |
|---|---|---|
| **Treasure found** (2-of-3 place) | `avi/ega_r2_treasure_found.avi`; `ega/r2_treasure_00_before` … `_07_closed_count1`, `ega/r2_treasure_01_found_hint` | Ada, level 0, cell 0xE9 (treasure 5). Throw AVI 61-152, POOF ~125, blink 154-171, opened with the treasure sprite 172-197, closed + HUD count 198, hint 4 at 204. State before the drop: `savestates/ega/r2_treasure_spot`; the AVI run pressed Down 488 ticks after it (pinned by an elf entering at AVI frame 64); `r2_treasure_02..07` and `_01` are AVI frames 100, 125, 152, 156, 173, 198 and 205. `r2/fl_search_t` is a separate run (Down at 114), `r2/fl_treasure5_found` another (from `r2/at_treasure5`, Down at 433; wave 4) |
| **End sequence, 2 found** | `avi/ega_r2_end_t2_nokey.avi`, `avi/ega_r2_end_t2_enter_skip.avi`; `ega/r2_end_01_castle_top_horn` … `_08_rank_chest_fill` | crafted from `ega/castle_shaft` (treasures marked found in RAM). Runs on without keys: castle top, treasures fly into the chest, chute, fall (AVI 1212-1657), dissolve, treasure room, dissolve, rank chest fill (8 rows, 4 ticks each) |
| **All found (6 at tier 0) and star rank-up** | `avi/ega_r2_end_t6_allfound_nokey.avi`, `avi/ega_r2_end_t6_to_clubhouse.avi`; `ega/r2_end_allfound_01/02_mom_reaction`, `ega/r2_end_t6_rank_chest_1star`, `ega/r2_end_t6_clubhouse_message`, `ega/r2_end_t6_clubhouse_after` | total 0 -> 6: the chest gets the 1-star coin row; the band label still reads "Trainee" on that screen; end message 34 and hint 13 afterwards |
| **Crown / champion ending (total 300)** | `avi/ega_r2_crown_nokey.avi` (all found, 294 -> 300), `avi/ega_r2_crown_t2_total300_nokey.avi` (2 found, 298 -> 300); `ega/r2_crown_01_chest_glow` … `_07_rank_champion_hidden`, `ega/r2_crown_a`, `ega/r2_crown_b` | chest glow, Master of Mischief loses the crown, crown on the pedestal, fall, treasure room with two figures, rank chest with stars and CHAMPION; flash visible at AVI 3042/3053/3063/3074/3084 |
| **Champion end messages** | `avi/ega_r2_crown_to_end_messages.avi`; `ega/r2_crown_08_end_message` (message 35 with the name), `_09_after_message` (36), `_10_after_message2` (hint 13) | Enter on the rank chest after the crown run |
| **Castle creature** | `avi/ega_r2_castle_tier3_climb.avi`; `ega/r2_castle_creature_01_appears`, `_02_strike`, `_03_hit_coin_lost` | `ega/castle_shaft` with g_tier forced to 3 in RAM; window creature left of the climber, arm strike, coin 31 -> 30 (AVI 228-260) |
| **Elf dust** | `avi/ega_r2_tierc_standing_elves.avi`; `ega/r2_elf_dust_01_before_hit`, `_02_hit_coin_lost`, `_03_coin_flies_back` | crafted player `tierc` (tier 3) standing outside the clubhouse: all coins taken (HUD changes at AVI 1490, 1857, 1957, 2263) |
| **Coin creature** | `avi/ega_r2_coin_creature_touch.avi`; `ega/r2_coin_creature_01` … `_04_touch_b` | coins 0, nets 3 (RAM); the gold coin on the path; touching it +2. State `savestates/ega/coin_creature` |
| **Bridges breaking (tier 2)** | `avi/ega_r2_bridge_break_l3.avi`, `avi/ega_r2_bridge_l3_jump.avi`, `avi/ega_r2_bridge_break_l2.avi`; `ega/r2_bridge_l3_00_before`, `_01*`, `_02_after`, `ega/r2_bridge_l2_02_after` | Ada's states with g_tier = 2 and total 120 in RAM; break at x 0x230 (level 3) / 0x3B0 (level 2), fall 8 px per 5 ticks, land on the level below with a jump; landing on the bridge after a jump also breaks it |
| **Falling through a hole** | `avi/ega_r2_geyser_hole_slide.avi`; `ega/r2_geyser_hole_00_before` … `_03_landed` | level 3 geyser top (cell 0x7E): slide state 30 at x 0x750, 8 rows per 7-8 ticks down to level 2 |
| **Level change both ways** | up: `avi/ega_r2_level_change_up_trunk_climb.avi` (trunk ladder level 1 -> 2, logged in `r2/fl_climb`); down: the geyser hole, the two bridge falls, the castle chute | |
| **Jumps on the path** | `avi/ega_r2_jumps_path.avi` | PgUp held (chained jumps), PgUp tapped, Up-then-Right (no jump) |
| **Prize rooms** | `avi/ega_r2_prize_rooms_walk.avi`; `ega/r2_prize_room_1` … `_6` | crafted player `tierb` (40 collected): four shelf rooms in strip rows 3-6, entered by walking right out of the start room |
| **Treasure room from the clubhouse** | `avi/ega_r2_treasure_room_from_clubhouse.avi`; `ega/r2_treasure_room_from_clubhouse_00` … `_03_down` | tierb: Up at x 0x168 of the start room enters scene 4; Left/Right change the treasure on the shelf; Down leaves |
| **Cave doors, bonus coins** | `ega/r2_w_cave_f7_00` … `_03`, `ega/r2_w_cave_eb_00/01`, `ega/r2_w_bonus_00` … `_03`; `avi/ega_r2_bonus_coins.avi` | camera x = player x − 0x124 after the 0xF7 door (state `savestates/ega/w_cave_f7`), player x − 0x20 after the 0xEB door (`savestates/ega/w_cave_eb`); bonus spot +5 once (state `savestates/ega/w_bonus`) |
| **Slow-CPU mode (-X)** | `avi/ega_r2_slowcpu_X_walk.avi`; `ega/r2_slowcpu_scroll_avi150/151/157/158` | 16-px scroll steps, rows 112..143 not scrolled (tearing); DS:A212 = 7 |
| **Start-of-game rank messages** | `ega/r2_tierb_start_message_outside` (4 stars), `ega/r2_tierc_start_message_outside` (champion) | shown when walking out of the clubhouse door, not at sign-in (from `r2/tierb_start_msg` / `r2/tierc_start`); the reference run had moved the mouse: the pointer shows at the right edge, rows 127 / 97 |
| **UI boxes and pages** | `ega/r2_ui_help_mouse_*` (4 pages, PgUp/PgDn), `ega/r2_ui_help_hints_p1`, `ega/r2_ui_box_start_over(_after_left)`, `ega/r2_ui_box_new_player_hints`, `ega/r2_ui_box_sound`, `ega/r2_ui_signin_*` (Esc box, Ctrl+R remove list and confirmation, identical name, 9 players, list full), `ega/r2_signin_crafted_list` | Help "Using the Mouse": `r2_ui_help_mouse_p1` is page 0, `_p2` and `_after_pgdn` page 1 (PgDn on page 0), `_next` page 2, `_next2` page 3, `_next3` the Help drop-down reopened after Done, `_p2_after_pgup` page 0 again (PgUp on page 1). The boxes and help pages were taken in the clubhouse of `savestates/r2/tierb_after_troom` (camera 0xC8, the tierb HUD; the boxes open with the player's settings hints No, music Yes, sound Yes); the sign-in captures on a boot with `USERS_crafted_tierb_layout.DAT` (the crafted list, Esc / Ctrl+R / identical name, `r2_kbd_*`) or with the nine-player roster `extracted/scratch/dos_w1_runtime/USERS_9players.DAT` (`_9players`, `_list_full_*`), more than 600 ticks after the boot (pointer hidden). All reproduced at 0 pixels in wave 4 (`tools/dos/scenarios/ui/`) |
| **Keyboard check** | `ega/r2_kbd_shift_test_name_entry` ("XYz"), `ega/r2_kbd_shift_test_first_letter` ("Bc") | only shifted A is dropped by the name box; other shifted letters work |
| **Title, 60 s** | `avi/ega_r2_title_60s_nokeys.avi` | title page then story page 1, sparkles |

Crafted USERS.DAT files: `USERS_crafted_tierb_layout.DAT`, `USERS_crafted_tierc_layout.DAT` (the
elves agent's crafted roster after a new game of tierb / tierc and Exit and Save), and
`extracted/scratch/dos_w1_runtime/USERS_9players.DAT` (list-full test).

### R2.3 Audio (silent capture)

| File | Device | Content |
|---|---|---|
| `audio/speaker_soundtest_sfx0-11_tunes0-2.wav` | PC speaker (`-Si`, EGA) | from a real game state (Ben, clubhouse): Music_Stop at +100 PIT ticks, effects 0..11 every 600 ticks, tune 0 at +700, tune 1 at +12 900, tune 2 at +17 300, stop at +6 000 (schedule in `extracted/scratch/dos_w1_runtime/sound_script.txt`); 188.7 s, 48 kHz stereo 16-bit |
| `audio/tandy_soundtest_sfx0-11_tunes0-2.wav` | Tandy 1000 PSG (`--machine tandy`) | same schedule (from `tandy/outside`) |
| `audio/adlib_soundtest_sfx0-11_tunes0-2.wav` + `.dro` | AdLib (SB16 OPL, EGA default); effects on the speaker | same schedule (from `ega/shack`); the .dro (51 KB) is the raw OPL register log of the whole run — evidence for the instrument register order open in `fmt_sound.md` §10 |
| `audio/ega_adlib_treasure_found.wav` + `.dro` | AdLib | the treasure search of R2.2 with music and effects 7 etc. |

Every AVI of Round 1 and Round 2 also carries the emulated audio (EGA runs: AdLib music + speaker
effects; Tandy runs: PSG); `extracted/scratch/dos_w1_runtime/avi_audio.py` extracts it.

Note (wave 2): the `snd_*` save states the sound tests started from have DS:63BA (the music loop
flag) = 1, so the recorded tunes loop; an engine render of the same schedule must set the loop flag
as well (in the game it is set by `Title_Run` and by every scene load, `Scene_Update`).

### R2.4 Timing measured (summary; details in `code_runtime.md` §1.3, §9, §10)

* The main loop ran 1690 frames/s when nothing was drawn, 820-880/s while walking, 73/s during the
  coin count-up, 55/s during the search; game ticks always 47.34/s; DS:910D blocked a tick only
  behind slow frames.
* Walk 8 px per 5.125 ticks (41 per 8 steps); climb 8 rows per 7.25 ticks; jump 58 ticks; net swing
  80 ticks; coin throw 71 + 30 ticks; search 37 frames (0.79 s); count-up 14.0 ms per coin (3.32 PIT
  ticks; wave 4, the 13.1 first given miscounted the intervals); rank chest 4 ticks per row; CHAMPION
  flash ~918 frames/s (0.697 s for 640 frames at the EGA AVIs' 60.29 frames/s); dissolve 127 ms;
  keyboard repeat 500 ms then ~30/s.

### R2.5 Save states added

`savestates/ega/`: `r2_treasure_spot`, `w_cave_f7`, `w_cave_eb`, `w_bonus`, `coin_creature`.
`savestates/r2/` (71 states): crafted end-game states (`castle_shaft_t2`, `castle_shaft_t6`,
`castle_shaft_t6_294`, `castle_upper_t2`, `castle_upper_t2_298`, `castle_upper_t6`,
`castle_upper_t6_294`, `castle_shaft_tier3`), the results after each run (`end_t2_enter_after`,
`end_t6_rankchest`, `end_t6_clubhouse`, `crown_t2_298_wait`), tier-2 level states
(`l3_at_tree_tier2`, `l2_at_nests_tier2`), crafted-player games (`tierb_exitbox`,
`tierb_start_msg`, `tierb_after_troom`, `tierb_prize_end`, `tierc_start`), speaker and slow-CPU
sessions (`speaker_clubhouse`, `slowcpu_clubhouse`), the sound-test states (`snd_speaker`,
`snd_tandy`, `snd_adlib`), and the logger states `fl_*`.

### R2.6 Corrections to Round 1

* §5.1 "Shifted keys are dropped": only shifted A is dropped (name box); Shift works otherwise
  (R2.2 keyboard check).
* §2 "`-X` toggles [4d94:923c] (purpose unknown)": it is the slow-CPU flag (`code_runtime.md` §2.4,
  R2.2).
* §8 "listening to the audio (muted by design)": solved without unmuting (R2.3).
* §5.6 "No treasure was found", §8 not-captured list: now captured (R2.2) except the items below.
* §4.1 `ega/002` shows story page 2, not page 1, and `ega/101` is the castle-top end screen (scene 3),
  not the treasure room (scene 4); §5.1 open point answered (wave 2 synthesis, from `code_ui.md`
  §8.3 and §16).

### R2.7 Still not captured

* A quiz answer paying +2 coins (the count-up was measured with a RAM edit and with the bonus spot).
* An elf-dust miss (jumping over the dust).
* Hercules mode; the MCGA and CGA looks of the Round 2 play scenes (their UI scenes: Round 3); Tandy
  captures of the Round 2 scenes (Tandy pixels equal EGA, §6; a Tandy session with sound: Round 3).
* Mouse input (cannot be injected without the host mouse).

---

## Round 3 (wave 4, looks agent)

Third capture pass for the looks and the sound, same setup as §1 (DOSBox-X 2026.10.01,
`tools/dosbox_tmdos.conf`, `cycles=fixed 6000`, host sound muted, window parked, keyboard posted),
`--machine cga`, `mcga` or `tandy`, the roster `USERS_after_tandy_newplayer_Ben.DAT` copied to
`extracted/dos_c/TM/USERS.DAT` for the session and the shipped file put back afterwards. Driver
scripts: `extracted/scratch/dos_w4_looks/r3_session.py` (CGA, MCGA) and `r3_tandy_av.py` (Tandy).

### R3.1 CGA and MCGA UI frames (`cga/r3_NN_*.png`, `mcga/r3_NN_*.png`, 24 each, 320x200)

01 story page 1 (the title had timed out during the start), 02 sign-in, 03 the sign-in's Esc box, 04
the name box with three letters typed, 05 Ada highlighted, 06 the saved-game prompt, 07 the resumed
level-2 game with Flutter's first hint, 08 the game, 09-11 the Help menu with items 1, 2 and 4
highlighted, 12-13 About the Game pages 1 and 2, 14 the game after Esc, 15 Using the Keyboard page 1,
16-17 the File menu (items 1 and 2), 18 the Options menu, 19 the New Player Hints box, 20-21 the
Music box with either button, 22-23 the Exit and Save box with either button, 24 the game. The
keys and their engine ticks: `tools/dos/scenarios/looks/boot_r3_cga.json` / `boot_r3_mcga.json`.
States at the end: `savestates/r3/cga/r3_level2_end`, `savestates/r3/mcga/r3_level2_end` (CGA and
MCGA states cannot be loaded by the engine: the CGA back buffer is 2 bpp).

### R3.2 Tandy session with sound (`avi/tandy_r3_session.avi`, 45.19 s, 2708 frames, PSG audio)

From the attract (story page 1) through the walk-off, the sign-in, Ada's saved game, Flutter's
hint, a coin toss, a net swing and the Help menu (state at the end: `savestates/r3/r3_session_end`).
The AVI's time maps to engine PIT ticks as `AVI seconds x 236.695 + 10336` (the title tune's
position at the AVI's start, `tools/dos/audiocmp.py align`); frames saved as `tandy/r3_session_fNNNNN.png` for the
scenario `boot_r3_tandy_session`. Timing findings (engine against the AVI, keys placed at the tick
the AVI shows them): the coin toss and the net swing frame for frame; the walk-off's figure 3 game
ticks late unless its Enter is placed 3 ticks before the redrawn title appears (72 of 80 frames
exact then: the original's title redraw is a long frame); the resumed game's screen about 8 ticks
early; sound in `fmt_sound.md` §11.6.
