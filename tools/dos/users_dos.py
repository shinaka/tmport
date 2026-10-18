"""DOS edition (SST.EXE, 1990): USERS.DAT (sign-in roster + saved games) decoder.

Layout (re/dos/docs/fmt_data.md section 4): 3 header bytes, then 9 player records of
0x168 bytes at file offsets 3 + 0x168 * i (DGROUP table 0x6132).  The whole file
(0xCAB bytes) is read into DS:ba14 by 47c7:0514 and written back by 47c7:055a.
Field offsets come from the pointer set-up at 47c7:09dd, the load code 47c7:0b19 /
47c7:0c62 and the save code 47c7:0e0e.

Usage (python -P):
  python -P tools/dos/users_dos.py [USERS.DAT] [--out TXT] [--json JSON]
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dos_text import load_image, ROOT, DEFAULT_OUT  # noqa: E402

FILE_SIZE = 0xCAB
HEADER = 3
REC = 0x168
N_REC = 9

# (offset, size, name, DGROUP variable / pointer, note)
FIELDS = [
    (0x000, 9, 'name', '-', 'NUL-terminated, empty = free slot (roster test 47c7:0452)'),
    (0x009, 1, 'saved_game', '-', '0 = no saved game (start new), else continue (47c7:08b5); cleared at game end 47c7:09a2'),
    (0x00A, 2, 'treasures_total', 'ptr 9138', 'sort key and rank (thresholds DS:629d); < 5 turns New Player Hints on (47c7:0b19)'),
    (0x00C, 90, 'puzzle_history', 'ptr 8fd2', '2 bits per puzzle: 0 unseen, 1 right first try, 2 right after a miss, 3 missed'),
    (0x066, 18, 'dealt_puzzles', 'ptr 921a', '9 x u16 puzzle index, 0xFFFF none, bit 15 = solved this game'),
    (0x078, 9, 'choice_masks', 'ptr 9107', 'per dealt puzzle: bit n = answer n still offered (7 = all)'),
    (0x081, 9, 'clue_words', 'ptr 91fd', '3 levels x 3 clue words (number, size, object): word-table index, bit 7 set until earned (46ce:06f0 clears it)'),
    (0x08A, 48, 'tbl_8a', 'ptr 927e', 'set to 0x80 x 48 for a new player (47c7:0a8b)'),
    (0x0BA, 1, 'pad_ba', '-', 'not referenced'),
    (0x0BB, 57, 'spot_kind', 'DS:0caa[]', '48 + 9 bytes copied to/from word arrays via index tables DS:11aa / DS:120a'),
    (0x0F4, 48, 'spot_content', 'DS:0faa[]', '48 bytes copied to/from word array via index table DS:11aa'),
    (0x124, 12, 'words_124', 'DS:1225[]', '6 x u16, element indices from DS:62ab {1,2,5,6,9,10}'),
    (0x130, 3, 'tbl_130', 'ptr 9209', 'zeroed for a new game (47c7:0bd6)'),
    (0x133, 12, 'tbl_133', 'ptr 8fb6', 'zeroed for a new player (47c7:0a8b)'),
    (0x13F, 1, 'puzzle_level', 'ptr 9101', 'index into DS:5fd2 {0,63,180,303,360,..}: 0..3, then 5'),
    (0x140, 2, 'w140', '9130', ''),
    (0x142, 2, 'w142', '928f', 'current mountain level 0..2 (clue word row)'),
    (0x144, 2, 'w144', '9134', 'new player: 5'),
    (0x146, 2, 'w146', '910f', ''),
    (0x148, 2, 'w148', '9113', ''),
    (0x14A, 2, 'w14a', '9120', 'new game: 8'),
    (0x14C, 2, 'w14c', '90f8', 'new game: 4'),
    (0x14E, 2, 'w14e', '37e2', 'new player: 0x140'),
    (0x150, 2, 'w150', '37e4', 'new player: 0x70'),
    (0x152, 2, 'w152', '37ea', 'new player: 0xc0'),
    (0x154, 2, 'w154', '37ec', 'new player: 0'),
    (0x156, 2, 'room', '910b', 'scene/room number; music: <4 tune 0, <7 tune 1, <15 tune 2'),
    (0x158, 2, 'w158', '92a1', 'new game: 4'),
    (0x15A, 2, 'w15a', '910f', 'new player: 10; also written at game end'),
    (0x15C, 2, 'w15c', '9134', 'new player: 5; also written at game end'),
    (0x15E, 2, 'music_on', '63b6', ''),
    (0x160, 2, 'sound_on', '63b4', ''),
    (0x162, 2, 'games_played', '926e', '+1 at game end (47c7:09a2)'),
    (0x164, 2, 'w164', '8fb0', 'new player: 0xFFFF'),
    (0x166, 2, 'w166', '8f9e', 'only the low byte is loaded'),
]


def history(rec):
    h = rec[0x0C:0x0C + 90]
    return [(h[i >> 2] >> ((i & 3) * 2)) & 3 for i in range(360)]


def decode(data: bytes):
    assert len(data) >= HEADER + N_REC * REC, 'USERS.DAT too short'
    out = {'size': len(data), 'header': list(data[:HEADER]), 'records': []}
    for r in range(N_REC):
        base = HEADER + r * REC
        rec = data[base:base + REC]
        f = {'slot': r, 'file_offset': base}
        for off, size, name, var, note in FIELDS:
            raw = rec[off:off + size]
            if name == 'name':
                f[name] = raw.split(b'\0')[0].decode('latin1')
            elif size == 1:
                f[name] = raw[0]
            elif size == 2:
                f[name] = struct.unpack('<H', raw)[0]
            elif name == 'dealt_puzzles':
                f[name] = list(struct.unpack('<9H', raw))
            elif name == 'words_124':
                f[name] = list(struct.unpack('<6H', raw))
            else:
                f[name] = raw.hex()
        hist = history(rec)
        f['puzzle_history_counts'] = [hist.count(s) for s in range(4)]
        f['puzzle_history_nonzero'] = {i: s for i, s in enumerate(hist) if s}
        out['records'].append(f)
    return out


def rank_name(img, total):
    th = [img.ds_u16(0x629D + 2 * i) for i in range(7)]
    r = sum(1 for t in th if t <= total)
    return r, img.ds(0x625D + 8 * r, 8).split(b'\0')[0].decode('latin1').replace('\x1a', '{crown glyph 1a}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('file', nargs='?', default=os.path.join(ROOT, 'TM-DOS', 'USERS.DAT'))
    ap.add_argument('--exe', default=None)
    ap.add_argument('--out', default=os.path.join(DEFAULT_OUT, 'users_dat.txt'))
    ap.add_argument('--json', default=os.path.join(DEFAULT_OUT, 'users_dat.json'))
    a = ap.parse_args()
    data = open(a.file, 'rb').read()
    img = load_image(a.exe)
    d = decode(data)
    L = ['USERS.DAT: %d bytes (expected %d = 3 + 9 x 0x168); header bytes %s'
         % (len(data), FILE_SIZE, ' '.join('%02x' % b for b in d['header'])), '']
    for f in d['records']:
        if not f['name']:
            empty = not any(data[f['file_offset']:f['file_offset'] + REC])
            L.append('slot %d @%#05x: free%s' % (f['slot'], f['file_offset'], ' (all zero)' if empty else ' (stale data)'))
            continue
        r, rn = rank_name(img, f['treasures_total'])
        L.append('slot %d @%#05x: %r  treasures %d -> rank %d (%s)' % (f['slot'], f['file_offset'], f['name'],
                                                                     f['treasures_total'], r, rn))
        for off, size, name, var, note in FIELDS[1:]:
            v = f[name]
            if isinstance(v, str) and len(v) > 48:
                v = v[:48] + '...'
            L.append('   +%03x %-16s %-10s %-30s %s' % (off, name, var, v, note))
        L.append('   puzzle history counts (unseen, right, right-after-miss, missed): %s'
                 % f['puzzle_history_counts'])
        L.append('')
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, 'w', encoding='latin1') as fo:
        fo.write('\n'.join(L) + '\n')
    with open(a.json, 'w', encoding='utf-8') as fo:
        json.dump(d, fo, indent=1)
    print('\n'.join(L[:60]))


if __name__ == '__main__':
    main()
