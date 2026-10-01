#!/usr/bin/env python3
"""List code sites referencing chosen rodata strings in an arm64 ELF (audit only)."""
import struct
import sys

path = sys.argv[1]
targets = [0x1001171, 0x1001181, 0x1001400, 0x1001401, 0x1001430]
for t in sys.argv[2:]:
    targets.append(int(t, 0))

data = open(path, "rb").read()
TEXT_OFF, TEXT_SIZE = 0xbdbf0, 0xdf75dc

hits = {}
for off in range(TEXT_OFF, TEXT_OFF + TEXT_SIZE - 4, 4):
    (w,) = struct.unpack_from("<I", data, off)
    # ADRP
    if (w & 0x9F000000) == 0x90000000:
        immlo = (w >> 29) & 3
        immhi = (w >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & (1 << 20):
            imm -= 1 << 21
        page = (off & ~0xFFF) + (imm << 12)
        rd = w & 0x1F
        # look ahead up to 8 instructions for add/sub with same reg
        for k in range(1, 9):
            (w2,) = struct.unpack_from("<I", data, off + 4 * k)
            if (w2 & 0x7F000000) == 0x11000000 and ((w2 >> 5) & 0x1F) == rd:
                imm12 = (w2 >> 10) & 0xFFF
                sh = (w2 >> 22) & 3
                val = page + (imm12 << (12 * sh))
                for t in targets:
                    if val == t:
                        hits.setdefault(t, []).append((off, "adrp+add"))
    # LDR literal (64-bit)
    if (w & 0x3B000000) == 0x18000000:
        imm19 = (w >> 5) & 0x7FFFF
        if imm19 & (1 << 18):
            imm19 -= 1 << 19
        val = off + (imm19 << 2)
        for t in targets:
            if val == t:
                hits.setdefault(t, []).append((val and off, "ldr-literal->0x%x" % val))

for t in targets:
    sites = hits.get(t, [])
    print(f"0x{t:x}: {len(sites)} refs")
    for off, kind in sites:
        print(f"   site 0x{off:x} {kind}")
