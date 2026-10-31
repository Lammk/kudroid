#!/usr/bin/env python3
"""Check a pinned engine candidate before device validation."""

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
from pathlib import Path


SNAPSHOT_HASH = "830f4f59e7969c70b595182826435c19"
REFERENCE_BUILD_ID = "fb5eaead4cf4256da528a8ebbd653cdf89ec99bc"
FEATURES = ("product no-code_comments no-dwarf_stack_traces_mode "
            "dedup_instructions no-tsan no-msan arm64 android compressed-pointers")
REVIEWED_IMPORTS = {
    "__vsprintf_chk": "bionic___vsprintf_chk",
    "vprintf": "bionic_vprintf",
    "exit": "host C library process exit",
}


def readelf(path, option):
    return subprocess.run(
        ["readelf", option, "--wide", str(path)], check=True,
        capture_output=True, text=True, timeout=30,
        env={**os.environ, "LC_ALL": "C"},
    ).stdout


def jni_signatures(data):
    jni_type = rb"(?:\[*[ZBCSIJFD]|\[*L[^;\x00]+;)"
    descriptor = rb"(?:^|\x00)(\(" + jni_type + rb"*\)(?:V|" + jni_type + rb"))(?=\x00)"
    return set(re.findall(descriptor, data))


def inspect(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError(f"{path}: expected little-endian ELF64")
    kind, machine = struct.unpack_from("<HH", data, 16)
    if kind != 3 or machine != 183:
        raise ValueError(f"{path}: expected ARM64 shared library")
    symbols = {}
    exports = set()
    imports = set()
    for line in readelf(path, "--dyn-syms").splitlines():
        parts = line.split()
        if len(parts) < 8 or not parts[0].rstrip(":").isdigit():
            continue
        if parts[6] != "UND":
            name = parts[7].split("@", 1)[0]
            symbols[name] = (int(parts[1], 16), int(parts[2], 0))
            if parts[4] in ("GLOBAL", "WEAK") and parts[5] in ("DEFAULT", "PROTECTED"):
                exports.add(name)
        else:
            imports.add(parts[7].split("@", 1)[0])
    dependencies = set(re.findall(r"\(NEEDED\).*\[([^\]]+)\]", readelf(path, "--dynamic")))
    build_id = re.search(r"Build ID: ([0-9a-f]+)", readelf(path, "--notes"))
    return {
        "data": data, "symbols": symbols, "exports": exports, "imports": imports,
        "dependencies": dependencies, "jni_signatures": jni_signatures(data),
        "build_id": build_id.group(1) if build_id else None,
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def snapshot_features(elf, symbol):
    data = elf["data"]
    address, size = elf["symbols"][symbol]
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phsize, phcount = struct.unpack_from("<HH", data, 54)
    if phsize < 56 or phoff + phsize * phcount > len(data):
        raise ValueError("Invalid ELF program headers")
    for index in range(phcount):
        kind, _, offset, vaddr, _, filesz, _, _ = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + index * phsize)
        if kind != 1 or address < vaddr or address - vaddr + size > filesz:
            continue
        start = offset + address - vaddr
        if size < 53 or start + size > len(data):
            raise ValueError("Invalid snapshot extent")
        snapshot = data[start:start + size]
        magic, length, snapshot_kind = struct.unpack_from("<Iqq", snapshot)
        if magic != 0xdcdcf5f5 or snapshot_kind != 3 or not 53 <= length + 4 <= size:
            raise ValueError("Expected a full AOT snapshot")
        version = snapshot[20:52].decode("ascii")
        end = snapshot.find(b"\x00", 52, length + 4)
        if end == -1:
            raise ValueError("Unterminated snapshot features")
        return version, snapshot[52:end].decode("ascii")
    raise ValueError("Snapshot symbol is outside ELF file mappings")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    args = parser.parse_args()
    reference = inspect(args.reference)
    candidate = inspect(args.candidate)
    app = inspect(args.snapshot)
    errors = []
    if reference["build_id"] != REFERENCE_BUILD_ID:
        errors.append("Reference is outside the pinned engine family")
    for name in ("_kDartVmSnapshotData", "_kDartIsolateSnapshotData"):
        version, features = snapshot_features(app, name)
        if version != SNAPSHOT_HASH or features != FEATURES:
            errors.append(f"{name}: snapshot version or features differ")
    for label, engine in (("reference", reference), ("candidate", candidate)):
        if SNAPSHOT_HASH.encode() + b"\x00" not in engine["data"]:
            errors.append(f"{label}: snapshot hash missing")
        if b"3.8.1 (stable)" not in engine["data"] or b'"android_arm64"' not in engine["data"]:
            errors.append(f"{label}: Dart platform/version differs")
        if "JNI_OnLoad" not in engine["exports"]:
            errors.append(f"{label}: JNI_OnLoad missing")
    if b"[KuDroidFlutterVM] compressed heap base=" not in candidate["data"]:
        errors.append("Candidate reservation diagnostic missing")
    missing_exports = sorted(reference["exports"] - candidate["exports"])
    extra_dependencies = sorted(candidate["dependencies"] - reference["dependencies"])
    extra_imports = sorted(candidate["imports"] - reference["imports"])
    unreviewed_imports = sorted(set(extra_imports) - REVIEWED_IMPORTS.keys())
    missing_signatures = reference["jni_signatures"] - candidate["jni_signatures"]
    if missing_exports:
        errors.append("Candidate removes exported symbols")
    if extra_dependencies:
        errors.append("Candidate adds shared-library dependencies")
    if unreviewed_imports:
        errors.append("Candidate adds imported symbols requiring shim review")
    if missing_signatures:
        errors.append("Candidate removes JNI signature strings")
    print(json.dumps({
        "candidate_sha256": candidate["sha256"],
        "candidate_build_id": candidate["build_id"],
        "snapshot_hash": SNAPSHOT_HASH, "snapshot_features": FEATURES,
        "missing_exports": missing_exports, "extra_dependencies": extra_dependencies,
        "extra_imports": extra_imports,
        "reviewed_extra_imports": {name: REVIEWED_IMPORTS[name] for name in extra_imports
                                   if name in REVIEWED_IMPORTS},
        "unreviewed_imports": unreviewed_imports,
        "missing_jni_signatures": sorted(s.decode("ascii", "replace") for s in missing_signatures),
        "preflight_passed": not errors, "device_verified": False,
        "errors": errors,
    }, indent=2))
    return bool(errors)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, struct.error, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
