"""Gizmos & Gadgets (DOS v1.0): DAM animation scripts (resources 0x114b..0x1156) -> text listings
and, optionally, one picture per tick.

Spec: re/gg/docs/fmt_graphics.md §6. Code (Ghidra SSSS:OOOO): interpreter 3d39:058f (per tick,
2 channels of 0x22 bytes at DS:ba66), start 3d39:03cb, still running 3d39:0468, draw
3d39:084a. The only user is the launch puzzle (overlay 3797): 3797:100e(script, frames, ..)
loads script id + k and frame bundle id + k (k = DS:b124 % 3) with the pairs
(0x114b, 0x1157), (0x114e, 0x115a), (0x1151, 0x115d), (0x1154, 0x1160)  (3797:129d..139e),
so script 0x114b + j animates the members of bundle 0x1157 + j.

    +0  'DAM' 0x1A
    +4  u16 words: opcode, operands (all little-endian words)
    7FF8 idx, arg32     call the far function DS:ba1e[idx](channel, arg) if it is set   (4 words)
    7FF9 x, y, w, h     queue an update rectangle (x, y relative to the channel)         (5 words)
    7FFA target, n      loop: counter = bit 15 of n; target is a word index from the
                        script start (header included)                                   (3 words)
    7FFB a              skipped (TM Windows: stop sound)                                  (2 words)
    7FFC a, b           skipped (TM Windows: play sound)                                  (3 words)
    7FFD dx, dy, f, fl  queue cel f (frame table + channel base) at channel x + dx, y + dy;
                        fl bit 0 is passed to the picture blit as its mode               (5 words)
    7FFE                end of this tick                                                  (1 word)
    7FFF                end: call the channel's callback, free the channel                (1 word)
    any other word      ignored (1 word)

CLI (python -P tools/gg/dam.py ...)::

    dam.py [ID ...]                listing of the scripts (default: all DAM resources)
    dam.py ID --render [--out DIR] [--pal ID]   one PNG per tick (cels of the paired bundle
                                   drawn at their positions; default palette 0x1134, the
                                   launch puzzle's)
    dam.py --check
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ggarc  # noqa: E402
import lpx  # noqa: E402

DAM_MAGIC = b'DAM\x1a'
OPS = {  # opcode: (name, operand words)
    0x7FF8: ('call', 3),
    0x7FF9: ('rect', 4),
    0x7FFA: ('loop', 2),
    0x7FFB: ('op_fb', 1),
    0x7FFC: ('op_fc', 2),
    0x7FFD: ('cel', 4),
    0x7FFE: ('tick', 0),
    0x7FFF: ('end', 0),
}
SCRIPT_FIRST, SCRIPT_COUNT = 0x114B, 12
FRAMES_FIRST = 0x1157
DEFAULT_PALETTE = 0x1134            # launch puzzle screen 0x1133 (3797:040d)


class DamError(ValueError):
    pass


def s16(v):
    return v - 0x10000 if v >= 0x8000 else v


def parse(p):
    """-> list of (word index, opcode, operands). Walks linearly to the end of the data."""
    if len(p) < 4 or p[:4] != DAM_MAGIC:
        raise DamError('not a DAM script')
    if (len(p) - 4) % 2:
        raise DamError('odd script length')
    w = struct.unpack_from('<%dH' % ((len(p) - 4) // 2), p, 4)
    out = []
    i = 0
    while i < len(w):
        op = w[i]
        name, n = OPS.get(op, ('ignored', 0))
        if i + 1 + n > len(w):
            raise DamError('operands of %04x at word %d run past the end' % (op, i))
        out.append((i + 2, op, w[i + 1:i + 1 + n]))     # +2: word index from the script start
        i += 1 + n
    return out


def fmt_op(op, a):
    name = OPS.get(op, ('ignored', 0))[0]
    if op == 0x7FFD:
        return 'cel   dx %4d dy %4d frame %2d flags %d' % (s16(a[0]), s16(a[1]), a[2], a[3])
    if op == 0x7FF9:
        return 'rect  x %d y %d w %d h %d' % tuple(s16(v) for v in a)
    if op == 0x7FFA:
        return 'loop  to word %d, counter %d, n %d' % (a[0], a[1] >> 15, a[1] & 0x7FFF)
    if op == 0x7FF8:
        return 'call  #%d arg 0x%08x' % (a[0], a[1] | a[2] << 16)
    if op in (0x7FFE, 0x7FFF):
        return name
    return '%-5s %s' % (name, ' '.join('%04x' % v for v in a)) if a else '%04x (%s)' % (op, name)


def ticks(ops):
    """Group cels by tick for a linear script (no loops): list of lists of (dx, dy, frame, fl)."""
    out, cur = [], []
    for _, op, a in ops:
        if op == 0x7FFD:
            cur.append((s16(a[0]), s16(a[1]), a[2], a[3]))
        elif op == 0x7FFE:
            out.append(cur)
            cur = []
        elif op == 0x7FFF:
            out.append(cur)
            break
    return out


def dam_ids(arc):
    for lf in ggarc.walk_leaves(arc):
        if lf.kind == 'dam':
            yield lf


def render(arc, rid, ops, out, pal_id):
    from PIL import Image
    j = rid - SCRIPT_FIRST
    if not 0 <= j < SCRIPT_COUNT:
        raise DamError('no known frame bundle for %04x' % rid)
    frames = [lpx.decode_lpx(m.payload) for m in ggarc.parse_bundle(arc.payload(FRAMES_FIRST + j))]
    pal = lpx.base_palette(arc)[0].apply(arc.payload(pal_id))
    tk = ticks(ops)
    cels = [c for t in tk for c in t]
    x0 = min(dx for dx, _, _, _ in cels)
    y0 = min(dy for _, dy, _, _ in cels)
    x1 = max(dx + frames[f].w for dx, _, f, _ in cels)
    y1 = max(dy + frames[f].h for _, dy, f, _ in cels)
    names = []
    for k, t in enumerate(tk):
        im = Image.new('RGBA', (x1 - x0, y1 - y0), (40, 40, 40, 255))
        for dx, dy, f, fl in t:
            im.alpha_composite(lpx.to_image(frames[f], pal), (dx - x0, dy - y0))
        fn = os.path.join(out, 'dam_%04x_t%02d.png' % (rid, k))
        im.save(fn)
        names.append(fn)
    return names, (x0, y0)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ggarc.add_data_arg(ap)
    ap.add_argument('ids', nargs='*')
    ap.add_argument('--render', action='store_true')
    ap.add_argument('--out', default=os.path.join(ggarc.DEFAULT_PREVIEWS, 'dam'))
    ap.add_argument('--pal', default='%04x' % DEFAULT_PALETTE)
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args(argv)
    try:
        arc = ggarc.Archive(a.data)
        leaves = ([ggarc.find_leaf(arc, i) for i in a.ids] if a.ids else list(dam_ids(arc)))
        errors = 0
        counts = {}
        for lf in leaves:
            try:
                ops = parse(lf.payload)
            except DamError as e:
                errors += 1
                print('ERROR %s: %s' % (lf.path, e))
                continue
            for _, op, _ in ops:
                counts[op] = counts.get(op, 0) + 1
            last = ops[-1][1] if ops else None
            if last != 0x7FFF:
                errors += 1
                print('ERROR %s: does not end with 7FFF' % lf.path)
            if a.check:
                continue
            j = lf.rid - SCRIPT_FIRST
            print('%s: %d bytes, %d ticks, frames from bundle %s'
                  % (lf.path, len(lf.payload), len(ticks(ops)),
                     '%04x' % (FRAMES_FIRST + j) if 0 <= j < SCRIPT_COUNT else '?'))
            for wi, op, args in ops:
                print('  w%-3d %04x  %s' % (wi, op, fmt_op(op, args)))
            if a.render:
                out = ggarc.safe_out_dir(a.out, arc.data)
                names, origin = render(arc, lf.rid, ops, out, ggarc.parse_rid(a.pal))
                print('  %d tick pictures -> %s (origin %d,%d)' % (len(names), out, origin[0], origin[1]))
        print('dam: %d scripts, opcodes %s; %d error%s'
              % (len(leaves), ', '.join('%04x x%d' % kv for kv in sorted(counts.items())),
                 errors, '' if errors == 1 else 's'))
        return 1 if errors else 0
    except (ggarc.ArchiveError, DamError, lpx.LpxError) as e:
        print('dam: %s' % e, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
