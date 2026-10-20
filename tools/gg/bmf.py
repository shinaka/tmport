"""Gizmos & Gadgets (DOS v1.0): BMF songs and sound effects -> event listings.

Spec: re/gg/docs/fmt_sound.md. Driver = resident segment 1bb4 (Ghidra SSSS:OOOO):
1bb4:0ebc checks 'BMF' 0x1A and registers BMF + 0x10 in one of 64 song slots (1bb4:0e71);
Snd_Tick 1bb4:0db3 (every 2nd PIT tick, 72.83 Hz) runs the class interpreter of each slot
through DS:1aaa: class 1 = 1bb4:0966 (PC speaker), class 3 = 1bb4:0ad6 (OPL2).

    BMF resource:
    +0x00  'BMF' 0x1A, then 12 zero bytes
    song block s = BMF + 0x10 (the driver works on it in place):
    s+0x00 u8  class                 1 speaker, 3 OPL2 (0 divisor list, 2 PSG, 4 MPU unused)
    s+0x01 u16 delay countdown       run time (1bb4:0045 sets 1)
    s+0x03 u16 tick counter          run time
    s+0x05 u16 stream position       run time, relative to s
    s+0x07 u16 cue tick, s+0x09 far cue callback, s+0x0d u8 loop flag   (set through the API)
    s+0x0e 16 x { u8 flags (bit 7: channel used by the song; bits 0/1 set at run time),
                  u8 program (0xFF = none yet), u16 pitch bend (run time) }
    s+0x4e u8 loop section, s+0x4f u8 current section, s+0x50 u8 transpose (copied from the
           section entry), s+0x51 u8 running status
    s+0x52 u8  section count n (1 in every shipped song)
    s+0x53 n x { u16 start (relative to s), u8 next section (0xFF = none), u8 transpose }
    class 3: s+0x53+4n u8 patch count m, then m x u16 offsets (relative to s) of 11-byte OPL
           patches: 5 bytes for operator 1 (registers 20/40/60/80/E0), 1 byte for C0+voice,
           5 bytes for operator 2 (1bb4:0270, register list DS:170d)

    section stream (at start): one delay byte the driver skips (0xFF: plus a u16), then events,
    each followed by a delay byte (0 = next event in the same tick, 0xFF = u16 delay follows):
      8n            note off (no data byte)
      9n kk         note on, class 1        9n kk vv   note on with velocity, class 3
      Cn pp         program (class 3 only; class 1 would take it as a note on)
      En ll hh      pitch bend (u16)
      FF len16 t v  meta (1bb4:08ef): skip len bytes; t = 1 with v = current section ends
                    the section (next section, else the loop section if looping, else stop)
      a data byte (< 0x80) in place of a status repeats the last status (running status)

CLI (python -P tools/gg/bmf.py ...)::

    bmf.py PATH [--events]        one song ('0027/5') or every song of a bank ('0027')
    bmf.py --all [--out FILE]     summary line per song (TSV) and a check of every song
    bmf.py --check                parse every song, report only
    bmf.py --banks                every bundle holding songs: music bank (M lists, I 0, one
                                  song; loader 1cdd:0024) or effect bank (songs only; 1cdd:012d),
                                  the class of its songs, and its partner: a speaker bank
                                  (class 1) at id pairs with an FM bank (class 3) at id + 1
"""
import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ggarc  # noqa: E402

BMF_MAGIC = b'BMF\x1a'
SONG_OFF = 0x10
SND_TICK_HZ = 1193181.6667 / 0x2000 / 2      # 72.826 Hz (Snd_Tick on even PIT ticks)
CLASS_NAMES = {0: 'divisor list', 1: 'speaker', 2: 'PSG', 3: 'OPL2', 4: 'MPU-401'}
PATCH_SIZE = 11


class BmfError(ValueError):
    pass


class Song:
    __slots__ = ('cls', 'channels', 'sections', 'patches', 'events', 'end', 'size',
                 'ticks', 'loop_section', 'transpose_field')


def parse(p):
    if len(p) < SONG_OFF + 0x57 or p[:4] != BMF_MAGIC:
        raise BmfError('not a BMF song')
    s = p[SONG_OFF:]
    g = Song()
    g.size = len(s)
    g.cls = s[0]
    g.channels = [struct.unpack_from('<BBH', s, 0x0E + 4 * k) for k in range(16)]
    g.loop_section = s[0x4E]
    g.transpose_field = s[0x50]
    n = s[0x52]
    g.sections = [struct.unpack_from('<HBB', s, 0x53 + 4 * k) for k in range(n)]
    g.patches = []
    if g.cls == 3:
        pt = 0x53 + 4 * n
        m = s[pt]
        offs = struct.unpack_from('<%dH' % m, s, pt + 1)
        for o in offs:
            if o + PATCH_SIZE > len(s):
                raise BmfError('patch at %d past the end' % o)
            g.patches.append((o, s[o:o + PATCH_SIZE]))
    g.events = []
    g.end = 0
    g.ticks = 0
    for si, (start, nxt, tr) in enumerate(g.sections):
        i = start
        d0 = s[i]
        i += 1
        if d0 == 0xFF:
            i += 2
        tick = 0
        run = None
        while True:
            if i >= len(s):
                raise BmfError('section %d runs past the end' % si)
            at = i                       # event start (status byte, or first data byte)
            st = s[i]
            if st < 0x80:
                if run is None:
                    raise BmfError('running status without a status at %d' % i)
                st = run
            else:
                i += 1
            run = st
            hi = st & 0xF0
            ch = st & 0x0F
            if hi == 0x80:
                ev = ('off', ch)
            elif hi == 0x90 and g.cls == 3:
                ev = ('on', ch, s[i], s[i + 1])
                i += 2
            elif hi == 0x90:
                ev = ('on', ch, s[i])
                i += 1
            elif hi == 0xC0:
                ev = ('prog', ch, s[i])
                i += 1
            elif hi == 0xE0:
                ev = ('bend', ch, s[i] | s[i + 1] << 8)
                i += 2
            elif st >= 0xF0:
                ln = struct.unpack_from('<H', s, i)[0]
                body = s[i + 2:i + 2 + ln]
                i += 2 + ln
                ev = ('meta', st, ln) + tuple(body)
                g.events.append((si, tick, at, ev))
                if st == 0xFF and ln >= 2 and body[0] == 1 and body[1] == si:
                    break
                d = s[i]
                i += 1
                if d == 0xFF:
                    d = struct.unpack_from('<H', s, i)[0]
                    i += 2
                tick += d
                continue
            else:
                raise BmfError('status %02x not handled by the class %d interpreter' % (st, g.cls))
            g.events.append((si, tick, at, ev))
            d = s[i]
            i += 1
            if d == 0xFF:
                d = struct.unpack_from('<H', s, i)[0]
                i += 2
            tick += d
        g.end = max(g.end, i)
        g.ticks = max(g.ticks, tick)
    return g


def fmt_event(ev):
    k = ev[0]
    if k == 'off':
        return 'off   ch%-2d' % ev[1]
    if k == 'on':
        return 'on    ch%-2d key %3d' % (ev[1], ev[2]) + (' vel %3d' % ev[3] if len(ev) > 3 else '')
    if k == 'prog':
        return 'prog  ch%-2d %d' % (ev[1], ev[2])
    if k == 'bend':
        return 'bend  ch%-2d 0x%04x' % (ev[1], ev[2])
    return 'meta  %02x len %d: %s' % (ev[1], ev[2], ' '.join('%02x' % b for b in ev[3:]))


def describe(path, p, events=True, out=None):
    out = out or sys.stdout
    g = parse(p)
    used = [k for k, c in enumerate(g.channels) if c[0] & 0x80]
    print('%s: class %d (%s), %d bytes, %d events, %d ticks = %.2f s, channels used %s, '
          'sections %s' % (path, g.cls, CLASS_NAMES.get(g.cls, '?'), len(p), len(g.events),
                           g.ticks, g.ticks / SND_TICK_HZ, used or '-',
                           ' '.join('@%d next %d tr %d' % sct for sct in g.sections)), file=out)
    for o, b in g.patches:
        print('  patch @0x%03x: op1 %s | C0 %02x | op2 %s'
              % (o, b[:5].hex(' '), b[5], b[6:].hex(' ')), file=out)
    if events:
        for si, tick, at, ev in g.events:
            print('  %6d  @%04x  %s' % (tick, at, fmt_event(ev)), file=out)
    return g


def bank_info(arc, rid):
    """(kind, song classes, members, songs) of a bundle that holds songs, else None.
    Effect banks may hold 1-byte 'C' members (FF) in place of an effect that has no version
    for this device; 1bb4:0ebc rejects them (not BMF) and the slot stays empty."""
    e = arc.entry(rid)
    if e.type != 0x01:
        return None
    mem = ggarc.parse_bundle(e.payload)
    songs = [m for m in mem if m.payload[:4] == BMF_MAGIC]
    holes = [m for m in mem if m.type == ord('C') and m.payload == bytes((0xFF,))]
    if not songs and not (holes and len(holes) == len(mem)):
        return None
    kind = 'effects' if len(songs) + len(holes) == len(mem) else 'music'
    return kind, sorted(set(m.payload[SONG_OFF] for m in songs)), len(mem), len(songs)


def _banks(arc):
    print('bank	kind	classes	members	songs	partner	partner_classes')
    bad = pairs = 0
    for rid in arc.ids():
        b = bank_info(arc, rid)
        if not b:
            continue
        speaker = b[1] in ([1], [])
        partner = rid + 1 if speaker else rid - 1
        pb = bank_info(arc, partner) if arc.exists(partner) else None
        ok = pb is not None and (pb[1] == [3] if speaker else pb[1] in ([1], [])) and (b[0] == 'music' or pb[2] == b[2])
        print('%04x	%s	%s	%d	%d	%s	%s' % (rid, b[0], ','.join(map(str, b[1])), b[2], b[3],
                                              '%04x' % partner if ok else '-',
                                              ','.join(map(str, pb[1])) if ok else '-'))
        if speaker:
            pairs += 1 if ok else 0
        bad += 0 if ok else 1
    print('%d speaker/FM pairs (id, id + 1); %d banks without a partner' % (pairs, bad),
          file=sys.stderr)
    return 0


def song_leaves(arc):
    for lf in ggarc.walk_leaves(arc):
        if lf.kind == 'song':
            yield lf


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ggarc.add_data_arg(ap)
    ap.add_argument('path', nargs='?')
    ap.add_argument('--events', action='store_true', help='list the events')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--banks', action='store_true')
    ap.add_argument('--out', default=None, help='--all: TSV file (default: stdout)')
    a = ap.parse_args(argv)
    try:
        arc = ggarc.Archive(a.data)
        if a.banks:
            return _banks(arc)
        if a.check or a.all:
            errors = 0
            stats = collections.Counter()
            rows = []
            for lf in song_leaves(arc):
                try:
                    g = parse(lf.payload)
                except (BmfError, IndexError, struct.error) as e:
                    errors += 1
                    print('ERROR %s: %s' % (lf.path, e))
                    continue
                stats['class%d' % g.cls] += 1
                stats['events'] += len(g.events)
                for _, _, _, ev in g.events:
                    stats['ev_%s_class%d' % (ev[0], g.cls)] += 1
                if g.end != g.size:
                    stats['bytes_after_end'] += 1
                    print('note %s: %d bytes after the end meta' % (lf.path, g.size - g.end))
                rows.append('%s\t%d\t%d\t%d\t%d\t%.3f\t%d\t%s'
                            % (lf.path, g.cls, len(lf.payload), len(g.events), g.ticks,
                               g.ticks / SND_TICK_HZ, len(g.patches),
                               ','.join(str(k) for k, c in enumerate(g.channels) if c[0] & 0x80)))
            if a.all:
                text = 'path\tclass\tbytes\tevents\tticks\tseconds\tpatches\tchannels\n' + '\n'.join(rows) + '\n'
                if a.out:
                    ggarc.safe_out_file(a.out, arc.data)
                    with open(a.out, 'w', newline='\n') as f:
                        f.write(text)
                else:
                    sys.stdout.write(text)
            for k in sorted(stats):
                print('  %s: %d' % (k, stats[k]), file=sys.stderr if a.all and not a.out else sys.stdout)
            print('bmf check: %d songs (%d class 1, %d class 3), %d events; %d error%s'
                  % (stats['class1'] + stats['class3'], stats['class1'], stats['class3'],
                     stats['events'], errors, '' if errors == 1 else 's'),
                  file=sys.stderr if a.all and not a.out else sys.stdout)
            return 1 if errors else 0
        if not a.path:
            ap.error('give a song or bank, --all or --check')
        leaf = ggarc.find_leaf(arc, a.path)
        if leaf.kind == 'bundle':
            for m in ggarc.parse_bundle(leaf.payload):
                tag = '%s/%d' % (leaf.path, m.index)
                if m.payload[:4] == BMF_MAGIC:
                    describe(tag, m.payload, a.events)
                else:
                    print('%s: type %s, %d bytes: %s' % (tag, ggarc.type_name(m.type),
                                                         len(m.payload), m.payload[:16].hex(' ')))
        elif leaf.kind == 'song':
            describe(leaf.path, leaf.payload, a.events)
        else:
            raise ggarc.ArchiveError('%s is a %s' % (leaf.path, leaf.kind))
    except (ggarc.ArchiveError, BmfError) as e:
        print('bmf: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
