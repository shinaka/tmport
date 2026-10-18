"""Extractor for the Treasure Mountain DOS-edition text font (the only font; it lives in
SST.EXE's first code segment, used by the glyph blitter 1000:2180).

Font block (address found at run time from the `MOV SI,imm16` at 1000:219a)::

    u8  unknown (7; never read by 1000:2180)
    u8  last_code              -> count = last_code + 1 (128)
    u8  height                 (10 rows)
    u8  width[count]           glyph advance in pixels (also its drawn width)
    u8  off_lo[count]          glyph data offset, low byte
    u8  off_hi[count]          glyph data offset, high byte
    glyph data: height rows of bpr bytes, 1 bpp, MSB = leftmost pixel,
                bpr = max(1, ceil(width / 8))  (table at DS:3C5B, 1 for widths 0..8)

1000:2180 draws set bits in the caller's colour (a word of four equal nibbles, e.g.
0xCCCC = logical colour 12) and leaves clear bits untouched; x is in bytes (2-pixel
steps) and advances by width/2 per character.

CLI::

    python -P dos_font.py SST.EXE [OUT_DIR] [--scale N] [--colour C] [--palette ...]

Writes dos_font_sheet.png (16 x 8 grid of codes 0..127), dos_font_sample.png and
dos_font_metrics.tsv. No game data is embedded here.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dos_gfx_common import (DEFAULT_GFX_OUT, PALETTE_KINDS, game_palette,  # noqa: E402
                            indexed_image, load_exe, scale)

# 1000:219a  BE 47 24        MOV SI,0x2447
# 1000:219d  33 C0           XOR AX,AX
# 1000:219f  26 8A 44 02     MOV AL,ES:[SI+2]
LOCATOR = (bytes.fromhex('be'), bytes.fromhex('33c0268a4402'))


class FontError(ValueError):
    pass


def locate(code: bytes) -> int:
    tail = LOCATOR[1]
    p = code.find(tail)
    while p != -1:
        if p >= 3 and code[p - 3:p - 2] == LOCATOR[0]:
            return code[p - 2] | code[p - 1] << 8
        p = code.find(tail, p + 1)
    raise FontError('glyph blitter signature not found in SST.EXE')


class Font:
    def __init__(self, image: bytes, off: int):
        self.offset = off
        self.unknown0, last, self.height = image[off], image[off + 1], image[off + 2]
        self.count = last + 1
        n = self.count
        self.widths = list(image[off + 3:off + 3 + n])
        lo = image[off + 3 + n:off + 3 + 2 * n]
        hi = image[off + 3 + 2 * n:off + 3 + 3 * n]
        self.offsets = [a | b << 8 for a, b in zip(lo, hi)]
        self.data_off = off + 3 + 3 * n
        self.glyphs = []
        end = 0
        for c in range(n):
            bpr = self.bpr(self.widths[c])
            o = self.data_off + self.offsets[c]
            g = image[o:o + bpr * self.height]
            if len(g) != bpr * self.height:
                raise FontError(f'glyph {c} runs past the image')
            self.glyphs.append(g)
            end = max(end, self.offsets[c] + bpr * self.height)
        self.data_size = end
        # glyphs are packed back to back in code order
        packed = all(self.offsets[c + 1] == self.offsets[c] + self.bpr(self.widths[c]) * self.height
                     for c in range(n - 1))
        self.packed = packed

    @staticmethod
    def bpr(w):
        return max(1, (w + 7) // 8)

    def glyph_rows(self, c):
        """List of rows, each a list of 0/1 of length width (at least 1)."""
        w = max(1, self.widths[c])
        bpr = self.bpr(self.widths[c])
        g = self.glyphs[c]
        rows = []
        for y in range(self.height):
            bits = []
            for x in range(w):
                b = g[y * bpr + x // 8]
                bits.append((b >> (7 - x % 8)) & 1)
            rows.append(bits)
        return rows

    def render(self, text_codes, fg, bg):
        width = sum(self.widths[c] for c in text_codes) or 1
        pix = bytearray([bg]) * (width * self.height)
        x0 = 0
        for c in text_codes:
            for y, row in enumerate(self.glyph_rows(c)):
                for x, bit in enumerate(row[:self.widths[c]]):
                    if bit:
                        pix[y * width + x0 + x] = fg
            x0 += self.widths[c]
        return width, self.height, pix


def export(exe_path, out_dir, factor=3, colour=12, bg=15, palette='ega', quiet=False):
    exe = load_exe(exe_path)
    off = locate(exe.image[:0x10000])
    font = Font(exe.image, off)
    pal = game_palette(exe, palette)
    os.makedirs(out_dir, exist_ok=True)
    cell_w = max(font.widths) + 2
    cell_h = font.height + 2
    W, H = 16 * cell_w, 8 * cell_h
    pix = bytearray([14]) * (W * H)
    for c in range(font.count):
        cx, cy = (c % 16) * cell_w + 1, (c // 16) * cell_h + 1
        for y in range(font.height):
            for x in range(max(1, font.widths[c])):
                pix[(cy + y) * W + cx + x] = bg
        for y, row in enumerate(font.glyph_rows(c)):
            for x, bit in enumerate(row):
                if bit:
                    pix[(cy + y) * W + cx + x] = colour
    scale(indexed_image(W, H, pix, pal), factor).save(os.path.join(out_dir, 'dos_font_sheet.png'))
    sample = 'The quick brown fox jumps over the lazy dog. 0123456789 !?,.'
    w, h, px = font.render([ord(ch) for ch in sample], colour, bg)
    scale(indexed_image(w, h, px, pal), factor).save(os.path.join(out_dir, 'dos_font_sample.png'))
    with open(os.path.join(out_dir, 'dos_font_metrics.tsv'), 'w', newline='\n') as fh:
        fh.write('code\tchar\twidth\toffset\tbytes_per_row\n')
        for c in range(font.count):
            ch = chr(c) if 33 <= c < 127 else ''
            fh.write(f'{c}\t{ch}\t{font.widths[c]}\t{font.offsets[c]}\t{font.bpr(font.widths[c])}\n')
    if not quiet:
        print(f'font at 1000:{off:04x}: {font.count} glyphs, height {font.height}, '
              f'byte0={font.unknown0}, data {font.data_size} B at 1000:{font.data_off:04x}'
              f'..{font.data_off + font.data_size:04x}, packed in code order: {font.packed}')
    return font


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('exe', help='SST.EXE (packed original or unpacked copy)')
    ap.add_argument('out', nargs='?', default=DEFAULT_GFX_OUT)
    ap.add_argument('--scale', type=int, default=3)
    ap.add_argument('--colour', type=int, default=12, help='logical ink colour (default 12)')
    ap.add_argument('--bg', type=int, default=15, help='logical paper colour (default 15)')
    ap.add_argument('--palette', choices=PALETTE_KINDS, default='ega')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args(argv)
    export(a.exe, a.out, a.scale, a.colour, a.bg, a.palette, a.quiet)


if __name__ == '__main__':
    main()
