"""Gizmos & Gadgets (DOS v1.0): text resources (T, H and string S) to readable text.

Spec: re/gg/docs/fmt_data.md §6. The text renderer is overlay 2b89 (2b89:007a measures and
draws; character switch CS:03ba, escape switch CS:03ae):

    00          end of the text (every text is NUL-terminated)
    0D, 0A      line break (CR in almost all texts)
    20          space, the wrap point
    FF 01 n     insert substitution string n-1 (set by the caller)
    FF 02 .. FF underline on/off
    FF 03 c .. FF   text colour c - 1 until the next colour escape
    FF 04       ignored
    FF 05 .. FF centring on/off
    FF 06 .. FF comment, not drawn (designer notes in the New Player Hints)
    11, 17..1D, 1F   key-cap pictograms of font 9 (Spacebar, Ctrl, Enter, arrows, Ctrl+R, Esc)
    %d, %s      printf fields filled by the caller (sprintf) before drawing

Shown here as: line breaks as new lines, {colour N}, {underline}, {centre}, {subst N},
{note: ...}, {Enter} etc., other control bytes as {xNN}.

CLI: python -P tools/gg/ggtext.py PATH [PATH ...]   (resource id or bundle member)
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ggarc  # noqa: E402

KEYCAPS = {0x11: 'Spacebar', 0x17: 'Ctrl', 0x18: 'Enter', 0x19: 'Left', 0x1A: 'Right',
           0x1B: 'Down', 0x1C: 'Up', 0x1D: 'Ctrl+R', 0x1F: 'Esc'}


def decode(p, stats=None):
    """Bytes of a text resource -> readable str. stats (a Counter) collects the markup used."""
    out = []
    i = 0
    n = len(p)

    def count(k):
        if stats is not None:
            stats[k] += 1

    while i < n:
        c = p[i]
        if c == 0:
            if i != n - 1:
                count('bytes_after_nul')
                out.append('{after NUL: %d bytes}' % (n - 1 - i))
            break
        if c == 0xFF and i + 1 < n:
            k = p[i + 1]
            j = p.find(b'\xff', i + 2)
            if k == 6:
                body = p[i + 2:j if j >= 0 else n]
                out.append('{note: %s}' % body.decode('latin-1'))
                count('FF06_note')
                i = (j + 1) if j >= 0 else n
                continue
            if k == 3 and i + 2 < n:
                out.append('{colour %d}' % (p[i + 2] - 1))
                count('FF03_colour')
                i = (j + 1) if j >= 0 else n
                continue
            if k == 1 and i + 2 < n:
                out.append('{subst %d}' % (p[i + 2] - 1))
                count('FF01_subst')
                i += 3
                continue
            if k in (2, 5):
                out.append('{underline}' if k == 2 else '{centre}')
                count('FF%02x' % k)
                i = (j + 1) if j >= 0 else n
                continue
            out.append('{FF %02x}' % k)
            count('FF_other')
            i += 2
            continue
        if c in (0x0D, 0x0A):
            out.append('\n')
            count('CR' if c == 0x0D else 'LF')
        elif c in KEYCAPS:
            out.append('{%s}' % KEYCAPS[c])
            count('keycap')
        elif c < 0x20 or c >= 0x7F:
            out.append('{x%02X}' % c)
            count('other_control')
        else:
            out.append(chr(c))
        i += 1
    return ''.join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ggarc.add_data_arg(ap)
    ap.add_argument('paths', nargs='+')
    a = ap.parse_args(argv)
    try:
        arc = ggarc.Archive(a.data)
        for path in a.paths:
            lf = ggarc.find_leaf(arc, path)
            if lf.kind == 'bundle':
                for m in ggarc.parse_bundle(lf.payload):
                    k = ggarc.classify(m.type, m.payload, lf.rid, m.index)
                    if k in ('text', 'hint', 'string'):
                        print('--- %s/%d (%s)' % (lf.path, m.index, ggarc.type_name(m.type)))
                        print(decode(m.payload))
            else:
                print('--- %s (%s)' % (lf.path, ggarc.type_name(lf.type)))
                print(decode(lf.payload))
    except ggarc.ArchiveError as e:
        print('ggtext: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
