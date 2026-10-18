"""Decode DOSBox-X video captures (AVI + ZMBV) into exact frames, in pure Python (zlib + Pillow).

DOSBox-X's "Record video to AVI" (tools/tmctl.py: rec start / rec stop OUT.avi) stores every
emulated frame losslessly with the ZMBV codec. This tool turns such a file into PNG frames and
lists which frames changed, so tick-exact behaviour (movement per tick, transitions, blink
timing) can be measured without taking one screenshot per tick.

Usage (python -P avi_frames.py <cmd> ...):
  info AVI                          size, frame rate, frame count, duration
  changes AVI [--box X,Y,W,H]       one line per frame whose content changed: index, time (ms),
                                    bounding box of the changed 16x16 blocks (optionally only
                                    changes intersecting the box)
  extract AVI OUTDIR [--frames SPEC] [--changed] [--box X,Y,W,H]
                                    write NNNNN.png per selected frame (SPEC like 0-99,120,200-);
                                    --changed keeps only frames that differ from the previous
                                    written one (inside --box if given)

Frame times are emulated time: index * 1e6 / (frames per second from the AVI header).
ZMBV format: keyframes carry palette + full frame; delta frames carry per-block motion vectors
and XOR data, all in one zlib stream that restarts at each keyframe (DOSBox zmbv.cpp).
"""
import os
import struct
import sys
import zlib

from PIL import Image

MAX_VECTOR = 16


def read_avi(path):
    """Return (info dict, list of video chunk payloads in order)."""
    data = open(path, 'rb').read()
    if data[:4] != b'RIFF' or data[8:12] != b'AVI ':
        raise SystemExit('not an AVI file')
    info = {}
    frames = []

    def walk(off, end):
        while off + 8 <= end:
            cid = data[off:off + 4]
            size = struct.unpack_from('<I', data, off + 4)[0]
            body = off + 8
            if cid in (b'RIFF', b'LIST'):
                walk(body + 4, min(body + size, end))
            elif cid == b'avih':
                info['usec_per_frame'] = struct.unpack_from('<I', data, body)[0]
                info['width'], info['height'] = struct.unpack_from('<II', data, body + 32)
            elif cid == b'strh' and data[body:body + 4] == b'vids':
                info['fourcc'] = data[body + 4:body + 8].decode('latin1')
                scale, rate = struct.unpack_from('<II', data, body + 20)
                info['fps'] = rate / scale if scale else 0
            elif cid[2:4] in (b'dc', b'db') and cid[:2] == b'00':
                frames.append(data[body:body + size])
            off = body + size + (size & 1)

    walk(12, len(data))
    if not info.get('fps'):
        info['fps'] = 1e6 / info['usec_per_frame']
    return info, frames


class Zmbv:
    def __init__(self, width, height):
        self.w, self.h = width, height
        self.pitch = width + 2 * MAX_VECTOR
        self.z = None
        self.fmt = None
        self.palette = bytearray(768)
        self.cur = None
        self.changed = None  # list of (x, y, w, h) blocks changed by the last decode

    def _setup(self, fmt, bw, bh):
        self.fmt = fmt
        self.ps = {4: 1, 5: 2, 6: 2, 8: 4}.get(fmt)
        if self.ps is None:
            raise SystemExit(f'unsupported ZMBV pixel format {fmt}')
        self.bw, self.bh = bw, bh
        self.blocks = []
        for by in range(0, self.h, bh):
            for bx in range(0, self.w, bw):
                self.blocks.append((bx, by, min(bw, self.w - bx), min(bh, self.h - by)))
        self.cur = bytearray(self.pitch * (self.h + 2 * MAX_VECTOR) * self.ps)

    def _off(self, x, y):
        return ((y + MAX_VECTOR) * self.pitch + x + MAX_VECTOR) * self.ps

    def decode(self, chunk):
        if not chunk:  # dropped / duplicate frame: unchanged
            self.changed = []
            return
        tag = chunk[0]
        if tag & 1:
            _hi, _lo, comp, fmt, bw, bh = chunk[1:7]
            if fmt != self.fmt or self.cur is None:
                self._setup(fmt, bw, bh)
            self.z = zlib.decompressobj() if comp else None
            payload = chunk[7:]
        else:
            payload = chunk[1:]
        work = self.z.decompress(payload) if self.z else payload
        pos = 0
        ps, W = self.ps, self.w * self.ps
        if tag & 1:
            oldpal = bytes(self.palette)
            if ps == 1:
                self.palette[:] = work[:768]
                pos = 768
            prev = self.cur
            buf = bytearray(len(self.cur))
            for y in range(self.h):
                o = self._off(0, y)
                buf[o:o + W] = work[pos:pos + W]
                pos += W
            self.cur = buf
            if prev is None or len(prev) != len(buf) or oldpal != bytes(self.palette):
                self.changed = [(0, 0, self.w, self.h)]
            else:  # periodic keyframe: report only the blocks that really differ
                self.changed = [b for b in self.blocks
                                if any(prev[self._off(b[0], b[1] + r):self._off(b[0], b[1] + r) + b[2] * ps]
                                       != buf[self._off(b[0], b[1] + r):self._off(b[0], b[1] + r) + b[2] * ps]
                                       for r in range(b[3]))]
            return
        if tag & 2 and ps == 1:
            for i in range(768):
                self.palette[i] ^= work[i]
            pos = 768
        old = bytes(self.cur)
        new = self.cur  # starts as a copy of old; only moved/xored blocks are rewritten
        nb = len(self.blocks)
        vec = work[pos:pos + 2 * nb]
        pos = (pos + 2 * nb + 3) & ~3
        changed = []
        for i, (bx, by, dx, dy) in enumerate(self.blocks):
            a, b = vec[2 * i], vec[2 * i + 1]
            xor = a & 1
            vx = (a if a < 128 else a - 256) >> 1
            vy = (b if b < 128 else b - 256) >> 1
            if not xor and vx == 0 and vy == 0:
                continue
            n = dx * ps
            for r in range(dy):
                d = self._off(bx, by + r)
                s = self._off(bx + vx, by + r + vy)
                if xor:
                    v = int.from_bytes(old[s:s + n], 'little') ^ int.from_bytes(work[pos:pos + n], 'little')
                    new[d:d + n] = v.to_bytes(n, 'little')
                    pos += n
                else:
                    new[d:d + n] = old[s:s + n]
            changed.append((bx, by, dx, dy))
        if tag & 2:
            changed = [(0, 0, self.w, self.h)]
        self.changed = changed

    def image(self):
        W = self.w * self.ps
        rows = b''.join(bytes(self.cur[self._off(0, y):self._off(0, y) + W]) for y in range(self.h))
        if self.ps == 1:
            im = Image.frombytes('P', (self.w, self.h), rows)
            im.putpalette(bytes(self.palette))
            return im
        if self.ps == 4:
            return Image.frombytes('RGB', (self.w, self.h), rows, 'raw', 'BGRX')
        mode = 'BGR;15' if self.fmt == 5 else 'BGR;16'
        return Image.frombytes('RGB', (self.w, self.h), rows, 'raw', mode)

    def region_bytes(self, box):
        x, y, w, h = box
        return b''.join(bytes(self.cur[self._off(x, yy):self._off(x, yy) + w * self.ps]) for yy in range(y, y + h))


def iter_frames(path):
    info, chunks = read_avi(path)
    dec = Zmbv(info['width'], info['height'])
    for i, c in enumerate(chunks):
        dec.decode(c)
        yield i, i * 1000.0 / info['fps'], dec


def parse_spec(spec, n):
    keep = set()
    for part in spec.split(','):
        if '-' in part:
            a, b = part.split('-')
            keep.update(range(int(a or 0), int(b) + 1 if b else n))
        else:
            keep.add(int(part))
    return keep


def _bbox(blocks):
    if not blocks:
        return None
    x0 = min(b[0] for b in blocks)
    y0 = min(b[1] for b in blocks)
    x1 = max(b[0] + b[2] for b in blocks)
    y1 = max(b[1] + b[3] for b in blocks)
    return x0, y0, x1 - x0, y1 - y0


def _intersects(b, box):
    return not (b[0] + b[2] <= box[0] or box[0] + box[2] <= b[0] or b[1] + b[3] <= box[1] or box[1] + box[3] <= b[1])


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return
    cmd, path = argv[0], argv[1]
    opts = argv[2:]
    box = None
    if '--box' in opts:
        box = tuple(int(v) for v in opts[opts.index('--box') + 1].split(','))
    if cmd == 'info':
        info, chunks = read_avi(path)
        print(f"{info['width']}x{info['height']} {info.get('fourcc', '?')} {info['fps']:.4f} fps, "
              f"{len(chunks)} frames, {len(chunks) / info['fps']:.2f} s")
    elif cmd == 'changes':
        for i, t, dec in iter_frames(path):
            blocks = dec.changed
            if box:
                blocks = [b for b in blocks if _intersects(b, box)]
            bb = _bbox(blocks)
            if bb:
                print(f'{i:5d} {t:9.1f} ms  changed {bb[0]},{bb[1]} {bb[2]}x{bb[3]}  ({len(blocks)} blocks)')
    elif cmd == 'extract':
        out = opts[0]
        os.makedirs(out, exist_ok=True)
        info, chunks = read_avi(path)
        keep = parse_spec(opts[opts.index('--frames') + 1], len(chunks)) if '--frames' in opts else None
        only_changed = '--changed' in opts
        last = None
        n = 0
        for i, t, dec in iter_frames(path):
            if keep is not None and i not in keep:
                continue
            if only_changed:
                cur = dec.region_bytes(box) if box else bytes(dec.cur)
                if cur == last:
                    continue
                last = cur
            dec.image().convert('RGB').save(os.path.join(out, f'{i:05d}.png'))
            n += 1
        print(f'{n} frames written to {out}')
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
