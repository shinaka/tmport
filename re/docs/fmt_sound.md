# Sound and music: SSND resources, wave playback, speech, sound effects, MIDI music

Scope: the digitised sound resources in `TM0SOUND.DLL`..`TM8SOUND.DLL`, the MIDI files in
`TMTWINCD\SOUNDS\`, and every piece of `TMTWINCD.EXE` code that loads, plays, sequences,
interrupts, pauses or selects them.

Evidence used: the Ghidra export (`re/win/ghidra_export`), extra disassembly of code Ghidra had not
decoded (window procedure `1010:01c5..054f`, `1090:03e6`, `10e0:0180..0240`, with my own headless run
on a copy of the project), NE relocation tables parsed straight from `TMTWINCD.EXE`, the raw resources
in `extracted/res`, and the MIDI files. Scratch scripts are in `extracted/scratch/sound/`. The most useful
ones are `reloc_targets.py` (every far call to a given `seg:off`), `anim_sfx_all.py` (sound opcodes in
animation scripts) and `midi_info.py` (MIDI file survey).

Conventions:

* **[O]** means observed in bytes, disassembly or decoded output. **[I]** means inferred.
* Code addresses use Ghidra selectors (`SSSS:OOOO`). `DGROUP:XXXX` (selector `11b8`) is at file offset
  `0x23c00 + XXXX` of `TMTWINCD.EXE`.
* Sound ids are decimal. "SSND n" means the sound resource with id n.
* "sfx n" means SSND `7000+n`.

Related documents: `code_1058-1090.md` §9–10 (the wave and MCI driver functions, which this document
re-derives and extends), `code_1008-1030.md` (window procedure, events, INI), `code_10e0-10f0.md`
(player, HUD, celebration), `code_1168-1198.md` (ladder climb, elves, treasure hall) and
`fmt_othr_puzzles.md` (elf questions; it also documents the puzzle speech rules in its §8).

---

## 1. Summary

| Topic | Fact |
|---|---|
| Sound assets | 2861 SSND resources in 9 DLLs, 77.9 minutes in total. Every one is 22050 Hz, 8-bit unsigned, mono PCM with a 12-byte header [O]. |
| Music assets | 10 tunes × 2 variants = 20 standard MIDI files (format 0, 480 PPQN) [O]. |
| Wave API | `waveOutOpen/PrepareHeader/Write/Reset/UnprepareHeader/Close` with one global header. Completion is signalled by `MM_WOM_DONE` posted to the main window [O]. No `sndPlaySound`, no volume calls, no mixing [O]. The EXE imports only five modules (GDI, KERNEL, MMSYSTEM, TOOLHELP, USER; NE module table). From MMSYSTEM it imports only these 12 names: `waveOutGetNumDevs`, `waveOutOpen`, `waveOutPrepareHeader`, `waveOutWrite`, `waveOutReset`, `waveOutUnprepareHeader`, `waveOutClose`, `midiOutGetDevCaps`, `mciSendCommand`, `timeGetTime`, `timeBeginPeriod` and `timeEndPeriod` (`imports.tsv`) [O]. |
| Channels | **One wave voice** is shared by sound effects and speech; starting any sound cuts off the current one. Music is a separate MCI sequencer stream [O]. |
| Settings | `DGROUP:0b96` "Music and Sound" controls the sound effects and the music. `DGROUP:0b97` "Voice" controls speech. Both are saved per player and both toggle together with **Ctrl+S** [O]. |
| Speech sync | No lip sync and no position callbacks. Speech is sequential and blocking (or polled). Puzzle text is highlighted line by line while each line is spoken. Animations run on an independent 100 ms timer [O]. |
| Music API | `mciSendCommand` only: `MCI_OPEN` "sequencer" + element, `MCI_PLAY`/`MCI_NOTIFY`, `MCI_STOP`, `MCI_SEEK`, `MCI_STATUS`, `MCI_CLOSE` [O]. No string commands, no fades [O]. |
| B / E files | **B** = base-level synth layout (MIDI channels 13–16, drums on 16). **E** = extended layout (channels 2–6, drums on 10). B is chosen when the active MIDI Mapper setup name in `\system\midimap.cfg` contains ADLIB, AD LIB, MVI PRO, BAS(E) or THUNDER BOARD [O]. |
| Looping | Every tune loops from its start when it ends, except `open1` (played once) [O]. |

---

## 2. Asset inventory

### 2.1 Sound DLLs [O]

Counts and durations come from `tools/assets/ssnd.py` (§4).

| DLL | SSND ids present | Count | Shortest / longest | Total | Content (§9) |
|---|---|---|---|---|---|
| TM0SOUND.DLL | 7000–7041 | 42 | 0.14 s / 7.00 s | 49.1 s | Sound effects (sfx 0–41) |
| TM1SOUND.DLL | 7100–7133, 7150–7159, 7300–7352, 7400–7452, 7500–7552 | 203 | 0.44 s / 10.55 s | 483.0 s | Hint narration, intro narration, praise / wrong-answer / clue-word phrases ×3 voices |
| TM2SOUND.DLL | 4000–4399 | 400 | 0.41 s / 4.41 s | 552.4 s | Elf-question narration, puzzles 0–56 |
| TM3SOUND.DLL | 4400–4799 | 400 | 0.44 s / 5.49 s | 583.0 s | Puzzle narration |
| TM4SOUND.DLL | 4800–5199 | 400 | 0.44 s / 4.48 s | 639.3 s | Puzzle narration |
| TM5SOUND.DLL | 5200–5599 | 400 | 0.46 s / 4.46 s | 640.7 s | Puzzle narration |
| TM6SOUND.DLL | 5600–5999 | 400 | 0.43 s / 4.86 s | 674.4 s | Puzzle narration |
| TM7SOUND.DLL | 6000–6399 | 400 | 0.42 s / 4.61 s | 674.3 s | Puzzle narration |
| TM8SOUND.DLL | 6400–6615 | 216 | 0.42 s / 5.55 s | 380.4 s | Puzzle narration, up to puzzle 358 |
| **Total** | | **2861** | | **4676.6 s** | |

The id ranges inside each DLL are contiguous except in TM1 [O]. Each id falls in the range the EXE
routes to that DLL (§5.1); this was checked for all 2861 ids [O].

### 2.2 MIDI files [O]

`TMTWINCD\SOUNDS\{B,E}{CASTLE,CLUBHSE,FINALE,LEVEL1,LEVEL2,LEVEL3,OPEN1,OPEN2,THRONE,TITLE}.MID`.
See §8.1 for the analysis.

---

## 3. SSND resource format

### 3.1 How SSND resources are stored in the DLLs [O]

* Each sound DLL is a resource-only Win16 NE module. The payloads use the **integer resource type
  0xFF01** (the raw NE type word is `0xFF01`; `tools/ne_resources.py` prints it as 32513 = 0x7F01).
  The resource id is the sound id.
* Each DLL also has an `RT_NAMETABLE` resource (type 15). It holds one 12-byte record per sound:

  | Off | Size | Value |
  |---|---|---|
  | 0 | u16 | record size = 12 |
  | 2 | u16 | type ordinal = 0xFF01 |
  | 4 | u16 | id ordinal (= sound id, without the 0x8000 flag) |
  | 6 | char[5] | `"SSND\0"` |
  | 11 | char[1] | `"\0"` (empty id name) |

  This table lets the EXE call `FindResource(hDll, MAKEINTRESOURCE(id), "SSND")` with the Mac type name
  (§5.1). In all nine DLLs the name table covers exactly the set of 0xFF01 resources and nothing else.
* NE resource flags are 0x0C30 for every SSND resource. NE stores lengths in 512-byte units, so each
  extracted payload ends with 0–510 bytes of zero padding (300 bytes on average; 2859 sounds have
  non-empty padding, all zero bytes).

A re-implementation can find a sound by scanning the NE resource table for type `0xFF01` and the id. The
name table does not need to be parsed.

### 3.2 Payload layout (little-endian) [O]

| Off | Size | Field | Observed values (all 2861 sounds) | EXE usage |
|---|---|---|---|---|
| 0x00 | u32 | reserved | always 0 | never read |
| 0x04 | u32 | sampleRate | always 22050 (0x5622) | `nSamplesPerSec` **and** `nAvgBytesPerSec` (`1088:0060..0072`) |
| 0x08 | u32 | sampleCount | 3068 .. 232692 | `dwBufferLength = sampleCount − startOffset` (`1088:00e0`) |
| 0x0C | u8[sampleCount] | samples | 8-bit **unsigned** PCM, mono; 0x80 = silence | `lpData = base + 12 + startOffset` |

Verification across all sounds [O]:

* `12 + sampleCount ≤ resource size` in every case, and the remainder is padding smaller than 512 bytes.
* The first sample is 0x80 in every sound. Sample values cover the full range 0..255. No sound is silent.
* **Format variations: none.** All rates, header words and padding behave the same.
* There are 27 groups of byte-identical recordings, all in TM1 and all clue words:
  7314–7322 = 7329–7337, 7414–7422 = 7429–7437 and 7514–7522 = 7529–7537 (clue words 6..14 repeat as
  21..29 in each voice bank).

The header word at +0 is probably a Mac `snd`/`SSND` artefact such as an offset or loop start [I]. A
port can ignore it.

To convert to WAV, copy the bytes unchanged into a PCM WAVE file (1 channel, 22050 Hz, 8 bits). WAV
8-bit data is also unsigned. For a signed pipeline use `s = byte − 128`.

---

## 4. Decoder tool: `tools/assets/ssnd.py`

Library API (importable, no third-party dependencies):

| Symbol | Purpose |
|---|---|
| `parse_ssnd(data, sid=-1, strict=True) -> SSND` | Parses one payload (padding allowed). `SSND` has `sid, reserved, sample_rate, samples, padding`, `.duration` and `.to_wav()`. |
| `iter_dll(path) -> (id, SSND)…` | Every SSND in one DLL, sorted. It reads the NE resource table and accepts type 0xFF01 or a name-table entry "SSND". |
| `load_sound(game_dir, sid)` | Loads a sound the way the game does: routes the id to a DLL (§5.1), then finds the resource. |
| `dll_for_sound_id(sid)` / `ID_ROUTING` | The EXE's id → DLL table. |

CLI: `python -P tools/assets/ssnd.py [--game DIR] [--out DIR] [--only TMnSOUND.DLL…] [--manifest F] [--no-wav]`.
By default it reads `extracted\wincd\TMTWINCD` and writes `extracted\previews\wav\<DLL>\<id>.wav`.

Run result (2026-10-17) [O]: **2861 WAVs written, 0 failures, 77.9 min in total.** The per-DLL figures
are in §2.1. A JSON manifest with the rate, sample count, duration and padding of every sound is at
`extracted\scratch\sound\manifest.json`. Re-check after the interrupted run: `--no-wav` reports the same
figures, and all 2861 files under `extracted\previews\wav\` open with Python's `wave` module as 1 channel,
8 bits, 22050 Hz, 77.94 min in total.

---

## 5. How the EXE plays digitised sound

### 5.1 Sound id → resource type → DLL [O]

**Type from id** (`1038:038a`; the strings are at `DGROUP:0741..075e`):

| id | Mac type |
|---|---|
| < 900 | BNDL |
| 900 – 949 | RRGB |
| 950 – 999 | FFNT |
| 1000 – 3999 | PMAP |
| 4000 – 9999 | **SSND** |
| ≥ 10000 | OTHR |

Music ids 9000–9009 share the numeric space, but they are never loaded as resources (§8).

**DLL from id** for type SSND. The same comparison ladder appears three times: `1038:011f..030a`
(size query), `1038:045a..06a5` (load a copy) and `1038:0c38..0e7d` (lock in place). The module
handles are loaded by `1018:06eb..07c7` from the names at `DGROUP:0608 + 13k`.

| Range (lo ≤ id < hi) | Handle | DLL |
|---|---|---|
| 7000 ≤ id < 7100 | `DGROUP:09de` | TM0SOUND.DLL |
| 7100 ≤ id < 8000 | `09e0` | TM1SOUND.DLL |
| 4000 ≤ id < 4400 | `09e2` | TM2SOUND.DLL |
| 4400 ≤ id < 4800 | `09e4` | TM3SOUND.DLL |
| 4800 ≤ id < 5200 | `09e6` | TM4SOUND.DLL |
| 5200 ≤ id < 5600 | `09e8` | TM5SOUND.DLL |
| 5600 ≤ id < 6000 | `09ea` | TM6SOUND.DLL |
| 6000 ≤ id < 6400 | `09ec` | TM7SOUND.DLL |
| 6400 ≤ id < 6800 | `09ee` | TM8SOUND.DLL |
| 6800–6999, 8000–9999 | none | Unmapped: the result pointer is left uninitialised. The game never asks for these ids [O]. |

(Type PMAP uses `g_hGfxLib` `0872`; every other type uses TMT.DLL `09f0`.) If `LockResource` returns
NULL, `MessageBox("You are out of memory.", "Memory Allocation Failed")` is shown (`DGROUP:07ba/07a3`).

**Two loading paths** [O]:

| Function | Used by | Behaviour |
|---|---|---|
| `Res_LockInPlace(id)` `1038:0ba8` | speech (`1088:01a9`, `1138:0283`) | Frees the 280000-byte general scratch block at `3d8c` (via `1038:0038`), then `FindResource/LoadResource/LockResource`. Stores the locked pointer in `3d8c:3d8e` and the HGLOBAL in `073a`. |
| `Res_UnlockInPlace()` `1038:0ef8` | after speech | `GlobalUnlock` + `FreeResource(073a)`, then re-allocates the 280000-byte scratch block (`1038:0022`). |
| `Res_LoadCopy(id)` `1038:08c5` → `076f` → `03da` | sound effects (`1170:0000`) | Allocates memory of the resource size and copies the resource into it. The SFX module frees it with `1030:0082`. |

### 5.2 Wave driver, segment 1088 [O]

**State** (DGROUP):

| Addr | Type | Name | Meaning |
|---|---|---|---|
| 09f2 | i16 | waveAvail | `waveOutGetNumDevs() != 0`, set once by `1088:0000` at start-up |
| 09f4 | far* | waveFmtPtr | `&waveFmt` while a device is open, NULL when closed |
| 09f8 | i16 | waveState | 1 = playing, −1 = idle (initial −1) |
| 4ac2 | PCMWAVEFORMAT (16 B) | waveFmt | `wFormatTag=1, nChannels=1, nSamplesPerSec=rate, nAvgBytesPerSec=rate, nBlockAlign=1, wBitsPerSample=8` |
| 4ad2 | HWAVEOUT | hWaveOut | |
| 4ad4 | WAVEHDR (32 B) | waveHdr | `lpData, dwBufferLength, …, dwUser (4ae0) = SSND pointer` |

**`Wave_Play(ssnd far*, u32 startOffset)`** (`1088:0014`):
1. Return if `!waveAvail` or `ssnd == NULL`.
2. `Wave_Stop()`. A new sound always cuts off the old one.
3. Fill `waveFmt` from `ssnd+4`.
4. `waveOutOpen(&hWaveOut, WAVE_MAPPER (0xFFFF), &waveFmt, dwCallback = hwndMain (3cdc), 0, CALLBACK_WINDOW (0x10000))`.
   The return value is **not checked**.
5. `lpData = ssnd + 12 + startOffset`, using huge-pointer selector arithmetic: the shift count is patched
   with `KERNEL.113 __AHSHIFT`, relocations at `1088:00b2/00c6`. `dwBufferLength = *(u32*)(ssnd+8) − startOffset`.
   `dwUser = ssnd`.
6. `waveOutPrepareHeader(h, &waveHdr, 0x20)`, `waveOutWrite(h, &waveHdr, 0x20)`, `waveState = 1`.

All three call sites pass `startOffset = 0` (`1088:01c0`, `1138:02b6`, `1170:0045`).

**`Wave_Stop()`** (`1088:0114`): if `waveAvail`:
* if `waveFmtPtr != NULL`: `waveOutReset`, `waveOutUnprepareHeader`, `waveOutClose`, then `waveFmtPtr = NULL`;
* drain every pending `MM_WOM_DONE` with `PeekMessage(…, 0, 0x3BD, 0x3BD, PM_REMOVE|PM_NOYIELD = 3)`;
* `waveState = −1`.

**`Wave_IsPlaying()`** (`1088:0175`): if `PeekMessage(…, 0x3BD, 0x3BD, 3)` removes an `MM_WOM_DONE`, call
`Wave_Stop()`. Return `waveState != −1`. This polled call is the only end-of-sound detection.

**Window procedure** (`1010:0196`, switch table at `1010:0574`): `MM_WOM_DONE` (0x3BD) → `Wave_Stop()`
(`1010:039c`). This closes the device when a fire-and-forget effect ends while nobody polls.

**`Wave_PlayResUntilEvent(id, stopCodes far*)`** (`1088:01a9`), the blocking speech primitive:
```
Res_LockInPlace(id)                   ; 1038:0ba8
Wave_Play(scratchPtr, 0)
interrupted = 0
do {
    e = PeekEvent()                   ; 1028:01f6: fetch into a 1-event look-ahead (06ec), do not consume
    if (e) {
        for k in stopCodes until 0: if ((e & 0x0FFF) == k) interrupted = 1
        if (e == 0x13) interrupted = 1          ; Ctrl+S
        if (interrupted) Wave_Stop()            ; the event stays queued for the caller
        else GetEvent()                         ; 1028:0131: any other event is DISCARDED
    }
} while (Wave_IsPlaying() && !interrupted)
Res_UnlockInPlace()                   ; 1038:0ef8
return interrupted
```
* Mouse and key events that do not match are thrown away while speech plays.
* If there is no wave device, `Wave_Play` does nothing, `waveState` stays −1 and the loop exits at once.
  Speech then costs no time.
* `Wave_PlaySync(ssnd)` (`1088:023a`: play, busy-wait, stop) is never referenced [O].

### 5.3 Event codes relevant to sound [O]

From `1028:0131` and the window procedure. The type table is at `1028:01e8`.

| Code | Origin |
|---|---|
| 0x0D, 0x1B, 0x20, ASCII | `WM_CHAR` (type 2). 0x1B also comes from close (type 1). |
| **0x13** | `WM_CHAR` Ctrl+S. `1028:004f` (inside the event fetch) calls `Audio_ToggleAll()` `1128:0151` **before** returning the code, so Ctrl+S both toggles sound and interrupts speech. |
| 0x125–0x128 | Arrow keys (VK + 0x100) |
| `h`, `h\|0x8000`, `h\|0x4000`, `h\|0x2000` | Hotspot id `h` (table of 50 at `4b7a`, `1098:0016/0089`) for left-up, left-down, right-down and right-up |
| 0x0D / 0x409 / 0x4B0 as hotspots | Full-screen hotspots registered around speech (`1098:0016(0x0D,0,0,0x200,0x180)` at `1150:061e` and in `1150:0748`; `10e8:0658` → 0x409; `1130:00e1`/`1138:00a6` → 0x4B0). A click therefore matches the "Enter-like" stop codes. |

### 5.4 Settings, toggles and persistence [O]

| Flag | Meaning | Gates |
|---|---|---|
| `0b96` (u8) | "Music and Sound" | `Sfx_Play` (`1170:002a`), `Music_PlayZone`/`Music_Resume` (`1128:008d`, `1128:00dc`) |
| `0b97` (u8) | "Voice" | Every speech call site (`10e8:020f`, `1130:0182`, `1138:0293`, `1150:060b/070d/0816/08dc/1421/144a`) |

* **Ctrl+S**: `Audio_ToggleAll` (`1128:0151`). If either flag is set, both become 0, then
  `Sfx_Stop()` (`1170:005b`: `Wave_Stop` + free the effect copy) and `Midi_Stop()`. Otherwise both
  become 1 and `Music_Resume()`.
* **Options menu** (handlers in the menu table at `DGROUP:1377/137b/137f`):
  * `1140:00cc` shows the yes/no dialog at `DGROUP:1625` ("Do you want the Music and Sound on?",
    `15e6`), pre-set from `0b96`. If the answer differs it calls `Audio_ToggleSound()` (`1128:012f`):
    on→off sets `0b96=0`, `Wave_Stop`, `Midi_Stop`; off→on sets `0b96=1`, `Music_Resume`.
  * `1140:00f7` shows the dialog at `163f` ("…Voice on?", `160a`). It sets `0b97` directly and does not
    stop a playing sentence.
* **Per player**: the save routine `1160:0fd3/0fe8` writes `0b96` to player record +0x0E and `0b97` to
  +0x0F (record size 0x62F). On sign-in, `1160:13ce..1415` does this:
  ```
  if (rec[+0x0E] != 0b96 && 0b96 != 0) { Audio_ToggleSound(); 0b96 = rec[+0x0E]; }   ; 1128:012f, reloc 1160:13ea
  0b97 = rec[+0x0F]
  ```
  So **sign-in can switch music and sound off but never on**: if it is currently off and the record says
  on, `0b96` stays 0. The voice flag is always taken from the record.
* **New player** (`1160:1632`): if `0b96 == 0`, toggle it on (`1128:012f`). New-player hint mask
  `5f01 = 0xFFFFFFFF`.
* Speech on/off is snapshotted per elf question in `6145` (`1150:103b`). It decides whether line
  hotspots are registered (§6.3).

### 5.5 Pause and resume (application focus, menus, dialogs) [O]

| Trigger | Code | Action |
|---|---|---|
| `WM_ACTIVATE` wParam=0 (deactivated) and not in a dialog (`09dc==0`) | `1010:0297` | `App_Deactivate()` `1010:05c8`: if `049a`, set `049a=0`, then `Sound_PauseAll()` `1090:0536` = `Wave_Stop()` + `Midi_Pause()` |
| `WM_ACTIVATE` wParam=1/2, not iconic, `09dc==0` | `1010:02ac..0309` | `App_Activate()` `1010:05db`: if `!049a` and not iconic: `049a=1`, `Sound_ResumeAll()` `1090:0540` = `Midi_Resume()` |
| `WM_ENTERMENULOOP` (0x211) / `WM_EXITMENULOOP` (0x212) | `1010:030f` / `0317` | suspend / resume (resume only if `04a2==0`) |
| `WM_SYSCOMMAND SC_RESTORE` | `1010:0373` | resume |
| Modal dialog `1080:0000` | | `09dc=1`, suspend, `DialogBox`, resume, `09dc=0` |
| In-game message box `10c0:0ae5` | `10d0:0000` / `000e` | nesting counter `5cc8`: pause on entry; resume when the counter returns to 0. `10d0:0025` forces counter 0 and resumes. |

An interrupted sound effect or sentence is **not** resumed; it is simply cut. The event fetch
`1028:004f` keeps pumping messages and does not return while `049a == 0` (`1028:0116`), so game code,
including speech loops, is frozen while the app is inactive. After reactivation they find the sound
already stopped.

---

## 6. Speech (voice)

All speech is gated by `0b97` and played with `Wave_PlayResUntilEvent` (§5.2), except the intro
narration (§6.2), which uses its own loop. The stop-code lists are u16 arrays in DGROUP, copied to the
stack with `F_SCOPY@` (`1000:02f7`):

| List | Contents | Used by |
|---|---|---|
| `0f54` | 0x0D, 0x1B, 0 | clue-word replay `10e8:01ef` |
| `1414` | 0x0D, 0x1B, 0x4B0, 0 | hint popup `1130:0166` |
| `1e01`, `1e17` | 0x1B, 0x0D, 0 | praise `1150:06ac`, wrong answer `1150:0891` |
| `1e1d`, `1e43` | 0x126, 0x128, 0x125, 0x127, 0x0D, 0x409, 0x1B, 0xA80–0xA82, 0xA70–0xA76, 0xA88, 0 | puzzle lines `1150:1409`, choices `1150:14d7` |

0x13 (Ctrl+S) always interrupts as well.

### 6.1 Hint popups ("New Player Messages"): SSND 7100 + n [O]

* `QueueHint(n)` (`1130:0000`): only if no hint is pending (`13ce < 0`). `b = DGROUP:13d0[n]` (34
  entries: `0..9, −1, 10, 11, 12, −1, 13, 14, −1, 15, 16, −1, …, 25`). If `b < 0`, set `13ce = n`
  (always shown). Otherwise show it only if bit `b` of `5f01` is set; clear the bit (show once).
* `10e0:01ea` (room loop) shows a pending hint with `Hint_Show(n)` (`1130:0068`), then sets `13ce = −1`.
* `Hint_Show(n)`: draws the popup with text = entry n of BNDL 327 (OTHR 11500+n). It stops the music
  (`1128:00d6`) and adds hotspot 0x4B0 over the whole screen. Then it calls `Hint_WaitDismiss(n)` (`1130:0166`):
  ```
  r = voiceOn ? Wave_PlayResUntilEvent(7100 + n, {0x0D, 0x1B, 0x4B0}) : 0
  SetCursorMode(2)
  while (!r) { e = GetEvent(); if e in {0x0D, 0x409, 0x1B, 0x4B0, 0x24B0}: r = 1 }   ; wait for dismissal
  FlushInput()
  ```
  Afterwards `Music_Resume()` (`1130:0145`) restarts the room tune **from its beginning**.
* Hint ids queued [O]. The NE relocations hold exactly 14 far calls to `QueueHint` (segment 39, offset 0;
  `extracted/scratch/sound/hint_relocs.txt`). Each is listed below with its `CALLF` address:
  * 0 and 2: `10e0:014d`, `0155`
  * 3: `10e8:06f8`
  * 4: `10e8:074c`
  * 5 or 7: `10e0:0a88` and `1168:08d1`. Pressing Space (swing the net) with no nets (`5e5c == 0`)
    queues hint 5 if the player has coins (`5e5a > 0`), otherwise hint 7 (pushes at `10e0:0a82/0a86` and
    `1168:08cb/08cf`; see `code_10e0-10f0.md` step 9).
  * 6: `10e0:0abe`, `1168:0907`
  * 7: `10e0:0ad6`, `1168:091f`
  * 8: `10e0:0d7b`
  * 9: `10e0:078e`
  * table driven: `10e0:10e7` uses `0e8f` = {11,13,16,19,22,25,28,31}, and `10f0:0844` uses
    `0fc8` = {−1,12,15,…,30} or `0fd8` = {10,14,…,32}.
* **Hint 1 (SSND 7101) is never queued** [O], although `13d0[1]` gives it a mask bit and BNDL 327 has
  text for it. 7133 (hint 33) is never queued either, and BNDL 327 has only 33 text entries (0..32). Both
  recordings are unused in this build [O for "no caller"; I for "unused"].

### 6.2 Opening narration: SSND 7150–7159 [O]

`Title_Run()` (`1138:007a`) calls `Title_WaitPage(p)` (`1138:043d`) for p = 0..5. Page 0 is the title
picture and pages 1–4 are the story pages; `Title_WaitPage(5)` is the wait for the ending animation, not a
text page (corrected per ARCHITECTURE.md §7.1 C24; an earlier version said "six pages"):
```
SetTimer(4, 60000); SetTimer(7, 100); 0862 = 1
if (!voiceOn || p == 0 || p == 5) r = Title_WaitStep(0, 1)
else {
    id = DGROUP:141a[p]   ; p=1..4 → 7150, 7153, 7156, 7158
    n  = DGROUP:1422[p]   ; p=1..4 → 3, 3, 2, 2
    for (i = 0; i < n−1; i++, id++) { r = Title_WaitStep(id, 0); if (r > 0) break }
    if (r == 0) r = Title_WaitStep(id, 1)
}
```
`Title_WaitStep(id, waitForUser)` (`1138:0283`). If `id && voiceOn`, the sound is locked and started
without blocking. Then it loops:
1. If timer 7 expired: re-arm 100 ms and run one animation frame (`10b8:029d/0312/06e4/090a`,
   `1058:0731`).
2. If `5f35` (intro music watch) is set and `open1` is no longer playing (`1128:010c(−95)`), start
   `open2` (`1128:0000(−94)`) and clear `5f35`.
3. `e = GetEvent()`: Enter / 0x4B0 / 0x24B0 → return 1 (next page). Space → 2 (skip intro). Esc → stop
   the voice, ask the quit question (`DGROUP:2234`), return 3 if confirmed. 0x13 → stop the voice if it
   is now off.
4. If timer 4 expired (60 s) → return 0 (auto-advance).
5. If `!waitForUser` and the voice was started and `Wave_IsPlaying()` is false → return 0 (next clip).
6. If an intro animation slot (`5f27`, `5f33`) finished (`10b8:020a == 0`) → return 0.

On exit it calls `Wave_Stop()` and, if speech was started, `Res_UnlockInPlace()`. Negative results
become 0.

### 6.3 Elf questions: SSND 4000–6615 [O]

See `fmt_othr_puzzles.md` §8 for the full screen logic. The parts relevant to sound:

* `speechBase[p] = DGROUP:1990[p]` (u16, p = 0..358). The value after the last entry (6616 at
  `1990 + 359*2`) ends the last puzzle's range. Values increase monotonically from 4000.
* Line k of the question: SSND `speechBase[p] + k`, for 0 ≤ k < L (`L = 610f` = number of text lines).
* The current puzzle number p is `DGROUP:60fc` and the current elf-question slot is `60fa` (`1150:1409`,
  `1150:14d7`) [O].
* Displayed choice i (0..2): SSND `speechBase[p] + L + perm[i]` (`perm` = bytes at `5f3c + 8*slot`).
  The choice is read only if the byte at `5f3f + 8*slot + i` is 0, i.e. the choice has not been
  eliminated (`1150:1449`) [O].
  It is skipped when `6147 = 1`, meaning the puzzle is in the 16-entry list at `DGROUP:1dc7`
  {8, 235, 257, 154, 50, 169, 298, 299, 170, 176, 177, 178, 210, 231, 117, 148}.
* Check: for puzzles 0 and 8, `speechBase[p+1] − speechBase[p]` equals text lines + 3 (8) and text
  lines + 0 (6, a no-choice puzzle). This matches OTHR 11000/11008. A heuristic line count over all 359
  puzzles agrees for 322; the remaining ones differ only in how blank lines split question and choices.
* **Read-aloud sequence** `Quiz_SpeakAll()` (`1150:1449`, only if `0b97`):
  ```
  198e = 1
  for k in 0..L−1:  Highlight(10+k); 6137 = PlayUntil(speechBase+k, list@1e1d); Unhighlight(10+k); if 6137: return
  BusyWait(250 ms)                                       ; 10a8:0000: t0 = timeGetTime(); while (timeGetTime() < t0 + ms) {}  (no message pump)
  for i in 0..2 if !eliminated[i]:
      BusyWait(500 ms); Highlight(i); 6137 = PlayUntil(speechBase+L+perm[i], list@1e43); Unhighlight(i); if 6137: return
  ```
  `Highlight(item, 0)` (`1150:0d2d`) puts a yellow "marker" over the line or choice row being read:
  `Gfx_HighlightRect` (`1058:19b2`) colour-remaps the rectangle (see `fmt_othr_puzzles.md` §8.2).
  `Unhighlight` (`1150:0e30`) restores the clean copy kept 384 rows lower in the back buffer. **This is
  the only speech/text synchronisation in the game:** the line being spoken is highlighted. There is no
  word timing.
* Replay: if speech was on when the screen opened (`6145`), each text line k gets hotspot `0xA70+k`.
  Clicking it (`1150:120d..1244`) replays `speechBase + k` (`1150:1409`). The speaker button 0xA88 and
  the choice rows 0xA80+i are also in the stop list, so clicking them cuts the current sentence.

### 6.4 Praise, wrong-answer and clue-word phrases: SSND 7300/7400/7500 + k [O]

`bank = 7300, 7400 or 7500` for voice set `v = DGROUP:1c60[p]` = 1, 2 or 3 (124 / 129 / 106 puzzles).

| k | Meaning | Text shown (DGROUP, identifying words only) | Played by |
|---|---|---|---|
| 0–3 | Praise variant r = `rand()*4/0x8000` | far pointers `1df1..1dff` → `1e69` "Great job, Super Seeker!…", `1ea1` "Yes! That's right.…", `1ed3` "Good job, Super Seeker!…", `1f21` "Great! You got it right.…" | `1150:06ac(v, r, clue)`, first sound |
| 4–7 | Wrong-answer variant r | `1e07..1e15` → `1f5b` "Sorry!…", `1f94` "Oops! You missed it!…", `1fe0` "Oops! That's not it.…", `200f` "Oops! You missed it this time.…" | `1150:0891(v, r)` plays `bank+4+r` |
| 8–52 | Clue word (45 words) | the clue word itself | `1150:06ac` second sound, `bank + clueVoice` (`clueVoice` = `5eb7[level*3 + n]`) |

* Correct-answer screen `1150:049a`: `Sfx_Play(12)`, draw the praise text matching `r` and the clue word,
  then **busy-wait until sfx 12 has finished** (`do {} while (Wave_IsPlaying())`, `1150:0602`). Then, if
  voice is on: add hotspot 0x0D over the whole screen, speak `bank+r` and, if that was not interrupted,
  `bank+clue`. If nothing interrupted the speech, wait for 0x0D / 0x1B / 0x20 / 0x409 / 0x2409.
* Wrong-answer screen `1150:0748`: `Sfx_Play(8)`, draw the text, wait for sfx 8 to finish, speak
  `bank+4+r`, then wait the same way.
* Clue review (`'c'`, `'C'` or 0x2063 in the room loop, `10e0:0196..01ac` → `10e8:0228`): for each clue
  collected on this level, `10e8:01ef(clueVoice)` speaks **7300 + clueVoice** (always voice bank 1) with
  stop list `0f54`. The result is ignored, so Esc or Enter skips only the current word.
* A crude autocorrelation pitch estimate of the first line of each puzzle gives median F0 of 269 Hz
  (bank 1), 230 Hz (bank 2) and 287 Hz (bank 3). This weakly supports "bank = the voice talent who read
  that puzzle" [I] (script `extracted/scratch/sound/pitch.py`).

### 6.5 Synchronisation and interruption rules (summary)

1. **No lip sync** [O]: there is no `waveOutGetPosition`, no per-buffer callbacks (a single WAVEHDR per
   sound) and no timing data in SSND.
2. Speech is **blocking** except in the intro, where it is polled alongside a 100 ms animation tick and
   a 60 s page timeout.
3. Text is drawn **before** its speech starts. On the puzzle screen each line is highlighted
   (yellow marker) while it is read.
4. A jingle (sfx 12 or 8) is allowed to **finish before** the following speech starts (§6.4).
5. Any sound start (effect or speech) **cuts** the current sound. Nothing queues.
6. Interruption: the listed stop codes and Ctrl+S end a sentence at once. In multi-part sequences an
   interruption also skips the rest (praise → clue word; question lines → choices; intro clips → page).
7. Music is **stopped** for elf questions (`1150:103e` → `1128:00d6`) and hint popups (`1130:0118`),
   and restarted from the beginning afterwards (`1150:1382`, `1130:0145` → `1128:00dc`).

---

## 7. Sound effects: SSND 7000 + n (TM0SOUND.DLL)

### 7.1 API [O]

* `Sfx_Play(n)` (`1170:0000`):
  1. If an effect copy is held (`2ab2:2ab4`), `Wave_Stop()` and free it.
  2. If `0b96`: `2ab2 = Res_LoadCopy(7000+n)` and `Wave_Play(2ab2, 0)`.

  It does not block. The device is closed by the window procedure on `MM_WOM_DONE`, or by the next
  `Wave_Stop`.
* `Sfx_Stop()` (`1170:005b`): `Wave_Stop()`, then free and NULL the copy. It does **not** wait for the
  effect to end: the code at `1170:005b..0081` stops immediately (`code_10e0-10f0.md` §2.3/§6.2 used to
  say "waits" and have been corrected per ARCHITECTURE.md §7.1 C12).
* `Sfx_IsPlaying()` (`1170:0082`) = `Wave_IsPlaying()`. It is never referenced.
* Animation scripts (OTHR resources tagged `"DAM\x1a"`, interpreted by `10b8:0312`) can also play
  effects. Opcode **0xFC** is followed by a big-endian u16, and the low byte is passed to
  `Sfx_Play(n)`: the interpreter skips one byte and reads the next. Opcode **0xFB** plus one pad byte
  calls `Sfx_Stop()`.

### 7.2 Effect index table

Meanings marked [I] come from the calling code and the cited documents. Nobody has listened to the
sounds.

| n | SSND | Dur (s) | Triggered at (all [O]) | Meaning |
|---|---|---|---|---|
| 0 | 7000 | 1.58 | coin toss step 7: `10e0:072e`, `1168:087b` | coin toss [I] |
| 1 | 7001 | 0.53 | elf behaviour 4 (flee) steps 4, 6, 7: sound table `DGROUP:2c1f` via `1178:0a62` | elf flees [I] |
| 2 | 7002 | 0.87 | anim script OTHR 20305 (celebration, BNDL 324) | celebration [I] |
| 3 | 7003 | 2.65 | anim 20301 (celebration, `10f0:01de`) | celebration [I] |
| 4 | 7004 | 3.57 | none found | unused? |
| 5 | 7005 | 0.86 | wall creature tick 3: `1168:12d7` | creature pops out [I] |
| 6 | 7006 | 0.38 | net swing step 6 (`10e0:05c2`, `1168:0720`), coin toss step 8 (`10e0:0740`, `1168:088c`) | throw / swish [I] |
| 7 | 7007 | 1.49 | bonus coin picked up: `1110:0149` | coin pickup [I] |
| 8 | 7008 | 1.78 | wrong answer `1150:0788`; the thrower elf's sparkle/coin projectile (BNDL 310) hits the player, coin lost: `1178:0eaf` (corrected per ARCHITECTURE.md §7.1 C19, was "snowball") | failure [I] |
| 9 | 7009 | 0.53 | anim 20305 | celebration |
| 10 | 7010 | 0.74 | anim 20300 (BNDL 323) | celebration |
| 11 | 7011 | 0.74 | anim 20301, 20305 | celebration |
| 12 | 7012 | 1.56 | correct answer `1150:04c4`; key obtained `10e8:06d9` (`Inv_SetHaveKey`) | success jingle [I] |
| 13 | 7013 | 7.00 | anim 20204 (BNDL 412, level-1 → level-2 transition, `1188:0047`) | transition [I] |
| 14 | 7014 | 0.22 | anim 20300, 20305 | celebration |
| 15 | 7015 | 0.31 | gate unlock, level 1: `10e0:08dd` | |
| 16 | 7016 | 0.49 | gate unlock, level 2: `10e0:08e1`; anim 20300 | |
| 17 | 7017 | 0.97 | gate unlock, level 0: `10e0:08d9`; anim 20300 | |
| 18 | 7018 | 1.83 | anim 20300 | celebration |
| 19 | 7019 | 1.88 | none found | unused? |
| 20 | 7020 | 1.69 | anim 20305 | celebration |
| 21 | 7021 | 2.55 | anim 20305 | celebration |
| 22 | 7022 | 2.10 | rank screen shown: `10f0:062b` | rank fanfare [I] |
| 23 | 7023 | 0.44 | player frame 12 (`10e0:0b47/0bda/0c61`, unreachable per `code_10e0-10f0.md`); anim 20301 | |
| 24 | 7024 | 0.29 | player frame 22 (`10e0:0b4f/0be2/0c69`, unreachable); anim 20301 | |
| 25 | 7025 | 0.14 | ladder climb phase 3: `1168:0f0b/0f84`; anims 20200/20201 (level-0 → level-1 transition, BNDL 410) | ladder step [I] |
| 26 | 7026 | 0.40 | none found | unused? |
| 27 | 7027 | 0.14 | ladder climb phase 1: `1168:0f07/0f80`; anims 20200/20201 | ladder step [I] |
| 28 | 7028 | 0.34 | none found | unused? |
| 29 | 7029 | 0.42 | wall creature tick 2: `1168:12d3` | |
| 30 | 7030 | 0.32 | net swing step 3: `10e0:05b1`, `1168:070f` | net whoosh [I] |
| 31 | 7031 | 1.89 | thrower elf launches its sparkle projectile (BNDL 310, throw state 5): `1178:0dc3` (corrected per ARCHITECTURE.md §7.1 C19, was "snowball") | |
| 32 | 7032 | 0.27 | none found | unused? |
| 33 | 7033 | 3.11 | falling (fall step 1, repeats every 7 ticks): `1168:0ddb`; anim 20308 (BNDL 325) | fall [I] |
| 34 | 7034 | 0.55 | coin toss step 4: `10e0:071c`, `1168:086a` | coin flip [I] |
| 35 | 7035 | 0.74 | player jump (frame 14): `10e0:0b4b/0bde/0c65`; anim 20301 | jump [I] |
| 36 | 7036 | 0.38 | none found | unused? |
| 37 | 7037 | 0.49 | elf behaviour 4 step 8 (`2c1f`) | |
| 38 | 7038 | 0.44 | none found | unused? |
| 39 | 7039 | 0.31 | none found | unused? |
| 40 | 7040 | 0.23 | bump step 6: `10e0:0a38`; landing step 1: `1168:0e64`; anim 20305 | bump / thud [I] |
| 41 | 7041 | 1.83 | treasure collected: `10e8:0784` (`Inv_CollectTreasure`) | treasure [I] |

The animation-script effects were found by parsing every `DAM\x1a` OTHR script (20000–20050,
20100–20102, 20200–20205, 20300–20308) with the opcode sizes from `10b8:0312`. Opcodes and argument
sizes: F9 = 6, FA = 3, FB = 1, FC = 2, FD = 6, FE = end of frame, FF = end of script. The interpreter
consumes the opcode byte before the switch. Every other byte value is consumed as 1 byte and ignored.
Script `extracted/scratch/sound/anim_sfx_all.py` scans every OTHR resource of TMT.DLL directly. The
older `anim_sfx.py` only followed the first entry of each BNDL. Per script [O]:
20200 → 27, 25, 27, 25, 27; 20201 → 25, 27, 25, 27, 25, 27, 25, 27; 20204 → 13;
20300 → 16, 18, 17, 14, 10; 20301 → 11, 23, 35, 24, 3; 20305 → 11, 14, 9, 20, 2, 40, 21; 20308 → 33.
No script uses opcode 0xFB (stop).

**The trigger list is complete** [O]. The NE relocation records of every code segment contain exactly
**30** far-call fixups to `Sfx_Play` (segment 47, offset 0). They are listed in
`extracted/scratch/sound/sfx_relocs.txt` (made by `reloc_targets.py 47:0000`): `10b8:04e1` (anim opcode),
`10e0:05b3 05c4 071e 0730 0742 08e3 0a3a 0b51 0be4 0c6b`, `10e8:06db 0786`, `10f0:062d`, `1110:014b`,
`1150:04c6 078a`, `1168:0711 0722 086c 087d 088e 0ddd 0e66 0f0d 0f86 12d9`, `1178:0a62 0dc5 0eb1`. Each one
is accounted for in the table. Of the five elf-behaviour wrappers `1178:0a6d/0a90/0ab3/0ad6/0af9`, only
behaviour 4 (`0af9`) passes a sound table (`2c1f` = −1, −1, −1, 1, −1, 1, 1, 37, −1, indexed by step−1).
The others pass NULL. **sfx 4, 19, 26, 28, 32, 36, 38 and 39 therefore have no trigger anywhere in the
EXE or in the animation scripts.**

---

## 8. MIDI music

### 8.1 The files [O]

All 20 files are SMF format 0, one track, 480 ticks per quarter note, a single tempo, and these meta
events: sequencer-specific `FF 7F 03 00 00 41`, track name (`"BCastle"`, `"ETitle"`, …), SMPTE offset,
time signature, set tempo and end of track. FINALE and OPEN2 (both variants) also carry one key
signature (`FF 59`). CLUBHSE has three time-signature events and OPEN1 has two. No SysEx. Only
controllers 7 (volume) and 64 (sustain) are used. B and E versions of the same tune have the same tempo
and end-of-track tick except OPEN1. CLUBHSE, LEVEL2 and LEVEL3 have the same file size in both variants
(only the channel numbers differ). Script: `extracted/scratch/sound/midi_info.py`.

| Tune | id | B channels (prog) | E channels (prog) | Length B / E (s) | BPM |
|---|---|---|---|---|---|
| castle | 9000 | 13 (6), 14 (73), 15 (33), 16 drums | 2 (6), 3 (73), 4 (33), 5 (48), 10 drums | 67.9 / 67.9 | 106 |
| clubhse | 9001 | 13 (73), 14 (24), 15 (35), 16 drums | 2 (73), 3 (24), 4 (35), 10 drums | 43.9 / 43.9 | 127 |
| finale | 9002 | 13 (60), 14 (35), 15 (4), 16 | 2 (60), 3 (6), 4 (35), 5 (4), 10 | 7.7 / 7.7 | 125 |
| level1 | 9003 | 13 (6), 14 (108), 15 (32/33), 16 | 2 (6), 3 (108), 4 (32/33), 5 (73), 10 | 52.0 / 52.0 | 97 |
| level2 | 9004 | 13 (73), 14 (24), 15 (33), 16 | 2 (73), 3 (24), 4 (33), 10 | 45.2 / 45.2 | 140 |
| level3 | 9005 | 13 (73), 14 (24), 15 (35), 16 | 2 (73), 3 (24), 4 (35), 10 | 55.0 / 55.0 | 157 |
| open1 | 9006 | 13 (60), 14 (35), 15 (24), 16 | 2 (60), 3 (73), 4 (35), 5 (24), 6 (6), 10 | **61.9** / 8.2 | 125 |
| open2 | 9007 | 13 (24), 14 (35), 15 (60), 16 | 2 (24), 3 (6), 4 (35), 5 (60), 6 (73), 10 | 23.0 / 23.0 | 125 |
| throne | 9008 | 13 (68), 14 (24), 15 (33), 16 | 2 (68), 3 (24), 4 (33), 5 (4), 6 (48), 10 | 27.4 / 27.4 | 140 |
| title | 9009 | 13 (73), 14 (35), 15 (71), 16 | 2 (73), 3 (35), 4 (71), 5 (73), 10 | 16.7 / 16.7 | 115 |

* **B files follow Microsoft's "base-level synthesizer" layout**: melodic parts on channels 13–15 and
  percussion on 16. **E files follow the "extended" layout**: melodic parts on 1–9 (here 2–6) and
  percussion on 10. E versions often add one or two extra parts. The FF 7F `00 00 41` event is the
  Microsoft-authored-MIDI marker written by MARKMIDI, which stops the Windows 3.1 MCI sequencer from
  warning about non-standard files [I, from the Windows 3.1 MIDI authoring guidelines].
* **BOPEN1.MID** has its last note-on at tick 7920 (7.9 s) and its last note-off at 8160 (8.2 s), but its
  end-of-track is at tick 61920 (61.9 s), which adds about 54 s of silence. EOPEN1 ends at 8160. Because `open1` plays once and the intro switches to
  `open2` only when `open1` ends (§8.5), base-level systems get about 54 s of silence before `open2`
  [O for the file, I for the audible effect].

### 8.2 Initialisation and B/E selection: `Midi_Init()` (`1090:0000`) [O]

Called from `CheckSoundHardware` (`1018:0484`) **only if** `Wave_Init()` reported a wave device.
1. `soundsDir` (`4af4`) = directory of `GetModuleFileName(hInst)` (cut after the last `\` or `:`) +
   `"SOUNDS\"` (`DGROUP:0b16`).
2. `extended (4b76) = 1`.
3. `midiOutGetDevCaps(MIDI_MAPPER = 0xFFFF, &caps, 50)`. If it returns 6 (`MMSYSERR_NODRIVER`),
   return 0.
4. `f = _lopen(GetWindowsDirectory() + "\system\midimap.cfg", OF_READ)` (`DGROUP:0acc`). If that fails,
   return 0.
5. `_llseek(f, 6, 0)`. Read u16 `n` [I: 1-based index of the current setup]. `_llseek(f, (n−1)*0x36 +
   0x12, 0)`. Read 16 bytes into the start of the path buffer [I: the setup name]. Apply `toupper` to each
   of the 16 bytes. The buffer is not NUL-terminated after byte 16, so the leftover tail of the path
   string follows; this is harmless.
6. If `_fstrstr(buf, s)` finds any of `"ADLIB"` (`0ae0`), `"AD LIB"` (`0ae6`), `"MVI PRO"` (`0aed`),
   `"BAS"` (`0af5`), `"BASE"` (`0af9`) or `"THUNDER BOARD"` (`0afe`), set `extended = 0` (B files).
   Any short read or failed seek skips the test and keeps E. `_lclose`.
7. `MCI_OPEN` (0x803), flags `MCI_OPEN_TYPE` (0x2000), `lpstrDeviceType = "sequencer"` (`0b0c`) →
   `seqDev (0a50) = wDeviceID`, `midiOk (0a4a) = 1`.
8. `Midi_OpenTrack(9000)`. Then `MCI_STATUS` (0x814), flags `MCI_STATUS_ITEM` (0x100),
   `dwItem = MCI_SEQ_STATUS_PORT` (0x4003). Then `Midi_Stop()`.
9. If the status call failed, or the low word of `dwReturn` ≠ 0xFFFF (port is not `MIDI_MAPPER`):
   `MCI_CLOSE seqDev`, `midiOk = 0`, return 0. Otherwise return `midiOk` (1).

If `Wave_Init()` is false or `Midi_Init()` returns 0, and `[TMTWIN] CheckSound` = `"TRUE"`, the
TOOBADNOSOUND dialog appears (`1018:0492`). Exit aborts start-up; the checkbox writes
`CheckSound=FALSE`.

`midiOk` is only consulted by `Midi_IsPlaying` and `Midi_Shutdown`. **`Midi_PlayTrack` does not check
it** (§8.3). When init returns 0 at step 3, 4 or 9, `soundsDir` is already set and `extended = 1`.
Later `Music_PlayZone` calls still open `…\SOUNDS\E*.mid` by file name and play it if the MCI sequencer can
open the file [O for the code path; I for the run-time result]. (`code_1058-1090.md` §10.1 used to say
music "never plays" without midimap.cfg; it has been corrected per ARCHITECTURE.md §7.1 C18: that holds
only if the MCI open also fails.) When there is no wave
device at all, `soundsDir` stays empty and the path becomes a relative `Bxxx.mid`, which normally fails
to open [I].

### 8.3 MCI command sequences [O]

Parameter blocks are Win16 MCI structs on the stack. Device ids: `seqDev` (`0a50`), `trackDev` (`0a52`).

| Function | Address | Sequence |
|---|---|---|
| `Midi_OpenTrack(id)` | `1090:02e9` | `idx (4b78)` = position of `id` in `DGROUP:09fa[10]` (10 if not found; never happens). `path = soundsDir`. Overwrite byte 0 of `musicFile[idx]` (far pointers at `0a22`, strings `0a56..0acb`, each starting with a space) with `'B'` or `'E'`. Append it. `MCI_OPEN` flags `MCI_OPEN_ELEMENT` (0x200), `lpstrElementName = path` (the device type follows from the `.mid` extension) → `trackDev`. `savedPos (0a54) = 0`. |
| `Midi_PlayTrack(id)` | `1090:0388` | If `trackDev`: `MCI_CLOSE trackDev`. `Midi_OpenTrack(id)`. If `trackDev && appActive (049a)`: `curMusic (0a4c) = id`; `MCI_PLAY` flags `MCI_NOTIFY` (1), `dwCallback = hwndMain`. |
| `Midi_Stop()` | `1090:0413` | If `trackDev`: `MCI_STOP` (0x808), `MCI_CLOSE` (0x804), `trackDev = 0`. Always `curMusic = −1`. |
| `Midi_Pause()` | `1090:044b` | If `!paused (0a4e)`: if `loopPos[idx] == −1`, `Midi_Stop()` (a one-shot tune is abandoned). Else if `trackDev`: `MCI_STATUS` item `MCI_STATUS_POSITION` (2) → `savedPos = (i16)dwReturn` (0 on error); `MCI_STOP`; `paused = 1`. |
| `Midi_Resume()` | `1090:04bf` | If `paused && trackDev`: `MCI_SEEK` flags `MCI_TO\|MCI_WAIT` (0x0A), `dwTo = (i32)savedPos`; `MCI_PLAY` + `MCI_NOTIFY` (hwndMain); `paused = 0`. |
| `Midi_OnNotify()` | `1090:03e6` | From the window procedure: `MM_MCINOTIFY` (0x3B9) with wParam = `MCI_NOTIFY_SUCCESSFUL` (1) (`1010:0529`). If `loopPos[idx] == −1`: `Midi_Stop()`. Else `paused = 1`, `savedPos = loopPos[idx]` (0), `Midi_Resume()`. Other notify codes and lParam (device id) are ignored. |
| `Midi_IsPlaying()` | `1090:0519` | `curMusic != −1 && trackDev && midiOk` |
| `Midi_Shutdown()` | `1090:02af` | If `midiOk`: close `trackDev` and `seqDev` if open. Called at exit (`1010:00cd`). |

Tables: `09fa` u16[10] = 9000..9009; `0a0e` i16[10] `loopPos` = 0 except index 6 (`open1`) = −1;
`0a22` far*[10] → `" castle.mid"`, `" clubhse.mid"`, `" finale.mid"`, `" level1.mid"`, `" level2.mid"`,
`" level3.mid"`, `" open1.mid"`, `" open2.mid"`, `" throne.mid"`, `" title.mid"` (verified through the
DGROUP relocations at `0a22..0a46`).

Notes [O]:
* **Looping** works by re-seeking to `loopPos` (0) and playing again on each successful notification.
  This leaves a small gap at the restart [I].
* **No fades or volume changes**: there is no `MCI_SET`, `midiOutSetVolume` or controller injection.
  Tune changes are hard stop / close / open / play.
* The time format is never set, so the device default is used for both `MCI_STATUS_POSITION` and
  `MCI_SEEK`. `savedPos` is truncated to 16 bits. If the default is milliseconds, pausing after 65.5 s
  resumes at the wrong place [I]. Only `castle` (67.9 s) is long enough.
* If `Midi_PlayTrack` runs while the app is inactive, the track is opened but not played, and
  `paused = 0`, so `Midi_Resume` will not start it either. Music then stays silent until the next
  `Music_PlayZone` or `Music_Resume` [I from the code paths].

### 8.4 Scene music controller, segment 1128 [O]

| Function | Address | Behaviour |
|---|---|---|
| `Music_PlayZone(code)` | `1128:0000` | `4abe = 0`. Map `code` to a tune id (table below), store it in `13ca`, then `5efe = code`, `13c6 = 1` (u32), `5efd = 0`. If `0b96`: `Midi_PlayTrack(13ca)`. Codes not in the table do nothing. The tune is recorded even with sound off, so turning sound on later starts it. |
| `Music_Halt()` | `1128:00d6` | `Midi_Stop()` |
| `Music_Resume()` | `1128:00dc` | If `0b96`: `Midi_PlayTrack(13ca)` (from the start) |
| `Music_StopScene()` | `1128:00ef` | If `13c6`: `Midi_Stop()`, `13c6 = 0`, `5efe = 0` |
| `Music_IsZonePlaying(code)` | `1128:010c` | `Midi_IsPlaying() && 5efe == code` |
| `Audio_ToggleSound()` | `1128:012f` | §5.4 |
| `Audio_ToggleAll()` | `1128:0151` | §5.4 (Ctrl+S) |

`Music_PlayZone` code → tune. The switch is at `1128:001a` (13 values at CS:`00a2`, targets at CS:`00bc`):

| code | tune id | file |
|---|---|---|
| 0 | 9001 | clubhse |
| 1, 2 | 9003 | level1 |
| 3, 4 | 9004 | level2 |
| 5, 6 | 9005 | level3 |
| 7 | 9000 | castle |
| −98 | 9002 | finale |
| −97 | 9009 | title |
| −96 | 9008 | throne |
| −95 | 9006 | open1 |
| −94 | 9007 | open2 |

### 8.5 Which tune plays where [O]

| Situation | Code | Tune | Evidence |
|---|---|---|---|
| Intro / title pages | −95 | open1 (once) | `Title_Run` `1138:00ae` |
| Intro, after open1 ends | −94 | open2 (loops) | `1138:031d..0331` (polled in the intro loop) |
| Player sign-in screen | −94 | open2, if not already playing | `1160:0130..013e` |
| Rooms 0–8 (castle treasure hall) | 0 | clubhse | RoomDef.music |
| Rooms 9–22 (mountain level 0) | 1 | level1 | RoomDef.music |
| Rooms 23–35 (level 1) | 3 | level2 | RoomDef.music |
| Rooms 36–48 (level 2) | 5 | level3 | RoomDef.music |
| Room 49 (castle-wall ladder climb) | 7 | castle | RoomDef.music |
| Celebration after a climb | −96 / −98 | throne; finale when the player has just become Champion (`5ef2`) | `10f0:001c..0022` |
| Elf question, hint popup | — | silence, then the room tune restarts | §6.5 rule 7 |
| title.mid (9009) | −97 | **never requested**: no push of −97 exists in any code segment (raw byte scan) | |

Room music: `RoomDef[room].music` is the byte at `DGROUP:23d8 + room*0x1C + 0x1B`, read by
`1118:019d/0312/0356`. Codes 2, 4 and 6 are not used by any room [O]. The main loop (`1118:0000`)
calls `Music_PlayZone(RoomDef[room].music)` when a climb starts. After each room change it calls
`Music_StopScene()` and `Music_PlayZone(new)`, but only if the music code differs. For rooms 0 and 9 the
select is deferred until after the room is drawn (`bVar3`). At the end of a climb it calls
`Music_StopScene()`, then the celebration. Other `Music_StopScene` callers: `1118:0414` (quit path),
`1140:007a`, `1160:0217`, `1160:0f1a`.

---

## 9. Sound id map

| Ids | DLL | Count | Meaning | Triggering code (all [O]) |
|---|---|---|---|---|
| 4000–6615 | TM2–TM8 | 2616 | Elf-question narration. Puzzle p uses `speechBase[p]` .. `speechBase[p+1]−1`: question lines, then the three answer choices in canonical order | `1150:1409` (line), `1150:14d7` (choice), sequenced by `1150:1449`, replay `1150:1244` |
| 7000–7041 | TM0 | 42 | Sound effects sfx 0–41 (§7.2) | `1170:0000` from 10e0/10e8/10f0/1110/1150/1168/1178, and anim opcode 0xFC (`10b8:04e1`) |
| 7100–7132 | TM1 | 33 | Hint narration n = id−7100; text = BNDL 327 entry n (OTHR 11500+n). 7101 (hint 1) is never queued | `1130:0166` via `1130:0068`; queued from the 14 sites in §6.1 |
| 7133 | TM1 | 1 | Hint 33: no trigger, no text. Unused | — |
| 7150–7152, 7153–7155, 7156–7157, 7158–7159 | TM1 | 10 | Opening narration for intro pages 1, 2, 3, 4 | `1138:043d` (tables `141a/1422`) → `1138:0283` |
| 7300–7303 / 7400–7403 / 7500–7503 | TM1 | 12 | Praise "you got it", voice banks 1/2/3, 4 variants each | `1150:06ac` |
| 7304–7307 / 7404–7407 / 7504–7507 | TM1 | 12 | Wrong answer, 4 variants per bank | `1150:0891` |
| 7308–7352 / 7408–7452 / 7508–7552 | TM1 | 135 | Clue words 0..44 (+8), per bank. Bank 1 is also used for the clue review | `1150:06ac` (2nd sound), `10e8:01ef` |
| 6800–6999, 8000–8999 | — | 0 | Unmapped by the EXE | — |
| 9000–9009 | (files) | 10 | Music tune ids (not resources) | `1090:02e9` |

### 9.1 Call-site index (from NE relocation records) [O]

Every far call to the sound and music entry points, found by walking the relocation chains of all code
segments (`extracted/scratch/sound/reloc_targets.py SEG:OFF`; segment 18 = `1088`, 19 = `1090`,
38 = `1128`, 47 = `1170`). Addresses are the `CALLF` (or `JMPF`) instructions. Near calls inside the
same segment have no relocation; they are listed in brackets.

| Target | Call sites | Context |
|---|---|---|
| `Wave_Play` `1088:0014` | `1138:02c1`; `1170:0050`; [`1088:01c0`] | intro narration; `Sfx_Play`; `Wave_PlayResUntilEvent` |
| `Wave_Stop` `1088:0114` | `1010:00c8`; `1010:039c`; `1090:0536`; `1128:013b`; `1138:036c`, `039c`, `041a`; `1170:000c`, `005b`; [inside 1088] | app shutdown; WndProc `MM_WOM_DONE`; `Sound_PauseAll`; `Audio_ToggleSound`; intro: Esc (voice on), Ctrl+S that turned voice off, exit of `Title_WaitStep`; `Sfx_Play`; `Sfx_Stop` |
| `Wave_IsPlaying` `1088:0175` | `1138:03c0`; `1150:0602`; `1150:080d`; `1170:0082`; [`1088:01a9`] | intro clip end; wait for sfx 12; wait for sfx 8; `Sfx_IsPlaying` (itself unreferenced) |
| `Wave_PlayResUntilEvent` `1088:01a9` | `10e8:021e`; `1130:0195`; `1150:071c`, `0732`; `1150:08e9`; `1150:143b`; `1150:1520` | clue review word; hint; praise + clue word; wrong answer; puzzle line; puzzle choice |
| `Wave_PlaySync` `1088:023a` | none | dead code |
| `Sfx_Play` `1170:0000` | 30 sites, §7.2 | |
| `Sfx_Stop` `1170:005b` | `10b8:04ff`; `10f0:0655`; `1128:0167`; `1188:0093`, `0191` | anim opcode 0xFB; rank screen `10f0:054b` (plays sfx 22, runs the per-rank loop `10f0:0702`, then cuts the effect before waiting for Enter/Space); `Audio_ToggleAll`; level-transition cut-scene `1188:0000` (skip path and normal end) |
| `Music_PlayZone` `1128:0000` | `10f0:0022`; `1118:01a4`, `0319`, `035d`; `1138:00b0`; `1138:032b`; `1160:013e` | celebration (−96/−98); room changes; intro (−95); intro (−94); sign-in (−94) |
| `Music_Halt` `1128:00d6` | `1130:0118`; `1150:103e` | hint popup; elf question |
| `Music_Resume` `1128:00dc` | `1130:0144`; `1150:1381` | after hint; after elf question |
| `Music_StopScene` `1128:00ef` | `1118:028b`, `037a`, `0413`; `1140:0079`; `1160:0216`, `0f19` | climb start/end and quit; menu; player screens |
| `Music_IsZonePlaying` `1128:010c` | `1138:031f`; `1160:0132` | intro open1 → open2; sign-in |
| `Audio_ToggleSound` `1128:012f` | `1140:00ee`; `1160:13e9`; `1160:1647` | options dialog; sign-in; new player |
| `Audio_ToggleAll` `1128:0151` | `1028:00f0` | Ctrl+S inside the event fetch |
| `Midi_PlayTrack` `1090:0388` | `1128:0098`; `1128:00e7` | `Music_PlayZone`; `Music_Resume` |
| `Midi_Stop` `1090:0413` | `1128:00d6` (JMPF), `00f7`, `0140`, `016c`; [inside 1090] | |
| `Midi_IsPlaying` `1090:0519` | `1128:0110` | |
| `Sound_PauseAll` `1090:0536` | `1010:05d5`; `10d0:0000` | `App_Deactivate`; message-box enter |
| `Sound_ResumeAll` `1090:0540` | `1010:05f5`; `10d0:001f`, `002a` | `App_Activate`; message-box leave / force |
| `Midi_Pause` / `Midi_Resume` `1090:044b/04bf` | [`1090:0536`, `0540`, `03e6`] only | |
| `Wave_Init` `1088:0000`, `Midi_Init` `1090:0000` | `1018:043a` (`CheckSoundHardware`) | start-up |

---

## 10. DGROUP reference (sound/music)

| Addr | Type | Name | Notes |
|---|---|---|---|
| 049a | i16 | appActive | Clear while deactivated; gates play and pause/resume |
| 0741..075e | char[] | type names | "BNDL", "RRGB", "FFNT", "PMAP", "SSND", "OTHR" |
| 073a | HGLOBAL | speechRes | Locked speech resource |
| 09dc | i16 | inDialog | Blocks focus-driven pause/resume |
| 09de..09ee | HINSTANCE[9] | hSoundDll | TM0..TM8SOUND |
| 09f2/09f4/09f8 | | wave state | §5.2 |
| 09fa / 0a0e / 0a22 | | music tables | §8.3 |
| 0a4a/0a4c/0a4e/0a50/0a52/0a54 | | midiOk / curMusic / paused / seqDev / trackDev / savedPos | §8.3 |
| 0a56..0b1e | char[] | strings | tune files, midimap path, synth names, "sequencer", "SOUNDS\" |
| 0b96 / 0b97 | u8 | soundOn / voiceOn | §5.4 |
| 0f54, 1414, 1e01, 1e17, 1e1d, 1e43 | u16[] | stop-code lists | §6 |
| 13c6 | u32 | sceneMusicActive | |
| 13ca | i16 | sceneTuneId | |
| 13ce | i16 | pendingHint | −1 = none |
| 13d0 | i16[34] | hintBit | §6.1 |
| 141a / 1422 | i16[] | intro clip start / count | indexed by page 1..4 |
| 15e6 / 160a / 1625 / 163f | | option dialogs | "Music and Sound on?" / "Voice on?" |
| 1990 | u16[359+1] | speechBase | §6.3 |
| 1c60 | u8[359] | voiceBank | 1..3 |
| 1dc7 | u16[16] | noChoiceSpeech | |
| 1df1 / 1e07 | far*[4] | praise / wrong texts | |
| 23d8 + 0x1C·r + 0x1B | u8 | RoomDef.music | §8.5 |
| 2ab2 | far* | sfxCopy | current effect copy |
| 2c1f | i16[9] | elf beh. 4 sounds | −1 = none |
| 3d8c | far* | scratch / speech pointer | |
| 4ac2/4ad2/4ad4 | | waveFmt / hWaveOut / waveHdr | §5.2 |
| 4af4 | char[130] | soundsDir | |
| 4b76 / 4b78 | i16 | extended / tuneIdx | |
| 4f62 | u32[] | timers | `10a8:002a/0066`. Intro uses timers 4 (60 s) and 7 (100 ms) |
| 5cc8 | u8 | msgBoxDepth | pause nesting |
| 5efd/5efe | u8/i16 | —/sceneCode | |
| 5f01 | u32 | g_hintUnseen | per player, +0x11 in the record |
| 5f35 | i16 | introMusicWatch | switch open1 → open2 |
| 6137 | i16 | speechInterrupted | puzzle read-aloud |
| 6145 / 6147 | i16 | voiceOnAtOpen / noChoiceSpeech | |

---

## 11. Re-implementation notes

* **Mixer model**: emulate a single "wave voice": `PlayWave(ssnd)` stops whatever is playing. Music is a
  separate MIDI stream. A modern engine may mix more, but must keep these observable rules: effects cut
  speech, speech cuts effects, a new effect cuts the previous one, and jingles before praise are waited
  for.
* **Completion**: replace `MM_WOM_DONE` polling with an "is voice playing" query. When audio output is
  unavailable, treat every sound as finished at once (this matches the original).
* **Blocking speech**: implement `PlaySpeechUntil(id, stopCodes)` as a cooperative wait that discards
  non-matching input, as `1088:01a9` does. Return "interrupted" and leave the interrupting event queued.
* **Settings**: two booleans (music+sfx, voice), saved per player. Ctrl+S toggles both. Pause the music
  and cut the wave voice on focus loss; resume the music only (from the saved position) on focus gain.
* **MIDI**: load `SOUNDS\E<name>.MID` for any General MIDI synth. Offer B only to emulate an OPL2
  "base-level" setup. Loop all tunes except open1. For a faithful intro, switch to open2 when open1 ends
  (using BOPEN1 reproduces the 54 s silence). Stop the music for elf questions and hints and restart it
  from the beginning afterwards.
* **Data-driven ids**: read `speechBase` (`1990`), `voiceBank` (`1c60`), the stop lists, the hint table
  (`13d0`), the intro tables (`141a/1422`), the tune tables (`09fa/0a0e`) and `RoomDef.music` from the
  user's EXE at run time, or re-derive them from the DLL contents. Do not ship copies.

---

## Open questions

1. The SSND header word at +0 is 0 in all shipped sounds. Did the Mac original use it, for example as a
   loop point or a compressed-format selector? It does not matter for this data set.
2. Exact `MIDIMAP.CFG` layout: is the u16 at offset 6 really the current-setup index, with 0x36-byte setup
   records from 0x12 whose name is the first 16 bytes? Only the offsets are observed. A real Windows 3.1
   `midimap.cfg` would confirm it.
3. What does the MCI sequencer default time format resolve to for these PPQN files (milliseconds or song
   pointer)? This decides whether the 16-bit `savedPos` truncation ever matters (only `castle`, 67.9 s,
   is long enough in milliseconds).
4. The run-time result of `Midi_Init` failing while `Midi_PlayTrack` still opens tracks by file name
   (§8.2): does music really play on systems without `midimap.cfg`, or when the sequencer is not routed to
   the MIDI Mapper? This needs a Windows 3.1 or 9x test.
5. sfx 4, 19, 26, 28, 32, 36, 38 and 39 have no trigger: none of the 30 `Sfx_Play` relocations pushes
   them, and no `DAM` script in TMT.DLL contains them (§7.2). The only data-driven paths into `Sfx_Play`
   are animation opcode 0xFC and the elf sound table `2c1f`. They are therefore unused in this build,
   unless some `DAM` script lives outside TMT.DLL's OTHR resources. No such script has been seen. Are
   they left over from the Mac or DOS version? The DOS export (`re/dos/ghidra_export`) may show whether
   the DOS build plays them.
6. Hints 1 and 33 (SSND 7101, 7133) are never queued. All 14 `QueueHint` call sites and their argument
   tables are known (§6.1). Hint 5 is reachable, which corrects an earlier draft of this document. What
   did hint 1 say? Its text is BNDL 327 entry 1. Was it dropped from the Windows build on purpose?
7. Voice banks 1/2/3: confirm by listening that each bank is a different speaker and matches that
   puzzle's narration voice. The pitch estimate is only weak evidence.
8. Meaning of the effects marked [I] in §7.2: none has been confirmed by listening. The WAVs are in
   `extracted\previews\wav\TM0SOUND.DLL\`.
9. What sets the intro animation slots `5f27`/`5f33` and the `0862` dirty-rectangle flag during
   narration? This belongs to the animation (10b8) analysis. Narration timing does not depend on it
   except for the "animation finished → next page" rule.
10. Is the `1088:01a9` filter `(e & 0x0FFF) == k` meant to ignore mouse button bits? It also maps
    0x2409 to 0x409 and 0x8A70 to 0xA70. This report assumes so; it affects which right-clicks interrupt
    speech.
