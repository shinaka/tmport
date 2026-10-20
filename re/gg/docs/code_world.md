# Gizmos & Gadgets (ssg.exe): the world — states, hub, buildings, rooms, actors, player, chimps

Wave 1 logic spec, topic "world". It specifies, well enough to implement without re-reading the
binary:

* the main `Seq_Run` table and the states that are not owned by other topic docs: the Technology
  Center hub (state 3), building set-up and resume (4), entering a room (5), the warehouse loop
  (7), leaving a room (8), save-and-leave (16), the vehicle-part list (10) and the race dispatch
  (11) as far as they touch the world;
* the room data model and loader (segment `1622`), room drawing and layer order, items lying in
  rooms, doors and their links;
* the actor system (`1834`): 12 slots of 0xA0 bytes, actor sets and sequences, the step
  interpreter, tick and draw order; the screen-animation module (`174b`) and the list helpers
  (`19c0`);
* the player controller and the projectile (`18f5`), collision and contacts (`1adb`), the parts
  catalogue helpers the world uses (`153f`);
* the Cyber Chimps (overlay 0, `2a80`, and the DGROUP behaviour tables);
* the HUD strip (`1a04`) as far as it is not workshop-specific;
* every `rand()` call in this area and their order within a frame.

Hand-offs: the attract loop and sign-in (states 1, 2) are in `code_ui.md`; the workshop (state 6),
the races and results (11..15) in `code_vehicles_races.md`; the puzzles (state 9) in
`code_puzzles_*.md`; the sound driver and banks in `code_sound.md`; the platform layer (timers,
input fetch, zones, graphics primitives, palette) in `code_runtime.md`; the parts model and the
full HUD in `code_vehicles_races.md`. Formats already specified in
`fmt_data.md`, `fmt_graphics.md` and `fmt_sound.md` are referenced, not repeated, except where this
pass corrects or completes them (§5, §9.2, §19.1).

Evidence: the named Ghidra export `re/gg/ghidra_export/` (disassembly for every claim about
arguments, widths and signedness; decompiled C only as a reading aid), the bytes of `ssg.exe`
and the archives, the wave 0 save states `extracted/captures/gg/savestates/w0/` (read with the
DGROUP-signature method of `tools/gg/mouse_tsr.py`). Scratch scripts (not in the repository):
`extracted/scratch/gg/wave1/world/` — `fn.py` (disassembly of one function), `exe.py` (bytes by
`SSSS:OOOO` / `DS:XXXX`), `rooms.py` (room-set parser), `actset.py` (actor-set parser), `st.py`
(actors and globals of a save state).

Names: the canonical names of `re/names/gg/`. This pass's 159 names, and the platform and
vehicles passes' names for the engine segments (1399, 1473, 1581, 15df, 17ab, 198d, 1bb4, 1cdd)
and a few shared labels, were merged there after wave 1 (the renames of older names are listed in
`re/names/gg/README.md`). The address is always given.

Notation: **[O]** observed in code, bytes, decoded data or a save state; **[I]** inferred.
`SSSS:OOOO` = Ghidra address (`re/CONTEXT.md`, G&G section). `DS:XXXX` = DGROUP `1ea7:XXXX`.
Numbers are hex unless written as decimal. Words are 16-bit little-endian; "s" = signed compare,
"u" = unsigned. "Frame" = one pass of a 4-tick loop (4 PIT ticks = 27.46 ms, 36.41 frames/s).
`R(n)` = `rand() * n / 0x8000` in 32-bit signed arithmetic (Borland `random(n)`; ARCHITECTURE
§8). "Hall" = a 6-floor room drawn at the small scale; "small room" = a 3-floor room at the large
scale (§5.1).

---

## 1. Module overview

| Seg | Role | Functions in the segment |
|---|---|---|
| `12e0` | main, `Seq_Run`, states 3, 4, 5, 7, 8, 10, 11, 16, 17, door opening, score helpers, music cue callbacks | 19 |
| `153f` | parts catalogue and part lists (load, deck, types, holders, save/restore) | 16 |
| `1581` (part) | the frame's sprite list (`Sprites_*`) | 4 |
| `1622` | rooms: load, enter, leave, draw layers, animated objects, items, music, save/restore | 30 |
| `174b` | screen animations of static screens (`ScreenAnim_*`; hub, workshop, attract, results) | 12 |
| `1834` | actor system: sets, sequences, step interpreter, tick, draw, parts carried, save/restore | 14 |
| `18f5` | player and projectile callbacks, actor initialisation | 12 |
| `19c0` | doubly linked lists with far pointers | 13 |
| `1a04` | HUD strip | 27 |
| `1adb` | rectangle overlap, wall collision, contacts | 4 |
| `2a80` (ovl00) | Cyber Chimps | 10 |

Everything runs on the main thread, inside the state functions; nothing in this area runs in an
interrupt [O: no caller from 1454/17a0].

---

## 2. `Seq_Run` and the main table (hand-off map)

`Seq_Run(table, &state)` (12e0:000f) [O]: `prev = -1`; loop: `f = table[state]` (far pointers,
4 bytes each); a NULL entry ends the run (returns 0). `r = f()`:

| r | effect |
|---|---|
| −2 | `state = prev` (`prev` unchanged; one step of history) |
| −1 | stop; `Seq_Run` returns −1 |
| 0 | `prev = state; state += 1` |
| n > 0 | `prev = state; state = n` |

A −2 before any change would index `table[−1]` (DS:00be) [O, not reached].

Main table DS:00c2 (index 1..17, 18 = NULL), started by `main` with DS:455c = 1. This doc owns:

| # | Function | Role | Returns |
|---|---|---|---|
| 3 | `State_03_Hub` 12e0:0148 | Technology Center (§3) | 0 (→ 4) after a building was chosen, 2 for the sign-in zone; Esc exits the program |
| 4 | `State_04_BuildingSetup` 12e0:0820 | set-up or resume (§4) | 5 |
| 5 | `State_05_EnterRoom` 12e0:096a | §15.1 | 6 (room 0) or 7 |
| 7 | `State_07_Warehouse` 12e0:0619 | §14 | 8 (leave the room) or 16 |
| 8 | `State_08_LeaveRoom` 12e0:09aa | §15.2 | 9 (puzzle door), 10 (vehicle finished) or 5 |
| 10 | `State_10_ListVehicleParts` 12e0:0506 | §15.3 | 0xb |
| 11 | `State_11_Race` 12e0:0564 | §15.3 | 0xc, or 0xe after a rematch |
| 16 | `State_16_SaveAndLeave` 12e0:012f | §15.4 | DS:455a (2 or 3) |
| 17 | `State_17_End` 12e0:0141 | returns 0 → NULL → end | 0 |

Hand-offs: 1 attract and 2 sign-in → `code_ui.md`; 6 workshop, 12..15 results and movie →
`code_vehicles_races.md`; 9 puzzle → `code_puzzles_*.md`. The puzzle state calls
`Room_OpenPuzzleDoor(solved)` (12e0:0a24, §6.6) before it returns 5 or 16 (38e2:0084..00ce).

State 11 returns 0xe (state 14, redraw the result) when `g_rematch` DS:455e ≠ 0, else 0xc [O
12e0:05c4..05d3] (ARCHITECTURE §2.2 had only 0xc before wave 1).

---

## 3. The Technology Center hub (state 3, 12e0:0148)

### 3.1 Set-up, in order [O]

1. Music: if `g_titleMusicBank` DS:0098 = −1: `DS:0098 = SndRes_LoadMusicBank(0x1020)` and
   `SndRes_PlayMusic(bank, 0, 1)` (start the bank). Then `SndRes_SelectArrangement(bank, 2)` on
   every entry. The bank stays loaded until state 4 frees it (12e0:0826..0837).
2. `g_inBuilding` DS:0094 = 0, play mode DS:0096 = 0.
3. `Screen_LoadPicture(0x1002, 0x1004)` (picture into the shared buffer, palette range 0x1004
   into the shadow), `Lpx_Draw` at (0,0); `Pal_LoadRes(0xf6)`; `Screen_SaveAll` 1473:034e
   (rows 0..199 → back page).
4. Screen animations: `ScreenAnim_Load(0x1005)` and `(0x1007)`, both started
   (`ScreenAnim_Start`; §19). In the shipped data each is one looping 3-frame sequence at a fixed
   place: 0x1005 at (265,10) with delay 3 (a frame every 4 frames = 16 ticks = 109.9 ms, the
   windmill step measured in REFERENCE_CAPTURES §4) and 0x1007 at (283,45) with delay 4 (every 5
   frames) [O data].
5. **Trophies** (drawn after step 3, so they are on the screen only, not in the back page):
   `Res_Read(0x1003)` (a bundle, shared buffer). Member 0 = u16 x table, member 1 = u16 y table,
   member 2 + t = trophy picture t, t = building·5 + k. For each building b and
   k = 0..`g_racesWon[b]` (DS:4596[b], −1..4; so racesWon + 1 trophies):
   `Gfx_RecolorPicture(picture t, "STUVW", to)` with to[i] = 0x20 + `g_vehicleColor[t]`·5 + i
   (DS:5c10 bytes, record +0x16AE, set by the workshop at 2dee:055d / 11fa), then
   `Lpx_DrawKeyed(x[t], y[t], picture t, mode 0)` [O 12e0:022e..02ee]. The source colours are
   the bytes 0x53..0x57.
6. Cursor labels: `Res_LoadCopy(0x101f)` (bundle of 4 cursor shapes); `Mouse_LoadCursors` of
   members 0..3 into a local table. Hover list: 8 local entries of 0x1C bytes `{list node 8, copy
   of the first 16 bytes of zone i, far cursor}` with cursor = shape[(release code of zone i) −
   0xbb8]; `MouseRegion_AddArray` (17ab:0771) inserts them at the head of the hover list DS:0a8c.
   Zones: `Zones_AddLocalArray(8, DS:0132)` (17ab:02de) links the 8 static zone records into the
   local list (§3.2). `Sprites_Reset`.
7. `complete = (g_racesWonTotal DS:5c0e == 15)`.

### 3.2 Zones DS:0132 [O bytes]

Eight 0x16-byte zone records (format: ARCHITECTURE §4.5), all rectangles, priority 5, press code
0, release codes only:

| Code | Meaning | Zones |
|---|---|---|
| 0xbb8 | sign-in booth → return 2 | 1 |
| 0xbb9 | building 0 (Automotive) | 4 |
| 0xbba | building 1 (Aircraft) | 1 |
| 0xbbb | building 2 (Alternative Energy) | 2 |

(0xbbc is in the switch but no zone uses it.) The hover cursor of a zone is cursor shape
`code − 0xbb8` (the text labels seen in the captures) [O code, O capture: the left building
showed the Aircraft label].

### 3.3 Frame loop [O 12e0:03bf..0479]

```
Timer_Set(0, 4)
ScreenAnim_Frame()                          # erase, step, draw the two animations
first frame: Screen_FadeOutIn() else Dirty_Flush()
Timer_Wait(0)
if complete: Dlg_MessageRes(0x106d); complete = 0      # once, after the first frame
code = Input_GetCode()
switch code (table CS:04ee, 6 codes):
  0x1b, 0xbbc -> Cmd_SaveAndExit (4106:00b2)          # saves and exits the program
  0xbb8       -> leave with result 2
  0xbb9/0xbba/0xbbb -> building b: if g_racesWon[b] == 4: Dlg_MessageRes(0x106e)
                       else g_building DS:4592 = b; leave with result 0
```

Leaving: `MouseRegion_Clear` (17ab:07a4: removes all hover entries but the last, the default arrow),
`Mouse_SetCursorPicture(DS:7a82)`, frees the 4 cursors (`Mouse_FreeCursor`), the 0x101f copy, both
screen animations (`ScreenAnim_Free`), clears the local zones (`Zones_ClearLocal`), `Pal_FadeOut`,
returns the result. No `rand()` call [O; the w0 state `06_hub` still has the seed in DS:442a].

---

## 4. Building set-up and resume (state 4, 12e0:0820)

### 4.1 Order of operations [O]

```
if g_titleMusicBank != -1: SndRes_FreeMusicBank(it), DS:0098 = -1
g_level  DS:4594 = g_racesWon[g_building] + 1          # 0..4
g_raceIndex DS:4590 = g_building*5 + g_level             # 0..14
Parts_LoadCatalogue()          # 153f:0002, resource 0x2f5 + race (§4.3)
Rooms_Load()                   # 1622:0085 (§6.1) - rand: deck + item grids
Hud_DrawAll(7)                 # 1a04:02c2: bar, 3 labels, rows 165..199 saved to the back page
if g_buildingInProgress[b] (DS:459c[b]):                  # resume, no prompt
    Actors_LoadPlayerSets(); Chimps_SetupActors()
    memcpy(DS:457c, block, 0x14)                          # block = DS:47e0 + b*0x6ba
    Parts_RestoreState(block + 0x14)
    Rooms_RestoreState(block + 0x26c, block + 0x302)
    Actors_RestoreState(block + 0x492)
    g_inBuilding DS:0094 = 1
else:                                                     # new visit
    Building_NewVisit()                                   # 18f5:094d
    g_partsNeeded DS:4582 = Parts_NeededCount()           # level description +0x06
    if g_level == 0: DS:458a = 0 (hint 0x1046 may fire again); g_projectiles DS:457e = 10
    g_setupCoin DS:458e = (R(100) < 50)                   # written only
    Chimps_SetupActors()                                  # 2a80:0635
    DS:457c = 0; DS:4586 = 0; DS:4588 = -1; DS:4584 = 0   # workshop live state
    g_tubeRoom DS:4580 = g_tubeRoomByLevel[g_level]       # DS:00b2: 1,2,2,3,3
    Chimps_Spawn()                                        # 2a80:0784 - rand
return 5
```

**Bananas are not refilled between the races of a building**: `g_projectiles` is set to 10 only
for level 0 [O 12e0:08f8..0905]: 10 per building, not per visit.

### 4.2 What is reset, what is restored

| Data | New visit | Resume |
|---|---|---|
| level, race index | recomputed | recomputed |
| part catalogue (holders +0x0c = −1, flags +0x36 = 0, pairs +0x1c = −1 in the file) | from the resource | from the resource, then holder and +0x36 per part from the block (re-adds them to the carried lists) |
| rooms: object types, door art, items | from the resource; items dealt anew (rand) | from the resource and dealt (rand calls happen and are discarded), then door types (7/9) and every item list from the block |
| actors +0x14..+0x41 (§9.1) | player and projectile: `Building_NewVisit` resets floor, room 0, immunity, contacts, carried count, list; chimps: `Chimps_Spawn` | from the block (12 × 0x2E bytes), then scale, delay and direction from the current sequence |
| actor fields outside +0x14..+0x41 (+0x70 carried count, +0x94, ...) | reset as above | **not reset** (§20 bug 7) |
| 20 bytes DS:457c..458f (projectiles, tube room, parts needed, workshop and hint state) | set as in §4.1 | from the block |
| `g_cratesLeft` DS:7a00 | I[0] − part count | crates of the **last** room in the block only (§20 bug 6) |
| `g_cratesTotal` DS:7a02 | I[0] − part count | same |

The block layout is `fmt_data.md` §8.4; its contents per this pass: +0x000 the 20 live bytes,
+0x014 per part `{i16 holder, u16 flags +0x36}` (`Parts_SaveState` 153f:0385), +0x26C one byte per
door object of type 7 or 9 in room order (7 or 9; `Rooms_SaveState` 1622:0fb3), +0x302 per room
present `{u16 n, n × {i16 x, i16 floor, i16 kind}}` (§7.5), +0x492 12 × 0x2E bytes of actors.
`Players_SaveCurrent` (2b5a:01ea) writes the block only while `g_inBuilding` is set, sets
DS:459c[b] and clears `g_inBuilding` [O 2b5a:01fa..026f].

`g_inBuilding` is set by state 4's resume path (12e0:08e3), by the workshop on entry
(2dee:1aab; every new visit starts in the workshop) and by state 8 when the vehicle is done
(12e0:0a14); cleared by the hub (12e0:0190), the sign-in (2caa:124c) and the save [O].

### 4.3 Level description (resource 0x2f5 + race) — additions to `fmt_data.md` §7.1 [O]

| Off | Meaning (this pass) |
|---|---|
| +0x02 | n part records |
| +0x06 | k = number of part **types** the vehicle needs = the initial `g_partsNeeded`; also the length of the word table at +0x2D |
| +0x2D | k u16, a category list that nothing reads (its pointer +0x25 is written at load and never used; in race 2 the words do not even match the parts' categories, `code_vehicles_races.md` §3.1) [O 153f:0028..0036] |

Part record fields used by the world [O]: +0x08 type, +0x0C holder (−1 none, 0 player, 2..11 a
chimp), +0x0E needed flag (non-zero: one of the needed types), +0x10 picture index in the parts
sprite bundle, +0x16 text id, +0x1A set to 0x2238 when carried (not read here), +0x1C pair link
(−1, or the index of the other part of the same type: the second-taken part holds the first's
index, the first holds `second | 0x8000`), +0x36 flags (0x4000 = already scored, 0x8000 = built
into the vehicle and cannot be stolen or trashed), +0x3A..+0x47 the HUD strip sprite entry,
+0x48..+0x5D a click zone (release code 5000 + index, priority 4), +0x82 picture, +0x86 text copy,
+0x8A cleared when carried, +0x8C own index. The record is itself the list node (+0..+7) of a
carried list. In every shipped catalogue each needed type has 4 parts [O data].

### 4.4 rand() at set-up (checked against a capture)

```
Parts_ShuffleDeck(I[0])                 Rand_Shuffle: 20 * I[0] calls
for room r = 1 .. roomCount-1 with I[r] != 0:
    Room_PlaceItems(room r, I[r], -1)   Rand_Shuffle(cells): 20 * 90 (hall) or 20 * 21 (small)
R(100)                                  g_setupCoin (new visit only)
Chimps_Spawn: per chimp k in list order: R(6) (hall) or R(3) (small room), then R(100)
```

[O] For the w0 capture (Aircraft, level 0, race 5: I[0] = 22, items in room 1 (hall) and rooms
4..7 (small), 2 chimps) this predicts 440 + 1800 + 4·420 + 1 + 4 = 3925 calls; the rand state
DS:442a goes from the seed 0x29CF (`w0/06_hub`) to 0xAD7A0B08 (`w0/09_blueprint`) in exactly
3925 LCG steps, and replaying the last 5 calls gives the first chimp's room 1, floor 4, y 119,
x 300 as stored in the state [O].

### 4.5 Global state used by the world (DGROUP) [O]

| DS | Name | Type | Meaning |
|---|---|---|---|
| 0094 | `g_inBuilding` | i16 | a building is in progress (the save writes its block) |
| 0096 | `g_kbdPlayMode` | i16 | 1 in the rooms (held keys swallowed) |
| 00bc | `g_puzzleDoorHit` | i16 | set by `Player_SequenceEnd` at a locked door → state 9 |
| 00be | `g_vehicleDone` | i16 | set by the workshop → state 10 |
| 455a | `g_leaveRequest` | i16 | 2 sign-in, 3 hub (state 16) |
| 4572 | `g_score` | u32 | +20 per part (§11.6) |
| 4578 | `g_withChimps` | i16 | 0 = Without Chimps |
| 457c..458f | live state | 20 bytes | saved per building: 457c/4584/4586/4588 workshop, 457e bananas, 4580 tube room, 4582 parts needed, 458a/458c hint cancels, 458e set-up coin |
| 4590 | `g_raceIndex` | i16 | building·5 + level |
| 4592 | `g_building` | i16 | chosen in the hub |
| 4594 | `g_level` | i16 | races won in the building + 1 |
| 4596 | `g_racesWon` | i16[3] | −1..4 |
| 459c | `g_buildingInProgress` | i16[3] | a saved block exists (resume) |
| 5c0e | `g_racesWonTotal` | i16 | 15 = everything won |
| 2450 | `g_cpuType` | i16 | 2 = 286 (chimp halving) |
| 132c | `g_hudDirty` | u16 | HUD redraw bits (§17; `code_vehicles_races.md` §4.2) |
| 1326 | — | i16 | part attached to the cursor, −1 none (HUD) |
| 8656 | `g_escHandler` | far | Esc/menu hook, `Hud_EscHook` in the warehouse |
| 7a8a | `g_actors` | 12 × 0xA0 | §9.1 |
| 0a9a, 0a9c | `g_playerBlink`, `g_actorSpritesLoaded` | i16 | §9.5, §9.6 |
| 0a9e | `g_playerSets` | far[4] | cached player/projectile sets |
| 9f1e, 9f22 | `g_chimpSetRoom`, `g_chimpSetHall` | far | cached chimp sets |
| 6638, 663c | `g_levelDesc`, `g_parts` | far | level description and part records |
| 659c, 6614 | `g_partDeck`, `g_partDealIndex` | i16[], i16 | §7.2 |
| 6fa8 | `g_itemPool` | 60 × 0x2C | §7.1 |
| 7a00, 7a02 | `g_cratesLeft`, `g_cratesTotal` | i16 | §7.6 |
| a03c | `g_chimpParts` | far | §13.2 |
| 1314, 8554, 5c1f | `g_lastKeyHeldTick`, `g_idleReported`, `g_keyHeldSeen` | | §11.1 |

Room state: §5.7. Constant tables: §22.

---

## 5. Room data model

### 5.1 Room sets (`A` 0x347..0x34f) [O loader 1622:0085, O data]

`g_roomSetIds` DS:09e8[race] selects the set: per building 8, 12, 12, 16, 16 rooms for levels
0..4 (three sets per building). Format: `u16 count`, then per room a 0x120-byte header followed
by its data, in this order:

1. static objects, 0x20 bytes each, **layer 3 first, then 2, 1, 0** (count of layer L = header
   word L+1);
2. animated objects, 0x48 bytes each, lists 0..3 (count of list L = header word 5+L);
3. 3 groups × 6 lists of 10-byte records, group g list k (counts: header words 9+k, 0x1B+k,
   0x2D+k).

Room 0 is always the workshop (one type-3 background). In the shipped sets: rooms 1..3 are halls
(header bit 0; one hall for level 0, two for levels 1-2, three for levels 3-4; unused hall slots
are empty rooms), rooms 4.. are small rooms. **Every object is in layer 0 and every animated
object in list 0, only in halls** [O all 108 rooms]; layers 1..3 and lists 1..3 are supported by
the code and empty. The parser `rooms.py` consumes every set exactly.

### 5.2 Room header (0x120 bytes) [O]

| Off | Size | Meaning |
|---|---|---|
| +0x00 | u16 | flags: bit 0 = hall (6 floors 26 px apart, small sprites, "scale 1"); bit 1 = small room attached to hall floors 3..5 (§5.6) |
| +0x02 | 4 × u16 | static object counts, layers 0..3 |
| +0x0A | 4 × u16 | animated object counts, lists 0..3 |
| +0x12 | 6 × u16 | group 0 (walls) counts per floor |
| +0x1E | 6 × far | group 0 lists (patched) |
| +0x36 | 6 × u16 | group 1 (left jump spots) counts per floor |
| +0x42 | 6 × far | group 1 lists |
| +0x5A | 6 × u16 | group 2 (right jump spots) counts per floor |
| +0x66 | 6 × far | group 2 lists |
| +0x7E | 4 × far | static object arrays, layers 0..3 (patched only for non-zero counts; else the file's bytes, 0 in all shipped rooms) |
| +0x8E | 4 × far | animated object arrays |
| +0x9E | u16 | items currently in the room (file: 0) |
| +0xA0 | far | item list (`List_New`, rooms ≥ 1 only; NULL for room 0) |
| +0xA4 | 6 × u16 | per floor: number of floor references |
| +0xB0 | 6 × u16 | per floor: first index into the reference array |
| +0xBC | 50 × u16 | floor references (0xFFFF fill): bit 15 = skip; bit 14 = animated object, else static; bits 0..1 = layer/list; `(ref & 0x3fff) >> 2` = index |

The far-pointer slots hold editor leftovers in the file and are overwritten at load.

### 5.3 Static objects (0x20 bytes) [O]

| Off | Meaning |
|---|---|
| +0x00 | flags: bits 0..1 blit mode (2 = mirrored); 0x04 and 0x10 = not drawn; 0x40 = hide request (§8.3); 0x80 = not erased; 0x1000 = y offset by +0x70 in `Obj_DrawKeyed` (never set: +0x70 lies outside the object) |
| +0x02..+0x08 | x, y, w, h (sprite entry) |
| +0x0A | far picture (filled by `Room_Enter`) |
| +0x0E | picture resource id |
| +0x10 | type |
| +0x12 | link: doors and passages `(link id << 6) \| target room` (target = `& 0xF`); type 6: the target room as a whole word; type 4: 0/1 (not read here) |
| +0x14 | i16 (not read by the world code; −2548 on poles) |
| +0x16..+0x1F | hit record `{u16 kind, x, y, w, h}` |

Types and hit kinds in the shipped data (the code tests the kind, the type only at a sequence's
end) [O counts and kinds; the "What" column is I, from the pictures and the sequences that react]:

| Type | Kind | What | Count |
|---|---|---|---|
| 3 | 0 | background picture (drawn into the back page at room entry) | 1255 |
| 4 | 1 | floor edge at a gap in a hall | 182 |
| 5 | 1 | floor edge at a gap in a small room | 74 |
| 6 | 2 | hole into another room (`+0x12` = target) | 14 |
| 7 | 8 | locked puzzle door | 710 |
| 8 | 0x40 | the workshop tube (link 100, target room 0) | 18 |
| 9 | 4 | open door | 18 |
| 10 | 0x10 | passage through the left wall | 77 |
| 11 | 0x20 | passage through the right wall | 77 |
| 12, 13 | 0 | left / right wall decoration | 247 + 247 |
| 16 | 0 | pole | 180 |
| 18 | 0 | tube segment | 90 |

### 5.4 Animated objects (0x48 bytes) [O 1622:0706, data]

| Off | Meaning |
|---|---|
| +0x00..+0x0D | sprite entry (flags 0x04 = paused, 0x10 = inactive, bits 0..1 mirror) |
| +0x0E | sprite bundle id |
| +0x12 | frame countdown; +0x14 its reload |
| +0x16 | script position |
| +0x1A | picture slots, one u16 per bundle picture (`PicCache_LoadBundle`) |
| +0x26 | script: 12 u16 words, ≥ 0 = frame, < 0 = command (§8.4); `Room_DrawAnimLayer` steps them in place, ScreenAnim_Step-style [O wave 2] |
| +0x3E..+0x47 | hit record `{kind, x, y, w, h}` |

Kinds in the data: 0x80 (bundle 0x340, 45 lift pads), 0x100 (0x345, 25 launch pads, paused),
0x200 / 0x400 (0x346, 12 + 14 flaps, paused, some mirrored) [O; roles I from the player and chimp
sequences that react to them, §11.3, §13.4].

### 5.5 The 10-byte record lists [O]

* **Group 0, walls** (per floor): `{u16 flags, x, y, w, h}`; the flags are not read
  (`Actor_CollideWalls` uses x..h) [O 1adb:0093].
* **Groups 1 and 2, jump spots** (per floor): `{u16 trigger sequence, x, u16 replacement
  sequence, w, u16 pad index}`. Group 1 serves the player's sequences 12..15, group 2 sequences
  16, 17 (§11.3).

### 5.6 Floors

An actor's floor (+0x1C) is always counted in **hall floors 0..5** (0 = top). In a small room
the code uses `floor − 3` when floor ≥ 3 (1adb:00fd, 0228, 03f8; 1622:0df0); small rooms with
flag bit 1 are reached from hall floors 3..5 and map them to their floors 0..2, the others map
hall floors 0..2 directly [O code, I design; the doors' link ids agree with this mapping in every
set]. Floor heights: hall rows at y = 9 + 26k, small rooms y = 9 + 52k (the type-3 strips).

### 5.7 Run-time room state [O]

| DS | Name | Meaning |
|---|---|---|
| 7a04 | `g_roomTable` | 16 far pointers to the headers inside the room-set copy (zeroed first) |
| 79f8 | `g_roomSet` | the room-set copy (`Res_LoadCopy`) |
| 79fc | `g_roomItemCounts` | copy of `I` 0x350 + race (16 words: word 0 = items of the level, word r = items of room r) |
| 08c4 | `g_roomCount` | rooms in the set |
| 08c2 | `g_displayRoom` | room on screen (set by `Room_Enter`) |
| 08c6 | `g_room` | copy of the displayed room's header (0x120 bytes); the code reads its counts and pointers through DS:08c8 (+2), 08d0 (+0xA), 08fc (+0x36), 0908 (+0x42), 0920 (+0x5A), 092c (+0x66), 0944 (+0x7E), 0954 (+0x8E), 0964 (+0x9E, item count, kept in step with the table entry) |
| 08c0 | `g_roomLoaded` | pictures of the displayed room are loaded |
| 09e6 | `g_roomMusicBank` | music bank 0x35f + 2·building (−1 none) |
| 08be | `g_roomMusicStep` | 0..5 |
| a01c | `g_roomChimpCount` | 16 words: chimps counted per room (music; §13.8) |
| 7aa4 | `g_playerRoom` | = actor 0 +0x1A (the label `g_roomIndex` was misleading) |

---

## 6. Loading, entering and leaving rooms

### 6.1 `Rooms_Load` (1622:0085) [O]

1. `g_roomMusicBank = SndRes_LoadMusicBank(0x35f + 2·g_building)`; `g_roomMusicStep = 0`.
2. `g_roomSet = Res_LoadCopy(g_roomSetIds[race])`; `g_roomCount` = its word 0.
3. `ItemPool_Reset()` (all 60 item nodes free).
4. `g_roomItemCounts = Res_LoadCopy(0x350 + race)`; `Parts_ShuffleDeck(I[0])` (§7.2).
5. `g_cratesLeft = g_cratesTotal = I[0] − (catalogue +0x02)`.
6. `memset(g_roomTable, 0, 0x40)`; for each room r: `g_roomTable[r]` = header; patch the
   pointers (§5.1 order); for r ≥ 1: item list = `List_New()`, `+0x9E = 0`, and if `I[r] ≠ 0`:
   `Room_PlaceItems(room r, I[r], −1)` (§7.3).
7. `g_displayRoom = 0`.

### 6.2 `Room_Enter(room)` (1622:0411) [O]

```
g_room = copy of the room's header (0x120 bytes); g_displayRoom = room
Screen_ClearPlayArea()              # colour 2 over x 0..319, y 9..163
music: room 0 -> SndRes_StopMusic(bank)
       rooms 1..3 -> RoomMusic_SelectLayers(room); RoomMusic_Start()
       rooms > 3  -> RoomMusic_SelectLayers(4);   RoomMusic_Start()
g_roomLoaded = 1
for L = 3 down to 0:
    for each static object of layer L:
        type 3: picture = Pic_Read(+0x0E) (shared buffer, not cached); Obj_DrawKeyed(object)
        else:   picture = PicCache_Load(+0x0E)
    for each animated object of list L:
        PicCache_LoadBundle(+0x0E, slots +0x1A); picture = cache[slot[0]]
for each static object of layer 0 with type != 3:
    Obj_DrawKeyed(object); PicCache_Release(+0x0E)        # baked into the background
for each item of the room (count g_room+0x9E): picture = PicCache_Load(item +0x1A)
Screen_SavePlayArea()               # rows 0..164 -> back page
```

So with the shipped data the whole room (backgrounds, walls, doors, edges, tubes) is one static
picture in the back page; only animated objects, items and actors are drawn per frame.

`Gfx_SaveToBack` (1399:02a2) and `Screen_SavePlayArea` (1473:0337), named `Gfx_CopyFromBack` /
`Screen_RestorePlayArea` in wave 0, copy the screen **into** the back page (destination ES:DI =
row y+200, source DS:SI = row y) [O 1399:02af..02df]. `Gfx_RestoreRect` (1399:0233) is
the reverse (back → screen, with a left clip at x < 0).

### 6.3 Room music [O 1622:0346, 03b6, 12e0:007f]

* `RoomMusic_SelectLayers(set)`: `SndRes_SelectArrangement(bank, set)` (the channel list `set`
  of the bank, `fmt_sound.md`), mutes channels 4 and 5 (`SndRes_MuteChannel`), and if
  `g_roomChimpCount[g_displayRoom] ≠ 0` unmutes channel 4 (levels 0-2) or 5 (levels 3-4)
  (`SndRes_UnmuteChannel`): the chimp voice.
* `RoomMusic_Start()`: only when the bank is not playing (`SndRes_IsPlaying`): start it
  (`SndRes_PlayMusic(bank, 0, 1)`), `g_roomMusicStep = 0`, `Snd_SetTranspose(handle, 0, v)` with v
  = the low byte at `DS:010e + building·6` (**bug**: the cue callback indexes `building·12`, so
  building 1 starts with entry 3 of building 0's row; buildings 0 and 2 read the right entry), and
  `Snd_SetCue(handle, g_roomMusicCueTicks[building] (DS:0a06), 12e0:007f)`.
* `Music_RoomCue` 12e0:007f (the cue): `g_roomMusicStep = (g_roomMusicStep + 1) % 6`;
  `Snd_SetTranspose(handle, 0, low byte of DS:010e[building·12 + g_roomMusicStep·2])`. The table
  `g_roomMusicTransposes` DS:010e is 3 × 6 words [O]; `Snd_SetTranspose` is specified in
  `code_sound.md`.
* The music keeps playing across rooms; entering the workshop stops it; leaving to a puzzle stops
  it (state 8).

### 6.4 `Room_Leave` (1622:05f2) and `Rooms_Free` (1622:02dd) [O]

`Room_Leave`, when `g_roomLoaded`: for layers 0..3: `PicCache_ReleaseBundle` of every animated
object's bundle and `PicCache_Release` of every static object's picture id (type-3 pictures were
never cached and the layer-0 pictures were already released at entry, so those releases find
nothing and do nothing [O 15df:0359 acts only on a cached id]); then the items' pictures;
`g_roomLoaded = 0`. `Rooms_Free`: `List_Free` of the item lists of rooms
1..count−1, frees `g_roomItemCounts`, `g_roomSet`, and the music bank (`SndRes_FreeMusicBank`).

### 6.5 Doors and links

* A door, passage or tube object carries `(link << 6) | target` in +0x12. Its twin in the target
  room has the same link id; `Room_FindLinkedDoor(room, link)` (1622:087f) returns the first
  object of type 7..11 with `(+0x12 >> 6) == (link >> 6)` (room < 0 means the copy `g_room`); if
  none matches it returns the address just past the last object [O, bug, unreachable with the
  data].
* Door art: open doors use picture 0x33c in halls and 0x30e in small rooms (1622:10e7,
  12e0:0a54..0a6a).

### 6.6 Opening a puzzle door — `Room_OpenPuzzleDoor(solved)` (12e0:0a24) [O]

Called by the puzzle state with the solved flag DS:b13c. If solved and the object
`g_actors[0].+0x36` in the player's room is of type 7: picture = hall ? 0x33c : 0x30e, type = 9,
kind (+0x16) = 4; target = `+0x12 & 0xF`; if DS:b144 ≠ 0 (set by the puzzle modules, e.g.
3083:0aaf) `g_playerRoom = target`; its twin (`Room_FindLinkedDoor(target, +0x12)`) gets the
target room's picture, type 9 and kind 4. The player was left with pending sequence 33 (§11.5),
so at state 5 he steps out of the door in the target room (or, with DS:b144 = 0, out of the same
door).

---

## 7. Items lying in rooms (parts and banana crates)

### 7.1 Item nodes [O 1622:0071, 0a61, 0a76, 0df0]

60 nodes of 0x2C bytes at `g_itemPool` DS:6fa8 (+8 = 1 free); `ItemPool_Alloc` returns the first
free node without a bounds check.

| Off | Meaning |
|---|---|
| +0x00 / +0x04 | next / prev (far) |
| +0x08 | 1 = free (also the "already taken this frame" test of the pick-up callbacks) |
| +0x0C..+0x19 | sprite entry `{flags 0, x, y, w, h, far picture}` (also the contact rectangle) |
| +0x1A | picture id: 0x332 (small room) / 0x343 (hall), +1 when the part is flagged 0x4000 |
| +0x1E | kind: part index, or 1000 = banana crate |
| +0x20 | 0 |
| +0x22..+0x2B | `{0x800, x, y, w, h}` (not read by the world code) |

Parts and crates look the same; only an already-scored part shows the +1 picture [O ids].

### 7.2 The deck [O 153f:01b6, 0203]

`Parts_ShuffleDeck(total)`: `Rand_Shuffle(total, g_partDeck DS:659c)` (entries 0..total−1,
then 10·total swaps of `R(total)`, `R(total)`, 2 calls each), every entry ≥ part count becomes
1000; `g_partDealIndex` DS:6614 = 0. `Parts_DealNext()` returns the next entry.

### 7.3 `Room_PlaceItems(room, n, kind)` (1622:0a76) [O]

| | Hall (flags bit 0) | Small room |
|---|---|---|
| grid rows × cols | 6 × 15 | 3 × 7 |
| cell (x, y) of index c | (10 + 20·(c % 15), 26 + 26·(c / 15)) | (15 + 40·(c % 7), 46 + 52·(c / 7)) |
| probe w × h | 20 × 7 | 40 × 13 |
| item w × h, picture | 10 × 7, 0x343 | 20 × 13, 0x332 |

```
cells = rows*cols; Rand_Shuffle(cells, local order)        # 20*cells rand calls, once per call
k = 0
repeat n times:
    node = ItemPool_Alloc()
    loop: c = order[k]; k += 1
          if k >= cells: return 0                          # the last cell is never tried (bug)
          probe = {0, cell x, cell y, cell w, cell h}
          free if it overlaps none of: the room's items (+0x0C), the layer-0 static objects
          with type >= 4 (their x..h), the animated objects of list 0
    x = cell x + cellw/2 (signed); w = item w
    node sprite = {0, x, y, w, h, NULL}; node+0x1A = picture id
    node+0x1E = kind == 1000 ? 1000 : kind == -1 ? Parts_DealNext() : kind
                (an explicit part flagged 0x4000 gets picture id + 1)
    node+0x20 = 0; node+0x22 = {0x800, x, y, w, h}
    room+0x9E += 1; node+8 = 0; List_InsertAt(room list, node, 0xffff)
return 1
```

Items are never placed in the displayed room by this routine, so no picture is loaded here.

### 7.4 Other item routines [O]

* `Room_RemoveItem(room, node)` 1622:0d8f: node+8 = 1; room+0x9E −= 1; in the displayed room
  `PicCache_Release(+0x1A)` and DS:0964 −= 1; unlink.
* `Room_DropItemAt(room, x, floor, kind, restoring)` 1622:0df0: a new node at the given x on a
  floor (small room: floor −3 if > 2, y = 46 + 52·floor; hall y = 26 + 26·floor; sizes and picture
  as §7.3, +1 for a 0x4000 part); when `restoring == 0` the room count is incremented and, in the
  displayed room, DS:0964 too and the picture is loaded; appended to the list in either case.
* `Item_PlaceInRandomRoom(kind)` 1622:0f4e: `r = R(g_roomCount − 1) + 1`, repeated until
  `r ≠ g_displayRoom` and `g_roomItemCounts[r] ≠ 0`; then `Room_PlaceItems(room r, 1, kind)`;
  on failure the whole draw is repeated. Each attempt costs ≥ 1 + 20·cells rand calls.
* `Room_GetItem(room, n)` 1622:1265 = `List_GetAt` (past the end: the tail).

### 7.5 Saving rooms [O 1622:0fb3, 10e7]

`Rooms_SaveState(doors, items)`: for rooms 0..15 present: a byte 7 or 9 per layer-0 object of
type 7 or 9, in order; `u16 n = List_Count(items)`; per item `{x (+0x0E), floor, kind}` with
floor = `Room_HallFloorFromY` (y < 0x1c, 0x36, 0x50, 0x6a, 0x84, else 5) or
`Room_SmallFloorFromY` (y < 0x2f, < 99, else 2). `Rooms_RestoreState`: `ItemPool_Reset`; per
room: rewrites the types (9 → kind 4 and open-door art), clears the list, `+0x9E = n`,
`g_cratesLeft = 0` (**per room**), then `Room_DropItemAt(r, x, floor, kind, 1)` and counts
crates. No sizes are checked: 0x96 door bytes and 0x190 item bytes are the gaps in the block.

### 7.6 Crate refills [O 18f5:0895..094a, 18f5:0063, 2a80:0043..0060]

Picking a crate up gives `Projectiles_Add(5)` and `g_cratesLeft −= 1`. When the player throws his
last banana and `g_cratesLeft == 0`, `g_cratesTotal` crates are placed with
`Item_PlaceInRandomRoom(1000)` and `g_cratesLeft = g_cratesTotal`. A chimp that takes a crate
moves it: the crate is removed and one is placed in a random other room (the count does not
change).

---

## 8. Drawing

### 8.1 The frame sprite list (1581) [O]

`g_sprites` DS:05f6, 14-byte entries copied from the object (`{flags, x, y, w, h, far
picture}`), count DS:05f4, write pointer DS:08b2. The list order is the z-order (first drawn
first). There is no capacity check [O].

* `Sprites_EraseAll` (1581:04b6): `Gfx_RestoreRect(x, y, w, h)` for every entry without flag
  0x80, then empties the list. An entry with flag 0x80 does not advance the walk, so every later
  entry is skipped too (**bug**, harmless: nothing sets 0x80) [O 1581:04c4..04e5].
* `Sprites_DrawAll` (1581:04fc): `Lpx_DrawKeyed(x, y, picture, flags)` for every entry without
  0x10; the blitter is chosen by `(picture format + flags) & 3` (`fmt_graphics.md`).

### 8.2 Layer order of a warehouse frame [O 12e0:06bc..06ee]

```
Sprites_EraseAll
Room_DrawLayer(1)          static layer 1 (empty in the data)
Room_DrawAnimLayer(0)      animated list 0 (pads, flaps)
Room_DrawLayer(2)          (empty)
Actors_Draw                projectile and chimps of the displayed room, then the player
Room_DrawAnimLayer(3)      (empty)
Room_DrawLayer(3)          static layer 3 (empty), then the room's items
Sprites_DrawAll
```

Items are therefore drawn over the actors. Layer 0 is in the background; animated lists 1 and 2
are queued only at room entry (`Room_QueueEntryLayers`) and are erased at the first frame [O;
empty in the data].

### 8.3 Static layers — `Room_DrawLayer(L)` (1622:0922) [O]

For each object of layer L of `g_room`: if flags & 0x40: `flags ^= 0x50` (clears the request,
toggles 0x10); if no flag 0x14: `Sprites_Add`. For L = 3 also every item of the displayed room
(`node + 0x0C`). `Obj_Hide*` set 0x40 only on a visible object, `Obj_Show*` clear 0x10 (§9.3 ops
−22/−23).

### 8.4 Animated objects — `Room_DrawAnimLayer(L)` (1622:0706) [O]

For **rooms 1..3 only** (the halls; animated objects elsewhere would never animate), for each
object of list L: skip if flags & 0x10; if not paused (0x04) and `−−countdown < 0`: countdown =
reload, then fetch script entries (`pos++`) until a frame:

| Entry | Effect |
|---|---|
| ≥ 0 | frame: in the displayed room picture = cache[slot[frame]], w/h from the picture (`Obj_SetSizeFromPicture`); stop fetching |
| −1 | pos = 0, continue |
| −3 | flags \|= 4 (pause), pos = 0, continue (so frame 0 is shown and kept) |
| −5 | pos = 0, flags \|= 4, not drawn this frame |
| −8 | picture = NULL, not drawn this frame |
| −2, −4, −6, −7, < −8 | ignored, continue |

Then, in the displayed room only, `Sprites_Add` when the picture is not NULL. Objects in other
halls keep ticking. The player and chimp step op −9 restarts a pad (§9.3).

### 8.5 The play area and the HUD

Play area rows 9..164 (the menu bar covers 0..8); HUD rows 165..199 (§17). Actors are clamped to
x ≥ 1 and x + w ≤ 319 (§9.3).

---

## 9. The actor system (1834)

### 9.1 Actor record (12 × 0xA0 bytes at `g_actors` DS:7a8a) [O]

Slot 0 player (DS:7a8a), 1 projectile (DS:7b2a), 2..11 chimps (DS:7bca + 0xA0·(i−2)).
"Saved" = inside +0x14..+0x41 (`Actors_SaveState`, 0x2E bytes per actor).

| Off | Name | Meaning | Saved |
|---|---|---|---|
| +0x00 | flags | sprite flags of the current frame (step c): bits 0..1 mirror; 0x8000 hidden (op −8) | |
| +0x02, +0x04 | x, y | drawn position | |
| +0x06, +0x08 | w, h | from the set's W/H tables | |
| +0x0A | picture | far | |
| +0x0E | bundle | loaded sprite bundle (+0x98 + scale), −1 none | |
| +0x10 | countdown | frames until the next step | |
| +0x12 | delay | reload = sequence header +0 (op −24 changes it) | |
| +0x14, +0x16 | anchor x, y | x accumulates step deltas; y is the base of the step's y offset | yes |
| +0x18 | seq | current sequence | yes |
| +0x1A | room | | yes |
| +0x1C | floor | hall floors 0..5 (§5.6) | yes |
| +0x1E | prev seq | sequence before the last change (op −7) | yes |
| +0x20 | exit seq | entry sequence chosen by the end callback (written only) | yes |
| +0x22 | step | index into the sequence's steps | yes |
| +0x24 | door cooldown | chimps: brain ticks before doors attract again (0x2da after a door), −1 idle | yes |
| +0x26, +0x28 | loop | counter and start step (ops −31/−32) | yes |
| +0x2A | disabled | 1 = not ticked, not drawn | yes |
| +0x2C | pending seq | sequence to start on the next room entry, −1 none | yes |
| +0x2E | walls on | wall collision (ops −16/−17) | yes |
| +0x30 | contacts on | actor-to-actor contacts (op −28) | yes |
| +0x32 | blocked | −1 wall at the left, 1 at the right, 0 free; also the direction used by `Actor_CollideWalls` when non-zero | yes |
| +0x34 | — | not read (projectile −1) | yes |
| +0x36 | door obj | static object index of the door, passage, hole or tube touched | yes |
| +0x38 | pad | animated object index (kinds 0x780) or a jump record's pad index | yes |
| +0x3A | edge obj | floor edge or hole object touched | yes |
| +0x3C | prev room | room left last (door twin lookup) | yes |
| +0x3E | immune | player: frames of immunity after a chimp hit (150) | yes |
| +0x40 | item | index of the item touched in the room's list | yes |
| +0x42 | id | actor index (0, 1, 2..11); copied into a touched actor's +0x6E | |
| +0x48 | contacts | contact flags (§10.3) | |
| +0x4A | wall bits | `Actor_CollideWalls` result | |
| +0x4C | dir | sequence header +2 (−2..2; sign = facing/motion) | |
| +0x4E | wall x | edge x found by `Actor_CollideWalls` | |
| +0x52 | seqs | far: active sequence table | |
| +0x56, +0x5A | seqs hall, room | far: sequence tables of the hall set and the room set | |
| +0x62 | brain | far callback `int brain(index)` | |
| +0x66 | on op −10 | far `void f(actor far *)` | |
| +0x6A | on op −29 | far `void f(actor far *)` | |
| +0x6E | toucher | +0x42 of the actor touched last | |
| +0x70 | carried | parts held (count) | |
| +0x72 | parts | far: carried-part list (player: own list; chimps: shared DS:a03c) | |
| +0x76, +0x7A | W tables | far, room / hall set | |
| +0x7E, +0x82 | H tables | far, room / hall set | |
| +0x88 | on end | far `void f(index)` (op −11) | |
| +0x8C | on enter | far `void f(index)` (room entry; op −11 for non-players) | |
| +0x90 | trans | near pointer of a transition index (player DS:127e, projectile DS:1276, chimps 0): written only | |
| +0x94 | — | u32, chimps: `contacts << 16` after each brain call; written only | |
| +0x98 | base | sprite bundle id of the large scale; +1 = hall scale | |
| +0x9A | slots | far: picture-slot table filled by `PicCache_LoadBundle` | |

The offsets not listed (+0x44, +0x46, +0x50, +0x5E, +0x86, +0x92, +0x9E) are never accessed
[O: no direct DGROUP access to any actor's copy and no indexed access in 1622, 1834, 18f5, 1adb,
2a80].

Initial values (`Actors_Init` 18f5:0721, `Chimps_SetupActors` 2a80:0635) [O]:

| | Player | Projectile | Chimp i |
|---|---|---|---|
| brain +0x62 | 18f5:029b `Player_Brain` | 18f5:0401 `Projectile_Brain` (−1) | 2a80:0112 `Chimp_Brain` |
| +0x66 | 18f5:0002 `Player_PickUpItem` | NULL | 2a80:0000 `Chimp_TakePart` |
| +0x6A | 18f5:0180 `Player_HitByChimp` | NULL | NULL |
| +0x88 | 18f5:04cf `Player_SequenceEnd` | NULL | 2a80:0431 `Chimp_SequenceEnd` |
| +0x8C | 18f5:0409 `Player_EnterRoom` | 18f5:06fb `Projectile_EnterRoom` | 2a80:032f `Chimp_EnterRoom` |
| sets (hall / room) | 0x1034 / 0x1033 | 0x1036 / 0x1035 | 0x1042 / 0x1041, from level 3 0x103e / 0x103d |
| seq tables (+0x56/+0x5A) | DS:832c / DS:8440 | DS:8304 / DS:8318 | DS:9e72 / DS:9dc6 (shared) |
| sprite base +0x98 | 0x1031 | 0x1037 | 0x103f, from level 3 0x103b |
| slots +0x9A | DS:8214 | DS:820a | DS:9f26 (shared) |
| other | delay 2, walls/contacts on, room 0, id 0, list `List_New` | disabled, delay 2, id 1 | id i, delay 2, walls/contacts on, list DS:a03c |

### 9.2 Actor sets (`S` 0x1033..0x1036, 0x103d, 0x103e, 0x1041, 0x1042) — `fmt_data.md` §7.3

`Actors_LoadSet(id, seqTable, &W, &H)` (1834:0088) [O]:

```
u16 nFrames
i16 W[nFrames]          frame widths    (the far pointer &W points here)
i16 H[nFrames]          frame heights
u16 nSeq
nSeq sequences: 10-byte header {i16 delay, i16 dir, far steps (patched), u16 count},
                then count steps of 10 bytes {i16 op, i16 a, i16 b, u16 c, u16 d}
```

The step list of every sequence ends with a step whose op is −1000; the very last sequence of a
set stores only the op word of that step (2 bytes) [O all 8 sets parse exactly]. The sequence
table (`seqTable`, far pointers) is NULL-terminated. A frame step has op = frame number (≥ 0),
a = x delta, b = y offset, c = sprite flags, d = picture slot (written at bind time by
`AnimSeq_BindPictures` 2bd4:01c1: d = slots[op]). The screen-animation sets of §19.1 use the
same 10-byte sequence header with the count at +8.

### 9.3 Step interpreter — `Actor_RunStep(i)` (1834:01b6) [O]

```
scale = Room_IsHall(actor.room); W = (+0x76/+0x7A)[scale]; H = (+0x7E/+0x82)[scale]
steps = (+0x52)[seq].steps
loop:
    s = steps[+0x22]
    if s.op < 0: execute the command (table below); most continue the loop
    else (frame):
        flags(+0) = s.c; w = W[s.op]; h = H[s.op]
        oldx = +0x14; +0x14 += s.a; x(+2) = +0x14; y(+4) = +0x16 + s.b
        if walls on (+0x2E):
            sweep = moving right (x >= oldx) ? {x: oldx, w: w + (x - oldx)}
                                             : {x: x,    w: 1 - s.a}       # left: the path only
            sweep.y = y, sweep.h = h
            r = Actor_CollideWalls(i, &sweep, -1)
            r == 0: +0x32 = 0
            r == 2 and dir(+0x4C) < 2:  x = +0x14 = wallx + 1,      +0x32 = -1
            r != 2 and dir > -2:        x = +0x14 = wallx - w + 1,  +0x32 = 1
        if +0x14 < 1: x = +0x14 = 1
        if x + w > 319: x = +0x14 = 319 - w
        picture = cache[s.d]; +0x22 += 1; return result (0)
```

Commands (op, a, b; "→" = continue with the next step unless stated). Jump table CS:06d7,
46 entries for ops −46..−1:

| Op | Effect |
|---|---|
| −46 | +0x14 = (scale ? 0x13e : 0x13c) − W[a] (right edge) → |
| −43 | hint: a == −1 → `Hint_Queue(g_withChimps ? 0x104b : 0x1055, 5, NULL)`; else `Hint_Queue(a, 0, NULL)` → |
| −42 | if contacts & 1: step = a (no increment), continue; else → |
| −41 | +0x14 = edge object (+0x3A) x + 4 → |
| −40 | +0x14 = edge object x + W[a] − 7 → |
| −37 | stop effect `DS:7a44 + a` if in the displayed room (`SndRes_StopEffect`) → |
| −36 | if in the displayed room and effect `DS:7a44 + a` is not playing: play it (`SndRes_PlayEffect(bank + a, b >> 8, b & 0xff)`) → |
| −33 | `Chimp_DropPart(i)` → |
| −32 | `if (--+0x26 != 0)` step = +0x28 (continue there); else → |
| −31 | +0x26 = `g_chimpStunLoops[g_level]` (DS:0a90); +0x22 += 1; +0x28 = +0x22; continue |
| −30 | projectile disabled (DS:7b54 = 1); +0x22 += 1; if `g_projectiles == 0`: `Hint_Queue(0x1051, 0, NULL)`; continue |
| −29 | `(+0x6A)(actor)` → |
| −28 | +0x30 = a → |
| −27 | `Projectile_Launch(a, b)` → |
| −26 | +0x14 = door object (+0x36) x → |
| −25 | +0x2A = 1 (disable self); if `g_projectiles == 0`: `Hint_Queue(0x1051, 0, NULL)`; return 0 (step not advanced) |
| −24 | +0x12 = a → |
| −23 | show: b == −1 → `Obj_Show(Room_GetObject(room, +0x38))`; else `Obj_ShowAt(layer a, index b)` of the displayed room → |
| −22 | hide: b == −1 → `Obj_Hide(Room_GetObject(room, +0x38))`; else `Obj_HideAt(a, b)` → |
| −21 | pad = `Room_GetAnim(room, +0x38)`; a ≠ 0x8000: +0x14 = pad x + a; b ≠ 0x8000: +0x16 = pad y + b → |
| −17 | walls off (+0x2E = 0) → |
| −16 | walls on, +0x32 = 0 → |
| −15 | +0x16 = a → |
| −14 | +0x14 = a → |
| −13 | +0x16 += a; floor +0x1C += b → |
| −12 | +0x14 += a → |
| −11 | end: `(+0x88)(i)`; if i == 0: return 1 (step not advanced); else `(+0x8C)(i)`, step = 0 and restart with the (new) current sequence |
| −10 | `(+0x66)(actor)` → |
| −9 | pad = `Room_GetAnim(room, +0x38)`: countdown 0, flags &= ~0x14, script pos = 1 → |
| −8 | +0x22 += 1; flags \|= 0x8000 (hidden); return 0 |
| −7 | `Actor_SetSequence(i, +0x1E)` (back to the previous sequence), step 0, continue |
| −6 | `Actor_SetSequence(i, a)`, step 0, continue |
| −4 | step = a, continue |
| −1 | step = 0, continue |
| −45, −44, −39, −38, −35, −34, −20, −19, −18, −5, −3, −2, < −46 (incl. −1000) | **the interpreter re-reads the same step forever** (the game hangs); no shipped sequence reaches them |

`Actor_SetSequence(i, n)` (1834:003b): +0x1E = +0x18; +0x18 = n; +0x22 = 0; +0x12 = header
delay; +0x4C = header dir. `Actor_SelectScale(i, hall)` (1834:015b): +0x52 = hall ? +0x56 :
+0x5A and binds the pictures of every sequence of that table to +0x9A.

The −23/−22 steps of the player's hall sequences 52 and 53 address layer 1, object 0, which
does not exist in any shipped room: the layer pointer is NULL, so the original masks or sets a
bit in the word at 0000:0000 (the interrupt vector table) [O 1622:0a17, data]. The engine must
ignore such an access.

### 9.4 Per-frame tick — `Actors_Tick` (1834:0ae9) [O]

```
result = 0
for i = 0 .. 11:                              # every room, not only the displayed one
    if +0x2A: continue
    Actor_UpdateContacts(i)                   # §10.3
    r = brain(i)
    if r != -1 and (r & 0x7fff) != seq:
        Actor_SetSequence(i, r & 0x7fff)
        if !(r & 0x8000): +0x22 = 0; +0x10 = 0  # start now; with bit 15 keep the countdown
    if --+0x10 < 0:
        +0x10 = +0x12
        result |= Actor_RunStep(i)
return result                                  # non-zero: the player's sequence ended
```

So an actor with delay d steps every d + 1 frames (players and chimps mostly d = 2: 12.1 steps/s;
the projectile d = 0). Actors are ticked only by the warehouse loop: while the player is in the
workshop, a puzzle, the hub or a dialog, nothing moves.

### 9.5 Drawing — `Actors_Draw` (1834:0733) [O]

For i = 1..11: enabled, in the displayed room and not hidden (0x8000) → `Sprites_Add`.
Then the player: if the immunity counter (+0x3E) is 0, `g_playerBlink` DS:0a9a = 0; if not
hidden: drawn when `g_playerBlink < 2`, and while immune `g_playerBlink = (g_playerBlink + 1) % 4`
(blinks 2 frames on, 2 off). The player is always drawn last (on top of the actors).

### 9.6 Room entry and exit of actors [O]

`Actors_EnterRoom` (1834:07ad), called by state 5 after `Room_Enter`, does nothing for room 0.
Otherwise: `Actors_LoadPlayerSets`, `Chimps_LoadSets` (both cached), `g_actorSpritesLoaded` DS:0a9c
= 1; player: +0x0E = +0x98 + scale, `PicCache_LoadBundle(+0x0E, +0x9A)`, `Player_EnterRoom(0)`;
projectile: same, `Projectile_EnterRoom(1)`; if `g_withChimps == 1`: every chimp gets
+0x0E = +0x98 + scale and `PicCache_LoadBundle` (shared slots), and each enabled chimp in the
displayed room gets `Chimp_EnterRoom(i)`. `Actors_LeaveRoom` (1834:0870) releases the same bundles
(`PicCache_ReleaseBundle`) and sets +0x0E = −1.

### 9.7 Carried parts [O 1834:08d7, 0990]

* `Actor_AddPart(a, p)`: `n = PartList_CountType(list, type(p), &last)`; if n < 2: p.+0x36 &=
  0x4000, +0x8A = 0, +0x1C = last, holder +0x0C = a, +0x1A = 0x2238; if last ≠ −1:
  part[last].+0x1C = p | 0x8000; `actor[a].+0x70 += 1`; append p to the list. Returns
  `last ≠ −1` (p is the second of its type). With n ≥ 2 nothing happens.
* `Actor_RemovePart(a, p, anyHolder)`: `n = PartList_CountType(...)`; finds p in a's list
  (holder must be a unless anyHolder == 0); the holder's +0x70 −= 1, +0x0C = −1, unlink; if p was
  paired, its partner is re-linked at p's old position (when it was the "second") and its
  +0x1C = −1. Returns n − 1.

---

## 10. Collision and contacts (1adb)

### 10.1 `Rect_Overlap(a, b)` (1adb:000c, Pascal convention) [O]

Rectangles are `{u16 flags, x, y, w, h}`; `a` is the first parameter (pushed first; the callee
removes 8 bytes). Returns 0 unless `a.x < b.x + b.w`, `a.x + a.w > b.x`,
`a.y < b.y + b.h`, `a.y + a.h > b.y` (signed). Otherwise bits: 1 if a.x < b.x, 2 if
a.right > b.right, 4 if a.y < b.y, 8 if a.bottom > b.bottom; 0xF when none (a inside b).

### 10.2 Walls — `Actor_CollideWalls(i, sweep, dir)` (1adb:0093) [O]

`d = +0x32 ≠ 0 ? +0x32 : current sequence's dir`. Wall list = group 0 of the actor's room for its
floor (−3 in small rooms). For each wall until one blocks: `r = Rect_Overlap(sweep, wall)`; by `r &
3`: 0 (vertical overlap only) → ignored, next wall; 1 → bit 1, wall x → +0x4E; 2 → bit 2, +0x4E =
wall.right − 1 (the dir argument −1 selects this; any other value would use wall x); 3 → by d < 0:
bit 2 and wall.right − 1, else bit 1 and wall x. +0x4A = the bits; returns them.

### 10.3 Contacts — `Actor_UpdateContacts(i)` (1adb:03b0), before every brain call [O]

```
+0x48 = 0
items of the room: first with Rect_Overlap(item +0x0C, actor's first 10 bytes) != 0:
    +0x48 |= 0x800; +0x40 = its index
if +0x30 (contacts on):
    list = i == 0 ? DS:13a0 : i == 1 ? DS:13b6 : DS:13cc          # -1 terminated
         (player: chimps 2..11; projectile: chimps 2..11; chimps: the projectile only)
    first j enabled, contacts on, same room, same floor, Rect_Overlap(actor, j) != 0:
        +0x48 |= (j.dir < 0) ? 0x1000 : 0x2000; +0x6E = j.+0x42
floor references of the actor's floor (§5.2), in order, until one stops the scan:
    hit = animated ? obj+0x3E : obj+0x16; r = Rect_Overlap(hit, actor); if r == 0: next
    +0x48 |= hit.kind
    kind & 0x4c (open door, locked door, tube): +0x36 = index; stop
    kind & 0x30 (side passage):                 +0x36 = index; stop
    kind & 0x780 (pads, flaps):                 +0x38 = index; stop
    kind & 1 (floor edge): +0x3A = index; if (r == 2 and dir == -1) or (r == 1 and dir == 1):
                           +0x48 &= 1 (only the edge bit survives); stop
                           # r is relative to the marker: 1 = it sticks out left of the
                           # actor only, 2 = right only (dir = sequence dir +0x4C)
    kind & 2 (hole):       +0x36 = +0x3A = index; stop
    else: next
```

Contact bits (+0x48): 1 floor edge, 2 hole, 4 open door, 8 locked door, 0x10 / 0x20 left / right
passage, 0x40 tube, 0x80 lift pad, 0x100 launch pad, 0x200 / 0x400 flaps, 0x800 item, 0x1000 /
0x2000 touched by an actor moving left / right (or still).

### 10.4 Chimp probes — `Chimp_ProbeContacts(i, probe)` (1adb:01ef) [O]

Like §10.3 for an arbitrary rectangle, without actors (`Rect_Overlap(item or hit, probe)`, only
tested for zero): the first item overlapping the probe sets
+0x48 = 0x800 (assigned, not or-ed: the other contact bits are lost until the next frame's
`Actor_UpdateContacts`; the brain's transitions use the copy taken before), +0x40 and the result
0x800; then every floor reference whose
hit overlaps ors its kind into the result, kinds 0x780 also set +0x38. Returns the kinds.

---

## 11. The player (18f5)

### 11.1 Input mask [O 198d:02b8 `Input_GetHeldKeys`]

Each frame the brain builds a 32-bit mask: low word = held keys DS:0218 (1 Left, 2 Right, 4 Up,
8 Down, 0x10 Ctrl, 0x20 Space) plus 0x40 once after 0x111c ticks (30.07 s) without a held key;
high word = the player's contacts +0x48. When any key is held: `g_keyHeldSeen` DS:5c1f = 1,
`g_lastKeyHeldTick` DS:1314 = tick, `g_idleReported` DS:8554 = 0. The idle bit is reported when
`DS:1314 + 0x111c < tick` (u32) and DS:8554 = 0, then DS:8554 = 1. The warehouse entry resets
DS:1314. Held keys are swallowed by the keyboard handler in play mode, so they never reach
`Input_GetCode` (ARCHITECTURE §4.4). The mouse plays no part in moving the player.

### 11.2 Brain — `Player_Brain` (18f5:029b) [O]

```
mask = Input_GetHeldKeys()
if +0x3E (immune) != 0: +0x3E -= 1; mask &= 0xCFFFFFFF          # chimp contacts ignored
list = g_playerTransitions[seq]          # DS:127e, near pointers, 0 = none -> return -1
for each 12-byte entry {u32 match, u32 ignore, u16 next, u16 unused} until match == 0xFFFFFFFF:
    if (mask & ~ignore) == match or match == 0:
        next = entry.next
        if 12 <= next <= 15: look in group 1 of g_room, list [floor]   # no -3 here
        if next == 16 or 17: look in group 2 of g_room, list [floor]
            first record with record.trigger == next and
            player.x < r.x + r.w and player.x + w > r.x:
            next = record.replacement; +0x38 = record.pad
        return next
return -1
```

The first matching entry wins; entries are in data order. The sixth word of an entry is never
read [O]. The table index covers 68 sequences (the hall set's count). Unlike the collision code,
the jump-spot lookup uses the floor without the small-room −3 [O 18f5:031e]; every shipped jump
record is in a hall, so this never matters.

### 11.3 What the tables encode [I from the tables and the sequences, O for the mechanism]

Sequences are shared by number between the room set (53 sequences) and the hall set (68; 52..67
exist only in halls):

| Seq | Role |
|---|---|
| 0, 1 | stand / idle animation (0x40 idle bit → 1) |
| 2, 3 | walk left / right (4 px per step) |
| 4, 5 | run left / right (Left/Right + Ctrl) |
| 6, 7 | stand facing left / right |
| 8..11 | turn around |
| 12, 13 / 16, 17 | jump left / right (Up + Left/Right, longer with Ctrl); a jump spot record turns them into 14, 15 / 18, 19, the jumps onto a lift pad (hall set) |
| 20, 21 | standing pose used after jumps and falls |
| 22..25 | throw a banana left / right (Space; the step −27 launches the projectile) |
| 26..31 | crouch (Down), hold, stand up |
| 32 | go through an open door (Up at contact 4) → −11 |
| 33 | come out of a door (entry sequence) |
| 34 | walk into a locked door (Up at contact 8) → −11 → puzzle |
| 36..39 | fall at a floor edge one floor down (contact 1) / through a hole into another room (contact 2, ends with −11) |
| 40, 41 | arrive through a hole from above |
| 42, 43 | leave through the left / right passage (contacts 0x10/0x20 with Left/Right) → −11 |
| 44, 45 | enter through a passage at the left / right edge |
| 46, 47 | crouch and pick up an item (contact 0x800 + Down + Left/Right; step −10 calls `Player_PickUpItem`) |
| 48..51 | knocked down by a chimp (contacts 0x1000/0x2000; step −29 `Player_HitByChimp`, hint 0x104d) |
| 52 | enter the tube to the workshop (hall set; Up at contact 0x40) → −11 |
| 53 | come out of the tube (hall set; hint −43 −1) |
| 54..67 | hall set: lift pads (0x80), launch pads (0x100), flaps (0x200/0x400) |

There are no ladders or elevators [I from the object types and sequences]: the player changes
floors by falling at a floor edge (one floor down, step −13 b = +1), by the hall pads and flaps
(up one floor, −13 b = −1 or −2), by jumping onto a pad from a jump spot, or by leaving the room
(doors, side passages, holes into the room below, the workshop tube). Poles, walls and tube
segments are scenery; only the wall lists (§5.5) stop movement. Keys drive the player; the mouse
only serves the HUD and the menus.

### 11.4 Leaving a room — `Player_SequenceEnd` (18f5:04cf, op −11) [O]

`obj = Room_GetObject(g_playerRoom, +0x36)`; `g_puzzleDoorHit` DS:00bc = 0. Target room T and
entry sequence S:

| Object | T | S |
|---|---|---|
| none (+0x36 = −1; the workshop's exit, 2dee:0e4a) | `g_tubeRoom` DS:4580 | 53 |
| type 6 (hole) | +0x12 | 41 if dir > 0 else 40 |
| type 7 (locked door) | the same room | 33; `g_puzzleDoorHit` = 1; DS:458c = 1 (cancels hint 0x1048) |
| type 8 (tube) | 0 (workshop) | 0; `g_tubeRoom` = the current room |
| type 9 (open door) | +0x12 & 0xF | 33; DS:458c = 1 |
| type 10 / 11 (passage) | +0x12 & 0xF | 45 / 44 |
| other | SI/DI uninitialised (bug, unreachable) | |

Then +0x20 = S, +0x3C = the current room, `g_playerRoom` = T, +0x2C = S. The step returns 1 and
the warehouse loop leaves (§14).

### 11.5 Entering a room — `Player_EnterRoom` (18f5:0409, +0x8C) [O]

`link = Room_GetObject(+0x3C, +0x36).+0x12` (the object used in the previous room);
`Actor_SelectScale(0, hall)`; +0x10 = 0. If +0x2C ≠ −1: S = +0x2C, +0x2C = −1, +0x18 = S,
`Actor_SetSequence(0, S)` (so +0x1E = S as well), +0x22 = 0; for S = 33, 44, 45: `door =
Room_FindLinkedDoor(T, link)` and x = +0x14 = door.x + (hall ? 3 : 10), y = +0x16 = door.y + (hall
? 5 : 14); for S = 53: floor = 5. The floor is not changed by doors (§5.6 makes it unnecessary).

### 11.6 Picking up — `Player_PickUpItem(actor)` (18f5:0002, op −10) [O]

```
item = Room_GetItem(room, +0x40); if item.+8 == 1: return      # already gone this frame
if item.kind == 1000: Projectiles_Add(5); Room_RemoveItem; g_cratesLeft -= 1; return
p = item.kind
if PartList_CountType(player list, type(p)) == 2:              # at most 2 of a type
    Hud_FlashPart(p, 1); Hint_Queue(0x1047, 0, NULL); return     # the item stays
play effect DS:7a44 + 1 (0, 4)
needed = Part_IsNeeded(p); second = Actor_AddPart(0, p); Hud_AddPart(p)
Hint_Queue(needed ? 0x104a : 0x1044, 0, NULL)
Room_RemoveItem(room, item); g_hudDirty |= 1
if !second and needed: g_partsNeeded -= 1
if g_partsNeeded == 0: Hint_Queue(0x1050, 0, NULL)
if !(p.+0x36 & 0x4000): Score_Add(20)                          # 12e0:0ace, u32 DS:4572
```

`g_partsNeeded` counts needed **types**: the first part of a needed type decrements it, a second
part of the same type does not. Points are given whenever the part is not flagged 0x4000. The
pick-up does not set that flag; the trash can does (1a04:0ae2, §17.2) and so does the workshop
(2dee:10ff), so a part that a chimp stole and dropped scores again when picked up [O code, I
effect].

### 11.7 Hit by a chimp — `Player_HitByChimp(actor)` (18f5:0180, op −29) [O]

```
chimp = +0x6E
Hud_EscHook()                       # puts back a part held on the cursor
+0x3E = 0x96                        # 150 frames of immunity (blinking)
if +0x70 == 0: return
k = R(+0x70)                        # rand
part = PartList_FindHeld(player list, 0, k); if none: return     # skips 0x8000 parts
t = type(part)
if PartList_CountType(g_chimpParts, t) == 2:
    q = PartList_FindType(g_chimpParts, t); Actor_RemovePart(chimp, q, 0)
    Item_PlaceInRandomRoom(q)       # rand: the chimps never hold 3 of a type
needed = Part_IsNeeded(part); anchor = Hud_StripAnchorFor(part)
n = Actor_RemovePart(0, part, 1); Actor_AddPart(chimp, part); Hud_RefreshStrip(anchor)
if n == 0 and needed: g_partsNeeded += 1
```

`PartList_FindHeld(list, holder, start)` (2bd4:0101) searches cyclically from index `start`; with
a start beyond the list it walks `List_GetAt`'s tail until the 16-bit index wraps (§20 bug 7).

### 11.8 The emergency reset (Alt+S then Alt+W) [O 12e0:0759..07c5]

In the warehouse loop, code 0x11F (Alt+S) arms a flag when it is clear; a following 0x111
(Alt+W) with the flag armed resets the player: anchor x = 10; hall: y = 0x90, floor 5; small
room: y = 0x7f, floor 5 if flag bit 1 else 2; `Actor_SetSequence(0, 0)`, step 0, contacts 0,
contacts on, walls on. Any other non-zero code disarms the flag; code 0 leaves it. Purpose: free
a stuck player [I].

---

## 12. The projectile (actor 1, bananas)

`Projectile_Launch(seq, dx)` (18f5:0895, player step −27) [O]: nothing if `g_projectiles == 0`.
Else: enabled; x = +0x14 = player anchor x + dx; y = player y; floor, room = the player's; +0x20 =
+0x2C = +0x1E = +0x34 = −1; step, countdown, contacts, blocked = 0; contacts on;
`Actor_SelectScale(1, Room_IsHall(room))`; `Actor_SetSequence(1, seq)`; `g_projectiles −= 1`;
`g_hudDirty |= 8`; refill rule §7.6.

Its four sequences per set (0/1 = left/right, 2/3 = the same with a longer flight) [O data]: a
flight of frame 0 moving 12-13 px per frame (6 in halls) with a growing y offset, contacts off,
a splat falling to the floor, contacts on for one step, stop the flight sound, then op −25
(disable; hint 0x1051 when no banana is left). The brain always returns −1; walls clamp it like
any actor. It never touches the player.

**What it does to a chimp** [O]: a chimp's contact list holds the projectile, so an overlapping
banana on the same floor sets the chimp's contact 0x1000 (banana moving left) or 0x2000. The
chimp transitions turn that into the stunned sequence 30/31 (§13.6), whose first step (−30)
disables the projectile: the banana disappears on a hit. A chimp in a sequence without a move
list (jumping, falling, at a door) ignores the hit.

---

## 13. Cyber Chimps (ovl00 2a80 and DGROUP tables)

### 13.1 Chimp lists and spawn [O 2a80:0784 `Chimps_Spawn`]

`g_chimpLists` DS:1bae is **15 far pointers** (not counts) to lists in DGROUP (DS:1adc..1bae):
`{u16 count, u16 room[count]}`; counts 2, 4, 6, 8, 10 for levels 0..4 in every building, the
first room is always 1 [O data].

```
list = g_chimpLists[race]; n = list[0]
if g_cpuType DS:2450 == 2: list[1] = list[1] / 2     # in place, signed; 1 -> 0 (workshop)
memset(g_roomChimpCount, 0, 0x20)
for slot i = 2..11:
    if i - 2 < n:
        +0x2A = (g_withChimps == 0)                  # Without Chimps: disabled, but placed
        +0x48 = +0x22 = +0x70 = 0; +0x94 = 0; +0x6E = +0x3C = -1
        r = list[1 + (i - 2)]; g_roomChimpCount[r] += 1; +0x1A = r
        big = (+0x98 == 0x103b)                      # level >= 3
        hall: floor = R(6); y = floor*26 + (big ? 14 : 15)
        else: floor = R(3); y = floor*52 + (big ? 24 : 28); if flags & 2: floor += 3
        +0x16 = y; +0x1E = -1; seq +0x18 = 0
        if R(100) < 50: +0x14 = 10, +0x94 = 2 else +0x14 = 300, +0x94 = 1
        Actor_SelectScale(i, hall)
    else: +0x2A = 1
```

**286 path** (the `cpu` option): the halving changes the first chimp's room to 0 at every set-up
(1 → 0 → 0, the table stays modified for the rest of the run). A chimp in room 0 is ticked but
can never leave (room 0 has no walls, doors or floor references), is never drawn (only the
warehouse draws actors and room 0 is the workshop) and finds no items (room 0's item list is NULL
and its count 0; the original reads a list head from 0000:0000 and ignores it) [O code, I effect].
So on a 286 there is one chimp fewer in the warehouse. Its first brain call still draws one
`R(1000)` (seq 0, §13.4); afterwards it walks against the screen edge (x is clamped, no wall sets
+0x32) and its probes touch nothing, so it draws no more [O code, I effect].

**With Chimps off** (`g_withChimps` DS:4578 = 0): the chimps are placed (same rand calls) but
disabled; their sprites are not loaded; hint 0x1055 replaces 0x104b at the first tube exit.

### 13.2 Actor set-up [O 2a80:0635 `Chimps_SetupActors`, 2a80:051e `Chimps_LoadSets`]

Called on every set-up (both paths). `g_chimpParts` DS:a03c = `List_New()` (one carried list for
all chimps; a part's holder says which chimp has it). For slots 2..11 the fields of §9.1
(id = slot, delay 2, countdown 0, immune 0, walls and contacts on, tables, callbacks, +0x20 =
+0x0E = +0x2C = +0x24 = −1, +0x90 = 0). Big chimps (level > 2): sprite base 0x103b and sets
0x103e (hall) / 0x103d (room); else 0x103f and 0x1042 / 0x1041. The sets are cached in DS:9f22
(hall) / DS:9f1e (room) with their W/H tables in DS:a018/a014 (hall) and DS:a010/a00c (room), and
freed by `Chimps_FreeSets` (state 8 before a puzzle) and `Chimps_Free` (also frees DS:a03c;
`Building_Free`).

### 13.3 Brain — `Chimp_Brain(i)` (2a80:0112) [O]

```
next = -1; contacts = +0x48 (computed just before by Actor_UpdateContacts)
if +0x24 >= 0: +0x24 -= 1                                # door cooldown
moves = g_chimpMoves[seq]  (DS:1d2e, near pointers; 0 = none)
+0x94 = 0
if moves:
    if moves[0] == -2: next = seq                         # "stay": only transitions apply
    else:
        n = moves[3] (+6); next = moves[4] (+8, default)
        for k = 0 .. n-1 (12-byte entries from +0xC) until one fires:
            e = {dist, kinds, unused, chance, next, flag}
            probe = the chimp's {flags, x, y, w, h}
            if e.kinds == 4:                              # doors: a 4-px probe at the centre
                probe.x += w/2 - 2 (signed); probe.w = 4
                if +0x24 >= 0: continue                   # cooldown running
            if e.dist == -1: fire = 1
            else:
                if dir(+0x4C) < 0: probe.x -= dist else probe.w += dist
                fire = (e.kinds & Chimp_ProbeContacts(i, &probe)) and e.flag == 0
            if fire and R(1000) < e.chance: next = e.next; stop     # rand per firing entry
if next == 28 or next == 29:                             # pick up
    Room_GetItem(room, +0x40)                              # result unused
    if +0x70 != 0: next = (next == 28) ? 2 : 3           # already carrying
if next != -1 and (trans = g_chimpTransitions[next]) (DS:1f10, indexed by next):
    input = contacts << 16                               # no key bits for chimps
    first 12-byte entry {match, ignore, next', unused} with (input & ~ignore) == match
    or match == 0 -> next = next'; list ends with match 0xFFFFFFFF
if next == 2 and +0x32 < 0: next = 10                    # blocked left: turn
if next == 3 and +0x32 > 0: next = 11
+0x96 = contacts                                         # +0x94 = contacts << 16, written only
return next
```

Formats [O]: a move list starts with a 12-byte header `{i16 marker (−2 = stay, else active),
i16, i16, u16 n, u16 default next, i16}` followed by n 12-byte entries `{i16 dist (−1 =
unconditional), u16 kinds, u16 (not read), u16 chance per mille, u16 next, u16 flag (≠ 0 never
fires)}`. Transition lists are the player's format (§11.2). Entries that need key bits can never
match for a chimp; the tables share entries with the player's [O].

### 13.4 What the tables do [O data, I roles]

* Seq 0 (the start sequence): one unconditional entry (500 ‰ → 2) and the default 3, so the
  first brain call draws one `R(1000)` and the chimp walks left or right. Seqs 6, 7: one
  unconditional entry (50 ‰ → turn 10/11), default walk 2/3; the chimp leaves them at once too.
* A walking chimp (seqs 2..5, default = itself) draws `R(1000)` only for probes that touch
  something, once per touching entry until one succeeds; the item entry (1000 ‰) always
  succeeds.
* Seqs 2..5 (walking) probe for floor edges (kinds 1, 2: jump 12/16), lift pads (0x80), launch
  pads (0x100), side passages (0x10/0x20 → 22/23), open doors (4 → 20, 300 ‰, centre probe,
  cooldown), flaps (0x200/0x400), items (0x800 → 28/29, 1000 ‰); default: keep walking.
* Transitions after a move decision: lift pad contact → 36/37, flap → 40/41, edge → 26/27 (fall),
  hole → 32/33, banana hit → 30/31, blocked → 10/11.
* 20 = enter an open door (→ −11), 21 = come out of a door, 22/23 = leave through a passage,
  24/25 = enter through a passage, 26/27 = fall a floor (then a "stay" list: launch pad contact
  → 38/39), 28/29 = take an item (step −10), 30/31 = stunned, 32/33 = fall into another room,
  34/35 = arrive from above.

### 13.5 Doors, passages, holes [O 2a80:0431, 032f]

`Chimp_SequenceEnd(i)` (op −11): `obj = Room_GetObject(room, +0x36)`; type 6: T = +0x12, S =
dir ≥ 0 ? 35 : 34; type 9: T = +0x12 & 0xF, S = 21; type 10 / 11: T = +0x12 & 0xF, S = 25 / 24;
step 0; other types leave T and S uninitialised (bug; chimps only use open doors). Then +0x94 =
0, +0x20 = +0x2C = S, `g_roomChimpCount[room] −= 1`, +0x3C = room, room = T.
`Actor_RunStep` then calls `Chimp_EnterRoom(i)`: `g_roomChimpCount[room] += 1`; scale;
if +0x2C ≠ −1: S = +0x2C; S == 21 → door cooldown +0x24 = 0x2da (730 brain ticks = 20 s);
+0x2C = −1; countdown 0; `Actor_SetSequence(i, S)`; for S = 21, 24, 25: the twin of the object
used (`Room_FindLinkedDoor(room, link)`) gives x = twin.x + (hall ? 4 : 10), y = twin.y +
(hall ? (big ? 5 : 6) : (big ? 14 : 18)).

### 13.6 Stunned by a banana (seqs 30/31) [O data and ops]

−30 (the banana disappears), −28 0 (contacts off), a short animation, −33 (`Chimp_DropPart`), −31
(loop count `g_chimpStunLoops[g_level]`, DS:0a90: 5 values falling with the level), a loop body of
16 frame steps (room set 0x1041), −32, recovery frames, −28 1, −6 2. With delay 2 the loop alone
lasts 16 × 3 × count frames [O data, I arithmetic: about two minutes at level 0].
`Chimp_DropPart(i)` (2a80:00a1): `part = PartList_FindHeld(g_chimpParts, i, 0)`; if any:
`Actor_RemovePart(i, part, 1)` and `Room_DropItemAt(room, +0x14, floor, part, 0)` — the part lands
where the chimp stands.

### 13.7 Taking parts [O 2a80:0000 `Chimp_TakePart`, op −10 of seqs 28/29]

`item = Room_GetItem(room, +0x40)`; if item.+8 == 1: nothing. A crate is removed and
`Item_PlaceInRandomRoom(1000)` puts one elsewhere (rand). A part is taken only when the chimp holds
none (`PartList_CountHeldBy(i) == 0`): `Actor_AddPart(i, part)`, then `Room_RemoveItem`
unconditionally. When the chimps already hold 2 parts of that type, `Actor_AddPart` refuses and the
part is still removed from the room: it is lost for the rest of the visit (**bug**, §20) [O
2a80:0074..009c, I reachable from level 1 on when two chimps hold parts of one type]. Chimps also
take parts from the player on contact (§11.7). They never give parts back except by dropping them
when stunned.

### 13.8 Chimp counts per room and the music [O]

`g_roomChimpCount` DS:a01c is incremented by `Chimps_Spawn` and by every `Chimp_EnterRoom`,
which also runs for every chimp in the displayed room each time the player enters it
(`Actors_EnterRoom`), and decremented only when a chimp leaves a room. The counts therefore grow
with every visit (**bug**; seen in `w0/14_warehouse`: room 1 counts 2 with one chimp there), so
the chimp voice of the room music (§6.3) stays on in rooms the chimps have left.

---

## 14. The warehouse loop (state 7, 12e0:0619)

### 14.1 Entry [O]

```
cheat = 0; next = 8
g_escHandler DS:8656 = 1a04:0d65 (Hud_EscHook)
g_effectBank DS:7a44 = SndRes_LoadEffectBank(0x1039)
Hud_EscHook()
Hud_Update(0)
Sprites_Reset()
Room_QueueEntryLayers()        # RestoreRect(0,9,320,155); anim 0, layer 1, anim 1, anim 2 (anims tick here)
Actors_ResetCountdowns()       # every actor steps in the next call
leave = Actors_Tick()          # rand: chimp brains, ...
Actors_Draw(); Room_DrawLayer(3); Sprites_DrawAll()
Screen_Show()                  # not faded: Screen_RandomWipe (R(8), R(2)); faded: flush + fade in
g_lastKeyHeldTick = tick
Hint_Queue(0x1048, 0x2238, &DS:458c)      # 60.1 s, cancelled by using a door
```

### 14.2 Frame [O 12e0:069c..07cd]

```
while !leave:
    Hint_Update()                              # may show a dialog (time frozen there)
    g_keyHeldSeen = 0
    Timer_Set(0, 4)
    if Actors_Tick(): leave = 1; break         # no drawing, no wait, no input this frame
    Sprites_EraseAll()
    Room_DrawLayer(1); Room_DrawAnimLayer(0); Room_DrawLayer(2)
    Actors_Draw()
    Room_DrawAnimLayer(3); Room_DrawLayer(3)
    Sprites_DrawAll()
    Hud_Update(0)
    Dirty_Flush()
    Timer_Wait(0)
    code = Input_GetCode()                     # HUD codes 5000.., 6000..6003 are handled inside
    switch code (CS:0808):
      0      : nothing
      0x1b   : Cmd_SaveAndExit                 # the hook has already put back a cursor part
      0x11f  : if cheat == 0: cheat = 1        # Alt+S
      0x111  : if cheat == 1: reset the player # Alt+W, §11.8
      0x17a2 : g_leaveRequest DS:455a = 3; next = 16; leave = 1
      0x17a3 : DS:455a = 2; next = 16; leave = 1
      other  : cheat = 0
```

### 14.3 Exit [O 12e0:07d0..0807]

`if g_playerRoom == 0 or next == 16: Pal_FadeOut()`; free the effect bank (`SndRes_FreeEffectBank`);
`Hint_ClearQueue()`; `Hud_EscHook()`; hook = NULL; return next. Going to another warehouse room is
therefore shown with a random wipe and no fade; going to the workshop fades.

### 14.4 Timing

A frame that fits takes 4 PIT ticks (27.46 ms). Steps: player and chimps every 3 frames, the
projectile every frame, pads per their delay (2 in the data → every 3 frames). The idle hint
0x1048 is due 0x2238 ticks after entry. Long frames: room entry (`Room_Enter`'s picture loads and
`Room_PlaceItems`/`Item_PlaceInRandomRoom` shuffles of 90 cells = 1800 rand calls) [I costs].

---

## 15. States 5, 8, 10, 11, 16

### 15.1 State 5, enter the room (12e0:096a) [O]

`Input_Flush()`; play mode DS:0096 = 1; `Menu_SetMenuBar(DS:4053)`; `Room_Enter(g_playerRoom)`;
`Actors_EnterRoom()`; `g_hudDirty |= 0x8000` (full HUD redraw at the next `Hud_Update`); return 6
for room 0, else 7.

### 15.2 State 8, leave the room (12e0:09aa) [O]

```
result = 5
Menu_SetMenuBar(NULL); Room_Leave(); Actors_LeaveRoom()
if g_puzzleDoorHit (DS:00bc):
    Font_SetColor(0); DS:00bc = 0; result = 9
    SndRes_StopMusic(room music); Pal_FadeOut(); Chimps_FreeSets(); Actors_FreePlayerSets()
if g_vehicleDone (DS:00be):
    result = 10; DS:00be = 0; g_buildingInProgress[b] = 1; g_inBuilding = 1; Players_SaveCurrent()
return result                                # the vehicle wins if both are set
```

### 15.3 States 10 and 11 (hand-off) [O]

State 10 (12e0:0506) copies the indexes of the parts with holder 0 (the player) and flag 0x8000
(built in) into `g_playerVehicle` DS:6616 (−1 terminated), `g_rematch` = 0, returns 0xb.
State 11 (12e0:0564): `Pal_Save`, `Mouse_Hide`, `g_keyHeldSeen` = 1, `Hud_Update(0)`, the race of
the building (`Race_Aircraft` for building 1, else `Race_LandEnergy`) → `g_raceResult` DS:4560;
a win that is not a rematch increments `g_racesWon[b]` and `g_racesWonTotal`; `Mouse_Show`;
returns 0xe after a rematch, else 0xc. Details: `code_vehicles_races.md`.

### 15.4 State 16, save and leave (12e0:012f) [O]

`Players_SaveCurrent()` (saves the building when `g_inBuilding`), `Building_Free()`, return
`g_leaveRequest` DS:455a (2 sign-in, 3 hub). `Building_Free` (12e0:05d5):
`Actors_FreePlayerSets`, `Chimps_Free`, clear the local zones, `Menu_SetMenuBar(NULL)`,
`Room_Leave`, `Rooms_Free`, `Actors_LeaveRoom`, `Parts_FreeCatalogue`, `List_Clear(player
list)` (the carried count +0x70 is not cleared).

---

## 16. rand() in the world, in order

All 13 world call sites of the 18 in the program [O: every `CALLF rand` of the export]:

| Site | Call | When |
|---|---|---|
| 2bd4:0043, 0064 | `Rand_Shuffle`: R(n) twice per swap, 10·n swaps | deck (n = I[0]) and item grids (n = 90 or 21) |
| 12e0:0910 | R(100) | new visit (`g_setupCoin`) |
| 2a80:0875 / 08a8 | R(6) hall floor / R(3) small floor | `Chimps_Spawn`, per chimp |
| 2a80:08ee | R(100) start side | `Chimps_Spawn`, per chimp after its floor |
| 2a80:0212 | R(1000) | `Chimp_Brain`, per firing move entry |
| 1622:0f5b | R(g_roomCount − 1) + 1 | `Item_PlaceInRandomRoom`, per draw (then a grid shuffle) |
| 18f5:01ac | R(+0x70) | `Player_HitByChimp` |
| 1473:0047 | R(8) (as `rand() << 3 / 0x8000`) | wipe pattern (`Screen_RandomWipe`) |
| 1473:0513 | R(2) | wipe sound |
| 174b:00f1 / 0116 | R(b − a) + a | screen-animation ops −34/−35 (not used by the hub's two sets) |

Order within a warehouse frame:

1. `Hint_Update` — none.
2. `Actors_Tick`, actor 0..11 in slot order; per actor: contacts (none), brain (chimps: zero or
   more R(1000), one per probe that fires, in entry order, until one succeeds), then the step
   commands of this frame in step order: player −29 → R(carried) (+ a random-room placement if
   the chimps hold 2 of that type); player −27 when the last banana goes and no crate is left →
   `g_cratesTotal` × `Item_PlaceInRandomRoom(1000)`; chimp −10 on a crate → one
   `Item_PlaceInRandomRoom(1000)`.
3. Drawing — none.
4. `Input_GetCode` — code 6000 (trash can) → `Item_PlaceInRandomRoom(part)`.

At room entry (state 7 entry): the entry `Actors_Tick` as above, then, when the palette is not
faded, R(8) and R(2) of the wipe. At set-up: §4.4. A run replays exactly only with the same seed,
the same input per frame and the same number of frames between events: a walking chimp draws
whenever one of its probes touches an edge, pad, door or item, which happens often, and where it
stands on each frame decides that (the "chimps call rand() every frame" of REFERENCE_CAPTURES
§3.2 is this, not a draw on every frame).

---

## 17. The HUD strip (1a04)

The HUD is specified once, in `code_vehicles_races.md` §4 (the master copy, from the same code):
the layout and counters (§4.1), `Hud_Update` and the dirty bits `g_hudDirty` DS:132c (§4.2: 1
strip, 2 parts needed, 4 score with global effect 1, 8 bananas with global effect 5, 0x10 the
part's text, 0x40 flash, 0x4000 bar, 0x8000 full redraw; then `Gfx_SaveToBack(0, 165, 320, 16)`),
the carried-part strip (§4.3) and the strip commands that `Input_GetCode` consumes (§4.4: 5000 + n
`Hud_SelectPart`, 6000 `Hud_TrashCursorPart`, 6001 `Hud_PutBackCursorPart`, 6002 / 6003 the
arrows). This section keeps only the world's side.

### 17.1 World-side calls [O]

`Hud_DrawAll(7)` at set-up; `g_hudDirty |= 0x8000` at room entry; `|= 1` and `Hud_AddPart` on a
pick-up; `|= 8` on a throw; `Score_Add` sets the score bit (12e0:0ace); `Hud_FlashPart(p, 1)` when
a third part of a type is refused; `Hud_StripAnchorFor` / `Hud_RefreshStrip` when a chimp takes a
part. `g_keyHeldSeen` DS:5c1f = 1 (set by `Input_GetHeldKeys` while a key is held) makes the next
`Hud_Update` put the labels back over the part's text; the warehouse entry clears it.

### 17.2 What the strip commands do to the world [O]

* 6000 (the trash can, warehouse only): the part leaves the player (`Actor_RemovePart(0, p, 1)`),
  is flagged 0x4000 (no more points when picked up again), and `Item_PlaceInRandomRoom(p)` puts it
  in a random room (**rand**, §16); parts needed += 1 if it was the only one of a needed type;
  hint 0x1049.
* 5000 + n and 6002 / 6003 are ignored while a part flashes (DS:874c ≥ 0).

The inventory's use in the workshop (dropping parts on the vehicle) is in
`code_vehicles_races.md` §5.

---

## 18. Sounds, hints and resources used

### 18.1 Sounds [O]

| What | Bank | Trigger |
|---|---|---|
| hub music | 0x1020 (+ FM offset DS:a0d8) | state 3, arrangement 2 (`code_sound.md` §13.3) |
| room music | 0x35f + 2·building | `RoomMusic_*` (§6.3) |
| warehouse effects | `SndRes_LoadEffectBank(0x1039)` → DS:7a44 | actor ops −36/−37 (member a, only in the displayed room); pick-up member 1 (0, 4); wipe members 25/26 (R(2)) |
| HUD effects | DS:9dc0 (loaded elsewhere) | +1 score, +3 trash, +4 select, +5 bananas, +6 scroll |

Effect members used by the shipped sets: player 0, 2, 6, 9..19, 21, 22; projectile 3 (flight),
20 (splat); chimps 7, 8, 15..19, 21..24 [O data].

### 18.2 Hints (`Hint_Queue(id, delay ticks, cancel)`) [O]

| Id | Trigger |
|---|---|
| 0x1044 / 0x104a | a part picked up (not needed / needed) |
| 0x1047 | third part of a type refused |
| 0x1048 | 60 s after entering a warehouse room, cancelled (DS:458c) once a door is used |
| 0x1049 | a part thrown in the trash can |
| 0x104b / 0x1055 | first tube exit (With / Without Chimps), 5 ticks |
| 0x104d | knocked down by a chimp (step −43) |
| 0x1050 | parts needed reaches 0 |
| 0x1051 | the last banana is gone (projectile end or chimp hit) |
| 0x106d / 0x106e | hub dialogs: all races won / building complete (`Dlg_MessageRes`) |

### 18.3 Resource ids [O]

0x1002/0x1004 hub picture and palette, 0xf6 palette, 0x1003 trophies, 0x101f hub cursor labels,
0x1005/0x1007 hub animations (frame bundles 0x1006/0x1008); 0x2f5 + race level description;
0x347..0x34f room sets; 0x350 + race item counts; 0x1033..0x1036, 0x103d, 0x103e, 0x1041, 0x1042
actor sets; sprite bundles 0x1031/0x1032 (player), 0x1037/0x1038 (projectile), 0x103b/0x103c and
0x103f/0x1040 (chimps); 0x30e/0x33c open doors; 0x332/0x333, 0x343/0x344 items; 0x340, 0x345,
0x346 pad and flap bundles; 0x1058, 0x105a HUD; 0x11d6..0x11dd wipe patterns.

---

## 19. Screen animations (174b) and list helpers (19c0)

### 19.1 Screen animations — `ScreenAnim_*` (the former `AnimSeq_*`) [O]

Used by static screens (hub, workshop, attract, results), not by actors. A resource (`S`/`A`) is
loaded as a private copy (`ScreenAnim_Load` 174b:0324) and is itself the running record:

| Off | Meaning |
|---|---|
| +0x00..+0x07 | list node (cleared at load) |
| +0x08 | 1 = finished (removed after the update), 2 = holding |
| +0x0A..+0x17 | sprite entry (flags 0x8000 = hidden) |
| +0x18 | frame bundle id (file) |
| +0x1A, +0x1C | countdown, delay (= first sequence's word 0 at load) |
| +0x1E, +0x20 | base x, y |
| +0x22 | current sequence; +0x24 in the list; +0x26 step |
| +0x28, +0x2A | x wrap range (op −12) |
| +0x2C | number of sequences; +0x2E far self pointer |
| +0x32 | far pointers to the sequences, then the sequences (10-byte header with the count at +8, 10-byte steps as §9.2) |

Per frame (`ScreenAnim_Frame` 174b:0510): `Sprites_EraseAll`, then for each running
animation `ScreenAnim_Step` and `Sprites_Add` unless hidden, removing finished ones,
`Sprites_DrawAll`. `ScreenAnim_Step` (174b:001a): `if --countdown >= 0` nothing; else reload,
then steps (op table CS:024d, 14 entries):

| Op | Effect |
|---|---|
| ≥ 0 | frame: flags = c, x = base x + a, y = base y + b, picture = cache[d], size from the picture; step++; stop |
| −1000 | finished (+8 = 1); stop |
| −45 | busy-waits until effect DS:7a44 + a stops, then step++ |
| −39 | countdown += a·0x92; hidden; step++; stop |
| −37 / −36 | stop / play effect DS:7a44 + a (b >> 8, b & 0xff); step++ |
| −35 / −34 | base y / x = a + R(b − a) (rand); step++ |
| −15 / −14 | base y / x = a; step++ |
| −12 | base x += a, wrapped into [+0x28, +0x2A]; step++ |
| −8 | hidden; step++; stop |
| −4 | step = a |
| −2 | +8 = 2; stop (the step repeats every frame) |
| −1 | step = 0 |
| other negative | re-reads the same step forever (hang) |

`ScreenAnim_PlayModal(done)` (174b:0522) runs 4-tick frames of `ScreenAnim_Frame` + `Dirty_Flush`
until `*done ≠ 0`.

### 19.2 Lists (19c0) [O]

List header `{far head, far tail}` (8 bytes, `List_New` allocates it), node `{far next, far prev,
...}` at the start of the record. `List_Head`, `List_Tail` (+4), `Node_Next`, `Node_Prev`,
`List_GetAt(list, n)` (the tail when n is past the end), `List_InsertAt(list, node, pos)` (0xffff =
append), `List_InsertSorted(list, node, cmp)`, `List_Remove`, `List_RemoveAt`, `List_Count`,
`List_Clear` (does not free nodes), `List_Free` (frees the header only). Users here: room item
lists, carried-part lists, the screen-animation list, the hub hover list. The draw order is not a
list: it is the order of `Sprites_Add` calls (§8.2).

---

## 20. Original bugs and quirks worth keeping

1. Unknown actor and screen-animation opcodes (and an empty sequence) hang the original; none is
   reached by the shipped data (§9.3, §19.1). Engine: assert in debug, skip in release.
2. `Sprites_EraseAll` stops erasing at a 0x80 entry (never set) (§8.1).
3. The player's hall sequences 52/53 write through a NULL layer pointer (§9.3). Engine: ignore.
4. Room music: building 1 starts the transposition cycle with building 0's entry 3 (§6.3).
5. `Room_PlaceItems` never tries the last shuffled cell (§7.3).
6. `Rooms_RestoreState` resets `g_cratesLeft` per room: after a resume it counts only the last
   room's crates, so the refill rule (§7.6) fires earlier or later than intended.
7. Carried counts +0x70 are not reset on a resume (§4.2): after leaving one building and resuming
   another in the same session, the player's and chimps' counts include the old values. Effects:
   a chimp with a stale count never picks up parts (§13.3); `R(+0x70)` in `Player_HitByChimp`
   can start beyond the list, and `PartList_FindHeld` then scans up to 65 536 indexes (the tail
   part is returned when eligible).
8. `g_roomChimpCount` grows at every room display (§13.8): the chimp music voice lingers.
9. Room animations run in rooms 1..3 only (§8.4).
10. Chimps use asymmetric probes (§13.3) and an asymmetric wall sweep (§9.3) — keep for identical
    behaviour.
11. Projectile lifetime: bananas carry over between the races of a building (§4.1).
12. The hub draws the trophies after saving the back page (§3.1): an animation crossing a trophy
    would erase it (none does).
13. `Player_SequenceEnd` / `Chimp_SequenceEnd` / `Room_FindLinkedDoor` have unreachable
    uninitialised or out-of-range results (§11.4, §13.5, §6.5).
14. A chimp that picks up a part of a type the chimps already hold twice destroys it (§13.7):
    the part leaves the room and joins no list. Keep it (it changes which parts remain).

---

## 21. Round 2 observations (REFERENCE_CAPTURES §10.7, §11) [O]

* Without chimps the warehouse frames call `rand()` never; with the 2 chimps of Aircraft level 1,
  11 calls came before the puzzle choice when the door was taken right after loading `w0/15_at_door`
  (§16).
* The item lists match §7 in live memory: room +0x9E count, +0xA0 list, 0x2C-byte nodes (+0x0E x,
  +0x10 y, +0x1A picture 0x343 / 0x332, +0x1E part); box y = 26·floor in halls, 46 / 98 / 150 in
  small rooms.
* Springs are type-4 objects with the landing point at +0x18 / +0x1A; those with +0x12 = 1 are not
  drawn. A door's twin is the target room's door with the same link id; a solved door is open from
  both sides.
* Walk about 16 px/s, run (Ctrl) about 54 px/s; items are obstacles the player can stand on; Down
  while overlapping an item picks it up; a third part of a category stays (hint 0x1047).
* An Up still held when the player arrives through a door takes him back through it as soon as
  another arrow is pressed (§11).
* A resume restores the room and position with no prompt (§4); the HUD strip's order differs after
  it (§20 bug 7 and the pair rebuild, `code_vehicles_races.md` §3.6).

---

## 22. Porting notes [R]

* Read every table from the user's files at run time: the room sets, actor sets, level
  descriptions and item counts from the archives; from `ssg.exe`'s DGROUP the state table
  (DS:00c2), hub zones (DS:0132), room-set ids (DS:09e8), tube rooms (DS:00b2), music tables
  (DS:010e, DS:0a06), stun loops (DS:0a90), the player transitions (DS:127e, 68 near pointers into
  DGROUP lists), the contact lists (DS:13a0/13b6/13cc), the chimp lists (DS:1bae far pointers →
  DGROUP), the chimp move and transition indexes (DS:1d2e, DS:1f10, 42 near pointers each) and
  the HUD tables (DS:131a, DS:1330, DS:135c, DS:1384). Near pointers are DGROUP offsets; far
  pointers carry DGROUP's image segment 0x0ea7.
* Keep the original data structures' semantics, not their memory layout, except where the save
  block needs bytes: the block (§4.2) is written byte for byte from the actor fields +0x14..+0x41,
  the part holders and flags, the door bytes and the item triples.
* Mutable copies: the 286 halving writes into the chimp list (DGROUP data); the engine must keep a
  mutable copy of DS:1adc..1bae for the session and halve the first room word on every set-up
  when `cpu = 286` (PORTING_NOTES §5).
* `rand()` must be one Borland LCG shared by everything (`rtl/borland_rand`), with every call of
  §16 in the same order; include the discarded calls (resume, Without Chimps) and the wipe.
* Model the frame exactly as §14.2, including the frame without drawing and without input when
  the player leaves, and the entry tick before the wipe.
* Clamp and guard where the original reads through NULL or past arrays (bugs 3, 13, the room-0
  item list) without changing the visible behaviour.

---

## Open questions

1. *Answered by `code_sound.md` §13.3 and §14.1:* an arrangement unmutes every channel and then
   mutes the channels of the bank's n-th `M` list; the room music's cue transposes the next pass
   by the building's step value, so the music changes key every pass (with two original bugs:
   the start transpose stride and the speaker Aircraft music that never reaches its cue).
2. The roles of the hall-only player sequences 54..67 and chimp sequences 36..41 are inferred
   from data (pads, flaps); a capture of a chimp or the player using a launch pad would confirm
   them.
3. Type-4 objects' +0x12 (0/1) and +0x14, the wall records' flags, the level description's
   +0x04 and +0x0C..+0x24: not read by the world code; maybe by the workshop or unused. (Round 2:
   springs with +0x12 = 1 are not drawn, `REFERENCE_CAPTURES.md` §11; whether they act was not
   tried. The catalogue's +0x04 is unread, `code_vehicles_races.md` §3.1.)
4. *Answered by `code_puzzles_a.md` §1.5-§1.6:* DS:b144 = 1 after Enter or a click on the panel
   of a solved puzzle, 0 after Go Back; leaving through the menu keeps the previous puzzle's value
   (an original quirk).
5. The stun duration in frames per level (§13.6) should be measured in a capture.
6. Long-frame costs of room entry and item placement on the reference machine (PORTING_NOTES
   §2.4).
7. Whether `Hud_SelectPart` in the warehouse has any use besides the trash can and the text.
