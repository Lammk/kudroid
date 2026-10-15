#!/usr/bin/env python3
"""Compare the FSB5 hex prefixes KuDroid served (stderr.log) with the real APK bytes.

Audit tool: proves whether the VFS hands FMOD correct file content at every
detected blob, or whether some reads straddle/miss the intended file region.
"""
import re
import struct
import sys
import zipfile

LOG = sys.argv[1] if len(sys.argv) > 1 else "logs/stderr.log"
APK = sys.argv[2] if len(sys.argv) > 2 else (
    "/home/kuzei/.local/share/waydroid/data/app/"
    "~~k2AkHU11A18MrBehH9HZ-g==/com.aarongamingdev.ULTRAKILL-exn_63BasKZDEPOBBHF7Pg==/base.apk")

served = re.compile(
    r"served FSB5 sid=(?P<sid>\d+) at apk_off=(?P<off>\d+) size=(?P<size>\d+) "
    r"hits=(?P<hits>\d+) seen=(?P<seen>\d+) method=(?P<method>\d+) usize=(?P<usize>\d+) "
    r"entryOff=(?P<entryoff>\d+) entryRem=(?P<entryrem>\d+) ver=(?P<ver>\d+) "
    r"num=(?P<num>\d+) shs=(?P<shs>\d+) nts=(?P<nts>\d+) dataSize=(?P<data>\d+) "
    r"mode=(?P<mode>\d+) blobTotal=(?P<total>\d+) next=\[(?P<next>[^\]]*)\] "
    r"hex=(?P<hex>[0-9a-f]+).*?entry=(?P<entry>\S+)")

rows = []
with open(LOG, "r", errors="replace") as fh:
    for line in fh:
        m = served.search(line)
        if m:
            rows.append(m.groupdict())

print(f"served FSB5 records: {len(rows)}")

# The device path is the same layout, but KuDroid lowercases names for lookup.
zf = zipfile.ZipFile(APK)
byname = {n.lower(): n for n in zf.namelist()}

def payload_offset(info):
    zf.fp.seek(info.header_offset)
    lh = zf.fp.read(30)
    nlen, elen = struct.unpack_from("<HH", lh, 26)
    return info.header_offset + 30 + nlen + elen

offsets = {}
for name in {r["entry"] for r in rows}:
    real = byname.get(name.lower())
    if real is None:
        print(f"!! entry not in this APK: {name}")
        offsets[name] = None
        continue
    info = zf.getinfo(real)
    offsets[name] = payload_offset(info)
    print(f"entry {real}: payload off {offsets[name]} size {info.file_size} method {info.compress_type}")

bad = match = skipped = 0
for r in rows:
    base = offsets.get(r["entry"])
    if base is None:
        skipped += 1
        continue
    want = bytes.fromhex(r["hex"])
    if int(r["usize"]) != zf.getinfo(byname[r["entry"].lower()]).file_size:
        skipped += 1
        print(f"SKIP {r['entry']}: device usize={r['usize']} host size="
              f"{zf.getinfo(byname[r['entry'].lower()]).file_size} (different build)")
        continue
    with open(APK, "rb") as apk:
        apk.seek(base + int(r["entryoff"]))
        got = apk.read(len(want))
    if got == want:
        match += 1
    else:
        bad += 1
        # first differing byte, for a compact report
        d = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
        print(f"MISMATCH entry={r['entry']} entryOff={r['entryoff']} apk_off={r['off']} "
              f"dev_hex={r['hex'][:48]} host={got[:24].hex()} first_diff={d}")

print(f"identical={match} mismatched={bad} skipped={skipped}")
