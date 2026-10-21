"""DOS edition (SST.EXE, Treasure Mountain! 1990): text decoders.

Covers PRG_DICT, HELP.DAT and the text kept in SST.EXE's data segment (DGROUP).
Also provides the shared EXE/DGROUP loader used by the other tools/dos scripts.
See re/dos/docs/fmt_data.md for the formats.

Usage (run with python -P):
  python -P tools/dos/dos_text.py dict  [--dos DIR] [--out FILE]   PRG_DICT word list
  python -P tools/dos/dos_text.py help  [--dos DIR] [--out FILE]   HELP.DAT pages
  python -P tools/dos/dos_text.py exe   [--out FILE]               DGROUP string map
  python -P tools/dos/dos_text.py all   [--outdir DIR]             all of the above

Output defaults to extracted/previews/dos/data/.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
DEFAULT_DOS = os.path.join(ROOT, 'TM-DOS')
DEFAULT_UNPACKED = os.path.join(ROOT, 'extracted', 'SST_unpacked.EXE')
DEFAULT_OUT = os.path.join(ROOT, 'extracted', 'previews', 'dos', 'data')


# ---------------------------------------------------------------------------
# EXE image / DGROUP loader (shared by puzzles.py, users_dos.py, dos_sound.py)
# ---------------------------------------------------------------------------

class Image:
    """The unpacked SST.EXE load image.

    Addresses follow the Ghidra export (re/dos/ghidra_export): Ghidra loads the
    image at segment 0x1000, so a Ghidra address SSSS:OOOO is load-image offset
    (SSSS - 0x1000) * 16 + OOOO.  Far pointers stored in the data are *not*
    relocated in the file: a stored segment S means image offset S * 16.
    """

    def __init__(self, image: bytes, entry_cs: int, entry_ip: int):
        self.image = image
        self.entry = entry_cs * 16 + entry_ip
        # Turbo C startup begins with MOV DX,DGROUP (BA lo hi) at the entry point.
        if image[self.entry] != 0xBA:
            raise ValueError('entry point does not start with MOV DX,imm16')
        self.dgroup_para = struct.unpack_from('<H', image, self.entry + 1)[0]
        self.dgroup = self.dgroup_para * 16          # image offset of DS:0000
        if image[self.dgroup + 4:self.dgroup + 11] != b'Turbo-C':
            raise ValueError('DS:0004 does not hold the Turbo C copyright string')

    # -- addressing helpers --
    def ghidra(self, seg: int, off: int) -> int:
        """Image offset of a Ghidra address SSSS:OOOO."""
        return (seg - 0x1000) * 16 + off

    def ds(self, off: int, n: int) -> bytes:
        return self.image[self.dgroup + off:self.dgroup + off + n]

    def ds_u8(self, off):
        return self.image[self.dgroup + off]

    def ds_u16(self, off):
        return struct.unpack_from('<H', self.image, self.dgroup + off)[0]

    def ds_cstr(self, off, maxlen=4096) -> bytes:
        p = self.dgroup + off
        e = self.image.index(b'\0', p, p + maxlen)
        return self.image[p:e]

    def far(self, seg: int, off: int, n: int) -> bytes:
        """Read through a stored (unrelocated) far pointer."""
        p = seg * 16 + off
        return self.image[p:p + n]

    @property
    def dgroup_init_size(self):
        return len(self.image) - self.dgroup


def load_image(path=None, dos_dir=None) -> Image:
    """Load the unpacked EXE (extracted/SST_unpacked.EXE), or unpack SST.EXE in memory."""
    path = path or DEFAULT_UNPACKED
    if os.path.exists(path):
        data = open(path, 'rb').read()
    else:
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import mischief_engine  # noqa: E402
        mischief_engine.add_tools_path()
        import unexepack  # noqa: E402  (the engine's tools/unexepack.py)
        packed = open(os.path.join(dos_dir or DEFAULT_DOS, 'SST.EXE'), 'rb').read()
        data, _, _ = unexepack.unpack(packed)
    (sig, last, pages, nrel, hdr_paras, _mn, _mx, ss, sp, _cs, ip, cs) = \
        struct.unpack_from('<2s11H', data, 0)
    assert sig == b'MZ'
    size = (pages - 1) * 512 + (last or 512)
    img = data[hdr_paras * 16:size]
    return Image(img, cs, ip)


def dos_file(name, dos_dir=None) -> bytes:
    return open(os.path.join(dos_dir or DEFAULT_DOS, name), 'rb').read()


# ---------------------------------------------------------------------------
# Text markup (renderer 43cb:0b63)
# ---------------------------------------------------------------------------
#   0x00            end of string
#   0x0D / 0x0A     new line: x = left margin, y += line height (DS:5b3c, 10 or 12)
#   '~' XXX YYY     move: x = XXX / 2 (2-pixel units) becomes the left margin, y = YYY
#   '`'             toggle highlight colour (0x4444) / normal colour (DS:9215)
#   '|' NN          colour NN (decimal), stored as nibble pair NN*0x11
#   0x09 NN         x += NN (decimal, 2-pixel units)
#   other           glyph (char & 0x7F); drawn only while x < 0x9A
# Bytes 0x01-0x1F other than TAB/LF/CR are key-symbol glyphs of the game font.

def markup(b: bytes) -> str:
    """Readable rendering of a DOS game string: CR -> {CR}, LF -> {LF}, TAB NN -> {TAB NN},
    other control/high bytes -> {xx}.  '~XXXYYY', '`' and '|NN' stay as they are."""
    out = []
    i = 0
    while i < len(b):
        c = b[i]
        if c == 0x0D:
            out.append('{CR}')
        elif c == 0x0A:
            out.append('{LF}')
        elif c == 0x09:
            out.append('{TAB %s}' % b[i + 1:i + 3].decode('latin1'))
            i += 2
        elif c < 0x20 or c >= 0x7F:
            out.append('{%02x}' % c)
        else:
            out.append(chr(c))
        i += 1
    return ''.join(out)


# ---------------------------------------------------------------------------
# PRG_DICT
# ---------------------------------------------------------------------------

def decode_dict(pd: bytes):
    return [pd[i:i + 16].split(b'\0')[0].decode('latin1') for i in range(0, len(pd), 16)]


def dump_dict(dos_dir=None):
    pd = dos_file('PRG_DICT', dos_dir)
    words = decode_dict(pd)
    used = [w for w in words if w]
    lines = ['PRG_DICT: %d bytes = %d entries x 16 bytes (NUL-padded); %d non-empty.'
             % (len(pd), len(words), len(used)),
             'Puzzle byte 0x80+n stands for entry n (46ce:08f1 uses (b & 0x7F) << 4, '
             'so only entries 0..127 are reachable).',
             '']
    for i, w in enumerate(words):
        if w:
            lines.append('%3d  0x%02x  %s' % (i, 0x80 + i, w))
    junk = [i for i in range(len(words)) if any(pd[i * 16 + len(words[i]):i * 16 + 16])]
    lines.append('')
    lines.append('entries with non-zero bytes after the terminator: %s' % (junk or 'none'))
    return '\n'.join(lines) + '\n'


# ---------------------------------------------------------------------------
# HELP.DAT
# ---------------------------------------------------------------------------

HELP_TOPICS = 4  # DS:52ee holds 4 far pointers to the topic titles


def decode_help(data: bytes):
    counts = list(data[:HELP_TOPICS])
    npages = sum(counts)
    offs = list(struct.unpack_from('<%dH' % npages, data, HELP_TOPICS))
    pages = []
    k = 0
    for t, n in enumerate(counts):
        for p in range(n):
            s = offs[k]
            e = data.index(b'\0', s)
            pages.append({'topic': t, 'page': p, 'index': k, 'offset': s, 'length': e - s,
                          'text': data[s:e]})
            k += 1
    return counts, offs, pages


def dump_help(dos_dir=None, img=None):
    data = dos_file('HELP.DAT', dos_dir)
    counts, offs, pages = decode_help(data)
    titles = []
    if img is not None:
        for t in range(HELP_TOPICS):
            o = img.ds_u16(0x52EE + 4 * t)
            titles.append(img.ds_cstr(o).decode('latin1'))
    lines = ['HELP.DAT: %d bytes; pages per topic %s; %d page offsets at 0x%x; text from 0x%x'
             % (len(data), counts, len(offs), HELP_TOPICS, offs[0]), '']
    for p in pages:
        title = titles[p['topic']] if titles else 'topic %d' % p['topic']
        lines.append('== topic %d (%s) page %d  @%#06x  %d bytes'
                     % (p['topic'], title, p['page'], p['offset'], p['length']))
        for ln in markup(p['text']).split('{CR}'):
            lines.append('   ' + ln)
        lines.append('')
    # per-page illustration tables (43cb:10c4): 5 parallel arrays of 15 x u16
    if img is not None:
        lines.append('Per-page illustration tables (DS:5bed, 5c0b, 5c29, 5c47, 5c65; s16 x 15, -1 = none):')
        for k in range(len(offs)):
            vals = [struct.unpack('<h', img.ds(base + 2 * k, 2))[0]
                    for base in (0x5BED, 0x5C0B, 0x5C29, 0x5C47, 0x5C65)]
            lines.append('   page %2d: %s' % (k, vals))
    return '\n'.join(lines) + '\n'


# ---------------------------------------------------------------------------
# SST.EXE DGROUP text and tables
# ---------------------------------------------------------------------------

FILE_TABLE = 0x0476     # 18 x 22-byte records: name[13], disk(u8), 8 bytes runtime
FILE_REC = 22
N_FILES = 18
HINT_TABLE = 0x7CA3     # 37 far pointers: Flutter balloon texts (49a6:0565)
N_HINTS = 37
WORD_TABLE = 0x5EB2     # 53 far pointers: numbers, digits, shapes, objects, plurals, comparatives
N_WORDS = 53
HELP_TITLES = 0x52EE    # 4 far pointers
RANK_NAMES = 0x625D     # 8 x 8-byte names (47c7:01e0); thresholds u16[8] at 0x629d
RANK_THRESH = 0x629D

WORD_GROUPS = [(0, 9, 'number words'), (9, 15, 'digits'), (15, 19, 'shapes'),
               (19, 29, 'objects (singular)'), (29, 34, 'objects only used in the plural'),
               (34, 35, 'empty string'), (35, 45, 'objects (plural)'), (45, 49, 'size adjectives'),
               (49, 53, 'superlatives')]


def scan_strings(img, start=0, end=None, minlen=3):
    """NUL-terminated runs of bytes 0x01..0x7E (CR/LF/TAB and glyph codes allowed)
    with at least 2 letters."""
    d = img.image[img.dgroup:]
    end = end or img.dgroup_init_size
    out = []
    i = start
    while i < end:
        j = i
        while j < end and 0 < d[j] < 0x7F:
            j += 1
        if j < end and d[j] == 0 and j - i >= minlen and sum(1 for c in d[i:j] if chr(c).isalpha()) >= 2:
            s = i
            # a preceding far pointer to DGROUP ends in bytes 94 3D ('=' is its high byte)
            if s > 0 and d[s - 1] == (img.dgroup_para & 0xFF) and d[s] == (img.dgroup_para >> 8):
                s += 1
            out.append((s, d[s:j]))
            i = j + 1
        else:
            i = j + 1 if j > i else i + 1
    return out


def dump_exe(img):
    L = ['SST.EXE DGROUP: image offset %#x (paragraph %#06x = Ghidra segment %04x); %#x bytes initialised'
         % (img.dgroup, img.dgroup_para, img.dgroup_para + 0x1000, img.dgroup_init_size), '']
    L.append('== File table DS:%04x (%d x %d bytes: name[13], disk, 8 runtime bytes)'
             % (FILE_TABLE, N_FILES, FILE_REC))
    for k in range(N_FILES):
        o = FILE_TABLE + k * FILE_REC
        rec = img.ds(o, FILE_REC)
        L.append('  %2d  %04x  %-13s disk %d  rest %s'
                 % (k, o, rec[:13].split(b'\0')[0].decode(), rec[13], rec[14:].hex()))
    L.append('')
    L.append('== Help topic titles DS:%04x' % HELP_TITLES)
    for t in range(HELP_TOPICS):
        o = img.ds_u16(HELP_TITLES + 4 * t)
        L.append('  %d  %04x  %s' % (t, o, markup(img.ds_cstr(o))))
    L.append('')
    L.append('== Word table DS:%04x (%d far pointers)' % (WORD_TABLE, N_WORDS))
    for a, b, name in WORD_GROUPS:
        ws = [img.ds_cstr(img.ds_u16(WORD_TABLE + 4 * k)).decode() for k in range(a, b)]
        L.append('  %2d..%2d %-36s %s' % (a, b - 1, name, ', '.join(ws)))
    L.append('')
    L.append('== Rank names DS:%04x (8-byte slots) and thresholds DS:%04x' % (RANK_NAMES, RANK_THRESH))
    for r in range(8):
        th = img.ds_u16(RANK_THRESH + 2 * (r - 1)) if r else 0
        L.append('  %d  %-10s total treasures >= %d' % (r, markup(img.ds(RANK_NAMES + 8 * r, 8).split(b'\0')[0]), th))
    L.append('')
    L.append('== Flutter hint balloons DS:%04x (%d far pointers; drawn at x=0x14,y=0x22 unless ~XXXYYY)'
             % (HINT_TABLE, N_HINTS))
    for k in range(N_HINTS):
        o = img.ds_u16(HINT_TABLE + 4 * k)
        L.append('  %2d  %04x  %s' % (k, o, markup(img.ds_cstr(o))))
    L.append('')
    L.append('== All NUL-terminated strings in DGROUP (offset, length, text)')
    for o, s in scan_strings(img):
        L.append('  %04x %4d  %s' % (o, len(s), markup(s)))
    return '\n'.join(L) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['dict', 'help', 'exe', 'all'])
    ap.add_argument('--dos', default=None)
    ap.add_argument('--exe', default=None, help='unpacked SST.EXE (default extracted/SST_unpacked.EXE)')
    ap.add_argument('--out', default=None)
    ap.add_argument('--outdir', default=DEFAULT_OUT)
    a = ap.parse_args()
    img = load_image(a.exe, a.dos)
    jobs = {'dict': ('prg_dict.txt', lambda: dump_dict(a.dos)),
            'help': ('help_dat.txt', lambda: dump_help(a.dos, img)),
            'exe': ('dgroup_text.txt', lambda: dump_exe(img))}
    todo = list(jobs) if a.cmd == 'all' else [a.cmd]
    os.makedirs(a.outdir, exist_ok=True)
    for k in todo:
        name, fn = jobs[k]
        text = fn()
        out = a.out if (a.out and a.cmd != 'all') else os.path.join(a.outdir, name)
        with open(out, 'w', encoding='latin1') as f:
            f.write(text)
        print('%s -> %s (%d lines)' % (k, out, text.count('\n')))


if __name__ == '__main__':
    main()
