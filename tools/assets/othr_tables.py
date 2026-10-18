#!/usr/bin/env python3
"""OTHR tables 10550-10555 and 20000-20308 of Super Solvers Treasure Mountain! (Win CD).

All of these live in TMT.DLL, NE custom type 32515 (Mac type 'OTHR').  Spec:
re/docs/fmt_othr_tables.md.  Summary:

  10555          hiding-place OBJECT TABLE: 112 x 4 bytes {count, size, shape, noun};
                 entry v describes the picture PMAP 1400+v (TMT256/TMT16.DLL).
  10550..10553   hiding-place LAYOUT tables, one per difficulty 0..3:
                 u16 n (=500) + n x 38-byte records
                 {u8 mask; u8 target; u8 obj[18]; u8 content[18]}.
                 Three different records are drawn at random per game, one for each
                 mountain level; obj/content k go to fixed (screen, slot) pairs
                 (ZONE_SLOTS below, from EXE 1100:0000).
  20000..20050,  "DAM" animation scripts ("DAM\\x1a" + byte-code, EXE interpreter
  20100..20102,  10b8:0312).  20000+t = treasure t (cels: BNDL 330+t); the others are
  20200..20205,  the first member of BNDLs 400..402, 410, 412, 323..325 or are loaded
  20300..20308   directly by id (20201, 20205, 20301, 20302).
  20105..20108   intro story pages: plain text, CR-terminated, NUL-terminated.

Library::

    import othr_tables as ot
    objs = ot.parse_object_table(open(r'...\\32515\\10555','rb').read())
    lay  = ot.parse_layout(open(r'...\\32515\\10550','rb').read())
    rec  = lay['records'][0]; ot.place_record(rec, zone=0)  # -> {(screen,slot): (obj,content)}
    dam  = ot.parse_dam(open(r'...\\32515\\20000','rb').read())
    frames = ot.simulate_dam(dam['raw'])

CLI::

    python -P othr_tables.py dump   [--res-dir DIR] [--exe PATH] [--out FILE.json]
    python -P othr_tables.py render [--res-dir DIR] [--exe PATH] [--out DIR]
                                    [--records N]          (needs Pillow + pmap.py)

No game data is embedded: everything (including the clue words, which are read from
TMTWINCD.EXE's DGROUP via its NE relocation table) comes from the user's files.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
DEFAULT_RES = os.path.join(REPO, 'extracted', 'res')
DEFAULT_EXE = os.path.join(REPO, 'extracted', 'wincd', 'TMTWINCD', 'TMTWINCD.EXE')
DEFAULT_JSON = os.path.join(REPO, 'extracted', 'previews', 'othr_tables.json')
DEFAULT_MAPS = os.path.join(REPO, 'extracted', 'previews', 'othr_maps')

OTHR_TYPE = '32515'
BNDL_TYPE = '32513'
PMAP_TYPE = '32513'

OBJECT_TABLE_ID = 10555
LAYOUT_IDS = (10550, 10551, 10552, 10553)          # difficulty 0..3 (EXE 1100:04da)
STORY_IDS = (20105, 20106, 20107, 20108)            # EXE 1138:017a: 0x4e88 + page 1..4
OBJECT_PMAP_BASE = 1400                             # EXE 1100:0638: obj + 0x578
RECORD_SIZE = 38
N_SLOTS = 18

# ---------------------------------------------------------------- property codes
# Property indices inside a 10555 entry, in the order of the mask bits 8,4,2,1
# (EXE 1100:06cc).  Word tables are far-pointer arrays in DGROUP (EXE 1100:0793),
# speech SSND id = 0x1c84 + index + bias (EXE 1100:08ae, 10e8:01ef).
PROPS = ('count', 'size', 'shape', 'noun')
MASK_BITS = (8, 4, 2, 1)
WORD_TABLES = {                      # DGROUP offset of the 4-byte far-pointer table
    'count': 0x10F9, 'size': 0x1119, 'shape': 0x112D,
    'noun': 0x1079, 'noun_plural': 0x10B9,
}
WORD_TABLE_LEN = {'count': 8, 'size': 5, 'shape': 5, 'noun': 16, 'noun_plural': 16}
SPEECH_BIAS = {'count': 0x25, 'size': 0x2C, 'shape': 0x30, 'noun': 0x07, 'noun_plural': 0x16}
SPEECH_BASE = 0x1C84                 # 7300

# ---------------------------------------------------------------- screen mapping
# Hiding-place slot geometry (scene coordinates: the 512x267 play area whose top-left
# is window (0, 20)).  Picture x: DGROUP 0x2dd4; item x: DGROUP 0x2dda;
# picture y 0x5a (window) = 70 (scene), item y 0x98 (window) = 132 (scene).
SLOT_PIC_X = (26, 197, 368)
SLOT_ITEM_X = (49, 220, 391)
SLOT_PIC_Y = 70
SLOT_ITEM_Y = 132
SLOT_PIC_SIZE = (118, 133)           # every PMAP 1400..1511; also the 0x76 x 0x85 copy
THIRD_WIDTH = 170                    # slot = clamp((player_x + off) / 0xaa, 0, 2) (10e0:0447)

# Record byte (2+k) -> obj, (20+k) -> content, written to screen[s].obj[slot] /
# screen[s].content[slot] (DGROUP 0x23ec + s*28 + slot, 0x23ef + s*28 + slot) by
# EXE 1100:0000 for zone (= mountain level) 0, 1, 2.
ZONE_SLOTS = {
    0: [(10, 0), (10, 2), (12, 0), (12, 1), (13, 0), (14, 0), (14, 1), (14, 2), (15, 0),
        (16, 0), (16, 2), (17, 1), (17, 2), (18, 0), (18, 1), (18, 2), (22, 0), (22, 1)],
    1: [(23, 0), (23, 2), (24, 2), (26, 0), (26, 2), (27, 0), (27, 1), (28, 0), (28, 2),
        (29, 1), (29, 2), (30, 1), (30, 2), (31, 0), (31, 1), (31, 2), (35, 0), (35, 1)],
    2: [(36, 0), (37, 0), (37, 2), (39, 0), (39, 1), (40, 0), (40, 2), (41, 0), (41, 1),
        (41, 2), (42, 2), (43, 1), (43, 2), (44, 0), (44, 1), (44, 2), (48, 0), (48, 1)],
}
# Constant markers written by the same function (obj and content both get the value).
GATE = 0xFE          # locked gate of the level: needs the key (10e0:0d18)
SPECIAL = 0xFD       # 4-search hollow / passage (10e0:0766, 10f8:0000)
EMPTY = 0xFF         # initial value of every slot (1100:04b5)
ZONE_FIXED = {
    0: {(11, 1): GATE, (11, 2): GATE, (13, 1): SPECIAL},
    1: {(24, 0): SPECIAL, (25, 1): GATE, (25, 2): GATE},
    2: {(36, 2): SPECIAL, (38, 0): GATE, (38, 1): GATE},
}
CONTENT_NAMES = {0: 'none', 1: 'none(1)', 2: 'treasure', 3: 'key',
                 GATE: 'gate', SPECIAL: 'special', EMPTY: 'unused'}
# Background PMAP of each mountain screen (screen table +0x12, DGROUP 0x23ea + s*28).
# Read from the EXE when available; these are only used by `render`.
SCREEN_TABLE_OFF = 0x23D8
SCREEN_REC = 28
N_SCREENS = 50

# ---------------------------------------------------------------- DAM
DAM_MAGIC = b'DAM\x1a'
OP_UPDATE_RECT = 0xF9   # x:i16 y:u8 w:i16 h:u8  -> invalidate (x+bx, y+by, w+2, h+2)
OP_LOOP = 0xFA          # target:u16 count:u8    (count bit7 selects counter 0/1)
OP_STOP_SOUND = 0xFB    # pad:u8
OP_SOUND = 0xFC         # pad:u8 n:u8            -> SSND 7000+n
OP_DRAW = 0xFD          # x:i16 y:i16 cel:u8 flags:u8
OP_FRAME = 0xFE         # end of frame (yield until next tick)
OP_END = 0xFF           # end of script (callback, slot freed)
SOUND_BASE = 7000       # EXE 1170:0034  ADD AX,0x1b58

# Known run-time context of every script (observed call sites; see the doc).
DAM_CONTEXT = {}
for _t in range(51):
    DAM_CONTEXT[20000 + _t] = dict(
        role=f'treasure {_t} display animation (castle treasure room)',
        cels_bndl=330 + _t, cels_from_member=0, loaded_at='1190:0158',
        started_at='1190:02e0/1190:0354', layer=0x4D, tick_ms=100,
        origin='(DGROUP 0x2d90[i], 0x2d9c[i]) or (358,73) on screen 0')
DAM_CONTEXT.update({
    20100: dict(role='intro: first animation (BNDL 400 member 0)', cels_bndl=400,
                cels_from_member=1, celbase=0, origin=(0, 20), layer=0, tick_ms=100,
                background=2100, started_at='1138:00ee'),
    20101: dict(role='intro: second animation (BNDL 401 member 0)', cels_bndl=401,
                cels_from_member=1, celbase=10, origin=(32, 21), layer=0, tick_ms=100,
                started_at='1138:0246'),
    20102: dict(role='BNDL 402 member 0; no reference to BNDL 402 or 20102 found in the EXE',
                cels_bndl=402, cels_from_member=1, celbase=0),
    20200: dict(role='level 1->2 transition, part 1 (BNDL 410 member 0)', cels_bndl=410,
                cels_from_member=1, celbase=0, origin=(0, 0), fps=7, started_at='1188:007c'),
    20201: dict(role='level 1->2 transition, part 2 over PMAP 2200 (loaded by id, DGROUP 0x2d71)',
                cels_bndl=410, cels_from_member=1, celbase=0, origin=(0, 0), fps=7,
                background=2200, started_at='1188:0158'),
    20204: dict(role='level 2->3 transition, part 1 (BNDL 412 member 0)', cels_bndl=412,
                cels_from_member=1, celbase=0, origin=(0, 0), fps=7, started_at='1188:007c'),
    20205: dict(role='level 2->3 transition, part 2 over PMAP 2201 (DGROUP 0x2d73)',
                cels_bndl=412, cels_from_member=1, celbase=0, origin=(0, 0), fps=7,
                background=2201, started_at='1188:0158'),
    20300: dict(role='treasure delivered to castle, part 1 (BNDL 323 member 0)', cels_bndl=323,
                cels_from_member=1, celbase=0, origin=(0, 20), fps=4, background=2299,
                started_at='10f0:0126'),
    20302: dict(role='treasure delivered, part 2 (loaded by id 0x4f4e); cel 80 = the treasure',
                cels_bndl=323, cels_from_member=1, celbase=0, origin=(0, 20), fps=4,
                background=2299, extra_cels={80: 'treasure BNDL 330+((n-1)%43) member 0'},
                started_at='10f0:0155'),
    20301: dict(role='treasure delivered, part 3 when no top-rank promotion (id 0x4f4d)',
                cels_bndl=323, cels_from_member=1, celbase=0, origin=(0, 20), fps=4,
                background=2299, started_at='10f0:0209'),
    20305: dict(role='treasure delivered, part 3 on reaching rank 7 (BNDL 324 member 0)',
                cels_bndl=324, cels_from_member=1, celbase=0, origin=(0, 20), fps=4,
                background=2299, started_at='10f0:01b9'),
    20308: dict(role='treasure delivered, part 4 over PMAP 2202 (BNDL 325 member 0)',
                cels_bndl=325, cels_from_member=1, celbase=0, origin=(0, 20), fps=4,
                background=2202, started_at='10f0:02db'),
})


# ================================================================ helpers

def res_path(res_dir, dll, rtype, rid):
    return os.path.join(res_dir, dll, str(rtype), str(rid))


def read_res(res_dir, dll, rtype, rid):
    with open(res_path(res_dir, dll, rtype, rid), 'rb') as f:
        return f.read()


# ================================================================ 10555

def parse_object_table(data: bytes) -> list[dict]:
    """10555: 4-byte entries {count, size, shape, noun}; the NE padding is all-zero
    (a real entry never has noun 0), so the table ends at the first entry with noun 0."""
    out = []
    for v in range(len(data) // 4):
        c, s, sh, n = data[v * 4:v * 4 + 4]
        if n == 0:
            break
        out.append(dict(id=v, pmap=OBJECT_PMAP_BASE + v, count=c, size=s, shape=sh, noun=n))
    return out


# ================================================================ 10550..10553

def parse_layout(data: bytes) -> dict:
    n = struct.unpack_from('<H', data, 0)[0]
    recs = []
    for i in range(n):
        r = data[2 + i * RECORD_SIZE: 2 + (i + 1) * RECORD_SIZE]
        if len(r) < RECORD_SIZE:
            raise ValueError(f'layout truncated at record {i}')
        recs.append(dict(index=i, mask=r[0], target=r[1],
                         objects=list(r[2:2 + N_SLOTS]),
                         contents=list(r[20:20 + N_SLOTS])))
    return dict(count=n, record_size=RECORD_SIZE, used_bytes=2 + n * RECORD_SIZE,
                records=recs)


def clue_props(mask: int) -> list[str]:
    """Properties named by the clue, in the order the game stores them (8,4,2,1)."""
    return [p for p, b in zip(PROPS, MASK_BITS) if mask & b]


def clue_values(rec: dict, objtab: list[dict]) -> list[tuple[str, int]]:
    """What 1100:06cc stores in DGROUP 0x5eab+zone*3: the target's property values."""
    e = objtab[rec['target']]
    return [(p, e[p]) for p in clue_props(rec['mask'])]


def check_record(rec: dict, objtab: list[dict]) -> list[str]:
    """Consistency rules that hold for all 2000 shipped records (see doc)."""
    errs = []
    props = clue_props(rec['mask'])
    if len(props) != 3 or 'noun' not in props:
        errs.append(f'mask {rec["mask"]:#x} is not one of 0x07/0x0b/0x0d')
    keys = [k for k, c in enumerate(rec['contents']) if c == 3]
    if len(keys) != 1 or rec['objects'][keys[0]] != rec['target']:
        errs.append('key not exactly once at the target picture')
    te = objtab[rec['target']]
    m = [k for k, o in enumerate(rec['objects']) if all(objtab[o][p] == te[p] for p in props)]
    if len(m) != 1:
        errs.append(f'{len(m)} pictures match the clue')
    return errs


def place_record(rec: dict, zone: int) -> dict:
    """{(screen, slot): (obj, content)} exactly as 1100:0000 writes them."""
    out = {k: (v, v) for k, v in ZONE_FIXED[zone].items()}
    for k, ss in enumerate(ZONE_SLOTS[zone]):
        out[ss] = (rec['objects'][k], rec['contents'][k])
    return out


def pick_records(count: int, rand) -> tuple[int, int, int]:
    """Record selection of 1100:04b5: idx = rand()*count / 0x8000 (32-bit signed),
    re-rolled until the three indices differ.  `rand` returns 0..0x7fff."""
    a = rand() * count // 0x8000
    while True:
        b = rand() * count // 0x8000
        if b != a:
            break
    while True:
        c = rand() * count // 0x8000
        if c != a and c != b:
            break
    return a, b, c


class BorlandRand:
    """Borland C++ rand()/srand() (EXE 1000:0859 / 1000:0842)."""

    def __init__(self, seed=1):
        self.s = seed & 0xFFFFFFFF

    def __call__(self):
        self.s = (self.s * 0x015A4E35 + 1) & 0xFFFFFFFF
        return (self.s >> 16) & 0x7FFF


# ================================================================ DAM

def parse_dam(data: bytes) -> dict:
    """Linear decode of a DAM script.  The interpreter (10b8:0312) starts at offset 4
    and treats any byte below 0xF9 as a 1-byte no-op; decoding stops after 0xFF."""
    if data[:4] != DAM_MAGIC:
        raise ValueError('not a DAM script')
    ops = []
    p = 4
    frame = 0
    while p < len(data):
        b = data[p]
        if b == OP_DRAW:
            x, y, cel, fl = struct.unpack_from('<hhBB', data, p + 1)
            ops.append(dict(off=p, op='draw', frame=frame, x=x, y=y, cel=cel, flags=fl))
            p += 7
        elif b == OP_FRAME:
            ops.append(dict(off=p, op='frame', frame=frame))
            frame += 1
            p += 1
        elif b == OP_LOOP:
            tgt, cnt = struct.unpack_from('<HB', data, p + 1)
            ops.append(dict(off=p, op='loop', frame=frame, target=tgt, count=cnt & 0x7F,
                            counter=cnt >> 7))
            p += 4
        elif b == OP_SOUND:
            ops.append(dict(off=p, op='sound', frame=frame, pad=data[p + 1], sound=data[p + 2],
                            ssnd=SOUND_BASE + data[p + 2]))
            p += 3
        elif b == OP_STOP_SOUND:
            ops.append(dict(off=p, op='stop_sound', frame=frame, pad=data[p + 1]))
            p += 2
        elif b == OP_UPDATE_RECT:
            x, y, w, h = struct.unpack_from('<hBhB', data, p + 1)
            ops.append(dict(off=p, op='update_rect', frame=frame, x=x, y=y, w=w, h=h))
            p += 7
        elif b == OP_END:
            ops.append(dict(off=p, op='end', frame=frame))
            p += 1
            break
        else:
            ops.append(dict(off=p, op='nop', frame=frame, byte=b))
            p += 1
    cels = sorted({o['cel'] for o in ops if o['op'] == 'draw'})
    return dict(magic=data[:4].decode('latin1'), length=p, ops=ops,
                static_frames=frame, cels=cels, raw=data[:p])


def simulate_dam(raw: bytes, max_ticks=600, stop_after_infinite=1):
    """Run the 10b8:0312 state machine.  Returns (frames, info): frames is a list of
    ticks, each a dict(draws=[(x,y,cel,flags)], sounds=[n], stop_sound=bool,
    rects=[...]).  An infinite loop (count 0) is followed `stop_after_infinite` times."""
    ptr = 4
    counters = [-1, -1]
    frames = []
    inf_jumps = 0
    info = dict(ended=False, infinite_loop_to=None)
    for _ in range(max_ticks):
        cur = dict(draws=[], sounds=[], stop_sound=False, rects=[])
        while True:
            op = raw[ptr]
            ptr += 1
            if op == OP_FRAME:
                break
            if op == OP_DRAW:
                x, y, cel, fl = struct.unpack_from('<hhBB', raw, ptr)
                ptr += 6
                cur['draws'].append((x, y, cel, fl))
            elif op == OP_SOUND:
                cur['sounds'].append(raw[ptr + 1])
                ptr += 2
            elif op == OP_STOP_SOUND:
                cur['stop_sound'] = True
                ptr += 1
            elif op == OP_UPDATE_RECT:
                cur['rects'].append(struct.unpack_from('<hBhB', raw, ptr))
                ptr += 6
            elif op == OP_LOOP:
                tgt, cnt = struct.unpack_from('<HB', raw, ptr)
                ptr += 3
                sel = cnt >> 7
                if counters[sel] == -1:
                    counters[sel] = cnt & 0x7F
                counters[sel] -= 1
                if counters[sel] == 0:
                    counters[sel] = -1
                else:
                    if cnt & 0x7F == 0:
                        info['infinite_loop_to'] = tgt
                        inf_jumps += 1
                        if inf_jumps > stop_after_infinite:
                            frames.append(cur)
                            return frames, info
                    ptr = tgt
            elif op == OP_END:
                info['ended'] = True
                frames.append(cur)
                return frames, info
            # any other byte: no-op
        frames.append(cur)
    return frames, info


# ================================================================ EXE (clue words, screens)

class ExeData:
    """Minimal NE reader for TMTWINCD.EXE: DGROUP bytes + internal far-pointer fixups."""

    def __init__(self, path):
        d = open(path, 'rb').read()
        ne = struct.unpack_from('<H', d, 0x3C)[0]
        if d[ne:ne + 2] != b'NE':
            raise ValueError('not an NE executable')
        segcnt = struct.unpack_from('<H', d, ne + 0x1C)[0]
        segtab = ne + struct.unpack_from('<H', d, ne + 0x22)[0]
        align = struct.unpack_from('<H', d, ne + 0x32)[0]
        autodata = struct.unpack_from('<H', d, ne + 0x0E)[0]
        self.segs = []
        for i in range(segcnt):
            sec, ln, fl, mn = struct.unpack_from('<HHHH', d, segtab + i * 8)
            self.segs.append((sec << align, ln or 0x10000, fl))
        self.dgroup_no = autodata
        off, ln, fl = self.segs[autodata - 1]
        self.dgroup = bytearray(d[off:off + ln])
        self.fixups = {}
        if fl & 0x100:
            r = off + ln
            n = struct.unpack_from('<H', d, r)[0]
            for k in range(n):
                st, f, so, a, _b, c = struct.unpack_from('<BBHBBH', d, r + 2 + k * 8)
                if f & 3 == 0 and st == 3 and a != 0xFF:     # far ptr, internal ref
                    self.fixups[so] = (a, c)

    def cstr(self, off):
        e = self.dgroup.index(0, off)
        return self.dgroup[off:e].decode('latin1')

    def far_string_table(self, off, n):
        out = []
        for i in range(n):
            seg, o = self.fixups.get(off + 4 * i, (None, None))
            out.append(self.cstr(o) if seg == self.dgroup_no else None)
        return out

    def clue_words(self):
        return {k: self.far_string_table(v, WORD_TABLE_LEN[k]) for k, v in WORD_TABLES.items()}

    def screens(self):
        out = []
        for s in range(N_SCREENS):
            b = self.dgroup[SCREEN_TABLE_OFF + s * SCREEN_REC: SCREEN_TABLE_OFF + (s + 1) * SCREEN_REC]
            out.append(dict(screen=s, background=struct.unpack_from('<H', b, 0x12)[0],
                            level=b[0x1A], left=b[6], right=b[7], link=b[8]))
        return out


def describe_entry(e, words):
    if not words:
        return None
    plural = e['count'] > 1
    return dict(count=words['count'][e['count']], size=words['size'][e['size']],
                shape=words['shape'][e['shape']],
                noun=words['noun_plural' if plural else 'noun'][e['noun']])


def clue_speech_ids(rec, objtab):
    """SSND ids spoken for the three clue words (1100:08ae + 10e8:01ef)."""
    e = objtab[rec['target']]
    out = []
    for p in clue_props(rec['mask']):
        key = p
        if p == 'noun' and e['count'] > 1:      # 1100:06cc: 0x5eb4 = count > 1
            key = 'noun_plural'
        out.append(SPEECH_BASE + SPEECH_BIAS[key] + e[p])
    return out


# ================================================================ dump

def build_json(res_dir, exe_path=None):
    words = screens = None
    if exe_path and os.path.exists(exe_path):
        exe = ExeData(exe_path)
        words = exe.clue_words()
        screens = exe.screens()
    objtab = parse_object_table(read_res(res_dir, 'TMT.DLL', OTHR_TYPE, OBJECT_TABLE_ID))
    out = dict(
        spec='re/docs/fmt_othr_tables.md',
        object_table=dict(resource=OBJECT_TABLE_ID, entry_size=4, count=len(objtab),
                          fields=list(PROPS), pmap_base=OBJECT_PMAP_BASE,
                          entries=[dict(e, words=describe_entry(e, words)) for e in objtab]),
        clue_words=words,
        zone_slots={str(z): [list(x) for x in v] for z, v in ZONE_SLOTS.items()},
        zone_fixed={str(z): {f'{s},{sl}': v for (s, sl), v in m.items()}
                    for z, m in ZONE_FIXED.items()},
        layouts={}, dam={}, story={},
    )
    for diff, rid in enumerate(LAYOUT_IDS):
        lay = parse_layout(read_res(res_dir, 'TMT.DLL', OTHR_TYPE, rid))
        bad = 0
        for r in lay['records']:
            r['clue'] = clue_values(r, objtab)
            r['speech'] = clue_speech_ids(r, objtab)
            errs = check_record(r, objtab)
            if errs:
                r['errors'] = errs
                bad += 1
        hist = {}
        for r in lay['records']:
            for c in r['contents']:
                hist[c] = hist.get(c, 0) + 1
        out['layouts'][str(rid)] = dict(difficulty=diff, count=lay['count'],
                                        record_size=RECORD_SIZE, used_bytes=lay['used_bytes'],
                                        records_failing_checks=bad,
                                        content_histogram={str(k): v for k, v in sorted(hist.items())},
                                        records=lay['records'])
    rdir = os.path.join(res_dir, 'TMT.DLL', OTHR_TYPE)
    for name in sorted(os.listdir(rdir), key=lambda s: int(s) if s.isdigit() else 0):
        if not name.isdigit():
            continue
        rid = int(name)
        data = read_res(res_dir, 'TMT.DLL', OTHR_TYPE, rid)
        if data[:4] == DAM_MAGIC:
            dam = parse_dam(data)
            frames, info = simulate_dam(dam['raw'])
            ctx = DAM_CONTEXT.get(rid, {})
            ctx = {k: (list(v) if isinstance(v, tuple) else v) for k, v in ctx.items()}
            if 'extra_cels' in ctx:
                ctx['extra_cels'] = {str(k): v for k, v in ctx['extra_cels'].items()}
            out['dam'][str(rid)] = dict(length=dam['length'], static_frames=dam['static_frames'],
                                        cels=dam['cels'], context=ctx,
                                        simulated_ticks=len(frames), ended=info['ended'],
                                        infinite_loop_to=info['infinite_loop_to'],
                                        ops=dam['ops'])
    for rid in STORY_IDS:
        data = read_res(res_dir, 'TMT.DLL', OTHR_TYPE, rid)
        end = data.find(b'\0')
        out['story'][str(rid)] = dict(page=rid - 20104, length=end,
                                      text=data[:end].decode('latin1'))
    if screens:
        out['screens'] = screens
    return out


# ================================================================ render

def _imgs(res_dir):
    sys.path.insert(0, HERE)
    import pmap  # noqa: E402  (sibling decoder, tools/assets/pmap.py)
    pal = pmap.load_rrgb(res_path(res_dir, 'TMT.DLL', '32516', 900))
    cache = {}

    def img(rid, transparent=True):
        key = (rid, transparent)
        if key not in cache:
            data = read_res(res_dir, 'TMT256.DLL', PMAP_TYPE, rid)
            pm = pmap.decode(data, depth=8)
            cache[key] = pmap.to_image(pm, pal, transparent).convert('RGBA')
        return cache[key]
    return img


def _bndl_members(res_dir, bid):
    d = read_res(res_dir, 'TMT.DLL', BNDL_TYPE, bid)
    n = struct.unpack_from('<I', d, 4)[0]
    return [x & 0xFFFF for x in struct.unpack_from('<%dI' % n, d, 8)]


def render(res_dir, exe_path, out_dir, n_records=1):
    from PIL import Image, ImageDraw
    os.makedirs(out_dir, exist_ok=True)
    img = _imgs(res_dir)
    exe = ExeData(exe_path)
    words = exe.clue_words()
    screens = exe.screens()
    objtab = parse_object_table(read_res(res_dir, 'TMT.DLL', OTHR_TYPE, OBJECT_TABLE_ID))
    key_bndl = _bndl_members(res_dir, 301)          # key pictures, member level*2 (1198:03e9)
    treasure_pic = _bndl_members(res_dir, 330)[0]   # stand-in treasure (real one varies)
    written = []

    # --- object sheet (10555 verification)
    cols, cw, ch = 8, 132, 170
    rows = (len(objtab) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols * cw, rows * ch), (52, 52, 60, 255))
    dr = ImageDraw.Draw(sheet)
    for e in objtab:
        x, y = (e['id'] % cols) * cw, (e['id'] // cols) * ch
        sheet.alpha_composite(img(e['pmap']), (x + 7, y + 2))
        w = describe_entry(e, words)
        dr.text((x + 2, y + 137), f"{e['id']} ({e['count']},{e['size']},{e['shape']},{e['noun']})",
                fill=(255, 255, 0))
        dr.text((x + 2, y + 152), f"{w['count']} {w['size']} {w['shape']} {w['noun']}"[:22],
                fill=(255, 255, 255))
    p = os.path.join(out_dir, 'objects_10555.png')
    sheet.save(p)
    written.append(p)

    # --- per difficulty / record / zone composites
    for diff, rid in enumerate(LAYOUT_IDS):
        lay = parse_layout(read_res(res_dir, 'TMT.DLL', OTHR_TYPE, rid))
        for ri in range(n_records):
            rec = lay['records'][ri]
            for zone in range(3):
                placed = place_record(rec, zone)
                scr_ids = sorted({s for s, _ in placed})
                cols = 3
                tw, th = 512, 268 + 30
                canvas = Image.new('RGBA', (cols * tw, ((len(scr_ids) + cols - 1) // cols) * th + 40),
                                   (20, 20, 24, 255))
                cd = ImageDraw.Draw(canvas)
                tgt = objtab[rec['target']]
                cw_ = describe_entry(tgt, words)
                clue = ' '.join(cw_[p] for p in clue_props(rec['mask']) if cw_[p])
                cd.text((8, 8), f'OTHR {rid} (difficulty {diff}) record {ri} -> mountain level {zone}: '
                                f'mask {rec["mask"]:#04x}, target picture {rec["target"]} '
                                f'(PMAP {OBJECT_PMAP_BASE + rec["target"]}); clue words: "{clue}"',
                        fill=(255, 255, 255))
                for n, s in enumerate(scr_ids):
                    ox, oy = (n % cols) * tw, 40 + (n // cols) * th
                    bgid = screens[s]['background']
                    bg = img(bgid, False).crop((0, 0, 512, 268))
                    tile = bg.copy()
                    td = ImageDraw.Draw(tile, 'RGBA')
                    for xline in (THIRD_WIDTH, 2 * THIRD_WIDTH):
                        td.line([(xline, 0), (xline, 267)], fill=(255, 255, 255, 90), width=1)
                    for slot in range(3):
                        if (s, slot) not in placed:
                            continue
                        obj, cont = placed[(s, slot)]
                        px, py = SLOT_PIC_X[slot], SLOT_PIC_Y
                        if obj < SPECIAL:
                            tile.alpha_composite(img(OBJECT_PMAP_BASE + obj), (px, py))
                            col = {3: (255, 40, 40, 255), 2: (255, 200, 0, 255),
                                   1: (80, 200, 255, 255)}.get(cont, (200, 200, 200, 255))
                            td.rectangle([px, py, px + SLOT_PIC_SIZE[0] - 1, py + SLOT_PIC_SIZE[1] - 1],
                                         outline=col, width=2)
                            if cont == 3:
                                k = img(key_bndl[zone * 2])
                                tile.alpha_composite(k, (SLOT_ITEM_X[slot], SLOT_ITEM_Y))
                            elif cont == 2:
                                tile.alpha_composite(img(treasure_pic), (SLOT_ITEM_X[slot], SLOT_ITEM_Y))
                            e = objtab[obj]
                            td.rectangle([px, py + SLOT_PIC_SIZE[1] - 22, px + 117, py + SLOT_PIC_SIZE[1] - 1],
                                         fill=(0, 0, 0, 170))
                            td.text((px + 2, py + SLOT_PIC_SIZE[1] - 21),
                                    f"#{obj} ({e['count']},{e['size']},{e['shape']},{e['noun']})", fill=col)
                            td.text((px + 2, py + SLOT_PIC_SIZE[1] - 11),
                                    f"content {cont}: {CONTENT_NAMES.get(cont, '?')}", fill=col)
                        else:
                            x0 = slot * THIRD_WIDTH
                            td.rectangle([x0 + 2, 2, x0 + THIRD_WIDTH - 3, 265],
                                         outline=(255, 0, 255, 255), width=3)
                            td.text((x0 + 8, 8), f'slot {slot}: {CONTENT_NAMES[obj]} ({obj:#04x})',
                                    fill=(255, 0, 255, 255))
                    canvas.alpha_composite(tile, (ox, oy))
                    cd.text((ox + 4, oy + 270), f'screen {s}  bg PMAP {bgid}  '
                                                f'(left {screens[s]["left"]}, right {screens[s]["right"]}, '
                                                f'link {screens[s]["link"]})', fill=(220, 220, 220))
                p = os.path.join(out_dir, f'layout_{rid}_r{ri:03d}_level{zone}.png')
                canvas.convert('RGB').save(p)
                written.append(p)

    # --- DAM previews (GIF per script)
    rdir = os.path.join(res_dir, 'TMT.DLL', OTHR_TYPE)
    for name in sorted(os.listdir(rdir)):
        if not name.isdigit():
            continue
        rid = int(name)
        data = read_res(res_dir, 'TMT.DLL', OTHR_TYPE, rid)
        if data[:4] != DAM_MAGIC:
            continue
        ctx = DAM_CONTEXT.get(rid, {})
        mem = _bndl_members(res_dir, ctx['cels_bndl'])
        cels = mem[ctx.get('cels_from_member', 0):]
        dam = parse_dam(data)
        frames, info = simulate_dam(dam['raw'], max_ticks=400, stop_after_infinite=1)
        if rid < 20100:
            W, H, ox, oy = 160, 140, 40, 40
            bg = Image.new('RGBA', (W, H), (60, 70, 60, 255))
        else:
            W, H = 512, 384
            ox, oy = ctx.get('origin', (0, 0))
            bg = Image.new('RGBA', (W, H), (40, 40, 48, 255))
            if ctx.get('background'):
                bg.alpha_composite(img(ctx['background'], False), (0, 20))
        work = bg.copy()          # stamped (flags bit1) cels persist here
        out_frames = []
        for fr in frames:
            im = work.copy()
            for (x, y, cel, fl) in fr['draws']:
                if cel < len(cels):
                    c = img(cels[cel])
                elif cel == 80 and rid == 20302:
                    c = img(treasure_pic)
                else:
                    continue
                if fl & 1:
                    c = c.transpose(Image.FLIP_LEFT_RIGHT)
                im.alpha_composite(c, (ox + x, oy + y)) if (ox + x >= 0 and oy + y >= 0) else \
                    _paste_clipped(im, c, ox + x, oy + y)
                if fl & 2:
                    _paste_clipped(work, c, ox + x, oy + y)
            out_frames.append(im.convert('RGB'))
        if not out_frames:
            continue
        ms = ctx.get('tick_ms') or (1000 // ctx['fps'] if ctx.get('fps') else 100)
        p = os.path.join(out_dir, f'dam_{rid}.gif')
        out_frames[0].save(p, save_all=True, append_images=out_frames[1:], duration=ms, loop=0)
        written.append(p)
    return written


def _paste_clipped(dst, src, x, y):
    sx0, sy0 = max(0, -x), max(0, -y)
    if sx0 >= src.width or sy0 >= src.height:
        return
    part = src.crop((sx0, sy0, src.width, src.height))
    dst.alpha_composite(part, (max(0, x), max(0, y)))


# ================================================================ CLI

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    d = sub.add_parser('dump', help='write JSON')
    d.add_argument('--res-dir', default=DEFAULT_RES)
    d.add_argument('--exe', default=DEFAULT_EXE)
    d.add_argument('--out', default=DEFAULT_JSON)
    r = sub.add_parser('render', help='write PNG/GIF previews')
    r.add_argument('--res-dir', default=DEFAULT_RES)
    r.add_argument('--exe', default=DEFAULT_EXE)
    r.add_argument('--out', default=DEFAULT_MAPS)
    r.add_argument('--records', type=int, default=1)
    a = ap.parse_args(argv)
    if a.cmd == 'dump':
        js = build_json(a.res_dir, a.exe)
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, 'w', encoding='utf-8') as f:
            json.dump(js, f, indent=1)
        bad = sum(v['records_failing_checks'] for v in js['layouts'].values())
        print(f'wrote {a.out}: {len(js["object_table"]["entries"])} objects, '
              f'{sum(v["count"] for v in js["layouts"].values())} layout records '
              f'({bad} failing checks), {len(js["dam"])} DAM scripts, {len(js["story"])} story pages')
    else:
        for p in render(a.res_dir, a.exe, a.out, a.records):
            print(p)
    return 0


if __name__ == '__main__':
    sys.exit(main())
