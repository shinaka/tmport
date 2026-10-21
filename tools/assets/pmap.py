"""Decoder for Treasure Mountain (Windows CD, TMTWINCD) PMAP pixmaps.

PMAP resources live in TMT16.DLL (16-colour, 4 bpp) and TMT256.DLL (256-colour,
8 bpp, normally "RUND" run-length compressed) as NE custom resource type 32513.
The 256-colour palette is the RRGB resource 900 of TMT.DLL (type 32516).
See re/docs/fmt_pmap.md for the full format description and the EXE evidence.

Library use::

    from pmap import decode, load_rrgb, to_image
    pm = decode(open(r'...\\TMT256.DLL\\32513\\1000', 'rb').read(), depth=8)
    pm.width, pm.height, pm.pixels    # pixels: bytes, one palette index per pixel
    img = to_image(pm, load_rrgb(r'...\\TMT.DLL\\32516\\900'))

CLI::

    python -P pmap.py export [--which 16|256|both] [--res-dir DIR | --dll-dir DIR]
                             [--out DIR] [--no-sheets] [--opaque]
    python -P pmap.py info ID [--which 16|256|both]
    python -P pmap.py verify          # decode all, re-encode RUND, compare bytes

No game data is embedded here; everything is read from the user's files.
"""
import argparse
import os
import struct
import sys

TAG_RUND = b'RUND'          # 52 55 4E 44, checked at file offset 4 (EXE 1058:0566)
HEADER_SIZE = 4             # u16 width, u16 height, little-endian

# Transparent colour keys as implemented by the EXE (1048:008b):
KEY_4BPP = 3                # 16-colour mode: nibble value 3
KEY_RGB_8BPP = (0x00, 0x00, 0xAA)   # 256-colour mode: GetNearestPaletteIndex(RGB(0,0,0xAA))

# Standard Windows VGA 16-colour device palette (the 4bpp nibble values are written
# to the bit planes of a 4-plane device bitmap unchanged, EXE 1050:09e7, so their
# colours are those of the display driver).  This is the generic Windows/VGA
# palette, not game data.
VGA16_PALETTE = [
    (0x00, 0x00, 0x00), (0x80, 0x00, 0x00), (0x00, 0x80, 0x00), (0x80, 0x80, 0x00),
    (0x00, 0x00, 0x80), (0x80, 0x00, 0x80), (0x00, 0x80, 0x80), (0x80, 0x80, 0x80),
    (0xC0, 0xC0, 0xC0), (0xFF, 0x00, 0x00), (0x00, 0xFF, 0x00), (0xFF, 0xFF, 0x00),
    (0x00, 0x00, 0xFF), (0xFF, 0x00, 0xFF), (0x00, 0xFF, 0xFF), (0xFF, 0xFF, 0xFF),
]

PMAP_TYPE = '32513'         # NE resource type of PMAP in TMT16.DLL / TMT256.DLL
RRGB_TYPE = '32516'         # NE resource type of RRGB in TMT.DLL
RRGB_ID = '900'

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
DEFAULT_RES = os.path.join(REPO, 'extracted', 'res')
DEFAULT_PREVIEWS = os.path.join(REPO, 'extracted', 'previews')


class PMapError(ValueError):
    pass


class PMap:
    """A decoded pixmap.

    width, height : int
    depth         : 4 or 8 (bits per pixel of the source data)
    pixels        : bytes of length width*height, one palette index (0..15 or 0..255)
                    per pixel, row-major, top row first, no padding
    encoding      : 'raw4' | 'raw8' | 'rund'
    consumed      : number of bytes of the resource actually used (rest is NE padding)
    key           : transparent palette index for this depth (4bpp: 3); for 8bpp this
                    is None because the key depends on the palette (see transparent_set)
    """

    __slots__ = ('width', 'height', 'depth', 'pixels', 'encoding', 'consumed', 'size', 'key', 'rid')

    def __init__(self, width, height, depth, pixels, encoding, consumed, size, rid=None):
        self.width = width
        self.height = height
        self.depth = depth
        self.pixels = pixels
        self.encoding = encoding
        self.consumed = consumed
        self.size = size
        self.key = KEY_4BPP if depth == 4 else None
        self.rid = rid

    def __repr__(self):
        return (f'PMap(id={self.rid}, {self.width}x{self.height}, {self.depth}bpp, '
                f'{self.encoding}, used {self.consumed}/{self.size} bytes)')

    def row(self, y):
        return self.pixels[y * self.width:(y + 1) * self.width]


def read_header(data):
    if len(data) < HEADER_SIZE:
        raise PMapError('PMAP shorter than its 4-byte header')
    return struct.unpack_from('<HH', data, 0)


def is_rund(data):
    return data[4:8] == TAG_RUND


def rund_decompress(data, width, height, start=8):
    """RUND decoder, equivalent to EXE 1058:0400.

    Control byte c: n = c & 0x7F.  c & 0x80 == 0 -> copy the next n bytes literally;
    c & 0x80 != 0 -> repeat the next byte n times.  Stops once width*height pixels
    have been produced (runs may cross row boundaries).  Returns (pixels, end_offset).
    """
    total = width * height
    out = bytearray()
    p = start
    n_data = len(data)
    while len(out) < total:
        if p >= n_data:
            raise PMapError(f'RUND stream truncated at {p} ({len(out)}/{total} pixels)')
        c = data[p]
        n = c & 0x7F
        if c & 0x80:
            if p + 1 >= n_data:
                raise PMapError('RUND run truncated')
            out += bytes((data[p + 1],)) * n
            p += 2
        else:
            if p + 1 + n > n_data:
                raise PMapError('RUND literal truncated')
            out += data[p + 1:p + 1 + n]
            p += 1 + n
    if len(out) != total:
        # The EXE writes the overshoot past the end of its w*h buffer; never happens in
        # the shipped data (verified for all 760 RUND images).
        raise PMapError(f'RUND overshoot: {len(out)} > {total}')
    return bytes(out), p


def rund_compress(pixels, width, height):
    """RUND encoder, a faithful re-implementation of EXE 1058:0000 (including its
    quirk of only looking for runs while index < w*h-1 in *header-inclusive*
    coordinates, i.e. no runs start in the last 5 pixels).  Returns the complete
    resource body: header + b'RUND' + stream."""
    hdr = struct.pack('<HH', width, height)
    src = hdr + bytes(pixels)
    n2 = width * height            # lVar2
    n3 = n2 + 4                    # lVar3 (buffer length incl. header)
    out = bytearray(hdr) + TAG_RUND
    i = 4

    def run4(j):
        return j < n2 - 1 and src[j] == src[j + 1] == src[j + 2] == src[j + 3]

    while i < n3:
        ctl_pos = len(out)
        if run4(i):
            val = src[i]
            out += bytes((0x80, val))
            i += 1
            cnt = 1
            while cnt < 0x7F and i < n3:
                if src[i] != val:
                    break
                cnt += 1
                i += 1
            out[ctl_pos] = 0x80 | cnt
        else:
            out.append(0)
            cnt = 0
            while cnt < 0x7F and i < n3:
                if run4(i):
                    break
                out.append(src[i])
                cnt += 1
                i += 1
            out[ctl_pos] = cnt
    return bytes(out)


def unpack_4bpp(data, width, height, start=HEADER_SIZE):
    """4bpp packed: each row is (width+1)//2 bytes, left pixel in the high nibble."""
    stride = (width + 1) // 2
    need = start + stride * height
    if len(data) < need:
        raise PMapError(f'4bpp PMAP truncated: {len(data)} < {need}')
    out = bytearray(width * height)
    o = 0
    for y in range(height):
        row = data[start + y * stride:start + (y + 1) * stride]
        for x in range(width):
            b = row[x >> 1]
            out[o] = (b >> 4) if (x & 1) == 0 else (b & 0x0F)
            o += 1
    return bytes(out), need


def decode(data, depth=None, rid=None):
    """Decode one PMAP resource body.

    depth: 4 (TMT16.DLL), 8 (TMT256.DLL) or None to guess (RUND tag -> 8; data too
    short for raw 8bpp -> 4; otherwise ambiguous -> PMapError).
    """
    data = bytes(data)
    w, h = read_header(data)
    if w == 0 or h == 0:
        raise PMapError(f'empty PMAP {w}x{h}')
    if depth in (16, 256):
        depth = 4 if depth == 16 else 8
    if depth is None:
        if is_rund(data):
            depth = 8
        elif len(data) < HEADER_SIZE + w * h:
            depth = 4
        else:
            raise PMapError('cannot tell 4bpp from raw 8bpp; pass depth=')
    if depth == 4:
        pixels, used = unpack_4bpp(data, w, h)
        return PMap(w, h, 4, pixels, 'raw4', used, len(data), rid)
    if depth != 8:
        raise PMapError(f'unsupported depth {depth}')
    if is_rund(data):
        pixels, used = rund_decompress(data, w, h)
        return PMap(w, h, 8, pixels, 'rund', used, len(data), rid)
    need = HEADER_SIZE + w * h
    if len(data) < need:
        raise PMapError(f'raw 8bpp PMAP truncated: {len(data)} < {need}')
    return PMap(w, h, 8, data[HEADER_SIZE:need], 'raw8', need, len(data), rid)


def load_rrgb(src):
    """RRGB palette: 256 x (R, G, B) bytes (768 bytes; the NE resource is padded to
    1024).  Loaded by EXE 1118:0000 -> 1048:010a.  src: path or bytes."""
    data = open(src, 'rb').read() if isinstance(src, (str, os.PathLike)) else bytes(src)
    if len(data) < 768:
        raise PMapError('RRGB palette shorter than 768 bytes')
    return [tuple(data[i * 3:i * 3 + 3]) for i in range(256)]


def transparent_set(depth, palette=None, key='default'):
    """Palette indices that the original treats as transparent when drawing sprites.

    key='default' (normal sprite draw, EXE 1048:008b):
        4bpp: {3}.  8bpp: every logical index whose RRGB colour is exactly (0,0,0xAA)
        (the EXE remaps PMAP pixels to system-palette indices and keys on the index
        nearest RGB(0,0,0xAA); in the shipped palette that is indices 15 and 241).
    key='white' (mirrored "white-key" draw, EXE 1058:0cf3 -> 1048:00b7(255,255,255)):
        4bpp: {15}.  8bpp: logical indices whose colour is exactly (255,255,255)
        (0 and 77 in the shipped palette; 0xFEFEFE entries stay opaque).
    key=None: nothing is transparent (opaque blits, EXE 1058:0c78).
    """
    if key is None:
        return set()
    if key not in ('default', 'white'):
        raise PMapError(f'unknown key {key!r}')
    if depth == 4:
        return {KEY_4BPP} if key == 'default' else {15}
    if palette is None:
        raise PMapError('8bpp transparency needs the RRGB palette')
    rgb = KEY_RGB_8BPP if key == 'default' else (0xFF, 0xFF, 0xFF)
    return {i for i, c in enumerate(palette) if tuple(c) == rgb}


def to_rgba(pm, palette=None, key='default'):
    """Return (width, height, bytes) of straight RGBA8888 pixels (alpha 0 for the
    key colour(s), 255 otherwise).  Pure Python, no Pillow needed."""
    if palette is None:
        if pm.depth != 4:
            raise PMapError('8bpp image needs the RRGB palette')
        palette = VGA16_PALETTE
    keys = transparent_set(pm.depth, palette, key)
    lut = [bytes(palette[i]) + (b'\x00' if i in keys else b'\xff') if i < len(palette) else b'\0\0\0\xff'
           for i in range(256)]
    return pm.width, pm.height, b''.join(lut[p] for p in pm.pixels)


def to_image(pm, palette=None, transparent=True, key='default'):
    """Return a Pillow image ('P' mode with a palette; key colour(s) transparent)."""
    from PIL import Image
    if palette is None:
        if pm.depth != 4:
            raise PMapError('8bpp image needs the RRGB palette')
        palette = VGA16_PALETTE
    img = Image.frombytes('P', (pm.width, pm.height), pm.pixels)
    flat = []
    for c in list(palette)[:256]:
        flat.extend(c)
    flat += [0] * (768 - len(flat))
    img.putpalette(flat)
    if transparent:
        keys = transparent_set(pm.depth, palette, key)
        if len(keys) == 1:
            img.info['transparency'] = next(iter(keys))
        elif keys:
            img.info['transparency'] = bytes(0 if i in keys else 255 for i in range(256))
    return img


# ---------------------------------------------------------------- resource access

def decode_file(path, depth=None):
    """Decode a PMAP from a raw resource file (e.g. extracted/res/TMT256.DLL/32513/1000)."""
    with open(path, 'rb') as f:
        data = f.read()
    rid = os.path.basename(path)
    return decode(data, depth, int(rid) if rid.isdigit() else None)


def iter_pmaps(source, bits):
    """Yield (id, PMap) for every PMAP of TMT16.DLL (bits=16) or TMT256.DLL (bits=256)."""
    dll = dll_name(bits)
    for rid in source.ids(dll):
        yield rid, decode(source.read(dll, rid), 4 if bits == 16 else 8, rid)


def _which_list(which):
    return {'16': [16], '256': [256], 'both': [16, 256]}[str(which)]


def dll_name(bits):
    return 'TMT16.DLL' if bits == 16 else 'TMT256.DLL'


class Source:
    """Reads PMAP / RRGB bodies either from extracted\\res\\<DLL>\\<type>\\<id> files or
    straight from the NE DLLs (using the engine's tools/ne_resources.py)."""

    def __init__(self, res_dir=None, dll_dir=None):
        self.res_dir = res_dir
        self.dll_dir = dll_dir
        self._ne = {}

    def _ne_table(self, dll):
        if dll not in self._ne:
            sys.path.insert(0, os.path.join(REPO, 'tools'))
            import mischief_engine
            mischief_engine.add_tools_path()
            import ne_resources  # the engine's tools/ne_resources.py
            d = open(os.path.join(self.dll_dir, dll), 'rb').read()
            tab = {}
            for t, n, off, ln, _fl in ne_resources.resources(d):
                tab[(t, n)] = d[off:off + ln]
            self._ne[dll] = tab
        return self._ne[dll]

    def ids(self, dll, rtype=PMAP_TYPE):
        if self.dll_dir:
            return sorted(int(n) for (t, n) in self._ne_table(dll) if t == rtype and n.isdigit())
        d = os.path.join(self.res_dir, dll, rtype)
        return sorted(int(f) for f in os.listdir(d) if f.isdigit())

    def read(self, dll, rid, rtype=PMAP_TYPE):
        if self.dll_dir:
            return self._ne_table(dll)[(rtype, str(rid))]
        with open(os.path.join(self.res_dir, dll, rtype, str(rid)), 'rb') as f:
            return f.read()

    def palette(self):
        return load_rrgb(self.read('TMT.DLL', RRGB_ID, RRGB_TYPE))


def make_source(args):
    if getattr(args, 'dll_dir', None):
        return Source(dll_dir=args.dll_dir)
    return Source(res_dir=args.res_dir or DEFAULT_RES)


# ---------------------------------------------------------------- contact sheets

def contact_sheets(items, out_dir, prefix, cell=160, cols=8, rows=6, bg=(48, 48, 56)):
    """items: list of (rid, PIL image).  Writes prefix_NN.png sheets; returns paths."""
    from PIL import Image, ImageDraw
    paths = []
    label_h = 14
    per = cols * rows
    checker = Image.new('RGB', (cell, cell), bg)
    dr = ImageDraw.Draw(checker)
    for yy in range(0, cell, 8):
        for xx in range(0, cell, 8):
            if (xx // 8 + yy // 8) % 2:
                dr.rectangle([xx, yy, xx + 7, yy + 7], fill=(64, 64, 74))
    for s in range(0, len(items), per):
        chunk = items[s:s + per]
        sheet = Image.new('RGB', (cols * cell, rows * (cell + label_h)), (20, 20, 24))
        d = ImageDraw.Draw(sheet)
        for k, (rid, img) in enumerate(chunk):
            cx = (k % cols) * cell
            cy = (k // cols) * (cell + label_h)
            sheet.paste(checker, (cx, cy))
            im = img.convert('RGBA')
            scale = min((cell - 4) / im.width, (cell - 4) / im.height, 2.0)
            if scale != 1.0:
                im = im.resize((max(1, int(im.width * scale)), max(1, int(im.height * scale))),
                               Image.NEAREST)
            sheet.paste(im, (cx + (cell - im.width) // 2, cy + (cell - im.height) // 2), im)
            d.text((cx + 3, cy + cell + 1), f'{rid} {img.width}x{img.height}', fill=(230, 230, 230))
        p = os.path.join(out_dir, f'{prefix}_{s // per:02d}_{chunk[0][0]}-{chunk[-1][0]}.png')
        sheet.save(p)
        paths.append(p)
    return paths


# ---------------------------------------------------------------- CLI

def cmd_export(args):
    src = make_source(args)
    out_root = args.out or DEFAULT_PREVIEWS
    pal = src.palette()
    failures = []
    for bits in _which_list(args.which):
        dll = dll_name(bits)
        out = os.path.join(out_root, f'pmap{bits}')
        os.makedirs(out, exist_ok=True)
        items = []
        stats = {}
        for rid in src.ids(dll):
            try:
                pm = decode(src.read(dll, rid), depth=4 if bits == 16 else 8, rid=rid)
                img = to_image(pm, None if bits == 16 else pal, transparent=not args.opaque)
                img.save(os.path.join(out, f'{rid}.png'))
                items.append((rid, img))
                stats[pm.encoding] = stats.get(pm.encoding, 0) + 1
            except Exception as e:      # report and continue
                failures.append((dll, rid, repr(e)))
        print(f'{dll}: {len(items)} images exported to {out}  {stats}')
        if not args.no_sheets:
            sd = os.path.join(out, 'sheets')
            os.makedirs(sd, exist_ok=True)
            paths = contact_sheets(items, sd, f'sheet{bits}')
            print(f'  {len(paths)} contact sheets in {sd}')
    for f in failures:
        print('FAILED', *f)
    print(f'{len(failures)} failures')
    return 1 if failures else 0


def cmd_info(args):
    src = make_source(args)
    for bits in _which_list(args.which):
        dll = dll_name(bits)
        data = src.read(dll, args.id)
        pm = decode(data, depth=4 if bits == 16 else 8, rid=args.id)
        hist = {}
        for b in pm.pixels:
            hist[b] = hist.get(b, 0) + 1
        top = sorted(hist.items(), key=lambda kv: -kv[1])[:8]
        print(dll, pm, 'top indices', top)


def cmd_verify(args):
    src = make_source(args)
    bad = 0
    n = {16: 0, 256: 0}
    rund_ok = 0
    for bits in (16, 256):
        dll = dll_name(bits)
        for rid in src.ids(dll):
            data = src.read(dll, rid)
            try:
                pm = decode(data, depth=4 if bits == 16 else 8, rid=rid)
            except Exception as e:
                print('decode failed', dll, rid, e)
                bad += 1
                continue
            n[bits] += 1
            if len(data) - pm.consumed >= 512:
                print('suspicious tail', dll, rid, len(data), pm.consumed)
            if pm.encoding == 'rund':
                enc = rund_compress(pm.pixels, pm.width, pm.height)
                if enc == data[:pm.consumed]:
                    rund_ok += 1
                else:
                    print('re-encode differs', dll, rid, len(enc), pm.consumed)
    print(f'decoded {n[16]} 16-colour + {n[256]} 256-colour; {rund_ok} RUND streams re-encode '
          f'byte-identically; {bad} failures')
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('export', 'info', 'verify'):
        p = sub.add_parser(name)
        p.add_argument('--res-dir', help='extracted resource root (default extracted/res)')
        p.add_argument('--dll-dir', help='read the NE DLLs directly from this directory instead')
        if name != 'verify':
            p.add_argument('--which', default='both', choices=['16', '256', 'both'])
        if name == 'export':
            p.add_argument('--out', help='output root (default extracted/previews)')
            p.add_argument('--no-sheets', action='store_true')
            p.add_argument('--opaque', action='store_true', help='do not make key colour transparent')
        if name == 'info':
            p.add_argument('id', type=int)
    args = ap.parse_args(argv)
    return {'export': cmd_export, 'info': cmd_info, 'verify': cmd_verify}[args.cmd](args) or 0


if __name__ == '__main__':
    sys.exit(main())
