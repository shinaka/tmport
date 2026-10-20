"""Gizmos & Gadgets (DOS v1.0, 1993): MRn resource archives, method-3 decompressor, bundles
and resource typing. Shared module of the tools in tools/gg/.

Spec: re/gg/docs/fmt_data.md (archive §2, compression §3, bundles §4, resource types §5).
Addresses are Ghidra SSSS:OOOO of the G&G export (image segment + 0x1000):

    Res_OpenFiles   14ff:0167  opens gizmo000..003.dat read-only, keeps all four open, reads the
                               last entry of volumes 0..2 and prints "Incorrect Resource File n"
                               unless its payload is the word 0xA55A
    Res_Read(id)    14ff:000d  vol = id >> 12, index = id & 0xFFF; offsets at file 4 + 4*index;
                               u8 type, u8 method; method 0 = stored, else Lzh_Decompress
    Lzh_Decompress  1b48:02aa  u32 size (low 30 bits), MSB-first bit stream: window bit, 16
                               ignored bits, 286 literal/length code lengths, distance lengths
                               (default table DS:13D0 or explicit), canonical Huffman + LZ77

The archive layout (all little-endian)::

    +0      'M' 'R' <volume digit> 0x1A
    +4      u32 off[n + 1]          relative to file offset 4; off[0] = 4 * (n + 1);
                                    off[n] = file length - 4 (end sentinel)
    entry k = file[off[k] + 4 .. off[k + 1] + 4) = u8 type, u8 method, payload
    resource id = volume << 12 | k

Library::

    from ggarc import Archive, walk_leaves
    arc = Archive()                          # GG_DATA or <repo>/GadgetsAndGizmos
    e = arc.entry(0x1002)                    # Entry(rid, type, method, csize, payload)
    for leaf in walk_leaves(arc): ...        # bundles expanded one level

CLI (python -P tools/gg/ggarc.py ...)::

    selftest                     decode all 1897 entries and check them (one-line summary)
    list [--vol N] [--leaves]    directory listing (with --leaves: bundle members too)
    info ID                      one resource: type, sizes, compression details, members
    get ID OUT [--raw]           write a resource's payload (decoded) to OUT

Every tool takes --data DIR (default: $GG_DATA, else <repo>/GadgetsAndGizmos). The tools only
read the data folder; they refuse to write into it. No game data is embedded here.
"""
import argparse
import collections
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
DEFAULT_DATA = os.path.join(REPO, 'GadgetsAndGizmos')
DEFAULT_PREVIEWS = os.path.join(REPO, 'extracted', 'previews', 'gg')

VOLUMES = 4
VOLUME_NAME = 'gizmo%03d.dat'
INTEGRITY_WORD = 0xA55A          # payload of the last entry of each volume (type 'I', stored)

METHOD_STORED = 0
METHOD_LZH = 3                   # the only non-zero method in the data; the game treats any
                                 # non-zero method as compressed (14ff:000d tests == 0)

# --- compression constants (Lzh_Decompress 1b48:02aa) --------------------------------------
NUM_LITLEN = 0x11E               # 286 literal/length symbols (1b48:0268 loops to 0x11E)
SYM_LONG = 0x11D                 # 285: length 32 + next 8 bits
LEN_BIAS = 0xFD                  # length = symbol - 0xFD for 256..285 (3..32)
DIST_LOW_BITS = 6                # distance = code * 64 + 6 raw bits + 1
# Default distance code lengths, DS:13D0 + (window - 12) * 0x20 (1b48:021f): the first word is
# the starting length, each further word a symbol index where the length grows by one; the
# zero words that end each row never match (the loop starts at symbol 1 for them).
# These are the values in the shipped ssg.exe; selftest --exe re-reads them from the file.
DEFAULT_DIST_ROWS = {
    12: (2, 1, 2, 4, 7, 17, 34),
    13: (2, 1, 1, 3, 6, 13, 31, 78),
    14: (3, 1, 4, 12, 24, 48),   # third row exists in the table but a 1-bit flag cannot select it
}
EXE_DS_FILE_OFFSET = 0xFC70      # ssg.exe: DS:0000 (DGROUP 1ea7) is at this file offset
EXE_DS_DEFAULT_DIST = 0x13D0


class ArchiveError(ValueError):
    pass


# ============================================================================================
# Data folder
# ============================================================================================

def data_dir(arg=None):
    """The G&G folder: --data, else $GG_DATA, else <repo>/GadgetsAndGizmos."""
    d = arg or os.environ.get('GG_DATA') or DEFAULT_DATA
    d = os.path.abspath(d)
    if not os.path.isfile(os.path.join(d, VOLUME_NAME % 0)):
        raise ArchiveError('%s: no %s here (set --data or GG_DATA)' % (d, VOLUME_NAME % 0))
    return d


def add_data_arg(ap):
    ap.add_argument('--data', default=None,
                    help='G&G folder (default: $GG_DATA, else <repo>/GadgetsAndGizmos)')


def safe_out_dir(out, data):
    """Create OUT and return it; refuse any path inside the data folder."""
    o = os.path.normcase(os.path.abspath(out))
    d = os.path.normcase(os.path.abspath(data))
    if o == d or o.startswith(d + os.sep):
        raise ArchiveError('refusing to write into the game folder: %s' % out)
    os.makedirs(out, exist_ok=True)
    return out


def safe_out_file(path, data):
    safe_out_dir(os.path.dirname(os.path.abspath(path)) or '.', data)
    return path


def type_name(t):
    """Printable form of a type byte: the letter, or two hex digits (bundles are 01)."""
    return chr(t) if 0x21 <= t < 0x7F else '%02x' % t


def rid_str(rid):
    return '%04x' % rid


def parse_rid(s):
    """'0x1002', '1002' (hex) or a decimal with a leading '#'."""
    s = s.strip()
    if s.startswith('#'):
        return int(s[1:])
    return int(s, 16)


# ============================================================================================
# Method-3 decompressor
# ============================================================================================

class BitReader:
    """MSB-first bit reader over bytes (the game's bit buffer 87bc/87ba, refill 1b48:005d).
    Bytes past `end` read as zero; `overrun_bits()` tells how many such bits were consumed
    (bits only peeked at do not count: the game's refill also reads ahead)."""

    __slots__ = ('d', 'start', 'pos', 'end', 'acc', 'n')

    def __init__(self, data, pos, end):
        self.d = data
        self.start = pos
        self.pos = pos
        self.end = end
        self.acc = 0
        self.n = 0

    def _fill(self, need):
        while self.n < need:
            self.acc = (self.acc << 8) | (self.d[self.pos] if self.pos < self.end else 0)
            self.pos += 1
            self.n += 8

    def bits(self, k):
        if k == 0:
            return 0
        if self.n < k:
            self._fill(k)
        self.n -= k
        v = (self.acc >> self.n) & ((1 << k) - 1)
        self.acc &= (1 << self.n) - 1
        return v

    def peek(self, k):
        if self.n < k:
            self._fill(k)
        return (self.acc >> (self.n - k)) & ((1 << k) - 1)

    def skip(self, k):
        self.n -= k
        self.acc &= (1 << self.n) - 1

    def consumed_bits(self):
        return (self.pos - self.start) * 8 - self.n

    def overrun_bits(self):
        return max(0, self.consumed_bits() - (self.end - self.start) * 8)


class HuffmanTable:
    """Canonical code built like the game's recursive builder 1b48:0109: a depth-first walk
    that takes the 0 branch first and gives each leaf the next unused symbol of the smallest
    length still wanted (lengths 1, 2, ...; within a length by symbol index). For a complete
    code this is the usual canonical (Deflate-style) code. The builder also fills an 8-bit
    lookup table (codes up to 8 bits, and internal nodes at depth 8); longer codes are walked
    bit by bit. A code that is not complete would make the builder recurse without end; it is
    rejected here."""

    __slots__ = ('nsym', 'look_sym', 'look_len', 'child0', 'child1', 'max_len', 'kraft')

    def __init__(self, lengths):
        nsym = len(lengths)
        self.nsym = nsym
        self.max_len = max(lengths) if lengths else 0
        # completeness: sum of 2^-len over the used symbols must be exactly 1
        kraft = sum(1 << (24 - L) for L in lengths if L)
        self.kraft = kraft
        if kraft != 1 << 24:
            raise ArchiveError('Huffman code not complete (Kraft sum %d/%d)' % (kraft, 1 << 24))
        self.look_sym = [0] * 256
        self.look_len = [0] * 256
        self.child0 = {}
        self.child1 = {}
        nodes = {(0, 0): nsym}           # (depth, prefix) -> internal node id; root = nsym
        code, prev = 0, 0
        for L, s in sorted((L, s) for s, L in enumerate(lengths) if L):
            code <<= L - prev            # canonical: next code value, widened to length L
            prev = L
            parent = nsym
            for d in range(1, L + 1):    # walk the prefix bits, creating internal nodes
                pre = code >> (L - d)
                if d == L:
                    child = s
                else:
                    child = nodes.get((d, pre))
                    if child is None:
                        child = nodes[(d, pre)] = nsym + len(nodes)
                (self.child1 if pre & 1 else self.child0)[parent] = child
                parent = child
            if L <= 8:                   # 8-bit lookup table, as filled by 1b48:0109
                base = code << (8 - L)
                for i in range(1 << (8 - L)):
                    self.look_sym[base + i] = s
                    self.look_len[base + i] = L
            code += 1
        for (d, pre), node in nodes.items():
            if d == 8:                   # internal nodes at depth 8: entry with length 8
                self.look_sym[pre] = node
                self.look_len[pre] = 8

    def decode(self, br):
        i = br.peek(8)
        s = self.look_sym[i]
        br.skip(self.look_len[i])
        nsym = self.nsym
        while s >= nsym:
            s = self.child1[s] if br.bits(1) else self.child0[s]
        return s


def default_dist_lengths(window_bits, rows=None):
    """Expand a default distance-length row (1b48:021f) to one length per distance code."""
    row = (rows or DEFAULT_DIST_ROWS)[window_bits]
    n = 1 << (window_bits - DIST_LOW_BITS)
    cur, j, out = row[0], 1, []
    for s in range(n):
        while j < len(row) and row[j] == s:
            cur += 1
            j += 1
        out.append(cur)
    return out


class LzhInfo:
    __slots__ = ('size', 'size_hi_bits', 'window_bits', 'ignored16', 'explicit_dist',
                 'used_bytes', 'overrun_bits', 'literals', 'matches', 'max_dist',
                 'before_start', 'past_end')

    def __init__(self):
        self.literals = self.matches = self.max_dist = 0
        self.before_start = self.past_end = 0


def decompress(data, pos=0, end=None, dist_rows=None):
    """Decode a method-3 stream that starts at data[pos] with its u32 size word.
    Returns (bytes, LzhInfo). Raises ArchiveError on malformed codes."""
    if end is None:
        end = len(data)
    if end - pos < 4:
        raise ArchiveError('compressed payload shorter than its size word')
    lo, hi = struct.unpack_from('<HH', data, pos)
    info = LzhInfo()
    info.size = lo | ((hi & 0x3FFF) << 16)
    info.size_hi_bits = hi >> 14
    br = BitReader(data, pos + 4, end)
    info.window_bits = 12 + br.bits(1)
    ndist = 1 << (info.window_bits - DIST_LOW_BITS)
    info.ignored16 = br.bits(16)                      # read and dropped by the game
    lit_len = [(br.bits(4) + 1) if br.bits(1) else 0 for _ in range(NUM_LITLEN)]
    lit = HuffmanTable(lit_len)
    info.explicit_dist = br.bits(1)
    if info.explicit_dist:
        dist_len = [br.bits(4) for _ in range(ndist)]
    else:
        dist_len = default_dist_lengths(info.window_bits, dist_rows)
    dist = HuffmanTable(dist_len)
    out = bytearray()
    # The game counts the low 16 bits of the size (1b48:02f5) and stops when the count reaches
    # exactly 0; every resource is smaller than 64 KB (the load buffer is 0xFFFF bytes).
    remaining = info.size
    while remaining > 0:
        s = lit.decode(br)
        if s < 256:
            out.append(s)
            remaining -= 1
            info.literals += 1
            continue
        if s == SYM_LONG:
            s += br.bits(8)
        length = s - LEN_BIAS
        dc = dist.decode(br)
        d = dc * 64 + br.bits(DIST_LOW_BITS) + 1
        info.matches += 1
        if d > info.max_dist:
            info.max_dist = d
        src = len(out) - d
        if src < 0:
            info.before_start += 1
        if length > remaining:
            info.past_end += 1
        for i in range(length):                       # byte by byte: overlaps repeat
            out.append(out[src + i] if src + i >= 0 else 0)
        remaining -= length
    info.used_bytes = (br.consumed_bits() + 7) // 8 + 4
    info.overrun_bits = br.overrun_bits()
    return bytes(out), info


# ============================================================================================
# Volumes and archive
# ============================================================================================

Entry = collections.namedtuple('Entry', 'rid type method csize payload')


class Volume:
    def __init__(self, path, index):
        self.path = path
        self.index = index
        with open(path, 'rb') as f:
            self.d = f.read()
        d = self.d
        if len(d) < 12 or d[0:2] != b'MR' or d[3] != 0x1A:
            raise ArchiveError('%s: not an MRn archive (magic %r)' % (path, d[:4]))
        self.digit = d[2]
        first = struct.unpack_from('<I', d, 4)[0]
        if first % 4 or first < 8 or 4 + first > len(d):
            raise ArchiveError('%s: bad offset table size %d' % (path, first))
        n = first // 4
        self.offs = list(struct.unpack_from('<%dI' % n, d, 4))
        self.count = n - 1
        if self.offs[-1] != len(d) - 4:
            raise ArchiveError('%s: end sentinel 0x%x != file length - 4 (0x%x)'
                               % (path, self.offs[-1], len(d) - 4))
        for k in range(self.count):
            if not (first <= self.offs[k] <= self.offs[k + 1] - 2):
                raise ArchiveError('%s: entry %d offsets out of order' % (path, k))

    def raw(self, k):
        """The whole entry: u8 type, u8 method, payload (still compressed)."""
        return self.d[self.offs[k] + 4:self.offs[k + 1] + 4]

    def file_offset(self, k):
        return self.offs[k] + 4


class Archive:
    """The four volumes of a G&G folder (opened on first use)."""

    def __init__(self, data=None, dist_rows=None):
        self.data = data_dir(data)
        self._vols = {}
        self.dist_rows = dist_rows
        self._cache = {}

    def volume(self, v):
        if v not in self._vols:
            if not 0 <= v < VOLUMES:
                raise ArchiveError('no volume %d' % v)
            self._vols[v] = Volume(os.path.join(self.data, VOLUME_NAME % v), v)
        return self._vols[v]

    def ids(self):
        for v in range(VOLUMES):
            vol = self.volume(v)
            for k in range(vol.count):
                yield (v << 12) | k

    def exists(self, rid):
        v, k = rid >> 12, rid & 0xFFF
        return 0 <= v < VOLUMES and k < self.volume(v).count

    def raw(self, rid):
        v, k = rid >> 12, rid & 0xFFF
        vol = self.volume(v)
        if k >= vol.count:
            raise ArchiveError('resource %04x does not exist (volume %d has %d entries)'
                               % (rid, v, vol.count))
        return vol.raw(k)

    def entry(self, rid, with_info=False):
        """Decoded entry. with_info=True returns (Entry, LzhInfo or None)."""
        if rid in self._cache and not with_info:
            return self._cache[rid]
        r = self.raw(rid)
        t, m = r[0], r[1]
        info = None
        if m == METHOD_STORED:
            payload = r[2:]
        else:
            payload, info = decompress(r, 2, len(r), self.dist_rows)
        e = Entry(rid, t, m, len(r), payload)
        if len(self._cache) < 4096:
            self._cache[rid] = e
        return (e, info) if with_info else e

    def payload(self, rid):
        return self.entry(rid).payload


# ============================================================================================
# Bundles and resource typing
# ============================================================================================

Member = collections.namedtuple('Member', 'index type method payload')


def is_bundle_payload(p):
    if len(p) < 4:
        return False
    first = struct.unpack_from('<H', p, 0)[0]
    if first < 4 or first % 2 or first > len(p):
        return False
    n = first // 2
    offs = struct.unpack_from('<%dH' % n, p, 0)
    return offs[-1] == len(p) and all(offs[i] + 2 <= offs[i + 1] for i in range(n - 1))


def parse_bundle(p):
    """Type-0x01 payload: u16 off[n + 1] (off[0] = 2 * (n + 1), off[n] = payload length);
    member i = p[off[i] .. off[i + 1]) = u8 type, u8 method (always 0), payload."""
    if len(p) < 2:
        raise ArchiveError('bundle shorter than its table')
    first = struct.unpack_from('<H', p, 0)[0]
    if first < 4 or first % 2 or first > len(p):
        raise ArchiveError('bundle table size %d invalid' % first)
    n = first // 2
    offs = struct.unpack_from('<%dH' % n, p, 0)
    if offs[-1] != len(p):
        raise ArchiveError('bundle end %d != length %d' % (offs[-1], len(p)))
    out = []
    for i in range(n - 1):
        a, b = offs[i], offs[i + 1]
        if b < a + 2:
            raise ArchiveError('bundle member %d shorter than its header' % i)
        out.append(Member(i, p[a], p[a + 1], p[a + 2:b]))
    return out


MOVIE_FIRST, MOVIE_END = 0x3000, 0x30CA       # Movie_Play(0x3000, 0x30ca, ...) at 2fcb:06a5
FONT_IDS = (0x0009, 0x000A)                   # 2b16:021c
LAST_IDS = (0x036D, 0x11DE, 0x214E, 0x30CC)   # integrity entries (14ff:0167 checks 0..2)

# kind -> description; the letter is the type byte of the entry or member
KINDS = collections.OrderedDict([
    ('bundle', 'type 01: members inline (fmt_data.md §4)'),
    ('picture', "LPX 0x1A picture (types G and P; fmt_graphics.md §2)"),
    ('palette', "palette range: u16 DAC byte offset, u16 byte count, 6-bit RGB (type P)"),
    ('font', 'fnt 0x1A font (type F, ids 9 and 10)'),
    ('dam', 'DAM 0x1A animation script, 16-bit opcodes (type D)'),
    ('movie', 'final-movie delta frame (type D, 0x3000..0x30c9)'),
    ('level', 'level description / part catalogue, 142-byte part records (type D, 0x2f5..0x303)'),
    ('song', 'BMF 0x1A song or effect (type S)'),
    ('string', 'NUL-terminated short string (type S)'),
    ('struct', 'small binary record (type S, not a string)'),
    ('text', 'NUL-terminated text, CR line breaks (type T)'),
    ('hint', 'New Player Hint: FF 06 note FF, then text (type H)'),
    ('grid', 'puzzle grid / board data (type F, vol 2)'),
    ('integer', 'small integers (type I)'),
    ('integrity', 'end-of-volume word 0xA55A (type I, last entry)'),
    ('table', 'game table (types A, C, D, L, M, R, W; partly understood)'),
    ('unknown', 'none of the above'),
])


def _printable_string(p):
    if not p or p[-1] != 0:
        return False
    body = p[:-1]
    return len(body) > 0 and all(0x20 <= c < 0x7F or c in (0x0D, 0x0A) for c in body)


def classify(t, p, rid=None, member=None):
    """Kind of an entry (member=None) or bundle member (member = index) with type byte t."""
    c = chr(t) if 0x20 <= t < 0x7F else None
    if t == 0x01:
        return 'bundle'
    magic = p[:4]
    if magic == b'LPX\x1a':
        return 'picture'
    if magic == b'fnt\x1a':
        return 'font'
    if magic == b'DAM\x1a':
        return 'dam'
    if magic == b'BMF\x1a':
        return 'song'
    if c == 'P':
        if len(p) >= 4:
            off, cnt = struct.unpack_from('<HH', p, 0)
            if off + cnt <= 768 and len(p) >= 4 + cnt:
                return 'palette'
        return 'unknown'
    if c == 'D':
        if member is None and rid is not None and MOVIE_FIRST <= rid < MOVIE_END:
            return 'movie'
        if member is None and rid is not None and rid >> 12 == 0:
            return 'level'
        return 'table'                       # puzzle tables 0x11a1 (35bb:1c03), 0x211b (32fe:0aa0)
    if c == 'T':
        return 'text'
    if c == 'H':
        return 'hint'
    if c == 'S':
        return 'string' if _printable_string(p) else 'struct'
    if c == 'F':
        return 'grid'
    if c == 'I':
        if member is None and rid in LAST_IDS:
            return 'integrity'
        return 'integer'
    if c in ('A', 'C', 'L', 'M', 'R', 'W'):
        return 'table'
    return 'unknown'


Leaf = collections.namedtuple('Leaf', 'rid path type method payload kind')


def leaf_path(rid, member=None):
    return rid_str(rid) if member is None else '%s/%d' % (rid_str(rid), member)


def walk_leaves(arc, ids=None):
    """Every non-bundle resource and every bundle member (bundles nest one level only)."""
    for rid in (ids if ids is not None else arc.ids()):
        e = arc.entry(rid)
        if e.type == 0x01:
            for m in parse_bundle(e.payload):
                if m.type == 0x01:
                    raise ArchiveError('%04x/%d: nested bundle' % (rid, m.index))
                yield Leaf(rid, leaf_path(rid, m.index), m.type, m.method, m.payload,
                           classify(m.type, m.payload, rid, m.index))
        else:
            yield Leaf(rid, leaf_path(rid), e.type, e.method, e.payload,
                       classify(e.type, e.payload, rid))


def find_leaf(arc, path):
    """'1002' or '0001/3' -> Leaf."""
    if '/' in path:
        a, b = path.split('/', 1)
        rid, mi = parse_rid(a), int(b)
        e = arc.entry(rid)
        if e.type != 0x01:
            raise ArchiveError('%04x is not a bundle' % rid)
        mem = parse_bundle(e.payload)
        if not 0 <= mi < len(mem):
            raise ArchiveError('%04x has %d members' % (rid, len(mem)))
        m = mem[mi]
        return Leaf(rid, leaf_path(rid, mi), m.type, m.method, m.payload,
                    classify(m.type, m.payload, rid, mi))
    rid = parse_rid(path)
    e = arc.entry(rid)
    return Leaf(rid, leaf_path(rid), e.type, e.method, e.payload, classify(e.type, e.payload, rid))


# ============================================================================================
# Self-test
# ============================================================================================

def exe_dist_rows(exe_path):
    """Read the three default distance rows (DS:13D0, 3 x 16 words) from ssg.exe."""
    with open(exe_path, 'rb') as f:
        d = f.read()
    o = EXE_DS_FILE_OFFSET + EXE_DS_DEFAULT_DIST
    rows = {}
    for k in range(3):
        w = struct.unpack_from('<16H', d, o + 0x20 * k)
        n = 1
        while n < 16 and w[n] != 0:
            n += 1
        rows[12 + k] = tuple(w[:n])
    return rows


def selftest(arc, verbose=False, out=sys.stdout):
    """Check every entry of the four volumes. Returns the number of errors."""
    errors = 0
    n_ent = n_lzh = n_stored = 0
    unpacked = 0
    max_size = 0
    stats = collections.Counter()

    def err(msg):
        nonlocal errors
        errors += 1
        print('ERROR ' + msg, file=out)

    for v in range(VOLUMES):
        try:
            vol = arc.volume(v)
        except ArchiveError as e:
            err(str(e))
            continue
        if vol.digit != ord('0') + v:
            err('volume %d: magic digit %r' % (v, chr(vol.digit)))
        for k in range(vol.count):
            rid = (v << 12) | k
            r = vol.raw(k)
            t, m = r[0], r[1]
            n_ent += 1
            if m == METHOD_STORED:
                n_stored += 1
                size = len(r) - 2
            elif m == METHOD_LZH:
                n_lzh += 1
                try:
                    payload, info = decompress(r, 2, len(r), arc.dist_rows)
                except (ArchiveError, IndexError, KeyError) as e:
                    err('%04x: %s' % (rid, e))
                    continue
                size = len(payload)
                if size != info.size:
                    err('%04x: decoded %d bytes, header says %d' % (rid, size, info.size))
                if info.overrun_bits:
                    err('%04x: read %d bits past the entry' % (rid, info.overrun_bits))
                if info.used_bytes > len(r) - 2:
                    err('%04x: used %d of %d payload bytes' % (rid, info.used_bytes, len(r) - 2))
                if info.before_start:
                    err('%04x: %d matches reach before the start' % (rid, info.before_start))
                if info.past_end:
                    err('%04x: %d matches run past the end' % (rid, info.past_end))
                stats['window%d' % info.window_bits] += 1
                stats['dist_%s' % ('explicit' if info.explicit_dist else 'default')] += 1
                stats['size_hi_bits=%d' % info.size_hi_bits] += 1
                stats['slack_bytes=%d' % (len(r) - 2 - info.used_bytes)] += 1
                if info.ignored16 == (info.literals + info.matches) & 0xFFFF:
                    stats['token_count_word'] += 1
                if info.max_dist > (1 << info.window_bits):
                    err('%04x: distance %d > window' % (rid, info.max_dist))
            else:
                err('%04x: unknown method %d' % (rid, m))
                continue
            unpacked += size
            max_size = max(max_size, size)
            if size >= 0xFFFF:
                err('%04x: %d bytes do not fit the 0xFFFF-byte load buffer' % (rid, size))
            if verbose:
                print('%04x %s m%d %6d -> %6d' % (rid, type_name(t), m, len(r) - 2, size), file=out)
        last = vol.raw(vol.count - 1)
        if not (last[0] == ord('I') and last[1] == 0 and last[2:] == struct.pack('<H', INTEGRITY_WORD)):
            err('volume %d: last entry is not I/stored/0xA55A (%s)' % (v, last[:6].hex(' ')))
    if verbose:
        for k in sorted(stats):
            print('  %s: %d' % (k, stats[k]), file=out)
    print('ggarc selftest: %d volumes, %d entries (%d method 3, %d stored), %d bytes unpacked, '
          'largest %d; method 3: window13 %d, default distances %d, ignored word = token count '
          '%d; sizes, over-reads, match bounds, Huffman codes and the four 0xA55A ends checked; '
          '%d error%s'
          % (VOLUMES, n_ent, n_lzh, n_stored, unpacked, max_size, stats['window13'],
             stats['dist_default'], stats['token_count_word'], errors,
             '' if errors == 1 else 's'), file=out)
    return errors


# ============================================================================================
# CLI
# ============================================================================================

def _cmd_list(arc, a):
    vols = [a.vol] if a.vol is not None else range(VOLUMES)
    print('id\tvol\tindex\tfile_off\ttype\tmethod\tcsize\tusize\tkind')
    for v in vols:
        vol = arc.volume(v)
        for k in range(vol.count):
            rid = (v << 12) | k
            e = arc.entry(rid)
            print('%04x\t%d\t%d\t0x%06x\t%s\t%d\t%d\t%d\t%s'
                  % (rid, v, k, vol.file_offset(k), type_name(e.type), e.method, e.csize - 2,
                     len(e.payload), classify(e.type, e.payload, rid)))
            if a.leaves and e.type == 0x01:
                for m in parse_bundle(e.payload):
                    print('%04x/%d\t%d\t%d\t-\t%s\t%d\t-\t%d\t%s'
                          % (rid, m.index, v, k, type_name(m.type), m.method, len(m.payload),
                             classify(m.type, m.payload, rid, m.index)))


def _cmd_info(arc, a):
    rid = parse_rid(a.id)
    e, info = arc.entry(rid, with_info=True)
    vol = arc.volume(rid >> 12)
    print('resource %04x: volume %d entry %d at file offset 0x%x' % (rid, rid >> 12, rid & 0xFFF,
                                                                    vol.file_offset(rid & 0xFFF)))
    print('type %s method %d stored %d bytes, payload %d bytes, kind %s'
          % (type_name(e.type), e.method, e.csize - 2, len(e.payload),
             classify(e.type, e.payload, rid)))
    if info:
        print('method 3: size word high bits %d, window %d bits, ignored word 0x%04x, %s distance '
              'lengths, %d literals, %d matches, max distance %d, %d of %d bytes used'
              % (info.size_hi_bits, info.window_bits, info.ignored16,
                 'explicit' if info.explicit_dist else 'default', info.literals, info.matches,
                 info.max_dist, info.used_bytes, e.csize - 2))
    print('first bytes: %s' % e.payload[:32].hex(' '))
    if e.type == 0x01:
        for m in parse_bundle(e.payload):
            print('  member %2d: type %s method %d %6d bytes  %-8s %s'
                  % (m.index, type_name(m.type), m.method, len(m.payload),
                     classify(m.type, m.payload, rid, m.index), m.payload[:12].hex(' ')))


def _cmd_get(arc, a):
    rid = parse_rid(a.id)
    safe_out_file(a.out, arc.data)
    data = arc.raw(rid) if a.raw else arc.payload(rid)
    with open(a.out, 'wb') as f:
        f.write(data)
    print('%04x -> %s (%d bytes)' % (rid, a.out, len(data)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    add_data_arg(ap)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('selftest', help='decode and check all entries')
    s.add_argument('-v', '--verbose', action='store_true')
    s.add_argument('--exe', nargs='?', const='', default=None,
                   help='also read the default distance rows from ssg.exe (default: the data '
                        'folder\'s) and decode with them')
    s = sub.add_parser('list', help='directory listing')
    s.add_argument('--vol', type=int)
    s.add_argument('--leaves', action='store_true', help='also list bundle members')
    s = sub.add_parser('info', help='one resource')
    s.add_argument('id')
    s = sub.add_parser('get', help='write one payload to a file')
    s.add_argument('id')
    s.add_argument('out')
    s.add_argument('--raw', action='store_true', help='the entry as stored (type, method, payload)')
    a = ap.parse_args(argv)
    try:
        if a.cmd == 'selftest':
            rows = None
            if a.exe is not None:
                exe = a.exe or os.path.join(data_dir(a.data), 'ssg.exe')
                rows = exe_dist_rows(exe)
                same = all(rows[k] == DEFAULT_DIST_ROWS[k] for k in rows)
                print('default distance rows from %s: %s' % (exe, 'same as built in' if same
                                                             else 'DIFFERENT: %r' % rows))
            arc = Archive(a.data, dist_rows=rows)
            return 1 if selftest(arc, a.verbose) else 0
        arc = Archive(a.data)
        {'list': _cmd_list, 'info': _cmd_info, 'get': _cmd_get}[a.cmd](arc, a)
    except ArchiveError as e:
        print('ggarc: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
