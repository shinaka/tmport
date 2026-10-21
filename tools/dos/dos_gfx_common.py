"""Shared helpers for the Treasure Mountain DOS-edition (1990, SST.EXE) graphics decoders.

Pixel model (see re/dos/docs/fmt_graphics.md):
  * Every picture in the DOS edition is stored as packed 4 bpp "logical colour" nibbles,
    two pixels per byte, high nibble = left pixel. The game composes a 320x200 back buffer
    in the same format (160 bytes per row) and converts it to the adapter at present time.
  * A logical colour (0..15) becomes a displayed colour through a per-adapter table that
    lives in SST.EXE's data segment (EGA attribute palette, Tandy palette registers, or the
    MCGA DAC values). Those tables are read from the user's SST.EXE at run time; no game
    data is embedded in this module.

Library use::

    from dos_gfx_common import load_exe, game_palette, nibbles_to_image
    exe = load_exe(r'C:\\Git\\TM\\TM-DOS\\SST.EXE')      # EXEPACK is undone on the fly
    pal = game_palette(exe, 'ega')                      # 16 (r,g,b) tuples
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.normpath(os.path.join(HERE, '..'))
REPO = os.path.normpath(os.path.join(TOOLS, '..'))
DEFAULT_DOS_DIR = os.path.join(REPO, 'TM-DOS')
DEFAULT_GFX_OUT = os.path.join(REPO, 'extracted', 'previews', 'dos', 'gfx')

# --- Hardware facts (not game data) -------------------------------------------------
# The 16 colours of an RGBI (CGA-compatible) display, which is what EGA 200-line modes,
# VGA in mode 0Dh/0Eh (default DAC), and the Tandy 1000 320x200x16 mode show. Index =
# I<<3 | R<<2 | G<<1 | B; colour 6 is shown as brown by the monitor / default DAC.
RGBI_COLOURS = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA), (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF),
]

# --- Addresses inside SST.EXE (Ghidra numbering: load image starts at segment 1000) ----
# DGROUP (DS) is segment 4d94 -> image offset 0x3d940. It is located at run time from the
# Turbo C copyright string that the C0 startup places at DS:0004.
TURBOC_SIG = b'Turbo-C - Copyright (c) 1988 Borland Intl.'
DS_PAL_MCGA = 0x0144      # 16 x (r,g,b) 6-bit DAC values applied last at start-up
                          # (1460:0003 -> 1437:0001 copies them to DS:38EA, 1000:1144 loads the DAC)
DS_PAL_MCGA_MODESET = 0x3C1A  # 16 x (r,g,b) loaded first by the mode-13h set-up 1000:07bf
DS_PAL_CGA = 0x0174       # 16 bytes of 2x2 dither patterns for CGA mode 4 (1437:0027 -> 1000:1087)
DS_PAL_EGA = 0x3C4A       # 16 attribute regs + overscan, RGBI (INT 10h AX=1002h at 1000:0852)
DS_PAL_TANDY = 0x3D1B     # 16 Tandy palette registers, IRGB   (OUT 3DAh/3DEh loop at 1000:0985)
DS_LAYOUT = 0x0CAA        # 16x16 word grid of PAN block ids (initial contents; see fmt doc)

PALETTE_KINDS = ('ega', 'tandy', 'mcga', 'mcga_modeset', 'identity')


class DosExe:
    """The unpacked load image of SST.EXE plus the offset of its data segment."""

    def __init__(self, image: bytes):
        self.image = image
        p = image.find(TURBOC_SIG)
        if p < 4:
            raise ValueError('Turbo C signature not found; is this SST.EXE?')
        self.ds = p - 4                       # image offset of DS:0000

    def ds_bytes(self, off: int, n: int) -> bytes:
        return self.image[self.ds + off:self.ds + off + n]

    def cs_bytes(self, seg_off: int, n: int) -> bytes:
        """Bytes at Ghidra address 1000:seg_off (the first code segment)."""
        return self.image[seg_off:seg_off + n]


def _unexepack():
    sys.path.insert(0, TOOLS)
    try:
        import mischief_engine  # tools/mischief_engine.py
    finally:
        sys.path.pop(0)
    sys.path.insert(0, mischief_engine.tools_dir())
    try:
        import unexepack  # the engine's tools/unexepack.py
    finally:
        sys.path.pop(0)
    return unexepack


def load_exe(path: str) -> DosExe:
    """Load SST.EXE (EXEPACK-packed original) or an already unpacked copy."""
    data = open(path, 'rb').read()
    if data[:2] != b'MZ':
        raise ValueError(f'{path}: not an MZ executable')
    if _is_exepacked(data):
        data = _unexepack().unpack(data)[0]
    hdr_paras = struct.unpack_from('<H', data, 8)[0]
    exe = DosExe(data[hdr_paras * 16:])
    # sanity check: the EGA palette table must be 16 six-bit values plus overscan
    if any(v > 0x3F for v in exe.ds_bytes(DS_PAL_EGA, 17)):
        raise ValueError(f'{path}: data segment tables not where expected (other version?)')
    return exe


def _is_exepacked(data: bytes) -> bool:
    """EXEPACK puts its 'RB' signature at offset 16 of the block at the entry CS."""
    hdr_paras = struct.unpack_from('<H', data, 8)[0]
    cs = struct.unpack_from('<H', data, 0x16)[0]
    p = hdr_paras * 16 + cs * 16 + 16
    return data[p:p + 2] == b'RB'


def find_exe(explicit, near_file):
    """Return a DosExe from --exe, or SST.EXE next to the input file, or None."""
    cands = [explicit] if explicit else []
    if near_file:
        cands.append(os.path.join(os.path.dirname(os.path.abspath(near_file)), 'SST.EXE'))
    cands.append(os.path.join(DEFAULT_DOS_DIR, 'SST.EXE'))
    for c in cands:
        if c and os.path.isfile(c):
            return load_exe(c)
    return None


def game_palette(exe, kind: str = 'ega'):
    """Return 16 (r,g,b) tuples: the displayed colour of each logical colour."""
    if kind == 'identity' or exe is None:
        return list(RGBI_COLOURS)
    if kind == 'ega':
        regs = exe.ds_bytes(DS_PAL_EGA, 16)
        # 200-line EGA modes drive an RGBI monitor: bit0 B, bit1 G, bit2 R, bit4 = I.
        return [RGBI_COLOURS[(v & 7) | ((v >> 1) & 8)] for v in regs]
    if kind == 'tandy':
        regs = exe.ds_bytes(DS_PAL_TANDY, 16)
        return [RGBI_COLOURS[v & 15] for v in regs]
    if kind in ('mcga', 'mcga_modeset'):
        raw = exe.ds_bytes(DS_PAL_MCGA if kind == 'mcga' else DS_PAL_MCGA_MODESET, 48)
        return [tuple(((raw[i * 3 + k] & 63) * 255 + 31) // 63 for k in range(3)) for i in range(16)]
    raise ValueError(f'unknown palette kind {kind!r}')


def palette_report(exe):
    """Text table of the logical colour -> adapter value mappings (for docs/CLI)."""
    lines = ['logical  EGA-reg  Tandy-reg  RGBI  MCGA-DAC(DS:0144)  MCGA-DAC(DS:3C1A)  CGA-dither']
    ega = exe.ds_bytes(DS_PAL_EGA, 17)
    tan = exe.ds_bytes(DS_PAL_TANDY, 16)
    mcga = exe.ds_bytes(DS_PAL_MCGA, 48)
    mcga0 = exe.ds_bytes(DS_PAL_MCGA_MODESET, 48)
    cga = exe.ds_bytes(DS_PAL_CGA, 16)
    for i in range(16):
        rgbi = (ega[i] & 7) | ((ega[i] >> 1) & 8)
        lines.append('  %2d      %02x       %02x       %2d     %02x %02x %02x           %02x %02x %02x           %02x' % (
            i, ega[i], tan[i], rgbi, mcga[i * 3], mcga[i * 3 + 1], mcga[i * 3 + 2],
            mcga0[i * 3], mcga0[i * 3 + 1], mcga0[i * 3 + 2], cga[i]))
    lines.append('EGA overscan register: %02x' % ega[16])
    return '\n'.join(lines)


# --- Pixel helpers ------------------------------------------------------------------

def unpack_nibbles(row: bytes):
    """Packed 4bpp bytes -> list of logical colours, high nibble first."""
    out = []
    for b in row:
        out.append(b >> 4)
        out.append(b & 15)
    return out


def indexed_image(width, height, pixels, palette, transparent=None):
    """pixels: flat list/bytes of logical colours. Returns a PIL image ('P' or 'RGBA')."""
    from PIL import Image
    im = Image.new('P', (width, height))
    flat = []
    for c in palette:
        flat.extend(c)
    flat.extend([0] * (768 - len(flat)))
    im.putpalette(flat)
    im.putdata(bytes(pixels))
    if transparent is not None:
        im.info['transparency'] = transparent
        im = im.convert('RGBA')
    return im


def scale(im, factor):
    from PIL import Image
    if factor == 1:
        return im
    return im.resize((im.width * factor, im.height * factor), Image.NEAREST)


def _on_checker(im):
    from PIL import Image, ImageDraw
    if im.mode != 'RGBA':
        return im.convert('RGB')
    chk = Image.new('RGB', im.size, (96, 96, 112))
    cd = ImageDraw.Draw(chk)
    for yy in range(0, im.height, 4):
        for xx in range((yy // 4 % 2) * 4, im.width, 8):
            cd.rectangle([xx, yy, xx + 3, yy + 3], fill=(128, 128, 144))
    chk.paste(im, (0, 0), im)
    return chk


def contact_sheet(items, out_path, cols=None, bg=(40, 40, 52), label=True, max_width=1600,
                  factor=1):
    """items: list of (label, PIL image). Writes a labelled sheet.

    With `cols` the cells form a uniform grid; otherwise images are shelf-packed left to
    right (row height = tallest image in the row), which suits frames of mixed sizes.
    """
    from PIL import Image, ImageDraw
    if not items:
        return
    items = [(n, scale(im, factor)) for n, im in items]
    lab = 12 if label else 0
    pad = 4
    if cols:
        cw = max(im.width for _, im in items) + pad
        ch = max(im.height for _, im in items) + lab + pad
        rows = (len(items) + cols - 1) // cols
        places = [((k % cols) * cw + 2, (k // cols) * ch + 2) for k in range(len(items))]
        W, H = cols * cw, rows * ch
    else:
        places, x, y, rowh, W = [], 2, 2, 0, 0
        for name, im in items:
            cw = max(im.width, 6 * len(str(name))) + pad
            if x + cw > max_width and x > 2:
                x, y, rowh = 2, y + rowh, 0
            places.append((x, y))
            x += cw
            W = max(W, x)
            rowh = max(rowh, im.height + lab + pad)
        H = y + rowh
    sheet = Image.new('RGB', (W + 2, H + 2), bg)
    draw = ImageDraw.Draw(sheet)
    for (name, im), (x, y) in zip(items, places):
        sheet.paste(_on_checker(im), (x, y))
        if label:
            draw.text((x, y + im.height + 1), str(name), fill=(230, 230, 230))
    sheet.save(out_path)


def add_common_args(ap):
    ap.add_argument('--exe', help='path to SST.EXE (packed or unpacked); default: next to input')
    ap.add_argument('--palette', choices=PALETTE_KINDS, default='ega',
                    help='adapter palette to show logical colours with (default ega)')


if __name__ == '__main__':
    # python -P dos_gfx_common.py [SST.EXE]  -> print the palette tables
    _exe = find_exe(sys.argv[1] if len(sys.argv) > 1 else None, None)
    if _exe is None:
        sys.exit('SST.EXE not found')
    print(palette_report(_exe))
