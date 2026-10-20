"""Read and patch the guest RAM of a DOSBox-X save state of Gizmos & Gadgets (SSG.EXE), and compute
rand() seeds for replayable captures.

A .sav is a zip archive; its entry 'Memory' is a 0x88-byte header followed by the guest's physical RAM
from address 0. SSG.EXE's DGROUP (Ghidra segment 1ea7) is found by the Borland C++ copyright at DS:0004,
never by a fixed segment (the MOUSEKEY TSR moves the game up by 86 paragraphs). DS:XXXX below is DGROUP;
the names are those of re/names/gg/dgroup.tsv.

Usage (python -P capture_mem.py ...):
  vars STATE [STATE ...]            the main game variables (state, building, level, score, rand state ...)
  peek STATE OFF LEN                hex dump DS:OFF .. +LEN (hex)
  poke STATE OUT OFF=HEX[,...]      write bytes at DS:OFF (hex offset, hex byte string) -> new .sav OUT
  pokew STATE OUT OFF=VAL[,...]     write 16-bit words (decimal or 0x..)
  poked STATE OUT OFF=VAL[,...]     write 32-bit words (decimal or 0x..)
  rand SEED N                       the first N rand() results and LCG states after SEED (u32)
  seedfor N K FIRST LAST [START]    a u32 rand state s (searched upwards from START) such that
                                    random(N) = K for every call number FIRST..LAST after s (1 = the
                                    first rand() call after the poke). Used to choose the puzzle type
                                    (N=8) when the exact number of rand() calls before the choice is
                                    not known.
  seedpuz KIND [PICK [SKIP [NCAND]]]
                                    a rand state for the puzzle choice of State_09_Puzzle (38e2:0000):
                                    after SKIP other rand() calls (default 0), call SKIP+1 = random(8) =
                                    KIND (Puzzle_RandomType 38e2:01e8) and call SKIP+2 = random(NCAND,
                                    default 3) = PICK, the index into the candidate list (the first
                                    three unsolved variants)
  door STATE OUT KIND [PICK [SKIP]] STATE (in a warehouse room, the player at a puzzle door) with
                                    g_heldKeys = 4 (Up held) and g_randState = seedpuz KIND PICK SKIP:
                                    loading OUT enters that door within a frame or two and deals that
                                    puzzle. SKIP = the rand() calls the chimps make before the choice;
                                    it is fixed for a given STATE (11 for w0/15_at_door) and found with
                                    'calls'
  calls STATE_A STATE_B             how many rand() calls lie between the rand state of A and that of B
                                    (steps the LCG backwards from B; A may also be a hex u32 state)
  diffds STATE_A STATE_B [OFF LEN]  DGROUP words that differ between two states
  boxes STATE                       the part boxes of every room of the building in progress: room,
                                    x, y, part index (catalogue record; 1000 = bananas). Read from the
                                    room table DS:7a04 (far pointer per room; the box list head at
                                    room+0xA0 holds first/last node pointers, room+0x9E the box count (copy DS:0964 for the current room DS:08c2); a node is 0x2C bytes:
                                    +0 next, +4 prev, +0x0E x, +0x10 y, +0x1E part, +0x24 x, +0x26 y)
  boxset STATE OUT ROOM X,Y [--part P] [--to NX,NY] [--room R]
                                    capture shortcut (a state edit, never a game rule): change the
                                    part in the box at X,Y of ROOM, move it to NX,NY, and/or relink it
                                    into the box list of room R
STATE may be a path or a name in extracted/captures/gg/savestates (without .sav), e.g. w0/15_at_door.
Pokes are written to a NEW file; load it with `tmctl.py state load NAME` after saving it into the
state library (OUT may be a library name too: OUT without a directory part and without .sav is taken
as a library name).
"""
import os
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
LIB = os.path.join(REPO, 'extracted', 'captures', 'gg', 'savestates')
HDR = 0x88
SIG = b'Borland C++ - Copyright 1991 Borland Intl.'
DGROUP_BYTES = 0xBD10
GHIDRA_DGROUP = 0x1EA7
PUZZLE_KINDS = ['electric circuits', 'simple machines', 'machine pieces', 'gears', 'balance', 'launch',
                'energy forms', 'magnets']
BUILDINGS = ['Automotive', 'Aircraft', 'Alternative Energy']

# (name, DS offset, type) - type: b u8, w u16, sw s16, d u32
VARS = [
    ('g_state', 0x455c, 'sw'), ('g_leaveRequest', 0x455a, 'sw'), ('g_ticks', 0x021a, 'd'),
    ('g_randState', 0x442a, 'd'), ('g_randSeed', 0x2454, 'w'), ('g_heldKeys', 0x0218, 'w'),
    ('g_kbdPlayMode', 0x0096, 'w'), ('g_cpuType', 0x2450, 'w'), ('g_soundDevice', 0x2452, 'sw'),
    ('g_fmPresent', 0xa0d8, 'w'), ('g_soundOff', 0x45cc, 'w'), ('g_sndEnabled', 0x1680, 'b'),
    ('player name', 0x4562, 's16'), ('g_playerSlot', 0x4576, 'sw'), ('g_rosterIndex', 0xa079, 'sw'),
    ('g_score', 0x4572, 'd'), ('g_withChimps', 0x4578, 'w'), ('g_projectiles', 0x457e, 'sw'),
    ('g_tubeRoom', 0x4580, 'sw'), ('g_partsNeeded', 0x4582, 'sw'), ('g_inBuilding', 0x0094, 'w'),
    ('g_building', 0x4592, 'sw'), ('g_level', 0x4594, 'sw'), ('g_raceIndex', 0x4590, 'sw'),
    ('g_racesWon[0]', 0x4596, 'sw'), ('g_racesWon[1]', 0x4598, 'sw'), ('g_racesWon[2]', 0x459a, 'sw'),
    ('g_buildingInProgress[0]', 0x459c, 'w'), ('[1]', 0x459e, 'w'), ('[2]', 0x45a0, 'w'),
    ('g_racesWonTotal', 0x5c0e, 'sw'), ('g_hintsOff', 0x45a2, 'w'), ('g_playerRoom', 0x7aa4, 'sw'),
    ('g_puzzleDoorHit', 0x00bc, 'w'), ('g_vehicleDone', 0x00be, 'w'), ('g_rematch', 0x455e, 'w'),
    ('g_raceResult', 0x4560, 'sw'), ('puzzle kind DS:b13a', 0xb13a, 'sw'),
    ('puzzle variant DS:b138', 0xb138, 'sw'), ('puzzle door DS:b13c', 0xb13c, 'sw'),
    ('puzzle exit DS:3332', 0x3332, 'w'), ('g_palFaded', 0x024c, 'w'), ('g_menuDisabled', 0x054e, 'w'),
    ('cursor x', 0x7a88, 'sw'), ('cursor y', 0x7a86, 'sw'), ('player x', 0x7a8a + 2, 'sw'),
    ('player y', 0x7a8a + 4, 'sw'),
]


def path_of(s):
    if os.path.exists(s):
        return s
    p = os.path.join(LIB, s + '.sav')
    if os.path.exists(p):
        return p
    raise SystemExit(f'no state {s}')


def out_path(s):
    if os.path.dirname(s) == '' and not s.endswith('.sav'):
        return os.path.join(LIB, s + '.sav')
    if not os.path.isabs(s) and not s.endswith('.sav') and not os.path.exists(os.path.dirname(s)):
        return os.path.join(LIB, s + '.sav')
    return s if s.endswith('.sav') else s + '.sav'


class State:
    def __init__(self, path):
        self.path = path_of(path)
        with zipfile.ZipFile(self.path) as z:
            self.mem = bytearray(z.read('Memory'))
        i = self.mem.find(SIG)
        if i < 0:
            raise SystemExit(f'{self.path}: SSG.EXE DGROUP not found (game not running in this state?)')
        self.ds = i - 4
        self.dsseg = (self.ds - HDR) >> 4

    @property
    def relocation(self):
        """run-time segment = Ghidra segment + relocation (-0x786 with MOUSEKEY, -0x7dc without)."""
        return self.dsseg - GHIDRA_DGROUP

    def b(self, o):
        return self.mem[self.ds + o]

    def w(self, o):
        return struct.unpack_from('<H', self.mem, self.ds + o)[0]

    def sw(self, o):
        return struct.unpack_from('<h', self.mem, self.ds + o)[0]

    def d(self, o):
        return struct.unpack_from('<I', self.mem, self.ds + o)[0]

    def get(self, o, t):
        if t == 'b':
            return self.b(o)
        if t == 'w':
            return self.w(o)
        if t == 'sw':
            return self.sw(o)
        if t == 'd':
            return self.d(o)
        if t.startswith('s'):
            raw = bytes(self.mem[self.ds + o:self.ds + o + int(t[1:])])
            return raw.split(b'\0')[0].decode('latin-1')
        raise ValueError(t)

    def put(self, o, data):
        self.mem[self.ds + o:self.ds + o + len(data)] = data

    def save(self, out):
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        with zipfile.ZipFile(self.path) as zin, zipfile.ZipFile(out + '.tmp', 'w') as zout:
            for info in zin.infolist():
                data = bytes(self.mem) if info.filename == 'Memory' else zin.read(info.filename)
                ni = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                ni.compress_type = info.compress_type
                ni.external_attr = info.external_attr
                ni.create_system = info.create_system
                zout.writestr(ni, data)
        os.replace(out + '.tmp', out)
        players = self.path[:-4] + '.players'
        if os.path.exists(players) and os.path.abspath(players) != os.path.abspath(out[:-4] + '.players'):
            with open(players, 'rb') as f, open(out[:-4] + '.players', 'wb') as g:
                g.write(f.read())


def lcg(s):
    return (s * 0x015A4E35 + 1) & 0xFFFFFFFF


def rand_out(s):
    """Borland rand(): returns (new state, (new state >> 16) & 0x7FFF)."""
    s = lcg(s)
    return s, (s >> 16) & 0x7FFF


def random_n(r, n):
    """rand() * n / 0x8000, the scaling of all of SSG.EXE's call sites."""
    return (r * n) >> 15


def seed_for(n, k, first, last, start=0x12340000):
    s = start
    while True:
        t, ok = s, True
        for j in range(1, last + 1):
            t, r = rand_out(t)
            if j >= first and random_n(r, n) != k:
                ok = False
                break
        if ok:
            return s
        s = (s + 1) & 0xFFFFFFFF


def seed_puzzle(kind, pick=0, skip=0, ncand=3, start=0x12340000):
    s = start
    while True:
        t = s
        for _ in range(skip):
            t = lcg(t)
        t, r1 = rand_out(t)
        _, r2 = rand_out(t)
        if random_n(r1, 8) == kind and random_n(r2, ncand) == pick:
            return s
        s = (s + 1) & 0xFFFFFFFF


def _lin(seg, off):
    return HDR + seg * 16 + off


def _far(st, lin):
    off, seg = struct.unpack_from('<HH', st.mem, lin)
    return seg, off


def box_lists(st):
    """{room: (head_lin, [(node_lin, node_seg, node_off, x, y, part)])} for rooms with a box list."""
    out = {}
    for r in range(16):
        seg, off = _far(st, st.ds + 0x7a04 + 4 * r)
        if not seg and not off:
            continue
        lseg, loff = _far(st, _lin(seg, off) + 0xA0)
        if not lseg and not loff:
            continue
        head = _lin(lseg, loff)
        nodes = []
        nseg, noff = _far(st, head)
        guard = 0
        while (nseg or noff) and guard < 64:
            n = _lin(nseg, noff)
            x, y = struct.unpack_from('<HH', st.mem, n + 0x0E)
            part = struct.unpack_from('<H', st.mem, n + 0x1E)[0]
            nodes.append((n, nseg, noff, x, y, part))
            nseg, noff = _far(st, n)
            guard += 1
        out[r] = (head, nodes)
    return out


def cmd_boxset(argv):
    st = State(argv[0])
    out = out_path(argv[1])
    room = int(argv[2])
    bx, by = (int(v) for v in argv[3].split(','))
    part = opt_val(argv, '--part')
    to = opt_val(argv, '--to')
    dest = opt_val(argv, '--room')
    lists = box_lists(st)
    head, nodes = lists[room]
    hit = [nd for nd in nodes if nd[3] == bx and nd[4] == by]
    if not hit:
        raise SystemExit(f'no box at {bx},{by} in room {room}: ' + ', '.join(f'{n[3]},{n[4]}' for n in nodes))
    n, nseg, noff = hit[0][:3]
    if part is not None:
        struct.pack_into('<H', st.mem, n + 0x1E, int(part, 0))
    if to is not None:
        nx, ny = (int(v) for v in to.split(','))
        struct.pack_into('<HH', st.mem, n + 0x0E, nx, ny)
        struct.pack_into('<HH', st.mem, n + 0x24, nx, ny)
    if dest is not None and int(dest) != room:
        dhead, dnodes = lists[int(dest)]
        nxt = struct.unpack_from('<HH', st.mem, n)        # (off, seg)
        prv = struct.unpack_from('<HH', st.mem, n + 4)
        # unlink from the source list (head +0 first, +4 last)
        if prv == (0, 0):
            struct.pack_into('<HH', st.mem, head, *nxt)
        else:
            struct.pack_into('<HH', st.mem, _lin(prv[1], prv[0]), *nxt)
        if nxt == (0, 0):
            struct.pack_into('<HH', st.mem, head + 4, *prv)
        else:
            struct.pack_into('<HH', st.mem, _lin(nxt[1], nxt[0]) + 4, *prv)
        # rooms of another scale use another crate picture: take size, picture pointer and picture
        # resource (+0x12..+0x1B, +0x28..+0x2B) from a box already in the destination room
        if dnodes:
            ref = dnodes[0][0]
            st.mem[n + 0x12:n + 0x1C] = st.mem[ref + 0x12:ref + 0x1C]
            st.mem[n + 0x28:n + 0x2C] = st.mem[ref + 0x28:ref + 0x2C]
        # append to the destination list
        last = struct.unpack_from('<HH', st.mem, dhead + 4)
        struct.pack_into('<HH', st.mem, n, 0, 0)
        struct.pack_into('<HH', st.mem, n + 4, *last)
        if last == (0, 0):
            struct.pack_into('<HH', st.mem, dhead, noff, nseg)
        else:
            struct.pack_into('<HH', st.mem, _lin(last[1], last[0]), noff, nseg)
        struct.pack_into('<HH', st.mem, dhead + 4, noff, nseg)
        # box counts: room struct +0x9E, and its copy DS:0964 in g_room (DS:08c6) for the current room
        for r, delta in ((room, -1), (int(dest), 1)):
            seg, off = _far(st, st.ds + 0x7a04 + 4 * r)
            c = _lin(seg, off) + 0x9E
            struct.pack_into('<H', st.mem, c, struct.unpack_from('<H', st.mem, c)[0] + delta)
            if st.w(0x08c2) == r:
                struct.pack_into('<H', st.mem, st.ds + 0x0964, st.w(0x0964) + delta)
    st.save(out)
    print(f'room {room} box {bx},{by}: part {part or "kept"}, position {to or "kept"}, '
          f'room {dest or room}; wrote {out}')


def opt_val(argv, name):
    return argv[argv.index(name) + 1] if name in argv else None


def cmd_vars(paths):
    for p in paths:
        st = State(p)
        print(f'{st.path}  DGROUP seg {st.dsseg:04x} (relocation {st.relocation:+#x})')
        for name, o, t in VARS:
            v = st.get(o, t)
            extra = ''
            if name.startswith('puzzle kind') and 0 <= v < 8:
                extra = f'  ({PUZZLE_KINDS[v]})'
            if name == 'g_building' and 0 <= v < 3:
                extra = f'  ({BUILDINGS[v]})'
            vs = f'{v:#010x}' if t == 'd' and name != 'g_score' else (repr(v) if t.startswith('s1') else str(v))
            print(f'  DS:{o:04x} {name:28s} {vs}{extra}')


def parse_pokes(spec):
    for item in spec.split(','):
        off, val = item.split('=')
        yield int(off, 16), val


def main(argv):
    if not argv:
        print(__doc__)
        return
    cmd = argv[0]
    if cmd == 'vars':
        cmd_vars(argv[1:])
    elif cmd == 'peek':
        st = State(argv[1])
        off, ln = int(argv[2], 16), int(argv[3], 16)
        for a in range(off, off + ln, 16):
            row = bytes(st.mem[st.ds + a:st.ds + min(a + 16, off + ln)])
            print(f'DS:{a:04x}  {row.hex(" ")}')
    elif cmd in ('poke', 'pokew', 'poked'):
        st = State(argv[1])
        out = out_path(argv[2])
        for off, val in parse_pokes(argv[3]):
            if cmd == 'poke':
                data = bytes.fromhex(val)
            elif cmd == 'pokew':
                data = struct.pack('<H', int(val, 0) & 0xFFFF)
            else:
                data = struct.pack('<I', int(val, 0) & 0xFFFFFFFF)
            st.put(off, data)
            print(f'DS:{off:04x} <- {data.hex(" ")}')
        st.save(out)
        print('wrote', out)
    elif cmd == 'rand':
        s, n = int(argv[1], 0), int(argv[2])
        for i in range(1, n + 1):
            s, r = rand_out(s)
            print(f'{i:3d} state {s:#010x} rand {r:5d}  random(8) {random_n(r, 8)}  random(3) {random_n(r, 3)}')
    elif cmd == 'seedfor':
        n, k, first, last = (int(x, 0) for x in argv[1:5])
        start = int(argv[5], 0) if len(argv) > 5 else 0x12340000
        print(f'{seed_for(n, k, first, last, start):#010x}')
    elif cmd == 'seedpuz':
        args = [int(x, 0) for x in argv[1:]]
        print(f'{seed_puzzle(*args):#010x}')
    elif cmd == 'door':
        st = State(argv[1])
        out = out_path(argv[2])
        kind = int(argv[3])
        pick = int(argv[4]) if len(argv) > 4 else 0
        skip = int(argv[5]) if len(argv) > 5 else 0
        s = seed_puzzle(kind, pick, skip)
        st.put(0x0218, struct.pack('<H', 4))
        st.put(0x442a, struct.pack('<I', s))
        st.save(out)
        print(f'wrote {out}: g_heldKeys=4, g_randState={s:#010x} (kind {kind} {PUZZLE_KINDS[kind]}, pick {pick}, skip {skip})')
    elif cmd == 'calls':
        a = int(argv[1], 16) if argv[1].lower().startswith('0x') else State(argv[1]).d(0x442a)
        s = State(argv[2]).d(0x442a)
        inv = pow(0x015A4E35, -1, 1 << 32)
        for n in range(1000000):
            if s == a:
                print(n)
                break
            s = ((s - 1) * inv) & 0xFFFFFFFF
        else:
            print('more than 1000000 (or unrelated states)')
    elif cmd == 'boxes':
        st = State(argv[1])
        for r, (_, nodes) in box_lists(st).items():
            print(f'room {r}: ' + (', '.join(f'({x},{y}) part {p}' for _, _, _, x, y, p in nodes) or '-'))
    elif cmd == 'boxset':
        cmd_boxset(argv[1:])
    elif cmd == 'diffds':
        a, b = State(argv[1]), State(argv[2])
        off = int(argv[3], 16) if len(argv) > 3 else 0
        ln = int(argv[4], 16) if len(argv) > 4 else DGROUP_BYTES
        for o in range(off, off + ln, 2):
            if a.w(o) != b.w(o):
                print(f'DS:{o:04x} {a.w(o):04x} -> {b.w(o):04x}')
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
