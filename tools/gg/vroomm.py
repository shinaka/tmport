"""Borland VROOMM overlay layout of ssg.exe (Gizmos & Gadgets DOS v1.0) and the flattened image for Ghidra.

ssg.exe is a Borland C++ 3.0 MZ program whose overlays (VROOMM, OVERLAY.LIB) live after the load module:

    MZ header | load module (resident code, overlay stubs, DGROUP, stack) | 'FBOV' header | overlay records

* The 'FBOV' header (16 bytes, right after the load module) holds: u32 overlay area size, u32 file offset of
  the segment table, i32 entry count. Overlay record offsets are relative to the end of this header.
* Segment table (8-byte entries, in the load module): u16 segment, u16 max offset, u16 flags, u16 min offset.
  flags 1 = code, 3 = code + overlay (an overlay's resident stub segment), 4 = data, 0 = other. Segments are
  packed byte-aligned, so a segment's code starts at its min offset.
* Stub segment: 0x20-byte header starting with `CD 3F` (INT 3Fh): +4 u32 record offset, +8 u16 code size,
  +0xA u16 fix-up bytes, +0xC u16 entry count; then one 5-byte entry `CD 3F <u16 offset> 00` per overlay
  function. Callers far-call stub:0020+5k; the INT 3Fh handler (the overlay manager) loads the overlay and
  jumps to the offset in the loaded copy.
* Overlay record: the code, then u16 fix-up offsets into it. Each fixed-up word holds a segment-table byte
  offset (index*8); the manager replaces it with that entry's segment (+ the load segment).

The flattened image is what tools/gg/ghidra_import.sh imports: a new MZ header covering the resident load
module only (the original relocations), then each non-empty overlay appended paragraph-aligned at its own
segment (image segment 1a80.., Ghidra 2a80..). The overlay bytes are written pre-relocated for Ghidra's load
segment 0x1000, because Ghidra's MZ loader does not relocate bytes past the load module:
  * every overlay fix-up word becomes its segment-table entry's segment + 0x1000;
  * every stub entry `CD 3F off 00` becomes `EA off ovlseg+0x1000` (JMP FAR), so a far call to a stub
    entry reaches the overlay function through a 5-byte thunk.
Stub headers stay as they are. The Ghidra numbering is therefore: resident image segment + 0x1000 (code
1000..1d42, stubs 1e0d..1e9f, DGROUP 1ea7, stack 2a78), overlays 2a80..4140 in overlay order.

CLI (ssg.exe from --exe, else --data DIR, else $GG_DATA, else <repo>/GadgetsAndGizmos; the game folder is only
read, and output paths inside it are refused)::

    python -P tools/gg/vroomm.py info [--data DIR | --exe ssg.exe]
    python -P tools/gg/vroomm.py flatten OUT.exe SETUP_DIR [--data DIR | --exe ssg.exe]
    python -P tools/gg/vroomm.py map [--data DIR | --exe ssg.exe] [--roles re/gg/docs/overlay_map.tsv] [--check]

`flatten` writes the flat image and the Ghidra set-up tables into SETUP_DIR (read by
tools/ghidra_scripts/SetupOverlays.java and DefineStrings.java):
    blocks.tsv    Ghidra segment, block name, kind (code/stub/dgroup/dgroup_part/data/stack), DS value or -
    entries.tsv   stub entry, overlay target, label (ovlNN_eKK)
    overlays.tsv  overlay block name, Ghidra segment, file offset in the flat image, size, DS value
    farcode.tsv   far code pointers stored in DGROUP (found through the relocation table): target, DS offset
    strings.tsv   NUL-terminated strings in DGROUP and the overlay manager's data (address, ssg.exe offset,
                  length, escaped text)
`map` prints the overlay map (re/gg/docs/overlay_map.tsv); --roles keeps the role column of an existing map,
--check compares the computed columns with that file and exits 1 on a difference.
"""
import argparse
import os
import struct
import sys

BASE = 0x1000           # Ghidra's MZ load segment
STUB_HDR = 0x20         # stub header size; entry k at stub:0020 + 5k
FBOV_LEN = 16
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class VroommError(ValueError):
    pass


def default_exe():
    root = os.environ.get('GG_DATA') or os.path.join(REPO, 'GadgetsAndGizmos')
    return os.path.join(root, 'ssg.exe')


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


class Seg:
    __slots__ = ('index', 'seg', 'maxoff', 'flags', 'minoff')

    def __init__(self, index, seg, maxoff, flags, minoff):
        self.index, self.seg, self.maxoff, self.flags, self.minoff = index, seg, maxoff, flags, minoff

    @property
    def is_code(self):
        return self.flags == 1 and self.maxoff > 0

    @property
    def is_stub(self):
        return self.flags == 3


class Overlay:
    """One overlay: its resident stub segment and its record in the overlay area."""
    __slots__ = ('n', 'stub', 'stub_size', 'rec_off', 'file_offset', 'code', 'fixups', 'entries', 'seg')

    def __init__(self, n, stub, stub_size, rec_off, file_offset, code, fixups, entries):
        self.n, self.stub, self.stub_size, self.rec_off = n, stub, stub_size, rec_off
        self.file_offset, self.code, self.fixups, self.entries = file_offset, code, fixups, entries
        self.seg = None  # image segment of the overlay code in the flat image (None when empty)


class VroommExe:
    """Parsed ssg.exe: MZ header, load module, relocations, segment table, overlays."""

    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            d = f.read()
        self.data = d
        if len(d) < 0x40 or d[:2] != b'MZ':
            raise VroommError('%s: not an MZ executable' % path)
        (cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs, lfarlc,
         ovno) = struct.unpack_from('<13H', d, 2)
        size = cp * 512 - ((512 - cblp) if cblp else 0)
        hdr = cparhdr * 16
        if not hdr < size <= len(d) or lfarlc + 4 * crlc > hdr:
            raise VroommError('%s: inconsistent MZ header' % path)
        self.mz = dict(cblp=cblp, cp=cp, crlc=crlc, cparhdr=cparhdr, minalloc=minalloc, maxalloc=maxalloc,
                       ss=ss, sp=sp, ip=ip, cs=cs, lfarlc=lfarlc, ovno=ovno)
        self.hdr_len = hdr
        self.load_end = size
        self.image = d[hdr:size]
        if len(self.image) % 16:
            raise VroommError('load module is not a whole number of paragraphs')
        self.relocs = [struct.unpack_from('<HH', d, lfarlc + 4 * i) for i in range(crlc)]
        fb = d[size:size + FBOV_LEN]
        if fb[:4] != b'FBOV':
            raise VroommError("no 'FBOV' overlay header after the load module (file 0x%x)" % size)
        self.ovr_size, self.segtab_off, nseg = struct.unpack_from('<IIi', fb, 4)
        self.ovr_base = size + FBOV_LEN
        if not hdr <= self.segtab_off < size or (self.segtab_off - hdr) % 16:
            raise VroommError('segment table outside the load module')
        self.segtab = [Seg(i, *struct.unpack_from('<4H', d, self.segtab_off + 8 * i)) for i in range(nseg)]
        self.overlays = []
        for s in self.segtab:
            if not s.is_stub:
                continue
            o = s.seg * 16
            if self.image[o:o + 2] != b'\xcd\x3f':
                raise VroommError('stub segment %04x does not start with INT 3Fh' % s.seg)
            rec, codesize, relsize, nent = struct.unpack_from('<IHHH', self.image, o + 4)
            entries = []
            for k in range(nent):
                eo = o + STUB_HDR + 5 * k
                if self.image[eo:eo + 2] != b'\xcd\x3f':
                    raise VroommError('stub %04x entry %d is not INT 3Fh' % (s.seg, k))
                entries.append(u16(self.image, eo + 2))
            fo = self.ovr_base + rec
            if fo + codesize + relsize > len(d):
                raise VroommError('overlay of stub %04x runs past the end of the file' % s.seg)
            fix = [u16(d, fo + codesize + 2 * j) for j in range(relsize // 2)]
            for v in fix:
                if v >= codesize - 1:
                    raise VroommError('stub %04x: fix-up offset %04x outside the code' % (s.seg, v))
            self.overlays.append(Overlay(len(self.overlays), s.seg, s.maxoff, rec, fo, d[fo:fo + codesize],
                                         fix, entries))
        self._find_groups()

    # -- well-known segments, derived from the bytes (asserted, not assumed) --
    def _find_groups(self):
        img = self.image
        relocated = {sg * 16 + off for off, sg in self.relocs}
        # DGROUP: Borland C0 starts with MOV DX,DGROUP (BA lo hi, relocated word)
        entry = self.mz['cs'] * 16 + self.mz['ip']
        if img[entry] != 0xBA or entry + 1 not in relocated:
            raise VroommError('entry point is not the Borland C0 MOV DX,DGROUP')
        self.dgroup = u16(img, entry + 1)
        dseg = [s for s in self.segtab if s.seg == self.dgroup]
        if not dseg:
            raise VroommError('DGROUP %04x not in the segment table' % self.dgroup)
        self.dgroup_size = max(s.maxoff for s in dseg)
        self.stack = self.mz['ss']
        # segments inside DGROUP (startup/exit tables, BSS end) that Ghidra's loader makes separate blocks of
        self.dgroup_parts = sorted({s.seg for s in self.segtab if self.dgroup < s.seg < self.stack})
        # overlay segment table segment
        self.segtab_seg = (self.segtab_off - self.hdr_len) // 16
        # overlay manager code: the code segment that starts with two relocated segment words, its own data
        # segment and DGROUP (OVERLAY.LIB: MOV DS,CS:[minoff] loads the first one)
        cands = []
        for s in self.segtab:
            if not s.is_code:
                continue
            a = s.seg * 16 + s.minoff
            if a in relocated and a + 2 in relocated and u16(img, a + 2) == self.dgroup:
                cands.append((s.seg, u16(img, a)))
        if len(cands) != 1:
            raise VroommError('overlay manager segment not found uniquely: %r' % cands)
        self.ovrman, self.ovrman_data = cands[0]

    def code_segments(self):
        return [s for s in self.segtab if s.is_code]

    def stub_overlay(self, stub_seg):
        for o in self.overlays:
            if o.stub == stub_seg:
                return o
        return None


def flatten(exe):
    """Return (flat image bytes, header length); sets Overlay.seg for every non-empty overlay."""
    img = bytearray(exe.image)
    resident_len = len(img)
    seg = len(img) // 16
    nseg = len(exe.segtab)
    for ov in exe.overlays:
        if not ov.code:
            ov.seg = None
            continue
        code = bytearray(ov.code)
        for fo in ov.fixups:
            v = u16(code, fo)
            if v % 8 or v // 8 >= nseg:
                raise VroommError('stub %04x: fix-up at %04x holds %04x, not a segment-table offset'
                                  % (ov.stub, fo, v))
            struct.pack_into('<H', code, fo, exe.segtab[v // 8].seg + BASE)
        ov.seg = seg
        img += code
        img += b'\0' * ((-len(img)) % 16)
        seg = len(img) // 16
        so = ov.stub * 16
        for k, eoff in enumerate(ov.entries):
            eo = so + STUB_HDR + 5 * k
            img[eo:eo + 5] = b'\xea' + struct.pack('<HH', eoff, ov.seg + BASE)
    nrel = len(exe.relocs)
    lfarlc = 0x2c  # keep the original 0x1c..0x2b bytes (TLINK signature) for reference
    hdr_len = lfarlc + 4 * nrel
    hdr_len += (-hdr_len) % 16
    if (hdr_len + resident_len) % 512 == 0:  # Ghidra reads e_cblp 0 as a 512-byte shorter image
        hdr_len += 16
    total = hdr_len + resident_len
    h = exe.mz
    hdr = bytearray(hdr_len)
    struct.pack_into('<2s13H', hdr, 0, b'MZ', total % 512, (total + 511) // 512, nrel, hdr_len // 16,
                     h['minalloc'], h['maxalloc'], h['ss'], h['sp'], 0, h['ip'], h['cs'], lfarlc, 0)
    hdr[0x1c:0x2c] = exe.data[0x1c:0x2c]
    for i, (o, sg) in enumerate(exe.relocs):
        struct.pack_into('<HH', hdr, lfarlc + 4 * i, o, sg)
    return bytes(hdr) + bytes(img), hdr_len


def far_code_pointers(exe):
    """Far pointers in DGROUP whose segment word is relocated and names a resident code segment.

    Returns sorted (ghidra_seg, offset, ds_offset_of_pointer, kind) with kind 'code' or 'stub'."""
    img = exe.image
    code = {s.seg: s for s in exe.segtab if s.is_code or s.is_stub}
    lo, hi = exe.dgroup * 16, exe.stack * 16
    out = set()
    for off, sg in exe.relocs:
        a = sg * 16 + off
        if not lo + 2 <= a < hi:
            continue
        s = code.get(u16(img, a))
        if s is None:
            continue
        o = u16(img, a - 2)
        if not s.minoff <= o < max(s.maxoff, s.minoff + 1):
            continue
        out.add((s.seg + BASE, o, a - 2 - lo, 'stub' if s.is_stub else 'code'))
    return sorted(out)


PRINTABLE = set(range(0x20, 0x7f)) | {9, 10, 13} | set(range(0x80, 0xa6))  # CP437 accented letters


def _esc(b):
    return (b.decode('cp437').replace('\\', '\\\\').replace('\r', '\\r').replace('\n', '\\n')
            .replace('\t', '\\t'))


def scan_strings(exe, minlen=4):
    """Printable NUL-terminated strings in DGROUP and in the overlay manager's data segments."""
    img = exe.image
    areas = [(exe.dgroup, exe.dgroup * 16, exe.dgroup * 16 + exe.dgroup_size)]
    first_stub = min(o.stub for o in exe.overlays)
    data_segs = sorted({s.seg for s in exe.segtab if exe.ovrman_data <= s.seg < first_stub})
    for i, sg in enumerate(data_segs):
        end = data_segs[i + 1] if i + 1 < len(data_segs) else first_stub
        areas.append((sg, sg * 16, end * 16))
    rows = []
    for segbase, lo, hi in areas:
        i = lo
        while i < hi:
            j = i
            while j < hi and img[j] in PRINTABLE:
                j += 1
            if j < hi and img[j] == 0 and j - i >= minlen:
                s = img[i:j]
                letters = sum(1 for c in s if chr(c).isalnum())
                if letters >= max(2, len(s) // 3):
                    rows.append(('%04x:%04x' % (segbase + BASE, i - segbase * 16), i + exe.hdr_len, len(s), _esc(s)))
            i = j + 1
    return rows


def block_rows(exe):
    """(ghidra_seg, name, kind, ds) for the memory blocks SetupOverlays.java names and configures."""
    rows = []
    for s in exe.code_segments():
        g = s.seg + BASE
        if s.seg == exe.ovrman:
            rows.append((g, 'CODE_%04x_OVRMAN' % g, 'code', exe.ovrman_data + BASE))
        else:
            rows.append((g, 'CODE_%04x' % g, 'code', None))
    for ov in exe.overlays:
        rows.append((ov.stub + BASE, 'STUB%02d_%04x' % (ov.n, ov.stub + BASE), 'stub', None))
    rows.append((exe.dgroup + BASE, 'DGROUP', 'dgroup', None))
    for p in exe.dgroup_parts:
        rows.append((p + BASE, 'DGROUP_%04x' % (p + BASE), 'dgroup_part', None))
    first_stub = min(o.stub for o in exe.overlays)
    for sg in sorted({s.seg for s in exe.segtab if exe.ovrman_data <= s.seg < first_stub}):
        if sg == exe.ovrman_data:
            name = 'OVRMAN_DATA'
        elif sg == exe.segtab_seg:
            name = 'OVR_SEGTAB'
        elif exe.image[sg * 16:sg * 16 + 2] == b'\xcd\x3f':
            name = 'OVRMAN_STUBHDR'
        else:
            name = 'DATA_%04x' % (sg + BASE)
        rows.append((sg + BASE, name, 'data', None))
    rows.append((exe.stack + BASE, 'STACK', 'stack', None))
    return rows


def write_setup(exe, flat, hdr_len, outdir):
    os.makedirs(outdir, exist_ok=True)

    def w(name, lines):
        with open(os.path.join(outdir, name), 'w', newline='\n', encoding='utf-8') as f:
            for ln in lines:
                f.write(ln + '\n')

    w('blocks.tsv', ['%04x\t%s\t%s\t%s' % (g, n, k, '%04x' % ds if ds is not None else '-')
                     for g, n, k, ds in block_rows(exe)])
    ent = []
    for ov in exe.overlays:
        if ov.seg is None:
            continue
        for k, eoff in enumerate(ov.entries):
            ent.append('%04x:%04x\t%04x:%04x\tovl%02d_e%02d' % (ov.stub + BASE, STUB_HDR + 5 * k,
                                                               ov.seg + BASE, eoff, ov.n, k))
    w('entries.tsv', ent)
    live = [ov for ov in exe.overlays if ov.seg is not None]
    ovl = []
    for i, ov in enumerate(live):
        end = live[i + 1].seg * 16 if i + 1 < len(live) else len(flat) - hdr_len
        ovl.append('OVL%02d_%04x\t%04x\t%d\t%d\t%04x' % (ov.n, ov.stub + BASE, ov.seg + BASE,
                                                       hdr_len + ov.seg * 16, end - ov.seg * 16,
                                                       exe.dgroup + BASE))
    w('overlays.tsv', ovl)
    w('farcode.tsv', ['%04x:%04x\tDS:%04x\t%s' % r for r in far_code_pointers(exe) if r[3] == 'code'])
    w('strings.tsv', ['address\tfile_offset\tlen\ttext'] +
      ['%s\t0x%05x\t%d\t%s' % r for r in scan_strings(exe)])


MAP_HEADER = ['ovl', 'stub_seg', 'ghidra_stub', 'ghidra_seg', 'file_offset', 'code_size', 'fixups', 'entries',
              'role']


def map_rows(exe):
    rows = []
    for ov in exe.overlays:
        rows.append([str(ov.n), '%04x' % ov.stub, '%04x' % (ov.stub + BASE),
                     '%04x' % (ov.seg + BASE) if ov.seg is not None else '-',
                     '0x%05x' % ov.file_offset, str(len(ov.code)), str(len(ov.fixups)), str(len(ov.entries)), ''])
    return rows


def read_map(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        for ln in f:
            p = ln.rstrip('\n').split('\t')
            if p and p[0].isdigit():
                rows.append(p + [''] * (len(MAP_HEADER) - len(p)))
    return rows


def cmd_info(exe):
    h = exe.mz
    print('%s: load module file 0x%x..0x%x (%d bytes), %d relocations, CS:IP %04x:%04x, SS:SP %04x:%04x'
          % (exe.path, exe.hdr_len, exe.load_end, len(exe.image), len(exe.relocs), h['cs'], h['ip'], h['ss'], h['sp']))
    print('FBOV at 0x%x: overlay area %d bytes, segment table at file 0x%x (%d entries)'
          % (exe.load_end, exe.ovr_size, exe.segtab_off, len(exe.segtab)))
    print('DGROUP %04x (size 0x%x, parts %s), stack %04x, overlay manager %04x (data %04x), segment table seg %04x'
          % (exe.dgroup, exe.dgroup_size, ' '.join('%04x' % p for p in exe.dgroup_parts), exe.stack,
             exe.ovrman, exe.ovrman_data, exe.segtab_seg))
    kinds = {0: '-', 1: 'code', 2: 'ovr', 3: 'stub', 4: 'data'}
    for s in exe.segtab:
        print('  seg %2d %04x (Ghidra %04x) min %04x max %04x %s' % (s.index, s.seg, s.seg + BASE, s.minoff,
                                                                    s.maxoff, kinds.get(s.flags, '?')))
    flatten(exe)
    tot = 0
    for ov in exe.overlays:
        tot += len(ov.code) + 2 * len(ov.fixups)
        print('  ovl%02d stub %04x -> %s file 0x%05x code %5d fixups %3d entries %2d' % (
            ov.n, ov.stub, '%04x' % (ov.seg + BASE) if ov.seg is not None else ' -  ', ov.file_offset,
            len(ov.code), len(ov.fixups), len(ov.entries)))
    print('%d overlays, %d bytes of code and fix-ups' % (len(exe.overlays), tot))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('info', 'flatten', 'map'):
        p = sub.add_parser(name)
        p.add_argument('--exe', default=None, help='ssg.exe (default: <game folder>/ssg.exe)')
        p.add_argument('--data', default=None, help='game folder (default: $GG_DATA, else GadgetsAndGizmos)')
        if name == 'flatten':
            p.add_argument('out', help='flattened image to write')
            p.add_argument('setup_dir', help='directory for the Ghidra set-up tables')
        if name == 'map':
            p.add_argument('--roles', help='existing overlay_map.tsv whose role column is kept')
            p.add_argument('--check', action='store_true', help='compare with --roles and exit 1 on a difference')
    a = ap.parse_args(argv)
    path = a.exe or (os.path.join(a.data, 'ssg.exe') if a.data else default_exe())
    try:
        exe = VroommExe(path)
        if a.cmd == 'info':
            cmd_info(exe)
        elif a.cmd == 'flatten':
            game = os.path.normcase(os.path.dirname(os.path.abspath(path)))
            for o in (a.out, a.setup_dir):
                q = os.path.normcase(os.path.abspath(o))
                if q == game or q.startswith(game + os.sep):
                    raise VroommError('refusing to write into the game folder: %s' % o)
            flat, hdr_len = flatten(exe)
            d = os.path.dirname(os.path.abspath(a.out))
            os.makedirs(d, exist_ok=True)
            with open(a.out, 'wb') as f:
                f.write(flat)
            write_setup(exe, flat, hdr_len, a.setup_dir)
            n = sum(1 for ov in exe.overlays if ov.seg is not None)
            print('wrote %s (%d bytes: resident 0x%x, %d overlays) and %s'
                  % (a.out, len(flat), len(exe.image), n, a.setup_dir))
        else:
            flatten(exe)
            rows = map_rows(exe)
            old = read_map(a.roles) if a.roles else []
            byn = {r[0]: r for r in old}
            for r in rows:
                if r[0] in byn:
                    r[-1] = byn[r[0]][-1]
            if a.check:
                bad = [r[0] for r in rows if r[0] not in byn or byn[r[0]][:-1] != r[:-1]]
                if bad or len(old) != len(rows):
                    print('overlay map differs for overlays: %s' % (' '.join(bad) or '(row count)'))
                    return 1
                print('overlay map matches (%d overlays)' % len(rows))
                return 0
            sys.stdout.reconfigure(newline='\n')
            print('\t'.join(MAP_HEADER))
            for r in rows:
                print('\t'.join(r))
    except (OSError, VroommError) as e:
        print('vroomm: %s' % e, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
