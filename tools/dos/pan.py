"""Decoder for Treasure Mountain DOS-edition .PAN pictures (OPENING, MOUNTAIN, MOUNT2N3,
MOUNT3, CLOSE, SHACK).

A .PAN file is a tile map plus a set of 8x8 4bpp tiles, both compressed:

    u16 map_packed_size   (bytes of block 1)
    u16 width_tiles       (tiles per picture row: 40 for OPENING, 16 for the others)
    u16 height_tiles      (tiles per picture column: 25 for OPENING, 16 for the others)
    u16 tiles_packed_size (bytes of block 2)
    block 1: map, word-RLE, decoded by SST.EXE 1000:2368 / 1000:23b9
    block 2: tiles, byte-RLE with escape 0xA5, decoded by 1000:240e

The map holds one or more width x height "pictures" (OPENING: one 320x200 screen; the
others: 128x128 scenery blocks that the game lays out on a grid). See
re/dos/docs/fmt_graphics.md for the full description and the EXE evidence.

CLI::

    python -P pan.py FILE.PAN [OUT_DIR] [--exe SST.EXE] [--palette ega|tandy|mcga]
                     [--scale N] [--tiles] [--layout] [--quiet]
    python -P pan.py --all [TM-DOS_DIR] [OUT_DIR]     # every *.PAN in the folder

Writes <NAME>_PAN_<nnn>.png per picture, <NAME>_PAN_sheet.png, and with --tiles
<NAME>_PAN_tiles.png (e.g. MOUNTAIN_PAN_012.png).
No game data is embedded here; everything is read from the user's files.
"""
import argparse
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dos_gfx_common import (DEFAULT_DOS_DIR, DEFAULT_GFX_OUT, DS_LAYOUT, add_common_args,  # noqa: E402
                            contact_sheet, find_exe, game_palette, indexed_image, scale)

# Buffer limits implied by the loader 1460:05e8 (packed data is read to the END of each
# buffer and decompressed in place towards its start).
MAP_BUF_SIZE = 0xA400     # segment 2897, end offset used by 1460:0654 and 1000:23ab
TILE_BUF_SIZE = 0xE880    # segment 32d7, end offset used by 1460:0678 and 1000:2424
TILE_BYTES = 32           # 8x8 pixels, 4 bytes per row, packed 4bpp, high nibble = left
BACKREF_LEN = 16          # 1000:23e9: a 0xFFF code copies a fixed 16 entries


class PanError(ValueError):
    pass


class Pan:
    def __init__(self, data: bytes, name='PAN'):
        if len(data) < 8:
            raise PanError('file too short')
        self.name = name
        self.map_size, self.width, self.height, self.tile_size = struct.unpack_from('<4H', data)
        end = 8 + self.map_size + self.tile_size
        if end != len(data):
            raise PanError(f'header sizes give {end} bytes, file has {len(data)}')
        self.map_packed = data[8:8 + self.map_size]
        self.tile_packed = data[8 + self.map_size:end]
        self.notes = []
        self.map_words = self._decode_map()
        self.tiles = self._decode_tiles()
        self.picture_words = self.width * self.height
        if len(self.map_words) % self.picture_words:
            raise PanError('map length is not a whole number of pictures')
        self.count = len(self.map_words) // self.picture_words
        self.n_tiles = len(self.tiles) // TILE_BYTES
        if len(self.tiles) % TILE_BYTES:
            raise PanError('tile data is not a whole number of 32-byte tiles')
        for v in self.map_words:
            if v & 1 or (v // 2) >= self.n_tiles:
                raise PanError(f'map entry {v:#x} does not address a tile')

    # -- block 1: map (1000:2368 outer loop, 1000:23b9 per row) -----------------------
    def _decode_map(self):
        src = self.map_packed
        a, b = self.width, self.height
        stride = b                      # BP = 2*height bytes = height words (1000:2377)
        out = {}
        si = 0
        base = 0                        # word index of the current picture
        in_start = MAP_BUF_SIZE - len(src)   # byte offset where block 1 sits in the buffer
        unsafe = 0
        backrefs = 0
        while si < len(src):            # 1000:23ab: loop until SI reaches the buffer end
            for c in range(b):          # one call of 1000:23b9 per tile row of the picture
                di = base + c
                left = a                # BX = width (entries per row)
                while left:
                    if si + 2 > len(src):
                        raise PanError('map stream ends inside a row')
                    w = src[si] | src[si + 1] << 8
                    si += 2
                    val, n = w & 0xFFF, w >> 12
                    if n:               # run: value repeated n+1 times
                        reps = n + 1
                        for _ in range(reps):
                            if di * 2 + 2 > in_start + si:
                                unsafe += 1
                            out[di] = val
                            di += stride
                            left -= 1
                    elif val == 0xFFF:  # copy a 16-entry row from an earlier word index
                        ref = src[si] | src[si + 1] << 8
                        si += 2
                        backrefs += 1
                        if a != BACKREF_LEN:
                            self.notes.append('back-reference in a picture that is not 16 wide')
                        s = ref
                        for _ in range(BACKREF_LEN):
                            if s not in out:
                                raise PanError(f'back-reference to undecoded word {s}')
                            out[di] = out[s]
                            di += stride
                            s += stride
                        left = 0
                    else:               # literal
                        if di * 2 + 2 > in_start + si:
                            unsafe += 1
                        out[di] = val
                        di += stride
                        left -= 1
                    if left < 0:
                        raise PanError('run crosses the end of a row')
            base += a * b
        if si != len(src):
            raise PanError(f'map decoder consumed {si} of {len(src)} bytes')
        if len(out) != base or min(out) != 0 or max(out) != base - 1:
            raise PanError('map has holes')
        if base * 2 > MAP_BUF_SIZE:
            self.notes.append('decoded map larger than the 0xA400 buffer')
        if unsafe:
            self.notes.append(f'{unsafe} map writes overtake the in-place input')
        self.backrefs = backrefs
        return [out[i] for i in range(base)]

    # -- block 2: tiles (1000:240e) -----------------------------------------------------
    def _decode_tiles(self):
        src = self.tile_packed
        out = bytearray()
        i = 0
        in_start = TILE_BUF_SIZE - len(src)
        unsafe = 0
        while i < len(src):             # loop until SI == 0xE880
            c = src[i]
            i += 1
            if c != 0xA5:
                out.append(c)
            else:
                c2 = src[i]
                i += 1
                if c2 == 0xA5:          # A5 A5 -> literal A5
                    out.append(0xA5)
                else:                   # A5 n v -> n copies of v
                    v = src[i]
                    i += 1
                    out += bytes([v]) * c2
            if len(out) > in_start + i:
                unsafe += 1
        if i != len(src):
            raise PanError(f'tile decoder consumed {i} of {len(src)} bytes')
        if len(out) > TILE_BUF_SIZE:
            self.notes.append('decoded tiles larger than the 0xE880 buffer')
        if unsafe:
            self.notes.append(f'{unsafe} tile writes overtake the in-place input')
        return bytes(out)

    # -- rendering ------------------------------------------------------------------------
    def tile_pixels(self, entry):
        """Map entry (paragraph offset into the tile buffer) -> 64 logical colours."""
        off = entry * 16
        t = self.tiles[off:off + TILE_BYTES]
        px = []
        for b in t:
            px.append(b >> 4)
            px.append(b & 15)
        return px

    def picture(self, k):
        """Return (w, h, pixels) of picture k (logical colours)."""
        W, H = self.width * 8, self.height * 8
        pix = bytearray(W * H)
        base = k * self.picture_words
        for col in range(self.width):
            for row in range(self.height):
                px = self.tile_pixels(self.map_words[base + col * self.height + row])
                for y in range(8):
                    o = (row * 8 + y) * W + col * 8
                    pix[o:o + 8] = bytes(px[y * 8:y * 8 + 8])
        return W, H, pix

    def tile_sheet(self, per_row=32):
        rows = (self.n_tiles + per_row - 1) // per_row
        W, H = per_row * 9, rows * 9
        pix = bytearray([15]) * (W * H)
        for t in range(self.n_tiles):
            px = self.tile_pixels(t * 2)
            x0, y0 = (t % per_row) * 9, (t // per_row) * 9
            for y in range(8):
                o = (y0 + y) * W + x0
                pix[o:o + 8] = bytes(px[y * 8:y * 8 + 8])
        return W, H, pix

    def summary(self):
        return (f'{self.name}: map {self.map_size} B -> {len(self.map_words)} words '
                f'({self.count} picture(s) of {self.width}x{self.height} tiles = '
                f'{self.width * 8}x{self.height * 8} px, {self.backrefs} row back-refs); '
                f'tiles {self.tile_size} B -> {len(self.tiles)} B = {self.n_tiles} tiles'
                + ('' if not self.notes else '; NOTES: ' + '; '.join(sorted(set(self.notes)))))


def layout_grid(exe):
    """The 16x16 word grid of block ids at DS:0CAA as stored in SST.EXE (see fmt doc)."""
    raw = exe.ds_bytes(DS_LAYOUT, 512)
    return list(struct.unpack('<256H', raw))


def render_layout(pan, grid, pal, shrink=4):
    """Overview of the 16x16 block grid (each block shrunk by `shrink`)."""
    from PIL import Image
    side = 128 // shrink
    out = Image.new('RGB', (16 * side, 16 * side), (0, 0, 0))
    cache = {}
    for r in range(16):
        for c in range(16):
            blk = grid[r * 16 + c] & 0x7F
            if blk >= pan.count:
                continue
            if blk not in cache:
                w, h, px = pan.picture(blk)
                cache[blk] = indexed_image(w, h, px, pal).convert('RGB').resize((side, side))
            out.paste(cache[blk], (c * side, r * side))
    return out


def export(path, out_dir, exe, palette='ega', factor=1, tiles=False, layout=False, quiet=False):
    data = open(path, 'rb').read()
    stem = os.path.basename(path).upper().replace('.', '_')      # e.g. CLOSE_PAN, CLOSE_PAK
    pan = Pan(data, stem)
    pal = game_palette(exe, palette)
    os.makedirs(out_dir, exist_ok=True)
    items = []
    for k in range(pan.count):
        w, h, px = pan.picture(k)
        im = indexed_image(w, h, px, pal)
        scale(im, factor).save(os.path.join(out_dir, f'{stem}_{k:03d}.png'))
        items.append((k, im))
    if pan.count > 1:
        contact_sheet(items, os.path.join(out_dir, f'{stem}_sheet.png'), cols=9)
    if tiles:
        w, h, px = pan.tile_sheet()
        scale(indexed_image(w, h, px, pal), 2).save(os.path.join(out_dir, f'{stem}_tiles.png'))
    if layout and exe is not None and pan.width == 16 and pan.count > 16:
        render_layout(pan, layout_grid(exe), pal).save(os.path.join(out_dir, f'{stem}_layout_ds0caa.png'))
    if not quiet:
        print(pan.summary())
    return pan


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('input', nargs='?', help='a .PAN file, or with --all the TM-DOS folder')
    ap.add_argument('out', nargs='?', default=DEFAULT_GFX_OUT, help='output folder')
    ap.add_argument('--all', action='store_true', help='decode every *.PAN in the folder')
    ap.add_argument('--scale', type=int, default=1)
    ap.add_argument('--tiles', action='store_true', help='also write a sheet of all tiles')
    ap.add_argument('--layout', action='store_true',
                    help='also render the DS:0CAA block grid with this file\'s blocks')
    ap.add_argument('--quiet', action='store_true')
    add_common_args(ap)
    a = ap.parse_args(argv)
    if a.all:
        folder = a.input or DEFAULT_DOS_DIR
        files = sorted(glob.glob(os.path.join(folder, '*.PAN')) + glob.glob(os.path.join(folder, '*.pan')))
        files = sorted(set(os.path.normcase(f) for f in files))
    else:
        if not a.input:
            ap.error('give a .PAN file or --all')
        files = [a.input]
    exe = find_exe(a.exe, files[0] if files else None)
    if exe is None and a.palette != 'identity':
        print('warning: SST.EXE not found; showing raw RGBI colours', file=sys.stderr)
    for f in files:
        export(f, a.out, exe, a.palette, a.scale, a.tiles, a.layout, a.quiet)


if __name__ == '__main__':
    main()
