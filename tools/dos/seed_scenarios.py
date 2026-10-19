"""Seeds the fidelity scenarios (tools/dos/scenarios/<group>/*.json) from the reference material.

  python -P seed_scenarios.py match [--out pairs.json]
        every DOSBox-X save state's screen (its video memory) against every capture of its machine:
        the best capture per state with the pixel count (0 = the state was saved at that very frame)
  python -P seed_scenarios.py states PAIRS.json [--max-px N]
        a scenario per state with a matching capture (load the state, compare the loaded moment and the
        first engine frame with the capture), filed under the group of its place in the game
  python -P seed_scenarios.py boot TRACE.csv FRAMES_DIR
        checkpoints of the boot playthrough (pt_full_script.txt, seed 0x1EE2): for each Round-1 EGA
        capture, the frame of the playthrough's frame dump that matches it best
  python -P seed_scenarios.py positions PAIRS.json TRACE.csv [--max-px N]
        the ticks of a boot playthrough's trace at the places of the states paired with a capture

Only numbers, file names and key scripts go into the scenario files.
"""
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import imgcmp  # noqa: E402
import savmem  # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
CAPTURES = os.environ.get('TM_DOS_CAPTURES') or os.path.join(REPO, 'extracted', 'captures', 'dos')
SCENARIOS = os.path.join(HERE, 'scenarios')


def state_files():
    out = []
    for sub in ('ega', 'r2', 'tandy'):
        out += sorted(glob.glob(os.path.join(CAPTURES, 'savestates', sub, '*.sav')))
    return out


def captures_for(machine):
    sub = 'tandy' if machine == 'MCH_TANDY' else 'ega'
    return sorted(glob.glob(os.path.join(CAPTURES, sub, '*.png')))


def cmd_match(out):
    pairs = []
    cache = {}
    for path in state_files():
        st = savmem.State(path)
        try:
            im = savmem.screen_image(st)
        except SystemExit:
            continue
        caps = captures_for(st.machine)
        best = []
        for c in caps:
            if c not in cache:
                cache[c] = imgcmp.load(c)
            n, _ = imgcmp.compare(im, cache[c])
            best.append((n, c))
        best.sort()
        rel = os.path.relpath(path, CAPTURES).replace('\\', '/')
        top = [(n, os.path.relpath(c, CAPTURES).replace('\\', '/')) for n, c in best[:3]]
        pairs.append(dict(state=rel, machine=st.machine, best=top, level=st.w(0x928f), scene=st.w(0x8fbe),
                          uimode=st.b(0x9119), area=st.w(0x910b)))
        print('%-40s %6d %-48s (next %d %s)' % (rel, top[0][0], top[0][1], top[1][0], top[1][1]), flush=True)
    with open(out, 'w') as f:
        json.dump(pairs, f, indent=1)
    print('wrote', out)


def group_of(p):
    """The scenario group of a state: by its place in the game."""
    name = os.path.basename(p['state'])
    if p['machine'] == 'MCH_TANDY':
        return 'looks'
    if p['uimode'] & 0x88 or p['uimode'] in (0x02, 0x20, 0x40) or 'signin' in name or 'box' in name:
        return 'ui'
    if (p['scene'] in (3, 4) or p['area'] == 0x0D or 'castle' in name or 'end_' in name or 'crown' in name or
            'prize' in name or 'troom' in name):
        return 'castle_end'
    if p['level'] == 0 and p['scene'] == 0:
        return 'level1'
    return 'levels23'


def cmd_states(pairs_file, max_px):
    pairs = json.load(open(pairs_file))
    plain = {os.path.basename(p['state']) for p in pairs}
    written = 0
    for p in pairs:
        n, cap = p['best'][0]
        if n > max_px:
            continue
        base = os.path.basename(p['state'])
        if base.startswith('fl_') and base[3:] in plain:
            continue  # the frame-logger copy of a plain state: the plain one has the scenario
        g = group_of(p)
        stem = 'state_' + os.path.splitext(os.path.basename(p['state']))[0]
        if p['state'].startswith('savestates/r2/'):
            stem = 'state_r2_' + os.path.splitext(os.path.basename(p['state']))[0]
        elif p['state'].startswith('savestates/tandy/'):
            stem = 'state_tandy_' + os.path.splitext(os.path.basename(p['state']))[0]
        sc = dict(description='load the state, compare the loaded moment (its own screen) and the first engine frame '
                              'with the capture taken there (%d px between the state and the capture)' % n,
                  start=dict(state=p['state']), ticks=1,
                  checkpoints=[dict(id='loaded', tick=0, capture=cap),
                               dict(id='frame1', tick=1, capture=cap)])
        d = os.path.join(SCENARIOS, g)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, stem + '.json'), 'w', encoding='utf-8') as f:
            json.dump(sc, f, indent=1)
            f.write('\n')
        written += 1
    print('wrote', written, 'state scenarios')


def cmd_boot(trace_csv, frames_dir):
    """Prints, for each Round-1 EGA capture, the best frame of a playthrough's frame dump."""
    frames = sorted(glob.glob(os.path.join(frames_dir, '*.png')))
    caps = sorted(glob.glob(os.path.join(CAPTURES, 'ega', '[0-9]*.png')))
    loaded = {f: imgcmp.load(f) for f in frames}
    for c in caps:
        im = imgcmp.load(c)
        best = sorted((imgcmp.compare(im, a)[0], f) for f, a in loaded.items())[:2]
        print('%-36s %6d %s' % (os.path.basename(c), best[0][0], os.path.basename(best[0][1])))


def cmd_positions(pairs_file, trace_csv, max_px):
    """For each state paired with a Round-1 capture: the first tick of a boot playthrough's trace with
    the state's level, scene, player x/y, camera and UI mode (the same place on the same layout)."""
    import csv
    pairs = json.load(open(pairs_file))
    with open(trace_csv, newline='') as f:
        rows = [r for r in csv.DictReader(line for line in f if not line.startswith('#'))]
    found = []
    for p in pairs:
        n, cap = p['best'][0]
        if n > max_px or not p['state'].startswith('savestates/ega/'):
            continue
        st = savmem.State(os.path.join(CAPTURES, p['state']))
        want = (st.w(0x928f), st.w(0x8fbe), st.w(0x37e2), st.w(0x37e4), st.w(0x37ea), st.w(0x37ec), st.b(0x9119))
        best = None
        for i, r in enumerate(rows[:-1]):
            nxt = rows[i + 1]  # the trace's x..camy are read at the start of a tick: row i+1 = after tick i
            got = (int(r['level']), int(r['scene']), int(nxt['x']), int(nxt['y']), int(nxt['camx']), int(nxt['camy']),
                   int(r['uimode'], 16))
            if got == want:
                best = (0, int(r['tick']))
                break
            # The same view (level, scene, camera, UI mode) with the player near the state's place.
            if (got[0], got[1], got[4], got[5], got[6]) == (want[0], want[1], want[4], want[5], want[6]):
                d = abs(got[2] - want[2]) + abs(got[3] - want[3])
                if d <= 64 and (best is None or d < best[0]):
                    best = (d, int(r['tick']))
        if best:
            found.append((best[1], cap, p['state']))
    for t, cap, state in sorted(found):
        print('%6d  %-40s %s' % (t, cap, state))
    return sorted(found)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    if a[0] == 'match':
        cmd_match(a[a.index('--out') + 1] if '--out' in a else 'pairs.json')
    elif a[0] == 'states':
        cmd_states(a[1], int(a[a.index('--max-px') + 1]) if '--max-px' in a else 6000)
    elif a[0] == 'boot':
        cmd_boot(a[1], a[2])
    elif a[0] == 'positions':
        cmd_positions(a[1], a[2], int(a[a.index('--max-px') + 1]) if '--max-px' in a else 6000)
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
