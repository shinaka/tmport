"""Gizmos & Gadgets (DOS v1.0): LPX pictures and palette resources to PNG.

Spec: re/gg/docs/fmt_graphics.md §2 (LPX), §3 (palettes), §4 (pairing). Code (Ghidra SSSS:OOOO):

    LPX picture (types G and P), little-endian:
        +0   'LPX' 0x1A
        +4   u16 format          0 = raw 8 bpp rows, 1 = skip/literal RLE rows
        +6   u16 width
        +8   u16 height
        +10  u16 key             low byte = colour key of the keyed raw blit (1399:0399)
        +12  u16 data offset     relative to +12 (2, or 0 in 55 RLE pictures whose stream
                                 then starts with the offset word 00 00 = skip 0)
    raw:  width * height bytes, row-major
    RLE:  per row: c = byte; c == 0: skip next-byte pixels (left transparent);
          else copy c literal bytes; the row ends when an 8-bit counter that starts at
          (width & 0xFF) reaches 0 (1399:0465; mirrored copy 1399:04d1)

    Palette resource (type P, not LPX): u16 byte offset into the 768-byte DAC table, u16 byte
    count, then count bytes of 6-bit R,G,B; merged into the shadow palette DS:024e
    (1473:02f0 applies it, 1473:041c only stores it). The shadow's initial contents are
    initialised data in ssg.exe (DS:024e), read from the user's ssg.exe here.

CLI (python -P tools/gg/lpx.py ...)::

    lpx.py PATH [--pal ID ...] [--out FILE] [--opaque] [--scale N]
        PATH = resource id ('1002') or bundle member ('0001/3'); a bundle id renders every
        picture member as a sheet. Palettes on top of the base DS:024e: --pal ids in the
        given order (they replace the context palette), else the pairing of
        fmt_graphics.md §4 (level palette, the code's picture/palette pairs, id + 1); the
        palette members of the picture's bundle always come last.
    lpx.py --check            decode every picture and palette and report (no output files)
    lpx.py --palettes [--out DIR]   one swatch PNG per palette resource (over the base)

PNG output is RGBA: pixels an RLE row skips are transparent; raw pictures are drawn keyed
(colour = key low byte transparent) unless --opaque. Colours: 6-bit DAC values v shown as
(v * 255 + 31) // 63 (the rounding DOSBox-X uses), values masked to 6 bits like the DAC.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ggarc  # noqa: E402

LPX_MAGIC = b'LPX\x1a'
LPX_HEADER = 14
FMT_RAW, FMT_RLE = 0, 1
EXE_DS_SHADOW_PALETTE = 0x024E      # DS:024e, 768 bytes of 6-bit RGB, initialised data

# Picture -> palette(s) as the code pairs them (1473:0399(picture, palette) and 1473:041c):
CODE_PAIRS = {
    0x1002: (0x1000,),        # 2c11:043f title (2c11:07e5 draws it again with 0x1004)
    0x1009: (0x100A,),        # 2c11:0630 story pages
    0x1029: (0x102A,),        # 2c11:084c, 2caa:016e sign-in
    0x1076: (0x1077,),        # 2fcb:0000 race results
    0x1093: (0x1094,),        # 33cd:1ba1 energy-forms puzzle
    0x1133: (0x1134,),        # 3797:040d launch puzzle
    0x119F: (0x11A0,),        # 35bb:1c03 magnets puzzle
    0x2021: (0x2022,),        # 3b2f:071c gears puzzle
    0x204A: (0x204B,),        # 3938:0e76 simple-machines puzzle
    0x2084: (0x2085,),        # 3a4a:0be5 machine-pieces puzzle
    0x20B2: (0x20B3,),        # 3083:250e electric puzzle
    0x2119: (0x211A,),        # 32fe:0aa0 balance puzzle
    # race backgrounds: DS:399a[race] with palette id + 1, then 0xf6 stored (3dd6:0762);
    # aircraft races also store 0x245 (3e77:0a23)
    0x00DF: (0x00E0, 0x00F6),
    0x00E1: (0x00E2, 0x00F6),
    0x00E3: (0x00E4, 0x00F6),
}


class LpxError(ValueError):
    pass


# ============================================================================================
# Palettes
# ============================================================================================

def expand6(v):
    v &= 0x3F
    return (v * 255 + 31) // 63


def parse_palette(p):
    """-> (byte offset, byte count, rgb bytes, extra bytes after them)."""
    if len(p) < 4:
        raise LpxError('palette shorter than its header')
    off, cnt = struct.unpack_from('<HH', p, 0)
    if off + cnt > 768 or len(p) < 4 + cnt:
        raise LpxError('palette range %d+%d does not fit (%d bytes)' % (off, cnt, len(p)))
    return off, cnt, p[4:4 + cnt], p[4 + cnt:]


class Palette:
    """The 768-byte 6-bit shadow palette (DS:024e) and its RGB view."""

    def __init__(self, base=None):
        self.dac = bytearray(base if base is not None else grey_base())

    def copy(self):
        return Palette(self.dac)

    def apply(self, payload):
        off, cnt, rgb, _ = parse_palette(payload)
        self.dac[off:off + cnt] = rgb
        return self

    def rgb(self):
        return bytes(expand6(v) for v in self.dac)

    def rgb_tuple(self, i):
        return tuple(expand6(v) for v in self.dac[3 * i:3 * i + 3])


def grey_base():
    """Fallback base when ssg.exe is not available: a grey ramp."""
    return bytes(v for i in range(256) for v in (i >> 2,) * 3)


def exe_base_palette(data):
    """DS:024e of the user's ssg.exe (the shadow palette before any resource is applied),
    or None."""
    path = os.path.join(data, 'ssg.exe')
    try:
        with open(path, 'rb') as f:
            d = f.read()
    except OSError:
        return None
    o = ggarc.EXE_DS_FILE_OFFSET + EXE_DS_SHADOW_PALETTE
    if len(d) < o + 768 or d[ggarc.EXE_DS_FILE_OFFSET + 4:ggarc.EXE_DS_FILE_OFFSET + 15] != b'Borland C++':
        return None
    return d[o:o + 768]


def base_palette(arc):
    b = exe_base_palette(arc.data)
    return Palette(b if b is not None else grey_base()), b is not None


def is_palette_resource(arc, rid):
    if not arc.exists(rid):
        return False
    e = arc.entry(rid)
    return ggarc.classify(e.type, e.payload, rid) == 'palette'


LEVEL_FIRST, LEVELS = 0x02F5, 15      # level descriptions (153f:0002: 0x2f5 + DS:4590)
LEVEL_PALETTE, LEVEL_BUNDLE = 8, 10   # word offsets: +8 palette (stored by 2dee:028f via
                                      # 1473:041c), +0xa sprite bundle with a palette member
DAM_FRAME_BUNDLES = range(0x1157, 0x1163)   # frames of the launch puzzle's DAM scripts
DAM_PALETTE = 0x1134                         # that puzzle's screen palette (3797:040d)


def _level_palettes(arc):
    """sprite bundle id -> level palette id, from the 15 level descriptions."""
    out = {}
    for k in range(LEVELS):
        rid = LEVEL_FIRST + k
        if not arc.exists(rid):
            break
        p = arc.payload(rid)
        if len(p) >= 12:
            pal, bun = struct.unpack_from('<HH', p, LEVEL_PALETTE)
            if is_palette_resource(arc, pal):
                out.setdefault(bun, pal)
    return out


def palettes_for(arc, leaf, extra_ids=None):
    """Ordered palette payloads for a picture leaf (fmt_graphics.md §4) and a note saying
    where they came from. Order: extra_ids (from the command line), the context palette
    (level palette, DAM puzzle palette, the code's picture/palette pairs, id + 1), then the
    palette members of the leaf's bundle."""
    rid = leaf.rid
    pals, notes = [], []
    for q in extra_ids or ():
        pals.append(arc.payload(q))
        notes.append('%04x' % q)
    e = arc.entry(rid)
    if e.type == 0x01:
        if not hasattr(arc, '_gg_level_pals'):
            arc._gg_level_pals = _level_palettes(arc)
        ctx = arc._gg_level_pals.get(rid)
        if ctx is None and rid in DAM_FRAME_BUNDLES:
            ctx = DAM_PALETTE
        if ctx is not None and not extra_ids:
            pals.append(arc.payload(ctx))
            notes.append('%04x' % ctx)
        # the bundle's palette members, in member order (15df:023b applies every 'P' member
        # it meets; 15df:00a1 / 01a5 look only at member 0)
        mem = ggarc.parse_bundle(e.payload)
        for m in mem:
            if ggarc.classify(m.type, m.payload, rid, m.index) == 'palette':
                pals.append(m.payload)
                notes.append('%04x/%d' % (rid, m.index))
    elif not extra_ids:
        if rid in CODE_PAIRS:
            for q in CODE_PAIRS[rid]:
                if arc.exists(q):
                    pals.append(arc.payload(q))
                    notes.append('%04x' % q)
        elif is_palette_resource(arc, rid + 1):
            pals.append(arc.payload(rid + 1))
            notes.append('%04x' % (rid + 1))
    return pals, ('base+' + '+'.join(notes)) if notes else 'base only'


# ============================================================================================
# LPX pictures
# ============================================================================================

class Picture:
    __slots__ = ('fmt', 'w', 'h', 'key', 'data_off', 'pixels', 'opaque', 'used', 'counter_mismatch')


def lpx_header(p):
    if len(p) < LPX_HEADER or p[:4] != LPX_MAGIC:
        raise LpxError('not an LPX picture (magic %r)' % bytes(p[:4]))
    return struct.unpack_from('<5H', p, 4)


def decode_lpx(p):
    """Decode an LPX payload. pixels = w*h colour indices; opaque = w*h flags (0 where an RLE
    row skipped the pixel). used = bytes of the payload consumed."""
    fmt, w, h, key, data_off = lpx_header(p)
    pic = Picture()
    pic.fmt, pic.w, pic.h, pic.key, pic.data_off = fmt, w, h, key, data_off
    pic.counter_mismatch = 0
    start = 12 + data_off
    n = w * h
    if fmt == FMT_RAW:
        if start + n > len(p):
            raise LpxError('raw picture %dx%d needs %d bytes, has %d' % (w, h, n, len(p) - start))
        pic.pixels = bytearray(p[start:start + n])
        pic.opaque = bytearray(b'\x01') * n
        pic.used = start + n
        return pic
    if fmt != FMT_RLE:
        raise LpxError('unknown LPX format %d' % fmt)
    px = bytearray(n)
    op = bytearray(n)
    i = start
    for y in range(h):
        x = 0
        cnt = w & 0xFF             # 1399:0465: MOV BH,BL / MOV BL,BH (8-bit width counter)
        row = y * w
        while True:
            if i >= len(p):
                raise LpxError('RLE data ends in row %d' % y)
            c = p[i]
            i += 1
            if c == 0:
                if i >= len(p):
                    raise LpxError('RLE data ends in a skip in row %d' % y)
                k = p[i]
                i += 1
                x += k
            else:
                k = c
                if x + k > w or i + k > len(p):
                    raise LpxError('RLE literal run overflows row %d' % y)
                px[row + x:row + x + k] = p[i:i + k]
                op[row + x:row + x + k] = b'\x01' * k
                i += k
                x += k
            cnt = (cnt - k) & 0xFF
            if cnt == 0:
                break
            if x > w + 255:
                raise LpxError('RLE row %d never ends' % y)
        if x != w:
            pic.counter_mismatch += 1
            if x > w:
                raise LpxError('RLE row %d is %d pixels wide, picture %d' % (y, x, w))
    pic.pixels, pic.opaque, pic.used = px, op, i
    return pic


def to_image(pic, pal, keyed=True, transparent_rgb=None):
    """RGBA Pillow image of a decoded picture."""
    from PIL import Image
    size = (pic.w, pic.h)
    im = Image.frombytes('P', size, bytes(pic.pixels))
    im.putpalette(pal.rgb())
    im = im.convert('RGBA')
    if keyed and pic.fmt == FMT_RAW:
        key = pic.key & 0xFF
        table = bytes(0 if c == key else 255 for c in range(256))
        alpha = bytes(pic.pixels).translate(table)
    else:
        alpha = bytes(pic.opaque).translate(bytes([0, 255]) + bytes(254))
    if transparent_rgb is not None:
        bg = Image.new('RGBA', size, tuple(transparent_rgb) + (255,))
        im.putalpha(Image.frombytes('L', size, alpha))
        bg.alpha_composite(im)
        return bg
    im.putalpha(Image.frombytes('L', size, alpha))
    return im


def sheet(images, gap=4, bg=(48, 48, 48, 255), max_width=1280):
    """Pictures side by side, wrapped into rows."""
    from PIL import Image
    if not images:
        return Image.new('RGBA', (1, 1), bg)
    rows, row, x, w_max = [], [], 0, 0
    for im in images:
        if row and x + im.width > max_width:
            rows.append(row)
            row, x = [], 0
        row.append(im)
        x += im.width + gap
        w_max = max(w_max, x)
    rows.append(row)
    H = sum(max(i.height for i in r) + gap for r in rows) + gap
    s = Image.new('RGBA', (w_max + gap, H), bg)
    y = gap
    for r in rows:
        x = gap
        for im in r:
            s.alpha_composite(im, (x, y))
            x += im.width + gap
        y += max(i.height for i in r) + gap
    return s


def swatch(pal, off=0, cnt=768, cell=10):
    """16x16 grid of the palette; entries outside [off, off+cnt) are drawn dimmed."""
    from PIL import Image, ImageDraw
    im = Image.new('RGB', (16 * cell, 16 * cell), (0, 0, 0))
    dr = ImageDraw.Draw(im)
    for i in range(256):
        r, g, b = pal.rgb_tuple(i)
        inside = off // 3 <= i < (off + cnt) // 3
        if not inside:
            r, g, b = r // 4, g // 4, b // 4
        x, y = (i % 16) * cell, (i // 16) * cell
        dr.rectangle([x, y, x + cell - 2, y + cell - 2], fill=(r, g, b))
    return im


def render_leaf(arc, leaf, pal_ids=None, keyed=True, base=None):
    """-> (Pillow RGBA image, Picture, palette note)."""
    pic = decode_lpx(leaf.payload)
    pal = (base or base_palette(arc)[0]).copy()
    pals, note = palettes_for(arc, leaf, pal_ids)
    for p in pals:
        pal.apply(p)
    return to_image(pic, pal, keyed), pic, note


# ============================================================================================
# CLI
# ============================================================================================

def _check(arc):
    import collections
    stats = collections.Counter()
    errors = 0
    for lf in ggarc.walk_leaves(arc):
        if lf.kind == 'picture':
            try:
                pic = decode_lpx(lf.payload)
            except LpxError as e:
                errors += 1
                print('ERROR %s: %s' % (lf.path, e))
                continue
            stats['pictures'] += 1
            stats['fmt%d' % pic.fmt] += 1
            if pic.used != len(lf.payload):
                stats['size_mismatch'] += 1
                print('note %s: used %d of %d bytes' % (lf.path, pic.used, len(lf.payload)))
            if pic.fmt == FMT_RLE and pic.w > 255:
                stats['rle_wider_than_255'] += 1
            if pic.counter_mismatch:
                stats['rows_counter_mismatch'] += pic.counter_mismatch
            stats['key=%d' % pic.key] += 1
            stats['data_off=%d' % pic.data_off] += 1
        elif lf.kind == 'palette':
            try:
                off, cnt, rgb, extra = parse_palette(lf.payload)
            except LpxError as e:
                errors += 1
                print('ERROR %s: %s' % (lf.path, e))
                continue
            stats['palettes'] += 1
            if extra:
                stats['palettes_with_%d_extra_bytes' % len(extra)] += 1
            if any(v > 63 for v in rgb):
                stats['palette_values_over_63'] += 1
    for k in sorted(stats):
        print('  %s: %d' % (k, stats[k]))
    print('lpx check: %d pictures (%d raw, %d RLE; %d not ending at the last byte), %d palettes; '
          '%d error%s' % (stats['pictures'], stats['fmt0'], stats['fmt1'], stats['size_mismatch'],
                          stats['palettes'], errors, '' if errors == 1 else 's'))
    return errors


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ggarc.add_data_arg(ap)
    ap.add_argument('path', nargs='?', help="resource id or bundle member ('1002', '0001/3')")
    ap.add_argument('--pal', action='append', default=None, help='palette id (repeatable)')
    ap.add_argument('--out', help='output PNG (default: extracted/previews/gg/lpx_<path>.png) '
                                  'or directory for --palettes')
    ap.add_argument('--opaque', action='store_true', help='draw raw pictures without the key')
    ap.add_argument('--scale', type=int, default=1)
    ap.add_argument('--check', action='store_true', help='decode everything, report')
    ap.add_argument('--palettes', action='store_true', help='swatches of all palette resources')
    a = ap.parse_args(argv)
    try:
        arc = ggarc.Archive(a.data)
        if a.check:
            return 1 if _check(arc) else 0
        base, from_exe = base_palette(arc)
        if a.palettes:
            out = ggarc.safe_out_dir(a.out or os.path.join(ggarc.DEFAULT_PREVIEWS, 'palettes'), arc.data)
            n = 0
            for lf in ggarc.walk_leaves(arc):
                if lf.kind != 'palette':
                    continue
                off, cnt, _, _ = parse_palette(lf.payload)
                pal = base.copy().apply(lf.payload)
                fn = os.path.join(out, 'pal_%s.png' % lf.path.replace('/', '_'))
                swatch(pal, off, cnt).save(fn)
                n += 1
            swatch(base).save(os.path.join(out, 'pal_base_ds024e.png'))
            print('%d palette swatches -> %s (base from %s)' % (n, out, 'ssg.exe DS:024e'
                                                                if from_exe else 'grey ramp'))
            return 0
        if not a.path:
            ap.error('give a resource id, --check or --palettes')
        pal_ids = [ggarc.parse_rid(q) for q in a.pal] if a.pal else None
        leaf = ggarc.find_leaf(arc, a.path)
        if leaf.kind == 'bundle':
            mem = ggarc.parse_bundle(leaf.payload)
            pal = base.copy()
            pals, pnote = palettes_for(arc, leaf, pal_ids)
            for p in pals:
                pal.apply(p)
            ims = [to_image(decode_lpx(m.payload), pal, not a.opaque) for m in mem
                   if m.payload[:4] == LPX_MAGIC]
            im = sheet(ims)
            note = '%d pictures, %s' % (len(ims), pnote)
        elif leaf.kind == 'picture':
            im, pic, note = render_leaf(arc, leaf, pal_ids, not a.opaque, base)
            note = '%dx%d fmt %d, %s' % (pic.w, pic.h, pic.fmt, note)
        else:
            raise ggarc.ArchiveError('%s is a %s, not a picture' % (leaf.path, leaf.kind))
        if a.scale > 1:
            from PIL import Image
            im = im.resize((im.width * a.scale, im.height * a.scale), Image.NEAREST)
        out = a.out or os.path.join(ggarc.DEFAULT_PREVIEWS, 'lpx_%s.png' % leaf.path.replace('/', '_'))
        ggarc.safe_out_file(out, arc.data)
        im.save(out)
        print('%s -> %s (%s)' % (leaf.path, out, note))
    except (ggarc.ArchiveError, LpxError) as e:
        print('lpx: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
