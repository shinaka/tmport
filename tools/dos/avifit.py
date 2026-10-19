"""Timing check of engine runs against the DOSBox-X AVIs: every AVI frame of a range is looked up among
the engine's per-tick frames by exact picture, and the AVI's time base is fitted to the game ticks.

  python -P tools/dos/avifit.py SCENARIO.json [...] [--cli EXE] [--data DIR] [--captures DIR] [--out DIR]
                               [--verbose]
  python -P tools/dos/avifit.py --frames DIR --avi AVI --range A B [--k K] [--mask x0,y0,x1,y1 ...]

A scenario (tools/dos/scenarios/<group>/*.json, see fidelity.py) may hold
  "avi_fits": [{"id": "end_t2", "avi": "avi/ega_r2_end_t2_nokey.avi", "frames": [200, 1450],
                "ticks": [140, 1470], "masks": [{"rect": [x0, y0, x1, y1], "reason": "..."}],
                "max_off": 9, "max_dev": 0.5}]
The scenario's run (start, look, keys) is repeated with a picture dump at every tick of "ticks"; AVI
frame f shows the frame of game tick floor(a + f * k) for one offset a over the whole range (k =
47.339086 / 60.29 game ticks per frame: the EGA AVIs run at 60.29 frames per emulated second, see
their audio track, not at the 59.92 of their header; pass "k" for a Tandy AVI, 47.339086 / 59.9228).
An AVI frame matches when its picture (640x400 reduced to 320x200, masks blanked) equals an engine
frame; frames without an equal engine frame (torn frames, dissolve cells, the AVI's own key timing)
are not counted. Reported: the matched frames, the fitted a, the frames whose own ticks miss the
fitted tick and by how much. Exit code 1 when a fit has more such frames than "max_off" or misses by
more than "max_dev" ticks (the AVI's 60 Hz sampling makes up to about 0.5; default 0.6).

The frame and AVI decoders read the user's files only; nothing of the game is stored.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import avi_frames  # noqa: E402

EGA_K = 47.339086 / 60.29
TANDY_K = 47.339086 / 59.9228


def picture_key(im, masks):
    """md5 of a frame as 320x200 RGB with the masks blanked."""
    im = im.convert('RGB')
    if im.size == (640, 400):
        im = im.resize((320, 200), Image.NEAREST)
    if masks:
        d = ImageDraw.Draw(im)
        for m in masks:
            d.rectangle(list(m), fill=(255, 0, 255))
    return hashlib.md5(im.tobytes()).hexdigest()


def engine_frames(folder, masks):
    """{picture key: [ticks]} of the f_<tick>.bin.png dumps in folder."""
    out = {}
    for f in os.listdir(folder):
        m = re.match(r'f_(\d+)\.bin\.png$', f)
        if m:
            out.setdefault(picture_key(Image.open(os.path.join(folder, f)), masks), []).append(int(m.group(1)))
    return out


def runs(ticks):
    out = []
    for t in sorted(ticks):
        if out and t == out[-1][1] + 1:
            out[-1][1] = t
        else:
            out.append([t, t])
    return out


def fit(avi_path, frames_dir, lo, hi, k=EGA_K, masks=()):
    """Fits AVI frames lo..hi to the engine frames in frames_dir. Returns a dict (see the header)."""
    eng = engine_frames(frames_dir, masks)
    if not eng:
        raise SystemExit('no engine frames in %s' % frames_dir)
    tmin = min(t for v in eng.values() for t in v)
    tmax = max(t for v in eng.values() for t in v)
    matched, unmatched = [], 0
    for i, _, dec in avi_frames.iter_frames(avi_path):
        if i > hi:
            break
        if i < lo:
            continue
        ticks = eng.get(picture_key(dec.image(), masks))
        if ticks:
            matched.append((i, runs(ticks)))
        else:
            unmatched += 1
    # The offset covered by the most frames: each run of ticks [t0, t1] allows a in
    # [t0 - i k, t1 + 1 - i k) (runs touching the ends of the dumped range are open there).
    events = []
    for i, rs in matched:
        for t0, t1 in rs:
            if t0 == tmin or t1 == tmax:
                continue
            events.append((t0 - i * k, 1))
            events.append((t1 + 1 - i * k, -1))
    events.sort()
    best, count = None, 0
    for j, (x, d) in enumerate(events):
        count += d
        if d == 1 and (best is None or count > best[0]):
            best = (count, x, events[j + 1][0] if j + 1 < len(events) else x)
    if best is None:
        return dict(matched=len(matched), unmatched=unmatched, a=None, off=[], max_dev=None)
    a = (best[1] + best[2]) / 2

    def miss(i, rs):
        t = a + i * k
        if any(t0 <= int(t // 1) <= t1 or (t0 == tmin and t < t0) or (t1 == tmax and t > t1) for t0, t1 in rs):
            return 0.0
        return min(t0 - t if t < t0 else t - (t1 + 1) for t0, t1 in rs)

    off = [(i, rs, miss(i, rs)) for i, rs in matched]
    off = [(i, rs, d) for i, rs, d in off if d > 0]
    return dict(matched=len(matched), unmatched=unmatched, a=a, a_range=(best[1], best[2]), off=off,
                max_dev=max((d for _, _, d in off), default=0.0))


def run_dumps(opts, sc, out, t0, t1):
    """Runs the scenario's start and keys with a picture dump at every tick t0..t1 into out."""
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, 'pref'))
    start = sc['start']
    args = [opts.cli, '--data', opts.data, '--edition', 'dos', '--headless', '--pref-dir', os.path.join(out, 'pref')]
    keys = sc.get('keys', '')
    if 'keys_file' in sc:
        with open(os.path.join(sc['dir'], sc['keys_file']), encoding='utf-8') as f:
            keys = ' '.join(line.strip() for line in f if line.strip() and not line.startswith('#'))
    if keys:
        args += ['--keys', keys]
    if 'state' in start:
        args += ['--dos-load-state', os.path.join(opts.captures, start['state'])]
    else:
        if start.get('seed') is not None:
            args += ['--dos-layout-seed', str(start['seed'])]
        if start.get('users'):
            shutil.copyfile(os.path.join(opts.captures, start['users']), os.path.join(out, 'pref', 'dos_users.dat'))
    if sc.get('look'):
        args += ['--look', sc['look']]
    args += sc.get('extra_args', [])
    env = dict(os.environ, SDL_VIDEO_DRIVER='offscreen', SDL_AUDIO_DRIVER='dummy')
    ticks = list(range(t0, t1 + 1))
    for c in range(0, len(ticks), 600):  # the Windows command line holds about 600 dumps
        chunk = ticks[c:c + 600]
        dumps = []
        for t in chunk:
            dumps += ['--dos-dump-state', '%d:f_%06d.bin' % (t, t)]
        p = subprocess.run(args + ['--dos-ticks', str(chunk[-1])] + dumps, capture_output=True, text=True, env=env, cwd=out)
        if p.returncode:
            raise SystemExit('engine run failed:\n' + (p.stdout + p.stderr)[-1500:])


def describe(name, r, verbose):
    lines = ['%s: %d AVI frames matched (%d without an equal engine frame)' % (name, r['matched'], r['unmatched'])]
    if r['a'] is None:
        lines.append('  no time base found')
        return lines
    lines.append('  tick = %.2f + frame * k; %d matched frames off it, at most %.2f ticks' % (
        r['a'], len(r['off']), r['max_dev']))
    for i, rs, d in r['off'][:(None if verbose else 12)]:
        lines.append('    AVI frame %d: engine ticks %s, fitted %.2f (off by %.2f)' % (
            i, ' '.join('%d-%d' % (t0, t1) for t0, t1 in rs), r['a'] + i * r['k'], d))
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('scenarios', nargs='*')
    ap.add_argument('--cli', default=os.environ.get('TM_CLI', os.path.join(REPO, 'build', 'tmport-cli.exe')))
    ap.add_argument('--data', default=os.environ.get('TM_DATA_DOS', os.path.join(REPO, 'TM-DOS')))
    ap.add_argument('--captures', default=os.environ.get('TM_DOS_CAPTURES', os.path.join(REPO, 'extracted', 'captures', 'dos')))
    ap.add_argument('--out', default=os.path.join(REPO, 'extracted', 'scratch', 'dos_w4', 'avifit'))
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--frames', help='a folder of f_<tick>.bin.png dumps (no engine run)')
    ap.add_argument('--avi')
    ap.add_argument('--range', nargs=2, type=int)
    ap.add_argument('--k', type=float, default=EGA_K)
    ap.add_argument('--mask', nargs='*', default=[])
    opts = ap.parse_args()
    opts.cli, opts.data, opts.captures, opts.out = (os.path.abspath(p) for p in (opts.cli, opts.data, opts.captures, opts.out))
    if opts.frames:
        masks = [tuple(int(v) for v in m.split(',')) for m in opts.mask]
        avi = opts.avi if os.path.exists(opts.avi) else os.path.join(opts.captures, opts.avi)
        r = fit(avi, opts.frames, opts.range[0], opts.range[1], opts.k, masks)
        r['k'] = opts.k
        print('\n'.join(describe(os.path.basename(avi), r, opts.verbose)))
        return 0
    failed = 0
    for path in opts.scenarios:
        with open(path, encoding='utf-8') as f:
            sc = json.load(f)
        sc['dir'] = os.path.dirname(os.path.abspath(path))
        name = os.path.splitext(os.path.basename(path))[0]
        for fe in sc.get('avi_fits', []):
            out = os.path.join(opts.out, name, fe['id'])
            run_dumps(opts, sc, out, fe['ticks'][0], fe['ticks'][1])
            k = fe.get('k', EGA_K)
            masks = [tuple(m['rect']) for m in fe.get('masks', [])]
            r = fit(os.path.join(opts.captures, fe['avi']), out, fe['frames'][0], fe['frames'][1], k, masks)
            r['k'] = k
            bad = r['a'] is None or len(r['off']) > fe.get('max_off', 0) or r['max_dev'] > fe.get('max_dev', 0.6)
            failed += bad
            print('\n'.join(describe('%s/%s' % (name, fe['id']), r, opts.verbose)) + ('  FAIL' if bad else ''))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
