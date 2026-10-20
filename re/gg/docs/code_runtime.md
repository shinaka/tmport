# ssg.exe runtime: clock, start-up, input, graphics, palette, resources

Scope: the platform layer of *Gizmos & Gadgets!* (DOS v1.0): start-up and the command line (ovl11
303a, ovl05 2c08, `main` 12e0:0b38), `Seq_Run` mechanics, shutdown and the exit paths, the timer
and keyboard interrupts (1454), the deadline timers (152c), the input fetch (198d), the mouse
event handler (17a0), cursor and click zones (17ab), the graphics primitives (1399), the screen
buffer, palette, fades, wipes and the movie player (1473), dirty tiles and the sprite list
(1581), the picture cache (15df), memory and resources (1536, 14ff), the small DOS helpers
(1d3c) and the overlay manager as far as it affects timing (1d42). The sound driver and the
sound banks are in [code_sound.md](code_sound.md). What the game states do with these services
is in the game-logic specs; this document cites callers only to show how a service is used.

Evidence: the Ghidra export `re/gg/ghidra_export/` (disassembly read for every function cited
here; most of these segments are hand-written assembly or small C functions whose decompilation
is unreliable around near helpers and the pascal calling convention), the bytes of `ssg.exe`,
the decoders in `tools/gg/`, and the wave 0 and round 2 captures (`REFERENCE_CAPTURES.md`; the
round 2 measurements of §10 there confirm the frame periods, palette loads, fades and the wipe
below). Scratch scripts
of this pass: `extracted/scratch/gg/wave1/platform/` (`wipes.py` decodes the wipe patterns,
`fades.py` simulates the palette fades load by load).

Notation: **[O]** observed (code, bytes, decoded data, captures), **[I]** inferred, **[R]**
recommendation for the engine. `SSSS:OOOO` are Ghidra addresses of the G&G project
(`re/CONTEXT.md`, G&G section); `DS:XXXX` is DGROUP 1ea7 (file offset `0xFC70 + XXXX`). Names
are those of `re/names/gg/`; this pass's names (for example `Sprites_Add`, `Zones_AddLocal`,
`KbdRing_Pop`) were merged into the area files after wave 1 (`re/names/gg/README.md`). "Tick"
is a PIT interrupt (145.652 Hz, 6.866 ms) unless "sound tick" (72.826 Hz) is said; "retrace" is
a VGA vertical retrace (70.086 Hz). Coordinates are mode 13h pixels, x 0..319, y 0..199.

Calling conventions. C functions take their arguments left to right as listed here. The
assembly primitives `Lpx_Draw`, `Lpx_DrawKeyed`, `Gfx_DrawGlyph` and `Dirty_Mark` use the pascal
convention (callee pops); their parameters are also listed left to right as the caller pushes
them [O: `RETF 8` / `RETF 0xA` and the stack offsets].

---

## 1. Time base

### 1.1 PIT and the tick counter [O]

`Input_InstallHandlers` (1454:0000) programs PIT channel 0 with port 43h = 36h (channel 0,
lobyte/hibyte, mode 3), divisor **0x2000** (port 40h = 00h then 20h), then writes port 43h = B6h
(channel 2, lobyte/hibyte, mode 3, binary) so that the sound driver only has to write the
speaker divisor (1454:0046..0055).

```
1 193 181.67 Hz / 8192 = 145.652 1 Hz   (6.865 7 ms per tick)
sound tick = every 2nd PIT tick = 72.826 Hz (13.731 ms)
```

`Input_RemoveHandlers` (1454:0058) writes port 43h = 36h and divisor 0 (= 65536, the BIOS's
18.2 Hz) after restoring the vectors.

### 1.2 `Timer_ISR` 1454:0096 (INT 8) [O]

Per interrupt, in this order:

1. Save AX, BX, CX, DX, SI, DI, DS, ES, BP; DS = DGROUP; **`STI`** (interrupts stay enabled for
   the rest of the handler, including the BIOS call and the sound tick).
2. BIOS chain: `DS:0216 += 1`; if the result is ≥ 0 (signed): `DS:0216 = 0`, then `PUSHF` and a
   far call to the saved vector DS:0212 (the old INT 8).
3. `g_ticks` DS:021a (u32) += 1; `g_ticks16` DS:021e (u16) += 1.
4. If the new `g_ticks` is even (bit 0 clear): `Snd_Tick` 1bb4:0db3 (code_sound.md §5).
5. EOI: port 20h = 20h. Restore the registers, `IRET`.

**BIOS chain quirk.** DS:0216 is 0 in the file and its only writers are 1454:00a5 (increment)
and 1454:00b0 (reset to 0), so the counter is 1 after every increment and the old INT 8 runs on
**every** tick, not every 8th as the design (a counter reloaded with −8) suggests. The BIOS
clock, INT 1Ch and the BIOS timeouts therefore run 145.65/18.21 = **8.0 times fast** while the
game runs [O code; I intent]. The game reads the DOS time only once, for `srand`, right after
installing the handler (§2.2), so the game is not affected; any TSR that counts BIOS ticks is
(the capture TSR `MOUSEKEY.COM` times out a command after 200 BIOS ticks = 1.37 s of real time).

**Double EOI.** The BIOS INT 8 handler sends its own EOI; step 5 sends a second non-specific EOI.
With interrupts enabled from step 1 this can acknowledge another in-service interrupt early
(the keyboard, the mouse driver). It has no effect a port needs to model [I].

`g_ticks16` (DS:021e) has no reader [O: its only reference is 1454:00c5]. The only other writer
of `g_ticks` is the dialog loop `Dlg_RunModal` (3f56:066e saves it in DS:6598, 3f56:08c3..08cd
writes it back), which makes time stand still while a dialog box is open (ARCHITECTURE §3.5).

### 1.3 What runs at interrupt time [O]

| Source | Code | Effect on the game state |
|---|---|---|
| INT 8 (145.65 Hz) | `Timer_ISR` 1454:0096, `Snd_Tick` every 2nd tick | tick counter; the sound driver (which can call song cue callbacks, code_sound.md §14) |
| INT 9 | `Kbd_ISR` 1454:00e4 | held-key bits, the key ring, the BIOS keyboard buffer (§6) |
| INT 33h user handler (mask 1Fh) | `Mouse_EventHandler` 17a0:0004 | mouse latches, cursor position, and a `Dirty_Flush` when the pointer moves (§7) |

No game logic runs at interrupt time: the game's loops are ordinary nested loops paced by the
deadline timers (§5).

---

## 2. Start-up

### 2.1 Order [O]

```
c0_startup 1000:0000                  Borland C++ 3.0 C0: DGROUP, PSP, _SaveVectors (INT 0/4/5/6),
                                      heap, zeroes BSS DS:4552..bd04, _INIT_ table DS:452e:
                                        __OvrInit 1d42:08e9 (overlay buffer, INT 3Fh), _setupio,
                                        _setargv, _setenvp, tzset
main 12e0:0b38
  atexit(Game_Shutdown 12e0:0b19)
  Init_CommandLine(argc, argv, envp) 303a:005c           §2.2
  Gfx_Init 1473:0074                                     §10.1
  Font_Init 2b16:021c (fonts 9 and 10)
  PicCache_Init 15df:000d                                §16.4
  Game_Init 12e0:0afb: Actors_Init 18f5:0721, Workshop_Init 2dee:1db5, Hud_Init 1a04:025b,
                       SndRes_Init 1cdd:0004 (code_sound.md §13.1), Hint_Init 3060:0013
  g_state DS:455c = 2, then = 1                          (the first store is dead)
  Seq_Run(DS:00c2, &g_state)                             §3
  exit(0)
```

### 2.2 `Init_CommandLine` 303a:005c [O]

1. **CPU.** If `g_cpuType` DS:2450 is −1 (its value in the file): `Cpu_Detect` (2c08:0032, §18)
   into DS:2450; below 2 (8086) → `exit(5)` with no message (`ssg.bat` reports error level 5).
   This runs before the archives are opened, so no message resource can be shown.
2. `Res_OpenFiles` (14ff:0167, §16.2).
3. **Switches.** For argv[1..argc−1]: an argument whose first character is not `-` or `/` is
   ignored. Otherwise the second character, upper-cased (`toupper`), selects:
   * `?` → usage (`Init_FatalMessage(0x368)`), `exit(1)`;
   * `G` → the third character upper-cased must be `V`: `g_displayCode` DS:244e = 8; any other
     → usage, `exit(1)`;
   * `S` → the third character upper-cased is looked up in a 7-entry table at CS:0234 (ovl11):
     NUL (`-S` alone), `-`, `N` → `g_soundDevice` DS:2452 = 0; `I` → 1; `A`, `B`, `M` → 4; any
     other → usage, `exit(1)`;
   * any other letter → usage, `exit(1)`.
   Only the first three characters of an argument are read (`-SBLASTER` = `-SB`).
4. **Display.** If DS:244e is still −1: `Video_DccSupported` (2c08:0000, §18) into DS:244e. If
   DS:244e < 7 → `Init_FatalMessage(0x369)`, `exit(1)`.
5. **Mouse.** `Mouse_DriverPresent` (2c08:0011, §18) = 0 → `Init_FatalMessage(0x36a)`, `exit(1)`.
6. **Memory.** `farcoreleft()` < 0x58BEC (363 500 bytes; compared as a signed high word against
   5, then the low word unsigned against 0x8BEC) → `Init_FatalMessage(0x36c)`, `exit(1)`.
7. **Sound.** `caps = Snd_Init(DS:2452)` (1bb4:1234, code_sound.md §3); `g_fmPresent` DS:a0d8 =
   1 if caps bit 2 (OPL) else 0; if DS:2452 == 0: `Snd_Silence` (1bb4:0b97).
8. `Input_InstallHandlers` (1454:0000, §1.1, §6.1, §7.1).
9. `DS:2454 = (u16) time(NULL)` (the low word of the returned long); `srand(DS:2454)` (303a:0229).
   `time` = DOS date and time (INT 21h 2Ah/2Ch) through `dostounix` with the RTL's TZ default
   (EST5EDT). Because step 8 has just made the BIOS clock run 8× fast, the seed is the clock at
   start-up to the second.

`Init_FatalMessage(id)` (303a:0000) reads resource `id` (a bundle) and prints its members in
order with `Dos_PrintString` (INT 21h AH=09h; 1d3c:0027 temporarily writes `$` over the string's
NUL) until it reaches a member whose first payload word is 0. The texts carry their own line
breaks. Resource 0x369's first member is a 1-byte empty string (printed as nothing) [O data].

Default when no switch is given: `g_soundDevice` = −1 → `Snd_Init` detects an AdLib.

### 2.3 After `Init_CommandLine` [O]

`Gfx_Init` (§10.1) sets mode 13h, initialises the mouse cursor and builds the screen buffer;
the DAC is loaded with black and the palette is marked faded (§13.2), so nothing is visible
until the first fade-in. The first `Seq_Run` state (the attract loop) loads its own sounds.

---

## 3. `Seq_Run` 12e0:000f [O]

`Seq_Run(table, &state)` (C, far):

```
prev = −1; stop = 0
while not stop:
    f = table[*state]                       (far function pointers, 4 bytes each)
    if f == NULL: return 0
    r = f()
    if r == −2:  *state = prev              (prev is not updated)
    elif r == −1: stop = 1                  (returns r = −1)
    elif r == 0: prev = *state; *state += 1
    else:        prev = *state; *state = r  (any other value, also negative ones)
return r
```

* One step of history only: two −2 in a row stay on `prev`. A −2 from the first state would
  select `table[−1]` (no state does this) [O].
* `main` runs the 17-state table DS:00c2 (index 0 unused, entry 18 NULL); `State_01_Attract`
  (2c11:0913) runs DS:20a8, the workshop (2dee:1a90) runs DS:219a. The states themselves are
  specified in the game-logic docs; the table and return conventions are in ARCHITECTURE §2.1.
* The far pointers in the tables are image-segment far pointers, most of them to overlay stub
  entries (`stub:0020 + 5k`); a port maps each to its state function (PORTING_NOTES §6).

---

## 4. Shutdown and exit paths [O]

`exit(code)` (1000:031e) runs, in this order (Borland RTL `__exit` 1000:02c7): the `atexit`
functions in reverse order of registration (only `Game_Shutdown`), `_cleanup` (the `_EXIT_`
table DS:454c..4552: `__OvrExit` 1d42:0191 restores INT 3Fh and closes the program file), the
stream flush hook, `_restorezero` (INT 0/4/5/6), `_checknull`, the file hooks, `_terminate`
(INT 21h AH=4Ch).

`Game_Shutdown` (12e0:0b19):

1. `DS:00c0 = −1` (no reader anywhere [O]).
2. `Snd_Off` (1bb4:0e15): every active song is stopped and the hardware silenced (code_sound.md
   §12.3).
3. `Gfx_Shutdown` (1473:0103) → `Video_RestoreMode` (1399:000e): INT 10h AX=0003h only if
   `g_videoModeSet` DS:01f6 is set.
4. `Res_CloseFiles` (14ff:0209): closes the four archive handles.
5. `Init_Shutdown` (303a:0250) → `Input_RemoveHandlers` (1454:0058): INT 33h AX=0Ch with CX = 0
   (mouse handler off), INT 9 and INT 8 restored from DS:020e / DS:0212 when their saved segment
   is non-zero, PIT divisor 0.

Exit codes: 0 normal end (after the final movie, `Cmd_SaveAndExit` 4106:00b2, Esc in the
attract loop); 1 start-up refusals and out of memory (`Mem_Alloc`); 2 / 3 corrupted far heap
(`Mem_Alloc`, `Mem_Free`, `Mem_CheckHeap`, 2fcb:06c4); 3 `abort` (also `assert`); 4 overlay
error (`_c0_OvrRuntimeError` 1000:027b); 5 CPU below 286.

Original hazards on the exit paths [O code, I effect]:

* `Game_Shutdown` also runs on the start-up refusals. `Input_RemoveHandlers` then calls INT 33h
  even when `Mouse_DriverPresent` found no driver (message 0x36a): with a null INT 33h vector
  this jumps to 0000:0000. `Res_CloseFiles` closes handle 0 (stdin) four times when the
  archives were never opened (CPU exit).
* `Players_SaveCurrent`'s write error path (2b5a:010a) calls `Gfx_Shutdown` and prints a message
  before exiting (save module).

---

## 5. Deadline timers (152c) [O]

Eight u32 deadlines `g_timers` DS:6578..6597 (slot k at DS:6578 + 4k).

| Function | Behaviour |
|---|---|
| `Timer_Set(slot, n)` 152c:0009 | deadline[slot] = `g_ticks` + n; **n is a u16** (only the low word is read; every caller pushes one word) |
| `Timer_Wait(slot)` 152c:0029 | busy loop until `g_ticks` ≥ deadline (u32 compare, high word first) |
| `Timer_Expired(slot)` 152c:004a | returns 1 if `g_ticks` > deadline (**strict**), else 0 |
| `Delay(n)` 152c:0072 | end = `g_ticks` + n (u16 n); while `g_ticks` < end: `Input_GetCode()` (result discarded); the test comes first, so `Delay(0)` returns at once without polling |

* `Timer_Wait` and `Timer_Expired` disagree by one tick on the same deadline (TM Windows'
  `Timer_Expired` 10a8:0066 uses ≥).
* The 32-bit tick is read as two words with interrupts enabled; a carry between the reads can
  end a wait early once every 65 536 ticks (7.5 minutes) [I, negligible].
* `Delay` polls `Input_GetCode` as fast as it can, so the global keys of §9.3 act during a delay
  (Ctrl+S toggles the sound, F1..F3 open the menu, Esc calls the Esc hook) and every other code
  is lost [O].
* `Timer_Wait` polls nothing, and neither wait flushes the screen; the cursor still follows the
  mouse because the mouse handler flushes at interrupt time (§7.2).

Slots in use [O, all `Timer_Set` / `Timer_Wait` / `Timer_Expired` call sites]:

| Slot | Users |
|---|---|
| 0 | frame pacer: hub 12e0:03c3, warehouse 12e0:06ab, workshop 2dee:0d3c, results 2fcb:0451 (4 ticks); races 3dd6:0292, 3e77:0671 (8); `ScreenAnim_PlayModal` 174b:0530 (4); attract animations 2c11:0113 (per frame from data); puzzles 32fe, 35bb:11a3 (4); `Movie_Play` (period, §15) |
| 1 | attract page and prompt time-outs (0x111C at 2c11:072a / 08c5), attract animation 2c11:00f1 |
| 2 | `Screen_Wipe` step period (§14.2) |
| 6 | text viewer 4140 (0x1d, 0x92, 7), launch puzzle 3797:0182 / 1174 (5) |
| 7 | puzzle animations (33cd, 3938, 3a4a, 3797) |
| 3, 4, 5 | unused |

`Delay` callers: `Btn_Flash` 4034:01f2 (4), `Movie_Play` cues (§15), `State_06_Workshop`
2dee:1c76 (15), 3e77:09d3 (0x92).

Measured periods (REFERENCE_CAPTURES §10.1, AVIs at 70.086 frames/s) [O]: the results count-up
3.94 ticks per update (50 updates), races 7.94, the warehouse walk and spring animations 12.0 (3
frames of 4 ticks), the hub windmill 16.1, the movie 18.0; no frame was seen to overrun in these
spans. A movie `Delay(1460)` cue lasts 10.06 s (705-706 video frames: 1460 ticks plus the frame
period).

---

## 6. Keyboard

### 6.1 Installation [O]

`Input_InstallHandlers` saves the INT 9 vector in DS:020e (offset) / DS:0210 (segment) and
installs `Kbd_ISR` 1454:00e4, then saves INT 8 in DS:0212 / DS:0214 and installs `Timer_ISR`
1454:0096 (both through INT 21h 35h/25h), then programs the PIT (§1.1). The mouse handler is
registered first (§7.1).

### 6.2 `Kbd_ISR` 1454:00e4 (INT 9) [O]

```
save registers; DS = ES = DGROUP; STI
b = IN 60h
if b == E0h or b == E1h:  acknowledge and swallow
table = make handlers DS:022c, make = 1
if b & 80h: table = break handlers DS:023a, b &= 7Fh, make = 0
i = index of b in the scan table DS:0224 (8 bytes: 4B 4D 48 50 39 1D 2E 00; REPNE SCASB over 8,
    a match on the trailing 0 counts as no match)
if found:
    r = table[i]()                                   (near handler, result in AX)
    if r < 0:                     acknowledge and swallow
    if r == 0 and g_kbdPlayMode DS:0096 == 1:  acknowledge and swallow
    (r > 0, or r == 0 outside play mode: pass on)
pass on:
    if make: write a ring record (§6.4)
    PUSHF; far call the old INT 9 (DS:020e)          (the BIOS reads port 60h itself, acknowledges, EOIs)
acknowledge and swallow:
    IN 61h; OUT 61h with bit 7 set; OUT 61h with bit 7 clear; OUT 20h, 20h
restore registers; IRET
```

Handlers (tables DS:022c make, DS:023a break; one near function each):

| Scan | Key | Make | Break | Held bit DS:0218 | Result |
|---|---|---|---|---|---|
| 4B | Left | 1454:0187 | 1454:018f | 0x01 | 0 |
| 4D | Right | 1454:0197 | 1454:019f | 0x02 | 0 |
| 48 | Up | 1454:01cf | 1454:01d7 | 0x04 | 0 |
| 50 | Down | 1454:01df | 1454:01e7 | 0x08 | 0 |
| 39 | Space | 1454:01a7 | 1454:01af | 0x20 | 0 |
| 1D | Ctrl | 1454:01b7 (also `g_ctrlHeld` DS:020c = 1) | 1454:01c3 (DS:020c = 0) | 0x10 | 1 (always passed on) |
| 2E | C | 1454:01ef | 1454:01ef | – | −1 (swallowed) while DS:020c = 1, else 1 |

Consequences [O code; I where marked]:

* The four arrows and Space set and clear their held bits always; in **play mode** their make
  and break codes (typematic repeats included) are swallowed, so they produce no BIOS key and no
  ring record. Outside play mode they reach the BIOS and produce key words 0x14B, 0x14D, 0x148,
  0x150 and 0x20.
* Ctrl is tracked and always reaches the BIOS (needed for Ctrl+S and Ctrl+R).
* C (make and break) is swallowed while Ctrl is held: no Ctrl+C. Ctrl+Break still reaches the
  BIOS as scan 46h with Ctrl held [I: the BIOS then raises INT 1Bh; no handler is installed].
* **E0/E1 prefixes are swallowed**, so the BIOS never sees them: the grey arrow keys arrive as
  the keypad arrows (with the keyboard's fake-shift bytes E0 2A / E0 AA reduced to a plain
  left-Shift make/break, which keeps them arrows under NumLock), grey Enter as Enter, right Ctrl
  as Ctrl; Pause (E1 1D 45 E1 9D C5) arrives as Ctrl make, NumLock make, Ctrl break, NumLock
  break [I: standard BIOS behaviour for those bytes].
* `g_kbdPlayMode` DS:0096 writers: `State_05_EnterRoom` 12e0:0972 sets 1; the hub (12e0:0193),
  the sign-in (2caa:1249) and `State_06_Workshop` (2dee:1aa5) clear it; `Dlg_RunModal`
  3f56:0682 and the menu drop-down 40a4:024b clear it and restore the old value at exit
  (3f56:088c, 40a4:0468). So the arrows and Space are swallowed in the warehouse rooms only.

### 6.3 BIOS key words [O]

* `Kbd_BiosGetKey` (1d3c:0004): INT 16h AH=00h; returns AL (ASCII, AH cleared) when AL ≠ 0,
  else `0x100 | scan`. Examples: Esc 0x1B, Enter 0x0D, Ctrl+S 0x13, Alt+O 0x118, Alt+F 0x121,
  Alt+H 0x123, F1..F3 0x13B..0x13D, PgUp 0x149, PgDn 0x151, Left 0x14B.
* `Kbd_BiosPeekKey` (1d3c:0015): INT 16h AH=01h; 0 when the buffer is empty, the raw word (scan
  in AH) when a key waits, 0x100 when the waiting word is 0000h (Ctrl+Break).

### 6.4 The key ring [O]

16 records of 16 bytes at `g_kbdRing` DS:8556..8655; write index DS:1310, read index DS:1312
(both 0..15, 0 at start).

* Writer (`Kbd_ISR` 1454:0148..0175, make codes that are passed on only): record[write] word +0 =
  3, u32 +6 = `g_ticks`; the other ten bytes are not written. Then `write = (write + 1) & 15`.
  The full test compares BX (a handler-table address, 0x22C..0x248) with the read index, so it
  never fires: **the ring overruns silently**; 16 unread records make write == read and the ring
  looks empty.
* Reader `KbdRing_Pop` (198d:0084, near): NULL if write == read, else the record at read and
  `read = (read + 1) & 15`.
* A record does not hold the key: it only allows one BIOS key to be taken. Shift, Ctrl and Alt
  makes create records but no BIOS key; such a record later yields code 0 (§9.2).

`Kbd_PopQueuedKey` (198d:00d4): under CLI, pops one record; if it is of type 3 and the BIOS has
a key, takes and returns it; else 0. When the ring is empty the NULL record pointer is
dereferenced (it reads 0000:0000, almost never 3). Callers: the LZH input refill 1b48:002e,
which calls it repeatedly until it returns 0 after each 4000-byte file read — **keys typed while
a compressed resource is being decoded are thrown away**; and the races (3dd6:01ca,
3e77:05e3).

### 6.5 Held keys and the idle bit: `Input_GetHeldKeys` 198d:02b8 [O]

(The wave 0 name was `Input_CheckIdle`.) Returns a u32: the low word is `g_heldKeys` DS:0218,
plus 0x40 once after 0x111C ticks (30.07 s) without a held key; the high word is the word at
DS:7ad2 (actor 0 + 0x48).

```
if g_heldKeys != 0:
    DS:5c1f = 1; DS:1314 (u32) = g_ticks; DS:8554 = 0
elif g_ticks > DS:1314 + 0x111C (strict) and DS:8554 == 0:
    low |= 0x40; DS:8554 = 1
high = DS:7ad2
```

Only held bits count as activity (mouse use does not). DS:1314 is also reset when a warehouse
room (12e0:0683) or the workshop (2dee:1cd6) starts. Its only caller is the player actor
(18f5:029b). It is not part of `Input_GetCode` (wave 0's ARCHITECTURE §4.6 listed 0x40 there; corrected).

---

## 7. Mouse

### 7.1 Driver set-up [O]

* `Input_InstallHandlers` (1454:0000) registers `Mouse_EventHandler` 17a0:0004 with INT 33h
  AX=0Ch, CX = 1Fh (move, left press, left release, right press, right release).
* `Mouse_Init` (17ab:000e, called by `Gfx_Init`): `g_cursorShown` DS:7a7c = 0; the default
  cursor record = `Mouse_LoadCursors(0)` (picture resource 0x0000, a 9×9 RLE arrow with key 1)
  into DS:7a82; INT 33h AX=03h: `g_mouseX` DS:7a88 = CX/2, `g_mouseY` DS:7a86 = DX; the cursor
  region list DS:0a8c = `List_New()` with the default region DS:0a54 added (§12.1); the default
  regions' record pointers DS:0a6c and DS:0a88 = the arrow; hot spot of the arrow = (0, 0).
* The driver's x range is 0..639 in mode 13h; the game halves it everywhere.

### 7.2 `Mouse_EventHandler` 17a0:0004 [O]

Called by the driver with AX = event bits, BX = buttons, CX = x (0..639), DX = y.

```
save registers; DS = DGROUP
g_mouseButtons DS:0a0c = BX
x = CX >> 1
if AX & 1 (moved) and g_mouseHideState DS:01e2 == 0:
    Mouse_MoveCursor(x, DX):      Mouse_RestoreUnder(); g_mouseX = x; g_mouseY = DX; Dirty_Flush()
    if BX == 1 (left only):       press record: DS:0a10 = 1, DS:0a1a = x + hotX, DS:0a1c = y + hotY
if AX & 2 (left pressed):
    DS:0a10 = 1; DS:0a1a = x + hotX; DS:0a1c = DX + hotY; g_mouseNewPress DS:0a1e = 1
    release record: DS:0a20 = 0
    (the release bit of the same event is not examined)
elif AX & 4 (left released):
    DS:0a20 = 2; DS:0a2a = x + hotX; DS:0a2c = DX + hotY; DS:0a26 (u32) = g_ticks
    DS:0a10 = 0
right-button bits: ignored
```

hotX / hotY are the current cursor's hot spot DS:7a74 / DS:7a76 (§12.1). Round 2 saw it [O]: with
the wire-spool cursor of the electric puzzle (hot spot 36, 8) a press at pointer (138, 120) was
latched at (174, 128), and the zones were hit there (REFERENCE_CAPTURES §11).

* A move is **dropped** while `g_mouseHideState` ≠ 0, i.e. between the first drawing call of a
  frame and that frame's `Dirty_Flush` (§11.4); in the hub single moves were often dropped in the
  captures. The position then stays at the last processed
  move until the next move event; presses and releases always carry their own coordinates.
* While the left button is held, a processed move also updates the press record's position
  (drags).
* When a move is processed the cursor is redrawn at interrupt time: `Mouse_RestoreUnder` marks
  the old cursor area dirty, and `Dirty_Flush` draws the cursor at the new place and copies the
  dirty tiles. So the cursor follows the mouse while the main loop waits.
* The latches are two 16-byte input records with the ring's layout (§6.4): press record at
  DS:0a10 (+0 held flag, +0Ah x, +0Ch y, +0Eh first-poll flag), release record at DS:0a20 (+0 =
  2 when pending, +06h u32 tick, +0Ah x, +0Ch y).
* `g_mouseButtons` DS:0a0c (bit 0 left, bit 1 right) is read directly by several puzzles
  (3083, 32fe, 33cd, 35bb).

### 7.3 Pointer limits: `Mouse_SetBounds(x, y, w, h)` 17ab:017f [O]

```
g_mouseMaxX DS:7a64 = x + w − cursorW (DS:7a70);   g_mouseMaxY DS:7a62 = y + h − cursorH (DS:7a72)
if g_mouseX > maxX: g_mouseX = maxX;   if g_mouseY > maxY: g_mouseY = maxY      (no lower clamp)
INT 33h AX=07h: CX = x (not doubled), DX = 2·maxX
INT 33h AX=08h: CX = y, DX = maxY
```

* The callers pass w = 319 and h = 199 for the full screen (`Mouse_SetBoundsFull` 17ab:01da,
  `Mouse_DrawCursor` 17ab:06b2), so the cursor picture always stays one pixel away from the right
  and bottom edges: its right edge reaches at most x 318 and its bottom row y 198.
* `Mouse_DrawCursor` calls `Mouse_SetBounds(0, 0, 319, 199)` every time the active cursor record
  changes (§12.2): a cursor change resets any narrower bounds.
* The x minimum is not doubled (it should be 2x in driver units), but every caller passes x = 0
  (17ab:01e3, 17ab:06b8, `Hud_SelectPart` 1a04:09bf: (0, 165, 319, 34), 2dee:0b9d: (0, 9, 319,
  154)), so it has no effect [O].
* Driver ranges seen in the captures (sign-in x ≤ 620, hub x ≤ 540) are 2·maxX of the current
  cursor size (REFERENCE_CAPTURES §2.3).

---

## 8. Click zones (17ab)

### 8.1 Zone record (22 bytes) [O: `Zones_HitTest` 17ab:0507, `Zone_Init` 17ab:083b, `Input_HitZones` 198d:0008, `Zones_RemoveLocalGroup` 17ab:07f4]

| Offset | Field |
|---|---|
| +00h | shape: 0 rectangle, any other value circle |
| +02h, +04h | x, y (rectangle: top-left; circle: centre) |
| +06h | width (circle: radius) |
| +08h | height (unused by circles) |
| +0Ah | priority (signed; must be ≥ 0 to ever be chosen) |
| +0Ch | far next (linked list) |
| +10h | press code (returned while the button is down) |
| +12h | release code (returned on release) |
| +14h | group id (`Zones_RemoveLocalGroup`) |

Static zone records live in DGROUP (for example the hub DS:0132, the workshop DS:21ce and
DS:2268, the post-race buttons DS:22fe, the attract's full-screen zone DS:0a3a = {rect 0, 0,
320, 200, priority 5, press 0, release 0x0D}, which turns a click anywhere into Enter;
2c11:0949). `Zone_Init(zone, x, y, w, h, priority, press, release)` (17ab:083b) fills a
rectangle record with next = NULL (it does not set +14h).

### 8.2 Lists [O]

Two singly linked lists, newest first: **local** `g_zonesLocal` DS:0a32 (far) and **global**
`g_zonesGlobal` DS:0a36 (far; menu titles, HUD).

| Function | Effect |
|---|---|
| `Zones_AddLocal(zone)` 17ab:0294 | zone.next = local head; head = zone |
| `Zones_AddGlobal(zone)` 17ab:02ba | same on the global list |
| `Zones_AddLocalArray(zones, n)` 17ab:02de | adds n consecutive 22-byte records (the last one ends at the head) |
| `Zones_RemoveLocal(zone)` 17ab:0314 | unlinks the zone (pointer compare) and clears its next |
| `Zones_RemoveGlobal(zone)` 17ab:03b7 | unlinks from the global list (next not cleared) |
| `Zones_RemoveLocalArray(zones, n)` 17ab:0441 | n × `Zones_RemoveLocal` |
| `Zones_RemoveLocalGroup(id)` 17ab:07f4 | removes every local zone whose +14h equals id |
| `Zones_ClearLocal()` 17ab:0465 | clears every local zone's next, head = NULL |
| `Zones_Push()` 17ab:04bd | stack DS:7a4e[depth] = local head; depth DS:0a50 += 1; local head = NULL |
| `Zones_Pop()` 17ab:04e8 | depth −= 1; local head = stack[depth] |

The stack has room for 5 entries before it runs into DS:7a62 (the pointer limits); there is no
overflow or underflow check [O layout; I: nesting never exceeds a few levels].

### 8.3 `Zones_HitTest(x, y, list)` 17ab:0507 [O]

```
best = NULL; bestPri = −1
for zone in list (following +0Ch):
    if shape == 0: hit = x ≥ zx and zx + w > x and y ≥ zy and zy + h > y     (half-open, signed)
    else:          hit = r² > dx² + dy²   (dx = x − zx, dy = y − zy as 32-bit; r² = 16-bit IMUL
                                           of +06h, sign-extended: strict, so the circle's rim
                                           is outside)
    if hit and priority > bestPri (signed): best = zone; bestPri = priority
return best
```

Ties keep the zone found first, i.e. the one added last.

`Input_HitZones(record)` (198d:0008, near): tests the local list, then (only if nothing was
hit there) the global list, at the record's x/y (+0Ah/+0Ch); returns the hit zone's press code
(+10h) if record +0 == 1, else its release code (+12h); 0 if no zone was hit. A local zone thus
beats any global zone regardless of priority.

---

## 9. `Input_GetCode` 198d:0106 and related

### 9.1 Algorithm [O]

```
code = 0
if press record DS:0a10 != 0:                          (button held)
    code = Input_HitZones(&DS:0a10)                    (press code)
    if g_mouseNewPress DS:0a1e == 1:
        DS:0a1e = 0;  if code: code |= 0x4000           (first poll after the press)
    elif code: code |= 0x8000                          (later polls while held)
if release record DS:0a20 != 0:
    DS:0a20 = 0; code = Input_HitZones(&DS:0a20)       (release code; replaces any press code)
CLI
rec = KbdRing_Pop()
if rec:
    type 1: keep code;  type 2: code = Input_HitZones(rec);  type 3: code = BIOS key if one waits, else 0
STI
global keys (§9.3) on the exact code (flags included); return code
```

* **A key record overrides the mouse.** If a ring record is popped, the mouse code computed above
  is replaced by the BIOS key or by 0. A Shift/Ctrl/Alt make, a typematic repeat whose key was
  already taken, or any record without a waiting BIOS key therefore **erases a click** that
  arrives in the same poll (REFERENCE_CAPTURES §2.2 hit exactly this with the first TSR).
  Type 1 and 2 records are never written [O: the only writer writes 3].
* A press and its release that both happen between two polls: the held flag is already 0, so no
  press code is returned and DS:0a1e stays 1 (until the next press); the release code is
  returned [O].
* If no zone is under the press, the first poll still clears DS:0a1e.
* Codes are 16-bit; zone codes with bit 14 or 15 set would be ambiguous (none are).

### 9.2 Code sources [O]

| Source | Code |
|---|---|
| left button held over a zone | press code \| 0x4000 on the first poll, \| 0x8000 later |
| left button released over a zone | release code (once) |
| key | BIOS word: ASCII, or 0x100 \| scan |
| menu request DS:130e = 1 / 2 (set by the menu commands 4106:0166 / 00ee) | 0x17A2 / 0x17A3, but only right after the menu hook has run (§9.3) |
| nothing | 0 |

### 9.3 Global keys (12-entry table CS:0275, handlers CS:028d) [O]

Matched against the full code (a press code with 0x4000 does not match):

| Code | Action | Returned code |
|---|---|---|
| 0x0013 Ctrl+S | `Snd_ToggleOption` 1cdd:05c8 (code_sound.md §12.2) | 0 |
| 0x001B Esc | if `g_escHandler` DS:8656 is set: call it | 0x1B |
| 0x0123 Alt+H, 0x013B F1 | → menu index 0 (Help) | see below |
| 0x0121 Alt+F, 0x013C F2 | → menu index 1 (File) | see below |
| 0x0118 Alt+O, 0x013D F3 | → menu index 2 (Options) | see below |
| 6000..6003 | `Hud_TrashCursorPart` 1a04:0a82, `Hud_PutBackCursorPart` 0b5b, `Hud_ScrollLeftArrow` 08ad, `Hud_ScrollRightArrow` 084c | 0 |
| not in the table, 5000..5100 (unsigned) | `Hud_SelectPart(code − 5000)` 1a04:090e | 0 |

Menu keys: the code becomes 0x13B + index; then **the Esc hook is called** (if set) as for Esc;
then, if the menu hook `g_menuHandler` DS:865a is set and `g_menuDisabled` DS:054e is 0, the
hook is called with the index (the drop-down 40a4:020b), the code becomes 0, and DS:130e is
turned into 0x17A2 (1) or 0x17A3 (2) and cleared. If the hook is not set or the menu is
disabled, the code 0x13B..0x13D is returned. The menu bar's title zones use the codes 0x13B +
index, so a click on a title is the same as its F key (menu record order: Help, File, Options;
ARCHITECTURE §5.5).

Hooks: the warehouse sets the Esc hook to 1a04:0d65 (12e0:0628) and the workshop to 2dee via
its stub (2dee:1ab1); both clear it on exit. `Dlg_RunModal` saves and clears the menu hook
(3f56:0693..06a6) and restores it (3f56:0882); the menu bar install sets it (40a4:0025, 0459).
`g_menuDisabled` DS:054e is set only inside `Res_Read` (14ff:0018, cleared at 14ff:00f2) and
`Input_GetCode` is never called from there, so in practice it never blocks the menu [O].

### 9.4 Helpers [O]

* `Input_Flush` (198d:00bf): while the ring is not empty, `Input_GetCode()` (codes discarded,
  global keys still act). It does nothing about the mouse latches when the ring is empty.
* `Input_WaitCode` (198d:02a5): calls `Input_GetCode` until it returns non-zero; no pacing, no
  screen flush.
* `Input_StripHeld(code)` (198d:0328): 0 if bit 15 is set, else code with bit 14 cleared (a
  press code once per press). Used by the puzzles.

---

## 10. Screen buffer and graphics primitives (1399, 1473)

### 10.1 `Gfx_Init` 1473:0074 [O]

1. `Video_SetMode13h` (12e0:0b92): INT 10h AX=0013h, `g_videoModeSet` DS:01f6 = 1.
2. `Mouse_Init` (§7.1).
3. `Mem_Alloc(0x1F400)` (128 000 bytes) → `g_screenBuffer` DS:0248 (far).
4. Row table `g_rowTable` DS:5f22: 400 far pointers; row 0 = the buffer, row i = the normalised
   pointer (`Ptr_Normalize` 1d3c:004d: segment += offset >> 4, offset &= 15) of row i−1 + 320.
   Rows 0..199 are the **screen** (composed picture), rows 200..399 the **back page** (clean
   background used to erase sprites).
5. `g_drawColor` DS:01f4 = 0; `Gfx_FillRect(0, 0, 320, 200)` (screen black, all tiles dirty);
   `Gfx_SaveToBack(0, 0, 320, 200)` (back page black).
6. `Pal_Blackout` (1473:000c): `Pal_SetRange` of 768 zero bytes (2 retraces), `g_palFaded`
   DS:024c = 1.

VRAM is the far pointer DS:01f8 = A000:0000. Only the tile flush (§11.2), the wipe (§14.2) and
the movie (§15) write it.

### 10.2 Clip rectangle [O]

DS:01e4 left = 0, DS:01e6 right = 319, DS:01e8 top = 0, DS:01ea bottom = 199 (inclusive
values in the file). The workshop narrows x to 0x5D..0xE2 temporarily (2dee:0821, restored at
2dee:0871) and the launch puzzle changes all four (3797:000e..006a, 0ea2..0ef1). Only
`Gfx_HLine` (reject only), `Gfx_VLine` (y), `Gfx_GetImage` and the keyed raw blit use them.

Two near helpers with an **exclusive** reading of the right and bottom bounds:

* `Gfx_ClipX` (1399:0021; AX = x, CX = length): CF = 1 (reject) if x > right; if x < left:
  length += x, x = left (correct only for left = 0); then if x + length > right: length −=
  (x + length) − (right − left). With the default bounds a span reaching column 319 loses that
  column.
* `Gfx_ClipY` (1399:004c; BX = y, DX = height): never rejects; if y < top: **height −= y** (the
  sign is wrong: a negative y makes the span longer) and y = top; then the same bottom rule as
  above, so a span reaching row 199 loses it.

The keyed raw blit (§10.4) clips to the inclusive rectangle instead.

### 10.3 Primitives [O]

Every primitive except the near blitters, `Gfx_RemapPicture` and the movie decoder first calls
`Gfx_HideMouseOnce` (1399:0b22, §12.3). "Marks" means `Dirty_Mark(x, y, w, h)` (§11.1).

| Function | Pixels | Clipping | Marks |
|---|---|---|---|
| `Gfx_HLine(x, y, len)` 1399:0072 | `len` bytes of `g_drawColor` from (x, y) | `Gfx_ClipX` only decides rejection; the **unclipped** x and len are drawn | (x, y, len, 1), unclipped, before drawing |
| `Gfx_VLine(x, y, len)` 1399:00b3 | `len` pixels down from (x, y) | y by `Gfx_ClipY` (applied); x none | (x, y, 1, len), before clipping |
| `Gfx_FillRect(x, y, w, h)` 1399:0100 | w×h of `g_drawColor` | none | (x, y, w, h) |
| `Gfx_XorRect(x, y, w, h)` 1399:0146 | each pixel ^= 0x0F | none | (x, y, w, h) |
| `Gfx_GetImage(x, y, w, h, buf)` 1399:0194 | buf = {u16 w', u16 h', w'×h' bytes} of the screen | x and y (applied; reject only if x > right) | none |
| `Gfx_PutImage(x, y, buf)` 1399:01f1 | copies buf's w×h to (x, y) | none | (x, y, w, h) |
| `Gfx_RestoreRect(x, y, w, h)` 1399:0233 | rows y+200.. (back page) → rows y.. | x < 0: w += x, x = 0, nothing if w < 0 | (x, y, w, h) after the x clip |
| `Gfx_SaveToBack(x, y, w, h)` 1399:02a2 | rows y.. (screen) → rows y+200.. (back page) | none | none |
| `Gfx_PutPixel(x, y, c)` 1399:0b45 | one pixel | none | (x, y, 1, 1) after drawing |
| `Gfx_DrawGlyph(x, y, glyph, –)` 1399:0541 | 1-bpp glyph in `g_glyphColor` DS:1f6c | none | **none** (the font code marks) |
| `Gfx_RemapPicture(pic, lut)` 1399:0b71 | raw LPX pictures only: every pixel = lut[pixel], **in place in the picture**; RLE pictures (format bit 0) untouched | – | – |
| `Gfx_FrameRect(x, y, w, h)` 1473:010d | HLine(x, y, w), VLine(x, y, h), HLine(x, y+h−1, w), VLine(x+w−1, y, h) | as those | as those |

`Gfx_SaveToBack` was named `Gfx_CopyFromBack` in wave 0, and `Screen_SavePlayArea` (1473:0337)
`Screen_RestorePlayArea`: both copy **screen → back page** (destination `LES DI` = row y+200,
source `LDS SI` = row y; 1399:02af..02cf). The opposite copy is `Gfx_RestoreRect`.

Screen helpers in 1473 [O]: `Screen_SavePlayArea` (0337) = `Gfx_SaveToBack(0, 0, 320, 165)`;
`Screen_SaveAll` (034e) = `Gfx_SaveToBack(0, 0, 320, 200)`; `Screen_RestoreAll` (0365) =
`Gfx_RestoreRect(0, 0, 320, 200)`; `Screen_ClearPlayArea` (037c) = colour 2, `Gfx_FillRect(0, 9,
320, 155)` (rows 9..163); `Gfx_ImageSize(w, h)` (040e) = w·h + 4; `Gfx_RecolorPicture(pic, from,
to)` (077e): identity LUT, then lut[from[i]] = to[i] for i = 0.. until to[i] == 0xFF, then
`Gfx_RemapPicture`; `Obj_DrawKeyed(rec)` (03df): `Lpx_DrawKeyed(rec+2 x, rec+4 y − (rec+70h if
rec+0 & 0x1000), rec+0Ah picture, mode = rec+0)` for the large object records (parts) [I which
records].

Glyph data (`Gfx_DrawGlyph`): u8 full bytes per row, u8 bits in a last partial byte, u16 rows,
then the rows; bit 0 of each byte is the leftmost pixel; set bits are drawn, clear bits left
alone (format: `fmt_graphics.md` §5).

### 10.4 LPX blitters [O]

`Lpx_Draw(x, y, pic)` (1399:0335): marks (x, y, width, height) unclipped, then calls
`g_lpxBlitters` DS:0204[format]: 0 → `Lpx_BlitRaw` 1399:036d, 1 → `Lpx_BlitRle` 1399:0465.

`Lpx_DrawKeyed(x, y, pic, mode)` (1399:02ee): calls `g_lpxKeyedBlitters` DS:01fc[(format + mode)
& 3]: 0 and 2 → `Lpx_BlitRawKeyed` 1399:0399, 1 → `Lpx_BlitRle`, 3 → `Lpx_BlitRleMirror`
1399:04d1. Each blitter leaves the drawn rectangle in DS:01ec..01f2 (x, y, w, h); afterwards, if
that x is ≥ 0, `Dirty_Mark(x, y, w, h)`. The mode is the caller's flag word (sprites, actors) or
0; with mode 2 an RLE picture is mirrored and a raw one is not; modes 1 and 3 would swap the
decoders (no caller found that passes them on purpose [I]).

| Blitter | Behaviour |
|---|---|
| `Lpx_BlitRaw` | w×h bytes, row stride 320, **no clipping**; height is a 16-bit count |
| `Lpx_BlitRawKeyed` | key = low byte of the header word +10; pixels equal to it are skipped; height is **8-bit** (the key overwrites the high byte); clipped to the **inclusive** rectangle [left..right]×[top..bottom]; **y is compared unsigned**, so a picture with y < 0 is rejected entirely (no top clipping of negative y); negative x is clipped; a rejected picture sets DS:01ec = −1 (no mark) |
| `Lpx_BlitRle` | per row: remaining width in an 8-bit register; code 0, n: skip n pixels; code c ≥ 1: copy c bytes; the row ends when the 8-bit remainder reaches 0; height 8-bit; **no clipping**; DS:01ec..01f2 = the unclipped x, y, w, h |
| `Lpx_BlitRleMirror` | as `Lpx_BlitRle`, starting at x + w − 1 and storing right to left; next row at +320 + w |

Notes [O code]: the RLE copy path computes the copy count with `RCR CL,1` after `SUB BL,CL`, so a
literal run longer than the remaining width would copy about 256 extra bytes; well-formed data
never does this (every shipped row sums exactly, `lpx.py --check`). An RLE picture drawn
partly off the right edge wraps into the next rows; one drawn at negative x is drawn before the
row start and **is not marked dirty**.

### 10.5 Movie frame decoder `Movie_DecodeFrame(src, dst)` 1399:0a85 [O]

Writes straight to `dst` (A000:0000), without `Gfx_HideMouseOnce` and without marks; returns the
end offset. Coding (`fmt_graphics.md` §7.3): `01..7F` literal run; `00 n v` fill (v = 1 skips);
`80 w` end (w = 0) / skip w (< 0x8000) / literal w & 0x3FFF (0x8000..0xBFFF) / fill (≥ 0xC000, v = 1
skips); `81..FF` skip code − 0x80. Word-sized fills and copies are aligned on even addresses
(no visible effect).

---

## 11. Dirty tiles, flush and the sprite list (1581)

### 11.1 `Dirty_Mark(x, y, w, h)` 1581:03ee [O]

The map is 40×25 bytes **in the code segment** at 1581:0006..03ed (row-major, 1 = dirty).

```
if y ≥ 200 (unsigned, so negative y too): return
first = 6 + (y >> 3)·40 + (x >> 3)          (x shifted unsigned: negative x writes far outside)
cols = ceil(((x & 7) + min(w, 320)) / 8);   rows = ceil(((y & 7) + h) / 8)
set rows × cols bytes to 1 (stride 40); g_dirtyPending DS:08b6 += 1
```

No right or bottom clamp: a rectangle past x 319 wraps into the next tile row; one past row 24
would write into the code of `Dirty_Mark` itself (1581:03ee follows the map). The shipped
game's callers stay inside [I: the keyed blitter clips first, full-screen draws are 320×200,
sprites are clamped]. **[R]** clamp in the engine.

`Dirty_Clear` (1581:0470): zeroes the map and DS:08b6.

### 11.2 `Dirty_Flush` 1581:052a [O]

```
if g_dirtyPending DS:08b6 == 0: return              (nothing at all is done: no cursor draw, no hide reset)
Mouse_DrawCursor()                                    §12.2 (draws the cursor into the screen rows, marks it)
for tile row 0..24, tile column 0..39:
    if map byte: clear it; count += 1; copy the 8×8 block from the screen rows to A000 (8 rows of 4 words)
Gfx_ResetMouseHide()                                  g_mouseHideState DS:01e2 = 0
DS:08b6 = 0; DS:08bc += 1; DS:08b8 (u32) += count     (DS:08b8/08bc are statistics; no reader)
```

The copy reads the screen as one contiguous 64 000-byte block from the normalised row 0
pointer. There is **no retrace wait**: a flush can be seen half done (tearing) [O code; I
visibility].

### 11.3 Sprite list [O]

A list of up to 50 records of 14 bytes at DS:05f6, count DS:05f4, write pointer DS:08b2 (no
overflow check; the 51st record would overwrite the write pointer):

| Offset | Field |
|---|---|
| +00h | flags: bits 0-1 blit mode, bit 4 = do not draw, bit 7 = do not erase |
| +02h, +04h | x, y |
| +06h, +08h | w, h (erase rectangle) |
| +0Ah | far picture |

| Function | Effect |
|---|---|
| `Sprites_Reset` 1581:0484 | count = 0, write pointer = DS:05f6 |
| `Sprites_Add(rec)` 1581:0496 | copies 14 bytes, count += 1 |
| `Sprites_EraseAll` 1581:04b6 | for each record without bit 7: `Gfx_RestoreRect(x, y, w, h)`; then `Sprites_Reset` |
| `Sprites_DrawAll` 1581:04fc | for each record without bit 4, in insertion order: `Lpx_DrawKeyed(x, y, picture, flags)` |

Typical frame (warehouse, 12e0:06a7..0714): `Timer_Set(0, 4)`, `Actors_Tick`, `Sprites_EraseAll`,
room layers and actors add sprites, `Sprites_DrawAll`, `Hud_Update`, `Dirty_Flush`,
`Timer_Wait(0)`, `Input_GetCode`, dispatch. Because the erase of frame n happens at the start of
frame n+1, the back page must hold the clean background at all times; `Gfx_SaveToBack` (via
`Screen_SavePlayArea` / `Screen_SaveAll`) is called after a background has been drawn.

### 11.4 Interplay with the cursor [O]

`g_mouseHideState` DS:01e2: 0 after a flush; the first primitive call sets it non-zero and
removes the cursor from the screen rows (§12.3); the next flush draws the cursor again and sets
0. The mouse handler only processes moves while it is 0 (§7.2). A frame that calls only
primitives that do not mark (`Gfx_GetImage`, `Gfx_SaveToBack`, `Gfx_DrawGlyph`, a fully clipped
keyed blit) while the cursor is hidden by `Mouse_Hide` leaves DS:08b6 = 0, so its flush returns
early and the hide state stays non-zero: mouse moves are then ignored until something is marked
and flushed [O code; I how often].

---

## 12. Software cursor (17ab)

### 12.1 Records [O]

**Cursor record** (24 bytes, `Mem_Alloc(0x18)` in `Mouse_LoadCursors(src)` 17ab:0083; src =
resource id when its high word is 0, else a far picture):

| Offset | Field |
|---|---|
| +00h | resource id, or −1 when built from a picture pointer |
| +02h, +04h | save-under position (only in the active copy) |
| +06h, +08h | width, height (from the picture header) |
| +0Ah, +0Ch | hot spot x, y (0 at creation; `Mouse_SetHotSpot(rec or NULL, hx, hy)` 17ab:0222 sets the values ≥ 0; NULL = the active copy) |
| +0Eh | far picture (LPX) |
| +12h | shown flag (active copy) |
| +14h | far save-under buffer (w·h + 4 bytes) |

The **active cursor** is a 24-byte copy at DS:7a6a: DS:7a6c/7a6e save-under x/y, DS:7a70/7a72
w/h, DS:7a74/7a76 hot spot, DS:7a78 picture, `g_cursorShown` DS:7a7c, DS:7a7e save buffer;
DS:7a4a points at the record it was copied from; DS:7a82 is the arrow.
`Mouse_FreeCursor(rec)` 17ab:0140 frees the save buffer, the picture if it was loaded by id,
and the record. `Mouse_RestoreDefaultCursor` 17ab:0263 selects the arrow
(`Mouse_SetCursorPicture(DS:7a82)`) and frees the record DS:7a4a pointed at.

**Cursor regions** (28-byte items in the list DS:0a8c, 19c0 list helpers): +00h..+07h list links,
+08h an embedded zone (shape, x, y, w, h, priority at +12h, next at +14h = NULL), +18h far cursor
record. The default region DS:0a54 = full screen (0, 0, 320, 200, priority 0) with the arrow;
the forced region DS:0a70 (same rectangle). `MouseRegion_Add(item)` 17ab:0754,
`MouseRegion_AddArray(items, n)` 17ab:0771, `MouseRegion_Clear()` 17ab:07a4 (removes all items
but the last, the default). `Mouse_SetCursorPicture(rec)` 17ab:01f0 stores rec into the default
region's record pointer DS:0a6c (restore under first, flush after) and returns the old one.
`Mouse_ForceDefault(on)` 17ab:07dd sets DS:0a52 and returns the old value (dialogs, menus and
hints use it so the arrow shows over them).

### 12.2 `Mouse_DrawCursor` 17ab:0612 (called only by `Dirty_Flush`) [O]

```
if g_cursorHidden DS:0a30: return                   (Mouse_Hide 17ab:0890 sets it, Mouse_Show 17ab:0885 clears it)
item = DS:0a52 ? DS:0a70 : first region of DS:0a8c whose zone contains (g_mouseX, g_mouseY)
       (the walk has no end test: the default region always matches)
if item.record != DS:7a4a:
    copy the 24-byte record to DS:7a6a; Mouse_SetBounds(0, 0, 319, 199) (§7.3); DS:7a4a = item.record
Gfx_GetImage(g_mouseX, g_mouseY, w, h, save buffer)  (removes nothing: the screen rows hold no cursor here)
g_cursorShown = 1; save-under position = (g_mouseX, g_mouseY)
Lpx_DrawKeyed(g_mouseX, g_mouseY, picture, 0)       (marks the cursor's tiles)
```

The cursor picture's top-left is at (g_mouseX, g_mouseY); the hot spot only shifts the click
coordinates (§7.2). Region hit tests use the top-left.

### 12.3 Removing the cursor [O]

`Mouse_RestoreUnder` (17ab:072a): if shown: shown = 0; `Gfx_PutImage(saved x, saved y, save
buffer)` (marks). `Gfx_HideMouseOnce` (1399:0b22): if DS:01e2 == 0 it becomes 1; if it is 1,
`Mouse_RestoreUnder` and DS:01e2 += 1 (a nested call from inside `Gfx_PutImage` leaves it at 3;
only "zero / non-zero" matters). `Gfx_ResetMouseHide` (1399:0b3e) sets 0.

So the screen rows contain the cursor from a flush until the next primitive call; the back page
never does (`Gfx_SaveToBack` hides it first); the wipe hides it before copying (§14.2); the movie
does not touch it (§15).

---

## 13. Palette and fades (1473, 1399)

### 13.1 Data [O]

* `g_palShadow` DS:024e: 768 bytes, 6-bit RGB, initialised in the file with the base palette
  (`fmt_graphics.md` §3.2).
* `g_palFaded` DS:024c: 1 while the DAC is black (set by `Pal_Blackout` and `Pal_FadeOut`,
  cleared by `Pal_FadeIn`).
* `g_palSaved` DS:5c22: 768 bytes (`Pal_Save` 1473:074e copies shadow → saved, `Pal_Restore`
  1473:0766 saved → shadow; neither touches the DAC).

### 13.2 `Pal_SetRange(rgb, first, groups)` 1399:05d6 [O]

```
OUT 3C8h, first
repeat groups times:
    wait while 3DAh bit 3 is set; wait while it is clear       (start of the next vertical retrace)
    CLI; 128 × (OUT 3C9h R, G, B) unrolled; STI
```

All three callers pass first = 0 and groups = 2, so a **palette load writes all 256 colours over
2 consecutive retraces**: colours 0..127 at the start of retrace k, 128..255 at the start of
retrace k+1 (two consecutive 70 Hz frames in the AVIs, REFERENCE_CAPTURES §4) [O]. The PIT
keeps running meanwhile.

### 13.3 Loading palettes [O]

| Function | Effect |
|---|---|
| `Pal_SetShadowRange(rgb, byteOffset, count)` 1473:02f0 | copies count bytes to shadow + byteOffset; if not faded, `Pal_SetRange(shadow, 0, 2)` (2 retraces) |
| `Pal_LoadRes(id)` 1473:041c | `Res_Read(id)`; copies the range into the shadow; **no DAC write** |
| `Screen_LoadPicture(pic, pal)` 1473:0399 | if pal > 0: `Res_Read(pal)` and `Pal_SetShadowRange`; then `Res_Read(pic)` (the picture stays in the shared buffer for the caller to draw) |
| `PicCache_Load`, `Pic_Read`, `PicCache_LoadBundle` (15df) | apply a bundle's leading or contained `P` members with `Pal_SetShadowRange` (§16.4) |

The signature of `Pal_SetShadowRange` in wave 0 listed (rgb, count, byteOffset); the order is
(rgb, byteOffset, count) [O: 1473:02f3..0304 and every caller].

When not faded, every `Pal_SetShadowRange` costs 2 retraces (up to ~28.5 ms plus the wait for
the first retrace).

### 13.4 `Pal_Fade(n, from, to)` 1473:015c (near) [O]

```
steps = 63 / n + 1                                   (n 6 → 11, n 8 → 8)
cur = copy of from
for i in 0..767: delta[i] = |to[i] − from[i]| / steps + 1      (signed bytes; byte result)
repeat steps + 1 times:
    for i in 0..767:
        if from[i] < to[i]: cur[i] += delta[i]; if cur[i] > to[i]: cur[i] = to[i]
        else:               cur[i] −= delta[i]; if cur[i] < to[i]: cur[i] = to[i]
    Pal_SetRange(cur, 0, 2)                          (2 retraces)
```

| Caller | n | Loads | Changing loads (max) | Retraces | Time |
|---|---|---|---|---|---|
| `Pal_FadeOut` 1473:0265 (if not faded: shadow → black; then faded = 1) | 6 | 12 | 11 | 24 | 0.342 s |
| `Pal_FadeIn` 1473:02a0 (if faded: black → shadow; then faded = 0) | 8 | 9 | 8 | 18 | 0.257 s |
| `Pal_FadeFromSaved` 1473:02db (DS:5c22 → shadow; faded flag unchanged) | 8 | 9 | 8 | 18 | 0.257 s |

The direction of each byte is decided by `from` vs `to`, not by `cur`; the last load repeats the
target for every colour, because delta > |d| / steps guarantees arrival within `steps` loads.
The work between two loads (768 bytes) is far shorter than a retrace interval, so the loads
follow each other without gaps [O code; I timing].

**Reconciliation with the wave 0 AVIs** (`fades.py` simulates every load for the attract
palettes; every load except the last changes colours in both 128-colour groups):

| AVI span (REFERENCE_CAPTURES §4) | Code | Expected |
|---|---|---|
| title fade-in, 16 frames (15-30) | `Pal_FadeIn` (attract animation player 2c11:0236) | 8 changing loads × 2 = 16 ✓ |
| "title fade-out", 19 frames (650-668) | 2c11:05cf: animation frame flushed, `Timer_Wait`, then `Pal_FadeFromSaved` (palette 0x1000 → 0x1004, 2c11:023d) | 16 frames of loads plus the preceding frame change and wait [I] |
| "story fade-in", 22 frames (861-882) | `Pal_FadeOut` at the end of 2c11:05cf (2c11:061b): this is the fade **to black** | 11 changing loads × 2 = 22 ✓ |
| 4-frame pause, then 16 frames (887-902) | story page 1 (2c11:0630): picture load and draw, `Screen_FadeOutIn` = `Pal_FadeOut` (already faded: nothing), `Dirty_Flush` (1000 tiles), `Pal_FadeIn` | pause = load, draw and flush; 8 × 2 = 16 ✓ |

So the code's 12 and 9 loads are right, and the visible lengths are 22 and 16 frames. Round 2
measured exactly that on 10 transitions in 6 AVIs (REFERENCE_CAPTURES §10.2): every load shows as
two consecutive frames, fade-out = 22 frames (the 12th load rewrites black), fade-in = 16 (the 9th
rewrites the target), and the 19-frame span of round 1 is a fade-in preceded by two redraw frames.
The black gap between a fade-out and the next fade-in (loading and drawing the next screen) was
7-18 frames: workshop → race 7, 8 and 14 (Automotive, Alternative Energy, Aircraft), race →
results 7-11, post-race → hub 8, warehouse tube → workshop 18 [O].

### 13.5 Other screen functions [O]

* `Screen_FadeOutIn` (1473:0325): `Pal_FadeOut`, `Dirty_Flush`, `Pal_FadeIn`.
* `Pal_Blackout` (1473:000c): §10.1.
* 6-bit to 8-bit: not done by the game. [R] Expand as `(v·255 + 31) / 63` (DOSBox-X's VGA
  rounding as used by the TM DOS port) until a G&G capture confirms it.

---

## 14. Screen transitions

### 14.1 `Screen_Show` 1473:045a [O]

If faded: `Dirty_Flush`, `Pal_FadeIn`. Otherwise: `Screen_RandomWipe`, then `Dirty_Clear`.
Its only caller is the warehouse room entry (12e0:0674).

After a wipe the dirty map is **cleared without flushing**: tiles marked before `Screen_Show`
outside the wiped rows (the menu bar rows 0..8, the HUD rows 165..199) do not reach the screen
until they are marked again [O code; I visibility].

### 14.2 `Screen_RandomWipe` 1473:0042 and `Screen_Wipe(pattern)` 1473:0479 [O]

`Screen_RandomWipe`: k = `rand()`·8 / 0x8000 (32-bit: `CWD`, shift left 3, `F_LDIV` by 0x8000)
→ `Res_Read(0x11D6 + k)` → `Screen_Wipe(g_resBuffer)`.

Wipe pattern (`W` resource, 636 bytes; the 8 resources 0x11D6..0x11DD):

| Offset | Type | Field |
|---|---|---|
| +000h | u16 | unused (0 in all 8) |
| +002h | u16 | step period in ticks (2 in all 8) |
| +004h | u16 | left sweep enabled |
| +006h | i16[156] | initial left frontier per row (rows 9..164) |
| +13Eh | u16 | right sweep enabled |
| +140h | i16[156] | initial right frontier per row |
| +278h | i16 | left frontier step per iteration |
| +27Ah | i16 | right frontier step per iteration |

Algorithm (rows r = 0..155 are screen rows 9..164; VRAM base A000:0B40, source row 9 of the
screen buffer):

```
Gfx_HideMouseOnce()
leftDone[r] = 0; rightDone[r] = 320; L[r] = pattern.left[r]; R[r] = pattern.right[r]; done[r] = 0; finished = 0
snd = g_effectBank DS:7a44 + 0x19 + rand()·2 / 0x8000;  SndRes_PlayEffect(snd, loop 1, priority 10)
while finished != 156:
    Timer_Set(2, period)
    for r in 0..155 (skipping rows already done):
        L[r] += stepL; R[r] += stepR
        if L[r] > R[r]: done[r] = 1, L[r] = R[r]
        if R[r] < L[r]: done[r] = 1, R[r] = L[r]
        if done[r]: finished += 1
        if leftEnabled  and L[r] − leftDone[r] > 0:  copy [leftDone[r], L[r]) of the row to VRAM; leftDone[r] = L[r]
        if rightEnabled and rightDone[r] − R[r] > 0: copy [R[r], rightDone[r]) to VRAM; rightDone[r] = R[r]
    Timer_Wait(2)
SndRes_StopEffect(snd); Gfx_ResetMouseHide()
```

The copies go straight to VRAM (`memcpy`), so the wipe bypasses the dirty map.

The eight patterns [O, `wipes.py`]:

| Id | Sweeps | Steps L / R | Iterations | Ticks |
|---|---|---|---|---|
| 0x11D6 | left | +10 / 0 | 33 | 66 |
| 0x11D7 | right | 0 / −10 | 33 | 66 |
| 0x11D8 | both | +7 / −7 | 24 | 48 |
| 0x11D9 | both, staggered rows | +8 / −8 | 40 | 80 |
| 0x11DA | left, staggered rows | +10 / 0 | 48 | 96 |
| 0x11DB | right, staggered rows | 0 / −10 | 48 | 96 |
| 0x11DC | both | +10 / −10 | 18 | 36 |
| 0x11DD | both, staggered | +10 / −10 | 33 | 66 |

(Iterations = steps until every row is done; each costs the period of 2 ticks plus the copy
time.) **Original bug [O data + code]:** in 0x11DA every row's right frontier is 319 and the
right sweep is disabled, so the left sweep stops at 319 and **column 319 of rows 9..164 is never
copied**: it keeps the previous screen's pixels until something marks that tile (the wipe is
followed by `Dirty_Clear`).

rand(): two calls per wipe, pattern first, sound second, both before the first copy.

Measured (REFERENCE_CAPTURES §10.3) [O]: entering a room through a door, the screen changed on
every video frame for 45 frames (642 ms ≈ 93 ticks) with no palette change; the diagonal look and
the length fit a staggered pattern of 48 iterations (0x11DA or 0x11DB, 96 ticks) [I].

---

## 15. Movie player `Movie_Play(first, end, period, cues, bank)` 1473:07d9 [O]

```
Res_Read(first)
for frame = first .. end−1, counter = 0, 1, ...:
    Timer_Set(0, period)
    Movie_DecodeFrame(g_resBuffer, A000:0000)
    if frame + 1 != end: Res_Read(frame + 1)          (disk read and LZH decode inside the frame)
    if cue.frame == counter:
        type (0..6 through CS:08bf; others skip): 0 Delay(cue.a); 1 SndRes_PlayEffect(bank + a, 0, b);
        2 SndRes_StopEffect(bank + a); 5 SndRes_PlayEffect(bank + a, 1, b); 3, 4, 6 nothing
        cue += 1                                      (also for unknown types; at most one cue per frame)
    Timer_Wait(0)
```

* The caller (`State_15_FinalMovie` 2fcb:0640) passes (0x3000, 0x30CA, 18, DS:236c, effect bank
  0x108D): 202 frames at 18 ticks = 123.6 ms per frame (8.09 frames/s) when the decode and the
  next read fit.
* A `Delay` cue runs **after** the frame's decode and read, so that frame lasts (decode + read +
  Delay) and the following `Timer_Wait` returns at once: the shipped 1460-tick pauses make four
  frames last about 10.0 s plus their work.
* Frames are decoded straight into VRAM: no dirty marks, no cursor handling. The results screen
  hides the cursor (`Mouse_Hide` from 2fcb:0000) before. If a mouse move arrives while the hide
  state is 0, the handler's `Dirty_Flush` returns early unless something is pending (DS:08b6),
  so nothing from the RAM screen is copied over the movie [O code; I: nothing is pending].
* The movie music (`SndRes_PlayMusic` of bank 0x30CA, cue tick 0x250 → 12e0:00ea) runs in
  parallel (code_sound.md §14.3).
* No palette is loaded (`fmt_graphics.md` §7.4).

---

## 16. Memory and resources

### 16.1 Memory (1536) [O]

| Function | Behaviour |
|---|---|
| `Mem_Alloc(n)` 1536:0007 | `farheapcheck` < 0 → `exit(2)`; `farmalloc(n)` (u32 n); NULL → `exit(1)` |
| `Mem_Free(p)` 1536:0047 | `farheapcheck` < 0 → `exit(3)`; `farfree`; `farheapcheck` < 0 → `exit(2)` |
| `Mem_CheckHeap()` 1536:007c | `farheapcheck` < 0 → `exit(2)` |

Conventional far heap only; no EMS/XMS. Large blocks: screen 128 000 bytes, resource buffer
65 535, the overlay buffer (§17).

### 16.2 Archives (14ff) [O]

* `Res_OpenFiles` (14ff:0167): `g_resBuffer` DS:6574 = `Mem_Alloc(0xFFFF)`; opens gizmo000..003.dat
  (names through DS:0550) with O_RDONLY|O_BINARY into `g_resHandles` DS:6566 — **open errors are
  not checked**; then reads the last entry of volumes 0..2 (0x36D, 0x11DE, 0x214E) and, if its
  first word is not 0xA55A, prints "Incorrect Resource File n" (DS:05a5/05bf/05d9) and **goes on**.
  Volume 3 is not checked.
* `Res_Read(id)` (14ff:000d): `g_menuDisabled` = 1; handle = DS:6566[id >> 12] (kept in DS:6564);
  seek to 4 + 4·(id & 0xFFF); read the u32 start and the next entry's u32 offset; seek to start
  + 4; read the type byte into `g_resType` DS:6572 and the method byte; method 0: read (next −
  start) bytes into the buffer (**2 bytes more than the payload**, from the next entry) and
  return next − start; any other method: `Lzh_Decompress(handle, buffer)` and return its result
  (the decoded size); `g_resLastIndex` DS:0560 = id & 0xFFF (the index only); `g_menuDisabled` =
  0. Format: `fmt_data.md`.
* `Res_LoadCopy(id)` (14ff:00fe): `Res_Read`, `Mem_Alloc(size)`, copy; the caller frees. Stored
  resources therefore get size + 2.
* `Res_LoadTo(id, dest)` (14ff:013d): `Res_Read`, `movmem` to dest.
* `Res_CloseFiles` (14ff:0209).

The LZH decoder reads the file in 4000-byte blocks (1b48:0002 into DS:9c52, count DS:9c4c) and
drains the keyboard after each block (§6.4).

### 16.3 The swap file `gizmo004.dat` [O]

* `Screen_SaveToSwap(x, y, w, h)` (14ff:0226): DS:6562 = w·h + 4; `Gfx_GetImage` into the
  resource buffer; `open("gizmo004.dat", O_RDWR|O_CREAT|O_TRUNC|O_BINARY)` → DS:0562; write
  DS:6562 bytes; the file stays open.
* `Screen_RestoreFromSwap(x, y)` (14ff:0284): seek 0, read DS:6562 bytes into the resource
  buffer, `Gfx_PutImage(x, y)`, close, DS:0562 = −1. The file is not deleted.
* No error checks at all. The only user is the workshop (2dee:17eb / 185d, a 280×163 area),
  which needs the resource buffer for other reads between save and restore.
* [R] keep the save-under in memory; never write the game folder.

### 16.4 Picture cache (15df) [O]

300 entries of 8 bytes at `g_picCache` DS:6640 `{i16 id (−1 free), i16 use count, far data}`,
then an end marker −3 (`PicCache_Init` 15df:000d, which also calls 174b:0009).

| Function | Behaviour |
|---|---|
| `PicCache_Find(id)` 15df:003e (near) | index \| 0x8000 of the first entry with that id; else the first free index; id −2 returns the first free index at once; −1 if the table is full |
| `PicCache_Load(id)` 15df:00a1 | use count += 1 (also on the slot about to be filled); if not cached: `Res_Read(id)`; a bundle whose member 0 is a `P` palette applies it (`Pal_SetShadowRange`) and caches member 1, else member 0; a plain picture is cached whole; private copy with `Mem_Alloc` |
| `Pic_Read(id)` 15df:01a5 | `Res_Read(id)` unless id == `g_resLastIndex`; applies a leading `P` member; returns the picture in the shared buffer (not cached) |
| `PicCache_LoadBundle(id, out[])` 15df:023b | private copy of the whole bundle (`Res_LoadCopy`), applies every `P` member, gives every other member its own cache entry (id = the bundle id) and writes their indices to out[], stops at a member whose payload starts with 0; when already cached, refills out[] from the following entries with that id; returns (first offset / 2) − 2 |
| `PicCache_Release(id)` 15df:0359 | use count −= 1; at 0 frees the data and the entry |
| `PicCache_ReleaseBundle(id)` 15df:03a2 | as above and clears every following entry with the same id |
| `PicList_Find(list, id)` 15df:007d | first 8-byte record with that id before the −3 marker, else NULL |
| `Obj_SetSizeFromPicture(obj)` 15df:0414 | obj+6 / obj+8 = width / height of the picture at obj+0Ah |

**Original bug [O code; I effect]:** `Pic_Read` compares the full id with `g_resLastIndex`, which
holds only the index (id & 0xFFF): for volume 0 ids the test works; for ids of volumes 1-3 it
never matches (a needless re-read), and a volume 0 id whose index equals the last read entry of
another volume is **not** re-read, returning the wrong data. Whether any call sequence hits it is
open.

---

## 17. Overlay manager and timing (1d42) [O, I]

* `__OvrInit` (1d42:08e9, `_INIT_` priority 1): computes the largest overlay size in paragraphs
  plus 2 (1d42:0299 into 1dd7:00aa); the buffer is `_ovrbuffer` (DS:40fe, 0 in the file) if that
  is larger, else **twice the largest overlay**; `farmalloc((size + 1)·16)`; opens the program
  file (`__OvrOpenExe`); failure → `abort`.
* Calling a function of an overlay that is not in the buffer traps through INT 3Fh (`__OvrTrap`
  1d42:04f2) and loads it (`__OvrLoad` 1d42:05a2): a DOS read of the overlay's code and fix-ups
  from `ssg.exe` (≤ 10 148 bytes of code for the largest, ovl13) and the fix-up pass. Older
  overlays are discarded when the buffer is full [I: Borland VROOMM policy].
* Effect on timing: the first call into an overlay after it was discarded costs a disk read
  inside the current frame. With a buffer of about twice the largest overlay (~21 KB [I]) and
  99.7 KB of overlays, state changes (hub ↔ rooms ↔ puzzles ↔ dialogs ↔ menu) reload overlays
  often. In DOSBox-X, whose DOS file reads take no emulated time, no stall is visible: a first
  and a later entry to the Aircraft race show the same 14-frame black gap (REFERENCE_CAPTURES
  §10.4) [O]. [R] Ignore overlay timing against the DOSBox-X references; the engine needs no
  overlays.

---

## 18. Hardware detection (ovl05 2c08) [O]

* `Video_DccSupported` (2c08:0000; wave 0 name `Video_GetDisplayCode`): INT 10h AX=1A00h; if AH
  is still 1Ah afterwards → −1, else AL with AH cleared. A BIOS that supports function 1Ah
  returns AL = 1Ah, so the result is 26 (and passes the "≥ 7" test); BL (the actual display
  combination code) is never read. The game starts in DOSBox-X without `-GV` [O], so its BIOS
  changes AH (DOSBox's handler returns AX = 001Ah [I]). A BIOS that leaves AH = 1Ah fails the test even with a VGA; `-GV` (DS:244e = 8)
  bypasses it.
* `Mouse_DriverPresent` (2c08:0011): INT 21h AX=3533h; vector 0000:0000 → 0; first byte at the
  vector = CFh (IRET) → 0; INT 33h AX=0000h (driver reset: pointer centred, hidden); then `JZ`
  on the flags as returned: with the usual IRET return they are the caller's (ZF = 0 after the
  preceding compare), so any driver that returns counts as present (AX = FFFFh is not checked)
  [O code; I driver behaviour].
* `Cpu_Detect` (2c08:0032): FLAGS bits 12-15 cleared by a POPF stay set → 1 (8086/8088); bits
  12-15 cannot be set → 2 (286); else toggling EFLAGS.AC (bit 18) fails → 3 (386), succeeds → 4
  (486 or later). Stored in DS:2450 by `Init_CommandLine`. The only game rule that depends on it
  is the chimp set-up's `== 2` test (2a80:07b1; ARCHITECTURE §2.6, the chimps spec).

---

## 19. rand() in the platform layer [O]

The RTL generator (`rand` 1000:06c1, seed DS:442a, seeded once at 303a:0229) is called here only
by the random wipe: `Screen_RandomWipe` (pattern, ×8) then `Screen_Wipe` (sound, ×2), in that
order, at a room entry that is not faded (§14). No interrupt handler, input, timer, sound or
drawing function calls `rand`. All other call sites belong to the game logic (ARCHITECTURE §8).

---

## 20. Engine notes [R]

1. **Clock.** A virtual PIT at 1 193 181.67 / 8192 Hz drives a u32 tick (DS:021a semantics);
   every second tick runs the sound driver (code_sound.md). Keep `Timer_Set` u16 counts,
   `Timer_Wait` ≥, `Timer_Expired` >, `Delay` polling `Input_GetCode` with its global keys. Drop
   the BIOS chain (8× clock) and the double EOI. The dialog tick freeze belongs to the dialog
   module.
2. **Waits pump the host.** `Timer_Wait`, `Delay`, `Input_WaitCode`, `Pal_SetRange`'s retrace
   waits and the puzzle sound waits are the engine's yield points (PORTING_NOTES §2.2). In a
   deterministic run a wait advances the clock to its deadline; a palette load advances to the
   next retrace, then one more.
3. **Retrace clock.** Retrace k at k / 70.086 s on the PIT time base; a load = 2 retraces
   (colours 0..127 visible at the first, 128..255 at the second). Fades: 12 / 9 loads.
4. **Keyboard model.** Held bits for the 7 scan codes, the play-mode swallow rule, E0/E1
   swallowing (feed keypad scan codes for the arrow keys), the 16-entry ring that gates BIOS keys
   one per `Input_GetCode` and erases a mouse code in the same poll, and the key draining during
   LZH decoding. Reuse tmport's `PcKeyboard` / `bios_keyboard` for the BIOS side.
5. **Mouse model.** Deliver host mouse events as INT 33h events at PIT-tick times: a move is
   processed only while the hide state is 0 (between a flush and the next primitive call);
   presses and releases latch with their own coordinates plus the current hot spot; the right
   button only updates DS:0a0c. In a 0-cost frame model all events during a wait see hide state
   0, which matches the original except for events that fall inside a long frame's drawing.
6. **Screen.** A 320×400 index buffer with the row semantics above, a 40×25 dirty map (clamped),
   and a separate 320×200 VRAM surface written by the flush, the wipe and the movie; present the
   VRAM surface. Composite nothing else: the cursor is drawn into the buffer by the flush as in
   the original.
7. **Primitives.** Implement each with its exact clipping (including the exclusive right/bottom
   of `Gfx_ClipX/Y`, the unclipped `Gfx_HLine` draw, the unsigned y reject of the keyed raw blit,
   the unclipped RLE blits and their missing mark at negative x). Where the original would write
   outside the buffer (RLE past the edges, unclipped fills), clip in the engine and log in debug
   builds; no shipped data is known to do it.
8. **Wipe and movie.** Port `Screen_Wipe` and `Movie_Play` literally, including the 0x11DA
   column-319 bug and the `Delay` cue placement.
9. **Resources.** Read the archives once; keep `Res_Read`'s "+2 bytes" only where a caller
   depends on the returned size (none known); never create `gizmo004.dat`.
10. **Start-up.** Drop the CPU, display, mouse and memory refusals and `-GV`; keep the `-S`
    switch semantics as an option (`sound = off | speaker | fm`, PORTING_NOTES §7).

---

## Open questions

1. *Answered by round 2 (REFERENCE_CAPTURES §10.2):* the 19-frame span at the end of the title
   is a 16-frame fade-in preceded by two redraw frames (650, 651, 653).
2. The cost of the work inside frames on the reference machine: picture loads (`Res_Read` +
   LZH), overlay loads, full flushes (1000 tiles), the wipe's per-step copy time. To be measured
   with a frame logger in resident code.
3. Whether any caller passes LPX blit modes 1 or 3, and whether any sprite or object is ever
   drawn with negative y (rejected by the keyed raw blit) or partly off the right edge (RLE wrap).
4. Whether a `Pic_Read` sequence ever hits the index-only "last read" test (§16.4).
5. Whether the hint chime is audible at all (code_sound.md §15 note; the dialog pauses the
   driver before the chime's first note).
6. DOSBox-X's 6-bit to 8-bit DAC rounding for a G&G capture (§13.5).
7. Whether a real VGA BIOS of 1993 left AH = 1Ah after INT 10h AX=1A00h (then the game needed
   `-GV`, §18).
8. The VROOMM discard policy in detail, if overlay reloads ever must be predicted for a slower
   reference than DOSBox-X (§17; not needed against the DOSBox-X captures).
