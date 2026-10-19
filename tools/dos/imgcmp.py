"""Pixel comparison of engine frames with the DOSBox-X reference captures, and contact sheets.

Engine frames (--frame-dump, --dos-dump-state <tick>:<file> -> <file>.png) are 320x200 RGB(A).
Captures are 320x200 (curated EGA frames, Tandy, MCGA, CGA) or 640x400 (DOSBox-X's EGA originals
in ega/raw/: every pixel a 2x2 block); 640x400 pictures are reduced to 320x200.

  python -P imgcmp.py ENGINE.png REF.png [--diff OUT.png] [--mask x0,y0,x1,y1 ...]
  python -P imgcmp.py --pairs FILE          lines "engine ref [mask...]"
  python -P imgcmp.py --montage OUT.png COLS file...

Library: load(path), compare(a, b, masks) -> (count, bbox), diff_image(a, b, masks),
montage(out, cols, files, labels).
"""
import os
import sys

from PIL import Image, ImageChops, ImageDraw


def load(path):
    """A picture as 320x200 RGB (640x400 2x-doubled captures reduced)."""
    im = Image.open(path).convert('RGB')
    if im.size == (640, 400):
        im = im.resize((320, 200), Image.NEAREST)
    if im.size != (320, 200):
        raise ValueError(f'{path}: {im.size[0]}x{im.size[1]} is not 320x200 or 640x400')
    return im


def mask_image(masks):
    """'L' image: 255 where pixels count, 0 inside the mask rectangles (inclusive coordinates)."""
    m = Image.new('L', (320, 200), 255)
    if masks:
        d = ImageDraw.Draw(m)
        for x0, y0, x1, y1 in masks:
            d.rectangle([x0, y0, x1, y1], fill=0)
    return m


def difference_mask(a, b, masks=()):
    """'L' image: 255 where a and b differ outside the masks."""
    d = ImageChops.difference(a, b).convert('L').point(lambda v: 255 if v else 0)
    if masks:
        d = ImageChops.multiply(d, mask_image(masks))
    return d


def compare(a, b, masks=()):
    """(differing pixels, bbox or None) of two 320x200 RGB images (or paths)."""
    if isinstance(a, str):
        a = load(a)
    if isinstance(b, str):
        b = load(b)
    d = difference_mask(a, b, masks)
    return d.histogram()[255], d.getbbox()


def diff_image(a, b, masks=()):
    """Side by side: engine | capture | engine with the differing pixels in magenta (masks dimmed)."""
    d = difference_mask(a, b, masks)
    marked = a.copy()
    marked.paste((255, 0, 255), None, d)
    if masks:
        dim = Image.new('RGB', (320, 200), (40, 40, 40))
        marked = Image.composite(marked, Image.blend(marked, dim, 0.6), mask_image(masks))
    out = Image.new('RGB', (960, 200))
    out.paste(a, (0, 0))
    out.paste(b, (320, 0))
    out.paste(marked, (640, 0))
    return out


def montage(out, cols, files, labels=None, cell=(320, 200), max_width=1920):
    """A grid of pictures with a label line above each."""
    files = list(files)
    rows = (len(files) + cols - 1) // cols
    w, h = cell[0], cell[1] + 12
    m = Image.new('RGB', (cols * w, max(1, rows) * h), (40, 40, 40))
    d = ImageDraw.Draw(m)
    for i, f in enumerate(files):
        im = f if isinstance(f, Image.Image) else Image.open(f).convert('RGB')
        if im.size != cell:
            im = im.resize(cell, Image.NEAREST)
        x, y = (i % cols) * w, (i // cols) * h
        m.paste(im, (x, y + 12))
        text = labels[i] if labels else os.path.basename(str(f))
        d.text((x + 2, y), text[:100], fill=(255, 255, 0))
    if m.size[0] > max_width:
        m = m.resize((max_width, int(m.size[1] * max_width / m.size[0])), Image.LANCZOS)
    m.save(out)


def _masks(args):
    return [tuple(int(v) for v in s.split(',')) for s in args]


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    if args[0] == '--pairs':
        for line in open(args[1]):
            line = line.split('#')[0].strip()
            if not line:
                continue
            parts = line.split()
            n, bbox = compare(parts[0], parts[1], _masks(parts[2:]))
            print('%6d  %-22s %s  vs  %s' % (n, bbox, os.path.basename(parts[0]), os.path.basename(parts[1])))
        return
    if args[0] == '--montage':
        montage(args[1], int(args[2]), args[3:])
        return
    diff_out, masks, i = None, [], 2
    while i < len(args):
        if args[i] == '--diff':
            diff_out = args[i + 1]
            i += 2
        elif args[i] == '--mask':
            i += 1
            while i < len(args) and not args[i].startswith('--'):
                masks.append(tuple(int(v) for v in args[i].split(',')))
                i += 1
        else:
            i += 1
    a, b = load(args[0]), load(args[1])
    n, bbox = compare(a, b, masks)
    if diff_out:
        diff_image(a, b, masks).save(diff_out)
    print(n, 'pixels differ', 'bbox', bbox)


if __name__ == '__main__':
    main()
