"""Install / read a frame + logic-step logger in a DOSBox-X save state of SST.EXE (measurement only).

The r2 reference logs (extracted/captures/dos/savestates/r2/fl_*.sav) were made with it: a state with
the logger installed is loaded in DOSBox-X, played, and saved again; the saved state then holds the
rings below (REFERENCE_CAPTURES.md R2.1).

install STATE OUT   patch the state's RAM (the game's behaviour is unchanged):
                    * main loop 130f:0097 -> CALLF 8000:0000: does the original DS:9283 computation and
                      appends g_ticks (low word, DS:3d2b) to the FRAME ring (one entry per loop iteration)
                    * Game_Tick step path 130f:0112 -> CALLF 8000:0080: appends a 16-byte STEP record
                      (ticks, player x, y, camera x, y, player state, g_keys, coins) per executed logic
                      tick, then runs the two instructions the call displaced (copied from the state)
                    * Game_Tick blocked path 130f:0190 (CALLF Anim_Tick) -> CALLF 8000:0180: appends
                      g_ticks to the BLOCKED ring, then calls Anim_Tick
                    Memory 8000:0000-8800:7fff lies in the free DOS block above the game.
                    The patched code is checked by CRC-32 before it is replaced.
read STATE [N]      statistics of the rings
steps STATE [N]     print the last N step records
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import savmem  # noqa: E402

SEG = 0x8000
FR_IDX, FR_CNT, BL_IDX, BL_CNT = 0x100, 0x102, 0x108, 0x10A
FR_RING, BL_RING, MASK = 0x200, 0x4200, 0x1FFF
STSEG, ST_MASK, ST_REC = 0x8800, 0x7FF0, 16

FRAME_STUB = bytes.fromhex(
    '53 06'                 # push bx; push es
    'a0 19 91'              # mov al,[9119]
    '3c 01 1b c0'           # cmp al,1; sbb ax,ax
    '25 0c 00 40'           # and ax,0c; inc ax
    'a3 83 92'              # mov [9283],ax          (what 130f:0097..00a8 computes)
    'b8 00 80 8e c0'        # mov ax,8000; mov es,ax
    '26 8b 1e 00 01'        # mov bx,es:[0100]
    'a1 2b 3d'              # mov ax,[3d2b]
    '26 89 87 00 02'        # mov es:[bx+0200],ax
    '83 c3 02 81 e3 ff 1f'  # add bx,2; and bx,1fff
    '26 89 1e 00 01'        # mov es:[0100],bx
    '26 ff 06 02 01'        # inc word es:[0102]
    '07 5b cb')             # pop es; pop bx; retf
STEP_FIELDS = (('ticks', 0x3d2b), ('x', 0x37e2), ('y', 0x37e4), ('camx', 0x37ea), ('camy', 0x37ec),
               ('state', 0x913c), ('keys', 0x9105), ('coins', 0x9134))
STEP_STUB_HEAD = bytes.fromhex('53 06 b8 00 88 8e c0 26 8b 1e 00 00') + b''.join(
    bytes([0xa1]) + struct.pack('<H', off) + bytes([0x26, 0x89, 0x47, 0x10 + 2 * i])
    for i, (_, off) in enumerate(STEP_FIELDS)) + bytes.fromhex(
    '83 c3 10 81 e3 f0 7f'  # add bx,16; and bx,7ff0
    '26 89 1e 00 00'        # mov es:[0000],bx
    '26 ff 06 02 00'        # inc word es:[0002]
    '07 5b')                # pop es; pop bx; then the displaced instructions, then retf
BLOCK_STUB_HEAD = bytes.fromhex(
    '53 06 b8 00 80 8e c0'
    '26 8b 1e 08 01'        # mov bx,es:[0108]
    'a1 2b 3d'
    '26 89 87 00 42'        # mov es:[bx+4200],ax
    '83 c3 02 81 e3 ff 1f'
    '26 89 1e 08 01'
    '26 ff 06 0a 01'
    '07 5b')                # then the original (relocated) CALLF Anim_Tick, then retf
# (segment, offset, length, CRC-32 of the original bytes, replacement)
PATCH_MAIN = (0x130f, 0x0097, 18, 0xe71013ab, bytes.fromhex('9a00000080' 'eb0b' + '90' * 11))
PATCH_TICK = (0x130f, 0x0112, 7, 0x5baf335f, bytes.fromhex('9a80000080' '9090'))


def install(src, out):
    st = savmem.State(src)
    delta = 0x4d94 - st.dsseg
    base = st.lin(SEG, 0)
    st.mem[base:base + 0x6200] = bytes(0x6200)
    sb = st.lin(STSEG, 0)
    st.mem[sb:sb + 0x8010] = bytes(0x8010)
    st.mem[base:base + len(FRAME_STUB)] = FRAME_STUB
    seg, off, n, crc, new = PATCH_TICK
    a = st.lin(seg - delta, off)
    displaced = bytes(st.mem[a:a + n])
    if displaced == new:
        old_stub = bytes(st.mem[base + 0x80 + len(STEP_STUB_HEAD):base + 0x80 + len(STEP_STUB_HEAD) + n])
        displaced = old_stub  # already patched: the stub keeps the displaced bytes
    elif zlib.crc32(displaced) != crc:
        raise SystemExit(f'unexpected code at {seg:04x}:{off:04x}')
    step_stub = STEP_STUB_HEAD + displaced + b'\xcb'
    assert len(step_stub) < 0x100
    st.mem[base + 0x80:base + 0x80 + len(step_stub)] = step_stub  # 0x80..0xfc
    a = st.lin(0x130f - delta, 0x0190)
    orig = bytes(st.mem[a:a + 5])
    if orig[0] != 0x9a:
        raise SystemExit('unexpected bytes at 130f:0190 ' + orig.hex())
    if orig != bytes.fromhex('9a80010080'):
        stub = BLOCK_STUB_HEAD + orig + bytes([0xcb])
        st.mem[base + 0x180:base + 0x180 + len(stub)] = stub
        st.mem[a:a + 5] = bytes.fromhex('9a80010080')
    for seg, off, n, crc, new in (PATCH_MAIN, PATCH_TICK):
        a = st.lin(seg - delta, off)
        cur = bytes(st.mem[a:a + n])
        if cur == new:
            continue
        if zlib.crc32(cur) != crc:
            raise SystemExit(f'unexpected code at {seg:04x}:{off:04x}')
        st.mem[a:a + n] = new
    st.save(out)
    print('wrote', out)


def rings(st):
    base = st.lin(SEG, 0)
    m = st.mem

    def w(o):
        return struct.unpack_from('<H', m, base + o)[0]

    def ring(rbase, idx, cnt):
        k = min(cnt, MASK // 2 + 1)
        return [w(rbase + ((idx - 2 * i) & MASK)) for i in range(k, 0, -1)]
    fr = ring(FR_RING, w(FR_IDX), w(FR_CNT))
    blk = ring(BL_RING, w(BL_IDX), w(BL_CNT))
    sb = st.lin(STSEG, 0)
    sidx, scnt = struct.unpack_from('<HH', m, sb)
    k = min(scnt, ST_MASK // ST_REC + 1)
    steps = []
    for i in range(k, 0, -1):
        o = sb + 0x10 + ((sidx - ST_REC * i) & ST_MASK)
        steps.append(dict(zip([f for f, _ in STEP_FIELDS], struct.unpack_from('<8H', m, o))))
    return fr, steps, blk


def read(src):
    st = savmem.State(src)
    fr, steps, blk = rings(st)
    ticks = st.d(0x3d2b)
    print(f'g_ticks now {ticks} (low {ticks & 0xffff}); frames {len(fr)}, steps {len(steps)}, blocked {len(blk)}')
    stp = [s['ticks'] for s in steps]
    for name, r in (('frame', fr), ('step', stp)):
        if len(r) < 2:
            continue
        d = [(b - a) & 0xffff for a, b in zip(r, r[1:])]
        span = (r[-1] - r[0]) & 0xffff
        if span:
            print(f'{name}s: {len(r)} entries over {span} PIT ticks = {span / 236.6954:.2f} s; '
                  f'rate {(len(r) - 1) * 236.6954 / span:.2f}/s')
        hist = {}
        for x in d:
            hist[x] = hist.get(x, 0) + 1
        print('  interval histogram (PIT ticks: count):', dict(sorted(hist.items())))
    print('blocked (last 20):', blk[-20:])
    return fr, steps, blk


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
    elif sys.argv[1] == 'install':
        install(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == 'steps':
        st = savmem.State(sys.argv[2])
        fr, steps, blk = rings(st)
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 60
        for s in steps[-n:]:
            print(' '.join(f'{k}={v:#06x}' if k in ('x', 'y', 'camx', 'camy', 'keys') else f'{k}={v}'
                           for k, v in s.items()))
    else:
        read(sys.argv[2])
