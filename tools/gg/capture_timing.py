"""Timing measurements on Gizmos & Gadgets reference captures (DOSBox-X AVIs and OPL .dro logs).

The AVIs are DOSBox-X's ZMBV recordings of mode 13h: one frame per emulated vertical retrace
(70.0863 frames/s, 14.268 ms), stored as 32-bit pixels (ZMBV format 8), so a palette load shows as
a change of every block that uses the changed colours; palette loads (fades) and screen updates can
be timed frame-exactly. The PIT runs at 145.652 Hz in the game
(6.866 ms per tick, ARCHITECTURE.md section 3.1): one video frame = 2.078 PIT ticks.
Decoding uses tools/avi_frames.py (pure Python; about 100-300 frames/s).

Usage (python -P capture_timing.py ...):
  info AVI                              frames, rate, duration
  palette AVI [--from N] [--to M] [--gap G] [--min-blocks K]
                                        frames where at least K (default 300) of the 1000 16x16 blocks
                                        change (palette loads and full redraws: DOSBox-X records mode
                                        13h as 32-bit pixels, so a palette load is a change of every
                                        block using the changed colours), with the mean brightness;
                                        then runs of them (a run ends after G frames, default 3)
  pace AVI --box X,Y,W,H [--from N] [--to M] [--min-blocks K]
                                        the frames where the box (AVI pixels, 640x400) changed and
                                        the histogram of the intervals between them, in frames, ms
                                        and PIT ticks: the frame period of whatever animates there
  gaps AVI [--from N] [--to M] [--min F]
                                        runs of at least F (default 20) identical frames: candidate
                                        stalls or waits
  dro DRO [--grid MS]                   DRO v2 log: hardware type, register writes, duration, the
                                        delay histogram and the best-fit tick of the write groups
                                        (default search 13.0..14.5 ms)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import avi_frames  # noqa: E402

PIT_HZ = 1193181.6667 / 8192.0          # 145.652 Hz
VGA_HZ = 70.0863


def opt(argv, name, default=None, conv=int):
    if name in argv:
        return conv(argv[argv.index(name) + 1])
    return default


def frames_range(argv, n):
    return opt(argv, '--from', 0), opt(argv, '--to', n - 1)


def cmd_info(path):
    info, chunks = avi_frames.read_avi(path)
    n = len(chunks)
    print(f'{path}: {info["width"]}x{info["height"]} {info.get("fourcc")} {info["fps"]:.4f} frames/s, '
          f'{n} frames, {n / info["fps"]:.2f} s')


def cmd_palette(path, argv):
    """DOSBox-X records mode 13h as 32-bit pixels (ZMBV format 8), so a palette load shows as a change
    of every block that uses the changed colours. A frame counts as a palette step when at least
    --min-blocks (default 300 of the 1000 16x16 blocks of a 640x400 frame) change; the mean
    brightness of a sample of pixels is printed so fades read as ramps."""
    info, chunks = avi_frames.read_avi(path)
    lo, hi = frames_range(argv, len(chunks))
    gap = opt(argv, '--gap', 3)
    minb = opt(argv, '--min-blocks', 300)
    dec = avi_frames.Zmbv(info['width'], info['height'])
    ms = 1000.0 / info['fps']
    steps = []
    for i, c in enumerate(chunks):
        if i > hi:
            break
        dec.decode(c)
        if i < lo or not dec.changed:
            continue
        n = len(dec.changed)
        if n >= minb:
            # brightness: mean of R+G+B over a 40x25 grid of pixels
            tot = 0
            for yy in range(8, info['height'], 16):
                for xx in range(8, info['width'], 16):
                    o = dec._off(xx, yy)
                    tot += dec.cur[o] + dec.cur[o + 1] + dec.cur[o + 2]
            bright = tot / (40 * 25 * 3)
            steps.append((i, n, bright))
    for i, n, br in steps:
        print(f'frame {i:6d} {i * ms:10.1f} ms  {n:4d} blocks changed  brightness {br:6.1f}')
    runs = []
    for st in steps:
        if runs and st[0] - runs[-1][-1][0] <= gap:
            runs[-1].append(st)
        else:
            runs.append([st])
    print(f'runs of global changes (a gap of more than {gap} frames ends a run):')
    for r in runs:
        a, b = r[0][0], r[-1][0]
        frames = b - a + 1
        trend = 'darker' if r[-1][2] < r[0][2] - 1 else ('brighter' if r[-1][2] > r[0][2] + 1 else 'flat')
        print(f'  frames {a}..{b}: {frames} frames = {frames * ms:.1f} ms, {len(r)} changed frames, '
              f'brightness {r[0][2]:.1f} -> {r[-1][2]:.1f} ({trend})')


def cmd_pace(path, argv):
    box = opt(argv, '--box', None, lambda s: tuple(int(v) for v in s.split(',')))
    if not box:
        raise SystemExit('pace: --box X,Y,W,H needed')
    minb = opt(argv, '--min-blocks', 1)
    info, chunks = avi_frames.read_avi(path)
    lo, hi = frames_range(argv, len(chunks))
    bx, by, bw, bh = box
    dec = avi_frames.Zmbv(info['width'], info['height'])
    hits = []
    for i, c in enumerate(chunks):
        if i > hi:
            break
        dec.decode(c)
        if i < lo or not dec.changed:
            continue
        n = sum(1 for (x, y, w, h) in dec.changed if x < bx + bw and bx < x + w and y < by + bh and by < y + h)
        if n >= minb:
            hits.append(i)
    ms = 1000.0 / info['fps']
    iv = [b - a for a, b in zip(hits, hits[1:])]
    print(f'{len(hits)} frames changed inside {box} between frames {lo} and {hi}')
    hist = {}
    for d in iv:
        hist[d] = hist.get(d, 0) + 1
    for d in sorted(hist):
        print(f'  interval {d:3d} frames = {d * ms:7.1f} ms = {d * ms * PIT_HZ / 1000:6.2f} PIT ticks: {hist[d]}')
    if iv:
        mean = sum(iv) / len(iv)
        print(f'  mean {mean:.3f} frames = {mean * ms:.2f} ms = {mean * ms * PIT_HZ / 1000:.2f} PIT ticks')
    if len(hits) > 2:
        # A flush that straddles the capture point shows in two consecutive frames: count it once.
        # The mean interval between the remaining update events over a steady span is the period of
        # whatever animates in the box (the game has no catch-up, so overrunning frames stretch it).
        firsts = [h for i, h in enumerate(hits) if i == 0 or h != hits[i - 1] + 1]
        if len(firsts) > 1:
            m = (firsts[-1] - firsts[0]) / (len(firsts) - 1)
            print(f'  {len(firsts)} update events after merging consecutive frames; mean interval '
                  f'{m:.3f} frames = {m * ms:.2f} ms = {m * ms * PIT_HZ / 1000:.2f} PIT ticks')
    if '--list' in argv:
        print(' '.join(str(h) for h in hits))


def cmd_gaps(path, argv):
    info, chunks = avi_frames.read_avi(path)
    lo, hi = frames_range(argv, len(chunks))
    mn = opt(argv, '--min', 20)
    dec = avi_frames.Zmbv(info['width'], info['height'])
    ms = 1000.0 / info['fps']
    run_start = None
    for i, c in enumerate(chunks):
        if i > hi:
            break
        dec.decode(c)
        same = not dec.changed
        if i < lo:
            continue
        if same and run_start is None:
            run_start = i
        elif not same and run_start is not None:
            if i - run_start >= mn:
                print(f'  frames {run_start}..{i - 1}: {i - run_start} unchanged frames = {(i - run_start) * ms:.1f} ms')
            run_start = None
    if run_start is not None and hi + 1 - run_start >= mn:
        print(f'  frames {run_start}..{hi}: {hi + 1 - run_start} unchanged frames (to the end)')


def read_dro(path):
    d = open(path, 'rb').read()
    if d[:8] != b'DBRAWOPL':
        raise SystemExit('not a DRO file')
    vmaj, vmin = struct.unpack_from('<HH', d, 8)
    if vmaj != 2:
        raise SystemExit(f'DRO v{vmaj}.{vmin} not handled')
    pairs, ms_len, hw, fmt, comp, sdelay, ldelay, cmap_len = struct.unpack_from('<IIBBBBBB', d, 12)
    cmap = d[26:26 + cmap_len]
    pos = 26 + cmap_len
    t = 0
    writes = []
    for _ in range(pairs):
        code, val = d[pos], d[pos + 1]
        pos += 2
        if code == sdelay:
            t += val + 1
        elif code == ldelay:
            t += (val + 1) << 8
        else:
            bank = 1 if code & 0x80 else 0
            writes.append((t, bank, cmap[code & 0x7F], val))
    return {'hw': hw, 'pairs': pairs, 'ms': ms_len, 'writes': writes}


def cmd_dro(path, argv):
    r = read_dro(path)
    w = r['writes']
    print(f'{path}: hardware type {r["hw"]} ({"OPL2" if r["hw"] == 0 else "dual/OPL3"}), {len(w)} register writes, '
          f'{r["ms"]} ms')
    if not w:
        return
    print(f'  first write at {w[0][0]} ms, last at {w[-1][0]} ms')
    times = sorted(set(t for t, *_ in w))
    gaps = [b - a for a, b in zip(times, times[1:])]
    hist = {}
    for g in gaps:
        hist[g] = hist.get(g, 0) + 1
    top = sorted(hist.items(), key=lambda kv: -kv[1])[:8]
    print('  most common gaps between write groups (ms): ' + ', '.join(f'{g}:{n}' for g, n in top))
    # best-fit tick: the period P minimising the distance of group times to a grid k*P + phase
    lo, hi = opt(argv, '--grid', (13.0, 14.5), lambda s: tuple(float(v) for v in s.split(',')))
    best = None
    p = lo
    while p <= hi:
        # phase from the first group; error = mean distance to the nearest grid point
        ph = times[0]
        err = sum(min((t - ph) % p, p - (t - ph) % p) for t in times) / len(times)
        if best is None or err < best[1]:
            best = (p, err)
        p += 0.001
    print(f'  best-fit grid {best[0]:.3f} ms ({1000 / best[0]:.3f} Hz), mean distance {best[1]:.3f} ms '
          f'(DRO times are whole ms)')
    regs = {}
    for _, bank, reg, _ in w:
        regs[reg] = regs.get(reg, 0) + 1
    rhythm = regs.get(0xBD, 0)
    print(f'  distinct registers {len(regs)}; writes to BD (rhythm/depth) {rhythm}')


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return
    cmd, path = argv[0], argv[1]
    if cmd == 'info':
        cmd_info(path)
    elif cmd == 'palette':
        cmd_palette(path, argv)
    elif cmd == 'pace':
        cmd_pace(path, argv)
    elif cmd == 'gaps':
        cmd_gaps(path, argv)
    elif cmd == 'dro':
        cmd_dro(path, argv)
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
