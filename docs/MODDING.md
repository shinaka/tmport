# Modding the TM Port

The engine reads every picture, sound, text, animation and table from your own copy of
*Treasure Mountain!* at run time. A **mod** is a folder of replacement files in friendly
formats (PNG, WAV, UTF-8 text, JSON, MIDI). Before the engine reads a resource from the game's
DLLs or a table from `TMTWINCD.EXE`, it looks in the installed mod folders first.

Nothing in a mod is ever written back into the game files. Remove the mod folder and the game is
exactly as before.

Contents: [Where mods live](#where-mods-live) · [Folder layout](#folder-layout) ·
[mod.json](#modjson) · [Pictures](#pictures-pmap-png) · [Sounds](#sounds-ssnd-wav) ·
[Text](#text-othr-txt) · [Animations](#animation-scripts-dam-damtxt) ·
[Bundles](#bundles-bndl-json) · [Font, palette, binary data](#font-palette-and-binary-data) ·
[Music](#music-midi-mid) · [Tables](#tables-tables-json) ·
[Constraints](#constraints-a-mod-must-respect) · [Tools](#tools) ·
[How it works](#how-it-works) · [Limits](#limits)

## Where mods live

Mod folders are listed in the `[mods]` section of `tmport.ini` (next to `tmport.exe`, or in the
preference directory, `%APPDATA%\TMPort\TMPort\` on Windows):

```ini
[mods]
enabled = true                      ; false: load no mods at all
paths = mods\Retro ; D:\My Mods\Big ; ';'-separated; relative paths start at tmport.ini's folder
```

Without a `paths` line the engine uses two folders, in this order:

1. `<preference directory>\mods`
2. `<directory of tmport.exe>\mods`

Each of these is **one mod folder** (it directly contains `PMAP\`, `SSND\`, ... ). Folders that do
not exist are skipped.

**Precedence:** later folders override earlier ones. When two folders replace the same resource,
the one listed later wins; the log names the file that was overridden.

Command line:

| Option | Effect |
|---|---|
| `--mods <dir>` | adds a mod folder after the configured ones (highest precedence); may be repeated. Works for the game and for every tool that reads the game files (`--scene`, `--screen`, `--room-run`, the `--render-*` and `--dump-*` tools, ...), except `--info`, `--export-mod-kit` and the MIDI/sound tools. The tools load only the folders named with `--mods`, not `tmport.ini`'s |
| `--no-mods` | the game loads no mods |

## Folder layout

Folder and file names are case-insensitive. `<id>` is the resource id in decimal.

| File | Replaces | Format |
|---|---|---|
| `mod.json` | – | optional manifest ([below](#modjson)) |
| `PMAP\<id>.png` | 256-colour picture (`TMT256.DLL`) | PNG |
| `PMAP16\<id>.png` | 16-colour picture (`TMT16.DLL`, `--16`) | PNG |
| `SSND\<id>.wav` | sound | WAV (PCM) |
| `OTHR\<id>.txt` | text resource | UTF-8 text with `{escapes}` |
| `OTHR\<id>.bin` | any other OTHR resource (binary tables) | raw bytes |
| `DAM\<id>.dam.txt` | animation script (an OTHR id) | DAM assembler text |
| `BNDL\<id>.json` | bundle (list of resource ids) | JSON |
| `FFNT\<id>.ffnt` | the font | binary FFNT |
| `RRGB\<id>.pal` | the palette | 768 bytes |
| `MIDI\<id>.mid` | music, both MIDI sets | Standard MIDI File |
| `MIDI\<id>.e.mid`, `MIDI\<id>.b.mid` | music, only the `E*.MID` / `B*.MID` set | Standard MIDI File |
| `TABLES\*.json` | tables of `TMTWINCD.EXE` | JSON |

Other files at the top level (a `README.txt`, say) are ignored; unknown sub-folders and badly named
files are reported in the log. If one folder holds several files for the same OTHR id, a
`DAM\<id>.dam.txt` wins over `OTHR\<id>.txt`, which wins over `OTHR\<id>.bin`.

## mod.json

```json
{
  "name": "Night Sky",
  "newIds": [2500, "2600-2610"],
  "resizable": [2100]
}
```

* `name`: shown in the log (default: the folder name).
* `newIds`: ids that do not exist in the game and that this mod adds. A file for an id that is
  neither in the game nor listed here is refused, so a typo (`10000.png` for `1000.png`) does not
  go unnoticed.
* `resizable`: existing pictures whose size may change and existing bundles whose member count
  may change (see [Constraints](#constraints-a-mod-must-respect) for why that is risky).

## Pictures: PMAP (PNG)

Pictures are palette-indexed: every pixel is a number into the game palette (RRGB 900, 256
colours). The colour **(0, 0, 170)** is the transparent colour key of sprites.

* **Indexed PNG with the game palette** (what `--export-mod-kit` writes): the indices are used as
  they are. This is the exact form; edit such files in a paint program that keeps the palette
  (indexed / "palette" mode). The game palette contains duplicate colours (for example two
  entries of the key colour), and only the index tells them apart.
* **Any other PNG** (RGB, RGBA, grey, or indexed with another palette): pixels with alpha below
  128 or with the colour (0, 0, 170) become transparent; every other pixel gets the nearest
  palette colour (smallest squared RGB distance, the lowest index on ties). The key colour is
  never chosen for an opaque pixel. The log says how many pixels were not exactly in the palette.
* The picture must have the **same width and height** as the one it replaces, unless its id is in
  `resizable`. The game positions, centres and hit-tests many pictures by their size.
* 256-colour art is stored like the original (run-length "RUND" compressed, or raw 8 bits per
  pixel for the few originals stored raw); new ids are compressed.
* **16-colour mode** (`--16`): `PMAP16\<id>.png` holds the 16-colour version (the 16 Windows VGA
  colours; key colour = palette entry 3, (0, 0, 170)). Without it, `PMAP\<id>.png` is mapped to
  the 16 colours by the same nearest-colour rule.

Interlaced PNGs, 1–16 bits per sample and all PNG colour types are read. Pictures up to
4096 × 4096 are accepted.

## Sounds: SSND (WAV)

Any uncompressed WAV: 8, 16, 24 or 32-bit integer PCM or 32-bit float, mono or stereo (more
channels are mixed down too), any sample rate. The engine converts it to its own format, 8-bit
unsigned mono at the rate of the sound it replaces (22050 Hz for every sound of the game; new ids
also use 22050 Hz): linear interpolation when the rate goes up, averaging when it goes down. An
8-bit mono file at that rate is used unchanged.

Sound ids: effects 7000–7099 (an animation's `sound n` plays 7000 + n), hint voices 7100–7133,
title narration 7150–7159, praise / wrong-answer / clue-word phrases 7300–7552, question
narration 4000–6615.

## Text: OTHR (.txt)

Text resources (questions, hints, help and story pages) are byte strings in the game font's
encoding with layout codes. In a `.txt` file they are written as UTF-8 text with `{escapes}`:

| In the file | In the game | Meaning |
|---|---|---|
| ordinary characters | ASCII | printable ASCII as is |
| `{{` | `{` | a literal brace (a lone `}` needs no escape) |
| a line break | CR | new line (CR LF, LF or CR in the file all read as one) |
| `{lf}`, `{tab}` | LF, TAB | (a literal tab character also works) |
| `{margin}` | FE | back to the left margin without a new line (two-column layouts) |
| `{center}` | FF 05 FF | centring on/off |
| `{ul}` | FF 02 FF | underline on/off; underscores are not drawn while underlining |
| `{color N}` | FF 03 N+1 FF | pen colour EGA N (0 black, 4 dark red, ...) |
| `{note text}` | FF 06 text FF | a comment the renderer skips (the questions' level tags) |
| `{arg n}` | FF 01 n FF | caller-supplied string n (unused by the game) |
| `{tabs a b ...}` | FF 04 a b ... FF | tab stops (unused by the game) |
| `{ff OP HH ...}` | FF OP HH ... FF | any other control code, hex bytes; `{ff}` is FF FF |
| `{left}` `{right}` `{up}` `{down}` | 80–83 | arrow-key glyphs |
| `{spacebar}` | 84 85 | the two-glyph Spacebar key (`{spacebar-1}`, `{spacebar-2}` for the halves) |
| `{esc}` `{enter}` `{ctrl}` `{delete}` `{tab-key}` | 86, 8B, 8C, 93, 8D | key glyphs |
| `{return}` `{backspace}` `{spacebar-alt}` `{return-alt}` | 87 88, 89 8A, 8E 8F, 94 95 | two-glyph keys (halves `-1`/`-2`) |
| `{esc-alt}` `{enter-alt}` `{tab-key-alt}` `{r-key}` `{s-key}` `{r-key-alt}` `{cmd}` `{cmd-alt}` | 92, 91, 90, 06, 17, B7, 99, B6 | further key glyphs |
| `{xHH}` | byte HH | any other byte (not 00) |
| `{end}` | – | end of the text; anything after it (an editor's final newline) is ignored |

Spaces at the end of a line are part of the text (a few answers end with one); an editor set to
trim trailing whitespace changes them.

Non-ASCII characters you type are mapped to the font's Mac Roman positions (0x96–0xFD), e.g.
`ñ` → 0x96, `“` → 0xD2; a character the font does not have is an error (use `{xHH}`).
The exporter writes only ASCII and escapes and ends each file with `{end}`, so an exported file
imports back to exactly the same bytes.

An original example, a question (the format is explained under
[Constraints](#constraints-a-mod-must-respect)):

```text
{center}{color 0}{note L=1}
Which of these can {color 4}fly{color 0}?


A bird
A stone
A shoe
{end}
```

`OTHR\<id>.bin` replaces a resource with raw bytes (the layout tables 10550–10553 and the
picture-attribute table 10555 are binary).

## Animation scripts: DAM (.dam.txt)

Animations (OTHR 20000–20308) are small byte-code scripts. `DAM\<id>.dam.txt` is a text
assembler for them: one statement per line, `;` starts a comment, `name:` defines a label,
numbers are decimal or `0x` hex, negative values allowed for signed fields.

| Statement | Byte code | Meaning |
|---|---|---|
| `tag DAM` / `tag DOP` / `tag b0 b1 b2 b3` | 4 bytes | header, first statement (`DOP`: the channel's drawing skips dirty-rectangle tracking) |
| `sprite dx dy frame [mode]` | FD i16 i16 u8 u8 | draw frame `frame` of the animation's picture set at (x + dx, y + dy); mode 0 normal |
| `tick` | FE | end of this channel's step: the queued sprites appear, wait for the next tick |
| `end` | FF | end of the animation (the channel is freed) |
| `loop target count [counter]` | FA i16 u8 | jump to `target` (label or offset) `count` times in total (1–127), or `forever`; two counters (0, 1) allow nesting |
| `sound n [pad]` | FC u8 u8 | play sound effect SSND 7000 + n |
| `stopsound [pad]` | FB u8 | stop the sound effect |
| `update dx dy w h` | F9 i16 u8 i16 u8 | present a screen rectangle immediately |
| `byte b ...` | raw | raw bytes (00–F8 are one-byte no-ops when executed) |
| `size n` | – | pad the resource to n bytes (default: the next multiple of 512) |

An original example: a two-frame blink that repeats until the game stops it, with a sound on the
first pass:

```text
tag DAM
    sound 3            ; SSND 7003
start:
    sprite 0 0 0       ; frame 0 of the picture set
    tick
    tick               ; hold it for two ticks
    sprite 0 0 1
    tick
    loop start forever
    end                ; reached only when the game stops the animation
```

The assembler refuses scripts the engine cannot run: no `tag`, no `end` and no endless loop, a
loop target outside the script, or a backward loop whose body has no `tick` (the animation engine
would never finish that step). A `sound n` whose SSND 7000 + n does not exist is reported. The
exporter writes labels for loop targets and the original byte offsets as comments; any bytes after
the first `end` are kept (marked as never executed) so the round trip is exact.

The title and cut-scene animations are member 0 of their bundle and the rest of the bundle is
their picture set (`frame 0` is member 1); the treasure animations 20000 + n use the pictures of
bundle 330 + n. Timing (ticks per second) and the position come from the game code.

## Bundles: BNDL (.json)

A bundle is an ordered list of resource ids:

```json
{"members": [1300, 1301, 1302]}
```

A plain array `[1300, 1301, 1302]` works too. Members that exist neither in the game nor in a mod
are reported. See [Constraints](#constraints-a-mod-must-respect) for member counts.

## Font, palette and binary data

* `FFNT\999.ffnt`: the font, in the game's binary FFNT layout (byte 1: last character, byte 2:
  height, then the width, offset-low and offset-high tables and the 1-bit glyph bitmaps). It is
  checked by parsing it. Line height and glyph widths drive all text layout, so a new font
  re-flows text.
* `RRGB\900.pal`: the palette, exactly 768 bytes (256 × R, G, B). Keep an entry (0, 0, 170): it is
  the transparent colour. All pictures are composed in palette indices, so a new palette recolours
  everything; PNGs of the same mod are matched against the new palette.

## Music: MIDI (.mid)

The ten tunes have ids 9000–9009 (`DS:09fa`; e.g. the title, the club house, the three mountain
levels, the castle). `MIDI\<id>.mid` replaces the tune in both sets; `<id>.e.mid` and `<id>.b.mid`
replace only the General MIDI set (`E*.MID`, the default) or the `B*.MID` set and win over
`<id>.mid` of the same folder. A file must be a Standard MIDI File that plays at least one note.
Whether a tune loops is decided by the game.

## Tables: TABLES (.json)

Each `.json` file in `TABLES\` is an object keyed by table name; every key is optional and keys
starting with `_` are ignored (use them for notes). Files are applied in name order, folders in
precedence order. A file is checked completely before anything is changed: one bad value and the
whole file is ignored (the log names the exact place, e.g. `rooms[12].exitLeft`).

An array must have the table's length (`null` keeps an element); an object `{"index": value}`
changes single elements:

```json
{
  "_note": "an original example",
  "gemsNeeded": [null, 4, null, null],
  "rooms": {"9": {"music": 3}},
  "clueWords": {"noun": {"1": "gizmo"}, "nounPlural": {"1": "gizmos"}}
}
```

| Table | Where | Value |
|---|---|---|
| `rooms` | DS:23d8, 50 records | objects with any of: `startX`, `startY` (entry position), `startFrame`, `startFacingLeft`, `exitLeft`, `exitRight`, `exitDoor` (room 0–49, 255 = wall / no door), `typeLeft`, `typeRight`, `typeDoor` (transition types), `doorX` (−100 = no door), `bgPmap` (background picture, 1000–3999), `level` (0–2 mountain, 3 castle wall), `music` (zone: 0 club house, 1/3/5 the levels, 7 the castle); `unused09`, `unused0D`, `unused10` exist for an exact copy |
| `rankThresholds` | DS:0ea0 | 8 numbers, score needed for each rank; element 0 must be 0, never decreasing |
| `rankNames` | DS:0fa8 | 8 strings |
| `gemsNeeded` | DS:0ec3 | 4 numbers 1–5, treasures per climb by rank tier |
| `gemX`, `gemY` | DS:0ec7, DS:0eef | 4 × 5 numbers, positions of the treasure icons on the status panel by tier |
| `elfCountByTier` | DS:128e | 4 numbers 0–32, elves per level by tier (elves 0–2 of a level carry its three questions, so fewer than 3 is reported) |
| `hintBit` | DS:13d0 | 34 numbers: the "already seen" bit (0–31) of each hint message, −1 = shown every time |
| `clueWords` | DS:10f9, 1119, 112d, 1079, 10b9 | object of string lists `count` (8), `size` (5), `shape` (5), `noun` (16), `nounPlural` (16); element 0 of each list is unused |

Strings use the [text escapes](#text-othr-txt). Strings may be longer than the originals. The
rooms' run-time fields (hiding places and their contents) are not table data. The spoken clue
words follow the original word lists (SSND 7300 + offset), so changed words are shown but the
narrator still says the original ones unless the sounds are replaced as well.

## Constraints a mod must respect

The game code addresses everything by number, so a mod has to keep the numbers the code expects.
The engine checks these rules when it loads a file and logs a clear message; a file that breaks a
rule is ignored and the game's own resource is used.

* **The id range decides the type**: bundles 0–899, palettes 900–949, fonts 950–999, pictures
  1000–3999, sounds 4000–9999, other data 10000–32767. A file in the wrong folder for its id is
  refused. New ids need `newIds` in `mod.json` (and something that uses them: a bundle, an
  animation's `sound n`, a table).
* **Picture sizes** stay those of the original (`resizable` lifts this per id): player frame
  widths, the sign-in list rows, hit tests and centring all use them.
* **Bundle member counts the code indexes**: the player frames (BNDL 300) exactly 69, the status
  panel (301) exactly 10, the net-shop sign (302) exactly 6, the elf animation sets (311–320) 1 to
  10 frames, the treasures (330–380) 1 to 10 frames, the hint texts (327) at least 33, the
  fountain (328) any number; the animation bundles (323–325, 400–402, 410, 412) need their script
  (an OTHR id) as member 0. Other existing bundles keep their member count unless `resizable`.
* **Question grammar** (OTHR 11000–11358): optional codes, a line break, the question lines (one
  empty line may separate stanzas), then **two empty lines** (three line breaks in a row end the
  question), then **exactly three answers**, one per line, without codes. **The first answer is
  the correct one**; the game shuffles their order on screen. At most 10 lines in all.
* **Voice tables**: the narration of question *q* is a run of sounds starting at the id in the
  table at DS:1990, one clip per text line and then one per answer. If a question gets a different
  number of text lines, the engine warns: with Voice on the wrong clips would be spoken. Keep the
  line count, or replace the question's sounds too (or play with Voice off).
* **Animations** must be runnable (see [DAM](#animation-scripts-dam-damtxt)) and may only use
  frames their bundle has.
* **Palette**: keep the colour key (0, 0, 170).

## Tools

```
tmport --export-mod-kit <dir> [--force] [--no-verify]
tmport --check-mods [<dir> ...] [--compare]
```

`--export-mod-kit` writes **your** game's assets into `<dir>` in all the formats above, laid out
like a mod folder, with a `README.txt` and an empty `mod.json`: every picture (both colour sets),
sound, text, animation, bundle, the font, the palette, the twenty MIDI files and the tables. Each
file is imported again and compared with the original bytes; the summary reports the round trip
(`import(export(x)) == x` holds for every resource). The directory must be empty unless `--force`
is given. **The kit is your private copy of the game's data: do not share or publish it.**

To make a mod, create a new folder in a mod directory and copy into it only the files you change,
in the same sub-folders. Then run `--check-mods` on it: it converts and checks every file and
lists all problems (exit code 1 if there are errors). `--compare` also compares what the mod
serves with the game's own data, which shows which files actually change something (an unedited
kit installed as a mod compares identical everywhere).

`--headless-frame <out.png> --mods <dir>` composes a test scene with the mod's pictures without
opening a window; `--dump-pmap <id> <out.png> --mods <dir>` shows one converted picture.
`--scene`, `--screen` and `--room-run` take `--mods` too, so a modded scene or room can be rendered
to PNG frames without playing (see `docs/BUILDING.md`, "Command-line tools").

## How it works

* At start-up the engine scans the configured folders, applies the table files to its copy of the
  EXE tables, puts the MIDI files in front of the game's `SOUNDS\` folder and installs an index of
  the resource overrides (`res/mod_overrides.h`, `src/mods/`).
* A resource override is converted to the game's binary format the first time it is used, padded
  like the game's DLL resources (to a multiple of 512 bytes), validated and cached. The game code
  cannot tell it from an original.
* Problems never stop the game: the log names the file and the rule, and the original data is
  used. `--verbose` also logs every override as it is loaded.

## Limits

Not supported yet: high-resolution replacement art (the logical size is fixed), pre-rendered
music (OGG), changing tables other than those listed (menus, dialog texts, voice-id and animation
tables), and adding new rooms or questions beyond the game's fixed counts.
