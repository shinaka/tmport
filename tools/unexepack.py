"""Unpack a Microsoft EXEPACK-compressed DOS MZ executable.

Usage: python unexepack.py IN.EXE OUT.EXE
"""
import struct
import sys


def unpack(data: bytes) -> bytes:
    (sig, last_page, pages, nrel, hdr_paras, min_alloc, max_alloc,
     ss, sp, csum, ip, cs, reloc_off, ovl) = struct.unpack_from('<2s13H', data, 0)
    assert sig == b'MZ'
    file_len = (pages - 1) * 512 + (last_page or 512)
    hdr_len = hdr_paras * 16
    image = data[hdr_len:file_len]
    xp = cs * 16  # EXEPACK block offset within the load image
    (real_ip, real_cs, mem_start, xp_size, real_sp, real_ss,
     dest_len, skip_len, xsig) = struct.unpack_from('<8H2s', image, xp)
    assert xsig == b'RB', 'not EXEPACK'
    hdr_size = 18
    # Locate relocation table: follows the "Packed file is corrupt" message.
    msg = b'Packed file is corrupt'
    m = image.find(msg, xp)
    assert m > 0
    reloc_pos = m + len(msg)

    # Decompress backwards. The compressed stream occupies image[0:xp - (skip_len-1)*16].
    src = bytearray(image[:xp])
    dst = bytearray(dest_len * 16)
    s = len(src) - (skip_len - 1) * 16
    while s > 0 and src[s - 1] == 0xFF:
        s -= 1
    d = len(dst)
    # Buffer must also contain original source (decompression happens in place).
    dst[:len(src)] = src
    while True:
        cmd = dst[s - 1] if False else src[s - 1]
        s -= 1
        length = src[s - 2] | (src[s - 1] << 8)
        s -= 2
        op = cmd & 0xFE
        if op == 0xB0:
            val = src[s - 1]
            s -= 1
            for _ in range(length):
                d -= 1
                dst[d] = val
        elif op == 0xB2:
            for _ in range(length):
                d -= 1
                s -= 1
                dst[d] = src[s]
        else:
            raise ValueError(f'bad EXEPACK command {cmd:#x} at {s:#x}')
        if cmd & 1:
            break
    # Remaining bytes [0:s] are the uncompressed prefix; already copied to dst.
    out_image = bytes(dst)

    # Relocations: 16 groups (segment 0x0000..0xF000), each a count + offsets.
    relocs = []
    p = reloc_pos
    for seg in range(16):
        n = struct.unpack_from('<H', image, p)[0]
        p += 2
        for _ in range(n):
            off = struct.unpack_from('<H', image, p)[0]
            p += 2
            relocs.append((off, seg * 0x1000))

    # Build a new MZ header.
    new_hdr_size = 28 + 4 * len(relocs)
    new_hdr_paras = (new_hdr_size + 15) // 16
    new_hdr_len = new_hdr_paras * 16
    total = new_hdr_len + len(out_image)
    new_pages = (total + 511) // 512
    new_last = total % 512
    extra_paras = (dest_len * 16 - len(out_image)) // 16  # zero
    # Preserve the original memory requirement (BSS/stack) beyond the packed image.
    orig_mem = len(image) // 16 + min_alloc
    new_min = max(0, orig_mem - len(out_image) // 16)
    hdr = struct.pack('<2s13H', b'MZ', new_last, new_pages, len(relocs), new_hdr_paras,
                      new_min, max_alloc, real_ss, real_sp, 0, real_ip, real_cs, 28, 0)
    hdr += b''.join(struct.pack('<HH', o, s) for o, s in relocs)
    hdr += b'\0' * (new_hdr_len - len(hdr))
    return hdr + out_image, relocs, (real_cs, real_ip, real_ss, real_sp)


if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    out, relocs, regs = unpack(data)
    open(sys.argv[2], 'wb').write(out)
    print(f'unpacked {len(out)} bytes, {len(relocs)} relocations, '
          f'CS:IP={regs[0]:04x}:{regs[1]:04x} SS:SP={regs[2]:04x}:{regs[3]:04x}')
