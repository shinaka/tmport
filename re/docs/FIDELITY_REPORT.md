# Whole-game fidelity report (full game path)

Date: 2026-10-18. Build: `build_fidelity` (Release, MSVC, 0 warnings), `tmtests` 294/294 cases,
120 176 assertions pass with `TM_DATA` set. Every scenario below was run through the **full game
path** (`tmport-cli --play --headless --keys ...`: title, Club Sign-In, climb set-up, rooms,
scenes) and compared pixel by pixel with the reference captures of the original
(`extracted/captures/ref/`, described in `REFERENCE_CAPTURES.md`). The module tools were used only
where the full path cannot show a frame (marked "room driver").

Tags as elsewhere: **[O]** observed in this work, **[I]** inferred.

## 1. Method

| Item | How |
|---|---|
| Comparison | 512×384 game area: original at (61,48) without / (61,67) with the menu bar (`tools/crop_game_area.py`), port canvas at (64,48); RGB tolerance 12 (6-bit DAC) |
| Saves | crafted `users.dat` files from `extracted/captures/savestates/round2/*.users` (and the one- / seven-player files of the screens work) copied into a scratch `--pref-dir` |
| Positions | the player's exact x / frame / facing in a reference is measured by template matching (`pose_x.py`), and a BFS walk planner over the EXE's movement tables (DS:0c34, 0c89, 0d33) produces the tap sequence (`--tap-latch`, one tap = one 100-ms tick) that reaches it |
| Jumps | `--keys` has no simultaneous arrows; a jump is started with a `click:` above-right / above-left of the player (mouse-directed walking sets L/R and U in the same tick, `code_10e0-10f0.md` §4.3.1) |
| Frames | `shot:` once per input fetch (= once per room tick); time-based shots (`wait:30..120`) during modal scenes; the best-matching shot is reported, and the order of the matched shots was checked against the reference order |
| Elves | elves are placed and moved from `srand(clock)`, so their positions never repeat the capture's. `elfmask.py` finds elf sprites (all frames of BNDL 310–320, both facings, decoded from the user's TMT256.DLL) in mismatch blobs of either image and masks their pixels; "exact except elves" = 0 mismatching pixels left |
| Scripts | `extracted/scratch/fidelity/` (`fid.py` harness, `scen.py`, `plan.py`, `elfmask.py`, `e*.py`, `cmp_*.py`, `mouse*.py`, `c16*.py`); run outputs in `runs/` |

## 2. Results by scenario

Counts are reference frames. "Exact" = 0 mismatching pixels in the whole game area (for E1 the
guest mouse pointer at game (259..270,192..211) is excluded). "Transient" = a frame the port shows
only between two input fetches, where `shot:` cannot capture it (see §5.1).

| Scenario | Frames | Exact | Exact except elves | Not pixel-compared / different (reason) |
|---|---|---|---|---|
| E1 title | 8 | 8 | – | – (animation box compared by best phase; 06 exact at the matching anim-1 phase) |
| E2 sign-in | 16 | 15 | – | 12 transient (black play-field + menu bar) |
| E2b ranks | 3 | 3 | – | – |
| E3 climb start | 11 | 8 | – | 02, 03 transient; 09 headless: popup exact, clue box already in mode 2 (no voice device) |
| E4 transitions | 19 | 8 | 3 | 01, 05, 11 last frame of a room (transient; 01, 05 exact in the room driver's per-tick dumps); 06, 09, 16, 18 black (transient); 02 capture tear |
| E5 movement | 30 | 27 | 2 | 28 = E4 01 (transient, room driver exact) |
| E6 coin / net / key | 39 | 15 | 18 | 31–36 elf catch at the capture's elf position (random; catch verified in E8 runs) |
| E7 castle | 66 | 54 | – | 48, 53 shot artefact (§5.1); 50 screen-3 background exact, climber at another y (path); 58–66 creature at random y |
| E7a route to castle | 35 | 17 | 14 | 01: 9 px (§4.2); 09 last part-1 step not sampled; 10, 14 black (transient). 16–30 compared with the fountain animation rect (184,179,128,108) excluded (its phase depends on ticks since room entry) |
| E8 quiz | 41 | 18 | – | 31–34, 38, 39 play area exact, HUD counters differ (fewer swings/catches); 26, 27, 29, 30, 35, 37, 40 random praise/sorry message (R(4)); 28, 36, 41 HUD exact / coin bonus verified, other room; 18, 19 headless (line reading needs voice); 01–03, 24, 25 elf at the capture's position |
| E9 treasure hall | 12 | 12 | – | – |
| E10 celebration / rank | 37 | 26 | – | 01 exact with the climber column excluded (path); 02, 13, 28, 35, 36 transient; 15, 16, 19, 30 rank-band iterations (50 ms) between 120-ms shots (neighbours exact; AVI frames f1693/f1700 exact); 22 Program Manager |
| E11 dialogs | 14 | 7 | 2 | 06, 08, 11, 13 Win16 popup menus: game area exact outside the popup (the port draws its own menu, §4.3); 04 button flash (transient) |
| E12 help | 9 | 9 | – | – |
| E13 net shop | 11 | 3 | 8 | – |
| E14 coin pile | 12 | 3 | 4 | 09 exact with the pile excluded (x = 50 + R(412)); 08 transient; 10–12 pile at another random x: move to room 9, pickup and HUD (coins 0 → 10) exact |
| E15 Ctrl+S | 8 | 4 | – | 01–04 need a playing hint voice (headless has none) |
| **Total** | **371** | **237** | **51** | 83 (all explained; none points at a port defect) |

Of the 51 "exact except elves", 46 were masked automatically by `elfmask.py`; in five (E4 13, E7a
16, 17, 19, 25) an elf stands mostly hidden behind the player and the residual (154–1 085 px) was
checked visually to be elf pixels only.

### 2.1 Notes per scenario

* **E1** All story pages exact; anim 1 matched in all five page captures; the end animation (06)
  depends on the anim-1 phase at the last Enter — exact when the Enter falls on the same phase.
  The sign-in at program start shows the title backdrop (07/08 exact) [O].
* **E2/E2b** Name entry (both caret phases, 8 letters, "Abdf", "Ab"), list with 1 and 7 players,
  Ctrl+R dialog (21e6), "already on the list" (2129), list full (2200), stars / Champion /
  thousands commas / empty 8-digit number, the post-M7 list: all exact [O].
* **E3** Room 9 start pose, menu-greyed frame, hint 0 and hint 2 popups (voice and CONTINUE
  states), dismissal and free play exact. The new player's random climb does not affect room 9.
* **E4** Door 9 → 0 → 9 (frame 9, room 0 start pose, x 90, room 9 again), door 11 → 17 (no black),
  gate 11 with the key: auto-walk, cut-scene 0 part 1 and part 2 (PMAP 2200), room 23 start pose
  exact. The type-1 black play-field is held 70 ms by `Room_Present` (room 9 / hall only)
  [O, trace "presentation pause 70 ms"], within the original's 57–86 ms.
* **E5** Every walk-right, walk-left and jump frame exact, including the type-0 room change that
  keeps the jump (frame 21 at x 24 in room 10).
* **E6** Coin toss into the treasure spot (all 13 steps with the blink, sparkle, POOF, collected,
  hint 4), into an empty spot (blink, coin lost), facing left into the middle third, the net swing
  (frames 25–34, 32 clipped at x −15) and the key under the bushes (blink shows the key, collected,
  hint 9 + filled key icon) exact [O].
* **E7** Castle screen 0: walking right with `dxLeft` (M8), walking left, both clamps; climbing,
  screen changes, jumps right/left, the fall into the empty column of screen 2 with the re-grab on
  screen 1, the fall to the ground with landing frames 18–23, climbing down and stepping off, coin
  and net wasted on screen 0: all exact; the castle trace (`castle: climb screen s frame f x y`)
  reproduces every value of `REFERENCE_CAPTURES.md` §R3.
* **E7a** Gate in room 25 (auto-walk, lock, door entry, key icon cleared), cut-scene 1 (PMAP 2201),
  room 36, the fountain bump sequence, the jump over the fountain and the late jump that bounces,
  the level-2 key in room 37, gate 38 → room 49 with no cut-scene and no black: exact.
* **E8** (elf caught through the full path, question 39 = record slot 0) Question screen with the
  menu greyed, focus on answers / lines / speaker, wrap-around, ignored Down on the speaker,
  answer reading highlights, greyed answers after one and two wrong tries (focus skips them):
  exact. Coins: +2 / +1 / +0 for tries 1 / 2 / 3 (coin counter compared before/after) [O]; after
  the first try the HUD (11 → 13 coins, 9 nets, the clue word in the clue box) is exact.
* **E9** Hall room 0 (newest treasure, tier-3 HUD), rooms 1–3 with each pedestal animating exactly
  when the player's centre is in its window, room 4 (bg 1038, one treasure, right wall), and room 0
  of Gate1's next climb (treasure 0): exact.
* **E10** Gate1 (not champion): PMAP 2299, DAM 20300 / 20302 (treasure in slot 80) / 20301,
  PMAP 2202 + DAM 20308, rank screen bands; champion run (Champ0) with DAM 20305 and all eight
  bands; Esc on the rank screen → Exit dialog exact; File > See Rank inside the game frame and the
  restored room exact; Enter during DAM 20300 skips the celebration and the rank screen, next climb
  in room 0 with three gem icons (tier 1) exact.
  **M7 reproduced byte for byte:** after Esc + Yes on Gate1's rank screen the port's `users.dat`
  is **identical to the original's** (`savestates/round2/USERS.DAT.after_M7_exit`, 11 085 bytes;
  total 25, climbs 1, stale tier 0, room 0xFE), and the next climb shows two gem icons (23 exact).
  The climb-end save writes the old total (24) and Exit saves twice, as specified [O].
* **E11/E12** Exit (default No, Tab), New Player Messages, Music and Sound, Voice, About, Start
  Another Game, all help pages and the restored room: exact.
* **E13** Four coins at the sign (one sign frame per coin), the door opening with the fourth coin,
  auto-walk, door entry frames 9/10, net taken (nets 20, empty niche), door closed 4 ticks later
  (checked tick by tick) [O].
* **E14** Ten coins spent (coins lost in room 9), hint 6 with CONTINUE at once (sound off), ten
  nets, hint 7 (empty swing), no pile in the hall or in room 9, the pile in room 10, moved back
  into room 9, picked up (coins 10) [O].
* **E15** Ctrl+S twice on story page 1 changes nothing on screen; Enter goes to page 2 [O].

## 3. Mouse (scripted `click:` / `move:` in `--keys`, 640×480 canvas)

| Behaviour | Test | Result |
|---|---|---|
| Title hot spot 1200 (= 0x4B0) acts as Enter | click in the play area on page 0 and 1 | pages 1 and 2 drawn, exact vs E1 02/03 [O] |
| Sign-in list rows (0x4ae) | click on the Tester row | "signed in: Tester (existing player)" [O] |
| "Start New Player" row (0x4ad) | click on row 1 | name-entry box, exact vs E1 08 (caret phase aside) [O] |
| Name entry footer 0x4af = Esc, body line 0x4b0 = Enter | click the footer, type "bob", click the Enter glyph line | back to the list (exact vs E1 07); new player "Bob" signed in [O] |
| Mouse-directed walking (0x600) | click left of the player in room 9 | two walk ticks (frames 1, 2 at x 203, 195: the down and the up event), then stand [O] |
| 0x600 latch while the button is held | room driver: `press`, 500 ms, `release` | 6 walk ticks (5 held + the release event), then stand; a click = 2 ticks [O] (`--keys` of `--play` has no press/release, §5.1) |
| Click above the player | click above-right / above-left | jump right / left (E5 20–28, E7a 24–30 exact) [O] |
| Hot-spot priority, clue box | click on the clue box in free play | nothing happens (0x409 shadows 99, so no `c` replay); allocator `HotspotTable` (lowest free slot, hit test from slot 49 down) and `Hud::setClueBoxMode` order match `code_10e0-10f0.md` §5.2. The right-click form (0x2409 instead of 0x2063) cannot be scripted (no right button in `--keys`) |
| Dialog buttons | Esc, click "No" (319,216); later Esc, click "Yes" (239,216) | dialog closed, room restored pixel-identical; Yes ends the game (S12) [O] |
| Menu bar | click "File", click "See Rank" | rank screen; a click in it (hot spot = Enter) closes it, room restored identical [O] |
| Quiz: line hot spots use k·lh | question 68 (record slots crafted to q 68; its blank slot is between lines 3 and 4) | click on the displayed line 4 (y 147): nothing; click on the blank row (y 130): line 4's voice (4494) plays; rows of lines 0 and 3: voices 4490 / 4493; speaker click reads all (4490–4494, answers 4497/4495/4496) [O] |
| Quiz answer click | click on answer position 0 of q 68 | "answer position 0 wrong", sorry screen [O] |

## 4. Remaining known differences

### 4.1 By design / by the environment (not defects)

1. **Randomness.** Elves, praise / sorry messages (R(4)), the coin pile x, the castle creature
   and the layout of a new climb come from `srand(timeGetTime())` as in the original, so their
   values differ from the captures. All other pixels of those frames match.
2. **Headless runs have no voice device**: speech ends at once, so the clue box goes to CONTINUE
   immediately (E3 09, E15 01–04) and the question-line reading highlights are not on screen long
   enough to be shot (E8 18–19). The answer reading highlights, timed by 250/500 ms delays, are
   exact.
3. **Transient frames.** The original shows some frames for the time it needs to load from CD
   (black before PMAP 2201, ≈ 900 ms of black before the celebration, the erased-sprite frame after
   a celebration skip); the port loads at once and shows them for well under a frame. No delay is
   coded there in the original either (`code_1168-1198.md` §6.1, `code_10f8-1148.md` §8).
4. **Menu bar.** The original's File/Options/Help popups are native Win16 menus drawn by Windows
   and overlap the game area; the port draws its own Win3.1-style menu (app/ui area). The game area
   outside the popups is exact.
5. **Mouse pointer.** The original captures contain the guest pointer (E1); the port's canvas has
   none (the system cursor is used).

### 4.2 Unexplained (minor)

* **E7a 01** (Gate1 resumed in room 25, frame 60 at x 83): 9 pixels at the player's left edge
  (x 91–92, y 177–181, and (120,219)) show the room background in the capture where the port
  draws the player's outline / backpack. The following frames of the same scene (02–08) are exact
  and the frame-60 sprite is exact in every other start pose (rooms 9, 23, 36). Probably an artefact
  of that one capture (an elf erase or a partial update in the original); not reproducible.

### 4.3 Observations for other owners (not changed by me)

* **Spec (code_1168-1198.md §2.8 / REFERENCE_CAPTURES §R3):** "on layout-0 screen 1 only column 0
  qualifies" for the wall creature is not what the code does: `1168:1143` retries only while the
  cell is 1 (full ladder), and screen 1 of layout 0 is `[0,1,1,1,0]`, so column 4 qualifies too
  (with flip 1). The port shows the creature in columns 0 and 4 (trace
  `castle: creature screen 1 column 4 ... flip 1`), which matches the code; the capture sample just
  had no column-4 spawn.
* **app area (`src/app/script_input.*`, `launch_options.*`)** — test tooling only:
  * no `press:` / `release:` / right-click steps in `--keys` (the room driver has press/release);
  * a step is posted at an input fetch, and `Engine::flushInput` also fetches: a key step that
    arrives during a flush is discarded (typing "tester" on the sign-in list without waits gave
    "Tster": the name box's `Input_Flush` ate the second letter). Add `wait:` between keys;
  * shots happen only at input fetches, so frames shown between fetches (the last frame of a room,
    black transitions, dialog button flash, the frame before the castle's walk loop) cannot be shot;
  * `--play` has no `--seed`; in scripted headless runs the seed (low 16 bits of ms since start)
    clusters within a few ms (0x0f79–0x0f83 here), so repeated runs replay a few elf layouts
    (e.g. every 256-colour quiz catch happened with seed 0x0f7a). `GameMainOptions::seed` exists; a
    launch option would make replays deterministic;
  * long scripts with many absolute shot paths exceed the Windows command-line limit (use relative
    shot names; they are relative to the working directory).

## 5. 16-colour mode (`--16`, no captures exist)

Run through the full path: title page 0 and story page 1, sign-in list, room 9 (resumed), Exit
dialog, help page, room 10, an elf caught (seed shifted to 0x0f7f) → question 39 with focus frames,
the line highlight, speaker focus, answer reading highlight, praise screen and the clue word in the
HUD. Nothing breaks (no error or warning in the log). Every pixel of every 16-colour shot is one of
the 16 standard VGA colours (`tools/assets/pmap.py` `VGA16_PALETTE`) [O]. Colour key nibble 3:
sprites show no key-colour pixels; the only VGA colour 3 pixels on screen belong to opaque pictures
(e.g. the HUD panel PMAP 1306 has exactly the 3 268 nibble-3 pixels found in room shots; the room
backgrounds have none), as expected for opaque draws [O]. Elf catches behave identically in 16 and
256 colours (room driver, seeds 1, 4–8: same catches, slots, coins).

## 6. Fixes made

None. No discrepancy found in this verification points at the owned files
(`src/game/room/*`, `src/game/scenes/*`, `game_main.*`, `title.*`, `signin.*`); every difference
is explained in §2 and §4. No source file was changed.

## 7. Reproducing

```
set BUILD_DIR=C:\Git\TM\build_fidelity && scripts\build.bat
cd extracted\scratch\fidelity
python -P e1b.py            (and e1d.py, e2a.py, e2c.py, e2bb.py, e3.py, e5.py, e6.py, e6n.py,
                             e4.py, e4g.py, e7a.py ABC, e7.py ABCD, e9.py, e9b.py, e10.py GCSR,
                             e11.py, e13.py, e14.py, e15.py, e8.py, e8b.py, mouse1.py, mouse2.py,
                             mouse_quiz.py, c16.py, e8_16.py)
python -P cmp_e6.py         (cmp_*.py: comparisons with elf masking)
```
