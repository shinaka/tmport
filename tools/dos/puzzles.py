"""DOS edition (SST.EXE, 1990): ALL_PUZS puzzle decoder (uses PRG_DICT).

ALL_PUZS = 360 x u16 offsets (relative to the end of the table, 0x2D0) + 360
NUL-terminated puzzle strings.  Bytes 0x80..0xFF in a puzzle stand for word
(b & 0x7F) of PRG_DICT (16-byte NUL-padded entries); the game expands them with
46ce:08f1.  A puzzle is two pad bytes, the question, '~', then three answer
lines; answer 0 is the correct one.  Levels come from the u16 table at DGROUP
0x5fd2.  See re/dos/docs/fmt_data.md section 2.

Usage (python -P):
  python -P tools/dos/puzzles.py [--dos DIR] [--outdir DIR] [--compare-win JSON]
Writes puzzles.txt and puzzles.json to extracted/previews/dos/data/ by default.
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dos_text import load_image, dos_file, ROOT, DEFAULT_OUT, markup  # noqa: E402

N_PUZZLES = 360                  # 46ce:0d9a loops to 0x168; table length 0x2D0 (46ce:09ab)
TABLE_LEN = 2 * N_PUZZLES
DS_LEVEL_TABLE = 0x5FD2          # u16[7]: {0, 63, 180, 303, 360, <computed>, 360}
DS_DEAL_TEMPLATE = 0x5FE0        # 9 history states dealt per game
DS_PERM = 0x5E9C                 # 6 x 3 answer display orders
DS_POINTS = 0x5F93               # points per remaining-choice mask


def load_dict(pd: bytes):
    return [pd[i:i + 16].split(b'\0')[0].decode('latin1') for i in range(0, len(pd), 16)]


def expand(raw: bytes, words):
    """46ce:08f1: copy bytes < 0x80, replace b >= 0x80 by PRG_DICT word (b & 0x7F)."""
    out = bytearray()
    for b in raw:
        if b < 0x80:
            out.append(b)
        else:
            out += words[b & 0x7F].encode('latin1')
    return bytes(out)


def split_puzzle(text: bytes):
    """46ce:0323: question = bytes before '~'; answers = the next 3 CR/NUL-terminated lines."""
    t = text.find(b'~')
    q = text[:t] if t >= 0 else text
    rest = text[t + 1:] if t >= 0 else b''
    answers = rest.split(b'\r')[:3]
    return q, answers


def decode(dos_dir=None, img=None):
    data = dos_file('ALL_PUZS', dos_dir)
    words = load_dict(dos_file('PRG_DICT', dos_dir))
    offs = struct.unpack_from('<%dH' % N_PUZZLES, data, 0)
    levels = None
    if img is not None:
        levels = [img.ds_u16(DS_LEVEL_TABLE + 2 * i) for i in range(5)]
    else:
        levels = [0, 63, 180, 303, 360]
    puzzles = []
    for i in range(N_PUZZLES):
        s = TABLE_LEN + offs[i]
        e = data.index(b'\0', s)
        raw = data[s:e]
        exp = expand(raw, words)
        pad, body = exp[:2], exp[2:]
        q, answers = split_puzzle(body)
        lvl = next(k for k in range(4) if levels[k] <= i < levels[k + 1])
        puzzles.append({
            'index': i,
            'level': lvl,
            'offset': s,
            'raw_len': len(raw),
            'dict_refs': sum(1 for b in raw if b >= 0x80),
            'pad': pad.decode('latin1'),
            'trailing_ctrl_z': body.endswith(b'\x1a'),
            'question': markup(q.rstrip(b'\x1a')),
            'answers': [markup(a.rstrip(b'\x1a')) for a in answers],
            'correct': 0,
        })
    return data, words, offs, levels, puzzles


def plain(s):
    return ' '.join(s.replace('`', '').replace('{CR}', ' ').split())


def compare_win(puzzles, path):
    j = json.load(open(path, encoding='utf-8'))
    win = j['puzzlers']

    def key_dos(p):
        return (plain(p['question']).lower(), tuple(plain(a).lower() for a in p['answers']))

    def key_win(p):
        q = ' '.join(l['plain'] for l in p['lines'] if l['plain'].strip())
        return (' '.join(q.split()).lower(), tuple(' '.join(c['plain'].split()).lower() for c in p['choices']))

    wk = {}
    for p in win:
        wk.setdefault(key_win(p), []).append(p['index'])
    matched, unmatched = [], []
    for p in puzzles:
        k = key_dos(p)
        if k in wk:
            matched.append((p['index'], wk[k][0]))
        else:
            unmatched.append(p['index'])
    return matched, unmatched, len(win)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dos', default=None)
    ap.add_argument('--exe', default=None)
    ap.add_argument('--outdir', default=DEFAULT_OUT)
    ap.add_argument('--compare-win', default=os.path.join(ROOT, 'extracted', 'previews', 'othr_puzzles.json'))
    a = ap.parse_args()
    img = load_image(a.exe, a.dos)
    data, words, offs, levels, puzzles = decode(a.dos, img)
    os.makedirs(a.outdir, exist_ok=True)

    tmpl = list(img.ds(DS_DEAL_TEMPLATE, 9))
    perm = [list(img.ds(DS_PERM + 3 * i, 3)) for i in range(6)]
    points = list(img.ds(DS_POINTS, 8))
    lines = ['ALL_PUZS: %d bytes, %d puzzles, table 0x%x bytes; PRG_DICT: %d entries (%d used)'
             % (len(data), N_PUZZLES, TABLE_LEN, len(words), sum(1 for w in words if w)),
             'level ranges (DS:5fd2): %s' % levels,
             'deal template (DS:5fe0, history state per slot): %s' % tmpl,
             'answer display orders (DS:5e9c): %s' % perm,
             'points by remaining-choice mask (DS:5f93): %s' % points, '']
    for p in puzzles:
        lines.append('#%03d  level %d  @%#06x  %d bytes (%d dict refs)%s' % (
            p['index'], p['level'], p['offset'], p['raw_len'], p['dict_refs'],
            '  [ends with ^Z]' if p['trailing_ctrl_z'] else ''))
        for ql in p['question'].split('{CR}'):
            lines.append('    ' + ql)
        for k, an in enumerate(p['answers']):
            lines.append('    %s %s' % ('*' if k == 0 else '-', an))
        lines.append('')

    summary = {'n_puzzles': N_PUZZLES, 'levels': levels,
               'per_level': [levels[k + 1] - levels[k] for k in range(4)]}
    if a.compare_win and os.path.exists(a.compare_win):
        matched, unmatched, nwin = compare_win(puzzles, a.compare_win)
        summary['windows_compare'] = {'windows_count': nwin, 'matched': len(matched),
                                      'unmatched_dos_indices': unmatched,
                                      'index_shift': sorted(set(w - d for d, w in matched))}
        lines.insert(5, 'Windows OTHR 11000+ comparison: %d of %d DOS puzzles have an identical question+answers '
                     'in the %d Windows puzzlers; unmatched DOS: %s' % (len(matched), N_PUZZLES, nwin, unmatched))
    with open(os.path.join(a.outdir, 'puzzles.txt'), 'w', encoding='latin1') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.join(a.outdir, 'puzzles.json'), 'w', encoding='utf-8') as f:
        json.dump({'source': 'TM-DOS/ALL_PUZS + PRG_DICT', 'spec': 're/dos/docs/fmt_data.md',
                   'dictionary': words, 'summary': summary, 'puzzles': puzzles}, f, indent=1)
    print(json.dumps(summary, indent=1)[:2000])


if __name__ == '__main__':
    main()
