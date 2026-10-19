"""Audio comparison of the engine's sound with DOSBox-X recordings (the PCM track of an AVI, or a WAV):
pitch features per 4 PIT ticks (Goertzel magnitudes of the 61 semitones C2..C7, normalised), their
best alignment, the alignment tracked segment by segment, and the sound on/off edges per PIT tick.
Engine sound: a headless run with --frame-dump DIR writes DIR/sound.wav (from the run's first PIT
tick); `tmport-cli --dos-sound DEVICE tune N OUT.wav` renders one entry.

  python -P tools/dos/audiocmp.py align A B [--from S] [--len S] [--lo W] [--hi W]
        best lag in windows (B window = A window + lag) for A[from, from+len]
  python -P tools/dos/audiocmp.py track A B LAG0 [--seg S] [--range W]
        per segment of A: the similarity at LAG0 and the best lag within LAG0 +- range
  python -P tools/dos/audiocmp.py edges A B OFFSET FROM_S TO_S
        sound on/off edges of A (mapped to B's PIT ticks: B = A seconds x PIT + OFFSET) and of B

A, B: .avi (DOSBox-X, 48 kHz stereo 16-bit '01wb' chunks) or 16-bit .wav. Features are cached in
$TM_AUDIOCMP_CACHE (default extracted/scratch/dos_w4/audiocmp_cache). Used in wave 4 for
avi/tandy_attract_full_cycle.avi and avi/tandy_r3_session.avi (re/dos/docs/fmt_sound.md §11.6).
"""
import array
import hashlib
import json
import math
import os
import struct
import sys
import wave

PIT = 1193181.67 / 5041  # PIT ticks per second
WIN = 4                  # PIT ticks per feature window
REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CACHE = os.environ.get('TM_AUDIOCMP_CACHE') or os.path.join(REPO, 'extracted', 'scratch', 'dos_w4', 'audiocmp_cache')


def load(path):
    """Mono samples and the sample rate."""
    if path.lower().endswith('.avi'):
        data = open(path, 'rb').read()
        chunks = []

        def walk(off, end):
            while off + 8 <= end:
                cid = data[off:off + 4]
                size = struct.unpack_from('<I', data, off + 4)[0]
                body = off + 8
                if cid in (b'RIFF', b'LIST'):
                    walk(body + 4, min(body + size, end))
                elif cid == b'01wb':
                    chunks.append(data[body:body + size])
                off = body + size + (size & 1)

        walk(12, len(data))
        a = array.array('h', b''.join(chunks))
        return [(a[i] + a[i + 1]) * 0.5 for i in range(0, len(a) - 1, 2)], 48000
    w = wave.open(path, 'rb')
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    if w.getsampwidth() != 2:
        raise SystemExit('%s: 16-bit WAV expected' % path)
    a = array.array('h', w.readframes(n))
    return (list(a) if ch == 1 else [sum(a[i:i + ch]) / ch for i in range(0, len(a), ch)]), rate


def envelope(x, rate):
    """Mean absolute deviation per PIT tick."""
    spt = rate / PIT
    out = []
    for k in range(int(len(x) / spt)):
        w = x[int(k * spt):int((k + 1) * spt)]
        if not w:
            break
        m = sum(w) / len(w)
        out.append(sum(abs(v - m) for v in w) / len(w))
    return out


def features(path):
    st = os.stat(path)
    key = hashlib.md5(('%s|%d|%d' % (os.path.abspath(path), st.st_size, st.st_mtime_ns)).encode()).hexdigest()
    os.makedirs(CACHE, exist_ok=True)
    cf = os.path.join(CACHE, key + '.json')
    if os.path.exists(cf):
        with open(cf) as f:
            return json.load(f)
    x, rate = load(path)
    dec = max(1, int(rate // 12000))
    y = [sum(x[i:i + dec]) / dec for i in range(0, len(x) - dec + 1, dec)]
    r = rate / dec
    spw = r * WIN / PIT
    coefs = [2 * math.cos(2 * math.pi * 440.0 * 2 ** ((n - 69) / 12.0) / r) for n in range(36, 97)]
    out = []
    for k in range(int(len(y) / spw)):
        w = y[int(k * spw):int((k + 1) * spw)]
        m = sum(w) / len(w)
        w = [v - m for v in w]
        col = []
        for c in coefs:
            s1 = s2 = 0.0
            for v in w:
                s0 = v + c * s1 - s2
                s2 = s1
                s1 = s0
            col.append(math.sqrt(max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)))
        nrm = math.sqrt(sum(v * v for v in col)) or 1.0
        out.append([v / nrm for v in col])
    with open(cf, 'w') as f:
        json.dump(out, f)
    return out


def mean_sim(A, B, start, length, lag):
    s, n = 0.0, 0
    for k in range(start, min(start + length, len(A))):
        j = k + lag
        if 0 <= j < len(B):
            s += sum(x * y for x, y in zip(A[k], B[j]))
            n += 1
    return s / n if n else -1.0


def edges(env, thr, k0, k1):
    out, on = [], env[k0] > thr if k0 < len(env) else False
    for k in range(k0 + 1, min(k1, len(env))):
        now = env[k] > thr
        if now != on:
            out.append((k, 'on' if now else 'off'))
            on = now
    return out


def main():
    a = sys.argv[1:]
    if not a or a[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    cmd = a.pop(0)
    opts, pos = {}, []
    while a:
        t = a.pop(0)
        if t.startswith('--'):
            opts[t[2:]] = float(a.pop(0))
        else:
            pos.append(t)
    wps = PIT / WIN
    if cmd == 'edges':
        rx, rr = load(pos[0])
        bx, br = load(pos[1])
        off, t0, t1 = int(pos[2]), float(pos[3]), float(pos[4])
        renv, benv = envelope(rx, rr), envelope(bx, br)
        k0, k1 = int(t0 * PIT), int(t1 * PIT)
        print('A edges (B PIT ticks):', ' '.join('%d:%s' % (k + off, e) for k, e in edges(renv, max(renv) * 0.03, k0, k1)))
        print('B edges              :', ' '.join('%d:%s' % x for x in edges(benv, max(benv) * 0.03, k0 + off, k1 + off)))
        return 0
    A, B = features(pos[0]), features(pos[1])
    print('A %d windows (%.1f s), B %d windows (%.1f s)' % (len(A), len(A) / wps, len(B), len(B) / wps))
    if cmd == 'align':
        start, length = int(opts.get('from', 0) * wps), int(opts.get('len', 8) * wps)
        lo, hi = int(opts.get('lo', -len(A))), int(opts.get('hi', len(B)))
        res = sorted(((mean_sim(A, B, start, length, lag), lag) for lag in range(lo, hi + 1)), reverse=True)
        for s, lag in res[:5]:
            print('lag %d windows (%.3f s, %d PIT ticks): similarity %.3f' % (lag, lag / wps, lag * WIN, s))
    elif cmd == 'track':
        lag0, seg, rng = int(pos[2]), int(opts.get('seg', 4) * wps), int(opts.get('range', 20))
        for start in range(0, len(A) - seg, seg):
            best = max((mean_sim(A, B, start, seg, lag), lag) for lag in range(lag0 - rng, lag0 + rng + 1))
            print('A %6.1f s: similarity %.3f at lag0; best lag %+d (%.3f)' % (start / wps, mean_sim(A, B, start, seg, lag0),
                                                                             best[1] - lag0, best[0]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
