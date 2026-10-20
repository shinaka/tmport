"""Gizmos & Gadgets (DOS v1.0): 'fnt' 0x1A fonts (resources 0x0009 and 0x000a) to PNG and TSV.

Spec: re/gg/docs/fmt_graphics.md §5. Code (Ghidra SSSS:OOOO): loader 2b16:0000 (fonts 9 and
10 loaded by 2b16:021c), draw char 2b16:0368, char width 2b16:03ea, line height 2b16:0418,
1-bpp glyph blit 1399:0541.

    +0   'fnt' 0x1A
    +4   u16 flags          bit 0 set: 1-bpp glyphs drawn by 1399:0541 in the text colour
                            (DS:1f6c); clear: glyphs are LPX pictures drawn by 1399:02ee
    +6   u16 count          glyph records, indexed by character code from 0
    +8   u16 height         2b16:0418 returns height + 1 as the line height
    +10  u16 spacing        added to every glyph width for the advance (2b16:03ea)
    +12  count records:     u16 y offset, u16 width (0 = no glyph);
                            if width != 0: u16 size, then size bytes of glyph
    1-bpp glyph: u8 full bytes per row, u8 bits in the last partial byte, u16 rows,
                 rows x (full + (bits > 0)) bytes; bit 0 = leftmost pixel; set bits are drawn,
                 clear bits are left alone
    Space (0x20) is never drawn and advances 3 pixels; other codes advance width + spacing.

CLI (python -P tools/gg/fnt.py ...)::

    fnt.py [ID ...] [--out DIR] [--scale N] [--text STRING]
        writes fnt_<id>.png (all glyphs, labelled grid), fnt_<id>.tsv (metrics) and, with
        --text, fnt_<id>_text.png (the string set like the game: advance = width + spacing)
    fnt.py --check
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ggarc  # noqa: E402

FNT_MAGIC = b'fnt\x1a'
SPACE_ADVANCE = 3          # 2b16:0368 / 03ea: ' ' -> 3, nothing drawn
# Key-cap pictograms in the UI font (observed in the glyph shapes; codes used in text)
KEYCAP_NAMES = {0x11: 'Spacebar', 0x17: 'Ctrl', 0x18: 'Enter', 0x19: 'Left', 0x1A: 'Right',
                0x1B: 'Down', 0x1C: 'Up', 0x1D: 'Ctrl+R', 0x1F: 'Esc'}


class FontError(ValueError):
    pass


class Glyph:
    __slots__ = ('code', 'yoff', 'width', 'size', 'full', 'rem', 'rows', 'bpr', 'bits', 'pic')

    def pixel(self, x, y):
        return (self.bits[y * self.bpr + (x >> 3)] >> (x & 7)) & 1

    @property
    def pixel_width(self):
        return self.full * 8 + self.rem


class Font:
    __slots__ = ('flags', 'count', 'height', 'spacing', 'glyphs', 'end')

    def advance(self, ch):
        if ch == 0x20:
            return SPACE_ADVANCE
        g = self.glyphs.get(ch)
        return (g.width if g else 0) + self.spacing


def parse(p):
    if len(p) < 12 or p[:4] != FNT_MAGIC:
        raise FontError('not a fnt resource (magic %r)' % bytes(p[:4]))
    f = Font()
    f.flags, f.count, f.height, f.spacing = struct.unpack_from('<4H', p, 4)
    pos = 12
    f.glyphs = {}
    for c in range(f.count):
        if pos + 4 > len(p):
            raise FontError('glyph record %d past the end' % c)
        yoff, width = struct.unpack_from('<hH', p, pos)
        pos += 4
        if width == 0:
            continue
        g = Glyph()
        g.code, g.yoff, g.width = c, yoff, width
        g.size = struct.unpack_from('<H', p, pos)[0]
        pos += 2
        data = p[pos:pos + g.size]
        pos += g.size
        if len(data) != g.size:
            raise FontError('glyph %02x data past the end' % c)
        if f.flags & 1:
            g.full, g.rem, g.rows = struct.unpack_from('<BBH', data, 0)
            g.bpr = g.full + (1 if g.rem else 0)
            if g.size != 4 + g.bpr * g.rows:
                raise FontError('glyph %02x: size %d != 4 + %d x %d' % (c, g.size, g.bpr, g.rows))
            g.bits = data[4:]
            g.pic = None
        else:
            g.full = g.rem = g.rows = g.bpr = 0
            g.bits = b''
            g.pic = data           # an LPX picture (not used by the shipped fonts)
        f.glyphs[c] = g
    f.end = pos
    return f


def render_grid(font, scale=3, cols=16):
    """All glyphs on a 16-column grid with hex labels; ink black on white."""
    from PIL import Image, ImageDraw
    cw = max([g.pixel_width for g in font.glyphs.values()] + [8]) + 4
    ch = max([g.yoff + g.rows for g in font.glyphs.values()] + [font.height]) + 4
    rows = (font.count + cols - 1) // cols
    lab = 10
    im = Image.new('L', (cols * cw, rows * (ch + lab)), 255)
    dr = ImageDraw.Draw(im)
    for c in range(font.count):
        x0 = (c % cols) * cw
        y0 = (c // cols) * (ch + lab)
        dr.text((x0 + 1, y0), '%02x' % c, fill=160)
        dr.rectangle([x0, y0 + lab, x0 + cw - 2, y0 + lab + ch - 2], outline=220)
        g = font.glyphs.get(c)
        if not g or not g.bits:
            continue
        for y in range(g.rows):
            for x in range(g.pixel_width):
                if g.pixel(x, y):
                    im.putpixel((x0 + 2 + x, y0 + lab + 2 + g.yoff + y), 0)
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


def render_text(font, text, scale=3):
    from PIL import Image
    codes = [ord(ch) for ch in text]
    w = sum(font.advance(c) for c in codes) + 4
    h = max([g.yoff + g.rows for g in font.glyphs.values()] + [font.height + 1]) + 4
    im = Image.new('L', (w, h), 255)
    x = 2
    for c in codes:
        g = font.glyphs.get(c)
        if g and c != 0x20 and g.bits:
            for y in range(g.rows):
                for xx in range(g.pixel_width):
                    if g.pixel(xx, y):
                        im.putpixel((x + xx, 2 + g.yoff + y), 0)
        x += font.advance(c)
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ggarc.add_data_arg(ap)
    ap.add_argument('ids', nargs='*', help='font resource ids (default 0009 000a)')
    ap.add_argument('--out', default=os.path.join(ggarc.DEFAULT_PREVIEWS, 'fonts'))
    ap.add_argument('--scale', type=int, default=3)
    ap.add_argument('--text', default=None, help='also set this string (\\xNN escapes allowed)')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args(argv)
    try:
        arc = ggarc.Archive(a.data)
        ids = [ggarc.parse_rid(i) for i in a.ids] or list(ggarc.FONT_IDS)
        errors = 0
        if not a.check:
            out = ggarc.safe_out_dir(a.out, arc.data)
        for rid in ids:
            p = arc.payload(rid)
            try:
                f = parse(p)
            except FontError as e:
                errors += 1
                print('ERROR %04x: %s' % (rid, e))
                continue
            print('%04x: flags %d, %d records, %d glyphs, height %d, spacing %d, %d of %d bytes'
                  % (rid, f.flags, f.count, len(f.glyphs), f.height, f.spacing, f.end, len(p)))
            if f.end != len(p):
                errors += 1
                print('ERROR %04x: %d bytes after the last glyph' % (rid, len(p) - f.end))
            if a.check:
                continue
            render_grid(f, a.scale).save(os.path.join(out, 'fnt_%04x.png' % rid))
            with open(os.path.join(out, 'fnt_%04x.tsv' % rid), 'w', newline='\n') as tf:
                tf.write('code\tchar\tyoff\twidth\tpixel_width\trows\tadvance\tnote\n')
                for c, g in sorted(f.glyphs.items()):
                    tf.write('%02x\t%s\t%d\t%d\t%d\t%d\t%d\t%s\n'
                             % (c, chr(c) if 0x21 <= c < 0x7F else '', g.yoff, g.width,
                                g.pixel_width, g.rows, f.advance(c), KEYCAP_NAMES.get(c, '')))
            if a.text:
                t = a.text.encode('latin-1').decode('unicode_escape')
                render_text(f, t, a.scale).save(os.path.join(out, 'fnt_%04x_text.png' % rid))
        if not a.check:
            print('-> %s' % out)
        return 1 if errors else 0
    except ggarc.ArchiveError as e:
        print('fnt: %s' % e, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
