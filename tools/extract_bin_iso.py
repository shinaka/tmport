"""Extract files from a MODE1/2352 BIN CD image containing an ISO9660 filesystem.

Usage: python extract_bin_iso.py CD.BIN OUTDIR
"""
import os
import struct
import sys

SECTOR_RAW = 2352
USER_OFF = 16
USER_LEN = 2048


class Bin:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.f.seek(0, 2)
        self.nsect = self.f.tell() // SECTOR_RAW

    def read(self, lba, nbytes):
        out = bytearray()
        while len(out) < nbytes:
            self.f.seek(lba * SECTOR_RAW + USER_OFF)
            out += self.f.read(USER_LEN)
            lba += 1
        return bytes(out[:nbytes])


def walk(img, lba, size, path, out, listing):
    data = img.read(lba, size)
    p = 0
    while p < len(data):
        rlen = data[p]
        if rlen == 0:
            p = (p // USER_LEN + 1) * USER_LEN
            continue
        ext_lba = struct.unpack_from('<I', data, p + 2)[0]
        ext_len = struct.unpack_from('<I', data, p + 10)[0]
        flags = data[p + 25]
        nlen = data[p + 32]
        name = data[p + 33:p + 33 + nlen]
        p += rlen
        if name in (b'\x00', b'\x01'):
            continue
        name = name.decode('latin-1').split(';')[0].rstrip('.')
        full = os.path.join(path, name)
        if flags & 2:
            listing.append((full + '/', ext_len, ext_lba))
            os.makedirs(os.path.join(out, full), exist_ok=True)
            walk(img, ext_lba, ext_len, full, out, listing)
        else:
            listing.append((full, ext_len, ext_lba))
            with open(os.path.join(out, full), 'wb') as f:
                f.write(img.read(ext_lba, ext_len))


if __name__ == '__main__':
    img = Bin(sys.argv[1])
    out = sys.argv[2]
    pvd = img.read(16, 2048)
    assert pvd[1:6] == b'CD001', pvd[:8]
    print('volume id:', pvd[40:72].decode('latin-1').strip())
    root = pvd[156:190]
    rlba = struct.unpack_from('<I', root, 2)[0]
    rlen = struct.unpack_from('<I', root, 10)[0]
    os.makedirs(out, exist_ok=True)
    listing = []
    walk(img, rlba, rlen, '', out, listing)
    for name, ln, lba in listing:
        print(f'{ln:>10} {lba:>7} {name}')
