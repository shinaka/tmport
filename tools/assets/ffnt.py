#!/usr/bin/env python3
"""FFNT bitmap-font decoder and text-layout reference for Super Solvers Treasure Mountain!
(Windows CD).  The game's only in-game font is TMT.DLL type 32514 (Mac type 'FFNT') id 999.

Format (little-endian where it matters; all tables are byte arrays), see
re/docs/fmt_bndl_ffnt.md for the full spec and EXE citations:

    +0           u8   unused by the EXE (0x00; probably Mac 'first char')
    +1           u8   last   -> N = last + 1 entries per table   (1068:0099)
    +2           u8   height -> glyph rows / line height          (1068:00a8)
    +3           u8   width[N]        advance == bitmap width     (1068:00b4)
    +3+N         u8   off_lo[N]                                     (1068:00bf)
    +3+2N        u8   off_hi[N]                                     (1068:00c7)
    +3+3N        bitmap data; glyph c starts at off_lo[c] | off_hi[c] << 8,
                 `height` rows of ceil(width[c] / 8) bytes, MSB = leftmost pixel,
                 1 = paint in the current colour, 0 = transparent    (1060:01ef)

The text layout engine (draw_text / measure_text) reproduces EXE 10b0:00b5 / 10b0:053c:
codes 0x09 TAB, 0x0A/0x0D newline, 0x20 word-wrap point, 0xFE carriage return,
0xFF <sub> ... 0xFF escapes (1 = substitute string, 2 = underline toggle,
3 <n> = colour n-1, 4 = tab stops, 5 = centring toggle, 6 = ignored payload).

CLI:
    python -P ffnt.py info  FONT
    python -P ffnt.py sheet FONT OUT.png [--scale 3]
    python -P ffnt.py text  FONT OUT.png --text "Hello\\xff\\x03\\x05\\xffred" [--width 512] [--scale 2]
    python -P ffnt.py file  FONT OUT.png TEXTRES [--width 400] [--scale 2]   (render a raw OTHR text)
"""
from __future__ import annotations

import argparse
import codecs
import os
import struct
import sys

# The 16 logical text colours (index = n-1 of an FF 03 n FF code).  Standard EGA/VGA colours;
# the EXE keeps the same RGB triples as 3 x u16 at DGROUP 0x07d8 (used by 1048:02fd).
EGA_RGB = [
    (0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
    (170, 0, 0), (170, 0, 170), (170, 85, 0), (192, 192, 192),
    (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
    (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255),
]

SCREEN_W, SCREEN_H = 512, 384     # off-screen play buffer (1058:05bc clips to 0x200 x 0x180)


class FontError(ValueError):
    pass


class Font:
    def __init__(self, data: bytes):
        if len(data) < 3:
            raise FontError("too short")
        self.raw = data
        self.byte0 = data[0]
        self.last = data[1]
        self.n = self.last + 1
        self.height = data[2]
        n = self.n
        need = 3 + 3 * n
        if len(data) < need:
            raise FontError("truncated tables")
        self.widths = list(data[3:3 + n])
        lo = data[3 + n:3 + 2 * n]
        hi = data[3 + 2 * n:3 + 3 * n]
        self.offsets = [lo[i] | (hi[i] << 8) for i in range(n)]
        self.base = 3 + 3 * n
        self.bitmap = data[self.base:]
        # sanity: every glyph must fit
        for c in range(n):
            end = self.offsets[c] + self.glyph_bytes(c)
            if end > len(self.bitmap):
                raise FontError(f"glyph {c:#x} overruns bitmap")

    @classmethod
    def load(cls, path: str) -> "Font":
        with open(path, "rb") as f:
            return cls(f.read())

    def width(self, c: int) -> int:
        """Advance width; the EXE indexes tables directly by the byte value (1060:02e4)."""
        return self.widths[c] if c < self.n else 0

    def row_bytes(self, c: int) -> int:
        return (self.width(c) + 7) // 8

    def glyph_bytes(self, c: int) -> int:
        return self.row_bytes(c) * self.height

    def glyph(self, c: int) -> list[list[int]]:
        """Rows of 0/1 pixels, `height` x `width(c)`."""
        w, rb = self.width(c), self.row_bytes(c)
        o = self.offsets[c]
        rows = []
        for y in range(self.height):
            row = []
            for x in range(w):
                byte = self.bitmap[o + y * rb + x // 8]
                row.append((byte >> (7 - (x & 7))) & 1)
            rows.append(row)
        return rows

    def ink_box(self, c: int):
        """(x0, y0, x1, y1) inclusive bounding box of set pixels, or None."""
        g = self.glyph(c)
        pts = [(x, y) for y, r in enumerate(g) for x, v in enumerate(r) if v]
        if not pts:
            return None
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        return min(xs), min(ys), max(xs), max(ys)

    def bitmap_end(self) -> int:
        return max(self.offsets[c] + self.glyph_bytes(c) for c in range(self.n))


# --------------------------------------------------------------------------- layout engine

class Canvas:
    """Minimal 8-bit indexed canvas (colour index per pixel, None = untouched)."""

    def __init__(self, w=SCREEN_W, h=SCREEN_H, bg=None):
        self.w, self.h = w, h
        self.px = [[bg] * w for _ in range(h)]

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:   # the EXE does NOT clip glyphs
            self.px[y][x] = c

    def fill(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)


class TextState:
    def __init__(self, colour=0):
        self.colour = colour          # logical colour 0..15 (DGROUP 0x4293)


def _skip_to_ff(text: bytes, i: int) -> int:
    """Consume bytes up to and including the next 0xFF (renderer loops at 10b0:0277 etc.).
    The EXE has no bound check; we stop at end of buffer."""
    while i < len(text):
        c = text[i]
        i += 1
        if c == 0xFF:
            break
    return i


def _cstr(buf: bytes, i: int) -> int:
    return buf[i] if i < len(buf) else 0


def measure_word(font: Font, delim: int, text: bytes, i: int, subst=None,
                 in_sub: bool = False, sub: bytes = b"", si: int = 0) -> int:
    """EXE 10b0:053c: width of glyphs from text[i] up to delim / NUL / LF / CR.

    Follows FF 01 n FF substitutions into subst[n-1]; other escapes contribute 0.
    If in_sub, scanning starts inside `sub` at index si (then continues in `text` at i).
    """
    total = 0
    done = False
    first_sub = in_sub
    idx = 0
    while not done:
        while not done and not in_sub:
            c = _cstr(text, i)
            i += 1
            if c == delim or c in (0, 10, 13):
                done = True
            elif c == 0xFF:
                sc = _cstr(text, i)
                i += 1
                if sc == 1:
                    in_sub = True
                    idx = _cstr(text, i) - 1
                    i += 1
                i = _skip_to_ff(text, i)
            else:
                total += font.width(c)
        if in_sub:
            if first_sub:
                s, p = sub, si
                first_sub = False
            else:
                s, p = (subst[idx] if subst and 0 <= idx < len(subst) else b""), 0
            while not done and in_sub:
                c = _cstr(s, p)
                p += 1
                if c == delim or c in (10, 13):
                    done = True
                elif c == 0:
                    in_sub = False
                else:
                    total += font.width(c)
    return total


def draw_text(font: Font, text: bytes, x0: int = 0, y0: int = 0, w: int = 512,
              subst=None, canvas: Canvas | None = None, state: TextState | None = None,
              exe_quirks: bool = True):
    """EXE 10b0:00b5.  If canvas is None only measures.  Returns (width, height, cursor).

    width  = max_x - min_x over drawn glyphs; height = last line y - y0 + line height
    (exactly the values the EXE writes through its out-pointers).
    exe_quirks=True reproduces the substitution-measure bug (see doc); False fixes it.
    """
    state = state or TextState()
    lh = font.height
    sp = font.width(0x20)
    limit = x0 + w - 1
    min_x = x0 + w - 1
    max_x = x0
    cx, cy = x0, y0
    underline = False
    centre = False
    tabs = [40 * (k + 1) for k in range(8)]
    i = 0

    def put(ch, x, y):
        if canvas is None:
            return font.width(ch)
        if underline and ch == 0x5F:          # '_' is not drawn under underline (10b0:066a)
            adv = font.width(ch)
        else:
            adv = font.width(ch)
            g = font.glyph(ch)
            for yy, row in enumerate(g):
                for xx, v in enumerate(row):
                    if v:
                        canvas.set(x + xx, y + yy, state.colour)
        if underline:                          # 1-px line on the cell's last row (10b0:0698)
            canvas.fill(x, y + lh - 1, adv, 1, state.colour)
        return adv

    def centre_x(pos):
        lw = measure_word(font, 10, text, pos, subst)
        return x0 + int((w - lw) / 2)          # C '/' truncates toward zero (CWD;SUB;SAR)

    while True:
        ch = _cstr(text, i)
        i += 1
        if ch == 0:
            break
        if ch == 0x09:
            for t in tabs:
                if t + x0 > cx:
                    cx = t + x0
                    break
        elif ch in (0x0A, 0x0D):
            cx = centre_x(i) if centre else x0
            cy += lh
        elif ch == 0xFE:
            cx = x0
        elif ch == 0x20:
            ww = measure_word(font, 0x20, text, i, subst) + sp
            if cx + ww > limit:
                while _cstr(text, i) in (0x20, 0x09):
                    i += 1
                cx = x0
                cy += lh
            else:
                min_x = min(min_x, cx)
                cx += put(0x20, cx, cy)
                max_x = max(max_x, cx)
        elif ch == 0xFF:
            sc = _cstr(text, i)
            i += 1
            if sc == 1:
                n = _cstr(text, i)
                i += 1
                s = subst[n - 1] if subst and 0 < n <= len(subst) else b""
                p = 0
                # main-text position handed to the measure: EXE passes the byte after n,
                # i.e. the closing 0xFF (quirk); fixed mode passes the byte after it.
                main_pos = i if exe_quirks else _skip_to_ff(text, i)
                while True:
                    c = _cstr(s, p)
                    p += 1
                    if c == 0:
                        break
                    if c == 0x20:
                        ww = measure_word(font, 0x20, text, main_pos, subst, True, s, p) + sp
                        if cx + ww > limit:
                            while _cstr(s, p) in (0x20, 0x09):
                                p += 1
                            cx = x0
                            cy += lh
                        else:
                            min_x = min(min_x, cx)
                            cx += put(0x20, cx, cy)
                            max_x = max(max_x, cx)
                    else:
                        min_x = min(min_x, cx)
                        cx += put(c, cx, cy)
                        max_x = max(max_x, cx)
                i = _skip_to_ff(text, i)
            elif sc == 2:
                underline = not underline
                i = _skip_to_ff(text, i)
            elif sc == 3:
                n = _cstr(text, i)
                i += 1
                state.colour = (n - 1) & 0xFFFF
                i = _skip_to_ff(text, i)
            elif sc == 4:
                # EXE reads a u16 at each *byte* position, advancing 1 byte (10b0:02de quirk)
                k = 0
                while _cstr(text, i) != 0xFF and k < 8:
                    v = _cstr(text, i) | (_cstr(text, i + 1) << 8)
                    tabs[k] = v - 0x10000 if v & 0x8000 else v
                    i += 1
                    k += 1
                i = _skip_to_ff(text, i)
            elif sc == 5:
                i = _skip_to_ff(text, i)
                centre = not centre
                if centre:
                    cx = centre_x(i)
            elif sc == 6:
                i = _skip_to_ff(text, i)
            # sc == 0 or > 6: nothing consumed beyond the sub-code (10b0:0162 JA)
        else:
            min_x = min(min_x, cx)
            cx += put(ch, cx, cy)
            max_x = max(max_x, cx)
    return max_x - min_x, cy - y0 + lh, (cx, cy)


def measure_text(font: Font, text: bytes, w: int = 512, subst=None):
    """EXE 10b0:0050 (measure-only wrapper) -> (width, height)."""
    tw, th, _ = draw_text(font, text, 0, 0, w, subst, None)
    return tw, th


def text_width(font: Font, text: bytes) -> int:
    """EXE 10b0:0080: width of a string laid out in a 512-px box (0 for empty)."""
    if not text or text[0] == 0:
        return 0
    return measure_text(font, text, 0x200)[0]


# --------------------------------------------------------------------------- rendering

def _need_pil():
    try:
        from PIL import Image, ImageDraw  # noqa: F401
    except ImportError:
        sys.exit("Pillow is required for PNG output")


def canvas_to_image(canvas: Canvas, bg=(255, 255, 255), scale=1, crop=None):
    from PIL import Image
    x0, y0, x1, y1 = crop or (0, 0, canvas.w, canvas.h)
    img = Image.new("RGB", (x1 - x0, y1 - y0), bg)
    pix = img.load()
    for y in range(y0, y1):
        row = canvas.px[y]
        for x in range(x0, x1):
            c = row[x]
            if c is not None:
                pix[x - x0, y - y0] = EGA_RGB[c & 15]
    if scale != 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    return img


def render_sheet(font: Font, out: str, scale: int = 3, cols: int = 16):
    """Glyph sheet: 16 x 16 grid, each cell shows the glyph box (advance x height) on a
    light background with its hex code."""
    _need_pil()
    from PIL import Image, ImageDraw
    maxw = max(font.widths)
    cell_w = max(maxw, 24) + 6
    label_h = 10
    cell_h = font.height + label_h + 6
    rows = (font.n + cols - 1) // cols
    W, H = cols * cell_w * scale, rows * cell_h * scale
    img = Image.new("RGB", (W, H), (255, 255, 255))
    d = ImageDraw.Draw(img)
    for c in range(font.n):
        cx, cy = (c % cols) * cell_w * scale, (c // cols) * cell_h * scale
        d.text((cx + 2, cy + 1), f"{c:02X}", fill=(120, 120, 120))
        gx, gy = cx + 3 * scale, cy + label_h * scale
        w = font.width(c)
        d.rectangle([gx, gy, gx + w * scale - 1, gy + font.height * scale - 1],
                    fill=(225, 235, 250))
        for y, row in enumerate(font.glyph(c)):
            for x, v in enumerate(row):
                if v:
                    d.rectangle([gx + x * scale, gy + y * scale,
                                 gx + (x + 1) * scale - 1, gy + (y + 1) * scale - 1],
                                fill=(0, 0, 0))
    img.save(out)
    return img


def render_text(font: Font, text: bytes, out: str, w: int = 512, scale: int = 2,
                colour: int = 0, subst=None, margin: int = 4, exe_quirks=True):
    _need_pil()
    tw, th = measure_text(font, text, w, subst)
    cw = w + 2 * margin
    ch = th + 2 * margin
    cv = Canvas(cw, max(ch, font.height + 2 * margin))
    draw_text(font, text, margin, margin, w, subst, cv, TextState(colour), exe_quirks)
    img = canvas_to_image(cv, scale=scale)
    img.save(out)
    return img, (tw, th)


def _parse_text_arg(s: str) -> bytes:
    return codecs.decode(s, "unicode_escape").encode("latin-1")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Treasure Mountain FFNT font tool")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info"); p.add_argument("font")
    p = sub.add_parser("sheet"); p.add_argument("font"); p.add_argument("out")
    p.add_argument("--scale", type=int, default=3)
    p = sub.add_parser("text"); p.add_argument("font"); p.add_argument("out")
    p.add_argument("--text", required=True, help="Python-escaped text, e.g. 'A\\xff\\x05\\xffB'")
    p.add_argument("--width", type=int, default=512); p.add_argument("--scale", type=int, default=2)
    p.add_argument("--colour", type=int, default=0)
    p = sub.add_parser("file"); p.add_argument("font"); p.add_argument("out"); p.add_argument("textres")
    p.add_argument("--width", type=int, default=400); p.add_argument("--scale", type=int, default=2)
    p.add_argument("--colour", type=int, default=0)
    a = ap.parse_args(argv)
    font = Font.load(a.font)
    if a.cmd == "info":
        print(f"byte0={font.byte0} last={font.last:#x} entries={font.n} height={font.height}")
        print(f"bitmap base={font.base:#x} bitmap used={font.bitmap_end()} bytes "
              f"(file {len(font.raw)} bytes)")
        printable = [c for c in range(font.n) if font.ink_box(c)]
        print(f"glyphs with ink: {len(printable)}; width range "
              f"{min(font.widths)}..{max(font.widths)}; space width {font.width(32)}")
        for c in range(font.n):
            box = font.ink_box(c)
            ch = chr(c) if 32 < c < 127 else "."
            print(f"  {c:3d} {c:02X} {ch} w={font.width(c):2d} off={font.offsets[c]:5d} ink={box}")
    elif a.cmd == "sheet":
        render_sheet(font, a.out, a.scale)
        print("wrote", a.out)
    elif a.cmd == "text":
        _, (tw, th) = render_text(font, _parse_text_arg(a.text), a.out, a.width, a.scale, a.colour)
        print(f"wrote {a.out}  (layout {tw}x{th})")
    elif a.cmd == "file":
        with open(a.textres, "rb") as f:
            raw = f.read()
        raw = raw.split(b"\0", 1)[0]
        _, (tw, th) = render_text(font, raw, a.out, a.width, a.scale, a.colour)
        print(f"wrote {a.out}  (layout {tw}x{th})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
