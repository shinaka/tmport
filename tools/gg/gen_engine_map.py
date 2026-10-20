"""Generates the function table of re/gg/docs/ENGINE_MAP.md ("Function map"): every function of
re/names/gg's tables -> the engine file and function whose comment carries its address -> the area that
owns it -> its status. Prints the per-table counts; writes the Markdown table to OUT.

    python -P tools/gg/gen_engine_map.py OUT.md [REPO]

The engine function is found from the `// SSSS:OOOO Name` comments in src/gg (and src/rtl, src/dos): the
first declaration or definition after the comment. Status:
  done        ported (a wave 2 module or a wave 3 area);
  partial     ported with a part left out (the note says which; PARTIAL below);
  stub        the body still says TODO(wave3);
  absorbed    no engine function of its own (the note says where its work is; ABSORBED below);
  not ported  no engine code names it.
Owners: the wave 3 areas by their cmake/gg_sources_<area>.cmake lists, the wave 2 files they extend by
ENGINE_MAP.md "Areas, owners and files", everything else "wave 2".
"""
import os
import re
import sys

OUT = sys.argv[1]
ROOT = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

AREAS = ['skeleton', 'flow', 'ui3', 'world3', 'actors', 'puzzles_a', 'puzzles_b', 'vehicles']
AREA_NAME = {'ui3': 'ui', 'world3': 'world'}

owner_of = {}
for a in AREAS:
    for line in open(os.path.join(ROOT, 'cmake', f'gg_sources_{a}.cmake'), encoding='utf-8'):
        line = line.strip()
        if line.startswith('src/'):
            owner_of[line] = AREA_NAME.get(a, a)

# Wave 2 files that a wave 3 area extends (ENGINE_MAP.md "Areas, owners and files").
WAVE2_OWNER = [
    (r'src/gg/game/(building|rooms|parts|hud|pic_cache|screen_anim)\.', 'world'),
    (r'src/gg/game/actors\.', 'actors'),
    (r'src/gg/game/(puzzles|pz_electric|pz_energy|pz_launch|pz_simple)\.', 'puzzles_a'),
    (r'src/gg/audio/pz_snd\.', 'puzzles_a'),
    (r'src/gg/game/(pz_pieces|pz_gears|pz_balance|pz_magnets)\.', 'puzzles_b'),
    (r'src/gg/game/(blueprint|rank)\.', 'vehicles'),
    (r'src/gg/game/(hub|attract)\.', 'flow'),
    (r'src/gg/ui/attract', 'flow'),
    (r'src/gg/ui/', 'ui'),
    (r'src/gg/app/(gg_app|preview|seq_run)\.', 'flow'),
]


def owner(path):
    if path in owner_of:
        return owner_of[path]
    for rx, o in WAVE2_OWNER:
        if re.match(rx, path):
            return o
    return 'wave 2'


ADDR = re.compile(r'\b([0-9a-f]{4}):([0-9a-f]{4})\b(?:\s+([A-Za-z_][A-Za-z0-9_]*))?')
DECL = re.compile(r'([A-Za-z_][A-Za-z0-9_]*(?:::~?[A-Za-z_][A-Za-z0-9_]*)?)\s*\(')
KEYWORDS = {'if', 'for', 'while', 'switch', 'return', 'static_cast', 'reinterpret_cast', 'const_cast', 'sizeof',
            'decltype', 'catch', 'alignof', 'defined', 'operator'}


def code_part(line):
    i = line.find('//')
    return line if i < 0 else line[:i]


def decl_name(code):
    for m in DECL.finditer(code):
        n = m.group(1)
        if n.split('::')[-1] in KEYWORDS:
            continue
        return n
    return None


refs = {}  # addr -> list of (file, symbol, stub)
for base in ('src/gg', 'src/rtl', 'src/dos'):
    for dirpath, _, files in os.walk(os.path.join(ROOT, base)):
        for fn in sorted(files):
            if not fn.endswith(('.h', '.cpp')):
                continue
            path = os.path.join(dirpath, fn).replace('\\', '/')
            rel = os.path.relpath(path, ROOT).replace('\\', '/')
            lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
            for i, line in enumerate(lines):
                ci = line.find('//')
                if ci < 0:
                    continue
                ms = list(ADDR.finditer(line[ci:]))
                if not ms:
                    continue
                code = code_part(line).strip()
                sym = decl_name(code) if code else None
                if code and not sym:
                    mm = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:\{[^}]*\}|=[^;]*)?;\s*$', code)
                    sym = mm.group(1) if mm else None
                j = i + 1
                if not sym and not code:
                    while j < len(lines) and j < i + 14:
                        c = code_part(lines[j]).strip()
                        if c:
                            sym = decl_name(c)
                            break
                        j += 1
                stub = bool(sym) and fn.endswith('.cpp') and 'TODO(wave3)' in '\n'.join(lines[j:j + 6])
                for m in ms:
                    refs.setdefault(f'{m.group(1)}:{m.group(2)}', []).append((rel, sym, stub))

TABLES = [
    ('game_flow.tsv', 'Game flow, start-up, states 1..17 of 12e0, the workshop, results (game_flow.tsv)'),
    ('game_world.tsv', 'World: rooms, parts, actors, player, chimps, collision, HUD (game_world.tsv)'),
    ('game_puzzles.tsv', 'Puzzles (game_puzzles.tsv)'),
    ('ui_tlc.tsv', 'TLC UI library, sign-in, PLAYERS.DAT, hints (ui_tlc.tsv)'),
    ('engine_lowlevel.tsv', 'Engine low level (engine_lowlevel.tsv)'),
    ('engine_sound.tsv', 'Sound (engine_sound.tsv)'),
]

# Ported with a part left out (ENGINE_MAP.md "Known gaps").
PARTIAL = {
    '1834:08d7': 'its two stores into the part record nothing reads (+0x8A = 0, +0x1A = 0x2238) are not modelled',
    '2caa:004f': 'the creation while g_rosterIndex is not 0 (the file deleted during a session) is logged, not modelled',
}

# Functions without an engine function of their own: where their work is.
ABSORBED = [
    (r'19c0:', 'std containers in the owners (the lists hold no state of their own)'),
    (r'1536:', 'C++ ownership (Mem_Alloc cannot fail; the heap checks have nothing to check)'),
    (r'1454:01[9a-f][0-9a-f]', '`input/keyboard.cpp` GgKeyboard (the ISR’s make / break table as one switch)'),
    (r'2b5a:0(04c|0db|10a|156|189)', '`save/players_store.cpp` PlayersStore (file I/O, the write error)'),
    (r'2caa:13f6', '`ui/signin.h` SignIn’s destructor (ui)'),
    (r'303a:0000', '`app/gg_app.cpp` start-up errors (flow)'),
    (r'(12e0:0b92|1399:000e|1473:0103|2c08:0000|1d3c:0027|1d3c:004d)', 'the host layer (no DOS video mode, DCC, DOS strings or far pointers)'),
    (r'14ff:0(00d|0fe|13d|209)', '`data/archive.h` Archive (resources read once, copies are spans)'),
    (r'14ff:02(26|84)', 'in-memory copies (the blueprint’s save-under: gizmo004.dat is not written)'),
    (r'15df:0(00d|03e|07d)', '`game/pic_cache.h` PicCache (its map and the mirrored slot table replace the lookups)'),
    (r'17ab:0140', '`gfx/cursor.h` (the cursor owns its picture)'),
    (r'1b48:02aa', '`data/lzh.h` (the decoder; the key drain is flow’s installLzhKeyDrain)'),
    (r'2c08:00(11|32)', '`app/options.h` (the mouse is always there; the CPU class is an option)'),
    (r'1bb4:0(198|1a9|1c7|1db)', 'MPU-401 output is not ported (OPL and the speaker are)'),
    (r'1bb4:0(41d|43a|461|48f|4a0|5a3|5f1|a1f)', 'PSG (Tandy) output is dead code in the shipped game'),
    (r'1bb4:(0f70|0fb2|11a2|11df|1225)', 'dead or without caller (`audio/snd_driver.h`)'),
    (r'1cdd:05ef', 'an empty function (the sign-in calls it): nothing to port'),
    (r'2bd4:00c1', 'Zone_SetFromObject: the owners build the zone from the object (world, vehicles)'),
    (r'2bd4:01(7e|c1)', 'pictures are bound when a set is loaded (`game/screen_anim.h`, `game/actors.h`)'),
    (r'1a04:0166', '`game/hud.cpp` (inside the HUD’s drawing)'),
    (r'1834:000b', 'no caller'),
    (r'3083:00(00|2b|7c|d5)', '`game/pz_electric.cpp` (the path search of PzElectric::isSolved)'),
    (r'(33cd:08(79|d1)|3938:0(ce7|d3f))', 'std::string (the overlays’ private string copies)'),
    (r'2b89:007a', '`text/text.cpp` (measure and draw as Text_Measure / Text_DrawBox)'),
    (r'4140:00a9', '`ui/text_viewer.h` TextViewer (ui)'),
    (r'3d39:0(000|049|11c|13a|323|37e|4ae|4f9|553|983)', 'no caller in the shipped game (`game/dam.h`)'),
    (r'411d:0(1ec|1f8|204|210)', '`ui/help.cpp` (one topic table for the four Help commands)'),
]


def norm(x):
    return re.sub(r'[^a-z0-9]', '', x.lower())


def similarity(orig, sym):
    if not sym:
        return 0
    last = norm(sym.split('::')[-1])
    o = norm(orig)
    if last == o:
        return 3
    if '::' in sym and norm(sym.replace('::', '')) == o:  # PzBalance_SelectHookZones ~ PzBalance::selectHookZones
        return 3
    if len(last) > 5 and (last in o or o.endswith(last)):
        return 2
    return 1


def rank(orig, r):
    rel, sym, _ = r
    sim = similarity(orig, sym)
    return (sim >= 2, rel in owner_of, sim, rel.endswith('.cpp'))


def pick(orig, rs):
    """The engine function that is the original: wave 3 files first, then the closest name, then a
    definition; and the best wave 2 reference besides it when the first is a wave 3 file."""
    good = [r for r in rs if r[1]]
    if not good:
        return None, None
    good.sort(key=lambda r: rank(orig, r), reverse=True)
    first, second = good[0], None
    if first[0] in owner_of:
        last = first[1].split('::')[-1]
        w2 = [r for r in good if r[0] not in owner_of and similarity(orig, r[1]) >= 2 and r[1].split('::')[-1] != last]
        if w2:
            second = w2[0]
    return first, second


def absorbed(addr):
    for rx, note in ABSORBED:
        if re.match(rx, addr):
            return note
    return None


out = []
totals = {}
for tsv, title in TABLES:
    rows = []
    for line in open(os.path.join(ROOT, 're', 'names', 'gg', tsv), encoding='utf-8'):
        parts = line.rstrip('\n').split('\t')
        if len(parts) >= 2 and re.match(r'^[0-9a-f]{4}:[0-9a-f]{4}$', parts[0]):
            rows.append((parts[0], parts[1]))
    out += [f'#### {title}', '', '| Original | Engine | Owner | Status |', '|---|---|---|---|']
    st = {}
    for addr, name in rows:
        rs = refs.get(addr, [])
        r, second = pick(name, rs)
        if r is not None and similarity(name, r[1]) < 2 and absorbed(addr):
            r = None
        if r is None:
            note = absorbed(addr)
            if note:
                out.append(f'| `{addr}` {name} | {note} | — | absorbed |')
                st['absorbed'] = st.get('absorbed', 0) + 1
            else:
                files = sorted({x[0] for x in rs})
                where = ('mentioned in ' + ', '.join(f'`{f}`' for f in files[:2])) if files else '—'
                out.append(f'| `{addr}` {name} | {where} | — | not ported |')
                st['not ported'] = st.get('not ported', 0) + 1
            continue
        rel, sym, _ = r
        last = sym.split('::')[-1]
        if rel in owner_of and any(x[2] for x in rs if x[1] and x[1].split('::')[-1] == last):
            status = 'stub'
        elif addr in PARTIAL:
            status = f'partial: {PARTIAL[addr]}'
        else:
            status = 'done'
        key = status.split(':')[0]
        st[key] = st.get(key, 0) + 1
        cell = f'`{rel.replace("src/gg/", "")}` `{sym}`'
        if second:
            cell += f'; wave 2: `{second[0].replace("src/gg/", "")}` `{second[1]}`'
        out.append(f'| `{addr}` {name} | {cell} | {owner(rel)} | {status} |')
    out.append('')
    totals[tsv] = st
open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
keys = ['done', 'partial', 'stub', 'absorbed', 'not ported']
print('| Table | Functions | ' + ' | '.join(k.capitalize() for k in keys) + ' |')
print('|---|---|' + '---|' * len(keys))
grand = {k: 0 for k in keys}
for tsv, st in totals.items():
    n = sum(st.values())
    for k in keys:
        grand[k] += st.get(k, 0)
    print(f'| `{tsv}` | {n} | ' + ' | '.join(str(st.get(k, 0)) for k in keys) + ' |')
print(f'| all | {sum(grand.values())} | ' + ' | '.join(str(grand[k]) for k in keys) + ' |')
