"""Read and patch the guest RAM of a DOSBox-X save state (.sav) of the DOS edition (SST.EXE).

A .sav is a zip archive. Its entry 'Memory' is a 0x88-byte header followed by the guest's physical
RAM from address 0; 'Vga' holds the EGA/VGA state with the video memory (four planes interleaved, one
4-byte group per address, starting at the game's latch table A000:0000); 'Machine_Type' names the
machine (MCH_EGA, MCH_TANDY). SST.EXE's DGROUP is found by the Turbo C copyright at DS:0004. The
engine's reader is src/dos/save/dosbox_state.h (same rules).

Usage (python -P savmem.py ...):
  vars STATE                      print the main game variables
  where STATE                     one line: position, level, scene, coins, state, g_ticks, ui mode
  peek STATE OFF LEN              hex dump DS:OFF (hex) .. LEN (hex) bytes
  far STATE OFF                   resolve the far pointer stored at DS:OFF
  rec STATE                       the signed-in player's USERS.DAT record (in RAM)
  dgroup STATE OUT                write DS:0000..CC1F (the layout --dos-dump-state writes)
  screen STATE OUT.png [--look L] [--scale N] [--data DIR]
                                  the picture on the screen (video memory) as a PNG, through the
                                  look's palette of the user's SST.EXE (default: the state's look)
  poke STATE OUT OFF=HEX[,..]     write bytes at DS:OFF (hex offset, little-endian hex string),
                                  e.g. 9134=2000 sets g_coins to 0x20; writes a new .sav to OUT
  pokew STATE OUT OFF=VAL[,..]    write 16-bit words (decimal or 0x..)
STATE may be a path or a name in extracted/captures/dos/savestates (without .sav), e.g. ega/shack.
"""
import os
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
LIB = os.path.join(REPO, 'extracted', 'captures', 'dos', 'savestates')
HDR = 0x88
SIG = b'Turbo-C - Copyright (c) 1988 Borland Intl.'
DGROUP_BYTES = 0xCC20
EGA_ROW_TABLE = 0x2EEE      # DS:2EEE: A000 offset of each screen row (0x4000 + y * 0x50)
TANDY_PAGE = 0x98000        # the Tandy 1000 video page (top 32 KB of the 640 KB)


def path_of(s):
    if os.path.exists(s):
        return s
    p = os.path.join(LIB, s + '.sav')
    if os.path.exists(p):
        return p
    raise SystemExit(f'no state {s}')


def latch_table():
    """A000:0000..003F as the game fills it (1000:0836): colour k at bits C0 of address k."""
    return bytes(0xC0 if (k >> p) & 1 else 0 for k in range(16) for p in range(4))


class State:
    def __init__(self, path):
        self.path = path_of(path)
        z = zipfile.ZipFile(self.path)
        self.mem = bytearray(z.read('Memory'))
        names = z.namelist()
        self.machine = z.read('Machine_Type').decode('ascii', 'replace') if 'Machine_Type' in names else ''
        self._vga = z.read('Vga') if 'Vga' in names and self.machine != 'MCH_TANDY' else None
        z.close()
        i = self.mem.find(SIG)
        if i < 0:
            raise SystemExit('DGROUP not found (game not running in this state?)')
        self.ds = i - 4             # entry offset of DS:0000
        self.dsseg = (self.ds - HDR) >> 4

    @property
    def relocation(self):
        return self.dsseg - 0x4D94

    def lin(self, seg, off):
        return HDR + seg * 16 + off

    def image_lin(self, ghidra_seg, off):
        return self.lin((ghidra_seg + self.relocation) & 0xFFFF, off)

    def b(self, o):
        return self.mem[self.ds + o]

    def w(self, o):
        return struct.unpack_from('<H', self.mem, self.ds + o)[0]

    def sw(self, o):
        return struct.unpack_from('<h', self.mem, self.ds + o)[0]

    def d(self, o):
        return struct.unpack_from('<I', self.mem, self.ds + o)[0]

    def far(self, o):
        off, seg = struct.unpack_from('<HH', self.mem, self.ds + o)
        return self.lin(seg, off)

    def far_rel(self, o):
        """far pointer at DS:o as a DS-relative offset (if it points into DGROUP)."""
        return self.far(o) - self.ds

    def dgroup(self):
        return bytes(self.mem[self.ds:self.ds + DGROUP_BYTES])

    def back_buffer(self):
        """Segment 2005 from 2005:0010: 160 x 200 bytes, two pixels per byte (high nibble left)."""
        a = self.image_lin(0x2005, 0x10)
        return bytes(self.mem[a:a + 32000])

    def screen(self):
        """The picture on the screen as 320*200 logical colours (row major), or None."""
        px = bytearray(64000)
        if self.machine == 'MCH_TANDY':
            for y in range(200):
                row = HDR + TANDY_PAGE + (y & 3) * 0x2000 + (y >> 2) * 160
                for x in range(160):
                    v = self.mem[row + x]
                    px[y * 320 + 2 * x] = v >> 4
                    px[y * 320 + 2 * x + 1] = v & 15
            return bytes(px)
        if self._vga is None:
            return None
        base = self._vga.find(latch_table())
        if base < 0:
            return None
        v = self._vga
        for y in range(200):
            row = self.w(EGA_ROW_TABLE + 2 * y) or (0x4000 + y * 0x50)
            for x in range(320):
                a = base + 4 * (row + (x >> 2))
                bit = 7 - 2 * (x & 3)  # each game pixel is two screen pixels
                px[y * 320 + x] = ((v[a] >> bit) & 1) | (((v[a + 1] >> bit) & 1) << 1) | \
                    (((v[a + 2] >> bit) & 1) << 2) | (((v[a + 3] >> bit) & 1) << 3)
        return bytes(px)

    def look(self):
        return {2: 'cga', 3: 'tandy', 4: 'cga', 5: 'mcga'}.get(self.sw(0x912C), 'ega')

    def cpu(self):
        """(cs, ip, ss, sp) where DOSBox-X stopped the CPU (run-time segments), or None. The 'CPU' entry
        starts with EAX..EDI (ESP at +0x10) and EIP (+0x20); the segment values follow as 8-byte words
        ES, CS, SS, DS, found by SS = DGROUP + 0xCC2 (Turbo C's stack follows DGROUP)."""
        if not hasattr(self, '_cpu'):
            self._cpu = None
            with zipfile.ZipFile(self.path) as z:
                if 'CPU' in z.namelist():
                    with z.open('CPU') as f:
                        h = f.read(1024)
                    q = lambda p: struct.unpack_from('<Q', h, p)[0]  # noqa: E731
                    ss = self.dsseg + DGROUP_BYTES // 16
                    for p in range(0x40 + 16, len(h) - 16):
                        if q(p) == ss and 0 < q(p - 8) <= 0xFFFF and max(q(p - 16), q(p + 8)) <= 0xFFFF:
                            self._cpu = (q(p - 8), struct.unpack_from('<I', h, 0x20)[0] & 0xFFFF, ss,
                                         struct.unpack_from('<I', h, 0x10)[0] & 0xFFFF)
                            break
        return self._cpu

    def inside_timer_before_countdown(self):
        """The timer interrupt the state was saved in had not counted DS:3D34 yet (Timer_ISR 1000:0428
        after the g_ticks increment and before DEC DS:3D34 at 046a, or below it in the BIOS clock or
        Sound_Tick: return address 1000:044a / 1000:046a on the stack)."""
        c = self.cpu()
        if not c:
            return False
        cs, ip, ss, sp = c
        isr = (0x1000 + self.relocation) & 0xFFFF
        if cs == isr and 0x042F < ip <= 0x046A:
            return True
        base = HDR + ss * 16
        for off in range(sp, min(sp + 0x200, 0xFFFC), 2):
            ret, seg = struct.unpack_from('<HH', self.mem, base + off)
            if seg == isr and ret in (0x044A, 0x046A):
                return True
        return False

    def first_tick_ticks(self):
        """g_ticks of the first game tick after this state (DS:3D34 + 1 PIT ticks later, one less when
        the interrupt in progress had not counted it yet)."""
        return self.d(0x3d2b) + min(self.b(0x3d34), 4) + (0 if self.inside_timer_before_countdown() else 1)

    def save(self, out):
        zin = zipfile.ZipFile(self.path)
        zout = zipfile.ZipFile(out, 'w')
        for info in zin.infolist():
            data = bytes(self.mem) if info.filename == 'Memory' else zin.read(info.filename)
            ni = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            ni.compress_type = info.compress_type
            ni.external_attr = info.external_attr
            ni.create_system = info.create_system
            zout.writestr(ni, data)
        zout.close()
        zin.close()


VARS = [
    ('g_ticks', 0x3d2b, 'd'), ('g_coins', 0x9134, 'w'), ('g_nets', 0x910f, 'w'),
    ('g_treasuresFound', 0x9113, 'w'), ('g_keyState', 0x9120, 'w'), ('g_level', 0x928f, 'w'),
    ('g_loadedScene', 0x8fbe, 'w'), ('g_area', 0x910b, 'w'), ('g_tier', 0x9130, 'w'),
    ('player x', 0x37e2, 'w'), ('player y', 0x37e4, 'w'), ('camera x', 0x37ea, 'w'),
    ('camera y', 0x37ec, 'w'), ('g_playerState', 0x913c, 'w'), ('g_uiMode', 0x9119, 'b'),
    ('g_screen', 0x913e, 'b'), ('g_keys', 0x9105, 'w'), ('g_stepDone', 0x910d, 'w'),
    ('g_seconds', 0x9115, 'w'), ('g_endState', 0x9234, 'w'), ('g_gameInProgress', 0x9205, 'w'),
    ('g_searchTimer', 0x8fa3, 'sw'), ('g_shopCoinsOwed', 0x92a1, 'sw'), ('g_hintsOn', 0x9224, 'w'),
    ('g_slowCpu', 0x923c, 'w'), ('g_videoType', 0x912c, 'sw'), ('g_soundDevice', 0x63bc, 'w'),
    ('g_soundOn', 0x63b4, 'w'), ('g_musicOn', 0x63b6, 'w'), ('g_inputContext', 0x922c, 'w'),
    ('g_castleLayout', 0x8f9e, 'b'), ('g_gamesPlayed', 0x926e, 'w'), ('DS:90fe', 0x90fe, 'w'),
    ('DS:9100', 0x9100, 'b'), ('DS:3d34', 0x3d34, 'b'), ('DS:3d33', 0x3d33, 'b'),
    ('g_randSeed', 0x8f48, 'd'),
]


def cmd_vars(st):
    print(f'state {st.path}  {st.machine}  DS base entry+{st.ds:#x} (seg {st.dsseg:#06x}, relocation {st.relocation:+#x})')
    for name, o, t in VARS:
        v = getattr(st, t)(o)
        print(f'  {name:18s} DS:{o:04x} = {v} ({v:#x})')
    rec = st.far_rel(0x9228)
    print(f'  g_userRec -> DS:{rec:04x}  total treasures (rec+0x0A) = {st.w(rec + 0x0a)}')
    print(f'  hint flags DS:923e = {bytes(st.mem[st.ds + 0x923e:st.ds + 0x923e + 15]).hex(" ")}')


def where(st):
    return ('x={:#06x} y={:#06x} cam={:#x},{:#x} level={} scene={} state={} coins={} nets={} ui={:#04x} '
            'ticks={} keys={:#x}').format(st.w(0x37e2), st.w(0x37e4), st.w(0x37ea), st.w(0x37ec), st.w(0x928f),
                                          st.w(0x8fbe), st.w(0x913c), st.w(0x9134), st.w(0x910f), st.b(0x9119),
                                          st.d(0x3d2b), st.w(0x9105))


def cmd_peek(st, off, ln):
    a = st.ds + off
    for i in range(0, ln, 16):
        chunk = st.mem[a + i:a + min(ln, i + 16)]
        print(f'DS:{off + i:04x}  {chunk.hex(" ")}')


def screen_image(st, look=None, scale=1, data=None):
    """The state's screen as a PIL image through the look's palette of the user's SST.EXE."""
    from PIL import Image
    sys.path.insert(0, HERE)
    from dos_gfx_common import load_exe, game_palette, DEFAULT_DOS_DIR
    px = st.screen()
    if px is None:
        raise SystemExit('no video memory found in ' + st.path)
    exe = load_exe(os.path.join(data or DEFAULT_DOS_DIR, 'SST.EXE'))
    pal = game_palette(exe, look or st.look())
    im = Image.new('RGB', (320, 200))
    im.putdata([pal[c] for c in px])
    if scale != 1:
        im = im.resize((320 * scale, 200 * scale), Image.NEAREST)
    return im


def main(argv):
    if not argv:
        print(__doc__)
        return
    cmd = argv[0]
    st = State(argv[1])
    if cmd == 'vars':
        cmd_vars(st)
    elif cmd == 'where':
        print(where(st))
    elif cmd == 'peek':
        cmd_peek(st, int(argv[2], 16), int(argv[3], 16))
    elif cmd == 'far':
        a = st.far(int(argv[2], 16))
        print(f'linear entry+{a:#x}  DS-relative {a - st.ds:#x}')
    elif cmd == 'dgroup':
        open(argv[2], 'wb').write(st.dgroup())
        print('DGROUP at entry offset %#x, segment %#x -> %s' % (st.ds, st.dsseg, argv[2]))
    elif cmd == 'screen':
        rest = argv[3:]
        opt = {rest[i]: rest[i + 1] for i in range(0, len(rest) - 1, 2)}
        im = screen_image(st, opt.get('--look'), int(opt.get('--scale', '1')), opt.get('--data'))
        im.save(argv[2])
        print('wrote', argv[2])
    elif cmd in ('poke', 'pokew'):
        out = argv[2]
        for item in argv[3].split(','):
            k, v = item.split('=')
            o = int(k, 16)
            if cmd == 'poke':
                data = bytes.fromhex(v)
            else:
                data = struct.pack('<H', int(v, 0) & 0xFFFF)
            st.mem[st.ds + o:st.ds + o + len(data)] = data
            print(f'DS:{o:04x} <- {data.hex()}')
        st.save(out)
        print('wrote', out)
    elif cmd == 'rec':
        rec = st.far_rel(0x9228)
        r = st.ds + rec
        print('record at DS:%04x' % rec)
        print('saved', st.mem[r + 9], 'total', struct.unpack_from('<H', st.mem, r + 10)[0])
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
