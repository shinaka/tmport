"""Stages of the wave-3 playthroughs (play.py) on the layout of seed 0x1EE2 (the Round-1 EGA capture
climb). Each takes the Player and the tick to start at and returns the tick where it ended (and prints
what it reached). Cells are world cells (strip row << 4 | column, code_world.md)."""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from play import SCR, cell_of  # noqa: E402,F401

BOOT_TITLE = [(60, 'tap:enter'), (120, 'tap:enter'), (180, 'tap:enter'), (240, 'tap:enter')]
NEW_ADA = [(400, 'tap:a'), (420, 'tap:d'), (440, 'tap:a'), (460, 'tap:enter'), (600, 'tap:enter'),
           (700, 'down:up'), (760, 'up:up')]


def in_cell(c, lo=0x30, hi=0x50):
    return lambda r: cell_of(r) == c and lo <= (r['x'] & 0x7F) <= hi


def log(msg):
    print(msg, flush=True)


def wait_for(p, start, cond, ticks=400):
    tr = p.run(start + ticks)
    r = tr.first(cond, start)
    if r is None:
        raise SystemExit('wait from %d: condition not met; last %s' % (start, tr.last()))
    return r['tick'], tr


def search_here(p, t):
    """Down: toss a coin at the hiding place here; returns the tick the search ended and its sfx."""
    p.add(t + 14, 'tap:down')
    t2, tr = wait_for(p, t + 30, lambda r: r['state'] == 10, 300)
    sfx = [e[1] for e in tr.events(t, t2 + 1, 'sfx=')]
    hint = tr.first(lambda r: r['uimode'] == 0x10, t2)
    if hint and hint['tick'] <= t2 + 2:
        p.add(hint['tick'] + 30, 'tap:enter')   # a first-time hint: close it
        t2 = hint['tick'] + 40
    return t2, sfx


def level0_key_treasure(p, t):
    t, _ = p.hold_until('left', t, in_cell(0xE8))
    t, sfx = search_here(p, t)
    log('level 0: coin at E8 (key) done on tick %d %s' % (t, sfx))
    t, _ = p.hold_until('right', t + 10, in_cell(0xE9))
    t, sfx = search_here(p, t)
    log('level 0: coin at E9 (treasure) done on tick %d %s' % (t, sfx))
    return t


def level0_climb(p, t):
    t, _ = p.hold_until('right', t, in_cell(0xEF))
    log('level 0: at the trunk gate EF on tick %d' % t)
    t, _ = p.hold_until('up', t + 10, lambda r: r['level'] == 1 and cell_of(r) == 0xBF and (r['y'] & 0x7F) == 0x70,
                        max_ticks=1500)
    log('level 1: on top of the trunk (BF) on tick %d' % t)
    return t


def level1(p, t):
    for cell, what in ((0xB8, 'treasure'), (0xB5, 'key')):
        t, _ = p.hold_until('left', t + 10, in_cell(cell))
        t, sfx = search_here(p, t)
        log('level 1: coin at %02X (%s) done on tick %d %s' % (cell, what, t, sfx))
    t, _ = p.hold_until('left', t + 10, in_cell(0xAE))
    log('level 1: at the geyser gate AE on tick %d' % t)
    t, _ = p.hold_until('up', t + 10, lambda r: r['level'] == 2 and cell_of(r) == 0x7E and (r['y'] & 0x7F) == 0x70,
                        max_ticks=1500)
    log('level 2: on the geyser top (7E) on tick %d' % t)
    return t


def level2(p, t):
    for cell, what in ((0x82, 'key'), (0x85, 'treasure')):
        t, _ = p.hold_until('right', t + 10, in_cell(cell))
        t, sfx = search_here(p, t)
        log('level 2: coin at %02X (%s) done on tick %d %s' % (cell, what, t, sfx))
    t, _ = p.hold_until('right', t + 10, in_cell(0x79))
    log('level 2: at the stone arch 79 on tick %d' % t)
    t, _ = p.hold_until('up', t + 10, lambda r: r['area'] == 0x0D and cell_of(r) == 0x73, max_ticks=400)
    log('castle: in the hall (73) on tick %d' % t)
    return t


def castle(p, t):
    t, _ = wait_for(p, t, lambda r: r['state'] == 10, 60)
    t, _ = p.hold_until('right', t + 10, in_cell(0x74, 0x40, 0x48))
    t, _ = p.hold_until('up', t + 10, lambda r: cell_of(r) == 0x54, max_ticks=800, release=False)
    t, _ = wait_for(p, t, lambda r: (r['y'] & 0x78) == 0x60 and cell_of(r) == 0x54, 200)
    p.add(t + 20, 'up:up')
    log('castle: at the top of the short ladder 54 on tick %d' % t)
    p.add(t + 40, 'tap:pgup:4')   # the ladder jump to the right (55)
    t, _ = wait_for(p, t + 40, lambda r: r['state'] == 15 and cell_of(r) == 0x55, 300)
    log('castle: on the ladder 55 after the jump on tick %d' % t)
    t, _ = p.hold_until('up', t + 10, lambda r: r['level'] == 3, max_ticks=1000)
    log('end: out of the exit 35 onto the castle top (scene 3) on tick %d' % t)
    return t


def end_to_clubhouse(p, t):
    t0 = t
    t, tr = wait_for(p, t, lambda r: r['scene'] == 4 and cell_of(r) == 0x99, 2000)
    log('end: treasure room (scene 4) on tick %d (%d ticks after the castle top)' % (t, t - t0))
    t, tr = wait_for(p, t, lambda r: cell_of(r) == 0x1D and r['scene'] == 4, 600)
    log('end: rank screen (1D) on tick %d' % t)
    p.add(t + 300, 'tap:enter')
    t, tr = wait_for(p, t + 300, lambda r: r['uimode'] == 4, 300)
    saves = tr.events(t - 30, t + 1, 'save=')
    log('end: back in the clubhouse with the end-of-game message on tick %d %s' % (t, saves))
    p.add(t + 100, 'tap:enter')
    tr = p.run(t + 400)
    t = t + 400
    for tt, ev in tr.events(t - 300, t, 'ui=0x10'):
        p.add(tt + 60, 'tap:enter')   # the hint after the message
        t = tt + 200
    return t


def exit_and_save(p, t, copy_to=None, dumps=()):
    p.add(t, 'tap:esc')
    p.add(t + 60, 'tap:y')
    tr = p.run(t + 300, dumps=dumps)
    q = tr.first(lambda r: 'quit' in r['events'], t)
    log('exit and save: %s' % (tr.events(t, t + 300, 'save='),))
    if copy_to:
        shutil.copyfile(os.path.join(tr.dir, 'pref', 'dos_users.dat'), copy_to)
    return q['tick'] if q else None, tr


def quiz_catch_and_answer(p):
    """Level 0, standing at the clubhouse door: face right, catch the clue carrier elf 2 (tick 2400),
    answer the quiz with the third choice (the right one), close the praise: +1 coin for the catch,
    +2 for the answer, the clue word in the panel."""
    p.add(2200, 'tap:right')
    p.add(2400, 'tap:space')
    t, tr = wait_for(p, 2401, lambda r: r['uimode'] == 2, 200)
    log('quiz: the quiz screen opens on tick %d (coins %d nets %d)' % (t, tr.at(t)['coins'], tr.at(t)['nets']))
    p.add(t + 76, 'tap:down')
    p.add(t + 96, 'tap:down')
    p.add(t + 156, 'tap:enter')
    p.add(t + 256, 'tap:enter')
    t2, tr = wait_for(p, t + 257, lambda r: r['uimode'] == 0, 200)
    r = tr.at(t2 + 60) or tr.last()
    log('quiz: answered, back in play on tick %d (coins %d after the count-up) %s' % (
        t2, r['coins'], [e for e in tr.events(t + 150, t2 + 1) if 'sfx' in e[1]][:3]))
    return t2, t


def net_shop(p, t):
    """Level 0: from the clubhouse door left to the net shop EA, 4 coins, Up into the cave (+5 nets),
    Down out again."""
    t, _ = p.hold_until('left', t, in_cell(0xEA))
    for k in range(4):
        p.add(t + 14, 'tap:down')
        t, tr = wait_for(p, t + 20, lambda r: r['state'] == 10, 300)
        t += 5
    r = tr.at(t - 5)
    log('net shop: 4 coins paid on tick %d (coins %d nets %d)' % (t, r['coins'], r['nets']))
    t, tr = p.hold_until('up', t + 10, lambda r: r['nets'] > 9 + 0 and r['nets'] >= 14, max_ticks=400)
    log('net shop: climbed into the cave, nets %d on tick %d' % (tr.at(t)['nets'], t))
    t, tr = p.hold_until('down', t + 10, lambda r: (r['y'] & 0x7F) == 0x70 and r['state'] == 10, max_ticks=400)
    log('net shop: back on the path on tick %d' % t)
    return t


def level0_from_shop(p, t):
    """Level 0 after the net shop (EA): the treasure in E9, the key in E8, then right to the gate."""
    for cell, what in ((0xE9, 'treasure'), (0xE8, 'key')):
        t, _ = p.hold_until('left', t + 10, in_cell(cell))
        t, sfx = search_here(p, t)
        log('level 0: coin at %02X (%s) done on tick %d %s' % (cell, what, t, sfx))
    return t
