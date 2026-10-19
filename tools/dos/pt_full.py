"""Playthrough 1 (complete): a new player from the boot through every stage to Exit and Save, on the
layout of seed 0x1EE2 (the Round-1 EGA capture climb). Prints the script it built (the one of
tools/dos/scenarios/level1/pt_full_script.txt and tests/test_dos_playthrough.cpp).

  python -P tools/dos/pt_full.py        (TM_CLI = tmport-cli.exe; runs take a few minutes)"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from play import Player, SCR, cell_of  # noqa: E402
from stages import (BOOT_TITLE, NEW_ADA, castle, end_to_clubhouse, exit_and_save, in_cell, level0_climb,  # noqa: E402
                    level0_from_shop, level2, log, net_shop, quiz_catch_and_answer, search_here)

if __name__ == '__main__':
    stage = sys.argv[1] if len(sys.argv) > 1 else 'all'
    p = Player('pt_full')
    for t, s in BOOT_TITLE + NEW_ADA:
        p.add(t, s)
    t, tquiz = quiz_catch_and_answer(p)
    t = net_shop(p, t + 20)
    t = level0_from_shop(p, t)
    t = level0_climb(p, t + 10)
    # level 1: only the key (coins)
    t, _ = p.hold_until('left', t + 10, in_cell(0xB5))
    t, sfx = search_here(p, t)
    log('level 1: coin at B5 (key) done on tick %d %s' % (t, sfx))
    t, _ = p.hold_until('left', t + 10, in_cell(0xAE))
    t, _ = p.hold_until('up', t + 10, lambda r: r['level'] == 2 and cell_of(r) == 0x7E and (r['y'] & 0x7F) == 0x70,
                        max_ticks=1500)
    log('level 2: on the geyser top (7E) on tick %d' % t)
    t = level2(p, t)
    t = castle(p, t)
    t = end_to_clubhouse(p, t)
    q, tr = exit_and_save(p, t, os.path.join(SCR, 'pt_full_users_after_exit.dat'))
    log('quit on tick %s; script: %s' % (q, os.path.join(tr.dir, 'script.txt')))
