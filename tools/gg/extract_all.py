"""Gizmos & Gadgets (DOS v1.0): extract every resource of gizmo000..003.dat.

Writes, under OUT (default <repo>/extracted/previews/gg, ignored by git):

    raw/<id>.<T>.bin            every entry's decoded payload (bundles as they are)
    raw/<id>/<m>.<T>.bin        every bundle member
    png/<path>.png              every picture (RGBA; palette as in index.tsv)
    png/<id>_sheet.png          the pictures of a bundle side by side
    pal/<path>.png              every palette resource over the base palette
    text/<path>.txt             texts, hints and strings decoded (ggtext.py)
    bmf/<path>.txt              every song's event listing (bmf.py)
    dam/<path>.txt              every DAM script listing (dam.py)
    fonts/                      fnt_0009/000a .png and .tsv (fnt.py)
    movie/movie_NNN.png         the 202 frames of the final movie (movie.py defaults)
    gizmo004.png                the run-time save-under file, if present
    index.tsv                   one row per entry and per bundle member
    summary.txt                 counts per type and kind, errors

<path> is the id ('1002') or id_member ('0001_3'); <T> the type letter (01 for bundles).
The data folder is only read. Exit code 1 if anything failed to decode.

CLI: python -P tools/gg/extract_all.py [--data DIR] [--out DIR] [--no-png] [--no-movie]
"""
import argparse
import collections
import io
import os
import struct
import sys
import time
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ggarc  # noqa: E402
import lpx  # noqa: E402
import fnt  # noqa: E402
import dam  # noqa: E402
import bmf  # noqa: E402
import movie  # noqa: E402
import ggtext  # noqa: E402

COLUMNS = ['path', 'id', 'member', 'vol', 'index', 'file_offset', 'type', 'method',
           'stored_bytes', 'bytes', 'kind', 'detail', 'outputs']


def fname(path):
    return path.replace('/', '_')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ggarc.add_data_arg(ap)
    ap.add_argument('--out', default=ggarc.DEFAULT_PREVIEWS)
    ap.add_argument('--no-png', action='store_true', help='skip pictures, palettes, fonts')
    ap.add_argument('--no-movie', action='store_true')
    a = ap.parse_args(argv)
    t0 = time.time()
    try:
        arc = ggarc.Archive(a.data)
        out = ggarc.safe_out_dir(a.out, arc.data)
    except ggarc.ArchiveError as e:
        print('extract_all: %s' % e, file=sys.stderr)
        return 1
    dirs = {k: os.path.join(out, k) for k in ('raw', 'png', 'pal', 'text', 'bmf', 'dam', 'fonts', 'movie')}
    for d in dirs.values():
        os.makedirs(d, exist_ok=True)
    base, from_exe = lpx.base_palette(arc)
    errors = []
    rows = []
    kinds = collections.Counter()
    types = collections.Counter()
    text_stats = collections.Counter()
    n_files = 0

    def write(path, data, mode='wb'):
        nonlocal n_files
        with open(path, mode, **({} if 'b' in mode else {'encoding': 'utf-8', 'newline': '\n'})) as f:
            f.write(data)
        n_files += 1
        return os.path.relpath(path, out).replace('\\', '/')

    def leaf_outputs(rid, member, t, payload, kind, leaf):
        """Write the decoded forms of one leaf; returns (detail, [outputs])."""
        path = ggarc.leaf_path(rid, member)
        outs, detail = [], ''
        try:
            if kind == 'picture':
                pic = lpx.decode_lpx(payload)
                detail = '%dx%d fmt%d key %d' % (pic.w, pic.h, pic.fmt, pic.key)
                if not a.no_png:
                    pal = base.copy()
                    pals, note = lpx.palettes_for(arc, leaf)
                    for p in pals:
                        pal.apply(p)
                    detail += ', ' + note
                    fn = os.path.join(dirs['png'], fname(path) + '.png')
                    lpx.to_image(pic, pal).save(fn)
                    n_add(1)
                    outs.append('png/' + fname(path) + '.png')
            elif kind == 'palette':
                off, cnt, rgb, extra = lpx.parse_palette(payload)
                detail = 'colours %d..%d%s' % (off // 3, (off + cnt) // 3 - 1,
                                              ', %d extra bytes' % len(extra) if extra else '')
                if not a.no_png:
                    fn = os.path.join(dirs['pal'], fname(path) + '.png')
                    lpx.swatch(base.copy().apply(payload), off, cnt).save(fn)
                    n_add(1)
                    outs.append('pal/' + fname(path) + '.png')
            elif kind in ('text', 'hint', 'string'):
                s = ggtext.decode(payload, text_stats)
                outs.append(write(os.path.join(dirs['text'], fname(path) + '.txt'), s + '\n', 'w'))
                detail = '%d lines' % (s.count('\n') + 1)
            elif kind == 'song':
                buf = io.StringIO()
                g = bmf.describe(path, payload, True, out=buf)
                outs.append(write(os.path.join(dirs['bmf'], fname(path) + '.txt'), buf.getvalue(), 'w'))
                detail = 'class %d, %d events, %d ticks' % (g.cls, len(g.events), g.ticks)
            elif kind == 'dam':
                ops = dam.parse(payload)
                lines = ['%s: %d ticks' % (path, len(dam.ticks(ops)))]
                lines += ['  w%-3d %04x  %s' % (wi, op, dam.fmt_op(op, args)) for wi, op, args in ops]
                outs.append(write(os.path.join(dirs['dam'], fname(path) + '.txt'), '\n'.join(lines) + '\n', 'w'))
                detail = '%d ops' % len(ops)
            elif kind == 'font':
                f = fnt.parse(payload)
                detail = '%d glyphs, height %d, spacing %d' % (len(f.glyphs), f.height, f.spacing)
                if not a.no_png:
                    fnt.render_grid(f).save(os.path.join(dirs['fonts'], 'fnt_%04x.png' % rid))
                    n_add(1)
                    outs.append('fonts/fnt_%04x.png' % rid)
            elif kind == 'movie':
                detail = 'delta frame %d' % (rid - ggarc.MOVIE_FIRST)
            elif kind == 'integrity':
                detail = 'word 0x%04x' % struct.unpack_from('<H', payload, 0)[0]
        except Exception as e:  # noqa: BLE001 - every failure is reported and counted
            errors.append('%s (%s): %s: %s' % (path, kind, type(e).__name__, e))
            detail = 'ERROR ' + str(e)
        return detail, outs

    added = [0]

    def n_add(k):
        added[0] += k

    for rid in arc.ids():
        v, k = rid >> 12, rid & 0xFFF
        vol = arc.volume(v)
        try:
            e = arc.entry(rid)
        except Exception as ex:  # noqa: BLE001
            errors.append('%04x: %s' % (rid, ex))
            continue
        kind = ggarc.classify(e.type, e.payload, rid)
        tn = ggarc.type_name(e.type)
        kinds[kind] += 1
        types[tn] += 1
        raw = write(os.path.join(dirs['raw'], '%04x.%s.bin' % (rid, tn)), e.payload)
        leaf = ggarc.Leaf(rid, ggarc.leaf_path(rid), e.type, e.method, e.payload, kind)
        mrows = []
        if kind == 'bundle':
            try:
                mem = ggarc.parse_bundle(e.payload)
            except ggarc.ArchiveError as ex:
                errors.append('%04x: %s' % (rid, ex))
                mem = []
            detail = '%d members: %s' % (len(mem), ' '.join(ggarc.type_name(m.type) for m in mem))
            outs = [raw]
            pics = []
            for m in mem:
                mk = ggarc.classify(m.type, m.payload, rid, m.index)
                mtn = ggarc.type_name(m.type)
                kinds['member:' + mk] += 1
                types['member:' + mtn] += 1
                os.makedirs(os.path.join(dirs['raw'], '%04x' % rid), exist_ok=True)
                mraw = write(os.path.join(dirs['raw'], '%04x' % rid, '%d.%s.bin' % (m.index, mtn)), m.payload)
                mleaf = ggarc.Leaf(rid, ggarc.leaf_path(rid, m.index), m.type, m.method, m.payload, mk)
                mdetail, mouts = leaf_outputs(rid, m.index, m.type, m.payload, mk, mleaf)
                if mk == 'picture':
                    pics.append(mleaf)
                mrows.append([mleaf.path, '%04x' % rid, str(m.index), str(v), str(k), '-', mtn,
                             str(m.method), '-', str(len(m.payload)), mk, mdetail,
                             ' '.join([mraw] + mouts)])
            if pics and not a.no_png:
                try:
                    pal = base.copy()
                    for p in lpx.palettes_for(arc, leaf)[0]:
                        pal.apply(p)
                    ims = [lpx.to_image(lpx.decode_lpx(pl.payload), pal) for pl in pics]
                    lpx.sheet(ims).save(os.path.join(dirs['png'], '%04x_sheet.png' % rid))
                    n_add(1)
                    outs.append('png/%04x_sheet.png' % rid)
                except Exception as ex:  # noqa: BLE001
                    errors.append('%04x sheet: %s' % (rid, ex))
        else:
            detail, outs = leaf_outputs(rid, None, e.type, e.payload, kind, leaf)
            outs = [raw] + outs
        rows.append([ggarc.leaf_path(rid), '%04x' % rid, '-', str(v), str(k), '0x%06x' % vol.file_offset(k),
                     tn, str(e.method), str(e.csize - 2), str(len(e.payload)), kind, detail, ' '.join(outs)])
        rows.extend(mrows)

    # fonts' metrics, the movie, gizmo004.dat
    if not a.no_png:
        for rid in ggarc.FONT_IDS:
            try:
                with redirect_stdout(io.StringIO()):
                    fnt.main(['--data', arc.data, '--out', dirs['fonts'], '%04x' % rid])
            except Exception as ex:  # noqa: BLE001
                errors.append('font %04x: %s' % (rid, ex))
    if not a.no_movie:
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = movie.main(['--data', arc.data, '--out', dirs['movie'], '--sheet'])
        if rc:
            errors.append('movie: ' + buf.getvalue().strip().replace('\n', ' | '))
        else:
            n_add(203)
    g4 = os.path.join(arc.data, 'gizmo004.dat')
    if os.path.isfile(g4) and not a.no_png:
        try:
            with open(g4, 'rb') as f:
                d = f.read()
            w, h = struct.unpack_from('<HH', d, 0)
            if 4 + w * h == len(d):
                from PIL import Image
                im = Image.frombytes('P', (w, h), d[4:4 + w * h])
                im.putpalette(base.rgb())
                im.save(os.path.join(out, 'gizmo004.png'))
                n_add(1)
            else:
                errors.append('gizmo004.dat: %d x %d does not match its size %d' % (w, h, len(d)))
        except Exception as ex:  # noqa: BLE001
            errors.append('gizmo004.dat: %s' % ex)

    with open(os.path.join(out, 'index.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\t'.join(COLUMNS) + '\n')
        for r in rows:
            f.write('\t'.join(r) + '\n')
    n_entries = sum(1 for r in rows if r[2] == '-')
    n_members = len(rows) - n_entries
    lines = ['G&G extract: %d entries, %d bundle members, %d files written (+%d images), %.1f s'
             % (n_entries, n_members, n_files, added[0], time.time() - t0),
             'base palette: %s' % ('ssg.exe DS:024e' if from_exe else 'grey ramp (no ssg.exe)'),
             'entry types: ' + ', '.join('%s %d' % kv for kv in sorted(types.items()) if not kv[0].startswith('member:')),
             'member types: ' + ', '.join('%s %d' % (k[7:], n) for k, n in sorted(types.items()) if k.startswith('member:')),
             'entry kinds: ' + ', '.join('%s %d' % kv for kv in sorted(kinds.items()) if not kv[0].startswith('member:')),
             'member kinds: ' + ', '.join('%s %d' % (k[7:], n) for k, n in sorted(kinds.items()) if k.startswith('member:')),
             'text markup: ' + ', '.join('%s %d' % kv for kv in sorted(text_stats.items())),
             'errors: %d' % len(errors)] + ['  ' + x for x in errors]
    with open(os.path.join(out, 'summary.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('\n'.join(lines[:1] + lines[-1 - len(errors):]))
    print('-> %s (index.tsv, summary.txt)' % out)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
