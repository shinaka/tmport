"""Summarise the STEP log of a frame-logger state (framelog.py): print only the ticks where x, y, the
camera, the player state, g_keys or the coins change.

  python -P stepchanges.py STATE        e.g. r2/fl_walk2 (t = executed tick count from the first record)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import savmem  # noqa: E402
import framelog  # noqa: E402

KEYS = ('x', 'y', 'camx', 'camy', 'state', 'keys', 'coins')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    st = savmem.State(sys.argv[1])
    fr, steps, blk = framelog.rings(st)
    prev = None
    t0 = steps[0]['ticks'] if steps else 0
    for s in steps:
        if prev is None or any(s[k] != prev[k] for k in KEYS):
            ch = [] if prev is None else [f"{k}:{prev[k]:#x}->{s[k]:#x}" for k in KEYS if s[k] != prev[k]]
            print(f"t={((s['ticks'] - t0) & 0xffff) // 5:5d}  " + ' '.join(ch))
        prev = s


main()
