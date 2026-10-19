"""The engine's OPL register writes against a DOSBox-X raw OPL capture (.dro), write for write.

python -P tools/dos/oplcmp.py ENGINE_LOG REF.dro [--start dump|tune] [--out REPORT.txt] [--max-list N]

ENGINE_LOG: 'tick reg value [c]' lines (hex reg and value), as `tmport --dos-sound adlib ... --opl-log`
writes them: every register write of the AdLib driver (SST.EXE 4ac9) with the PIT tick it belongs to;
'c' marks a command's writes (Music_Play, Music_Stop: made between two interrupts, logged with the
tick they run before).

DOSBox-X's capture (v2 .dro, 1 ms resolution) starts with a dump of the chip's registers (the key
bits of 0xB0-0xB8 masked, zero registers left out) and then logs only writes that change a register
(a write of the value a register already holds is dropped; the timer registers are not logged).
The comparison
  * --start dump (default; for an engine run started from the state the capture was made on,
    `--dos-sound adlib soundtest --from-state`): the capture usually starts inside a tick's group of
    writes, so it begins with the first complete group (the first write after 1 ms) and looks for the
    engine tick whose writes, filtered from the capture's registers at that point, continue the
    capture; it reports how the engine's registers at that tick (after the restore and the ticks
    before) compare with the capture's dump;
  * --start tune: replays the capture's registers up to its first instrument burst (AdLib_PlayTune)
    and starts the engine's stream at its own first instrument burst;
  * then drops the engine's writes that would not change a register, like DOSBox-X, and compares the
    rest write for write (register and value, in order);
  * fits the capture's time base to the engine's PIT ticks on the writes made inside the timer
    interrupt (note on/off): a write is expected at A + tick * 1000 / 236.69543 + k * 0.070 ms, k =
    the writes before it in its tick (logged or not; 70 us per AdLib_Write on the reference machine,
    opl_chip.h), and reports how far each lies from that (the capture's ms are whole, so up to 1 ms
    early), and where the commands' writes lie (between the tick before and their tick).
Exit code 1 when a write differs or is missing.
"""
import argparse
import statistics
import struct
import sys

TICK_MS = 1000.0 * 5041 / 1193181.6667
KEY_REGS = range(0xB0, 0xB9)


def parse_dro(path):
    d = open(path, 'rb').read()
    if d[:8] != b'DBRAWOPL':
        raise SystemExit(f'{path}: not a DOSBox raw OPL capture')
    major, _minor = struct.unpack_from('<HH', d, 8)
    if major != 2:
        raise SystemExit(f'{path}: DRO v{major} not supported')
    pairs = struct.unpack_from('<I', d, 12)[0]
    sdel, ldel, cml = d[23], d[24], d[25]
    cmap = d[26:26 + cml]
    p = 26 + cml
    t = 0
    out = []
    for _ in range(pairs):
        c, v = d[p], d[p + 1]
        p += 2
        if c == sdel:
            t += v + 1
        elif c == ldel:
            t += (v + 1) << 8
        else:
            out.append((t, (0x100 if c & 0x80 else 0) | cmap[c & 0x7F], v))
    return out


WRITE_MS = 0.070


def parse_engine(path):
    """(tick, reg, value, command, k): k = the writes before it in its tick (of its kind)."""
    out = []
    last = None
    k = 0
    for line in open(path):
        f = line.split()
        if len(f) in (3, 4):
            t, cmd = int(f[0]), len(f) == 4 and f[3] == 'c'
            k = k + 1 if last == (t, cmd) else 0
            last = (t, cmd)
            out.append((t, int(f[1], 16), int(f[2], 16), cmd, k))
    return out


def is_setup(reg):
    """A register AdLib_PlayTune writes and the timer interrupt never does."""
    return 0xC0 <= reg <= 0xC8 or 0x40 <= reg <= 0x55


def initial_dump_len(dro):
    """The register dump DOSBox-X writes when a capture starts: writes at 0 ms in ascending order."""
    i, prev = 0, -1
    while i < len(dro) and dro[i][0] == 0 and (dro[i][1] & 0xFF) > prev:
        prev = dro[i][1] & 0xFF
        i += 1
    return i


def first_setup(stream, start=0):
    for i in range(start, len(stream)):
        if is_setup(stream[i][1]):
            j = i
            while j > start and stream[j - 1][1] in (0xBD, 0x08) and stream[j - 1][0] >= stream[i][0] - 1:
                j -= 1
            return j
    return None


def filtered_writes(eng, cache, limit=None):
    """The engine's writes DOSBox-X would log, from a register cache (changed in place)."""
    out = []
    for w in eng:
        r, v = w[1], w[2]
        if cache.get(r, 0) == v:
            continue
        cache[r] = v
        out.append(w)
        if limit and len(out) == limit:
            break
    return out


def start_dump(dro, eng, say, max_list):
    nd = initial_dump_len(dro)
    di0 = nd
    while di0 < len(dro) and dro[di0][0] <= 1:
        di0 += 1
    base = {}
    for _, r, v in dro[:di0]:
        base[r] = v
    probe = [w[1:] for w in dro[di0:di0 + 60]]
    img = {}
    i = 0
    while i < len(eng):
        t0 = eng[i][0]
        cache = dict(base)
        for r in KEY_REGS:  # the dump masked the key bits: the engine's registers decide them
            if r in img and (img[r] & ~0x20) == (cache.get(r, 0) & ~0x20):
                cache[r] = img[r]
        got = filtered_writes(eng[i:], dict(cache), len(probe))
        if [g[1:3] for g in got] == probe:
            diff = [r for r in sorted(set(img) | set(base))
                    if ((img.get(r, 0) & ~0x20) != (base.get(r, 0) & ~0x20) if r in KEY_REGS else img.get(r, 0) != base.get(r, 0))]
            say(f'the capture continues engine tick {t0}; the engine\'s registers there against the capture\'s: '
                f'{len(set(img) | set(base)) - len(diff)} equal, {len(diff)} differ' +
                (': ' + ' '.join('%02x %02x/%02x' % (r, img.get(r, 0), base.get(r, 0)) for r in diff[:max_list]) if diff else ''))
            return cache, eng[i:], dro[di0:]
        while i < len(eng) and eng[i][0] == t0:
            img[eng[i][1]] = eng[i][2]
            i += 1
    raise SystemExit('the capture does not continue any engine tick (another state or schedule?)')


def start_tune(dro, eng):
    di0 = first_setup(dro, initial_dump_len(dro))
    ei0 = first_setup(eng)
    if di0 is None or ei0 is None:
        raise SystemExit('no AdLib_PlayTune burst in one of the streams')
    cache = {}
    for _, r, v in dro[:di0]:
        cache[r] = v
    return cache, eng[ei0:], dro[di0:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('engine')
    ap.add_argument('dro')
    ap.add_argument('--out')
    ap.add_argument('--max-list', type=int, default=20)
    ap.add_argument('--start', choices=('dump', 'tune'), default='dump')
    a = ap.parse_args()

    dro = parse_dro(a.dro)
    eng = parse_engine(a.engine)
    lines = []

    def say(s):
        lines.append(s)

    cache, eng_part, ref = start_dump(dro, eng, say, a.max_list) if a.start == 'dump' else start_tune(dro, eng)
    filtered = filtered_writes(eng_part, cache)
    n = min(len(filtered), len(ref))
    first_bad = next((i for i in range(n) if filtered[i][1:3] != ref[i][1:]), None)
    same = n if first_bad is None else first_bad
    ok = first_bad is None and len(filtered) == len(ref)
    say(f'engine writes {len(eng)} ({len(eng_part)} compared, {len(filtered)} after the DOSBox-X filter); '
        f'.dro writes {len(dro)} ({len(ref)} compared; start: {a.start})')
    say(f'identical in order: {same} of {len(ref)} .dro writes' +
        ('' if ok else f'; first difference at write {same}: engine {filtered[same][:3] if same < len(filtered) else None}, '
                       f'.dro {ref[same] if same < len(ref) else None}'))

    # time base on the timer-interrupt writes, each at its place in its tick (70 us per write)
    isr = [(ref[i][0] - filtered[i][0] * TICK_MS - filtered[i][4] * WRITE_MS, i) for i in range(same) if not filtered[i][3]]
    cmd = [i for i in range(same) if filtered[i][3]]
    if not isr:
        raise SystemExit('no note writes to fit the time base on')
    a0 = statistics.median(x for x, _ in isr)
    dev = [((x - a0) / TICK_MS, i) for x, i in isr]
    off = [d for d, _ in dev]
    say(f'time base: .dro ms = {a0:.2f} + tick * {TICK_MS:.4f} + {WRITE_MS} * (writes before it in the tick); '
        f'{len(isr)} logged timer-interrupt writes')
    say(f'  against the engine: min {min(off):+.3f}, max {max(off):+.3f} ticks; within -0.25..+0.25 tick: '
        f'{sum(1 for d in off if abs(d) <= 0.25)}, within 0.5: {sum(1 for d in off if abs(d) <= 0.5)}')
    for d, i in [x for x in dev if abs(x[0]) > 0.5][:a.max_list]:
        say(f'    tick {filtered[i][0]} write {filtered[i][4]} ({filtered[i][1]:02x}={filtered[i][2]:02x}): {d:+.3f}')
    # the same with the time base fitted per segment between two tune starts (a capture made in turbo
    # mode can let its ms clock drift against the emulated PIT by a few ms)
    cuts = sorted({filtered[i][0] for i in cmd if is_setup(filtered[i][1])}) + [float('inf')]
    seg_dev, lo = [], -1
    for hi in cuts:
        part_dev = [x for x, i in isr if lo <= filtered[i][0] < hi]
        if part_dev:
            m = statistics.median(part_dev)
            seg_dev += [(x - m) / TICK_MS for x in part_dev]
        lo = hi
    say(f'  per segment between tune starts: min {min(seg_dev):+.3f}, max {max(seg_dev):+.3f} ticks; within 0.25: '
        f'{sum(1 for d in seg_dev if abs(d) <= 0.25)} of {len(seg_dev)}')
    if cmd:
        cd = [(ref[i][0] - a0 - filtered[i][0] * TICK_MS) / TICK_MS for i in cmd if filtered[i][4] < 8]
        say(f'  commands (first writes): {len(cd)} logged, {min(cd):+.2f} .. {max(cd):+.2f} ticks from the tick they run before')
    # the tune starts: the set-up and the first note after it
    starts = sorted({filtered[i][0] for i in cmd if is_setup(filtered[i][1])})
    for t in starts:
        ms = [ref[i][0] for i in cmd if filtered[i][0] == t]
        nxt = min((filtered[i][0] for _, i in isr if filtered[i][0] >= t), default=None)
        first = next(i for _, i in isr if filtered[i][0] == nxt) if nxt is not None else None
        say(f'  Music_Play before engine tick {t}: set-up .dro {(min(ms) - a0) / TICK_MS - t:+.2f} .. '
            f'{(max(ms) - a0) / TICK_MS - t:+.2f} ticks; first note at engine tick {nxt} '
            f'(+{nxt - t}), .dro {(ref[first][0] - a0 - filtered[first][4] * WRITE_MS) / TICK_MS - nxt:+.2f}'
            if nxt is not None else f'  Music_Play before engine tick {t}: no note after it')
    text = '\n'.join(lines) + '\n'
    if a.out:
        open(a.out, 'w').write(text)
    print(text, end='')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
