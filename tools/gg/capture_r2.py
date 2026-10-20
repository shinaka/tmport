"""Replay the round 2 G&G reference captures in DOSBox-X through tools/tmctl.py (focus-safe, parked).

Round 2 (re/gg/docs/REFERENCE_CAPTURES.md, "Round 2") captured all 8 puzzle kinds, a race in each
building, the workshop, the final movie, sound runs and a second player. Every segment here starts
from a named state in extracted/captures/gg/savestates/r2/ (made in that session), so it replays
without walking the warehouse again. The states themselves were reached as the doc describes; the
door states were made with `capture_mem.py door` (puzzle kind chosen by the rand state) and two
buildings' boxes were moved next to the tube with `capture_mem.py boxset` (state edits, documented).

Usage (python -P capture_r2.py ...):
  list                     the segments and their steps
  run SEGMENT OUTDIR [--av] [--states PREFIX]
                           replay a segment into OUTDIR/NN_name.png (and named states PREFIX/NN_name
                           when --states is given). --av also records OUTDIR/SEGMENT.avi and .dro.
                           Needs a running gg-profile DOSBox-X (tmctl.py start --profile gg --park).
  compare DIR_A DIR_B      pixel differences between equally named PNGs of two runs
Segments: puzzle_k0 .. puzzle_k7 (solve variant 0 of each kind from r2/puzzle/kK_screen),
          race_aircraft, race_automotive, race_altenergy (race from the built vehicle to the
          post-race buttons), movie (race 15 from r2/movie/00_vehicle_ready_total14 to the end),
          workshop_build (aircraft: remove and refit the engine), resume (Save & Exit dialog).
"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import tmctl  # noqa: E402


def crank(cx, cy, r, turns=2.0, step_ms=60, ccw=True):
    """mouse moves along a circle (screen y down): counter-clockwise = the direction that pushes the
    racks of gears variant 0 to the right."""
    out = []
    n = int(24 * turns)
    for k in range(n + 1):
        a = math.pi / 2 + (-1 if ccw else 1) * k * 2 * math.pi / 24
        out.append(f'move {round(cx + r * math.cos(a))} {round(cy + r * math.sin(a))} wait {step_ms}')
    return ' '.join(out)


# A step: (kind, arg). Kinds: load STATE | key TOKENS | mouse TOKENS | sleep S | shot NAME | state NAME
PUZZLES = {
    'puzzle_k0': [   # electric circuits v0: string the wire from the battery's free end to the bulb
        ('load', 'r2/puzzle/k0_screen'), ('sleep', 1.0), ('shot', '01_screen'),
        ('mouse', 'click 32 145 wait 500 move 136 125 wait 400 click 136 125 wait 400 move 160 100 '
                  'wait 200 move 192 58 wait 500'), ('shot', '02_wire_strung'),
        ('mouse', 'click 192 58'), ('sleep', 3.0), ('shot', '03_solved'), ('state', 'solved')],
    'puzzle_k1': [   # simple machines v0: the wedge on the nail's tip, then the check button
        ('load', 'r2/puzzle/k1_screen'), ('sleep', 1.0), ('shot', '01_screen'),
        ('mouse', 'click 24 88 wait 600 move 150 110 wait 300 move 178 120 wait 500 click 178 120 '
                  'wait 700 move 120 60 wait 300'), ('shot', '02_placed'),
        ('mouse', 'click 28 156'), ('sleep', 3.0), ('shot', '03_solved'), ('state', 'solved')],
    'puzzle_k2': [   # machine pieces v0: handle, then the head over the handle's end
        ('load', 'r2/puzzle/k2_screen'), ('sleep', 1.0), ('shot', '01_screen'),
        ('mouse', 'click 47 122 wait 600 move 150 80 wait 400 click 150 80 wait 600'),
        ('shot', '02_handle'),
        ('mouse', 'click 47 122 wait 600 move 146 59 wait 400 click 146 59 wait 600 move 200 140'),
        ('sleep', 3.0), ('shot', '03_solved'), ('state', 'solved')],
    # gears v0: the gear on the peg, then turn the crank counter-clockwise. NOT reliably replayable:
    # while the crank is held the game moves the pointer itself (INT 33h fn 4, 3b2f:09a2), so the
    # outcome depends on how the posted moves fall between frames (solved in 2 of 4 tries; the
    # reference is the state r2/puzzle/k3_solved)
    'puzzle_k3': [
        ('load', 'r2/puzzle/k3_screen'), ('sleep', 1.0), ('shot', '01_screen'),
        ('mouse', 'click 56 104 wait 600 move 174 67 wait 400 click 174 67 wait 800 move 250 140'),
        ('sleep', 1.0), ('shot', '02_gear_placed'),
        ('mouse', 'move 196 128 wait 300 down 196 128 wait 200 ' + crank(196, 112, 16)),
        ('sleep', 1.0), ('shot', '03_cranked'), ('mouse', 'up'), ('sleep', 3.0), ('shot', '04_solved'),
        ('state', 'solved')],
    'puzzle_k4': [   # balance v0: the 3 weight on the left hook 3
        ('load', 'r2/puzzle/k4_screen'), ('sleep', 1.0), ('shot', '01_screen'),
        ('mouse', 'click 16 113 wait 600 move 117 50 wait 400'), ('shot', '02_carried'),
        ('mouse', 'click 117 50 wait 1500 move 60 150'), ('sleep', 3.0), ('shot', '03_solved'),
        ('state', 'solved')],
    # launch v0: ball on the launcher, plunger to energy 4, release. The plunger follows the pointer
    # with a lag, so it is pulled in 1-pixel steps 100 ms apart (a fast pull gave energy 7)
    'puzzle_k5': [
        ('load', 'r2/puzzle/k5_screen'), ('sleep', 1.0), ('shot', '01_screen'),
        ('mouse', 'drag 37 97 228 102 600'), ('sleep', 1.0),
        ('mouse', 'move 283 141 wait 300 down 283 141 wait 300 ' +
                  ' '.join(f'move {283 + i} {141 + round(i * 8 / 14)} wait 100' for i in range(1, 15)) + ' wait 500'),
        ('shot', '02_energy4'), ('mouse', 'up'), ('sleep', 5.0), ('shot', '03_solved'), ('state', 'solved')],
    'puzzle_k6': [   # energy forms v0: pencil lines mechanical-3, chemical-1, heat-2, then check
        ('load', 'r2/puzzle/k6_screen'), ('sleep', 1.0), ('shot', '01_screen'),
        ('mouse', 'click 24 72 wait 600 move 181 36 wait 300 click 181 36 wait 500 move 219 103 wait 400 '
                  'click 219 103 wait 600 move 181 70 wait 300 click 181 70 wait 500 move 219 36 wait 400 '
                  'click 219 36 wait 600 move 181 104 wait 300 click 181 104 wait 500 move 219 70 wait 400 '
                  'click 219 70 wait 600 move 120 140 wait 300'), ('shot', '02_matched'),
        ('mouse', 'click 26 137'), ('sleep', 10.0), ('shot', '03_solved'), ('state', 'solved')],
    'puzzle_k7': [   # magnets v0: three magnets N S in a row
        ('load', 'r2/puzzle/k7_screen'), ('sleep', 1.0), ('shot', '01_screen'),
        ('mouse', 'click 28 56 wait 600 move 160 83 wait 400 click 160 83 wait 700 click 28 56 wait 600 '
                  'move 184 83 wait 400 click 184 83 wait 700'), ('shot', '02_two'),
        ('mouse', 'click 28 56 wait 600 move 208 83 wait 400 click 208 83 wait 700 move 150 140'),
        ('sleep', 3.0), ('shot', '03_solved'), ('state', 'solved')],
}


def race(state, building):
    return [('load', state), ('sleep', 2.0), ('shot', '01_vehicle'), ('av', 'start'),
            ('mouse', 'click 305 135'), ('sleep', 6.0), ('shot', '02_race'), ('sleep', 30.0),
            ('shot', '03_result_box'), ('state', 'result_box'), ('key', 'enter'), ('sleep', 4.0),
            ('shot', '04_postrace'), ('state', 'postrace'), ('av', 'stop')]


SEGMENTS = dict(PUZZLES)
SEGMENTS.update({
    'race_aircraft': race('r2/aircraft/55_vehicle_narrow', 1),
    'race_automotive': race('r2/automotive/11_vehicle_built', 0),
    'race_altenergy': race('r2/altenergy/11_vehicle_built', 2),
    'movie': [('load', 'r2/movie/00_vehicle_ready_total14'), ('sleep', 2.0), ('av', 'start'),
              ('mouse', 'click 305 135'), ('sleep', 30.0), ('shot', '01_rankup'), ('key', 'enter'),
              ('sleep', 3.0), ('shot', '02_movie_start'), ('sleep', 20.0), ('shot', '03_movie_20s'),
              ('sleep', 30.0), ('shot', '04_movie_end'), ('av', 'stop')],
    'workshop_build': [('load', 'r2/aircraft/36_vehicle_built'), ('sleep', 2.0), ('shot', '01_built'),
                       ('mouse', 'click 195 155 wait 900 move 160 100 wait 500'), ('shot', '02_engine_carried'),
                       ('mouse', 'click 160 100 wait 1500'), ('shot', '03_engine_on_belt'),
                       ('mouse', 'click 190 42 wait 1500 click 160 66 wait 900 move 150 135 wait 500 '
                                 'click 150 135 wait 2500'), ('shot', '04_engine_refitted')],
    'resume': [('load', 'r2/aircraft/26_all_parts'), ('sleep', 2.0), ('mouse', 'click 14 4'), ('sleep', 1.0),
               ('shot', '01_file_menu'), ('mouse', 'click 100 38'), ('sleep', 2.0), ('shot', '02_save_exit')],
})


def run(name, outdir, prefix=None, av=False):
    os.makedirs(outdir, exist_ok=True)
    t0 = time.time()
    for kind, arg in SEGMENTS[name]:
        print(f'{time.time() - t0:7.1f}s {kind} {arg}', flush=True)
        tmctl.ensure_parked()
        if kind == 'load':
            tmctl.state(['load', arg], 'gg')
            time.sleep(1.5)            # the load is posted; let DOSBox-X finish it
        elif kind == 'key':
            for k in arg.split():
                tmctl.key_token(k)
        elif kind == 'mouse':
            tmctl.mouse(arg.split())
        elif kind == 'sleep':
            time.sleep(arg)
        elif kind == 'shot':
            tmctl.shot(os.path.join(outdir, arg + '.png'))
        elif kind == 'state':
            if prefix:
                tmctl.state(['save', f'{prefix}/{name}_{arg}'], 'gg')
        elif kind == 'av' and av:
            if arg == 'start':
                tmctl.record('start')
                tmctl.audio('opl', 'start')
            else:
                tmctl.record('stop', os.path.join(outdir, name + '.avi'))
                tmctl.audio('opl', 'stop', os.path.join(outdir, name + '.dro'))


def compare(a, b):
    from PIL import Image, ImageChops
    for f in sorted(os.listdir(a)):
        if not f.endswith('.png') or not os.path.exists(os.path.join(b, f)):
            continue
        ia = Image.open(os.path.join(a, f)).convert('RGB')
        ib = Image.open(os.path.join(b, f)).convert('RGB')
        if ia.size != ib.size:
            print(f'{f}: size {ia.size} vs {ib.size}')
            continue
        bbox = ImageChops.difference(ia, ib).getbbox()
        if not bbox:
            print(f'{f}: identical')
            continue
        diff = ImageChops.difference(ia, ib).convert('L').point(lambda v: 255 if v else 0)
        n = diff.histogram()[255]
        print(f'{f}: {n} differing pixels (640x400), box {bbox}')


def main(argv):
    if not argv or argv[0] not in ('list', 'run', 'compare'):
        print(__doc__)
        return
    if argv[0] == 'list':
        for name, steps in SEGMENTS.items():
            print(name)
            for s in steps:
                print('   ', *s)
        return
    if argv[0] == 'compare':
        compare(argv[1], argv[2])
        return
    name, outdir = argv[1], argv[2]
    if name not in SEGMENTS:
        raise SystemExit(f'unknown segment {name}; see list')
    if tmctl.running_profile() != 'gg':
        raise SystemExit('start DOSBox-X with: python -P tools/tmctl.py start --profile gg --park')
    prefix = argv[argv.index('--states') + 1] if '--states' in argv else None
    run(name, outdir, prefix, '--av' in argv)


if __name__ == '__main__':
    main(sys.argv[1:])
