# Porting notes: Gizmos & Gadgets in the tmport engine

**Status: wave 0 draft.** It records what is already known to matter for the engine (`ggport`,
`src/gg/`). Wave 1 extends every section from its topic specs and the first reference captures;
nothing here has been checked against a capture yet.

How the original DOS program (`ssg.exe`, Borland C++ 3.0, 1993) maps onto the engine: the runtime
model, the screen, the clocks, the CPU-dependent path, why the overlays do not matter, the original's
switches as options, input, saves, and which quirks to keep. Read [SCOPING.md](SCOPING.md) (plan,
decision) and [ARCHITECTURE.md](ARCHITECTURE.md) (program, flow, platform layer) first; the file
formats are in [fmt_data.md](fmt_data.md), [fmt_graphics.md](fmt_graphics.md) and
[fmt_sound.md](fmt_sound.md).

Tags: **[O]** observed (code, bytes, decoded data), **[I]** inferred, **[R]** recommendation of this
document. Addresses as in ARCHITECTURE (conventions).

---

## 1. Inputs and validation [R]

| File | Needed for | Check |
|---|---|---|
| `ssg.exe` | DGROUP tables and strings: the `Seq_Run` tables, zone tables, menu records, chimp lists, puzzle counts, rank table and names, start-up and UI words | 213 200 bytes; MZ; TLINK byte 0x50 at file 0x1C; "Borland C++ - Copyright 1991" at file 0xFC74 (DS:0004); `FBOV` at 0x1BA00 with the segment table at 0xF030 |
| `gizmo000.dat` .. `gizmo003.dat` | every picture, palette, font, text, hint, sound bank, DAM script, game table and the movie | `MR0`..`MR3` + 0x1A; offset table ends at file length − 4; last entry `I` stored `5A A5` (the original checks volumes 0-2 only, 14ff:0167) |
| `PLAYERS.DAT` (optional) | import of the roster and records | 578 487 bytes, `USR` 0x1A, version word 12 |
| `gizmo004.dat` | — | ignore: a screen save-under the original writes at run time (ARCHITECTURE §4.8) |
| `ssg.bat`, `Read_1st.txt`, `ssg.ico` | — | ignore |

* The game folder is found through `GG_DATA` for tests (default: the repository's
  `GadgetsAndGizmos\` folder) and through the command line or settings for the game. The engine
  never writes into it.
* Far pointers in DGROUP hold image segments (0x0ea7 = DGROUP, others = code or stub segments) and
  must be resolved like TM Windows' relocated pointers [O]. Code pointers to stub entries (the state
  tables, menu handler tables, the menu hook DS:865a = 1e94:0025) are mapped to engine functions by
  their stub entry (§6).
* Other releases of the game (other versions, Windows or CD editions) are refused with a clear
  message when a size or magic check fails (SCOPING §8 risk 8). A missing file is named.

---

## 2. Runtime model

### 2.1 What the original does [O]

* PIT channel 0 at 3 579 545 / 3 / 8192 = **145.652 059 Hz** (6.866 ms); the ISR increments the u32
  tick DS:021a and runs the sound driver on every second tick (**72.826 Hz**). No game logic runs in
  the interrupt (ARCHITECTURE §3.1).
* The game is a tree of ordinary nested loops: `Seq_Run` calls a state function, which runs modal
  loops, which call dialogs with their own loops. Every loop paces itself on a deadline timer:
  `Timer_Set(0, 4)` … draw, flush … `Timer_Wait(0)`, `Input_GetCode`, dispatch. **4 ticks per frame =
  36.41 frames/s; races 8 ticks = 18.21 frames/s** (ARCHITECTURE §3.3).
* No catch-up: a frame whose work takes longer than its ticks makes the game slower [I].
* `Timer_Wait` and `Delay` end at tick ≥ deadline; `Timer_Expired` only at tick > deadline
  (152c:0029 / 0072 / 004a).
* `Delay(n)` calls `Input_GetCode` while it waits and discards the codes; global keys (Ctrl+S, Esc,
  the menu keys) still act inside it [O loop, I consequence].
* **Time stands still in dialog boxes**: the dialog loop (3f56:0655) restores the tick it had at
  entry when it closes. The menu drop-down does not.
* The mouse is serviced at interrupt time: the INT 33h event handler latches buttons and redraws
  the cursor (a dirty-tile flush) even while the main loop waits.
* Overlays load from disk on demand (§6).

### 2.2 Engine model [R]

```
host clock (ns) --> virtual PIT: tick = floor(elapsed * 3579545 / (3 * 8192)) - frozen   (u32, as DS:021a)
                     '-- every 2nd PIT tick: Snd_Tick (rendered by sample count in the audio stream)
game code: nested functions on the main thread, as the original
   every wait primitive (Timer_Wait, Delay, the retrace waits of palette loads, the dialog and
   menu loops' polls) pumps the host: SDL events -> keyboard and mouse models, audio service,
   present at most once per display refresh
```

* This is the Windows edition's model (`docs/DESIGN.md` "Runtime model": nested blocking loops,
  every wait pumps the engine) with PIT ticks instead of milliseconds. It is **not** the DOS
  edition's lockstep runtime (`dos/app/dos_runtime`): G&G has no logic in the interrupt, no step
  gate and no per-tick game step.
* The dialog freeze is an offset (`frozen` above): on closing a box the engine adds the ticks spent
  inside it, so the counter reads as it did at entry and later deadlines keep their meaning.
* Deterministic runs (tests, frame dumps, headless tools) use a manual clock: a wait advances the
  clock to its deadline, and the work inside a frame costs 0 ticks unless a cost model says
  otherwise (§2.4). Cost models are expressed in PIT ticks, never in host time.
* Sound reuses the DOS edition's sample-count ticking (`dos_audio_device`, `dos_sound_renderer`,
  generalised): one driver tick per 2 PIT ticks.

### 2.3 Frame pacing details [O, R]

* A frame begins where the previous `Timer_Wait` returned, i.e. on a tick edge, so frames that fit
  are exactly 4 (or 8) ticks long. The first frame after a long operation starts wherever that
  operation ended [O].
* Keep the one-tick difference between `Timer_Expired` and `Timer_Wait` (TM Windows' `Timer_Expired`
  uses ≥; the shared `timing/deadline_timers` needs the boundary as a parameter) [R].
* Other time bases to keep: 0x111C ticks (30.07 s) for attract pages and the idle code, hint delays
  of 0x490 and 0x2238 ticks, the button flash (4 toggles of `Delay(4)`), the results count-up over
  50 frames, the movie at 18 ticks per frame with its cue table (ARCHITECTURE §3.4).

### 2.4 Long frames and unknown costs [I]

On the reference machine some frames take longer than their 4 ticks: room entries (resource loads
through the LZH decompressor), full redraws, overlay loads (VROOMM reads, copies and fixes up the
overlay), the random wipe (paced by timer slot 2) and palette fades (paced by the retrace, §3.3).
Their costs decide when the next frame starts and so the timing of everything after them. They must
be measured from reference frame logs before the fidelity wave, as the TM DOS port did
(`re/dos/docs/PORTING_NOTES.md` §2.5), and modelled as costs in PIT ticks.

---

## 3. Screen presentation

### 3.1 Geometry [O, R]

* Mode 13h, 320×200, shown at 4:3 (pixel aspect 6:5): `PresenterLayout::dos1990()` as is (integer
  5×6, square-pixel option).
* Frame buffer: 320×400 bytes of palette indices; rows 0..199 the composed screen, rows 200..399
  the back page; sprites are erased by copying from y+200 [O]. A G&G frame-buffer class (or a
  parameterised `gfx/frame_buffer`) keeps this layout so that the original's coordinates work
  unchanged [R].

### 3.2 What reaches the screen [O, R]

* The original copies dirty 8×8 tiles from the RAM screen to VRAM once per frame without waiting
  for the retrace, so a capture can show a half-copied frame. The engine presents complete frames;
  fidelity checks compare settled frames [R].
* Two paths bypass the RAM screen: the random wipe copies RAM-screen columns into VRAM over several
  timer-paced steps, and the movie decodes straight into VRAM, leaving the RAM screen untouched
  [O]. So the engine keeps a separate 320×200 **VRAM surface** that the tile flush, the wipe and the
  movie write and the presenter shows [R].
* The software cursor is drawn into the RAM screen at each flush and redrawn at interrupt time
  whenever the mouse moves; primitives first restore the background under it (`Gfx_HideMouseOnce`
  1399:0b22) [O]. The engine may composite the cursor into the presented frame at the current
  mouse position, which gives the same picture between frames; whether the wipe and the movie hide
  or overwrite the cursor must be checked against captures [R, I].
* DAC colours are 6-bit. Expand to 8 bits as `(v*255 + 31) / 63`, the rule that matched DOSBox-X for
  the TM DOS MCGA look (`src/dos/gfx/palettes.h`); check it against a G&G capture [I].

### 3.3 Palette loads and fades [O, R]

* A palette load (`Pal_SetRange` 1399:05d6) writes 128 colours at the start of a vertical retrace,
  then the other 128 at the next one: **2 retraces per load**. Mode 13h on VGA refreshes at
  25.175 MHz / (800 × 449) = **70.086 Hz** [O code, I rate].
* Fades: out = 12 loads (24 retraces, ~0.34 s), in = 9 loads (18 retraces, ~0.26 s); step sizes from
  `Pal_Fade` 1473:015c (ARCHITECTURE §4.2). The wave 0 AVIs show longer fades on the title and story
  pages (`REFERENCE_CAPTURES.md` §4); wave 1 reconciles the two.
* Engine: a retrace clock on the same time base as the virtual PIT (retrace k at k / 70.086 s); a
  load waits for the next retrace start and becomes visible there. The PIT keeps running during a
  fade (only the dialog loop writes the tick counter), so fade time counts against deadlines [O, R].
* The phase of the retrace relative to the PIT at the start of a fade is a timing detail for the
  fidelity wave (§2.4).

---

## 4. Clocks and randomness

* **The BIOS clock runs 8× fast** while the game runs: the timer ISR chains the BIOS handler on
  every one of its 145.65 Hz ticks instead of every 8th (ARCHITECTURE §3.1) [O code, I intent]. The
  game reads the DOS time only once, for `srand`, right after it installs the handler, so the game
  is not affected; the DOS clock is wrong after the game exits. **Drop it** [R].
* Seed: `srand(time(NULL))` at 303a:0229 with the low word of `time()` (kept at DS:2454). The RTL
  seed is the u32 at DS:442a; `rand` is the Borland LCG (`seed·0x015A4E35 + 1`, bits 16..30) and
  every call site scales as `rand()·n / 0x8000` [O]. Reuse `rtl/borland_rand` [R].
* Engine: seed from the host time like the original; a fixed seed option for tests; fidelity runs
  take DS:442a from the DOSBox-X state they start from [R].

---

## 5. CPU class: the 286 path

* `Cpu_Detect` (2c08:0032) stores 1 (8086), 2 (286), 3 (386) or 4 (486) in DS:2450; below 2 the
  game exits with code 5 [O].
* On a 286 the chimp set-up (2a80:07b1) halves the word after the count in the race's chimp list,
  in place: that word is the first chimp's room, 1 in every shipped list, so the first chimp goes to
  room 0, the workshop, at every set-up (1 → 0 → 0) [O code and data, ARCHITECTURE §2.6]. The likely
  effect is one chimp fewer in the warehouse rooms [I]. The scoping report's "the chimp count is
  halved" is wrong.
* Engine: an option `cpu = 286 | 386` (default 386, matching a 386-class reference machine [I: the
  DOSBox-X profile decides]). Implement it as the original does (halve the first room word of the
  list once per set-up, in the mutable copy of the table) so that modded lists behave the same [R].
* The 8086 refusal is dropped [R].

---

## 6. Overlays are not needed [O, R]

* The engine runs all code resident: no overlay manager, no stub segments, no INT 3Fh.
* DGROUP code pointers that point at stub entries (`stub:0020 + 5k`) are resolved through the stub
  to the overlay function and mapped to the engine function that ports it (ARCHITECTURE §1.2,
  §1.4) [R].
* Overlay loads are disk reads during play on the reference machine; they matter only as long
  frames (§2.4).
* The VROOMM import method (flattened image, `SetupOverlays.java`, `FixSwitches.java`) matters for
  reverse engineering only.

---

## 7. Options and the original switches [O 303a:005c, R names]

| Option (engine) | Original | Values | Default [R] |
|---|---|---|---|
| `sound` | `-SN` / `-S-` / `-S` (off), `-SI` (speaker), `-SA` / `-SB` / `-SM` (FM); none = detect an AdLib | `off`, `speaker`, `fm` (OPL2 at 388h on the emulated chip) | `fm` [I: the reference machine has an OPL2] |
| `cpu` | CPU detection | `286`, `386` (§5) | `386` |
| — | `-GV` (skips a BIOS display check) | — | dropped |
| — | `-?` (usage) | — | the engine's own `--help` |
| sound on/off | Ctrl+S, Options > Sound & Music | per player (DS:45cc) | from the save |
| New Player Hints | Options menu | per player (DS:45a2) | from the save |
| chimps | asked for each new player | per player (DS:4578) | from the save |

The engine may accept the original switches for familiarity (for example `--gg-args "-SI"`), as
the DOS edition accepts `--dos-args`.

---

## 8. Input [O, R]

* **Keyboard**: the G&G INT 9 model: held bits for Left, Right, Up, Down, Space and Ctrl (DS:0218),
  everything else to the BIOS queue; in play mode (DS:0096 = 1) the held keys are swallowed instead
  of queued; E0/E1 prefixes and Ctrl+C are swallowed (ARCHITECTURE §4.4). Reuse `PcKeyboard` and
  `bios_keyboard` with DOSBox-X typematic (500 ms, then 33 ms, as measured for TM DOS) [R, I].
* **Mouse**: always present in the engine (the original refuses to start without a driver). Host
  pointer → driver coordinates x 0..639, y 0..199; the game halves x; the right button is ignored.
  Model the INT 33h fn 0Ch event latches: press (first poll → zone press code | 0x4000), held
  (| 0x8000), release (release code with a tick stamp) [O]. `Mouse_SetBounds` passes the x minimum
  undoubled: keep it, it limits where the pointer can go [I].
* **Click zones**: linked lists with priorities, circles and push/pop (17ab); `Input_GetCode`'s global
  keys and code blocks as ARCHITECTURE §4.6.
* **Scripted input** (`--keys` and the fidelity scenarios) must carry mouse moves, presses and
  releases in PIT ticks, because the game cannot be played from the keyboard [R].

---

## 9. Saves [O, R]

* Keep `PLAYERS.DAT` byte for byte: header, the 100-entry roster, 99 pre-allocated 5821-byte
  records, in the engine's preference directory (separate from TM's files); offer import from the
  game folder's `PLAYERS.DAT` and an export back.
* Write exactly what the original writes: only the current player's record and roster entry
  (2b5a:01ea), at the same moments: a new player, Save & Exit, before a race (state 8), state 16,
  leaving the post-race menu after a win, after the final movie (ARCHITECTURE §6.3).
* A building in progress is resumed without a prompt (state 4).
* The original re-creates the file when the version word is not 12, and deletes it and exits when
  a write fails: the engine reports both and never destroys a file [R].

---

## 10. Quirks: keep, make optional, or drop [R]

Keep (they change what the player sees or how the game plays):

* 4- and 8-tick frame pacing with no catch-up; frames start where the previous wait returned;
* `Timer_Expired` one tick later than `Timer_Wait` for the same deadline;
* time frozen in dialog boxes only (not in the menu drop-down); sound paused in both;
* `Delay` running `Input_GetCode` (codes lost, global keys act);
* retrace-paced palette loads (2 retraces each) and fades of 12 and 9 loads;
* the random wipe (pattern by `rand()`, paced by timer slot 2) and the movie at 18 ticks per frame
  with its cues, both written straight to the screen;
* the idle code 0x40 after 30 s without a held key; 30-s attract pages; Esc in the attract loop
  quits at once;
* the 286 chimp path behind the `cpu` option;
* the right mouse button ignored; the undoubled x minimum of the mouse bounds;
* names of up to 15 characters with the first letter upper-cased; the roster in name order; resume
  without a prompt.

Make optional:

* `cpu = 286`;
* (wave 1 adds any frame-rate-dependent behaviour it finds).

Drop:

* the 8× BIOS clock; the PIT, INT 9 and INT 33h installation; the overlay manager;
* the start-up refusals (CPU below 286, display code, mouse driver, free memory) and the `-GV`
  switch;
* the `gizmo004.dat` swap file (keep the save-under in memory) and its missing error checks;
* deleting `players.dat` on a write error; DOS critical-error prompts;
* the key ring's overrun (the full test compares the wrong register), unless wave 1 finds a reader
  of the ring that depends on it;
* the dead PSG and MT-32 driver paths.

---

## 11. Verification (later waves)

* Reference captures need guest mouse input in DOSBox-X. Wave 0 provides it: the `tmctl.py` gg
  profile and the MOUSEKEY TSR (`REFERENCE_CAPTURES.md`; `re/CONTEXT.md`, G&G section). Scripted
  mouse input is posted in host time, so runs through the warehouse rooms (where the Cyber Chimps
  call `rand()` every frame) start from save states.
* Screens: pixel comparisons of settled frames, as for TM DOS (`tools/dos/imgcmp.py`).
* Sound: DOSBox-X `.dro` OPL register streams against the engine's driver output
  (`tools/dos/oplcmp.py`), song by song.
* Timing: frame logs from a logger patched into **resident** code (overlay code moves in memory),
  read with a DGROUP-aware state reader (`tools/dos/savmem.py` pattern).

---

## Open questions

1. The costs of long frames on the reference machine: room entries, overlay loads, wipes, the phase
   of the retrace at the start of a fade (§2.4).
2. Whether the help pager and the puzzle screens freeze time like the dialog loop (ARCHITECTURE §10).
3. The DAC expansion rule for a G&G capture (§3.2).
4. Whether anything reads the keyboard ring (DS:8556, read index DS:1312).
5. Whether the cursor shows during the wipe and the movie.
6. The reference machine's CPU class and sound device (they set the defaults of `cpu` and `sound`).
   The wave 0 DOSBox-X profile emulates a 486 (not the 286 path) with an OPL2
   (`REFERENCE_CAPTURES.md` §1).
