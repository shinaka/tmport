"""Gizmos & Gadgets (DOS v1.0): PLAYERS.DAT (sign-in roster and saved players) dump.

Spec: re/gg/docs/fmt_data.md §8. Code (Ghidra SSSS:OOOO): overlay 2b5a (open 2b5a:004c checks
the version word, read slot 2b5a:0198, save 2b5a:01ea, blank record 2b5a:0000), sign-in
overlay 2caa (create 2caa:004f, roster insert/remove 2caa:01c5).

    file = header (8) + index (100 x 22 = 2200; entry 99 is written only when the file is
           created, with uninitialised bytes, and never read) = 0x8A0 bytes,
           then 99 player records of 0x16BD (5821) bytes at 0x8A0 + slot * 0x16BD
    header: 'USR' 0x1A, u16 version (12; any other value: the file is created anew),
            u16 player count
    index entry (22 bytes) = a copy of the first 22 bytes of a record:
            char name[16], u32 points, u16 slot; entries 0..count-1 are the players in name
            order, the rest are free slots in the order they will be taken. The creation loop
            rewrites entry 0 for each of the 99 blank records, so a new file lists slot 98 twice
            and slot 0 never (2caa:010d, 2b5a:02bc; code_ui.md §12.3)
    record: the memory image DS:4562..5C1E (offsets in RECORD_FIELDS below), with three
            per-building blocks of 0x6BA (1722) bytes at +0x27E

CLI (python -P tools/gg/players.py ...)::

    players.py [FILE]                header, index summary and one line per record
    players.py [FILE] --record N     the known fields of record N and its non-zero bytes
    players.py [FILE] --index        all 100 index entries
FILE defaults to PLAYERS.DAT in the data folder; the file is only read.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ggarc  # noqa: E402

MAGIC = b'USR\x1a'
VERSION = 12
HEADER = 8
ENTRY = 22
ENTRIES = 100
INDEX_END = HEADER + ENTRIES * ENTRY       # 0x8A0
RECORD = 0x16BD
RECORDS = 99
FILE_SIZE = INDEX_END + RECORDS * RECORD   # 578 487
BLOCK_OFF, BLOCK_SIZE, BUILDINGS = 0x27E, 0x6BA, 3
RECORD_DS = 0x4562                         # the record's place in DGROUP

# (offset, size, name, note) - DS address = 0x4562 + offset. [O] = seen in code, [I] = inferred.
RECORD_FIELDS = [
    (0x000, 16, 'name', 'NUL-terminated, up to 15 characters, first letter upper case [O 2caa]'),
    (0x010, 4, 'points', 'u32 score, DS:4572 [O]'),
    (0x014, 2, 'slot', 'u16 own slot number, DS:4576; the save seeks to 0x8A0 + slot * 0x16BD [O 2b5a:01ea]'),
    (0x016, 2, 'chimps', 'DS:4578: "With/Without Chimps" choice of a new player [O 2caa]'),
    (0x01A, 20, 'live_state', 'DS:457c..458f: blueprint seen, bananas DS:457e, tube room DS:4580, parts '
                              'needed DS:4582, installed, bonus given, paint colour, hint cancels '
                              'DS:458a/458c, set-up coin; copied to block +0 on save [O 2b5a:01ea]'),
    (0x02E, 2, 'race_index', 'DS:4590 = building * 5 + level [O 12e0:0820]'),
    (0x030, 2, 'building', 'DS:4592: current building, -1 none (new record: 0xFFFF) [O 2b5a:0000]'),
    (0x032, 2, 'level', 'DS:4594 [O 12e0:0820]'),
    (0x034, 6, 'races_won', '3 x s16 DS:4596: races won per building, -1..4 (new record: -1) [O]'),
    (0x03A, 6, 'in_progress', '3 x u16 DS:459c: building has a saved block [O 2b5a:01ea]'),
    (0x040, 2, 'hints_off', 'DS:45a2 [O]'),
    (0x042, 0x26, 'hints_seen', 'u16 per hint, DS:45a4 + 2 * (id - 0x1043), hints 0x1043..0x1055: New '
                                'Player Hint shown (set also when hints are off) [O 3060:013b]'),
    (0x068, 2, 'unused_68', 'never referenced [O]'),
    (0x06A, 2, 'sound_off', 'DS:45cc; 0 in a new record (the copy of DS:1ad4 is overwritten) [O 2b5a:0000]'),
    (0x06C, 2, 'unused_6c', 'never referenced [O]'),
    (0x06E, 0x210, 'puzzle_flags', '8 types x 33 u16, DS:45d0 + type * 0x42 + variant * 2 [O 38e2]'),
    (0x27E, 3 * 0x6BA, 'buildings', '3 x 0x6BA: Automotive, Aircraft, Alternative Energy (§8.4)'),
    (0x16AC, 2, 'races_total', 'DS:5c0e: races won in all; 15 = the end [O 12e0:0564]'),
    (0x16AE, 15, 'vehicle_colors', 'DS:5c10: vehicle colour per race index (races, hub trophies) [O 2dee:11fa]'),
]
# Per-building block (0x6BA bytes, DS:47e0 + building * 0x6BA), filled by 2b5a:01ea:
BLOCK_FIELDS = [
    (0x000, 0x14, 'live_state', 'copy of record +0x1A (DS:457c) [O]'),
    (0x014, 0x258, 'objects', 'written by 153f:0385 (parts / objects) [O call, I size]'),
    (0x26C, 0x96, 'rooms_a', 'written by 1622:0fb3 (first pointer) [O call, I size]'),
    (0x302, 0x190, 'rooms_b', 'written by 1622:0fb3 (second pointer) [O call, I size]'),
    (0x492, 0x228, 'actors', 'written by 1834:0b5e (actor slots) [O call, I size]'),
]


class PlayersError(ValueError):
    pass


def load(path):
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != MAGIC:
        raise PlayersError('%s: no USR 0x1A header' % path)
    return d


def header(d):
    ver, count = struct.unpack_from('<HH', d, 4)
    return ver, count


def entry(d, i):
    e = d[HEADER + i * ENTRY:HEADER + (i + 1) * ENTRY]
    name = e[:16].split(b'\0', 1)[0]
    pts, slot = struct.unpack_from('<IH', e, 16)
    return name, pts, slot, e


def record(d, k):
    o = INDEX_END + k * RECORD
    return d[o:o + RECORD]


def show_name(b):
    return b.decode('latin-1') if b else '-'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ggarc.add_data_arg(ap)
    ap.add_argument('file', nargs='?')
    ap.add_argument('--record', type=int)
    ap.add_argument('--index', action='store_true')
    a = ap.parse_args(argv)
    try:
        path = a.file or os.path.join(ggarc.data_dir(a.data), 'PLAYERS.DAT')
        d = load(path)
        ver, count = header(d)
        print('%s: %d bytes (expected %d), version %d%s, %d players'
              % (path, len(d), FILE_SIZE, ver, '' if ver == VERSION else ' (the game recreates the file)',
                 count))
        if len(d) != FILE_SIZE:
            print('note: size differs from header + index + 99 records')
        slots = [entry(d, i)[2] for i in range(RECORDS)]
        missing = sorted(set(range(RECORDS)) - set(slots))
        dup = sorted(s for s in set(slots) if slots.count(s) > 1)
        print('index: %s; slots missing from the index %s, '
              'twice %s; entry 99 (unused) bytes %s'
              % ('entries 0..%d players, %d..98 free slots' % (count - 1, count) if count else 'no players, entries 0..98 free slots',
                 missing or '-', dup or '-', entry(d, 99)[3].hex(' ')))
        if a.index:
            print('entry\tname\tpoints\tslot')
            for i in range(ENTRIES):
                name, pts, slot, _ = entry(d, i)
                print('%d\t%s\t%d\t%d' % (i, show_name(name), pts, slot))
            return 0
        if a.record is not None:
            if not 0 <= a.record < RECORDS:
                raise PlayersError('records are 0..98')
            r = record(d, a.record)
            print('record %d at file offset 0x%x' % (a.record, INDEX_END + a.record * RECORD))
            for off, size, name, note in RECORD_FIELDS:
                v = r[off:off + size]
                shown = (str(struct.unpack('<h', v)[0]) if size == 2 else
                         str(struct.unpack('<I', v)[0]) if size == 4 and name == 'points' else
                         repr(v.split(b'\0', 1)[0].decode('latin-1')) if name == 'name' else
                         ('%d non-zero bytes' % sum(1 for b in v if b)) if size > 8 else v.hex(' '))
                print('  +0x%04x %5d  %-15s %-24s DS:%04x  %s' % (off, size, name, shown,
                                                                  RECORD_DS + off, note))
            for b in range(BUILDINGS):
                base = BLOCK_OFF + b * BLOCK_SIZE
                nz = sum(1 for x in r[base:base + BLOCK_SIZE] if x)
                print('  building %d block at +0x%04x: %d non-zero bytes' % (b, base, nz))
            nz = [i for i, x in enumerate(r) if x]
            print('  non-zero offsets: %s' % (' '.join('%x' % i for i in nz[:64]) + (' ...' if len(nz) > 64 else '')))
            return 0
        print('rec\tslot_field\tname\tpoints\tbuilding\traces_won\ttotal\tnonzero_bytes')
        for k in range(RECORDS):
            r = record(d, k)
            name = r[:16].split(b'\0', 1)[0]
            pts, slot = struct.unpack_from('<IH', r, 16)
            bld = struct.unpack_from('<h', r, 0x30)[0]
            won = struct.unpack_from('<3h', r, 0x34)
            tot = struct.unpack_from('<h', r, 0x16AC)[0]
            print('%d\t%d\t%s\t%d\t%d\t%s\t%d\t%d' % (k, slot, show_name(name), pts, bld,
                                                     ','.join(map(str, won)), tot,
                                                     sum(1 for x in r if x)))
    except (OSError, PlayersError, ggarc.ArchiveError) as e:
        print('players: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
