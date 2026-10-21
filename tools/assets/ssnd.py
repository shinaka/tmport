"""Decoder for Treasure Mountain! (Windows CD) SSND sound resources.

SSND resources live in TM0SOUND.DLL .. TM8SOUND.DLL (Win16 NE, resource-only).
They are stored under the numeric NE resource type 0xFF01 (shown as 32513 by
the engine's tools/ne_resources.py) and mapped to the Mac-style type name "SSND" by the
DLL's RT_NAMETABLE (type 15) resource.  See re/docs/fmt_sound.md.

Payload layout (all little-endian):
    +0  u32  reserved      always 0 in all 2861 shipped sounds (ignored by the EXE)
    +4  u32  sample_rate   always 22050 (EXE passes it as nSamplesPerSec)
    +8  u32  num_samples   number of PCM bytes that follow (EXE uses it as buffer length)
    +12 u8[num_samples]    8-bit unsigned mono PCM, 0x80 = silence
The NE resource is padded with zero bytes up to the DLL's 512-byte alignment.

Library use:
    import ssnd
    for sid, snd in ssnd.iter_dll(r'...\\TM2SOUND.DLL'):
        wav_bytes = snd.to_wav()

CLI:
    python -P ssnd.py [--game DIR] [--out DIR] [--only TMnSOUND.DLL ...] [--manifest FILE]
Exports every sound to <out>/<DLL>/<id>.wav and prints a summary.
"""
from __future__ import annotations

import argparse
import io
import json
import os
import struct
import sys
import wave
from dataclasses import dataclass
from typing import Iterator, List, Optional, Tuple

DEFAULT_GAME = r'C:\Git\TM\extracted\wincd\TMTWINCD'
DEFAULT_OUT = r'C:\Git\TM\extracted\previews\wav'
SOUND_DLLS = [f'TM{n}SOUND.DLL' for n in range(9)]
SSND_NE_TYPE = 0xFF01  # integer resource type used for SSND payloads
HEADER_SIZE = 12

# Sound-id -> DLL routing exactly as performed by the EXE (1038:0ba8).
# (lo inclusive, hi exclusive, dll)
ID_ROUTING: List[Tuple[int, int, str]] = [
    (7000, 7100, 'TM0SOUND.DLL'),
    (7100, 8000, 'TM1SOUND.DLL'),
    (4000, 4400, 'TM2SOUND.DLL'),
    (4400, 4800, 'TM3SOUND.DLL'),
    (4800, 5200, 'TM4SOUND.DLL'),
    (5200, 5600, 'TM5SOUND.DLL'),
    (5600, 6000, 'TM6SOUND.DLL'),
    (6000, 6400, 'TM7SOUND.DLL'),
    (6400, 6800, 'TM8SOUND.DLL'),
]


def dll_for_sound_id(sid: int) -> Optional[str]:
    """Return the DLL the EXE would search for sound `sid` (None = unmapped)."""
    for lo, hi, dll in ID_ROUTING:
        if lo <= sid < hi:
            return dll
    return None


class SSNDError(ValueError):
    pass


@dataclass
class SSND:
    sid: int
    reserved: int
    sample_rate: int
    samples: bytes          # 8-bit unsigned PCM, mono
    padding: int = 0        # bytes after the samples inside the NE resource

    @property
    def duration(self) -> float:
        return len(self.samples) / float(self.sample_rate) if self.sample_rate else 0.0

    def to_wav(self) -> bytes:
        buf = io.BytesIO()
        with wave.open(buf, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(1)  # WAV 8-bit PCM is unsigned, same as SSND
            w.setframerate(self.sample_rate)
            w.writeframes(self.samples)
        return buf.getvalue()


def parse_ssnd(data: bytes, sid: int = -1, strict: bool = True) -> SSND:
    """Parse one SSND payload (raw NE resource bytes, padding allowed)."""
    if len(data) < HEADER_SIZE:
        raise SSNDError(f'sound {sid}: resource too short ({len(data)} bytes)')
    reserved, rate, count = struct.unpack_from('<III', data, 0)
    if count > len(data) - HEADER_SIZE:
        raise SSNDError(f'sound {sid}: sample count {count} exceeds resource size {len(data)}')
    if strict:
        if rate == 0 or rate > 192000:
            raise SSNDError(f'sound {sid}: implausible sample rate {rate}')
    samples = bytes(data[HEADER_SIZE:HEADER_SIZE + count])
    return SSND(sid, reserved, rate, samples, len(data) - HEADER_SIZE - count)


# ---------------------------------------------------------------- NE parsing

def _ne_resources(d: bytes) -> Iterator[Tuple[int, int, int, int]]:
    """Yield (type_id, res_id, file_offset, length) for integer-typed/-named resources."""
    if d[:2] != b'MZ':
        raise SSNDError('not an MZ executable')
    ne = struct.unpack_from('<I', d, 0x3C)[0]
    if d[ne:ne + 2] != b'NE':
        raise SSNDError('not an NE executable')
    base = ne + struct.unpack_from('<H', d, ne + 0x24)[0]
    shift = struct.unpack_from('<H', d, base)[0]
    p = base + 2
    while True:
        tid = struct.unpack_from('<H', d, p)[0]
        if tid == 0:
            break
        count = struct.unpack_from('<H', d, p + 2)[0]
        p += 8
        for _ in range(count):
            off, ln, _flags, rid = struct.unpack_from('<HHHH', d, p)
            p += 12
            if tid & 0x8000 and rid & 0x8000:
                yield tid, rid & 0x7FFF, off << shift, ln << shift


def _ne_nametable(d: bytes) -> dict:
    """Parse RT_NAMETABLE (type 15): {(type_ordinal, id_ordinal): (type_name, id_name)}."""
    out = {}
    for tid, rid, off, ln in _ne_resources(d):
        if tid != 0x800F:
            continue
        p, end = off, off + ln
        while p + 6 <= end:
            size, typ, iid = struct.unpack_from('<HHH', d, p)
            if size == 0:
                break
            names = d[p + 6:p + size].split(b'\0')
            tname = names[0].decode('latin-1') if names else ''
            iname = names[1].decode('latin-1') if len(names) > 1 else ''
            out[(typ, iid)] = (tname, iname)
            p += size
    return out


def iter_dll(path: str, strict: bool = True) -> Iterator[Tuple[int, SSND]]:
    """Yield (sound_id, SSND) for every SSND resource of one sound DLL, sorted by id."""
    with open(path, 'rb') as f:
        d = f.read()
    names = _ne_nametable(d)
    items = []
    for tid, rid, off, ln in _ne_resources(d):
        # raw NE type word is 0xFF01; the name table maps (0xFF01, id) -> ("SSND", "")
        tname = names.get((tid, rid), (None,))[0]
        if tid == SSND_NE_TYPE or tname == 'SSND':
            items.append((rid, off, ln))
    for rid, off, ln in sorted(items):
        yield rid, parse_ssnd(d[off:off + ln], rid, strict)


def load_sound(game_dir: str, sid: int) -> SSND:
    """Load a sound by id the way the game does (route id -> DLL, then find resource)."""
    dll = dll_for_sound_id(sid)
    if dll is None:
        raise SSNDError(f'sound id {sid} is outside every DLL range')
    for rid, snd in iter_dll(os.path.join(game_dir, dll)):
        if rid == sid:
            return snd
    raise SSNDError(f'sound id {sid} not present in {dll}')


# ---------------------------------------------------------------- CLI

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='Export Treasure Mountain SSND sounds to WAV')
    ap.add_argument('--game', default=DEFAULT_GAME, help='directory containing TMnSOUND.DLL')
    ap.add_argument('--out', default=DEFAULT_OUT, help='output root (<out>/<DLL>/<id>.wav)')
    ap.add_argument('--only', nargs='*', help='restrict to these DLL names')
    ap.add_argument('--manifest', help='write a JSON manifest of all sounds here')
    ap.add_argument('--no-wav', action='store_true', help='parse and report only')
    a = ap.parse_args(argv)

    dlls = a.only or SOUND_DLLS
    manifest = []
    total = fails = 0
    grand = 0.0
    print(f'{"DLL":14} {"count":>5} {"ids":>11} {"min s":>6} {"max s":>6} {"total s":>8} {"fail":>4}')
    for dll in dlls:
        path = os.path.join(a.game, dll)
        n = 0
        bad = 0
        durs = []
        ids = []
        try:
            it = list(iter_dll(path))
        except Exception as e:  # whole-file failure
            print(f'{dll:14} ERROR {e}')
            fails += 1
            continue
        outdir = os.path.join(a.out, dll)
        if not a.no_wav:
            os.makedirs(outdir, exist_ok=True)
        for sid, snd in it:
            try:
                if not a.no_wav:
                    with open(os.path.join(outdir, f'{sid}.wav'), 'wb') as f:
                        f.write(snd.to_wav())
                n += 1
                durs.append(snd.duration)
                ids.append(sid)
                routed = dll_for_sound_id(sid)
                manifest.append({'dll': dll, 'id': sid, 'rate': snd.sample_rate,
                                 'samples': len(snd.samples), 'seconds': round(snd.duration, 4),
                                 'reserved': snd.reserved, 'padding': snd.padding,
                                 'routes_to_same_dll': routed == dll})
                if routed != dll:
                    print(f'  warning: {dll} id {sid} would be routed to {routed} by the EXE')
            except Exception as e:
                bad += 1
                print(f'  {dll} id {sid}: {e}')
        total += n
        fails += bad
        grand += sum(durs)
        if durs:
            print(f'{dll:14} {n:5} {min(ids):5}-{max(ids):5} {min(durs):6.2f} {max(durs):6.2f} '
                  f'{sum(durs):8.1f} {bad:4}')
        else:
            print(f'{dll:14} {n:5} {"-":>11}')
    print(f'TOTAL {total} sounds, {grand / 60:.1f} min, {fails} failures'
          + ('' if a.no_wav else f'; WAVs in {a.out}'))
    if a.manifest:
        with open(a.manifest, 'w') as f:
            json.dump(manifest, f, indent=1)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
