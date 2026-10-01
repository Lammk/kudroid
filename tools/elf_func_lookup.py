#!/usr/bin/env python3
"""Map a libunity.so address to its function range (.eh_frame) and name PLT calls.

Audit helper for reading crash/trace return addresses: given `from=libunity.so+0x...`
it reports the enclosing function and its call targets.
"""
import bisect
import struct
import sys

path = sys.argv[1]
addrs = [int(a, 16) for a in sys.argv[2:]]
data = open(path, "rb").read()

EH_HDR = 0x104b5b0
FRAMES = 0x10b09f8
PLT = 0xbc650
RELA_PLT = 0xba608

# --- eh_frame_hdr table (version 1, sdata4|datarel) -------------------------
ver, eh_ptr_enc, fde_cnt_enc, table_enc = data[EH_HDR:EH_HDR + 4]
assert ver == 1 and table_enc == 0x3B, hex(table_enc)
# eh_frame_ptr is sdata4 pcrel (4 bytes), then fde_count sdata4/udata4 (3 enc)
count = struct.unpack_from("<i", data, EH_HDR + 8)[0]
table = EH_HDR + 12
starts = []
for i in range(count):
    rel = struct.unpack_from("<i", data, table + 8 * i)[0]
    starts.append(EH_HDR + rel)

# --- function ranges --------------------------------------------------------
# The table is sorted by initial_location, so the enclosing function is the last
# entry at or below the address; its end is the next entry's start.
def describe(addr):
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0:
        return None
    lo = starts[i]
    hi = starts[i + 1] if i + 1 < count else addr + 1
    return lo, hi - lo

# --- PLT stub -> imported symbol name ---------------------------------------
dynsym_off = 0xc10
dynsym_size = 0x2118
dynstr_off = 0x2d28
rela_count = 0x2040 // 24
plt_names = {}
for i in range(rela_count):
    off = RELA_PLT + 24 * i
    r_info = struct.unpack_from("<Q", data, off + 8)[0]
    symidx = r_info >> 32
    s_off, s_size, s_info, s_other, s_shndx, s_value = struct.unpack_from(
        "<IBBHQQ", data, dynsym_off + 24 * symidx)
    name_end = data.find(b"\0", dynstr_off + s_off)
    plt_names[PLT + 0x20 + 16 * i] = data[dynstr_off + s_off:name_end].decode()

if addrs and addrs[0] == 0xD0:  # --dump lo-hi
    lo, hi = addrs[1], addrs[2]
    i = bisect.bisect_right(starts, lo) - 1
    while i < count and starts[i] < hi:
        nxt = starts[i + 1] if i + 1 < count else starts[i]
        print(f"func 0x{starts[i]:x}..0x{nxt:x} (0x{nxt - starts[i]:x})")
        i += 1
    sys.exit(0)

for a in addrs:
    r = describe(a)
    if r is None:
        print(f"0x{a:x}: no FDE")
        continue
    lo, rng = r
    print(f"0x{a:x}: func 0x{lo:x}..0x{lo + rng:x} (size 0x{rng:x}, +0x{a - lo:x})")

# dump the PLT map for callers that want to annotate
if len(sys.argv) > 2 and sys.argv[2] == "--plt":
    for k in sorted(plt_names):
        print(f"plt 0x{k:x} {plt_names[k]}")
