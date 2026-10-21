"""Crop the 512x384 game area out of a 640x480 capture of the original game (tmctl.py shot,
or a frame decoded by avi_frames.py; both are the engine's tools).

Where the game area sits (measured on captures of TMTWINCD.EXE under Windows 3.1, 640x480,
small fonts; see re/docs/REFERENCE_CAPTURES.md):

  * Window: maximised, caption rows 0..19 (row 0 border, rows 1..18 caption, row 19 line).
    Client area starts at screen (1, 20).
  * The game computes its viewport origin once (InitInstance 1018:0115) as
    x = ((640 - (2*1 + 512)) / 2) & ~3 = 60, y = (480 - (2*1 + 20 + 18 + 384)) / 2 = 28
    in client coordinates (SM_CXBORDER 1, SM_CYBORDER 1, SM_CYCAPTION 20, SM_CYMENU 18).
  * Without the game menu bar (title, Club Sign-In, celebration): client top = 20, so the game
    area is at screen (61, 48)..(572, 431).
  * With the menu bar (File / Options / Help, rows 20..37 plus a 1-pixel line at row 38, present
    while a climb is played): client top = 39, game area at screen (61, 67)..(572, 450).

The menu bar is detected from the capture itself (row 25 white at the right edge) unless
--menu yes/no is given.

Usage: python -P crop_game_area.py IN.png [IN2.png ...] [--out DIR | -o OUT.png] [--menu auto|yes|no]
       python -P crop_game_area.py --info IN.png     (print the detected origin only)
Library: from crop_game_area import game_origin, crop_game_area
"""
import os
import sys

from PIL import Image

GAME_W, GAME_H = 512, 384
ORIGIN_NO_MENU = (61, 48)
ORIGIN_MENU = (61, 67)


def has_menu_bar(img):
    im = img.convert('RGB')
    if im.size != (640, 480):
        raise ValueError(f'expected a 640x480 capture, got {im.size}')
    px = im.load()
    white = (255, 255, 255)
    return all(px[x, 25] == white for x in (600, 620, 635)) and px[620, 38] != white


def game_origin(img, menu='auto'):
    if menu == 'yes' or (menu == 'auto' and has_menu_bar(img)):
        return ORIGIN_MENU
    return ORIGIN_NO_MENU


def crop_game_area(img, menu='auto'):
    x, y = game_origin(img, menu)
    return img.crop((x, y, x + GAME_W, y + GAME_H))


def main(argv):
    if not argv:
        print(__doc__)
        return
    menu, out, outdir, info, files = 'auto', None, None, False, []
    it = iter(argv)
    for a in it:
        if a == '--menu':
            menu = next(it)
        elif a == '-o':
            out = next(it)
        elif a == '--out':
            outdir = next(it)
        elif a == '--info':
            info = True
        else:
            files.append(a)
    for f in files:
        img = Image.open(f)
        if info:
            print(f, 'menu bar' if has_menu_bar(img) else 'no menu bar', 'origin', game_origin(img, menu))
            continue
        res = crop_game_area(img, menu)
        if out and len(files) == 1:
            dst = out
        else:
            base = os.path.splitext(os.path.basename(f))[0] + '_game.png'
            dst = os.path.join(outdir or os.path.dirname(os.path.abspath(f)), base)
        os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
        res.save(dst)
        print(dst)


if __name__ == '__main__':
    main(sys.argv[1:])
