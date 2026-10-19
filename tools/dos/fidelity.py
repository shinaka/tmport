"""Fidelity runner of the DOS edition: drives tmport-cli headless through scenario files, compares
the frames with the DOSBox-X reference captures and the per-tick traces with the DOSBox-X frame logs,
checks timings, writes a report and fails on regressions against a stored baseline.

  python -P tools/dos/fidelity.py [GROUP ...] [--scenario NAME ...] [--cli EXE] [--data DIR]
                                  [--captures DIR] [--out DIR] [--baseline FILE] [--update-baseline]
                                  [--jobs N] [--no-avi] [--list]

  GROUP            ui, level1, levels23, castle_end, looks (default: all groups)
  --scenario NAME  only these scenarios (file stem, repeatable)
  --cli            tmport-cli.exe (default: $TM_CLI, else build/tmport-cli.exe)
  --data           the DOS game folder (default: $TM_DATA_DOS, else TM-DOS)
  --captures       the reference captures (default: $TM_DOS_CAPTURES, else extracted/captures/dos)
  --out            report folder (default: extracted/scratch/dos_w4/report)
  --baseline       results to compare with (default: tools/dos/scenarios/baseline.json)
  --update-baseline  store this run's results as the baseline (for the scenarios run)
  --jobs N         parallel engine runs (default 2)
  --no-avi         skip the scenarios' AVI fits (they dump a picture per tick: about 15 s per 1000 ticks)
Exit code: 0 = no regression, 1 = a regression (a checkpoint with more pixels than its baseline or
max_px, a log with more mismatches or fewer compared ticks, a timing out of tolerance, an AVI fit
with more frames off its time base than max_off or further off than max_dev, a failed run), 2 = bad
usage.

Scenario files: tools/dos/scenarios/<group>/<name>.json (numbers, file names and key scripts only):
{
  "description": "what the scenario shows (no game text)",
  "start": {"state": "savestates/r2/fl_walk2.sav"}            a DOSBox-X state (--dos-load-state)
        or {"boot": true, "seed": "0x1EE2", "users": "USERS_after_signin_Ada.DAT"}
                                                             the boot; users: a roster file of the
                                                             captures copied to the pref folder
  "look": "ega",                                             optional (a state's own look otherwise)
  "keys": "55:down:right, 103:up:right",                     the --keys script (game ticks)
  "keys_file": "pt_full_script.txt",                         or a script file next to the scenario
  "ticks": 600,                                              minimum run length
  "checkpoints": [
    {"id": "first", "tick": 1, "capture": "ega/008_shack.png", "max_px": 0,
     "masks": [{"rect": [x0, y0, x1, y1], "reason": "elves are random"}]},
    {"id": "door", "event": "scene=1", "nth": 1, "offset": 3, "capture": "..."}   tick from the trace
    {"id": "end", "at_log_end": "savestates/r2/fl_walk2.sav", "capture_state": "savestates/r2/fl_walk2.sav"}
        the tick the log's end state was saved at (its executed ticks since the start state), against
        the picture on the screen in that state
    {"id": "f204", "tick": 265, "avi": "avi/ega_r2_bonus_coins.avi", "frame": 204}
        against frame 204 of a reference AVI (tools/avi_frames.py)
    "info": "why"  on a checkpoint: informational (the reference shows another moment, e.g. a capture
        taken before the state was saved); compared and kept in the baseline like the others, but
        counted apart in the summary
  ],
  "logs": [{"id": "walk", "state": "savestates/r2/fl_walk2.sav", "since": true,
            "align": ["ticks", "seq"], "fields": ["x", "y", "camx", "camy", "state", "coins"]}],
  "timings": [{"id": "dissolve", "from": {"event": "dissolve=on"}, "to": {"event": "dissolve=off"},
               "expect_ticks": 7, "tolerance": 1, "source": "avi/ega_doorway_dissolve.avi 74-81"}],
  "avi_fits": [{"id": "end_t2", "avi": "avi/ega_r2_end_t2_nokey.avi", "frames": [200, 1450],
                "ticks": [140, 1470], "masks": [...], "max_off": 9, "max_dev": 0.5}]
        every AVI frame of the range looked up among the run's per-tick pictures and the AVI's time
        base fitted (tools/dos/avifit.py; "k": game ticks per AVI frame, default the EGA AVIs')
}
A checkpoint compares the picture shown after its tick (--dos-dump-state TICK:FILE writes FILE.png;
tick 0 of a state run is the loaded moment: the state's own screen). "since": true compares the log
from the scenario's start state on (fl2csv.py --since).
"""
import concurrent.futures
import csv
import datetime
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import avifit  # noqa: E402
import fl2csv  # noqa: E402
import imgcmp  # noqa: E402
import savmem  # noqa: E402
import avi_frames  # noqa: E402  (tools/, put on the path by avifit)

REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
SCENARIOS = os.path.join(HERE, 'scenarios')
GROUPS = ['ui', 'level1', 'levels23', 'castle_end', 'looks']


def env_path(var, default):
    v = os.environ.get(var)
    return v if v else default


class Options:
    def __init__(self, argv):
        self.groups, self.names = [], []
        self.cli = env_path('TM_CLI', os.path.join(REPO, 'build', 'tmport-cli.exe'))
        self.data = env_path('TM_DATA_DOS', os.path.join(REPO, 'TM-DOS'))
        self.captures = env_path('TM_DOS_CAPTURES', os.path.join(REPO, 'extracted', 'captures', 'dos'))
        self.out = os.path.join(REPO, 'extracted', 'scratch', 'dos_w4', 'report')
        self.baseline = os.path.join(SCENARIOS, 'baseline.json')
        self.update = False
        self.jobs = 2
        self.list = False
        self.avi = True
        i = 0
        while i < len(argv):
            a = argv[i]
            if a in ('--cli', '--data', '--captures', '--out', '--baseline', '--scenario', '--jobs'):
                if i + 1 >= len(argv):
                    raise SystemExit(f'{a} needs a value')
                v = argv[i + 1]
                i += 2
                if a == '--scenario':
                    self.names.append(v)
                elif a == '--jobs':
                    self.jobs = max(1, int(v))
                else:
                    setattr(self, a[2:], v)
                continue
            if a == '--update-baseline':
                self.update = True
            elif a == '--list':
                self.list = True
            elif a == '--no-avi':
                self.avi = False
            elif a in ('-h', '--help'):
                print(__doc__)
                raise SystemExit(0)
            elif a in GROUPS:
                self.groups.append(a)
            else:
                print(f'unknown argument {a}', file=sys.stderr)
                raise SystemExit(2)
            i += 1
        self.groups = self.groups or GROUPS
        self.cli = os.path.abspath(self.cli)
        self.data = os.path.abspath(self.data)
        self.captures = os.path.abspath(self.captures)
        self.out = os.path.abspath(self.out)


def load_scenarios(opts):
    out = []
    for g in opts.groups:
        d = os.path.join(SCENARIOS, g)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.endswith('.json'):
                continue
            name = f[:-5]
            if opts.names and name not in opts.names:
                continue
            with open(os.path.join(d, f), encoding='utf-8') as fh:
                sc = json.load(fh)
            sc['name'], sc['group'], sc['dir'] = name, g, d
            out.append(sc)
    return out


def read_trace(path):
    rows = []
    with open(path, newline='') as f:
        lines = [line for line in f if not line.startswith('#')]
    for r in csv.DictReader(lines):
        rows.append((int(r['tick']), r['events'] or ''))
    return rows


def event_tick(trace, spec):
    """The tick of the nth row (1-based) with the event spec['event'] (a whole ';'-separated token:
    'title=1' does not match 'title=11'), plus spec['offset']."""
    nth, seen = int(spec.get('nth', 1)), 0
    for tick, events in trace:
        if spec['event'] in events.split(';'):
            seen += 1
            if seen == nth:
                return tick + int(spec.get('offset', 0))
    return None


def log_end_tick(opts, sc, log_state):
    """The engine tick of the moment a log's end state was saved: the game ticks (time slots of 5 PIT
    ticks, also the ones the original lost in a stall) from the first after the scenario's start state
    up to the end state's g_ticks."""
    if 'state' not in sc['start']:
        return None
    start = savmem.State(os.path.join(opts.captures, sc['start']['state']))
    end = savmem.State(os.path.join(opts.captures, log_state))
    first = start.first_tick_ticks()
    return (end.d(0x3d2b) - first) // 5 + 1 if end.d(0x3d2b) >= first else None


def avi_frame(path, frame, out):
    """Frame `frame` of a reference AVI, saved as a PNG at `out`."""
    for i, _, dec in avi_frames.iter_frames(path):
        if i == frame:
            dec.image().save(out)
            return
    raise ValueError('%s has no frame %d' % (path, frame))


def keys_of(sc):
    if 'keys_file' in sc:
        with open(os.path.join(sc['dir'], sc['keys_file']), encoding='utf-8') as f:
            return ' '.join(line.strip() for line in f if line.strip() and not line.startswith('#'))
    return sc.get('keys', '')


def run_engine(opts, sc, work, dumps, ticks):
    """One headless run: trace.csv, cp_<tick>.bin(.png) for the dump ticks. Returns (ok, log text)."""
    pref = os.path.join(work, 'pref')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(pref)
    start = sc['start']
    args = [opts.cli, '--data', opts.data, '--edition', 'dos', '--headless', '--pref-dir', pref,
            '--dos-trace', os.path.join(work, 'trace.csv'), '--dos-ticks', str(ticks)]
    keys = keys_of(sc)
    if keys:
        args += ['--keys', keys]
    if 'state' in start:
        args += ['--dos-load-state', os.path.join(opts.captures, start['state'])]
    else:
        if start.get('seed') is not None:
            args += ['--dos-layout-seed', str(start['seed'])]
        if start.get('users'):
            shutil.copyfile(os.path.join(opts.captures, start['users']), os.path.join(pref, 'dos_users.dat'))
    if sc.get('look'):
        args += ['--look', sc['look']]
    for t in sorted(set(dumps)):
        args += ['--dos-dump-state', '%d:%s' % (t, os.path.join(work, 'cp_%06d.bin' % t))]
    args += sc.get('extra_args', [])
    env = dict(os.environ)
    env['SDL_VIDEO_DRIVER'] = 'offscreen'
    env['SDL_AUDIO_DRIVER'] = 'dummy'
    p = subprocess.run(args, capture_output=True, text=True, env=env)
    log = ' '.join('"%s"' % a if ' ' in a else a for a in args) + '\n' + p.stdout + p.stderr
    with open(os.path.join(work, 'log.txt'), 'w', encoding='utf-8') as f:
        f.write(log)
    return p.returncode == 0, log


def run_scenario(opts, sc):
    """Runs one scenario and returns its results (dict)."""
    work = os.path.join(opts.out, sc['group'], sc['name'])
    res = dict(group=sc['group'], name=sc['name'], description=sc.get('description', ''), checkpoints=[], logs=[],
               timings=[], avi_fits=[], error=None)
    cps = sc.get('checkpoints', [])
    ticks = int(sc.get('ticks', 0))
    fixed = [int(c['tick']) for c in cps if 'tick' in c] + [log_end_tick(opts, sc, c['at_log_end']) or 0
                                                          for c in cps if 'at_log_end' in c]
    need_events = any('event' in c for c in cps) or sc.get('timings')
    ok, log = True, ''
    trace = None
    if need_events:
        ok, log = run_engine(opts, sc, work, [], max([ticks] + fixed))
        if ok:
            trace = read_trace(os.path.join(work, 'trace.csv'))
    if not ok:
        res['error'] = 'engine run failed: ' + log[-400:]
        return res
    resolved = []
    for c in cps:
        if 'tick' in c:
            t = int(c['tick'])
        elif 'at_log_end' in c:
            t = log_end_tick(opts, sc, c['at_log_end'])
        else:
            t = event_tick(trace, c)
        resolved.append(t)
    dump_ticks = [t for t in resolved if t is not None]
    ok, log = run_engine(opts, sc, work, dump_ticks, max([ticks] + dump_ticks))
    if not ok:
        res['error'] = 'engine run failed: ' + log[-400:]
        return res
    if trace is None:
        trace = read_trace(os.path.join(work, 'trace.csv'))
    # ---- checkpoints
    for c, t in zip(cps, resolved):
        ref = c.get('capture') or c.get('capture_state') or ('%s#%s' % (c['avi'], c['frame']) if 'avi' in c else None)
        cid = c.get('id') or (os.path.splitext(os.path.basename(ref))[0])
        r = dict(id=cid, tick=t, capture=ref, max_px=c.get('max_px'), px=None, bbox=None,
                 masks=[m.get('reason', '') for m in c.get('masks', [])], info=c.get('info'))
        cap = os.path.join(opts.captures, c['capture']) if 'capture' in c else None
        if 'capture_state' in c:  # the picture on the screen when DOSBox-X saved that state
            cap = os.path.join(work, 'ref_%s.png' % cid)
            savmem.screen_image(savmem.State(os.path.join(opts.captures, c['capture_state'])), data=opts.data).save(cap)
        if 'avi' in c:  # a frame of a reference AVI
            cap = os.path.join(work, 'ref_%s.png' % cid)
            avi_frame(os.path.join(opts.captures, c['avi']), int(c['frame']), cap)
        eng = os.path.join(work, 'cp_%06d.bin.png' % t) if t is not None else None
        if t is None:
            r['error'] = 'event not found: %s' % c.get('event')
        elif not os.path.exists(cap):
            r['error'] = 'capture missing'
        elif not os.path.exists(eng):
            r['error'] = 'engine picture missing (the run ended before tick %d?)' % t
        else:
            masks = [tuple(m['rect']) for m in c.get('masks', [])]
            a, b = imgcmp.load(eng), imgcmp.load(cap)
            r['px'], bbox = imgcmp.compare(a, b, masks)
            r['bbox'] = list(bbox) if bbox else None
            diff = os.path.join(work, 'diff_%s.png' % cid)
            imgcmp.diff_image(a, b, masks).save(diff)
            r['diff'] = os.path.relpath(diff, opts.out)
        res['checkpoints'].append(r)
    # ---- logs
    for lg in sc.get('logs', []):
        since = os.path.join(opts.captures, sc['start']['state']) if lg.get('since') and 'state' in sc['start'] else None
        for align in lg.get('align', ['ticks', 'seq']):
            try:
                s = fl2csv.compare(os.path.join(opts.captures, lg['state']), os.path.join(work, 'trace.csv'), since=since,
                                   align=align, fields=lg.get('fields'), quiet=True)
                s['id'] = '%s/%s' % (lg.get('id', os.path.basename(lg['state'])), align)
                res['logs'].append(s)
            except SystemExit as e:
                res['logs'].append(dict(id='%s/%s' % (lg.get('id', ''), align), error=str(e)))
    # ---- timings
    for tm in sc.get('timings', []):
        a, b = event_tick(trace, tm['from']), event_tick(trace, tm['to'])
        r = dict(id=tm['id'], expect=tm['expect_ticks'], tolerance=tm.get('tolerance', 0), source=tm.get('source', ''),
                 measured=None if a is None or b is None else b - a)
        r['ok'] = r['measured'] is not None and abs(r['measured'] - r['expect']) <= r['tolerance']
        res['timings'].append(r)
    # ---- AVI time bases (tools/dos/avifit.py): a picture dump per tick of the range
    for fe in (sc.get('avi_fits', []) if opts.avi else []):
        out = os.path.join(work, 'avi_' + fe['id'])
        avifit.run_dumps(opts, sc, out, fe['ticks'][0], fe['ticks'][1])
        k = fe.get('k', avifit.EGA_K)
        f = avifit.fit(os.path.join(opts.captures, fe['avi']), out, fe['frames'][0], fe['frames'][1], k,
                       [tuple(m['rect']) for m in fe.get('masks', [])])
        r = dict(id=fe['id'], avi=fe['avi'], matched=f['matched'], off=len(f['off']), max_dev=f['max_dev'],
                 max_off=fe.get('max_off', 0), dev_limit=fe.get('max_dev', 0.6))
        r['ok'] = f['a'] is not None and r['off'] <= r['max_off'] and r['max_dev'] <= r['dev_limit']
        res['avi_fits'].append(r)
        shutil.rmtree(out, ignore_errors=True)
    return res


def baseline_key(res, kind, rid):
    return '%s/%s/%s/%s' % (res['group'], res['name'], kind, rid)


def judge(results, baseline):
    """Marks each item ok / regression / new; returns the number of regressions."""
    regressions = 0
    for res in results:
        if res.get('error'):
            res['status'] = 'regression'
            regressions += 1
            continue
        for c in res['checkpoints']:
            b = baseline.get(baseline_key(res, 'cp', c['id']))
            if c.get('error') or c['px'] is None:
                c['status'] = 'regression'
            elif c.get('max_px') is not None and c['px'] > c['max_px']:
                c['status'] = 'regression' if b is None or c['px'] > b else 'over max_px (as baseline)'
            elif b is None:
                c['status'] = 'new'
            elif c['px'] > b:
                c['status'] = 'regression'
            elif c['px'] < b:
                c['status'] = 'better'
            else:
                c['status'] = 'ok'
            c['baseline'] = b
            regressions += c['status'] == 'regression'
        for lg in res['logs']:
            b = baseline.get(baseline_key(res, 'log', lg['id']))
            lg['baseline'] = b
            if lg.get('error'):
                lg['status'] = 'regression'
            elif b is None:
                lg['status'] = 'new'
            elif lg['mismatching'] > b['mismatching'] or lg['compared'] < b['compared']:
                lg['status'] = 'regression'
            elif lg['mismatching'] < b['mismatching'] or lg['compared'] > b['compared']:
                lg['status'] = 'better'
            else:
                lg['status'] = 'ok'
            regressions += lg['status'] == 'regression'
        for tm in res['timings']:
            b = baseline.get(baseline_key(res, 'timing', tm['id']))
            tm['status'] = 'ok' if tm['ok'] else ('regression' if b is None or b else 'off (as baseline)')
            regressions += tm['status'] == 'regression'
        for fe in res.get('avi_fits', []):
            b = baseline.get(baseline_key(res, 'avi', fe['id']))
            fe['status'] = 'ok' if fe['ok'] else ('regression' if b is None or b else 'off (as baseline)')
            regressions += fe['status'] == 'regression'
    return regressions


def new_baseline(results, old):
    out = dict(old)
    for res in results:
        for c in res['checkpoints']:
            if c.get('px') is not None:
                out[baseline_key(res, 'cp', c['id'])] = c['px']
        for lg in res['logs']:
            if 'mismatching' in lg:
                out[baseline_key(res, 'log', lg['id'])] = dict(compared=lg['compared'], mismatching=lg['mismatching'])
        for tm in res['timings']:
            out[baseline_key(res, 'timing', tm['id'])] = bool(tm['ok'])
        for fe in res.get('avi_fits', []):
            out[baseline_key(res, 'avi', fe['id'])] = bool(fe['ok'])
    return dict(sorted(out.items()))


def write_reports(opts, results, regressions):
    os.makedirs(opts.out, exist_ok=True)
    stamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    rows = []
    for res in results:
        for c in res['checkpoints']:
            notes = (['info: ' + c['info']] if c.get('info') else []) + c.get('masks', [])
            rows.append([res['group'], res['name'], 'checkpoint', c['id'], c.get('tick'), c['capture'], c.get('px'),
                         c.get('max_px'), c.get('baseline'), c.get('status'), '; '.join(notes), c.get('error', '')])
        for lg in res['logs']:
            rows.append([res['group'], res['name'], 'log', lg['id'], '', '', lg.get('mismatching'), '',
                         (lg.get('baseline') or {}).get('mismatching', ''), lg.get('status'),
                         '%s of %s ticks compared%s%s' % (lg.get('compared'), lg.get('records'),
                                                         ', %d missing' % len(lg['missing']) if lg.get('missing') else '',
                                                         ', %d extra' % len(lg['extra']) if lg.get('extra') else ''),
                         lg.get('first', lg.get('error', ''))])
        for tm in res['timings']:
            rows.append([res['group'], res['name'], 'timing', tm['id'], '', tm['source'], tm['measured'], tm['expect'], '',
                         tm['status'], 'tolerance %s' % tm['tolerance'], ''])
        for fe in res.get('avi_fits', []):
            dev = '-' if fe['max_dev'] is None else '%.2f' % fe['max_dev']
            rows.append([res['group'], res['name'], 'avi fit', fe['id'], '', fe['avi'], fe['off'], fe['max_off'], '',
                         fe['status'], '%d frames matched, %d off the time base by at most %s ticks (limit %s)' % (
                             fe['matched'], fe['off'], dev, fe['dev_limit']), ''])
        if res.get('error'):
            rows.append([res['group'], res['name'], 'run', '', '', '', '', '', '', 'regression', '', res['error'][:200]])
    header = ['group', 'scenario', 'kind', 'id', 'tick', 'capture/source', 'px/mismatches/measured', 'max_px/expected',
              'baseline', 'status', 'notes', 'detail']
    groups = sorted({r['group'] for r in results})
    for g in groups:
        gdir = os.path.join(opts.out, g)
        os.makedirs(gdir, exist_ok=True)
        grows = [r for r in rows if r[0] == g]
        with open(os.path.join(gdir, 'report.csv'), 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(header)
            w.writerows(grows)
        md = ['# DOS fidelity report: %s' % g, '', 'Run %s with %s.' % (stamp, opts.cli), '']
        md += ['| scenario | kind | id | tick | capture / source | result | baseline | status | notes |', '|---|---|---|---|---|---|---|---|---|']
        for r in grows:
            md.append('| %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (r[1], r[2], r[3], r[4], r[5], r[6], r[8], r[9],
                                                                     (r[10] + ((' ' + r[11]) if r[11] else '')).replace('|', '/')))
        with open(os.path.join(gdir, 'report.md'), 'w', encoding='utf-8') as f:
            f.write('\n'.join(md) + '\n')
        diffs = [(os.path.join(opts.out, c['diff']), '%s %s t=%s: %s px' % (res['name'], c['id'], c['tick'], c['px']))
                 for res in results if res['group'] == g for c in res['checkpoints'] if c.get('diff')]
        if diffs:
            imgcmp.montage(os.path.join(gdir, 'montage.png'), 2, [d for d, _ in diffs], [lab for _, lab in diffs],
                           cell=(960, 200), max_width=1920)
    with open(os.path.join(opts.out, 'results.json'), 'w', encoding='utf-8') as f:
        json.dump(results, f, indent=1)
    md = ['# DOS fidelity report', '', 'Run %s; %d regression(s).' % (stamp, regressions), '',
          '| group | scenarios | checkpoints | 0 px | informational | total px | logs | logs exact | timings ok | '
          'AVI fits ok | regressions |',
          '|---|---|---|---|---|---|---|---|---|---|---|']
    for g in groups:
        rs = [r for r in results if r['group'] == g]
        cps = [c for r in rs for c in r['checkpoints']]
        lgs = [lg for r in rs for lg in r['logs']]
        tms = [t for r in rs for t in r['timings']]
        fes = [f for r in rs for f in r.get('avi_fits', [])]
        md.append('| %s | %d | %d | %d | %d | %d | %d | %d | %d/%d | %d/%d | %d |' % (
            g, len(rs), len(cps), sum(1 for c in cps if c.get('px') == 0),
            sum(1 for c in cps if c.get('info') and c.get('px') != 0),
            sum(c.get('px') or 0 for c in cps if not c.get('info')), len(lgs),
            sum(1 for lg in lgs if lg.get('mismatching') == 0 and lg.get('compared') == lg.get('records')),
            sum(1 for t in tms if t['ok']), len(tms), sum(1 for f in fes if f['ok']), len(fes),
            sum(1 for r in rs if r.get('status') == 'regression') + sum(1 for c in cps if c.get('status') == 'regression') +
            sum(1 for x in lgs + tms + fes if x.get('status') == 'regression')))
    with open(os.path.join(opts.out, 'report.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(md) + '\n\n"informational": checkpoints against a reference of another moment, not 0 px; '
                '"total px": the other checkpoints. Per group: <group>/report.md, report.csv, montage.png; per scenario: '
                '<group>/<scenario>/ (trace.csv, log.txt, diff_*.png).\n')


def main():
    opts = Options(sys.argv[1:])
    scenarios = load_scenarios(opts)
    if opts.list:
        for sc in scenarios:
            print('%-11s %-36s %s' % (sc['group'], sc['name'], sc.get('description', '')))
        return 0
    if not scenarios:
        print('no scenarios', file=sys.stderr)
        return 2
    if not os.path.exists(opts.cli):
        print('tmport-cli not found: %s (use --cli or TM_CLI)' % opts.cli, file=sys.stderr)
        return 2
    def safe(sc):
        try:
            return run_scenario(opts, sc)
        except Exception as e:  # noqa: BLE001 - one broken scenario must not stop the report
            return dict(group=sc['group'], name=sc['name'], description=sc.get('description', ''), checkpoints=[],
                        logs=[], timings=[], avi_fits=[], error='%s: %s' % (type(e).__name__, e))
    with concurrent.futures.ThreadPoolExecutor(max_workers=opts.jobs) as ex:
        results = list(ex.map(safe, scenarios))
    baseline = {}
    if os.path.exists(opts.baseline):
        with open(opts.baseline, encoding='utf-8') as f:
            baseline = json.load(f)
    regressions = judge(results, baseline)
    write_reports(opts, results, regressions)
    for res in results:
        cps = ' '.join('%s=%s' % (c['id'], c.get('px')) for c in res['checkpoints'])
        lgs = ' '.join('%s=%s/%s' % (lg['id'], lg.get('mismatching'), lg.get('compared')) for lg in res['logs'])
        tms = ' '.join('%s=%s(%s)' % (t['id'], t['measured'], t['expect']) for t in res['timings'])
        tms += ''.join(' avi:%s=%s/%s' % (f['id'], f['off'], f['matched']) for f in res.get('avi_fits', []))
        print('%-10s %-34s %s %s %s%s' % (res['group'], res['name'], cps, lgs, tms,
                                          ('  ERROR ' + res['error'][:120]) if res.get('error') else ''))
    print('%d scenarios, %d regression(s); report: %s' % (len(results), regressions, os.path.join(opts.out, 'report.md')))
    if opts.update:
        with open(opts.baseline, 'w', encoding='utf-8') as f:
            json.dump(new_baseline(results, baseline), f, indent=1)
            f.write('\n')
        print('baseline written:', opts.baseline)
    return 1 if regressions else 0


if __name__ == '__main__':
    sys.exit(main())
