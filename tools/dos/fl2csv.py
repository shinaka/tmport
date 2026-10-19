"""DOSBox-X frame logs (framelog.py rings of an r2 fl_* state) against the engine's per-tick trace
(--dos-trace, src/dos/game/trace.h).

  python -P fl2csv.py csv LOG OUT.csv [--since START]
        STEP records -> rows in the trace's layout (gated ticks from the BLOCKED ring as gated rows;
        nets..events left empty)
  python -P fl2csv.py cmp LOG ENGINE.csv [--since START] [--align ticks|seq|tick] [--from N]
                          [--fields x,y,camx,camy,state,keys,coins] [--json OUT.json] [--quiet]
        compare the executed (ungated) ticks and print the mismatches and a summary line
  python -P fl2csv.py keys LOG [--since START] [--gap N]
        a --keys script that holds the arrows and Space while the log's g_keys has their bits
        (a starting point for a scenario; jumps and BIOS keys are added by hand)

LOG is the state that holds the log (saved after the run); START the state the run started from
(the one the engine loads with --dos-load-state). With --since only the records written after START
count (a ring can still hold records of an earlier run) and tick numbers count from START: tick 1 is
the first game tick after the load, as in the engine's trace of a --dos-load-state run.

Alignments:
  ticks  by game-tick slot, (g_ticks - first) / 5 + 1 = the engine's tick number of a run from START
         (default with --since): exact timing; a tick the original lost (the scene-load stall:
         Game_Tick frozen while files load, g_quit = 1) and the engine ran counts as extra, one the
         engine did not run as missing
  seq    the k-th executed tick of the log against the k-th of the engine: the logic sequence
         without the timing (stalls and late ticks absorbed); the gaps are listed
  tick   by tick number, the first record against engine row N (--from, default: the first engine
         row whose x, y, state equal it); for runs not started from the log's state
"""
import csv
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import savmem  # noqa: E402
import framelog  # noqa: E402

COLS = ['tick', 'ticks', 'gated', 'x', 'y', 'camx', 'camy', 'state', 'keys', 'coins', 'nets', 'level', 'scene',
        'uimode', 'area', 'events']
FIELDS = ['x', 'y', 'camx', 'camy', 'state', 'keys', 'coins']


def dosbox_rows(log_state, since=None):
    """Rows of the log: dict(tick, ticks (full g_ticks when --since, else the low word), gated, fields)."""
    st = savmem.State(log_state)
    fr, steps, blk = framelog.rings(st)
    if not steps:
        raise SystemExit('no STEP records in ' + log_state)
    rows = []
    if since:
        s0 = savmem.State(since)
        first = s0.first_tick_ticks()  # g_ticks of the first game tick after the load
        end = st.d(0x3d2b)
        end16 = end & 0xffff

        def full(t16):
            return end - ((end16 - t16) & 0xffff)
        for s in steps:
            t = full(s['ticks'])
            if t >= first:
                rows.append(dict(tick=(t - first) // 5 + 1, ticks=t, gated=0, **{k: s[k] for k in FIELDS}))
        for b in blk:
            t = full(b)
            if t >= first:
                rows.append(dict(tick=(t - first) // 5 + 1, ticks=t, gated=1))
    else:
        t0 = steps[0]['ticks']
        for s in steps:
            rows.append(dict(tick=((s['ticks'] - t0) & 0xffff) // 5 + 1, ticks=s['ticks'], gated=0,
                             **{k: s[k] for k in FIELDS}))
        for b in blk:
            d = (b - t0) & 0xffff
            if d < 0x8000:
                rows.append(dict(tick=d // 5 + 1, ticks=b, gated=1))
    rows.sort(key=lambda r: (r['ticks'] if since else r['tick'], r['gated']))
    return rows


def engine_rows(path):
    with open(path, newline='') as f:
        lines = [line for line in f if not line.startswith('#')]
    out = []
    for r in csv.DictReader(lines):
        out.append({k: int(r[k]) for k in ['tick', 'ticks', 'gated'] + FIELDS})
    return out


def compare(log_state, engine_csv, since=None, align=None, start=None, fields=None, limit=30, quiet=False):
    """Returns a summary dict: compared, mismatching, first (text), gaps, align, records."""
    fields = fields or FIELDS
    align = align or ('ticks' if since else 'tick')
    ref = [r for r in dosbox_rows(log_state, since) if not r['gated']]
    eng = [r for r in engine_rows(engine_csv) if not r['gated']]
    pairs = []
    gaps = []
    missing, extra = [], []
    if align == 'seq':
        # From the engine tick at the log's first g_ticks (a ring that wrapped starts late in the run).
        e0 = 0
        if since and ref:
            e0 = next((i for i, r in enumerate(eng) if r['tick'] >= ref[0]['tick']), len(eng))
        pairs = list(zip(ref, eng[e0:]))
        for a, b in zip(ref, ref[1:]):
            if b['tick'] - a['tick'] > 1:
                gaps.append((a['tick'], b['tick'] - a['tick'] - 1))
    elif align == 'ticks':
        # Every log tick against the engine row of the same game-tick slot (g_ticks - first) / 5 + 1,
        # which is the engine's tick number of a run from the start state (the logger reads g_ticks
        # inside Game_Tick, so a timer interrupt nested before it shows as +1..4). A log tick the
        # engine did not run counts as missing, an engine row between the log's ticks whose slot the
        # original did not run (a stall the engine lacks) as extra. Both count as mismatching.
        if not since:
            raise SystemExit('--align ticks needs --since START')
        by = {e['tick']: e for e in eng}
        last_eng = max((e['tick'] for e in eng), default=0)
        for r in ref:
            if r['tick'] > last_eng:
                break  # past the end of the engine's run
            e = by.get(r['tick'])
            if e is None:
                missing.append(r['tick'])
                continue
            pairs.append((r, e))
        if ref:
            logged = {r['tick'] for r in dosbox_rows(log_state, since)}  # executed and blocked ticks
            lo, hi = ref[0]['tick'], min(ref[-1]['tick'], last_eng)
            extra = [e['tick'] for e in eng if lo <= e['tick'] <= hi and e['tick'] not in logged]
        for a, b in zip(ref, ref[1:]):
            if b['tick'] - a['tick'] > 1:
                gaps.append((a['tick'], b['tick'] - a['tick'] - 1))
    else:
        if start is None:
            for r in eng:
                if all(r[k] == ref[0][k] for k in ('x', 'y', 'state')):
                    start = r['tick']
                    break
        if start is None:
            raise SystemExit('no engine row matches the first DOSBox-X record')
        by = {r['tick']: r for r in eng}
        for r in ref:
            e = by.get(start + r['tick'] - 1)
            if e is None:
                break
            pairs.append((r, e))
    bad = len(missing) + len(extra)
    first = ''
    if missing:
        first = f"log tick {missing[0]} has no engine tick at its g_ticks"
    if extra and (not missing or extra[0] < missing[0]):
        first = f"engine tick {extra[0]} has no log tick (the original did not run a tick there)"
    for r, e in pairs:
        diff = [f"{k}:{r[k]:#x}/{e[k]:#x}" for k in fields if r[k] != e[k]]
        if diff:
            bad += 1
            line = f"dosbox t={r['tick']} engine t={e['tick']}: " + ' '.join(diff)
            if not first:
                first = line
            if not quiet and bad <= limit:
                print(line)
    summary = dict(align=align, records=len(ref), compared=len(pairs) + len(missing), mismatching=bad, first=first,
                   gaps=gaps, missing=missing[:50], extra=extra[:50], fields=fields)
    if not quiet:
        gap_text = ', '.join(f'{n} lost after log tick {t}' for t, n in gaps) or 'none'
        print(f"{summary['compared']} of {len(ref)} log ticks compared ({align}): {bad} mismatching "
              f"({len(missing)} missing, {len(extra)} extra in the engine); gaps in the log: {gap_text}")
    return summary


KEY_BITS = ((0x01, 'up'), (0x02, 'down'), (0x04, 'left'), (0x08, 'right'), (0x10, 'space'))


def keys_script(log_state, since=None, gap=3):
    """A --keys script (game ticks from the load) that holds each key while the log's g_keys has its
    bit: a run of a bit (interruptions of up to `gap` ticks, the terrain clearing a held bit until the
    next typematic repeat, belong to the run) becomes TICK:down:KEY .. TICK:up:KEY. A starting point
    for a scenario: jumps (bit 0x20, Up with Left/Right) and BIOS keys (menus, the quiz) are not in
    g_keys' low bits and must be added by hand."""
    ref = [r for r in dosbox_rows(log_state, since) if not r['gated']]
    events = []
    for bit, name in KEY_BITS:
        run_start = last = None
        for r in ref:
            if r['keys'] & bit:
                if run_start is None or r['tick'] - last > gap + 1:
                    if run_start is not None:
                        events.append((run_start, 'down', name))
                        events.append((last + 1, 'up', name))
                    run_start = r['tick']
                last = r['tick']
        if run_start is not None:
            events.append((run_start, 'down', name))
            events.append((last + 1, 'up', name))
    events.sort()
    return ', '.join(f'{t}:{a}:{k}' for t, a, k in events)


def main():
    args = sys.argv[1:]
    if len(args) < 3:
        print(__doc__)
        return

    def opt(name, default=None):
        return args[args.index(name) + 1] if name in args else default
    since = opt('--since')
    if args[0] == 'csv':
        rows = dosbox_rows(args[1], since)
        with open(args[2], 'w', newline='') as f:
            f.write('# from %s (framelog STEP / BLOCKED rings)\n' % os.path.basename(args[1]))
            w = csv.writer(f)
            w.writerow(COLS)
            for r in rows:
                w.writerow([r.get(c, '') for c in COLS])
        print(len(rows), 'rows')
        return
    if args[0] == 'keys':
        print(keys_script(args[1], since, int(opt('--gap', '3'))))
        return
    fields = opt('--fields')
    s = compare(args[1], args[2], since, opt('--align'), int(opt('--from')) if opt('--from') else None,
                fields.split(',') if fields else None, quiet='--quiet' in args)
    if opt('--json'):
        with open(opt('--json'), 'w') as f:
            json.dump(s, f, indent=1)


if __name__ == '__main__':
    main()
