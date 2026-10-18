"""DOS edition (SST.EXE, 1990): TUNES.IBM/.TAN and SFX.IBM/.TAN decoder and renderer.

The sound files hold event streams for the driver in SST.EXE segment 491b
(see re/dos/docs/fmt_sound.md).  This tool re-implements that driver tick by tick
(timer rate 1193182/5041 Hz) and renders:
  * .IBM files on a PC-speaker model (PIT channel 2 square wave, port 0x61 gate);
  * .TAN files on an SN76496 model (3 square-wave tone voices + noise voice,
    2 dB attenuation steps, volume envelopes taken from SST.EXE's DGROUP).

Usage (python -P):
  python -P tools/dos/dos_sound.py list   [--dos DIR]                 # entries per file
  python -P tools/dos/dos_sound.py dump   FILE [--out TXT]            # event listing
  python -P tools/dos/dos_sound.py render FILE [IDX] [--out WAV] [--loops N]
  python -P tools/dos/dos_sound.py all    [--outdir DIR]              # every tune+sfx, both forms
  python -P tools/dos/dos_sound.py adlib  [--out TXT]                 # AdLib instrument/orchestration tables

Default output: extracted/previews/dos/audio/.
"""
import argparse
import math
import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dos_text import load_image, dos_file, ROOT  # noqa: E402

DEFAULT_OUT = os.path.join(ROOT, 'extracted', 'previews', 'dos', 'audio')

PIT_HZ = 1193182.0               # 8253/8254 input clock (14.31818 MHz / 12)
TIMER_DIVISOR = 0x13B1           # 1000:03f8, INT 8 reprogrammed to 236.7 Hz
TICK_HZ = PIT_HZ / TIMER_DIVISOR
PSG_HZ = 3579545.0               # SN76496 clock on Tandy 1000 / PCjr
RATE = 44100

# DGROUP offsets of the driver's tables (observed in SST_unpacked.EXE)
DS_SPK_PERIODS = 0x6538          # 12 x u16 PIT divisors, C..B of the octave below C2... (C1=32.70 Hz)
DS_PSG_PERIODS = 0x6550          # 12 x u16 SN76496 periods (C2..B2)
DS_PERIOD_PTRS = 0x6568          # 2 x u16 near pointers -> the two tables above (index = device)
DS_PORT = 0x656C                 # u16 PSG/OPL port: 0x388 initial, 0xC0 Tandy, 0x205 PS/1
DS_ENV_TABLE = 0x656E            # 36 x u16 near pointers to attenuation envelopes (0xFF-terminated)
N_ENVELOPES = 36

SPEAKER, TANDY, ADLIB = 0, 1, 2  # DAT_4d94_63bc values (3 = PS/1 audio, mapped to TANDY @ 491b:06bd)

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


# ---------------------------------------------------------------------------
# File structure
# ---------------------------------------------------------------------------

def parse_file(data: bytes, voices: int):
    """Return list of entries; each entry = list of `voices` absolute offsets.

    The header is an array of u16 offsets (1 per entry for .IBM, 4 per entry for .TAN).
    Its length is the first offset (0x96 / 0x230).  The entry count is not stored:
    the game uses tune ids 0..2 and sfx ids 0..11.  Header slots after the real
    entries hold stale copies of another file's header, so we stop at the first
    slot that is zero, out of range, or not ascending."""
    hdr_len = struct.unpack_from('<H', data, 0)[0]
    words = struct.unpack_from('<%dH' % (hdr_len // 2), data, 0)
    entries = []
    last = 0
    for i in range(0, len(words) - voices + 1, voices):
        e = list(words[i:i + voices])
        if any(w == 0 or w >= len(data) or w < hdr_len for w in e) or e[0] < last:
            break
        if any(e[j] > e[j + 1] for j in range(voices - 1)):
            break
        entries.append(e)
        last = e[-1]
    return hdr_len, entries


def file_kind(name):
    n = os.path.basename(name).upper()
    return (TANDY if n.endswith('.TAN') else SPEAKER), ('TUNES' if n.startswith('TUNES') else 'SFX')


# ---------------------------------------------------------------------------
# Hardware models
# ---------------------------------------------------------------------------

def _sq_int(x):
    """Integral over one period-normalised square wave (+1 first half, -1 second half)."""
    f = x - math.floor(x)
    return f if f < 0.5 else 1.0 - f


class SquareOsc:
    """Box-filtered (per output sample) square oscillator with continuous phase."""

    def __init__(self):
        self.phase = 0.0

    def render(self, freq, amp, n, out, start):
        if amp == 0.0 or freq <= 0.0:
            # keep phase running so re-enabling is seamless
            self.phase += freq * n / RATE if freq > 0 else 0.0
            return
        d = freq / RATE
        ph = self.phase
        if d >= 0.5:  # above Nyquist: averages to ~0 anyway
            self.phase = ph + d * n
            return
        g0 = _sq_int(ph)
        inv = amp / d
        for i in range(start, start + n):
            ph += d
            g1 = _sq_int(ph)
            out[i] += (g1 - g0) * inv      # mean of the square over this sample
            g0 = g1
        self.phase = ph


class Speaker:
    """PIT channel 2 (mode 3 square wave) gated by port 0x61 bits 0-1."""

    def __init__(self):
        self.divisor = 0
        self.gate = 0
        self.osc = SquareOsc()
        self.log = []

    def set_divisor(self, v):
        self.divisor = v & 0xFFFF

    def port61_or(self, m):
        self.gate |= m

    def port61_and(self, m):
        self.gate &= m

    def render(self, n, out, start):
        on = (self.gate & 3) == 3
        div = self.divisor or 0x10000
        self.osc.render(PIT_HZ / div, 0.5 if on else 0.0, n, out, start)


class SN76496:
    """SN76496 / NCR 8496 model: 3 tone voices, 1 noise voice, 2 dB attenuation steps."""

    VOL = [10 ** (-2.0 * a / 20.0) for a in range(15)] + [0.0]

    def __init__(self):
        self.period = [0, 0, 0]
        self.att = [15, 15, 15, 15]
        self.noise_ctrl = 0
        self.latch = 0
        self.osc = [SquareOsc(), SquareOsc(), SquareOsc()]
        self.lfsr = 0x4000
        self.nphase = 0.0
        self.writes = 0

    def write(self, b):
        b &= 0xFF
        self.writes += 1
        if b & 0x80:
            self.latch = (b >> 4) & 7
            r, val = self.latch, b & 0x0F
            if r in (0, 2, 4):
                v = r >> 1
                self.period[v] = (self.period[v] & 0x3F0) | val
            elif r == 6:
                self.noise_ctrl = val & 7
                self.lfsr = 0x4000
            else:
                self.att[r >> 1] = val
        else:
            r = self.latch
            if r in (0, 2, 4):
                v = r >> 1
                self.period[v] = (self.period[v] & 0x00F) | ((b & 0x3F) << 4)
            elif r == 6:
                self.noise_ctrl = b & 7
                self.lfsr = 0x4000
            else:
                self.att[r >> 1] = b & 0x0F

    def tone_freq(self, v):
        p = self.period[v] or 0x400
        return PSG_HZ / (32.0 * p)

    def render(self, n, out, start):
        for v in range(3):
            self.osc[v].render(self.tone_freq(v), 0.25 * self.VOL[self.att[v]], n, out, start)
        amp = 0.25 * self.VOL[self.att[3]]
        nf = self.noise_ctrl & 3
        rate = self.tone_freq(2) if nf == 3 else PSG_HZ / (512 << nf)   # LFSR shifts per second
        step = rate / RATE
        white = self.noise_ctrl & 4
        ph = self.nphase
        lf = self.lfsr
        for i in range(start, start + n):
            ph += step
            while ph >= 1.0:
                ph -= 1.0
                if white:
                    fb = (lf ^ (lf >> 1)) & 1          # TI 15-bit LFSR, taps 0 and 1
                else:
                    fb = lf & 1                        # periodic: 15-step pulse
                lf = (lf >> 1) | (fb << 14)
            if amp:
                out[i] += amp if (lf & 1) else -amp
        self.nphase = ph
        self.lfsr = lf


# ---------------------------------------------------------------------------
# Driver re-implementation (SST.EXE segment 491b)
# ---------------------------------------------------------------------------

class Block:
    """One 0x78-byte driver state block (sfx block DS:6448, music block DS:64c0).

    Field names / DGROUP offsets of the working copy at DS:63ce:
      status 63ce, id 63d0, tempo 63d2, tempo_adj 63d4, ptr[4] 63d6, start[4] 63de,
      dur[4] 63e6, fixdur[4] 63ee, active[4] 63f6, legato[4] 63fe, release[4] 6406,
      env_start[4] 640e, env_ptr[4] 6416, env_done[4] 641e, sustain[4] 6426,
      newnote[4] 642e, rest[4] 6436, tie[4] 643e."""

    def __init__(self):
        self.status = 0
        self.id = 0xFFFF
        self.tempo = 0
        self.adj = 0
        z = lambda: [0, 0, 0, 0]  # noqa: E731
        self.ptr, self.start, self.dur, self.fixdur = z(), z(), z(), z()
        self.active, self.legato, self.release = z(), z(), z()
        self.env_start, self.env_ptr, self.env_done, self.sustain = z(), z(), z(), z()
        self.newnote, self.rest, self.tie = z(), z(), z()


class Driver:
    def __init__(self, img, data: bytes, device: int, nvoices: int):
        self.img = img
        self.data = data
        self.device = device
        self.nv = nvoices
        self.spk_tab = [img.ds_u16(DS_SPK_PERIODS + 2 * i) for i in range(12)]
        self.psg_tab = [img.ds_u16(DS_PSG_PERIODS + 2 * i) for i in range(12)]
        self.env_tab = [img.ds_u16(DS_ENV_TABLE + 2 * i) for i in range(N_ENVELOPES)]
        self.spk = Speaker()
        self.psg = SN76496()
        self.owned = [0, 0, 0, 0]   # DS:63c6, voices taken by an sfx (always 0 here)
        self.warnings = set()
        self.b = Block()
        self.adlib_events = []

    # -- hardware access, per voice (491b:0575 / 05b8 / 0607) --
    def note_on(self, v):
        if self.device == TANDY:
            pass  # 0x61 |= 0x60 (route SN76496 to the speaker) - done once at init
        elif self.device == ADLIB:
            self.adlib_events.append(('on', v, self.adlib_pitch[v]))
        elif not self.owned[v]:
            self.spk.port61_or(3)

    def note_off(self, v):
        if self.owned[v]:
            return
        if self.device == TANDY:
            self.psg.write(0x90 | (v << 5) | 0x0F)
            self.tone(v, 0)
        elif self.device == ADLIB:
            self.adlib_events.append(('off', v, 0))
        else:
            self.spk.port61_and(0xFC)

    def tone(self, v, ax):
        if self.owned[v]:
            return
        ax &= 0xFFFF
        if self.device == TANDY:
            self.psg.write(0x80 | (v << 5) | (ax & 0x0F))
            self.psg.write((ax >> 4) & 0xFF)
        elif self.device == ADLIB:
            self.adlib_pitch[v] = ax
        else:
            self.spk.set_divisor(ax)

    # -- 491b:02d8 duration --
    def set_dur(self, v, b1):
        b = self.b
        bx = (b.tempo + b.adj) & 0xFFFF
        if not (b1 & 0x80):
            bx >>= (b1 & 7)
            if b1 & 0x08:
                bx += bx >> 1
            if b1 & 0x10:
                if b.tie[v] != 3:
                    b.tie[v] = 3 if b.tie[v] == 1 else b.tie[v] + 1
            elif b.tie[v] != 0:
                b.tie[v] += 1
        else:
            self.warnings.add('duration byte with bit 7 set (driver uses the whole tempo value)')
        b.dur[v] = bx & 0xFFFF
        b.newnote[v] = 1

    # -- 491b:025d pitch --
    def pitch(self, v, octv, note):
        if self.device == TANDY:
            if v == 3:
                self.psg.write(0xE0 + note - 1)
            else:
                o = octv - 1 if octv else 0
                ax = self.psg_tab[note - 1] >> o
                if ax > 0x3FF:
                    ax >>= 1
                self.tone(v, ax)
        elif self.device == ADLIB:
            self.tone(v, (note - 1 + octv * 12) & 0xFF)
            self.note_on(v)
        else:
            self.tone(v, self.spk_tab[note - 1] >> octv)
            self.note_on(v)

    # -- 491b:03fe commands (event low nibble 0xD) --
    def command(self, v, cmd, val):
        b = self.b
        if cmd == 0:
            if val == 0:
                b.legato[v] = 1
            else:
                b.legato[v] = 0
                b.release[v] = val
        elif cmd <= 2:
            if val == 0:
                b.adj = 0
            else:
                q, r = divmod(b.tempo * val, 100)
                if r > 0x31:
                    q += 1
                b.adj = (q if cmd == 1 else -q) & 0xFFFF
        elif cmd == 3:
            b.fixdur[v] = val
        elif cmd == 4:
            b.tempo = val * 4
        elif cmd == 5:
            if val >= N_ENVELOPES:
                self.warnings.add('envelope index %d outside the 36-entry table' % val)
                b.env_start[v] = self.img.ds_u16(DS_ENV_TABLE + 2 * val)
            else:
                b.env_start[v] = self.env_tab[val]
        elif cmd == 6:
            b.sustain[v] = val
        else:
            self.warnings.add('unknown command %d' % cmd)

    # -- 491b:04ce raw-pitch event (event low nibble 0xE) --
    def raw(self, v, octv, val):
        b = self.b
        if val == 0:
            self.note_off(v)
            b.rest[v] = 1
        elif self.device == TANDY:
            bx = ((val * 3) >> 2) + 1
            cl = octv or 1
            dx = 0xD70 >> cl
            if cl < 4:
                bx = (bx << (4 - cl)) & 0xFFFF
            elif cl > 4:
                bx >>= (cl - 4)
            dx = (dx - bx) & 0xFFFF
            if dx < 0x8000 and dx > 0x400:
                dx >>= 1
            self.tone(v, dx)
            b.newnote[v] = 1
            b.rest[v] = 0
        elif self.device == ADLIB:
            b.newnote[v] = 1
        else:
            dx = (-(val << 7)) & 0xFFFF
            base = self.img.ds_u16(self.img.ds_u16(DS_PERIOD_PTRS + 2 * self.device))
            ax = ((base + dx) & 0xFFFF) >> octv
            self.tone(v, ax)
            self.note_on(v)
        b.dur[v] = b.fixdur[v]

    # -- 491b:01ee event fetch --
    def fetch(self, v):
        b = self.b
        d = self.data
        while True:
            p = b.ptr[v]
            b0, b1 = d[p], d[p + 1]
            octv, note = b0 >> 4, b0 & 0x0F
            if note == 0:
                self.note_off(v)
                self.set_dur(v, b1)
                b.rest[v] = 1
            elif note == 0x0D:
                self.command(v, octv, b1)
            elif note == 0x0E:
                self.raw(v, octv, b1)
            elif note == 0x0F:
                b.active[v] = 0
                self.note_off(v)
                return
            else:
                self.pitch(v, octv, note)
                self.set_dur(v, b1)
                b.rest[v] = 0
            b.ptr[v] = p + 2
            if b.dur[v] != 0:
                return

    # -- 491b:0136 voice init --
    def init(self):
        b = self.b
        b.status = 2
        b.adj = 0
        for v in range(self.nv):
            b.active[v] = 0
            if self.data[b.ptr[v]] != 0xFF:
                b.active[v] = 2
                b.dur[v] = 0
                b.fixdur[v] = 1
                b.legato[v] = 0
                b.rest[v] = 0
                b.tie[v] = 0
                b.release[v] = 6

    # -- 491b:0656 stop --
    def stop(self):
        b = self.b
        b.status = 0
        b.id = 0xFFFF
        for v in range(self.nv):
            b.active[v] = 0
            self.note_off(v)

    # -- 491b:033d Tandy attenuation / envelope pass --
    def volumes(self):
        b = self.b
        for v in range(4):
            bl = 0x90 + 0x20 * v
            if self.owned[v] or not b.active[v]:
                continue
            go_env = False
            if b.newnote[v] == 1:
                b.newnote[v] = 0
                if b.tie[v] < 2:
                    b.env_done[v] = 0
                    b.env_ptr[v] = b.env_start[v]
                    if b.env_start[v] == 0:
                        self.warnings.add('voice %d plays without an envelope (reads DS:0000)' % v)
                elif b.tie[v] != 3:
                    b.tie[v] = 0
                go_env = True
            elif b.rest[v] == 1:
                self.psg.write(0x0F + bl)
                continue
            elif b.tie[v] in (1, 3) or b.legato[v] == 1:
                go_env = True
            elif _s16(b.dur[v]) <= _s16(b.release[v]):
                self.psg.write(0x0F + bl)
                continue
            else:
                go_env = True
            if go_env:
                if b.env_done[v] == 0:
                    al = self.img.ds_u8(b.env_ptr[v])
                    b.env_ptr[v] += 1
                    if al == 0xFF:
                        b.env_done[v] = 1
                        continue
                    self.psg.write((al + bl) & 0xFF)
                elif b.env_done[v] == 1:
                    self.psg.write((b.sustain[v] + bl) & 0xFF)
                else:
                    self.psg.write(0x0F + bl)

    # -- 491b:0181 per-tick voice pass --
    def process(self):
        b = self.b
        for v in range(self.nv):
            if not b.active[v]:
                continue
            if b.dur[v] == 0:
                self.fetch(v)
                if not b.active[v]:
                    continue
            b.dur[v] = (b.dur[v] - 1) & 0xFFFF
            if self.device != TANDY and b.legato[v] != 1 and b.dur[v] == ((b.release[v] - 1) & 0xFFFF):
                self.note_off(v)
        if not any(b.active):
            self.stop()
        elif self.device == TANDY:
            self.volumes()

    # -- 491b:00be one tick of one block --
    def tick(self, loop=False):
        b = self.b
        if b.status != 2:
            self.init()
        self.process()
        # 491b:00e7: a stopped block with its loop flag set restarts in the same tick
        while b.status == 0 and loop:
            self.loop_count += 1
            b.ptr = list(b.start)
            self.init()
            self.process()

    def start(self, offsets, ident=0):
        """491b:0791 (tune) / 491b:06d7 (sfx): set pointers, status = 1."""
        self.b = Block()
        self.b.id = ident
        self.b.ptr = list(offsets) + [0] * (4 - len(offsets))
        self.b.start = list(self.b.ptr)
        self.b.status = 1
        self.adlib_pitch = [0, 0, 0, 0]
        self.loop_count = 0


def _s16(x):
    x &= 0xFFFF
    return x - 0x10000 if x & 0x8000 else x


def render(img, data, offsets, device, loops=1, max_seconds=600):
    nv = len(offsets)
    drv = Driver(img, data, device, nv)
    drv.start(offsets)
    acc = 0.0
    ticks = 0
    max_ticks = int(max_seconds * TICK_HZ)
    buf = []
    while ticks < max_ticks:
        # loop like the game's music (flag DS:63ba) until `loops` passes have played
        drv.tick(loop=drv.loop_count < loops - 1)
        ticks += 1
        acc += RATE / TICK_HZ
        n = int(acc)
        acc -= n
        seg = [0.0] * n
        if device == TANDY:
            drv.psg.render(n, seg, 0)
        else:
            drv.spk.render(n, seg, 0)
        buf.extend(seg)
        if drv.b.status == 0:
            break
    # short tail so the last note is not cut by the player
    buf.extend([0.0] * int(RATE * 0.05))
    return buf, ticks, drv


def write_wav(path, samples):
    peak = max(1e-9, max(abs(s) for s in samples)) if samples else 1.0
    gain = min(1.0, 0.9 / peak) * 32767
    frames = bytearray()
    for s in samples:
        v = int(round(s * gain))
        frames += struct.pack('<h', max(-32768, min(32767, v)))
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))


# ---------------------------------------------------------------------------
# Event listing
# ---------------------------------------------------------------------------

CMD_NAMES = {0: 'release', 1: 'tempo+%', 2: 'tempo-%', 3: 'rawdur', 4: 'tempo', 5: 'envelope', 6: 'sustain'}


def describe_event(b0, b1, voice, device):
    octv, note = b0 >> 4, b0 & 15
    if note == 0:
        return 'rest      %s' % dur_str(b1)
    if note == 0x0D:
        n = CMD_NAMES.get(octv, 'cmd%d' % octv)
        extra = ''
        if octv == 4:
            extra = '  (= %d ticks per whole note)' % (b1 * 4)
        if octv == 0 and b1 == 0:
            extra = '  (legato)'
        return 'cmd %-9s %d%s' % (n, b1, extra)
    if note == 0x0E:
        return 'raw  oct %d val %d' % (octv, b1)
    if note == 0x0F:
        return 'end'
    if device == TANDY and voice == 3:
        nm = ('periodic', 'periodic', 'periodic', 'periodic', 'white', 'white', 'white', 'white')[(note - 1) & 7]
        rate = ('/512', '/1024', '/2048', 'tone3')[(note - 1) & 3]
        return 'noise %-8s %-6s %s' % (nm, rate, dur_str(b1))
    return 'note %-3s%d  %s' % (NOTE_NAMES[note - 1], octv + 1, dur_str(b1))


def dur_str(b1):
    if b1 & 0x80:
        return 'dur=tempo (bit7) %02x' % b1
    s = '1/%d' % (1 << (b1 & 7))
    if b1 & 8:
        s += '.'
    if b1 & 0x10:
        s += ' tie'
    return s


def dump(name, data, img=None, out=None):
    device, kind = file_kind(name)
    nv = 4 if device == TANDY else 1
    hdr_len, entries = parse_file(data, nv)
    lines = ['%s: %d bytes, header %#x bytes (%d slots), %d entries, %d voice(s) per entry'
             % (os.path.basename(name), len(data), hdr_len, hdr_len // (2 * nv), len(entries), nv)]
    for i, e in enumerate(entries):
        lines.append('')
        lines.append('== %s %d  voice offsets %s' % (kind.lower(), i, ' '.join('%#06x' % x for x in e)))
        for v, off in enumerate(e):
            lines.append('  -- voice %d @ %#06x' % (v, off))
            p = off
            while p + 1 < len(data):
                b0, b1 = data[p], data[p + 1]
                if b0 == 0xFF:
                    lines.append('    %04x: ff %02x  end%s' % (p, b1, '  (voice unused)' if p == off else ''))
                    break
                lines.append('    %04x: %02x %02x  %s' % (p, b0, b1, describe_event(b0, b1, v, device)))
                p += 2
                if b0 & 0x0F == 0x0F:
                    break
        if img is not None:
            _, ticks, drv = render(img, data, e, device, loops=1)
            lines.append('  length: %d ticks = %.2f s' % (ticks, ticks / TICK_HZ))
            for w in sorted(drv.warnings):
                lines.append('  warning: %s' % w)
    text = '\n'.join(lines) + '\n'
    if out:
        open(out, 'w', encoding='ascii').write(text)
    return text


# ---------------------------------------------------------------------------
# AdLib tables (segment 4ac9) - listing only, no OPL2 rendering
# ---------------------------------------------------------------------------

DS_OPL_INSTR_PTRS = 0x88BC   # 24 far pointers to instrument records (stride 0x4E in practice)
N_OPL_INSTR = 24
DS_OPL_ORCH = (0x891C, 0x8988, 0x89F4)   # 12 rows x 9: instrument / transpose / third value
DS_OPL_SLOT_MAP = 0x8124     # 9 x (modulator slot, carrier slot)
DS_OPL_SLOT_OFS = 0x8136     # 18 operator register offsets


def dump_adlib(img):
    L = ['AdLib tables in DGROUP (see re/dos/docs/fmt_sound.md section 5.5)', '']
    L.append('Orchestrations (4ac9:0002): row = 4*tune + (rand() & 3); per OPL channel 0..8')
    for r in range(12):
        ins = list(img.ds(DS_OPL_ORCH[0] + 9 * r, 9))
        tr = [x - 256 if x > 127 else x for x in img.ds(DS_OPL_ORCH[1] + 9 * r, 9)]
        th = list(img.ds(DS_OPL_ORCH[2] + 9 * r, 9))
        L.append('  tune %d variant %d: instr %s  transpose %s  third %s' % (r // 4, r % 4, ins, tr, th))
    L.append('')
    L.append('Channel -> (modulator, carrier) slot (DS:8124): %s'
             % [tuple(img.ds(DS_OPL_SLOT_MAP + 2 * c, 2)) for c in range(9)])
    L.append('Slot register offsets (DS:8136): %s' % list(img.ds(DS_OPL_SLOT_OFS, 18)))
    L.append('')
    L.append('Instruments: 13 x s16 modulator params @+0x00, 13 x s16 carrier params @+0x1A, '
             'wave select s16 @+0x34 / +0x36 (4ac9:019a, 4ac9:0847)')
    for i in range(N_OPL_INSTR):
        off, seg = struct.unpack_from('<HH', img.image, img.dgroup + DS_OPL_INSTR_PTRS + 4 * i)
        rec = img.far(seg, off, 0x38)
        v = struct.unpack('<28h', rec)
        L.append('  %2d @%04x:%04x  mod %s  car %s  wave %d/%d' % (i, seg, off, list(v[0:13]), list(v[13:26]), v[26], v[27]))
    return chr(10).join(L) + chr(10)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

FILES = ['TUNES.IBM', 'TUNES.TAN', 'SFX.IBM', 'SFX.TAN']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['list', 'dump', 'render', 'all', 'adlib'])
    ap.add_argument('file', nargs='?')
    ap.add_argument('index', nargs='?', type=int)
    ap.add_argument('--dos', default=None, help='TM-DOS directory')
    ap.add_argument('--exe', default=None, help='unpacked SST.EXE')
    ap.add_argument('--out', default=None)
    ap.add_argument('--outdir', default=DEFAULT_OUT)
    ap.add_argument('--loops', type=int, default=1)
    a = ap.parse_args()
    img = load_image(a.exe, a.dos)

    if a.cmd == 'adlib':
        text = dump_adlib(img)
        out = a.out or os.path.join(a.outdir, 'adlib_tables.txt')
        os.makedirs(os.path.dirname(out), exist_ok=True)
        open(out, 'w', encoding='ascii').write(text)
        print(text, end='')
        return
    if a.cmd == 'list':
        for fn in FILES:
            data = dos_file(fn, a.dos)
            dev, kind = file_kind(fn)
            hdr, ents = parse_file(data, 4 if dev == TANDY else 1)
            print('%-10s %5d bytes  header %#x  %d entries' % (fn, len(data), hdr, len(ents)))
            for i, e in enumerate(ents):
                _, ticks, drv = render(img, data, e, dev)
                print('   %2d  %-28s %6d ticks  %6.2f s  %s' % (
                    i, ' '.join('%04x' % x for x in e), ticks, ticks / TICK_HZ, '; '.join(sorted(drv.warnings))))
        return
    if a.cmd == 'dump':
        path = a.file if os.path.exists(a.file) else os.path.join(a.dos or os.path.join(ROOT, 'TM-DOS'), a.file)
        print(dump(path, open(path, 'rb').read(), img, a.out), end='')
        return
    if a.cmd == 'render':
        path = a.file if os.path.exists(a.file) else os.path.join(a.dos or os.path.join(ROOT, 'TM-DOS'), a.file)
        data = open(path, 'rb').read()
        dev, kind = file_kind(path)
        _, ents = parse_file(data, 4 if dev == TANDY else 1)
        idx = a.index or 0
        samples, ticks, drv = render(img, data, ents[idx], dev, loops=a.loops)
        out = a.out or os.path.join(a.outdir, '%s_%02d.wav' % (os.path.basename(path).replace('.', '_').lower(), idx))
        write_wav(out, samples)
        print('%s: %d ticks, %.2f s -> %s' % (path, ticks, ticks / TICK_HZ, out))
        return
    if a.cmd == 'all':
        os.makedirs(a.outdir, exist_ok=True)
        for fn in FILES:
            data = dos_file(fn, a.dos)
            dev, kind = file_kind(fn)
            _, ents = parse_file(data, 4 if dev == TANDY else 1)
            dump(fn, data, img, os.path.join(a.outdir, fn.replace('.', '_').lower() + '.txt'))
            for i, e in enumerate(ents):
                samples, ticks, drv = render(img, data, e, dev, loops=a.loops)
                out = os.path.join(a.outdir, '%s_%02d.wav' % (fn.replace('.', '_').lower(), i))
                write_wav(out, samples)
                print('%-10s %2d  %6.2f s  %s%s' % (fn, i, ticks / TICK_HZ, os.path.basename(out),
                                                   ('  [' + '; '.join(sorted(drv.warnings)) + ']') if drv.warnings else ''))


if __name__ == '__main__':
    main()
