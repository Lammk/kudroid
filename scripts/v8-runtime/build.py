#!/usr/bin/env python3
"""Build the source-pinned Android allocator adapter and audit its imports."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

VERSION = "9.4.146.24"
HEADERS = {
    "include/v8-platform.h": "7398d2879534f2986b59efbda90178580e8b9a7ab803b716405fa9aad0a4f64a",
    "include/v8-internal.h": "926de5f50088bb013d4c0248371f7b700974494993c8ed5340c8f774e2723782",
    "src/utils/allocation.h": "b97c7c182c546a10e3d0cb058044b45310f93b18fe091255efb5c781748afe09",
    "src/base/bounded-page-allocator.h": "872739641e321789153cf2ada02165e9d6035370d2bcf004d8f6e4e990fc1949",
}
IMPORTS = {
    "_ZN2v84base20BoundedPageAllocatorC1EPNS_13PageAllocatorEmmm",
    "_ZN2v88internal13VirtualMemory5ResetEv",
    "_ZN2v88internal13VirtualMemoryD1Ev", "_Znwm", "_ZdlPv",
}
EXPORTS = {
    "_ZN2v88internal17VirtualMemoryCage15InitReservationERKNS1_17ReservationParamsE",
    "kudroid_v8_cage_configure",
    "kudroid_v8_cage_requirements",
}

def requirements(elf):
    data = elf.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        raise SystemExit("Requirements need a little-endian ELF64 image")
    table = struct.unpack_from("<Q", data, 40)[0]
    stride, count = struct.unpack_from("<HH", data, 58)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, table + i * stride)
                for i in range(count)]
    for section in sections:
        if section[1] != 11:
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, info, visibility, index, value, size = struct.unpack_from("<IBBHQQ", data, offset)
            if names[name:].split(b"\0", 1)[0] != b"kudroid_v8_cage_requirements":
                continue
            if info & 15 != 1 or visibility & 3 or size != 24 or not 0 < index < count:
                raise SystemExit("Invalid compiled requirements symbol")
            owner = sections[index]
            relative = value - owner[3]
            if owner[1] == 8 or relative < 0 or relative + size > owner[5]:
                raise SystemExit("Requirements are outside file-backed data")
            version, length, alignment = struct.unpack_from("<QQQ", data, owner[4] + relative)
            if version != 1 or not length or not alignment or alignment & (alignment - 1):
                raise SystemExit("Invalid source requirements")
            return length, alignment
    raise SystemExit("Compiled requirements symbol missing")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output or root / "ios-app/KuDroidShell/Resources/runtime-adapters"
    for header, expected in HEADERS.items():
        if hashlib.sha256((args.source / header).read_bytes()).hexdigest() != expected:
            raise SystemExit(f"Unsupported source header: {header}")
    compiler = args.ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++"
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="v8-cage-") as temporary:
        candidate = Path(temporary) / f"v8-{VERSION}.so"
        subprocess.run([
            str(compiler), "-std=c++17", "-O2", "-DNDEBUG", "-DV8_COMPRESS_POINTERS",
            "-DV8_COMPRESS_POINTERS_IN_SHARED_CAGE", "-fPIC", "-fno-exceptions", "-fno-rtti",
            "-fvisibility=hidden", "-ffunction-sections", "-fdata-sections", "-nostdlib", "-shared",
            "-I", str(root / "include"), "-I", str(args.source),
            str(root / "runtime-adapters/v8/cage-9.4.cc"), "-Wl,--gc-sections,-z,now", "-o", str(candidate),
        ], check=True, timeout=120)
        header = subprocess.run(["readelf", "-hW", str(candidate)], check=True,
                                capture_output=True, text=True, timeout=20).stdout
        if "AArch64" not in header:
            raise SystemExit("Adapter target is not AArch64")
        symbols = subprocess.run(["readelf", "--dyn-syms", "-W", str(candidate)], check=True,
                                 capture_output=True, text=True, timeout=20).stdout
        imports, exports = set(), set()
        for line in symbols.splitlines():
            fields = line.split()
            if len(fields) < 8 or not fields[0].endswith(":") or not fields[0][:-1].isdigit():
                continue
            (imports if fields[6] == "UND" else exports).add(fields[7])
        if imports != IMPORTS or exports != EXPORTS:
            raise SystemExit(f"Unexpected ABI: imports={sorted(imports)} exports={sorted(exports)}")
        target = output / candidate.name
        shutil.copyfile(candidate, target)
    length, alignment = requirements(target)
    record = {
        "v8_version": VERSION, "file": target.name,
        "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
        "api_version": 1, "source": f"https://github.com/v8/v8/tree/{VERSION}",
        "early_length": length, "early_alignment": alignment,
        "source_headers": HEADERS, "imports": sorted(IMPORTS), "exports": sorted(EXPORTS),
        "device_verified": False,
    }
    (output / f"v8-{VERSION}.json").write_text(json.dumps(record, indent=2) + "\n")
    shutil.copyfile(args.source / "LICENSE", output / f"V8-{VERSION}-LICENSE.txt")
    print(f"Built and verified {target}: {target.stat().st_size} bytes, SHA256 {record['sha256']}")

if __name__ == "__main__":
    main()
