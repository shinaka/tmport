"""Decoder for Treasure Mountain DOS-edition .PAK sprite packs (SPRITES, BEGIN, CLOSE, SHACK).

Layout (all little-endian; see re/dos/docs/fmt_graphics.md for the EXE evidence)::

    u16 count
    u16 0, u16 62, u16 58          (constant in all four files; not read by the blitter)
    u16 para[count]                frame k starts at file offset para[k]*16
    u16 0xFFFF, u16 0xFFFF         trailer; first frame follows on the next 16-byte boundary

    frame (paragraph aligned):
    +0  u16 0
    +2  u16 width in bytes (2 pixels per byte)
    +4  u16 height in rows
    +6  i16 x offset in bytes  (added to the draw x unless flag 0x80; mirrored with 0x20)
    +8  i16 y offset in rows
    +A  u16 class/flags        (returned by 1000:2159 in AX; meaning not established)
    +C  u16, +E u16            (not read by the blitter)
    +10 rows: per row one length byte L, then
           L & 0x80: raw row, (L & 0x7F) == width bytes of packed 4bpp pixels follow
           else:     L bytes of (count, pixel-pair byte) runs that add up to width bytes

The blitter is SST.EXE 1000:1b8d; colour 0 is transparent when it is called with flag 0x40.

CLI::

    python -P pak.py FILE.PAK [OUT_DIR] [--exe SST.EXE] [--palette ega|tandy|mcga]
                     [--opaque] [--scale N] [--quiet]
    python -P pak.py --all [TM-DOS_DIR] [OUT_DIR]

Writes <NAME>_PAK_<nnn>.png per frame, <NAME>_PAK_sheet.png and <NAME>_PAK_frames.tsv.
No game data is embedded here; everything is read from the user's files.
"""
import argparse
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dos_gfx_common import (DEFAULT_DOS_DIR, DEFAULT_GFX_OUT, add_common_args,  # noqa: E402
                            contact_sheet, find_exe, game_palette, indexed_image, scale)

HEADER_WORDS = 4
FRAME_HDR = 16
TRANSPARENT = 0          # mask table 2005:8600: a nibble of 0 keeps the destination


class PakError(ValueError):
    pass


class Frame:
    __slots__ = ('index', 'offset', 'alloc', 'used', 'w_bytes', 'height', 'x_off', 'y_off',
                 'field0', 'fa', 'fc', 'fe', 'rows', 'raw_rows', 'rle_rows')

    @property
    def width(self):
        return self.w_bytes * 2

    def pixels(self):
        out = bytearray()
        for r in self.rows:
            for b in r:
                out.append(b >> 4)
                out.append(b & 15)
        return out


def decode_frame(data, off, index=0):
    if off + FRAME_HDR > len(data):
        raise PakError(f'frame {index}: header past end of file')
    f = Frame()
    f.index, f.offset = index, off
    (f.field0, f.w_bytes, f.height, f.x_off, f.y_off, f.fa, f.fc, f.fe) = \
        struct.unpack_from('<HHHhhHHH', data, off)
    si = off + FRAME_HDR
    f.rows, f.raw_rows, f.rle_rows = [], 0, 0
    for y in range(f.height):
        n = data[si]
        si += 1
        if n & 0x80:                       # raw row (1000:1d1e)
            ln = n & 0x7F
            row = data[si:si + ln]
            si += ln
            f.raw_rows += 1
        else:                              # (count, byte) runs (1000:1cda)
            seg = data[si:si + n]
            si += n
            if n % 2:
                raise PakError(f'frame {index} row {y}: odd run-list length')
            row = bytearray()
            for i in range(0, n, 2):
                row += bytes([seg[i + 1]]) * seg[i]
            f.rle_rows += 1
        if len(row) != f.w_bytes:
            raise PakError(f'frame {index} row {y}: {len(row)} bytes, header says {f.w_bytes}')
        f.rows.append(bytes(row))
    f.used = si - off
    return f


class Pak:
    def __init__(self, data: bytes, name='PAK'):
        self.name = name
        if len(data) < 12:
            raise PakError('file too short')
        self.count = struct.unpack_from('<H', data)[0]
        self.consts = struct.unpack_from('<3H', data, 2)
        self.paras = struct.unpack_from('<%dH' % self.count, data, 2 * HEADER_WORDS)
        tpos = 2 * HEADER_WORDS + 2 * self.count
        self.trailer = struct.unpack_from('<2H', data, tpos)
        first = (tpos + 4 + 15) // 16 * 16
        self.notes = []
        if self.trailer != (0xFFFF, 0xFFFF):
            self.notes.append(f'unexpected trailer {self.trailer}')
        if self.paras and self.paras[0] * 16 != first:
            self.notes.append(f'first frame at {self.paras[0] * 16:#x}, expected {first:#x}')
        self.frames = []
        for k, p in enumerate(self.paras):
            off = p * 16
            nxt = self.paras[k + 1] * 16 if k + 1 < self.count else len(data)
            f = decode_frame(data, off, k)
            f.alloc = nxt - off
            if (f.used + 15) // 16 * 16 != f.alloc:
                self.notes.append(f'frame {k}: uses {f.used} B, slot is {f.alloc} B')
            if f.field0 != 0:
                self.notes.append(f'frame {k}: word +0 is {f.field0}')
            self.frames.append(f)

    def summary(self):
        raw = sum(f.raw_rows for f in self.frames)
        rle = sum(f.rle_rows for f in self.frames)
        return (f'{self.name}: {self.count} frames, header consts {self.consts}, '
                f'{raw} raw rows / {rle} run rows, all frames exact'
                + ('' if not self.notes else '; NOTES: ' + '; '.join(self.notes)))


def export(path, out_dir, exe, palette='ega', opaque=False, factor=1, quiet=False):
    data = open(path, 'rb').read()
    stem = os.path.basename(path).upper().replace('.', '_')      # e.g. CLOSE_PAN, CLOSE_PAK
    pak = Pak(data, stem)
    pal = game_palette(exe, palette)
    os.makedirs(out_dir, exist_ok=True)
    items = []
    tsv = ['index\toffset\tslot\tused\twidth_px\theight\tx_off_px\ty_off\tfA\tfC\tfE\traw_rows\trle_rows']
    for f in pak.frames:
        im = indexed_image(f.width, f.height, f.pixels(), pal,
                           None if opaque else TRANSPARENT)
        scale(im, factor).save(os.path.join(out_dir, f'{stem}_{f.index:03d}.png'))
        items.append((f.index, im))
        tsv.append('\t'.join(str(v) for v in (
            f.index, hex(f.offset), f.alloc, f.used, f.width, f.height, f.x_off * 2, f.y_off,
            f.fa, f.fc, f.fe, f.raw_rows, f.rle_rows)))
    contact_sheet(items, os.path.join(out_dir, f'{stem}_sheet.png'), factor=2)
    with open(os.path.join(out_dir, f'{stem}_frames.tsv'), 'w', newline='\n') as fh:
        fh.write('\n'.join(tsv) + '\n')
    if not quiet:
        print(pak.summary())
    return pak


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('input', nargs='?', help='a .PAK file, or with --all the TM-DOS folder')
    ap.add_argument('out', nargs='?', default=DEFAULT_GFX_OUT, help='output folder')
    ap.add_argument('--all', action='store_true', help='decode every *.PAK in the folder')
    ap.add_argument('--opaque', action='store_true', help='draw colour 0 instead of transparency')
    ap.add_argument('--scale', type=int, default=1)
    ap.add_argument('--quiet', action='store_true')
    add_common_args(ap)
    a = ap.parse_args(argv)
    if a.all:
        folder = a.input or DEFAULT_DOS_DIR
        files = sorted(set(os.path.normcase(f) for f in
                           glob.glob(os.path.join(folder, '*.PAK')) + glob.glob(os.path.join(folder, '*.pak'))))
    else:
        if not a.input:
            ap.error('give a .PAK file or --all')
        files = [a.input]
    exe = find_exe(a.exe, files[0] if files else None)
    if exe is None and a.palette != 'identity':
        print('warning: SST.EXE not found; showing raw RGBI colours', file=sys.stderr)
    for f in files:
        export(f, a.out, exe, a.palette, a.opaque, a.scale, a.quiet)


if __name__ == '__main__':
    main()
