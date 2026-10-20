"""Gizmos & Gadgets (DOS v1.0): the final movie (volume 3, resources 0x3000..0x30c9) to PNG frames.

Spec: re/gg/docs/fmt_graphics.md §7. Code (Ghidra SSSS:OOOO):

    2fcb:0640  state 15 (the end of the game): loads sound bank 0x30ca, then
               Movie_Play(0x3000, 0x30ca, 18, cues DS:236c, ...) = 1473:07d9
    1473:07d9  for id in [first, end): Timer_Set(0, 18 PIT ticks); decode frame into A000:0000;
               read the next frame; run the cue of this frame number, if any; Timer_Wait(0)
    1399:0a85  frame decoder ("RunSkipDump", the Deluxe Paint Animation delta coding),
               writing straight into the 320x200 screen:

        c = byte
        c in 01..7F : copy c literal bytes
        c == 00     : n = byte, v = byte: n bytes of v; v == 1 skips n bytes instead
        c == 80     : w = u16: w == 0 ends the frame; w < 0x8000 skips w bytes;
                      0x8000 | n (bit 14 clear): copy n literal bytes;
                      0xC000 | n: v = byte, n bytes of v (v == 1 skips)
        c in 81..FF : skip c & 0x7F bytes

    The movie loads no palette: it shows whatever the shadow palette holds when state 15
    starts, right after the results screen (state 12, 2fcb:0000 loads picture 0x1076 with
    palette 0x1077 = colours 64..255). Frame 0 rewrites rows 0..164 and equals picture 0x1076
    in 47 860 of those 52 800 bytes, and the frames use only colours 0..31 and 64..255, so
    the default palette here is ssg.exe's DS:024e (colours 0..63) with 0x1077 on top
    [inferred, not checked against a capture]; --pal replaces 0x1077.

CLI (python -P tools/gg/movie.py ...)::

    movie.py [--out DIR] [--frames LIST] [--pal ID ...] [--no-base] [--sheet] [--cues]
        --frames 0,1,60 or 0-201 (default: all 202), --sheet writes a contact sheet,
        --cues prints the cue table DS:236c read from ssg.exe
    movie.py --check        decode all frames and report (no output)

The frame number k is resource 0x3000 + k. Frames are applied in order to one 320x200
buffer that starts with the results picture 0x1076 in rows 0..164 (--blank: colour 0);
rows 165..199 (the HUD on the real screen) start at colour 0 until frames 1..9 draw them.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ggarc  # noqa: E402
import lpx  # noqa: E402

FIRST, END = ggarc.MOVIE_FIRST, ggarc.MOVIE_END
W, H = 320, 200
FRAME_TICKS = 18                     # PIT ticks per frame (2fcb:069d pushes 0x12)
PIT_HZ = 1193181.6667 / 0x2000       # 145.652 Hz (PIT divisor 0x2000, 1454:0046)
EXE_DS_CUES = 0x236C                 # 18 cues of 4 words: type, frame, a, b
N_CUES = 18
DEFAULT_PALETTES = (0x1077,)         # stated default (see the docstring)
RESULTS_PICTURE = 0x1076             # drawn by 2fcb:0000 before the movie


class MovieError(ValueError):
    pass


def apply_delta(buf, d):
    """Apply one frame to buf (bytearray of 64000). Returns (bytes used, last offset,
    literal bytes, run bytes, skipped bytes)."""
    i = o = 0
    lit = run = skip = 0
    n = len(d)
    size = len(buf)
    while True:
        if i >= n:
            raise MovieError('frame data ends without the 80 00 00 terminator')
        c = d[i]
        i += 1
        if 0 < c < 0x80:
            if o + c > size or i + c > n:
                raise MovieError('literal run past the screen or the data')
            buf[o:o + c] = d[i:i + c]
            i += c
            o += c
            lit += c
        elif c == 0:
            k, v = d[i], d[i + 1]
            i += 2
            if v != 1:
                if o + k > size:
                    raise MovieError('run past the screen')
                buf[o:o + k] = bytes((v,)) * k
                run += k
            else:
                skip += k
            o += k
        elif c == 0x80:
            w = struct.unpack_from('<H', d, i)[0]
            i += 2
            if w == 0:
                return i, o, lit, run, skip
            if w < 0x8000:
                o += w
                skip += w
            elif w & 0x4000 == 0:
                k = w & 0x3FFF
                if o + k > size or i + k > n:
                    raise MovieError('long literal past the screen or the data')
                buf[o:o + k] = d[i:i + k]
                i += k
                o += k
                lit += k
            else:
                k = w & 0x3FFF
                v = d[i]
                i += 1
                if v != 1:
                    if o + k > size:
                        raise MovieError('long run past the screen')
                    buf[o:o + k] = bytes((v,)) * k
                    run += k
                else:
                    skip += k
                o += k
        else:
            k = c & 0x7F
            o += k
            skip += k


def read_cues(data):
    path = os.path.join(data, 'ssg.exe')
    with open(path, 'rb') as f:
        d = f.read()
    o = ggarc.EXE_DS_FILE_OFFSET + EXE_DS_CUES
    return [struct.unpack_from('<4h', d, o + 8 * k) for k in range(N_CUES)]


def parse_frames(s, n):
    out = []
    for part in s.split(','):
        if '-' in part:
            a, b = part.split('-')
            out.extend(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    return sorted(set(k for k in out if 0 <= k < n))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ggarc.add_data_arg(ap)
    ap.add_argument('--out', default=os.path.join(ggarc.DEFAULT_PREVIEWS, 'movie'))
    ap.add_argument('--frames', default=None)
    ap.add_argument('--pal', action='append', default=None,
                    help='palette id(s) on top of the base (default: 1077)')
    ap.add_argument('--blank', action='store_true', help='start from colour 0, not picture 1076')
    ap.add_argument('--no-base', action='store_true', help='grey ramp instead of DS:024e')
    ap.add_argument('--sheet', action='store_true', help='also write a contact sheet')
    ap.add_argument('--cues', action='store_true', help='print the cue table')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args(argv)
    try:
        arc = ggarc.Archive(a.data)
        n = END - FIRST
        if a.cues:
            print('cue\ttype\tframe\ta\tb\t(DS:236c, ssg.exe; frame period %d PIT ticks = %.3f s)'
                  % (FRAME_TICKS, FRAME_TICKS / PIT_HZ))
            for k, c in enumerate(read_cues(arc.data)):
                print('%d\t%d\t%d\t%d\t%d' % ((k,) + c))
            if not (a.check or a.frames or a.sheet):
                return 0
        buf = bytearray(W * H)
        if not a.blank and arc.exists(RESULTS_PICTURE):
            bg = lpx.decode_lpx(arc.payload(RESULTS_PICTURE))
            if bg.w == W and bg.h <= H:
                buf[:W * bg.h] = bg.pixels
        errors = 0
        frames = parse_frames(a.frames, n) if a.frames else list(range(n))
        want = set(frames)
        if not a.check:
            out = ggarc.safe_out_dir(a.out, arc.data)
            pal = lpx.Palette() if a.no_base else lpx.base_palette(arc)[0]
            ids = [ggarc.parse_rid(q) for q in a.pal] if a.pal else list(DEFAULT_PALETTES)
            for q in ids:
                pal.apply(arc.payload(q))
            rgb = pal.rgb()
        thumbs = []
        totals = [0, 0, 0]
        for k in range(n):
            e = arc.entry(FIRST + k)
            try:
                used, last, lit, run, skip = apply_delta(buf, e.payload)
            except (MovieError, IndexError, struct.error) as ex:
                errors += 1
                print('ERROR frame %d (%04x): %s' % (k, FIRST + k, ex))
                continue
            totals[0] += lit
            totals[1] += run
            totals[2] += skip
            if used != len(e.payload):
                errors += 1
                print('ERROR frame %d: used %d of %d bytes' % (k, used, len(e.payload)))
            if a.check or k not in want:
                continue
            from PIL import Image
            im = Image.frombytes('P', (W, H), bytes(buf))
            im.putpalette(rgb)
            im = im.convert('RGB')
            im.save(os.path.join(out, 'movie_%03d.png' % k))
            if a.sheet:
                thumbs.append(im.resize((W // 2, H // 2), Image.BILINEAR))
        print('movie: %d frames (%04x..%04x), %d literal, %d run, %d skipped bytes in all; %d error%s'
              % (n, FIRST, END - 1, totals[0], totals[1], totals[2], errors, '' if errors == 1 else 's'))
        if not a.check:
            print('%d frames -> %s (palette: %s + %s)' % (len(want), out, 'grey' if a.no_base
                                                         else 'DS:024e', '+'.join('%04x' % q for q in ids)))
            if a.sheet and thumbs:
                from PIL import Image
                cols = 8
                rows = (len(thumbs) + cols - 1) // cols
                s = Image.new('RGB', (cols * (W // 2 + 2), rows * (H // 2 + 2)), (40, 40, 40))
                for j, t in enumerate(thumbs):
                    s.paste(t, ((j % cols) * (W // 2 + 2), (j // cols) * (H // 2 + 2)))
                s.save(os.path.join(out, 'movie_sheet.png'))
        return 1 if errors else 0
    except ggarc.ArchiveError as e:
        print('movie: %s' % e, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
