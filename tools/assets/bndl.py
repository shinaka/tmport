#!/usr/bin/env python3
"""BNDL ("bundle") resource decoder for Super Solvers Treasure Mountain! (Windows CD).

A BNDL resource (TMT.DLL, Win type 32513, Mac type 'BNDL', ids 300..899) is a list of
resource ids that the game loads as one unit; the in-memory bundle then holds far
pointers to each loaded member, which the game indexes by position.

On-disk layout (all little-endian, see re/docs/fmt_bndl_ffnt.md):

    +0  char[4]  magic  "BNDL"
    +4  u32      count  (EXE reads only the low u16, 1038:0088)
    +8  u32[count] ids  (EXE reads only the low u16 of each, 1038:076f)
    ... zero padding to the 512-byte NE resource alignment

The type of each member id is implied by its numeric range (EXE 1038:038a):

    id <  900  BNDL (nested bundle)      TMT.DLL     type 32513
    id <  950  RRGB (palette)            TMT.DLL     type 32516
    id < 1000  FFNT (font)               TMT.DLL     type 32514
    id < 4000  PMAP (pixmap)             TMT256.DLL / TMT16.DLL type 32513
    id < 10000 SSND (sound)              TM0..TM8SOUND.DLL type 32513 (by sub-range)
    else       OTHR (misc data)          TMT.DLL     type 32515

Usage:
    python -P bndl.py RES_DIR [--id N] [--json OUT.json] [--recursive] [--dam OTHRFILE]

RES_DIR is the extracted resource root (extracted/res) containing <DLL>/<type>/<id>.
Importable: parse_bndl(), type_for_id(), resource_path(), load_bundle_tree().
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys

MAGIC = b"BNDL"

# (lo, hi_inclusive, dll) for SSND ids -- mirrors the if-chain in 1038:03da / 1038:00c9,
# which selects the DLL instance handle stored at DGROUP 0x09de..0x09ee.
SOUND_RANGES = [
    (7000, 7099, "TM0SOUND.DLL"),   # DGROUP 0x09de
    (7100, 7999, "TM1SOUND.DLL"),   # 0x09e0
    (4000, 4399, "TM2SOUND.DLL"),   # 0x09e2
    (4400, 4799, "TM3SOUND.DLL"),   # 0x09e4
    (4800, 5199, "TM4SOUND.DLL"),   # 0x09e6
    (5200, 5599, "TM5SOUND.DLL"),   # 0x09e8
    (5600, 5999, "TM6SOUND.DLL"),   # 0x09ea
    (6000, 6399, "TM7SOUND.DLL"),   # 0x09ec
    (6400, 6799, "TM8SOUND.DLL"),   # 0x09ee
]

WIN_TYPE = {"BNDL": 32513, "FFNT": 32514, "OTHR": 32515, "RRGB": 32516,
            "PMAP": 32513, "SSND": 32513}


# Where the EXE loads each bundle (observed in the Ghidra export; see the doc's catalogue).
# Values: (loader call site, holder / consumer, role).  Bundles absent here are not
# referenced by a constant in the analysed code.
KNOWN_USES = {
    300: ("1198:0009", "far ptr DGROUP 0x2db4; drawn by 10e0:0ff4/1079 (frame DAT_5cd0 at DAT_5ccc,5cce)",
          "player-character sprite frames; w/h cached in DGROUP 0x5cd6/0x5d80"),
    301: ("10e8:0008", "far ptr DGROUP 0x5e54; used by status panel 10e8:0040/00dd/026e/03cb",
          "bottom status-panel art (entries 6, 9, ... indexed directly)"),
    302: ("1198:0085", "far ptr DGROUP 0x5e80", "UI art set (6 PMAPs)"),
    310: ("1178:02e1", "far ptr DGROUP 0x65c6", "scene object art set (9 PMAPs)"),
    311: ("1178:02cc", "1178:0041 slot 6 -> frame list DGROUP 0x6228+6*0x32 "
          "{u16 count; u16 1; u16 slot; far ptr frame[10]; far ptr bundle @+0x2e}",
          "animated object frame cycle"),
    312: ("1178:029f", "1178:0041 slot 2", "animated object frame cycle"),
    314: ("1178:02db", "1178:0041 slot 5", "animated object frame cycle"),
    315: ("1178:02ae", "1178:0041 slot 3", "animated object frame cycle"),
    316: ("1178:0281", "1178:0041 slot 0", "animated object frame cycle"),
    317: ("1178:0290", "1178:0041 slot 1", "animated object frame cycle"),
    320: ("1178:02bd", "1178:0041 slot 4", "animated object frame cycle"),
    321: ("1168:00c3", "far ptr DGROUP 0x6192", "art set (24 PMAPs)"),
    322: ("1160:0e97", "far ptr DGROUP 0x6166", "art set (4 PMAPs)"),
    323: ("10f0:0093", "10d8:0000 -> script ptr DGROUP 0x0f70, frames -> frame table slot 0..",
          "DAM animation: entry 0 = OTHR script 20300, entries 1.. = frames"),
    324: ("10f0:0196", "10d8:0000 -> script ptr 0x0f80", "DAM animation (script 20305)"),
    325: ("10f0:02a6", "10d8:0000 -> script ptr 0x0f84", "DAM animation (script 20308)"),
    326: ("1168:0182", "far ptr DGROUP 0x6202", "art set (4 PMAPs)"),
    327: ("1130:00cb", "1038:088a copy into scratch buffer DGROUP 0x3d8c; entry n drawn by 10b0:0023",
          "33 OTHR text pages (11500-11532)"),
    328: ("1198:009f", "far ptr DGROUP 0x2dd0", "art set (4 PMAPs)"),
    400: ("1138:000d", "10d8:0000 slot base 0 -> script ptr 0x5f13", "title DAM animation (script 20100)"),
    401: ("1138:0027", "10d8:0000 slot base = frames(400) -> script ptr 0x5f1d", "title DAM animation (script 20101)"),
    410: ("1188:0047", "10d8:0000 via id table DGROUP 0x2d6d[0]", "DAM animation (script 20200), then OTHR 20201"),
    412: ("1188:0047", "10d8:0000 via id table DGROUP 0x2d6d[1]", "DAM animation (script 20204), then OTHR 20205"),
    415: ("1188:01ed", "far ptr DGROUP 0x2d79", "art set (3 PMAPs)"),
}
for _k in range(43):   # 330 + k, k = item index mod 43 (10f0:00a9, 1190:013c, 1198:01ac)
    KNOWN_USES.setdefault(330 + _k, (
        "1190:0140/1198:01b0/10f0:00ae",
        "1190:009b: frames -> frame table DGROUP 0x5152 slots 0x2da8[i] (=20,30,..,70)+j; "
        "1198:0103: far ptr DGROUP 0x2dbc[i]",
        f"item art (2-4 frames), paired with OTHR DAM script {20000 + _k} by 1190:009b"))
for _k in range(43, 51):   # only reachable through the random picks of 1198:0103 (index < 51)
    KNOWN_USES.setdefault(330 + _k, (
        "1198:01b0 (index = rand in [DAT_65ce % 43, 51))",
        "far ptr DGROUP 0x2dbc[i] (i >= 1, or i = 0 when DAT_5e61 != 0)",
        "extra item art; never paired with a DAM script (20043-20050 unreferenced)"))
# Not referenced anywhere: no immediate in any code segment (raw scan incl. code outside the
# Ghidra functions) and no u16 copy in DGROUP.
UNREFERENCED = {313, 318, 319, 402}


class BndlError(ValueError):
    pass


def type_for_id(rid: int) -> str:
    """Mac resource type implied by a resource id (EXE 1038:038a)."""
    if rid < 900:
        return "BNDL"
    if rid < 950:
        return "RRGB"
    if rid < 1000:
        return "FFNT"
    if rid < 4000:
        return "PMAP"
    if rid < 10000:
        return "SSND"
    return "OTHR"


def dll_for_id(rid: int, colour256: bool = True) -> str | None:
    t = type_for_id(rid)
    if t == "PMAP":
        return "TMT256.DLL" if colour256 else "TMT16.DLL"
    if t == "SSND":
        for lo, hi, dll in SOUND_RANGES:
            if lo <= rid <= hi:
                return dll
        return None  # the EXE has no DLL for 6800-6999 / 8000-9999
    return "TMT.DLL"


def resource_path(res_dir: str, rid: int, colour256: bool = True) -> str | None:
    dll = dll_for_id(rid, colour256)
    if dll is None:
        return None
    return os.path.join(res_dir, dll, str(WIN_TYPE[type_for_id(rid)]), str(rid))


def parse_bndl(data: bytes) -> dict:
    """Parse a raw BNDL resource. Returns {'count': n, 'ids': [...], 'raw_ids': [...]}.

    'ids' are the low 16 bits (what the EXE uses); 'raw_ids' are the full u32 values.
    """
    if len(data) < 8 or data[:4] != MAGIC:
        raise BndlError("not a BNDL resource (bad magic)")
    count32 = struct.unpack_from("<I", data, 4)[0]
    count = count32 & 0xFFFF
    end = 8 + 4 * count
    if end > len(data):
        raise BndlError(f"count {count} overruns resource ({len(data)} bytes)")
    raw = list(struct.unpack_from(f"<{count}I", data, 8))
    trailing = data[end:]
    return {
        "count": count,
        "count_u32": count32,
        "ids": [r & 0xFFFF for r in raw],
        "raw_ids": raw,
        "payload_bytes": end,
        "padding_nonzero": any(trailing),
        # Bytes the EXE allocates when it loads this bundle (1038:00c9 doubles the
        # SizeofResource value for BNDL so the parallel size table fits).
        "inmem_needed": 8 + 8 * count,
    }


def parse_dam(data: bytes) -> dict | None:
    """Decode a 'DAM\\x1a' sprite-animation script (OTHR 20000-20308), as interpreted by
    EXE 10b8:0312.  Returns {'header', 'ticks': [[op,...],...], 'end'} or None.

    Opcodes (signed byte): FD dx:s16 dy:s16 frame:u8 flag:u8 = draw cel; FE = end of tick;
    FF = end of script; FA off:s16 cnt:u8 = loop (cnt&0x7f times, 0 = forever; bit7 selects
    counter 1; off is from the start of the script incl. header); FC pad:u8 n:u8 = play
    sound 7000+n; FB = stop sound; F9 x:s16 y:u8 w:s16 h:u8 = add rectangle.
    """
    if len(data) < 5 or data[:3] not in (b"DAM", b"DOP"):
        return None
    p = 4
    ticks, cur = [], []
    end = None
    while p < len(data):
        op = data[p]
        p += 1
        if op == 0xFE:
            ticks.append(cur)
            cur = []
        elif op == 0xFF:
            end = p - 1
            break
        elif op == 0xFD:
            dx, dy, fr, fl = struct.unpack_from("<hhBB", data, p)
            p += 6
            cur.append({"op": "cel", "dx": dx, "dy": dy, "frame": fr, "flag": fl})
        elif op == 0xFA:
            off, cnt = struct.unpack_from("<hB", data, p)
            p += 3
            cur.append({"op": "loop", "target": off, "count": cnt & 0x7F, "counter": cnt >> 7})
        elif op == 0xFC:
            cur.append({"op": "sound", "ssnd": 7000 + data[p + 1], "pad": data[p]})
            p += 2
        elif op == 0xFB:
            cur.append({"op": "stop_sound"})
        elif op == 0xF9:
            x, y, w, h = struct.unpack_from("<hBhB", data, p)
            p += 6
            cur.append({"op": "rect", "x": x, "y": y, "w": w + 2, "h": h + 2})
        else:
            cur.append({"op": "ignored", "byte": op})
    if cur:
        ticks.append(cur)
    frames = [c["frame"] for t in ticks for c in t if c["op"] == "cel"]
    return {"header": data[:4].decode("latin-1"), "end_offset": end, "n_ticks": len(ticks),
            "max_frame": max(frames) if frames else None, "ticks": ticks}


def load_bundle(res_dir: str, bid: int) -> dict:
    path = resource_path(res_dir, bid)
    with open(path, "rb") as f:
        return parse_bndl(f.read())


def describe_member(res_dir: str, rid: int) -> dict:
    t = type_for_id(rid)
    d = {"id": rid, "type": t}
    p256 = resource_path(res_dir, rid, True)
    d["dll"] = dll_for_id(rid, True)
    d["exists"] = bool(p256 and os.path.exists(p256))
    if d["exists"]:
        d["size"] = os.path.getsize(p256)
        if t == "PMAP":
            with open(p256, "rb") as f:
                hdr = f.read(4)
            if len(hdr) == 4:
                w, h = struct.unpack("<HH", hdr)
                d["pmap_w"], d["pmap_h"] = w, h
            p16 = resource_path(res_dir, rid, False)
            d["exists_16"] = os.path.exists(p16)
        elif t == "OTHR":
            with open(p256, "rb") as f:
                dam = parse_dam(f.read())
            if dam:
                d["dam_script"] = {k: dam[k] for k in ("header", "n_ticks", "max_frame", "end_offset")}
                d["dam_script"]["ticks"] = dam["ticks"]
    return d


def load_bundle_tree(res_dir: str, bid: int, _seen=None) -> dict:
    """Recursively resolve a bundle like the EXE's 1038:076f does."""
    _seen = set() if _seen is None else _seen
    b = load_bundle(res_dir, bid)
    out = {"id": bid, "count": b["count"], "members": []}
    if bid in _seen:
        out["cycle"] = True
        return out
    _seen = _seen | {bid}
    for rid in b["ids"]:
        m = describe_member(res_dir, rid)
        if m["type"] == "BNDL" and m["exists"]:
            m["bundle"] = load_bundle_tree(res_dir, rid, _seen)
        out["members"].append(m)
    return out


def all_bundle_ids(res_dir: str) -> list[int]:
    d = os.path.join(res_dir, "TMT.DLL", "32513")
    return sorted(int(n) for n in os.listdir(d) if n.isdigit())


def summarise(members: list[dict]) -> dict:
    s: dict[str, int] = {}
    for m in members:
        s[m["type"]] = s.get(m["type"], 0) + 1
    return s


def dump_all(res_dir: str, recursive: bool = False) -> dict:
    out = {"format": "BNDL", "source": "TMT.DLL type 32513", "bundles": []}
    for bid in all_bundle_ids(res_dir):
        b = load_bundle(res_dir, bid)
        members = [describe_member(res_dir, r) for r in b["ids"]]
        entry = {
            "id": bid,
            "count": b["count"],
            "ids": b["ids"],
            "types": summarise(members),
            "id_ranges": ranges(b["ids"]),
            "missing": [m["id"] for m in members if not m["exists"]],
            "members": members,
        }
        if any(r > 0xFFFF for r in b["raw_ids"]):
            entry["raw_ids"] = b["raw_ids"]
        if bid in KNOWN_USES:
            site, holder, role = KNOWN_USES[bid]
            entry["exe_use"] = {"call_site": site, "holder": holder, "role": role}
        elif bid in UNREFERENCED:
            entry["exe_use"] = None
            entry["note"] = "not referenced by the EXE (no code immediate, no DGROUP table entry)"
        if recursive:
            entry["tree"] = load_bundle_tree(res_dir, bid)
        out["bundles"].append(entry)
    return out


def ranges(ids: list[int]) -> list[str]:
    """Compress an id list into 'a-b' runs, preserving order."""
    out = []
    i = 0
    while i < len(ids):
        j = i
        while j + 1 < len(ids) and ids[j + 1] == ids[j] + 1:
            j += 1
        out.append(str(ids[i]) if i == j else f"{ids[i]}-{ids[j]}")
        i = j + 1
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("res_dir", help="extracted resource root (contains TMT.DLL/32513/...)")
    ap.add_argument("--id", type=int, help="print one bundle (recursively)")
    ap.add_argument("--json", help="write all bundles to this JSON file")
    ap.add_argument("--recursive", action="store_true", help="include nested trees in JSON")
    ap.add_argument("--dam", metavar="FILE", help="decode one DAM animation script file and exit")
    a = ap.parse_args(argv)
    if a.dam:
        with open(a.dam, "rb") as f:
            print(json.dumps(parse_dam(f.read()), indent=1))
        return 0
    if a.id is not None:
        print(json.dumps(load_bundle_tree(a.res_dir, a.id), indent=1))
        return 0
    data = dump_all(a.res_dir, a.recursive)
    if a.json:
        os.makedirs(os.path.dirname(os.path.abspath(a.json)), exist_ok=True)
        with open(a.json, "w") as f:
            json.dump(data, f, indent=1)
        print(f"wrote {len(data['bundles'])} bundles to {a.json}")
    else:
        for b in data["bundles"]:
            miss = f" missing={b['missing']}" if b["missing"] else ""
            print(f"BNDL {b['id']:4d} n={b['count']:3d} {b['types']} {' '.join(b['id_ranges'])}{miss}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
