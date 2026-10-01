#!/usr/bin/env python3
"""Locate FMOD's FSB5 parser and error strings inside a libunity.so (audit only)."""
import struct
import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else None
if not PATH:
    print("usage: find_fsb5_code.py <libunity.so>")
    sys.exit(2)
data = open(PATH, "rb").read()

TEXT_OFF, TEXT_SIZE = 0xbdbf0, 0xdf75dc
RODATA_OFF, RODATA_SIZE = 0xeb5200, 0x1963b0

# 1. ASCII "FSB5" literals anywhere.
print("== raw 'FSB5' occurrences ==")
i = 0
while True:
    i = data.find(b"FSB5", i)
    if i < 0:
        break
    where = "text" if TEXT_OFF <= i < TEXT_OFF + TEXT_SIZE else (
        "rodata" if RODATA_OFF <= i < RODATA_OFF + RODATA_SIZE else "other")
    print(f"  off=0x{i:x} {where}")
    i += 1

# 2. MOVZ/MOVK materialisation of the magic value 0x35425346 ("FSB5" LE).
MASK = 0x7FE0001F  # opcode bits that must match
MOVZ_W5346 = 0x52800000 | (0x5346 << 5)   # movz wN, #0x5346
MOVK_W3542 = 0x72A00000 | (0x3542 << 5)   # movk wN, #0x3542, lsl #16
print("== movz/movk materialisation in .text ==")
hits = []
for off in range(TEXT_OFF, TEXT_OFF + TEXT_SIZE - 4, 4):
    (w,) = struct.unpack_from("<I", data, off)
    if (w & MASK) == MOVZ_W5346 or (w & MASK) == MOVK_W3542:
        hits.append(off)
for off in hits:
    (w,) = struct.unpack_from("<I", data, off)
    kind = "MOVZ" if (w & MASK) == MOVZ_W5346 else "MOVK"
    reg = w & 0x1F
    print(f"  off=0x{off:x} {kind} w{reg}")
print(f"  ({len(hits)} hits)")

# 3. FMOD error strings: find "Error loading file" and the surrounding table.
needle = b"Error loading file"
idx = data.find(needle, RODATA_OFF, RODATA_OFF + RODATA_SIZE)
print("== error strings ==")
while idx != -1 and idx < RODATA_OFF + RODATA_SIZE:
    end = data.find(b"\0", idx)
    print(f"  str off=0x{idx:x}  -> {data[idx:end]!r}")
    idx = data.find(needle, end)
