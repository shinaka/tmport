"""Headless playthrough driver for the DOS edition: builds a --keys script (game ticks) step by step.

Each step reruns tmport-cli from the boot (or from a DOSBox-X state) with the script so far (a run is
deterministic with --dos-layout-seed), reads the --dos-trace CSV, finds the tick where a condition
holds and appends the next key event there. stages.py has the stages of the wave-3 playthroughs on
the layout of seed 0x1EE2; pt_full.py strings them into the complete game.

Library:
    from play import Player, cell_of
    p = Player('mine')                                 # runs under <scratch>/mine/
    for t, s in [(60, 'tap:enter'), ...]: p.add(t, s)
    t, tr = p.hold_until('right', 800, lambda r: cell_of(r) == 0xE9)
    print(p.script())

Environment: TM_CLI (tmport-cli.exe, default build/tmport-cli.exe), TM_DATA_DOS (the DOS folder,
default TM-DOS), TM_DOS_PLAY_SCRATCH (default extracted/scratch/dos_w4/play). Every run gets its
own preference folder under the scratch folder.
"""
import csv
import os
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
CLI = os.path.abspath(os.environ.get('TM_CLI') or os.path.join(ROOT, 'build', 'tmport-cli.exe'))
DATA = os.path.abspath(os.environ.get('TM_DATA_DOS') or os.path.join(ROOT, 'TM-DOS'))
SCR = os.path.abspath(os.environ.get('TM_DOS_PLAY_SCRATCH') or os.path.join(ROOT, 'extracted', 'scratch', 'dos_w4', 'play'))


class Trace:
    """A --dos-trace CSV: rows as dicts of ints (uimode from hex, events as text)."""

    def __init__(self, path):
        with open(path, newline='') as f:
            lines = [line for line in f if not line.startswith('#')]
        self.rows = []
        for r in csv.DictReader(lines):
            d = {}
            for k, v in r.items():
                if k == 'events':
                    d[k] = v or ''
                elif k == 'uimode':
                    d[k] = int(v, 16)
                else:
                    d[k] = int(v)
            self.rows.append(d)
        self.by_tick = {r['tick']: r for r in self.rows}

    def at(self, tick):
        return self.by_tick.get(tick)

    def last(self):
        return self.rows[-1]

    def first(self, cond, start=0):
        for r in self.rows:
            if r['tick'] >= start and cond(r):
                return r
        return None

    def events(self, start=0, end=1 << 60, needle=None):
        out = []
        for r in self.rows:
            if start <= r['tick'] < end and r['events'] and (needle is None or needle in r['events']):
                out.append((r['tick'], r['events']))
        return out


class Player:
    """A playthrough under construction: key events (tick, 'down:KEY' | 'up:KEY' | 'tap:KEY[:N]')."""

    def __init__(self, name, seed=0x1EE2, users=None, look=None, state=None, extra=()):
        self.name = name
        self.seed = seed
        self.users = users  # a USERS.DAT to start with (else the import of the DOS folder's USERS.DAT)
        self.state = state  # a DOSBox-X .sav to start from (--dos-load-state) instead of the boot
        self.items = []
        self.look = look
        shutil.rmtree(os.path.join(SCR, name), ignore_errors=True)
        self.extra = list(extra)
        self.n = 0

    def add(self, tick, text):
        self.items.append((tick, text))

    def script(self):
        return ', '.join('%d:%s' % (t, s) for t, s in sorted(self.items, key=lambda i: i[0]))

    def run(self, ticks, dumps=(), frames=False, tag=None):
        """One headless run of `ticks` game ticks with the script so far; returns its Trace (with
        .dir and .log). dumps: ticks for --dos-dump-state (d<tick>.bin)."""
        self.n += 1
        tag = tag or 'run%03d' % self.n
        out = os.path.join(SCR, self.name, tag)
        shutil.rmtree(out, ignore_errors=True)
        pref = os.path.join(out, 'pref')
        os.makedirs(pref)
        if self.users:
            shutil.copyfile(self.users, os.path.join(pref, 'dos_users.dat'))
        args = [CLI, '--edition', 'dos', '--data', DATA, '--headless', '--pref-dir', pref,
                '--keys', self.script() or '1:up:f12', '--dos-ticks', str(ticks),
                '--dos-trace', os.path.join(out, 'trace.csv')]
        if self.state:
            args += ['--dos-load-state', self.state]
        elif self.seed is not None:
            args += ['--dos-layout-seed', str(self.seed)]
        if self.look:
            args += ['--look', self.look]
        for t in dumps:
            args += ['--dos-dump-state', '%d:%s' % (t, os.path.join(out, 'd%06d.bin' % t))]
        if frames:
            args += ['--frame-dump', os.path.join(out, 'frames')]
        args += self.extra
        env = dict(os.environ)
        env['SDL_VIDEO_DRIVER'] = 'offscreen'
        env['SDL_AUDIO_DRIVER'] = 'dummy'
        with open(os.path.join(out, 'script.txt'), 'w') as f:
            f.write(self.script() + '\n')
        p = subprocess.run(args, capture_output=True, text=True, env=env)
        with open(os.path.join(out, 'log.txt'), 'w') as f:
            f.write(p.stdout + p.stderr)
        if p.returncode != 0:
            print(p.stdout, p.stderr)
            raise SystemExit('cli failed %d' % p.returncode)
        tr = Trace(os.path.join(out, 'trace.csv'))
        tr.dir = out
        tr.log = p.stdout + p.stderr
        return tr

    def hold_until(self, key, start, cond, max_ticks=3000, release=True, dismiss=True):
        """Hold `key` from `start` until cond(row) holds (released on that tick). A hint or message
        that opens on the way (g_uiMode 0x10 or 4) is closed with Enter 30 ticks later."""
        self.add(start, 'down:' + key)
        tr = self.run(start + max_ticks)
        r = tr.first(lambda r: cond(r) or (dismiss and r['uimode'] in (4, 0x10)), start + 1)
        scan = start + 1
        while r is not None and not cond(r):
            t = r['tick'] + 30
            self.add(t, 'tap:enter')
            print('  (closed a hint/message opened on tick %d)' % r['tick'], flush=True)
            scan = t + 1
            tr = self.run(start + max_ticks)
            if tr.at(t + 5) and tr.at(t + 5)['uimode'] in (4, 0x10):
                raise SystemExit('could not close the box at %d' % t)
            r = tr.first(lambda r: cond(r) or (dismiss and r['uimode'] in (4, 0x10)), scan)
        if r is None:
            self.items.remove((start, 'down:' + key))
            raise SystemExit('hold %s from %d: condition not met; last row %s' % (key, start, tr.last()))
        if release:
            self.add(r['tick'], 'up:' + key)
        return r['tick'], tr


def cell_of(r):
    """The world cell (strip row << 4 | column) of the player in a trace row."""
    return ((r['y'] >> 7) << 4) | ((r['x'] >> 7) & 0xF)


COLS = ('x', 'y', 'camx', 'camy', 'state', 'keys', 'coins', 'nets', 'level', 'scene', 'uimode', 'area')


def show(tr, start, end, every=1, cols=COLS):
    for r in tr.rows:
        if start <= r['tick'] < end and (r['tick'] - start) % every == 0:
            print(r['tick'], ' '.join('%s=%s' % (c, hex(r[c]) if c in ('x', 'y', 'camx', 'camy') else r[c]) for c in cols),
                  'cell=%02X' % cell_of(r), r['events'][:150])


def changes(tr, start, end, cols=('x', 'y', 'camx', 'camy', 'state', 'coins', 'nets', 'level', 'scene', 'uimode', 'area')):
    prev = None
    for r in tr.rows:
        if not (start <= r['tick'] < end):
            continue
        cur = tuple(r[c] for c in cols)
        ev = ';'.join(e for e in r['events'].split(';') if e and not e.startswith('key='))
        if cur != prev or ev:
            print(r['tick'], ' '.join('%s=%s' % (c, hex(r[c]) if c in ('x', 'y', 'camx', 'camy') else r[c]) for c in cols),
                  'cell=%02X' % cell_of(r), ev[:160])
        prev = cur
