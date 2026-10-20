"""Replay the wave-0 G&G reference tour in DOSBox-X through tools/tmctl.py (focus-safe, parked).

The tour proves the capture path end to end: title (with an AVI and a raw OPL capture of the title
music), attract, sign-in by typing, the Technology Center hub, the Aircraft building (blueprint,
part information, workshop), a warehouse room, a puzzle door, the puzzle solved with the mouse
(MOUSEKEY TSR, re/gg/docs/REFERENCE_CAPTURES.md section 2), the room behind the door and back to
the hub through the File menu. It needs a gg-profile DOSBox-X: `tmctl.py start --profile gg --park
--fresh` for segment 'door', or a running instance for segment 'puzzle' (it loads a state).

Segments (each reproduces its part of extracted/captures/gg/w0/):
  door     fresh boot -> title, story, sign-in "Ada" (mouse: Start Game, With Chimps), hub, Aircraft
           building, blueprint and a part's page, workshop, warehouse, walk to the first X door, ↑.
           Up to the warehouse the run is deterministic (fixed clock, rand() not yet called). In the
           warehouse the Cyber Chimps call rand() every frame, so where they stand and WHICH
           puzzle the door holds depend on the real-time pace of the run.
  puzzle   from the state w0/17_puzzle (the machine-pieces puzzle, 3 pieces): take, place,
           assemble -> solved, through the door, File > Return to Technology Center > Yes.
Usage (python -P capture_tour.py ...):
  run SEGMENT OUTDIR [--states PREFIX] [--no-av]
      shots OUTDIR/NN_name.png; named states PREFIX/NN_name (default: no states). Segment 'door'
      also records OUTDIR/title_attract.avi and .dro unless --no-av.
  list SEGMENT       print the steps
"""
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import tmctl  # noqa: E402

# A step: (kind, args...). Kinds: key TOKENS | mouse TOKENS | sleep S | shot NAME | state NAME |
# rec start/stop | opl start/stop | load NAME.  Coordinates are game coordinates (320x200).
DOOR = [
    ('boot', 'fresh'), ('rec', 'start'), ('opl', 'start'),
    ('sleep', 2.0), ('shot', '01_title'), ('state', '01_title'),
    ('sleep', 9.0), ('shot', '02_story1'),
    ('sleep', 32.0), ('rec', 'stop'), ('opl', 'stop'),
    ('key', 'space'), ('sleep', 2.0), ('shot', '03_signin'), ('state', '03_signin'),
    ('key', 'text:ada'), ('sleep', 0.5), ('shot', '04_signin_name'),
    ('mouse', 'click 164 180'), ('sleep', 1.5), ('shot', '05_chimps_dialog'),          # Start Game
    ('mouse', 'click 93 137'), ('sleep', 3.0), ('shot', '06_hub'), ('state', '06_hub'),  # With Chimps
    ('mouse', 'move 100 50'), ('sleep', 0.5), ('shot', '07_hub_hover_aircraft'),
    ('mouse', 'click 62 85'), ('sleep', 4.0), ('shot', '08_building_hint'),            # Aircraft door
    ('key', 'enter'), ('sleep', 1.5), ('shot', '09_blueprint'), ('state', '09_blueprint'),
    ('mouse', 'click 53 51'), ('sleep', 1.5), ('shot', '10_blueprint_part_info'),       # Decal
    ('mouse', 'click 269 150'), ('sleep', 1.5),                                         # Done
    ('mouse', 'click 269 150'), ('sleep', 2.0), ('shot', '11_workshop_hint'),          # Done
    ('key', 'enter'), ('sleep', 1.5), ('shot', '12_workshop'), ('state', '12_workshop'),
    ('key', 'hold:up:300'), ('sleep', 4.0), ('shot', '13_warehouse_hint'),             # tube
    ('key', 'enter'), ('sleep', 1.5), ('shot', '14_warehouse'), ('state', '14_warehouse'),
    ('key', 'hold:right:2000'), ('sleep', 0.5), ('shot', '15_warehouse_at_door'), ('state', '15_at_door'),
    ('key', 'hold:up:300'), ('sleep', 4.0), ('shot', '16_puzzle_hint'),
    ('key', 'enter'), ('sleep', 1.5), ('shot', '17_puzzle'), ('state', '17_puzzle'),
]
PUZZLE = [
    ('load', 'w0/17_puzzle'), ('sleep', 1.0),
    ('mouse', 'drag 47 122 200 80 600'), ('sleep', 1.0),        # piece 1 (shaft) out of the box
    ('mouse', 'move 200 30'), ('sleep', 0.5), ('mouse', 'click 200 30'), ('sleep', 1.0),
    ('shot', '18_puzzle_piece1_placed'), ('state', '18_puzzle_piece1'),
    ('mouse', 'click 47 122'), ('sleep', 0.8), ('mouse', 'move 150 60'), ('sleep', 0.8),   # wedge
    ('mouse', 'click 120 40'), ('sleep', 0.8),
    ('mouse', 'click 47 122'), ('sleep', 0.8), ('mouse', 'move 260 40'), ('sleep', 0.8),   # bar
    ('mouse', 'click 190 24'), ('sleep', 1.0), ('shot', '19_puzzle_t_handle'),             # T
    ('mouse', 'click 125 48'), ('sleep', 0.6), ('mouse', 'move 150 90'), ('sleep', 0.6),
    ('mouse', 'click 200 126'), ('sleep', 3.5), ('shot', '20_puzzle_solved'), ('state', '20_puzzle_solved'),
    ('key', 'enter'), ('sleep', 4.0), ('shot', '21_room_behind_door'), ('state', '21_room_behind_door'),
    ('mouse', 'move 290 128'), ('sleep', 0.6), ('shot', '22_room_hover_part'),
    ('mouse', 'click 14 4'), ('sleep', 1.0), ('shot', '23_file_menu'),                   # File
    ('mouse', 'click 107 27'), ('sleep', 1.5), ('shot', '24_return_dialog'),             # Return to TC
    ('mouse', 'click 221 133'), ('sleep', 5.0), ('shot', '25_hub_again'), ('state', '25_hub_again'),
]
SEGMENTS = {'door': DOOR, 'puzzle': PUZZLE}


def run(steps, outdir, prefix=None, av=True):
    os.makedirs(outdir, exist_ok=True)
    t0 = time.time()
    for step in steps:
        kind, arg = step[0], step[1]
        print(f'{time.time() - t0:7.1f}s {kind} {arg}', flush=True)
        tmctl.ensure_parked()
        if kind == 'boot':        # fresh start, parked; the 45 s re-park loop runs alongside the steps
            if tmctl._windows():
                raise SystemExit('DOSBox-X is already running (segment door starts it: tmctl.py stop first)')
            threading.Thread(target=tmctl.start, args=(True, 'gg'), kwargs={'fresh': True},
                             daemon=True).start()
            tmctl.find(timeout=30)
            t1 = time.time()            # the window exists before DOSBox-X has built its menu
            while time.time() - t1 < 30:
                try:
                    if tmctl.running_profile() == 'gg' and any('Record video' in p for _, p in tmctl.menu_items()):
                        break
                except SystemExit:
                    pass
                time.sleep(0.2)
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
                tmctl.state(['save', f'{prefix}/{arg}'], 'gg')
        elif kind == 'load':
            tmctl.state(['load', arg], 'gg')
        elif kind in ('rec', 'opl') and av:
            if kind == 'rec':
                tmctl.record(arg, os.path.join(outdir, 'title_attract.avi') if arg == 'stop' else None)
            else:
                tmctl.audio('opl', arg, os.path.join(outdir, 'title_attract.dro') if arg == 'stop' else None)


def main(argv):
    if len(argv) < 2 or argv[0] not in ('run', 'list') or argv[1] not in SEGMENTS:
        print(__doc__)
        return
    steps = SEGMENTS[argv[1]]
    if argv[0] == 'list':
        for s in steps:
            print(*s)
        return
    if argv[1] != 'door' and tmctl.running_profile() != 'gg':
        raise SystemExit('start DOSBox-X with: python -P tools/tmctl.py start --profile gg --park')
    prefix = argv[argv.index('--states') + 1] if '--states' in argv else None
    run(steps, argv[2], prefix, '--no-av' not in argv)


if __name__ == '__main__':
    main(sys.argv[1:])
