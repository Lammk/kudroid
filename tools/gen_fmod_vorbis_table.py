#!/usr/bin/env python3
"""Dump FMOD's built-in Vorbis setup table out of a libunity.so.

FMOD keeps the Vorbis codebooks (the "setup" packets an FSB5 bank references by
CRC) in a table of 32-byte entries inside libunity.so. The lookup loop is
recognisable by its shape:

    ldur  w9, [x28, #-4]     ; entry->crc   (entry stride 0x20, x28 = entry+0x10)
    cmp   w9, w25            ; w25 = wanted crc
    b.eq  found
    add   x8, x8, #1
    cmp   x8, #<count>       ; 161 for Unity 2021
    add   x28, x28, #0x20
    b.cc  loop

The table base and count are decoded from that sequence, so nothing here is
tied to one Unity build. The entries hold pointers into .rodata; those regions
are copied verbatim into one blob and the pointers are rebased to it.

Output: the blob and the entry array, in the exact shape
src/platform/FmodVorbisFallback.cpp expects.
"""

import struct
import sys

ENTRY_SIZE = 32


def load_elf(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF":
        raise SystemExit("%s: not an ELF" % path)
    phoff = struct.unpack_from("<Q", data, 0x20)[0]
    phentsize = struct.unpack_from("<H", data, 0x36)[0]
    phnum = struct.unpack_from("<H", data, 0x38)[0]
    segs = []  # every PT_LOAD: address translation needs the data segments too
    exec_segs = []
    for i in range(phnum):
        off = phoff + i * phentsize
        p_type, p_flags = struct.unpack_from("<II", data, off)
        p_offset, p_vaddr, _paddr, p_filesz, _memsz, _align = struct.unpack_from(
            "<QQQQQQ", data, off + 8
        )
        if p_type != 1:
            continue
        segs.append((p_offset, p_vaddr, p_filesz))
        if p_flags & 1:
            exec_segs.append((p_offset, p_vaddr, p_filesz))
    return data, segs, exec_segs


def vaddr_to_file(segs, va):
    for p_offset, p_vaddr, p_filesz in segs:
        if p_vaddr <= va < p_vaddr + p_filesz:
            return p_offset + (va - p_vaddr)
    raise SystemExit("vaddr 0x%x is not in a file-backed segment" % va)


def find_table(data, segs):
    """Find the CRC lookup loop and decode its table base and entry count."""
    ldur = 0xB85FC389  # ldur w9, [x28, #-4]
    cmp_reg = 0x6B19013F  # cmp w9, w25
    for p_offset, p_vaddr, p_filesz in segs:
        for i in range(p_filesz // 4 - 8):
            words = struct.unpack_from("<8I", data, p_offset + 4 * i)
            if words[0] != ldur or words[1] != cmp_reg:
                continue
            if (words[2] & 0xFF000010) != 0x54000000:  # b.eq
                continue
            if (words[3] & 0xFFFFFC00) != 0x91000400:  # add x8, x8, #1
                continue
            if (words[4] & 0xFFC003FF) != 0xF100011F:  # cmp x8, #imm
                continue
            if words[5] != 0x9100839C:  # add x28, x28, #0x20
                continue
            if (words[6] & 0xFF000010) != 0x54000000:  # b.cond back
                continue
            count = (words[4] >> 10) & 0xFFF
            # The table pointer is set up just before the loop: adrp + add.
            base = None
            for k in range(1, 8):
                w = struct.unpack_from("<I", data, p_offset + 4 * (i - k))[0]
                if (w & 0x9F000000) == 0x90000000:  # adrp xN, #imm
                    immhi = (w >> 5) & 0x7FFFF
                    immlo = (w >> 29) & 0x3
                    imm = (immhi << 2) | immlo
                    if imm & (1 << 20):
                        imm -= 1 << 21
                    pc = p_vaddr + 4 * (i - k)
                    page = (pc & ~0xFFF) + (imm << 12)
                    rd = w & 0x1F
                    for j in range(k - 1, -1, -1):
                        w2 = struct.unpack_from("<I", data, p_offset + 4 * (i - j))[0]
                        if (w2 & 0xFF800000) == 0x91000000 and (w2 & 0x1F) == rd:
                            add_rd = (w2 >> 5) & 0x1F
                            if add_rd != rd:
                                continue
                            add_imm = (w2 >> 10) & 0xFFF
                            base = page + add_imm
                            break
                    if base is not None:
                        break
            if base is None:
                continue
            return base, count
    raise SystemExit("Vorbis setup table lookup loop not found")


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: %s <libunity.so> [out.c]" % sys.argv[0])
    data, segs, exec_segs = load_elf(sys.argv[1])
    table_va, count = find_table(data, exec_segs)
    table_start = table_va - 0x10
    print("table vaddr 0x%x count %d -> %d entries" % (table_start, count, count))
    entries = []
    need_end = 0
    for i in range(count):
        fo = vaddr_to_file(segs, table_start + i * ENTRY_SIZE)
        p1, len1, crc, p2, off2, len2 = struct.unpack_from("<QIIQii", data, fo)
        entries.append((p1, len1, crc, p2, off2, len2))
    # The blob keeps libunity's own layout for the setup regions: one flat copy
    # starting at the first entry's p1. Every pointer is rebased onto it, so
    # offsets are addr - blob_base. p1 is the short per-entry patch and p2 is
    # the shared setup template; len1 is the reconstructed setup length while
    # len2 is the patch length. Do not swap these lengths when computing the
    # source spans: p1+len2 and p2+len1 are the two actual source ranges.
    blob_base = min(e[0] for e in entries if e[0])
    for p1, len1, _crc, p2, _off2, len2 in entries:
        need_end = max(need_end, p1 - blob_base + len2)
        if p2:
            need_end = max(need_end, p2 - blob_base + len1)
    blob = data[vaddr_to_file(segs, blob_base):][:need_end]
    print("blob base 0x%x size %d" % (blob_base, len(blob)))

    out = sys.stdout if len(sys.argv) < 3 else open(sys.argv[2], "w")
    out.write("alignas(16) const uint8_t g_fmod_vorbis_data[%d] = {\n" % len(blob))
    for i in range(0, len(blob), 16):
        chunk = blob[i:i + 16]
        out.write("    " + " ".join("0x%02x," % b for b in chunk) + "\n")
    out.write("};\n\nalignas(16) FmodVorbisEntry g_fmod_entries[%d] = {\n" % count)
    for p1, len1, crc, p2, off2, len2 in entries:
        p1s = "g_fmod_vorbis_data + %d" % (p1 - blob_base) if p1 else "nullptr"
        p2s = "g_fmod_vorbis_data + %d" % (p2 - blob_base) if p2 else "nullptr"
        out.write("    { %s, %uu, 0x%08Xu, %s, %d, %d },\n" % (p1s, len1, crc, p2s, off2, len2))
    out.write("};\n")
    if out is not sys.stdout:
        out.close()


if __name__ == "__main__":
    main()
