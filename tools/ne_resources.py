"""List / extract resources from a Win16 NE executable or DLL.

Usage: python ne_resources.py FILE [OUTDIR]
"""
import os
import struct
import sys

RT_NAMES = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG', 6: 'STRING',
            7: 'FONTDIR', 8: 'FONT', 9: 'ACCELERATOR', 10: 'RCDATA', 12: 'GROUP_CURSOR',
            14: 'GROUP_ICON', 16: 'VERSION'}


def read_name(d, base, off):
    n = d[base + off]
    return d[base + off + 1:base + off + 1 + n].decode('latin-1')


def resources(d):
    ne = struct.unpack_from('<I', d, 0x3c)[0]
    assert d[ne:ne + 2] == b'NE'
    rsrc_off = struct.unpack_from('<H', d, ne + 0x24)[0]
    base = ne + rsrc_off
    shift = struct.unpack_from('<H', d, base)[0]
    p = base + 2
    out = []
    while True:
        tid = struct.unpack_from('<H', d, p)[0]
        if tid == 0:
            break
        count = struct.unpack_from('<H', d, p + 2)[0]
        p += 8
        tname = RT_NAMES.get(tid & 0x7fff, str(tid & 0x7fff)) if tid & 0x8000 else read_name(d, base, tid)
        for _ in range(count):
            off, ln, flags, rid = struct.unpack_from('<HHHH', d, p)
            p += 12
            rname = str(rid & 0x7fff) if rid & 0x8000 else read_name(d, base, rid)
            out.append((tname, rname, off << shift, ln << shift, flags))
    return out


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    res = resources(d)
    outdir = sys.argv[2] if len(sys.argv) > 2 else None
    from collections import Counter
    c = Counter(r[0] for r in res)
    print(dict(c))
    for t, n, off, ln, fl in res:
        if outdir:
            p = os.path.join(outdir, t)
            os.makedirs(p, exist_ok=True)
            open(os.path.join(p, n), 'wb').write(d[off:off + ln])
        else:
            print(f'{t:12} {n:20} off={off:#x} len={ln}')
