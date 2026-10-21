"""Parse the puzzler / hint / help-page text resources of Treasure Mountain! (Windows CD).

Resources: TMT.DLL, Windows custom type 32515 (Mac type 'OTHR'), ids
    11000-11358  puzzlers ("elf riddles"), 359 records, index = id - 11000
    11500-11532  Flutter hint balloons, 33 records,      index = id - 11500
    11600-11611  help / parent-info dialog pages, 12 records

The raw resources are read from the directory produced by the engine's tools/ne_resources.py
(extracted/res/TMT.DLL/32515/<id>).  Optionally TMTWINCD.EXE is read to pull the
tables the game keeps in DGROUP (level ranges, speech ids, voice bank, hint map ...).
No game text is embedded in this script.

Format summary (see re/docs/fmt_othr_puzzles.md for the full spec):
  * payload = bytes up to the first 0x00; the rest of the (512-byte aligned) NE
    resource is zero padding.
  * 0x0D (and 0x0A) = newline, 0x09 = tab, 0xFE = return to left margin (same line),
    0x80..0x8F = special key glyphs of the game font, 0xFF <op> ... 0xFF = control code:
        FF 01 n FF   insert caller argument string n (1-based)      (unused here)
        FF 02 FF     toggle underline mode ('_' glyphs are not drawn, only underlined)
        FF 03 c FF   set text colour index c-1
        FF 04 .. FF  set tab stops (buggy byte-stride reader)        (unused here)
        FF 05 FF     toggle centred mode (current and following lines)
        FF 06 .. FF  comment, ignored by the renderer (puzzlers carry "L=<level>")
  * Puzzler layout (parser of FUN_1150_0901): question text, terminated by the third
    consecutive newline after visible content (i.e. two empty lines), followed by
    exactly three answer choices separated by newlines.  Choice 0 is the correct one;
    the game shuffles the display order.

Usage:  python -P othr_puzzles.py [--res DIR] [--exe TMTWINCD.EXE] [--out FILE.json]
                                  [--sounds DIR]   (parent of TMnSOUND.DLL/32513; optional
                                                    cross-check that every speech id exists)
"""
import argparse
import json
import os
import re
import struct
import sys
from collections import Counter, OrderedDict

DEF_RES = r'C:\Git\TM\extracted\res\TMT.DLL\32515'
DEF_EXE = r'C:\Git\TM\extracted\wincd\TMTWINCD\TMTWINCD.EXE'
DEF_OUT = r'C:\Git\TM\extracted\previews\othr_puzzles.json'
DEF_SOUNDS = r'C:\Git\TM\extracted\res'      # parent of TM0SOUND.DLL .. TM8SOUND.DLL dirs
PUZ_SPEECH_LO, PUZ_SPEECH_HI = 4000, 6999    # puzzle line/choice recordings (TM2..TM8SOUND)

PUZ_FIRST, PUZ_COUNT = 11000, 359
HINT_FIRST, HINT_COUNT = 11500, 33
HELP_FIRST, HELP_LAST = 11600, 11611

CR, LF, TAB, LMARGIN, CODE = 0x0D, 0x0A, 0x09, 0xFE, 0xFF
OPNAMES = {1: 'arg', 2: 'ul', 3: 'color', 4: 'tabs', 5: 'center', 6: 'note'}
NAME2OP = {v: k for k, v in OPNAMES.items()}

# --- DGROUP tables of TMTWINCD.EXE (offsets inside the automatic data segment) ---
DG_LEVEL_START = 0x1DE7      # u16[5]  first puzzle index of level 0..3, + end (359)
DG_SPEECH_BASE = 0x1990      # u16[359] first speech sound id of each puzzle
DG_VOICE = 0x1C60            # u8[359]  speaker / voice bank 1..3 (praise 7x00, x=2+voice)
DG_NO_CHOICE_SPEECH = 0x1DC7  # u16[16] puzzle indices whose answer choices are not spoken
DG_HINT_BIT = 0x13D0         # i16[34] hint index -> "show once" bit in user mask (-1 = always)
DG_RANK_START_HINT = 0x0E8F  # i16[8]  hint shown at trip start, by rank
DG_RANK_PROMO_HINT = 0x0FC8  # i16[8]  hint shown on promotion, by rank
DG_RANK_DONE_HINT = 0x0FD8   # i16[8]  hint shown at trip end without promotion, by rank
# help dialogs: (first resource id, page count, DGROUP offset of the title string,
# entry point in segment 1180 that calls FUN_1180_0000)
HELP_DIALOGS = [(11600, 4, 0x2D1C, '1180:0117'),
                (11605, 2, 0x2D2B, '1180:0128'),
                (11607, 1, 0x2D3E, '1180:0139'),
                (11608, 4, 0x2D4E, '1180:014a')]
# hint trigger sites (from the call sites of FUN_1130_0000); descriptions are ours.
HINT_TRIGGERS = {
    0: '10e0:014b main loop, every pass (first ever play)',
    2: '10e0:0155 main loop, every pass after hint 0',
    3: '10e8:06f8 second clue word of a path obtained (DAT_1078 becomes 2)',
    4: '10e8:074a treasure found',
    5: '10e0:0a82 / 1168:08cb out of nets while coins > 0',
    6: '10e0:0abc / 1168:0905 out of coins while nets > 0',
    7: '10e0:0a86,0ad4 / 1168:08cf,091d out of coins and nets',
    8: '10e0:0d79 reached the lock without the key',
    9: '10e0:078e key found',
}


# ----------------------------------------------------------------------------------
# NE helpers
# ----------------------------------------------------------------------------------
class Exe:
    def __init__(self, path):
        d = open(path, 'rb').read()
        ne = struct.unpack_from('<I', d, 0x3C)[0]
        if d[ne:ne + 2] != b'NE':
            raise ValueError('not an NE file')
        autodata = struct.unpack_from('<H', d, ne + 0x0E)[0]
        segtab = ne + struct.unpack_from('<H', d, ne + 0x22)[0]
        shift = struct.unpack_from('<H', d, ne + 0x32)[0]
        off, ln, fl, mn = struct.unpack_from('<4H', d, segtab + (autodata - 1) * 8)
        self.d = d
        self.dg = off << shift
        self.dglen = ln
        self.autodata = autodata

    def u8(self, o, n):
        return list(self.d[self.dg + o:self.dg + o + n])

    def u16(self, o, n):
        return list(struct.unpack_from('<%dH' % n, self.d, self.dg + o))

    def i16(self, o, n):
        return list(struct.unpack_from('<%dh' % n, self.d, self.dg + o))

    def cstr(self, o):
        e = self.d.index(b'\0', self.dg + o)
        return self.d[self.dg + o:e].decode('latin-1')


# ----------------------------------------------------------------------------------
# Tokeniser (mirrors the dispatcher of the text engine FUN_10b0_00b5)
# ----------------------------------------------------------------------------------
def tokenize(p, issues):
    """Split payload bytes p (no NUL) into tokens. Every byte belongs to exactly one token."""
    toks = []
    i = 0
    n = len(p)
    while i < n:
        c = p[i]
        if c == CODE:
            j = p.find(b'\xff', i + 1)
            if j < 0:
                issues.append('unterminated FF code at +%d' % i)
                toks.append(('raw', p[i:]))
                break
            body = p[i + 1:j]
            op = body[0] if body else None
            if op not in OPNAMES:
                issues.append('unknown FF op %r at +%d' % (op, i))
            toks.append(('code', op, bytes(body[1:]), i, j + 1))
            i = j + 1
        elif c in (CR, LF):
            toks.append(('nl', c, i))
            i += 1
        elif c == TAB:
            toks.append(('tab', i))
            i += 1
        elif c == LMARGIN:
            toks.append(('lm', i))
            i += 1
        elif c >= 0x80 or c < 0x20:
            toks.append(('glyph', c, i))
            i += 1
        else:
            j = i
            while j < n and 0x20 <= p[j] < 0x7F:
                j += 1
            toks.append(('text', p[i:j].decode('ascii'), i))
            i = j
    return toks


def markup(p, issues=None):
    """Lossless, readable escape of payload bytes."""
    out = []
    for t in tokenize(p, issues if issues is not None else []):
        k = t[0]
        if k == 'text':
            out.append(t[1].replace('{', '{{').replace('}', '}}'))
        elif k == 'nl':
            out.append('\n' if t[1] == CR else '{LF}')
        elif k == 'tab':
            out.append('{tab}')
        elif k == 'lm':
            out.append('{lm}')
        elif k == 'glyph':
            out.append('{g:%02x}' % t[1])
        elif k == 'code':
            op, arg = t[1], t[2]
            name = OPNAMES.get(op, 'op%s' % ('?' if op is None else '%02x' % op))
            if op is None:                      # FF FF: empty code (2-byte no-op in the engine)
                out.append('{nop}')
            elif op == 6:
                out.append('{note:%s}' % arg.decode('latin-1'))
            elif op in (1, 3):
                out.append('{%s:%d}' % (name, arg[0]) + ('{%s}' % arg[1:].hex() if len(arg) > 1 else ''))
            elif arg:
                out.append('{%s:%s}' % (name, arg.hex()))
            else:
                out.append('{%s}' % name)
        else:
            out.append('{raw:%s}' % t[1].hex())
    return ''.join(out)


def unmarkup(s):
    """Inverse of markup() (used to prove the escape is lossless)."""
    out = bytearray()
    i = 0
    while i < len(s):
        ch = s[i]
        if s.startswith('{{', i):
            out += b'{'; i += 2; continue
        if s.startswith('}}', i):
            out += b'}'; i += 2; continue
        if ch == '\n':
            out.append(CR); i += 1; continue
        if ch == '{':
            j = s.index('}', i)
            tag = s[i + 1:j]
            i = j + 1
            name, _, arg = tag.partition(':')
            if name == 'LF':
                out.append(LF)
            elif name == 'tab':
                out.append(TAB)
            elif name == 'lm':
                out.append(LMARGIN)
            elif name == 'g':
                out.append(int(arg, 16))
            elif name == 'note':
                out += b'\xff\x06' + arg.encode('latin-1') + b'\xff'
            elif name in ('arg', 'color'):
                out += bytes([0xFF, NAME2OP[name], int(arg)])
                if s.startswith('{', i) and not s.startswith('{{', i) and re.match(r'\{[0-9a-f]+\}', s[i:]):
                    k = s.index('}', i)
                    out += bytes.fromhex(s[i + 1:k]); i = k + 1
                out.append(0xFF)
            elif name == 'raw':
                out += bytes.fromhex(arg)
            elif name == 'nop':
                out += b'\xff\xff'
            else:
                op = NAME2OP.get(name)
                if op is None:
                    op = int(name[2:], 16)
                out += bytes([0xFF, op]) + (bytes.fromhex(arg) if arg else b'') + b'\xff'
            continue
        out.append(ord(ch)); i += 1
    return bytes(out)


def plain(p):
    """Visible text only: codes dropped, newlines as '\n', glyphs as <xNN>, 0xFE as '|'."""
    out = []
    for t in tokenize(p, []):
        if t[0] == 'text':
            out.append(t[1])
        elif t[0] == 'nl':
            out.append('\n')
        elif t[0] == 'tab':
            out.append('\t')
        elif t[0] == 'lm':
            out.append('|')
        elif t[0] == 'glyph':
            out.append('<x%02x>' % t[1])
    return ''.join(out)


def emphasis(p):
    """Runs of text drawn in colour index 4 (FF 03 05 FF) and underlined runs."""
    col, ul = 0, False
    emph, cur, under, ucur = [], [], [], []
    for t in tokenize(p, []):
        if t[0] == 'code' and t[1] == 3:
            if col == 4 and cur:
                emph.append(''.join(cur)); cur = []
            col = t[2][0] - 1
        elif t[0] == 'code' and t[1] == 2:
            if ul and ucur:
                under.append(''.join(ucur)); ucur = []
            ul = not ul
        elif t[0] == 'text':
            if col == 4:
                cur.append(t[1])
            if ul:
                ucur.append(t[1])
        elif t[0] == 'nl':
            if cur:
                emph.append(''.join(cur)); cur = []
    if cur:
        emph.append(''.join(cur))
    if ucur:
        under.append(''.join(ucur))
    return [e for e in emph if e.strip()], under


# ----------------------------------------------------------------------------------
# Puzzler parser: exact re-implementation of FUN_1150_0901
# ----------------------------------------------------------------------------------
def parse_puzzler(payload, issues):
    buf = bytearray(payload)
    n = len(buf)
    i = 0
    seen = False          # local_a : visible content seen
    nl = 0                # local_8 : consecutive newlines after content (0..2)
    nlines = 0            # DAT_610e
    blanks = []           # local_24[] set entries
    term = None
    converted = []
    while i < n and term is None:
        c = buf[i]
        if c in (CR, LF):
            if not seen:
                buf[i] = 0x20
                converted.append(i)
            else:
                if nl == 1:
                    blanks.append(nlines)
                elif nl == 2:
                    buf[i] = 0
                    term = i
                if nl < 2:
                    nl += 1
                nlines += 1
        elif c == CODE:
            i += 1
            while i < n and buf[i] != CODE:
                i += 1
        else:
            seen = True
            nl = 0
        i += 1
    if term is None:
        issues.append('no question terminator (CR CR CR) found')
        return None
    ntext = nlines - 1 - len(blanks)          # DAT_610f
    # y-offset table DAT_6115 (in line units): non-blank line slots among the first 10
    ytab = [k for k in range(10) if k not in blanks]
    # choices
    j = term + 1
    while j < n and buf[j] in (CR, LF):
        j += 1
    starts = [j]
    k = j
    while k < n and len(starts) < 3:
        if buf[k] in (CR, LF):
            buf[k] = 0
            starts.append(k + 1)
        k += 1
    if len(starts) < 3:
        issues.append('fewer than 3 choices')
    choices = []
    for ci, s in enumerate(starts):
        e = s
        while e < n and buf[e] != 0:
            e += 1
        choices.append((s, e))
    # leftovers after the third choice: the game keeps them inside choice 3's string
    last_s, last_e = choices[-1]
    raw3 = bytes(buf[last_s:last_e])
    trail = b''
    if raw3.endswith(b'\r'):
        trail = b'\r'
        raw3 = raw3[:-1]
    leftover = b''
    m = re.search(rb'[\r\n]', raw3)
    if m:
        leftover = raw3[m.start():]
        issues.append('extra bytes after third choice: %r' % leftover)
    question = bytes(buf[:term])
    # split the question into display lines exactly as the renderer will
    lines = []
    cur = bytearray()
    q = question
    x = 0
    while x < len(q):
        c = q[x]
        if c == CODE:
            y = q.index(b'\xff', x + 1)
            cur += q[x:y + 1]
            x = y + 1
            continue
        if c in (CR, LF):
            lines.append(bytes(cur)); cur = bytearray()
        else:
            cur.append(c)
        x += 1
    lines.append(bytes(cur))
    return dict(buf=buf, term=term, nlines=nlines, ntext=ntext, blanks=blanks, ytab=ytab,
                converted=converted, question=question, lines=lines,
                choices=[bytes(buf[s:e]) for s, e in choices],
                choice3_trailing=trail, choice_offsets=choices, leftover=leftover)


def guess_category(qplain, choices):
    t = ' '.join(qplain.lower().split())
    ch = [c.lower().strip() for c in choices]
    numw = {'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten',
            'eleven', 'twelve', 'zero'}
    if 'take away the' in t or 'new letter' in t:
        return 'letter substitution'
    if 'rhym' in t:
        return 'rhyming words'
    if re.search(r'\b(short|long) [aeiou]\b', t):
        return 'vowel sounds'
    if re.search(r'start(s|ing)? (with|the same|that way|like)|starting sound|sounds? at the start|'
                 r'begin(s|ning)? (with|the same)', t):
        return 'beginning sounds'
    if re.search(r'end(s)? (with|the same|that way|like)|end sound|sounds? at the end', t):
        return 'ending sounds'
    if re.search(r'\b(is|are) to\b', t) and re.search(r'\bas\b', t):
        return 'analogies'
    if 'opposite' in t or 'silly game' in t or 'funny game' in t:
        return 'opposites'
    if 'compound' in t or re.search(r'[a-z] \+ [a-z]', t):
        return 'compound words'
    if all(re.fullmatch(r'[0-9,]+', c) or c in numw for c in ch) or \
            re.search(r'how (many|much)|cents|dime|nickel|quarter|plus|number', t):
        return 'numbers & arithmetic'
    if re.search(r'\bfirst\b', t) and re.search(r'\bwhat (do|did|should)\b', t):
        return 'sequencing'
    if all(c.endswith('est') for c in ch) or any(c in ('more than', 'less than', 'equal to', 'the same as') for c in ch):
        return 'comparisons'
    if re.search(r'what am i|what is it\?|thinking of|guessing game|who am i', t):
        return 'riddles'
    if re.search(r'\bnot\b|isn.t|doesn.t|same|like them|like these|belong', t):
        return 'classification'
    if '____' in qplain:
        return 'cloze (vocabulary / science / reasoning)'
    return 'questions (science / reasoning)'


# ----------------------------------------------------------------------------------
def load(res_dir, rid):
    b = open(os.path.join(res_dir, str(rid)), 'rb').read()
    n = b.find(b'\0')
    if n < 0:
        n = len(b)
    pad = b[n:]
    return b, b[:n], sum(1 for x in pad if x)


def sound_ids(sounds_dir):
    """Set of SSND resource ids present in <sounds_dir>/TM?SOUND.DLL/32513/<id> (or None)."""
    if not sounds_dir or not os.path.isdir(sounds_dir):
        return None
    ids = set()
    for d in os.listdir(sounds_dir):
        if re.fullmatch(r'TM\dSOUND\.DLL', d, re.I):
            sub = os.path.join(sounds_dir, d, '32513')
            if os.path.isdir(sub):
                ids.update(int(f) for f in os.listdir(sub) if f.isdigit())
    return ids or None


def byte_report(raw, payload, pad_nz, tokens_ok, extra=None):
    r = OrderedDict(resource_len=len(raw), payload_len=len(payload),
                    padding_len=len(raw) - len(payload), padding_nonzero=pad_nz,
                    lossless_markup=tokens_ok)
    if extra:
        r.update(extra)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--res', default=DEF_RES)
    ap.add_argument('--exe', default=DEF_EXE)
    ap.add_argument('--out', default=DEF_OUT)
    ap.add_argument('--sounds', default=DEF_SOUNDS,
                    help='dir holding TMnSOUND.DLL/32513/<id> (speech cross-check; optional)')
    a = ap.parse_args()
    snd = sound_ids(a.sounds)
    missing_total = 0
    # end of the puzzle speech block (exclusive): bounds the last puzzle's id span
    snd_end = (max(i for i in snd if PUZ_SPEECH_LO <= i <= PUZ_SPEECH_HI) + 1) if snd else None

    exe = None
    if a.exe and os.path.exists(a.exe):
        exe = Exe(a.exe)
    tables = OrderedDict()
    if exe:
        tables['level_start'] = exe.u16(DG_LEVEL_START, 5)
        speech_base = exe.u16(DG_SPEECH_BASE, PUZ_COUNT)
        voice = exe.u8(DG_VOICE, PUZ_COUNT)
        no_choice_speech = exe.u16(DG_NO_CHOICE_SPEECH, 16)
        hint_bit = exe.i16(DG_HINT_BIT, 34)
        tables['no_choice_speech'] = no_choice_speech
        tables['hint_bit'] = hint_bit
        tables['rank_start_hint'] = exe.i16(DG_RANK_START_HINT, 8)
        tables['rank_promotion_hint'] = exe.i16(DG_RANK_PROMO_HINT, 8)
        tables['rank_trip_done_hint'] = exe.i16(DG_RANK_DONE_HINT, 8)
    else:
        tables['level_start'] = [0, 63, 179, 302, 359]
        speech_base = voice = hint_bit = None
        no_choice_speech = []

    problems = []
    total_left = 0
    out = OrderedDict()
    out['source'] = 'TMT.DLL NE resources, type 32515 (Mac OTHR)'
    out['spec'] = 're/docs/fmt_othr_puzzles.md'
    out['exe_tables'] = tables if exe else None

    # ------------------------------------------------------------------ puzzlers
    ls = tables['level_start']
    puzzles = []
    cat_count = Counter()
    for idx in range(PUZ_COUNT):
        rid = PUZ_FIRST + idx
        raw, p, pad_nz = load(a.res, rid)
        issues = []
        mk = markup(p, issues)
        ok = unmarkup(mk) == p
        if not ok:
            issues.append('markup round-trip failed')
        pz = parse_puzzler(p, issues)
        level = next(L for L in range(4) if ls[L] <= idx < ls[L + 1])
        rec = OrderedDict(id=rid, index=idx, level=level + 1)
        notes = [t[2].decode('latin-1') for t in tokenize(p, []) if t[0] == 'code' and t[1] == 6]
        tag = None
        for nt in notes:
            m = re.fullmatch(r'L=(\d+)', nt)
            if m:
                tag = int(m.group(1))
        rec['level_tag'] = tag
        if tag != level + 1:
            issues.append('level tag %r disagrees with EXE level table (%d)' % (tag, level + 1))
        if pz is None:
            rec['issues'] = issues
            puzzles.append(rec)
            problems.append((rid, issues))
            continue
        q = pz['question']
        # leading control codes before the first visible character
        head = []
        for t in tokenize(p, []):          # original bytes: codes before the first non-code byte
            if t[0] != 'code':
                break
            head.append(markup(p[t[3]:t[4]]))
        rec['header_codes'] = head
        rec['centered'] = any(t[0] == 'code' and t[1] == 5 for t in tokenize(q, []))
        rec['question_markup'] = markup(q)            # as rendered (leading newlines -> spaces)
        rec['display_line_count'] = pz['nlines']      # DAT_610e
        rec['text_line_count'] = pz['ntext']          # DAT_610f
        rec['blank_line_indices'] = pz['blanks']
        rec['leading_newlines_converted'] = len(pz['converted'])
        base = speech_base[idx] if speech_base else None
        spoken = idx not in no_choice_speech
        lines = []
        k = 0
        for di, ln in enumerate(pz['lines'][:pz['nlines']]):
            if di in pz['blanks'] or di >= pz['nlines'] - 1:
                continue
            em, ul = emphasis(ln)
            lines.append(OrderedDict(display_index=di, markup=markup(ln), plain=plain(ln).strip(),
                                     emphasis=em, blank=bool(ul),
                                     speech_id=(base + k) if base is not None else None))
            k += 1
        if k != pz['ntext']:
            issues.append('line count mismatch %d/%d' % (k, pz['ntext']))
        rec['lines'] = lines
        chs = []
        for ci, cb in enumerate(pz['choices']):
            cbody = cb[:-1] if (ci == 2 and cb.endswith(b'\r')) else cb
            sid = (base + pz['ntext'] + ci) if (base is not None and spoken) else None
            chs.append(OrderedDict(index=ci, correct=(ci == 0), markup=markup(cbody),
                                   plain=plain(cbody), speech_id=sid))
        rec['choices'] = chs
        rec['correct_choice'] = 0
        qtext = plain(q)
        rec['category_guess'] = guess_category(qtext, [c['plain'] for c in chs])
        cat_count[(level + 1, rec['category_guess'])] += 1
        if base is not None:
            nxt = speech_base[idx + 1] if idx + 1 < PUZ_COUNT else snd_end
            cnt = pz['ntext'] + (3 if spoken else 0)
            span = (nxt - base) if nxt is not None else None
            sp = OrderedDict(base=base, ids_used=cnt, voice=voice[idx], choices_spoken=spoken,
                             ids_allocated=span)
            if snd is not None:
                used = [base + k for k in range(cnt)]
                miss = [s for s in used if s not in snd]
                sp['missing_sound_ids'] = miss
                if miss:
                    missing_total += len(miss)
                    issues.append('speech ids not found in the sound DLLs: %r' % miss)
            if span is not None:
                # allocated ids = text lines (+3 when choice recordings exist)
                sp['choice_recordings'] = span >= pz['ntext'] + 3
                if span < cnt:
                    issues.append('speech id span %d < ids used %d' % (span, cnt))
                elif span not in (pz['ntext'], pz['ntext'] + 3):
                    sp['note'] = 'span %d = lines %d + %d (extra trailing id)' % (
                        span, pz['ntext'], span - pz['ntext'])
            rec['speech'] = sp
        # byte accounting: question bytes + terminator + choices + separators; anything after
        # the third choice's first newline is not explained by the grammar -> leftover
        left = len(pz['leftover'])
        consumed = len(p) - left
        rec['bytes'] = byte_report(raw, p, pad_nz, ok, OrderedDict(
            question_len=pz['term'], terminator_offset=pz['term'],
            choice_offsets=[s for s, e in pz['choice_offsets']],
            choice3_trailing_cr=bool(pz['choice3_trailing']),
            consumed=consumed, leftover=left,
            leftover_hex=pz['leftover'].hex() if left else None))
        total_left += left + pad_nz
        if issues:
            rec['issues'] = issues
            problems.append((rid, issues))
        puzzles.append(rec)
    out['puzzlers'] = puzzles

    # ------------------------------------------------------------------ hints
    hints = []
    rank_names = ['trainee', '1-star', '2-star', '3-star', '4-star', '5-star', '6-star', 'champion']
    usage = {}
    if exe:
        for r, h in enumerate(tables['rank_start_hint']):
            usage.setdefault(h, []).append('trip start, rank %s (10e0:10b2)' % rank_names[r])
        for r, h in enumerate(tables['rank_promotion_hint']):
            if h >= 0:
                usage.setdefault(h, []).append('promotion to rank %s (10f0:07f5)' % rank_names[r])
        for r, h in enumerate(tables['rank_trip_done_hint']):
            usage.setdefault(h, []).append('trip ended, rank %s, no promotion (10f0:07f5)' % rank_names[r])
    for n in range(HINT_COUNT):
        rid = HINT_FIRST + n
        raw, p, pad_nz = load(a.res, rid)
        issues = []
        mk = markup(p, issues)
        ok = unmarkup(mk) == p
        if not ok:
            issues.append('markup round-trip failed')
        rec = OrderedDict(id=rid, index=n)
        if hint_bit:
            b = hint_bit[n]
            rec['show_once_bit'] = None if b < 0 else b
        rec['speech_id'] = 7100 + n
        if snd is not None:
            rec['speech_present'] = (7100 + n) in snd
            if not rec['speech_present']:
                missing_total += 1
                issues.append('hint speech id %d not found' % (7100 + n))
        trig = []
        if n in HINT_TRIGGERS:
            trig.append(HINT_TRIGGERS[n])
        trig += usage.get(n, [])
        rec['triggers'] = trig
        rec['markup'] = mk
        rec['lines'] = plain(p).split('\n')
        rec['glyphs'] = sorted({'%02x' % t[1] for t in tokenize(p, []) if t[0] == 'glyph'})
        rec['bytes'] = byte_report(raw, p, pad_nz, ok, OrderedDict(consumed=len(p), leftover=0))
        total_left += pad_nz
        if issues:
            rec['issues'] = issues
            problems.append((rid, issues))
        hints.append(rec)
    out['hints'] = hints

    # ------------------------------------------------------------------ help pages
    pages = []
    owner = {}
    for first, count, toff, entry in HELP_DIALOGS:
        title = exe.cstr(toff) if exe else None
        for k in range(count):
            # FUN_1180_0000: id = first + k (special case for 11605/k==1 yields the same id)
            owner[first + k] = (title, k, entry)
    for rid in range(HELP_FIRST, HELP_LAST + 1):
        raw, p, pad_nz = load(a.res, rid)
        issues = []
        mk = markup(p, issues)
        ok = unmarkup(mk) == p
        if not ok:
            issues.append('markup round-trip failed')
        rec = OrderedDict(id=rid)
        if rid in owner:
            t, k, e = owner[rid]
            rec['dialog'] = t
            rec['page'] = k
            rec['entry'] = e
        else:
            rec['dialog'] = None
            rec['page'] = None
            rec['note'] = 'not referenced by the Windows EXE (variant of 11606)'
        rec['markup'] = mk
        rec['lines'] = plain(p).split('\n')
        rec['glyphs'] = sorted({'%02x' % t[1] for t in tokenize(p, []) if t[0] == 'glyph'})
        rec['bytes'] = byte_report(raw, p, pad_nz, ok, OrderedDict(consumed=len(p), leftover=0))
        total_left += pad_nz
        if issues:
            rec['issues'] = issues
            problems.append((rid, issues))
        pages.append(rec)
    out['help_pages'] = pages

    # ------------------------------------------------------------------ summary
    lv = Counter(p['level'] for p in puzzles)
    summ = OrderedDict()
    summ['puzzlers_per_level'] = {str(k): v for k, v in sorted(lv.items())}
    summ['level_id_ranges'] = {str(L + 1): [PUZ_FIRST + ls[L], PUZ_FIRST + ls[L + 1] - 1] for L in range(4)}
    summ['category_guess_per_level'] = OrderedDict()
    for (L, c), v in sorted(cat_count.items()):
        summ['category_guess_per_level'].setdefault(c, OrderedDict())[str(L)] = v
    summ['max_display_lines'] = max(p.get('display_line_count', 0) for p in puzzles)
    summ['max_text_lines'] = max(p.get('text_line_count', 0) for p in puzzles)
    summ['puzzlers_with_internal_blank_line'] = [p['id'] for p in puzzles
                                                 if len(p.get('blank_line_indices', [])) > 1]
    summ['centered_puzzlers'] = sum(1 for p in puzzles if p.get('centered'))
    if exe:
        summ['voice_counts'] = {str(k): v for k, v in sorted(Counter(voice).items())}
    if snd is not None:
        # praise / wrong-answer phrases of each voice bank (1150:06ac, 1150:0891)
        bank_miss = [b + k for b in (7300, 7400, 7500) for k in range(8) if (b + k) not in snd]
        missing_total += len(bank_miss)
        summ['sound_check'] = OrderedDict(
            dir=a.sounds, puzzle_speech_end_exclusive=snd_end,
            voice_bank_phrases_missing=bank_miss, missing_ids_total=missing_total)
    else:
        summ['sound_check'] = None
    summ['unparsed_bytes_total'] = total_left
    summ['problems'] = [{'id': r, 'issues': i} for r, i in problems]
    out['summary'] = summ

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1, ensure_ascii=False)
    print('puzzlers %d, hints %d, help pages %d -> %s' % (len(puzzles), len(hints), len(pages), a.out))
    print('unparsed/leftover bytes: %d; records with issues: %d' % (total_left, len(problems)))
    if snd is not None:
        print('speech cross-check: %d referenced SSND ids missing' % missing_total)
    for r, i in problems:
        print('  %d: %s' % (r, '; '.join(i)))
    return 0 if not problems else 1


if __name__ == '__main__':
    sys.exit(main())
